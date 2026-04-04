#include "AudioDeviceSDL.h"
#include "Misc.h"
#include <algorithm>
#include <cstring>

SdlAudioDevice::SdlAudioDevice(std::unique_ptr<AudioSource> in_source)
	: audio_source(std::move(in_source))
{
	// Each packet holds ~100 ms of stereo 16-bit audio
	size_t packet_bytes = (size_t)(0.1 * AudioSource::SAMPLE_RATE)
	                    * AudioSource::NUM_CHANNELS
	                    * (AudioSource::SAMPLE_DEPTH / 8);

	for (Packet& p : packets)
	{
		p.buffer_size = packet_bytes;
		p.buffer      = std::make_unique<uint8[]>(packet_bytes);
		p.stream_pos  = 0;
	}
}

SdlAudioDevice::~SdlAudioDevice()
{
	Destroy();
}

// ---------------------------------------------------------------------------

Error* SdlAudioDevice::Create()
{
	SDL_AudioSpec want{}, got{};
	want.freq     = AudioSource::SAMPLE_RATE;
	want.format   = AUDIO_S16SYS;
	want.channels = (Uint8)AudioSource::NUM_CHANNELS;
	want.samples  = 4096;
	want.callback = AudioCallback;
	want.userdata = this;

	device_id = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
	if (device_id == 0)
		return new Error(std::string("SDL_OpenAudioDevice: ") + SDL_GetError());

	SDL_PauseAudioDevice(device_id, 0); // start playing
	return nullptr;
}

Error* SdlAudioDevice::Destroy()
{
	if (device_id)
	{
		SDL_CloseAudioDevice(device_id);
		device_id = 0;
	}
	return nullptr;
}

Error* SdlAudioDevice::Tick(float /*elapsed*/)
{
	// Nothing to do; SDL drives the callback on its own thread.
	return nullptr;
}

// ---------------------------------------------------------------------------
// AudioSource::Read — returns what is currently being played back
// ---------------------------------------------------------------------------

void SdlAudioDevice::Read(void* read_data, size_t read_size)
{
	std::lock_guard<std::mutex> lock(mutex);

	int packet_idx = next_packet;
	for (int i = 0; i < NUM_PACKETS; i++)
	{
		const Packet& pkt = packets[packet_idx];

		if (read_pos < pkt.stream_pos)
			read_pos = pkt.stream_pos;

		size_t ofs = read_pos - pkt.stream_pos;
		if (ofs < pkt.buffer_size)
		{
			size_t chunk = std::min(read_size, pkt.buffer_size - ofs);
			memcpy(read_data, pkt.buffer.get() + ofs, chunk);
			read_pos  += chunk;
			read_size -= chunk;
			read_data  = (uint8*)read_data + chunk;
			if (read_size == 0) break;
		}

		if (++packet_idx == NUM_PACKETS)
			packet_idx = 0;
	}
}

// ---------------------------------------------------------------------------
// SDL audio callback — called from the SDL audio thread
// ---------------------------------------------------------------------------

void SDLCALL SdlAudioDevice::AudioCallback(void* userdata, Uint8* stream, int len)
{
	SdlAudioDevice* self = (SdlAudioDevice*)userdata;
	std::lock_guard<std::mutex> lock(self->mutex);

	// Fill current packet with fresh audio from the source
	Packet& pkt = self->packets[self->next_packet];
	pkt.stream_pos = self->stream_pos;

	size_t to_fill = (size_t)len;
	if (to_fill > pkt.buffer_size)
		to_fill = pkt.buffer_size;

	self->audio_source->Read(pkt.buffer.get(), to_fill);
	memcpy(stream, pkt.buffer.get(), to_fill);

	// Zero-fill any remainder
	if ((size_t)len > to_fill)
		memset(stream + to_fill, 0, (size_t)len - to_fill);

	self->stream_pos += to_fill;

	if (++self->next_packet == NUM_PACKETS)
		self->next_packet = 0;
}
