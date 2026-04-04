#pragma once

#include "AudioDevice.h"
#include "AudioSource.h"
#include "Error.h"
#include <SDL2/SDL.h>
#include <memory>
#include <mutex>

// SDL2-based audio device: plays audio from an AudioSource and also
// exposes AudioSource::Read() so Tripex can read back what is playing.
class SdlAudioDevice : public AudioDevice, public AudioSource
{
public:
	explicit SdlAudioDevice(std::unique_ptr<AudioSource> in_source);
	~SdlAudioDevice();

	virtual Error* Create() override;
	virtual Error* Destroy() override;
	virtual Error* Tick(float elapsed) override;

	// AudioSource – lets Tripex read back the currently-playing audio
	virtual void Read(void* read_data, size_t read_size) override;

private:
	static const int NUM_PACKETS = 4;

	struct Packet
	{
		size_t stream_pos = 0;
		std::unique_ptr<uint8[]> buffer;
		size_t buffer_size = 0;
	};

	SDL_AudioDeviceID device_id = 0;
	std::unique_ptr<AudioSource> audio_source;

	size_t stream_pos = 0;
	size_t read_pos   = 0;
	int    next_packet = 0;
	Packet packets[NUM_PACKETS];

	std::mutex mutex;

	static void SDLCALL AudioCallback(void* userdata, Uint8* stream, int len);
};
