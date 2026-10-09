# Audio Tech Pro

A native DAW in C++20 + JUCE 8, in the spirit of Logic Pro, with a console-style
look (navy control bar, cream track headers, a gold ruler, and a mixer with
coloured knobs and metal fader caps). It has:

- a multitrack arrange view and an undoable project model
- saving and opening projects
- audio recording with measured latency compensation
- loop (cycle) recording into takes
- a count-in, a metronome, and input monitoring
- instrument tracks with a built-in synth or a VST3 / Audio Unit instrument plugin
- MIDI regions, a piano roll, and MIDI keyboard playing and recording
- a mixer with insert effects (built-in, VST3 and Audio Unit) on every track and the master
- aux buses with pre- and post-fader sends
- automation of volume, pan and built-in effect knobs
- plugin delay compensation
- export of the song to WAV
- a console section on every mixer strip: four-band EQ (HF, HMF, LMF, LF) and one-knob Dynamics
- the Master Bus Compressor on the master, with a gain-reduction VU meter
- Logic Pro's key commands, a full menu bar, and a Key Commands window to change any key

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
open "build/AudioTechPro_artefacts/Debug/Audio Tech Pro.app"
```

- The first configure downloads JUCE and Catch2, which takes a few minutes.
- After you change code, you only need `cmake --build build` again.

### Windows (Visual Studio 2022 with "Desktop development with C++")

```bat
cmake -B build
cmake --build build --config Debug
"build\AudioTechPro_artefacts\Debug\Audio Tech Pro.exe"
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build git libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
"./build/AudioTechPro_artefacts/Debug/Audio Tech Pro"
```

## Tests

```bash
cmake --build build --target AudioTechProTests
./build/AudioTechProTests_artefacts/Debug/AudioTechProTests
```

The tests cover:

- **Model:** tracks and regions, undo and redo, move, split, trim, and the JSON round trip.
- **Engine:** sample-accurate region placement, source offsets, mute, solo, volume, pan, resampling alignment, and swapping graphs mid-playback.
- **Recording:** WAV output, punch-in position, input monitoring, the metronome, and latency compensation.
- **Routing and automation:** sends before and after the fader, mute silencing sends, bus effects, solo-safe buses, volume fades and effect knobs following their lanes, and delay compensation lining up tracks and buses to the sample.
- **Mixer:** insert chains in the project (order, undo, save, older files), every built-in effect's behaviour, track and master chains in the engine, bypass, track meters, and exporting (length, contents, no click, cancel).
- **MIDI:** notes, splitting, quantize, the JSON round trip, turning recorded events into notes, synth timing and release, sample-accurate note playback, stop releasing notes, live MIDI routing, and MIDI recording positions.
- **Console and Master Bus Compressor:** EQ bands, Dynamics, IN switches, ratio, make-up, mix, fading in without a click, settings saved with the project, and both running in the engine.
- **Key commands:** every command has a name and menu, and the Logic Pro preset has Logic's keys with no key used twice.
- **Cycle:** sample-exact wrap, count-in clicks, splitting loop recordings into passes, take folders (switch, split, flatten, save), and the latency estimator, including a full loopback through a simulated cable.

## Key commands (Logic Pro preset)

These are Logic Pro's default keys for everything the app can do. Change any of
them in **Key Commands** (Option+K, or the **Keys** button), where you can also
go back to the preset.

| Key | Command | Key | Command |
| --- | --- | --- | --- |
| Space | Play / stop | R | Record |
| Return | Go to beginning | , / . | Back / forward one bar |
| C | Cycle | Cmd+U | Set cycle to the selected region |
| K | Metronome | Shift+K | Count-in |
| X | Mixer | P or E | Piano roll |
| A | Show automation | Q | Quantize the selected MIDI region |
| M / Ctrl+M | Mute selected track | S / Ctrl+S | Solo selected track |
| Up / Down | Select track above / below | Cmd+Delete | Delete track |
| Option+Cmd+A | New audio track | Option+Cmd+S | New instrument track |
| Cmd+T | Split at playhead | Delete | Delete region |
| Cmd+Z / Shift+Cmd+Z | Undo / redo | Cmd+Left / Right | Zoom out / in |
| Cmd+N / O / S | New / open / save | Shift+Cmd+S | Save as |
| Shift+Cmd+I | Import audio | Cmd+B | Bounce (export) |
| Option+K | Key Commands | Cmd+, | Audio settings |

### Other DAWs

Open **Key Commands** (Option+K), pick **Pro Tools**, **Ableton Live**, **FL Studio** or **Cubase**, and click **Use this preset**. Each gives that DAW's default keys for every command this app has, for example:

| | Pro Tools | Ableton Live | FL Studio | Cubase |
| --- | --- | --- | --- | --- |
| Play / stop | Space | Space | Space | Space |
| Record | F12, keypad 3 | F9 | R | keypad * |
| Split | Cmd+E | Cmd+E | C | Option+X |
| Mixer | Cmd+= | Option+Cmd+M | F9 | F3 |
| Piano roll | Ctrl+= | Option+Cmd+L | F7 | Return |
| Loop / cycle | Shift+Cmd+L | Cmd+L (to selection) | | keypad / |
| Metronome | keypad 7 | | Cmd+M | C |
| Bounce | Option+Cmd+B | Shift+Cmd+R | Cmd+R | |
| Zoom in / out | Cmd+] / Cmd+[ | = / - | | H / G |

A preset also sets the stop workflow: with Pro Tools, Live and FL Studio, stopping jumps back to where you pressed play; with Logic and Cubase the playhead stays where you stopped. Change it any time with **Stop returns to where playback started** in the Key Commands window. Option+K and Cmd+, open Key Commands and Audio settings in every preset.

In Logic, M and S mute and solo regions; this app doesn't have region mute yet,
so they act on the selected track. The menu bar uses Logic's menu names: File,
Edit, Track, Navigate, Record, Mix, View.

## Using it

| Action | How |
| --- | --- |
| Record | Press **R** or click **Rec**. It records on every armed track, or arms the selected track (or a new one) if none are armed. Press **R** again to punch out, or **Space** to stop. |
| Arm a track | Click its **R** button. Right-click **R** to choose the input: mono, or a stereo pair. |
| Monitor the input | **I** on an armed track. Use headphones, or a mic near speakers will feed back. |
| Metronome | **K** or the **Click** button |
| Cycle (loop) | Drag in the **top half of the ruler** to set the range. Click the bar, press **C**, or use **Cycle** to turn it on or off. |
| Generate MIDI | Click **Generate** (top left) or choose **Track > Generate MIDI...**. Pick a genre, key, length and parts, then **Generate**. New instrument tracks appear at the playhead's bar. **Try another** replaces the last take with a new one. Drums use General MIDI notes (kick 36, snare 38, hats 42/46), so load any drum plugin on the Drums track. |
| Ask Claude | Click **Claude** (top left) or choose **View > Claude Assistant**. The first time, paste your Anthropic API key (from console.anthropic.com) in **Settings**. Then type what you want: "set the tempo to 92 and loop bars 5 to 8", "write a sad piano progression in F minor", "add a reverb bus for the vocals", "analyze my mix", or any question about the app. Everything Claude changes in one go is one **Undo**. Right-click a message to copy it. **New chat** starts over. |
| Use two interfaces | **View > Combine Audio Interfaces...** (or the **Audio** key's menu). Tick the interfaces, choose the clock (your main interface), and keep drift correction on unless they share a word clock cable. Their inputs become one list, the clock's first, named by interface ("SSL 2 MKII 1"); playback comes out of the clock interface's outputs 1-2. It's set up again each time you start the app. **Use one interface** goes back. |
| Record the screen | Click **Screen** (top left) or **View > Record Screen...**. Choose the app's windows or the whole screen, the size (up to 4K), 30 or 60 fps, and the sound: the mix, or the mix plus a mic input (the mic goes only into the video). Click **Screen** again to stop; the MP4 is saved in Movies > Audio Tech Pro unless you choose another folder. The first time, allow Audio Tech Pro under System Settings > Privacy & Security > Screen & System Audio Recording, then reopen the app. |
| Loop recording | Turn Cycle on and record. Each pass becomes a take. Click a take folder's name bar (or right-click it, then **Takes**) to switch takes, or choose **Keep only this take**. |
| Count-in | **Count** gives one bar of click before recording starts. |
| Latency calibration | **Audio > Calibrate recording latency...** Connect a cable from output 1 to an input and click **Measure**. The app stores the difference from the driver's reported latency and applies it to every recording. |
| File | **File** menu: New, Open, Save, Save As, Import Audio (Cmd+I), Export Song as WAV (Cmd+E). The old shortcuts still work. |
| Mixer | **Mixer** key or **X**. Each strip, top to bottom: the console section, 6 effect slots, 2 sends, the fader and meter, M and S, and the name plate. The Master Bus Compressor and the master strip are on the right. |
| Console strips | Each strip is built from the console artwork. Top to bottom: the EQ (HF 8 kHz shelf, HMF 2.5 kHz, LMF 400 Hz, LF 100 Hz shelf, each +/-15 dB) with its red **EQ** lamp key (switches EQ and Dynamics in or out), the **M** and **S** keys, **Dynamics** (a one-knob compressor) with its red **DYN** lamp key, then two displays: the Dynamics amount, and pan (drag it left or right; double-click to centre). Under those: 6 effect slots, 2 sends, the fader with its meter and level display, and the name plate. Double-click any knob or the fader to reset it. If the mixer is taller than its pane, scroll it up and down. |
| Master Bus Compressor | Next to the master strip. Click **IN** to switch it on. Threshold, Make-up, Attack (0.1 to 30 ms), Release (0.1 to 1.2 s, or Auto), Ratio (2, 4 or 10:1) and Mix (for parallel compression). The meter shows how much it's turning the mix down. It sits after the master effect slots and before the master fader, and is included when you bounce. |
| Console mode (UA bus compressor) | If Universal Audio's SSL G Bus Compressor is installed (UADx or UAD, Audio Unit or VST3, found by a plugin scan), the mixer shows a hardware-style face for it instead of the built-in compressor. Press **IN** once to load your plugin onto the master; after that **IN** switches it in and out. The knobs turn the plugin's own controls, the needle shows its gain reduction, the left LEDs show gain reduction and the right LEDs the output level. Right-click the face to open the plugin's window, choose which plugin to use, reassign any knob to a different parameter, or go back to the built-in compressor. Nothing is unlocked or included: it only works with a copy you own and have authorised. |
| Console mode (UA channel strips) | If UA's SSL 4000 E channel strip is installed (UADx or UAD), the mixer header shows **Console**. Choose the plugin there and every track gets its own copy of it, in place of the built-in EQ and Dynamics; new tracks get it too, and **Built-in** takes them all off again (one undo step). Each strip's knobs then turn the plugin: HF, HMF, LMF and LF gain, and the Dynamics knob turns the compressor threshold; the red **EQ** and **DYN** keys are the plugin's EQ and Dynamics switches. "UAD" is engraved on strips running the plugin. Right-click a strip's console section to open the plugin's own window (for frequencies, Q, filters, gate and the rest), switch just that strip to the built-in strip or back, or assign a knob to a different parameter. |
| Add an effect | Click an empty slot in the mixer, then pick a built-in effect (EQ, Compressor, Delay, Reverb, Limiter) or one of your plugins under **Plugins**. Effects run top to bottom, before the fader. |
| Change an effect | Click a filled slot to open its window. Right-click it to **Bypass**, **Replace...** or **Remove**. |
| Aux bus | **+ Track > Aux bus**, or **New aux bus** from a send's menu. Put an effect (e.g. Reverb at 100% mix) on the bus. |
| Send | On a mixer strip, click **Send** under the effect slots and choose a bus; the small knob is the send level. In the same menu: **Pre-fader** and **Remove send**. |
| Automation | Click **A** on a track to show its automation line over the lane. Click to add points, drag to move them (they snap to the grid; hold Cmd to move freely), double-click a point to remove it. Right-click **A** to pick what to automate: Volume, Pan, or any knob of a built-in effect on that track. |
| Clear automation | Right-click **A**, then **Clear ... automation** (the line you're looking at) or **Clear all automation on this track**. Undo brings it back. |
| Export | **File > Export Song as WAV...** exports the whole song plus 2 seconds for reverb tails, or just the cycle range when Cycle is on. The file is 24-bit at your interface's sample rate; the metronome is never included. |
| New instrument track | **+ Track > Instrument track (MIDI)**. It starts with Tech Synth. |
| Choose an instrument | Click the instrument button on the track (it shows "Tech Synth" at first). Pick **Tech Synth**, or a plugin under **Plugins**. **Show plugin window** / **Show synth controls** opens its window. |
| Find your plugins | Instrument button > **Scan for plugins...**, then **Options > Scan for new or updated VST3 / AudioUnit plug-ins**. Run this once; the list is remembered. |
| Play with a MIDI keyboard | Plug it in; The app listens to every MIDI input. It plays the armed instrument track, or else the selected one. |
| Record MIDI | Arm an instrument track and press **R**. With Cycle on, each pass merges into the same region. |
| Make a MIDI region | Double-click an empty spot on an instrument track to get a one-bar region. |
| Piano roll | Double-click a MIDI region. Click to add a note (drag to set its length), drag a note to move it, drag its right edge to resize, Shift-click to select several, Delete to remove, **Q** to quantize to 16ths, Cmd+A to select all. Click the keyboard on the left to hear a pitch. |
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
| Save / open | Cmd+S, Cmd+Shift+S, Cmd+O. Projects are `.atp` files; older `.riff` projects open too. |

Recordings go to an `Audio Files` folder next to a saved project. For a
project you haven't saved yet, they go to `Music/Audio Tech Pro/Recordings/<project name>`.
Each take is named after its track, for example `Vocals#01.wav`.

Recordings are shifted earlier by the round-trip latency your interface
reports, so they line up with what you heard while playing.

Projects store audio paths relative to the project file. If you keep the audio
next to the project, you can move the folder and it still opens.

## Code layout

```
Source/
  model/
    Identifiers.h         names used in the project tree
    Project.*             the project tree: edits (all undoable), queries, JSON save/load
    Generator.*           the MIDI genre generator (pure, repeatable per seed)
  ai/
    ClaudeApi.*           HTTPS to Anthropic's Messages API with the user's key
    Assistant.*           the conversation and the tool-use loop
    AssistantTools.*      what Claude can see and do: the project as JSON, edits, transport, mix measurement
  engine/
    AudioEngine.*         device I/O, real-time render, lock-free graph hand-over
    PlaybackGraph.h       immutable snapshot the audio thread renders
    ProjectPlayer.*       watches the model and rebuilds the graph when it changes
    AudioFileCache.*      decodes and resamples audio files into memory
    ConsoleChannel.*      each strip's EQ and Dynamics
    MasterBusCompressor.* the compressor on the master
    ConsolePlugins.*      console mode: finding UA plugins and matching their parameters
  ui/
    ConsoleLook.*         the look: knobs, fader caps, keys, transport icons, the VU meter
    Commands.*            every command and the key presets (Logic Pro)
    KeyCommands.*         the Key Commands window
    Mixer.*               channel strips, the Master Bus Compressor panel
    BusCompConsolePanel.* console mode's face for the UA bus compressor
    StripParts.*          the moving parts on the strip artwork: pointers, lamp keys, fader
    ArrangeCanvas.*       ruler, lanes, regions, waveforms, mouse editing, file drops
    TrackHeaders.*        per-track name / mute / solo / volume / pan
    TimelineState.h       zoom, scroll and selection shared by the views
    WaveformCache.h, LevelMeter.h, Theme.*
  MainComponent.*         control bar, layout, project lifecycle
  MainCommands.cpp        what each command does, and the menu bar
  AppInfo.h               the app's name, in one place
Tests/                    Catch2 unit tests
Tools/Snapshots.cpp       renders the main views to PNG files (cmake -DRIFF_BUILD_SNAPSHOTS=ON)
Tools/make_bus_comp_panel.py  builds Resources/MasterBusCompressorPanel.png from the design mockup
Tools/make_strip_art.py   builds the strip artwork (StripTop, StripPlate, FaderCap) from the design mockup
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

## Phase 2: MIDI and instruments

- [x] Instrument tracks with a built-in polyphonic synth (saw/square/triangle/sine, resonant filter, ADSR)
- [x] MIDI regions with notes; trim, split and move like audio
- [x] Piano roll: draw, move, resize, delete, quantize, keyboard preview
- [x] MIDI keyboard input (hot-plug), live playing, MIDI recording (merging on cycle)
- [x] VST3 and Audio Unit instrument hosting: scanning, plugin windows, settings saved in the project
- [ ] CLAP hosting (needs our own CLAP host layer; JUCE doesn't host CLAP)
- [ ] Velocity editing, pitch bend and controllers in the piano roll
- [ ] A sampler instrument (also used by the drum machine in Phase 4)

## Phase 3: mixer and effects

- [x] Mixer with a strip per track and a master strip: insert slots, pan, fader, post-fader meters, mute, solo
- [x] Built-in effects: EQ (low shelf, mid peak, high shelf), compressor, delay, reverb, limiter
- [x] VST3 and Audio Unit effects in any slot, with their windows and saved settings
- [x] Export the song (or the cycle range) to a 24-bit WAV, faster than real time, with progress and cancel
- [x] Aux buses and sends (pre/post fader); buses are solo-safe
- [x] Automation of volume, pan and built-in effect parameters
- [x] Plugin delay compensation across tracks and buses; recordings account for it too
- [ ] CLAP instruments and effects (next)
- [ ] Automation of plugin parameters, and moving faders that follow automation

## Audio Tech Pro features

- [x] New name, console skin, Logic Pro key commands and menus, Key Commands window
- [x] Master Bus Compressor with a VU meter; console EQ and Dynamics on every strip
- [x] Key presets for other DAWs (Pro Tools, Ableton Live, FL Studio, Cubase), with each one's stop workflow
- [x] MIDI genre generator: drums, bass, chords and melody in 11 genres (MIDI only; you play it with your own instruments)
- [ ] Built-in instruments (drum kit, sampler), at the end
- [x] Console mode for the master: your UA SSL G Bus Compressor plugin, with a hardware-style face
- [x] Console mode for the strips: your UA SSL 4000 E channel strip plugin driven from each strip's console section
- [ ] Console strips, part 2: an IN key on every strip (bypasses the whole channel strip plugin, keeps it loaded), and every 4000 E control in a separate strip window that opens from the strip (as in LUNA): filters, EQ frequencies, Q and bell switches, compressor, gate/expander, input and output
- [x] Claude AI assistant (with your own API key): controls the DAW, writes MIDI, measures and advises on the mix, answers questions
- [x] Screen recording of the app (or the whole screen) with the mix's sound and an optional mic, up to 4K (Mac)
- [x] Several audio interfaces at once: Combine Audio Interfaces makes one device from them, with a clock and drift correction (Mac)
- [ ] The Audio Tech Pro plugin suite
- [ ] CLAP hosting

## Logic Pro parity checklist

Working through Logic Pro's user guide, area by area. [x] done, [ ] to do.

**Tracks area and track headers**
- [x] Resize a track by dragging the lower edge of its header (Option-drag: every track); Zoom Vertical In/Out (Cmd+Down / Cmd+Up)
- [x] One track selection shared by the tracks area and the mixer (click a strip to select its track; the mixer follows)
- [x] Select several tracks (Shift / Cmd-click, in the tracks area or the mixer) and act on all of them: mute, solo, arm, delete, hide, colour; their mixer faders move together
- [x] Reorder tracks by dragging headers (several at once); double-click to rename; track colours (Option+C)
- [x] Hide tracks (Ctrl+H hides and selects the next; H shows or leaves out hidden tracks)
- [x] Inspector (I): the selected track's channel strip and the Stereo Out on the left
- [ ] Inspector: region and track parameter boxes above the strips; track icons
- [ ] Track stacks (folder and summing stacks), track freeze
- [ ] Global tracks: arrangement markers, markers, tempo track with tempo changes, time signature and key changes
- [ ] Bounce in place, export tracks as audio files (stems), import tracks from other projects

**Editing in the tracks area**
- [ ] Tool menu (T): pointer, pencil, eraser, text, scissors, glue, solo, mute, zoom, fade, automation, marquee
- [ ] Copy, cut, paste, duplicate (Cmd+D), repeat (Cmd+R), loop regions, snap modes and drag modes (overlap, no overlap, X-fade, shuffle)
- [ ] Region gain, fades and crossfades, nudging, trim to playhead, join, mute regions
- [ ] Flex Time and Flex Pitch, time stretch, Smart Tempo, quick swipe comping refinements
- [ ] Marquee selection across tracks; edit with the playhead (split by locators, delete between)

**Editors**
- [ ] Piano Roll: velocity lane, MIDI CC / pitch bend automation, Time Handles, scale quantize, note repeat, MIDI transform functions
- [ ] Step Editor, Event List, Score Editor, Audio Track Editor and Audio File Editor
- [ ] Step Sequencer
- [ ] Editors open below the tracks with a resizable divider; the editor follows the selected track and region

**Mixer**
- [ ] Resizable mixer, narrow/wide strips, filters (audio, instrument, aux, bus, input, output, master/VCA)
- [ ] Groups (linked volume, mute, solo, selection), VCA faders, I/O labels, stereo panner modes
- [ ] Channel EQ display on the strip, signal flow view, track automation modes (Read, Touch, Latch, Write)
- [x] Inserts, sends (pre/post), aux buses, master bus compressor, console channel strips

**Recording and MIDI**
- [x] Audio and MIDI recording, punch in/out, cycle recording with take folders, count-in, metronome
- [ ] Autopunch, replace mode, multi-track recording settings, MIDI merge/overdub, step input, MIDI learn and controller assignments

**Project and file**
- [ ] Autosave and project backups, project alternatives, project templates, consolidate (collect audio into the project)
- [ ] Undo History window, preferences window, Library and loop browser, Smart Controls
- [ ] Live Loops, Drummer and Session Players (instruments come last)

## Later phases

- **Phase 4:** Drums and loops. Step sequencer and a tempo-synced loop browser.
