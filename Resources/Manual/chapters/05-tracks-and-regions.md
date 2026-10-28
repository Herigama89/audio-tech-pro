# Tracks and Regions

## Kinds of track

- **Audio track** (`Cmd/Ctrl+Option/Alt+A`) - recordings and audio files.
- **Software instrument track** (`Cmd/Ctrl+Option/Alt+S`) - MIDI regions played by an instrument: Tech Synth, Nemix, Drum Kit, Sampler or a plugin instrument. Choose the instrument with the instrument key on the track header (it shows the instrument's name) or the input slot of its mixer strip.
- **Aux bus** - receives sends from other tracks (for a shared reverb or delay) or is the output of a group of tracks.
- **VCA** (Track > New VCA) - a fader that moves the selected tracks together, keeping their balance. It carries no sound itself.
- **Drummer, Bass Player and Keyboard Player** tracks - session players that play along with your song (see *Session Players and Generate*).

## Working with tracks

- **Select** a track by clicking its header; `Up` and `Down` select the track above or below.
- **Rename** - double-click the name.
- **Mute** (`M`), **Solo** (`S`), **Record arm**, **Input monitoring** and **Automation** (`A`) - the M, S, R, I and A keys on the header.
- **Reorder** - drag a header up or down.
- **Colour** - **Track > Track Colours...** (`Option/Alt+C`).
- **Duplicate** - **Track > New Track With Duplicate Settings** (`Cmd/Ctrl+D`): the same instrument, fader, inserts and sends, without the regions.
- **Hide** - **Track > Hide Track** hides tracks you're not working on (they still play). `H` shows or hides the hidden ones.
- **Delete** - `Cmd/Ctrl+Backspace`.
- **MIDI controller** - right-click a track > **Learn MIDI Controller**, then move a knob or fader on your controller. **Controller Assignments...** lists them all.

## Track stacks and groups

Select several tracks and choose **Track > Create Track Stack...** (`Cmd/Ctrl+Shift+D`):

- **Folder stack** - one fader, mute and solo on top for all of them. Fold it up to tidy the window.
- **Summing stack** - they play into one aux bus, so you can add effects to all of them together (a drum bus, a vocal bus).

**Mix > Group Settings...** names groups and chooses what each one links: volume, mute, solo, record. Put tracks in a group from the mixer strip or the Inspector.

**Freeze Track** (Track menu) renders a track with its instrument and plugins to audio, to save processing. Choose it again to unfreeze and edit.

## Regions

A **region** is a piece of audio or MIDI on a track.

- **Move** - drag it. It lands on the grid (see **Snap**); hold `Cmd` (Ctrl on Windows) while dragging to move freely.
- **Copy** - `Option/Alt`-drag, or `Cmd/Ctrl+C` and `Cmd/Ctrl+V` (pastes at the playhead on the selected track).
- **Shorten or lengthen** - drag its left or right edge. `Option/Alt`-drag an edge of an audio region to **time-stretch** it instead.
- **Loop** - `L`, or drag the upper right corner: the region repeats up to where you drag.
- **Repeat** - `Cmd/Ctrl+R` puts a copy right after it.
- **Split** - `Cmd/Ctrl+T` at the playhead, or click with the Scissors.
- **Join** - `Cmd/Ctrl+J` joins the selected regions on a track into one. **Consolidate Clips** (`Cmd/Ctrl+Shift+J`) makes selected audio regions one new file.
- **Nudge** - `Option/Alt+Left/Right` moves by a 16th.
- **Trim to playhead** - `Option/Alt+[` and `Option/Alt+]`.
- **Mute a region** - **Edit > Mute Regions**, or click with the Mute tool.
- **Delete** - `Backspace` or `Delete`.
- **New MIDI region** - double-click an empty spot on an instrument track, or draw with the Pencil.

![Regions in the tracks area](img/regions.jpg)

### Fades and clip gain

- **Fades** - drag the small handle at the **top corner** of an audio region (start or end) to fade in or out. The Fade tool does the same anywhere. **Edit > Remove Fades** takes them off.
- **Clip gain** - each audio region has a gain handle: drag it up or down to make the region louder or quieter. Double-click it for 0 dB. **Edit > Region Gain...** sets an exact amount.

### Editing a whole section

Set the cycle (drag in the top half of the ruler) over a section, then:

- **Cut Section Between Locators** - removes that stretch of the song from every track and closes the gap.
- **Insert Silence Between Locators** - opens a gap the length of the cycle.
- **Repeat Section Between Locators** - plays the section twice (a copy right after it).
- **Split Regions by Locators** - splits every region at the cycle's start and end.
- `Cmd/Ctrl+U` sets the cycle to the selected region.

## The tools

Press `T` for the tool menu, or right-click an empty space in the tracks area.

- **Pointer** - select, move, trim and loop regions.
- **Pencil** - draws a new MIDI region on an instrument track (drag for its length).
- **Eraser** - deletes the region you click.
- **Text** - click a region to rename it.
- **Scissors** - splits a region where you click.
- **Glue** - joins regions.
- **Solo** - hold the mouse on a region to hear its track alone from there.
- **Mute** - mutes or unmutes a region.
- **Zoom** - click or drag to zoom in; `Option/Alt`-click zooms out.
- **Fade** - drag at a region's start or end to make a fade; `Option/Alt`-click removes it.
- **Marquee** - select a stretch of time across tracks; `Delete` removes it, `Cmd/Ctrl+T` splits at its edges.

## Snap and Drag modes

**Snap** (`N` turns it on and off): Smart (follows the zoom), Bar, Beat, Division or Off.

**Drag**: *Overlap* (regions can lie on top of each other), *No Overlap* (a moved region shortens what it covers), *X-Fade* (they crossfade where they overlap), *Shuffle R* and *Shuffle L* (regions close up the gaps, like beads on a string).
