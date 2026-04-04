#include "Platform.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "Tripex.h"
#include "RendererOpenGL.h"
#include "AudioDeviceSDL.h"
#include "AudioSource.h"
#include "AudioData.h"
#include "Error.h"
#include "Misc.h"

// ---------------------------------------------------------------------------
// macOS native file dialog via osascript
// ---------------------------------------------------------------------------
static std::string OpenAudioFileDialog()
{
	FILE* f = popen(
	    "osascript -e '"
	    "POSIX path of (choose file with prompt \"Select a WAV or MP3 file\" "
	    "of type {\"com.microsoft.waveform-audio\", \"public.mp3\", \"public.audio\"})"
	    "' 2>/dev/null",
	    "r");
	if (!f) return {};

	char buf[2048] = {};
	if (fgets(buf, sizeof(buf), f))
	{
		size_t len = strlen(buf);
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';
	}
	pclose(f);
	return std::string(buf);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------
static void ShowError(SDL_Window* win, const char* title, const Error* err)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title,
	                         err->GetDescription().c_str(), win);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int /*argc*/, char* /*argv*/[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Init",
		                         SDL_GetError(), nullptr);
		return 1;
	}

	// Request OpenGL 3.2 Core Profile
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   16);

	SDL_Window* window = SDL_CreateWindow(
	    "Tripex",
	    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	    800, 600,
	    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Window",
		                         SDL_GetError(), nullptr);
		SDL_Quit();
		return 1;
	}

	SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
	if (!gl_ctx)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GL Context",
		                         SDL_GetError(), window);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	SDL_GL_SetSwapInterval(1); // vsync

	int win_w, win_h;
	SDL_GetWindowSize(window, &win_w, &win_h);

	// Create renderer
	auto renderer = std::make_shared<RendererOpenGL>();
	{
		Error* err = renderer->Open(window, gl_ctx, win_w, win_h);
		if (err)
		{
			ShowError(window, "Renderer", err);
			delete err;
			SDL_GL_DeleteContext(gl_ctx);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 1;
		}
	}

	// Create Tripex
	auto tripex = std::make_shared<Tripex>(renderer);
	{
		Error* err = tripex->Startup();
		if (err)
		{
			ShowError(window, "Tripex Startup", err);
			delete err;
			renderer->Close();
			SDL_GL_DeleteContext(gl_ctx);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 1;
		}
	}

	// Audio state
	std::shared_ptr<AudioDevice> audio_device;
	std::shared_ptr<AudioSource> audio_source = std::make_shared<RandomAudioSource>();

	// Main loop
	bool running = true;
	while (running)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			switch (ev.type)
			{
			case SDL_QUIT:
				running = false;
				break;

			case SDL_WINDOWEVENT:
				if (ev.window.event == SDL_WINDOWEVENT_RESIZED)
				{
					SDL_GetWindowSize(window, &win_w, &win_h);
					renderer->Resize(win_w, win_h);
				}
				break;

			case SDL_KEYDOWN:
				switch (ev.key.keysym.sym)
				{
				case SDLK_ESCAPE:
					running = false;
					break;

				case SDLK_F1:
					tripex->ToggleHelp();
					break;

				case SDLK_F2:
					tripex->ToggleAudioInfo();
					break;

				case SDLK_LEFT:
					tripex->MoveToPrevEffect();
					break;

				case SDLK_RIGHT:
					tripex->MoveToNextEffect();
					break;

				case SDLK_r:
					tripex->ReconfigureEffect();
					break;

				case SDLK_e:
					tripex->ChangeEffect();
					break;

				case SDLK_h:
					tripex->ToggleHoldingEffect();
					break;

				// M — switch to random (mock) audio
				case SDLK_m:
					audio_device.reset();
					audio_source = std::make_shared<RandomAudioSource>();
					break;

				// O — open WAV or MP3 file
				case SDLK_o:
				{
					std::string path = OpenAudioFileDialog();
					if (!path.empty())
					{
						// Determine format by extension
						bool is_mp3 = path.size() >= 4 &&
						              strcasecmp(path.c_str() + path.size() - 4, ".mp3") == 0;

						std::unique_ptr<MemoryAudioSource> src;
						Error* err = is_mp3
						    ? MemoryAudioSource::CreateFromMp3File(path.c_str(), src)
						    : MemoryAudioSource::CreateFromWavFile(path.c_str(), src);

						if (err)
						{
							ShowError(window, "Open Audio", err);
							delete err;
						}
						else
						{
							auto dev = std::make_shared<SdlAudioDevice>(std::move(src));
							err = dev->Create();
							if (err)
							{
								ShowError(window, "Audio Device", err);
								delete err;
							}
							else
							{
								audio_device = dev;
								audio_source = dev;
							}
						}
					}
					break;
				}

				// T — test tone (alternating 256 Hz / 512 Hz)
				case SDLK_t:
				{
					const int length      = 20; // seconds
					const int sample_rate = AudioSource::SAMPLE_RATE;
					const int num_ch      = AudioSource::NUM_CHANNELS;
					const size_t num_blocks  = (size_t)length * sample_rate;
					const size_t buffer_size = num_blocks * num_ch * sizeof(int16);

					auto buffer = std::shared_ptr<uint8[]>(new uint8[buffer_size]);
					int16* out  = (int16*)buffer.get();

					for (size_t i = 0; i < num_blocks; i++)
					{
						float t    = (float)i / sample_rate;
						float freq = ((((int)t) % 2) == 0) ? 256.0f : 512.0f;
						int16 s    = (int16)(32760.0f * sinf(t * freq * 6.2831853f));
						*out++ = s;
						*out++ = s;
					}

					auto src = std::make_unique<MemoryAudioSource>(buffer, buffer_size);
					auto dev = std::make_shared<SdlAudioDevice>(std::move(src));
					Error* err = dev->Create();
					if (err)
					{
						ShowError(window, "Audio Device", err);
						delete err;
					}
					else
					{
						audio_device = dev;
						audio_source = dev;
					}
					break;
				}

				default:
					break;
				} // keydown switch
				break;
			} // event type switch
		} // poll events

		if (audio_device)
			audio_device->Tick(0.0f);

		Error* err = tripex->Render(*audio_source);
		if (err)
		{
			ShowError(window, "Render", err);
			delete err;
			break;
		}
	}

	// Cleanup
	if (audio_device)
	{
		audio_device->Destroy();
		audio_device.reset();
	}

	tripex->Shutdown();
	tripex.reset();

	renderer->Close();
	renderer.reset();

	SDL_GL_DeleteContext(gl_ctx);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
