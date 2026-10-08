# Riff — a Logic-style DAW

Phase 0 of the roadmap: a native app (C++20 + JUCE 8) that opens your audio
device, plays a test tone, loads and plays an audio file with a waveform view,
and shows input/output meters.

## Build and run

### macOS

```bash
# one-time setup
xcode-select --install          # Apple compiler, if you don't have it
brew install cmake ninja        # needs Homebrew: https://brew.sh

# build (first configure downloads JUCE, ~1-2 min)
cd riff
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# run
open build/Riff_artefacts/Debug/Riff.app
```

macOS asks for microphone access the first time; allow it so the input meter works.

### Windows (Visual Studio 2022 with "Desktop development with C++")

```bat
cd riff
cmake -B build
cmake --build build --config Debug
build\Riff_artefacts\Debug\Riff.exe
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build git libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/Riff_artefacts/Debug/Riff
```

After editing code, only `cmake --build build` is needed again.

## Using it

| Control | What it does |
| --- | --- |
| Open... / drag a file in | Loads WAV, AIFF, FLAC, Ogg (and MP3 on macOS) |
| Play / Space | Start and stop playback |
| `|<` / Return | Back to the start |
| Click the waveform | Move the playhead |
| Test tone | 440 Hz sine (adjustable) — the quickest check that audio output works |
| Audio settings | Choose interface, sample rate, buffer size, inputs |

The status line shows the device, sample rate, buffer size and estimated round-trip latency.

## Code layout

```
Source/
  Main.cpp                 app + window
  MainComponent.*          UI: transport bar, waveform, meters
  engine/AudioEngine.*     device I/O and the real-time audio callback
```

`AudioEngine` follows the real-time rules from the architecture doc: the
audio callback never locks, allocates or touches the disk. File reading
happens on a read-ahead thread, and the UI talks to the engine through atomics.

## Next (rest of Phase 0)

- [ ] Project model: `Project` → `Track` → `AudioRegion`, with stable IDs
- [ ] Command + undo/redo system with unit tests (Catch2)
- [ ] Save/load `project.json`
- [ ] Gate: a saved project reopens and plays back
