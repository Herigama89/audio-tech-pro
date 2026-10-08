# Riff: a Logic-style DAW

A native DAW in C++20 + JUCE 8. Phases 0 and 1 are complete. Riff has:

- a multitrack arrange view and an undoable project model
- saving and opening projects
- audio recording with measured latency compensation
- loop (cycle) recording into takes
- a count-in, a metronome, and input monitoring

All of it runs on a real-time engine that never blocks the audio thread.

## Build and run

### macOS

Run these one at a time, without the comment lines:

```bash
xcode-select --install
brew install cmake ninja
cd riff
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
open build/Riff_artefacts/Debug/Riff.app
```

- The first configure downloads JUCE and Catch2, which takes a few minutes.
- After you change code, you only need `cmake --build build` again.

### Windows (Visual Studio 2022 with "Desktop development with C++")

```bat
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

## Tests

```bash
cmake --build build --target RiffTests
./build/RiffTests_artefacts/Debug/RiffTests
```

The tests cover:

- **Model:** tracks and regions, undo and redo, move, split, trim, and the JSON round trip.
- **Engine:** sample-accurate region placement, source offsets, mute, solo, volume, pan, resampling alignment, and swapping graphs mid-playback.
- **Recording:** WAV output, punch-in position, input monitoring, the metronome, and latency compensation.
- **Cycle:** sample-exact wrap, count-in clicks, splitting loop recordings into passes, take folders (switch, split, flatten, save), and the latency estimator, including a full loopback through a simulated cable.

## Using it

| Action | How |
| --- | --- |
| Record | Press **R** or click **Rec**. It records on every armed track, or arms the selected track (or a new one) if none are armed. Press **R** again to punch out, or **Space** to stop. |
| Arm a track | Click its **R** button. Right-click **R** to choose the input: mono, or a stereo pair. |
| Monitor the input | **I** on an armed track. Use headphones, or a mic near speakers will feed back. |
| Metronome | **K** or the **Click** button |
| Cycle (loop) | Drag in the **top half of the ruler** to set the range. Click the bar, press **C**, or use **Cycle** to turn it on or off. |
| Loop recording | Turn Cycle on and record. Each pass becomes a take. Click a take folder's name bar (or right-click it, then **Takes**) to switch takes, or choose **Keep only this take**. |
| Count-in | **Count** gives one bar of click before recording starts. |
| Latency calibration | **Audio > Calibrate recording latency...** Connect a cable from output 1 to an input and click **Measure**. Riff stores the difference from the driver's reported latency and applies it to every recording. |
| Add audio | Drag files from Finder onto a track, or onto empty space for a new track. Or use **Import** (Cmd+I) to place them at the playhead. |
| Play / stop | Space |
| Go to start | Return |
| Move the playhead | Click or drag in the ruler |
| Move a region | Drag it, including onto another track. It snaps to the grid; hold Cmd to move freely. |
| Trim a region | Drag its left or right edge |
| Split | Cmd+T splits at the playhead (the selected region, or every region under the playhead on the selected track) |
| Delete a region | Select it and press Delete, or right-click it |
| Undo / redo | Cmd+Z / Cmd+Shift+Z |
| Zoom | Cmd+scroll, or Cmd+Left / Cmd+Right |
| Scroll | Trackpad, the scroll wheel (with Shift to go sideways), or the scroll bars |
| Track controls | Name (double-click to rename), **M**ute, **S**olo, volume, pan. Right-click a track for Delete. |
| Tempo | Double-click the BPM value. The ruler and grid follow it. |
| Save / open | Cmd+S, Cmd+Shift+S, Cmd+O. Projects are `.riff` files. |

Recordings go to an `Audio Files` folder next to a saved project. For a
project you haven't saved yet, they go to `Music/Riff/Recordings/<project name>`.
Each take is named after its track, for example `Vocals#01.wav`.

Recordings are shifted earlier by the round-trip latency your interface
reports, so they line up with what you heard while playing.

Projects store audio paths relative to the `.riff` file. If you keep the audio
next to the project, you can move the folder and it still opens.

## Code layout

```
Source/
  model/
    Identifiers.h         names used in the project tree
    Project.*             the project tree: edits (all undoable), queries, JSON save/load
  engine/
    AudioEngine.*         device I/O, real-time render, lock-free graph hand-over
    PlaybackGraph.h       immutable snapshot the audio thread renders
    ProjectPlayer.*       watches the model and rebuilds the graph when it changes
    AudioFileCache.*      decodes and resamples audio files into memory
  ui/
    ArrangeCanvas.*       ruler, lanes, regions, waveforms, mouse editing, file drops
    TrackHeaders.*        per-track name / mute / solo / volume / pan
    TimelineState.h       zoom, scroll and selection shared by the views
    WaveformCache.h, LevelMeter.h, Theme.*
  MainComponent.*         toolbar, layout, shortcuts, project lifecycle
Tests/                    Catch2 unit tests
```

### How an edit reaches your speakers

1. A view calls a function in `model::` with the `UndoManager`, for example `moveRegion`.
2. The project tree changes. Every listener updates: the views repaint and `ProjectPlayer` schedules a rebuild.
3. `ProjectPlayer` builds a new `PlaybackGraph`, converting seconds to samples and attaching decoded audio from the cache.
4. `AudioEngine::setGraph` swaps it in with one atomic pointer exchange. The audio thread picks it up at its next block.
5. The old graph is freed on the message thread once the audio thread has finished with it.

## Phase 1: record and arrange

- [x] Record-arm on tracks, and record from the interface into new regions (lock-free FIFO to a disk thread)
- [x] Input selection per track (mono or stereo pair) and input monitoring
- [x] Metronome
- [x] Latency compensation from the device's reported latency
- [x] `Audio Files/` folder next to saved projects
- [x] Cycle (loop) region in the ruler, and cycle recording into take folders
- [x] Count-in before recording
- [x] Loopback latency calibration, saved between sessions
- [ ] Later: comping (stitching the best parts of several takes), and disk streaming for very long files

## Later phases

- **Phase 2:** MIDI and instruments. Piano roll, a built-in synth and sampler, and VST3 / AU / CLAP instrument hosting.
- **Phase 3:** Mixer and effects. Channel strips with plugin inserts (VST3 / AU / CLAP), sends, buses, automation, and delay compensation.
- **Phase 4:** Drums and loops. Step sequencer and a tempo-synced loop browser.
