# Tripex (macOS port)

> **This is a macOS port** of [ben-marsh/tripex](https://github.com/ben-marsh/tripex) by Marc Santa.
> The original Windows/Direct3D version lives in the repo linked above.

<p align="center">
  <img src="tripex.jpg" />
</p>

## Building on macOS

**Requirements**

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools
- [Homebrew](https://brew.sh)
- SDL2 (`brew install sdl2`)
- CMake (`brew install cmake`)

**Build**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/Tripex.app
```

## Controls

| Key | Action |
|-----|--------|
| `ESC` | Quit |
| `←` / `→` | Previous / next effect |
| `E` | Random effect |
| `R` | Reconfigure current effect |
| `H` | Hold / freeze effect |
| `M` | Switch to random (mock) audio |
| `O` | Open WAV or MP3 file |
| `T` | Play test tone |
| `F1` | Toggle help overlay |
| `F2` | Toggle audio info |

## macOS port notes

The original codebase used Win32, Direct3D 9, and WaveOut. This port replaces those with:

- **SDL2** — windowing, input, and audio playback
- **OpenGL 3.2 Core Profile** — rendering (replacing Direct3D 9)
- **[stb_image](https://github.com/nothings/stb)** — image loading
- **[minimp3](https://github.com/lieff/minimp3)** — MP3 decoding

The core effect, audio analysis, and math code is unchanged from the original.
