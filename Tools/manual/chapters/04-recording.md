tab: Record & Edit
title: Recording
---
# Recording

## Set up your audio interface

Click **Audio** at the top right, or choose **Audio Settings...** (`Cmd/Ctrl+,`):

- **Device** - your interface. On Mac it's Core Audio, on Windows choose the ASIO driver for the lowest delay.
- **Sample rate** - 44.1 or 48 kHz for most music.
- **Buffer size** - 64 or 128 samples while recording (you hear yourself with little delay); 512 or 1024 while mixing with many plugins.

**Calibrate Recording Latency...** plays a click through your interface and measures how long it takes to come back, so recordings line up exactly with the music. Connect an output to an input with a cable (or hold a mic to a speaker) and click Measure.

**Combine Audio Interfaces...** uses several interfaces at once, as one (on Mac).


**Mix > I/O Labels...** gives the interface's inputs your own names ("Vocal mic", "Guitar DI"); they show in the input menus.

## Record audio

1. Make an audio track: **Track > New Audio Track** (`Cmd/Ctrl+Option/Alt+A`).
2. Choose its **input** on the mixer strip or in the Inspector (In 1, In 2, or a stereo pair), and mono or stereo with the round button beside it.
3. Arm it: click **R** on the track header. Click **I** (input monitoring) to hear yourself through the track's plugins.
4. Check the level: sing or play your loudest; the meter should stay out of the red. Turn the gain on your interface, not the fader.
5. Press `R` to record, `Space` to stop.

The format (16-bit, 24-bit or 32-bit float WAV) is set in **Settings > Recording format**. 24-bit is the usual choice.

## Record MIDI

Make an instrument track (`Cmd/Ctrl+Option/Alt+S`), choose its instrument, connect your MIDI keyboard, and press `R`. MIDI keyboards are found automatically.

- **Merge MIDI Recordings** (Record menu) - MIDI recorded over a MIDI region is added to it instead of making a new region.
- **Quantize** afterwards with `Q`, or in the piano roll.

## Helpers

- **Metronome** (`K`) and **Count-in** (`Shift+K`, one bar of click before recording).
- **Autopunch** (`Cmd/Ctrl+Option/Alt+I`) - records only between the punch locators: the red bar under the cycle in the ruler. Drag its ends to set where recording starts and stops. Play from before it; recording drops in and out by itself.
- **Replace** - a new recording replaces the parts of regions it covers. Off, new recordings go on top (and become takes).
- **Stop Recording on Clip** - if an input clips (hits 0 dB), recording stops by itself, keeping what came before.

## Takes and comping

Record over the same place several times - or turn on **Cycle** (`C`) and keep playing: each pass becomes a new **take**, and they're kept together in a **take folder**.

- Click the take folder's **name bar** for its menu: choose which take plays, **Open Take Folder**, **Flatten Comp**, **Clear Comp** or **Keep only this take**.
- With the folder open, each take is a lane. **Swipe** across the best parts of each take with the mouse - the parts you swipe are lit and play together as one performance (a *comp*). This is called *Quick Swipe comping*.
- When you're happy, **Flatten Comp** turns it into ordinary regions.

## Markers

- `Option/Alt+'` makes a marker at the playhead; double-click the marker track to make one anywhere. Double-click a marker to rename it.
- `Option/Alt+.` and `Option/Alt+,` jump to the next and previous marker.
- Use them for Verse, Chorus, Bridge - then jump between parts while you work.
