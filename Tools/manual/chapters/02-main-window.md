tab: Start
title: The Main Window
---
# The Main Window

Everything starts in the main window: the control bar along the top, the tracks in the middle, and panels you can show around them (the Inspector on the left, the mixer or an editor below, the Library, Smart Controls and more).

![The main window, part by part](img/main-window.jpg)

1. **File menu** - new, open, save, import audio, bounce the song.
2. **Undo and Redo** - `Cmd/Ctrl+Z` and `Cmd/Ctrl+Shift+Z`. Hover over them to see what will be undone.
3. **Add tracks** - a new audio track, instrument track or aux bus.
4. **Audio settings** - your audio interface, sample rate and buffer size, latency calibration.
5. **Go to start** - `Return`.
6. **Stop** - press again to go back to the beginning.
7. **Play** - `Space` plays and stops.
8. **Record** - `R` records on the armed tracks (if none is armed, the selected track is armed for you).
9. **Cycle** - `C` turns the loop on and off. Drag in the top half of the ruler to set the range. Recording with Cycle on makes a new take on every pass.
10. **Count-in** - one bar of click before recording (`Shift+K`).
11. **Metronome** - the click (`K`).
12. **Punch in/out** - Autopunch: record only between the punch locators (the red bar under the cycle in the ruler).
13. **Bypass all** - hear the mix with all the processing switched off, to compare. Click again to bring it all back.
14. **Position** - bar, beat and division, and the time in hours, minutes and seconds.
15. **Tempo** - double-click to type a new tempo; right-click to detect it from a region, or to tap it.
16. **Time signature and key** - click to change them.
17. **Mixer** - show or hide the mixer under the tracks (`X`).
18. **Editor** - the piano roll for the selected MIDI region, or the audio editor for an audio region (`P`).
19. **Library** - patches and sounds for the selected track (`Y`).
20. **Smart Controls** - the main knobs of the selected track (`B`).
21. **Quick Help** - a box that explains whatever is under the mouse.
22. **Loops** - the Loop Browser: browse loops and samples, click to hear, drag onto a track (`O`).
23. **Generate** - write drums, bass, chords and a melody in a genre's style.
24. **Screen** - record the app with the mix's sound to a video.
25. **Claude** - the Claude assistant: ask questions or tell it what to do.
26. **Tool menu** - the tool the mouse uses in the tracks area (`T` opens a menu of all tools).
27. **Snap** - where regions land when you move them. *Smart* follows the zoom. Hold `Cmd` (Ctrl on Windows) while dragging to move freely.
28. **Track headers** - each track's number, name, icon and buttons: **M** mute, **S** solo, **R** record arm, **I** input monitoring, **A** automation, plus a volume fader and pan knob.
29. **Tracks area** - the regions (audio and MIDI) on a timeline, with the ruler, markers and tempo lane above.
30. **Input and output meters** - click to clear the peaks; right-click for peak hold and fall speed.
31. **Master volume** - the level of everything you hear.

## The control bar keys

The second row of the control bar has keys for the panels and functions you use most. From the left: **Library**, **Inspector**, **Help** (Quick Help), **Toolbar**, **Smart** (Smart Controls), **Mixer**, **Editor**; the transport (**Start**, **Rewind**, **Forward**, **Stop**, **Return to start**, **Play**, **Rec**); **Cycle**, **Count**, **Click**, **Punch**, **Replace**, **Clip Stop** and **Bypass**; then **Lists** (Event List), **Notes** (Notepad), **Loops**, **Browser**, **Generate**, **Claude**, **Screen** and **Keys** (Key Commands).

- **Return to start** - when lit, stopping jumps back to where playback started (as some programs do). Off, the playhead stays where you stopped.
- **Replace** - a new recording replaces the parts of regions it covers.
- **Clip Stop** - recording stops by itself if an input clips (hits 0 dB).

## The bar above the tracks

- **Edit, Functions, View** - the tracks area's own menus.
- **Tool** - Pointer, Pencil, Eraser, Text, Scissors, Glue, Solo, Mute, Zoom, Fade and Marquee. See *Tracks and Regions* for each tool.
- **Catch** - the tracks area follows the playhead while it plays. Off, the view stays where you put it.
- **Snap** - Smart, Bar, Beat, Division or Off.
- **Drag** - what happens when regions meet: *Overlap* lets them lie on top of each other, *No Overlap* shortens what they cover, *X-Fade* crossfades where they overlap, *Shuffle R/L* closes the gaps.
- **Waveform Zoom**, **fit height**, **fit width**, and the **track height** and **horizontal zoom** sliders.

![The tools](img/tools.jpg)

## Panels you can show

- **Inspector** (`I`) - on the left: the selected region's settings, the selected track's settings, and its channel strip beside the master's.
- **Mixer** (`X`) - below the tracks, or in its own window (`Cmd/Ctrl+2`).
- **Editors** (`P`) - the piano roll, step editor and score for MIDI; the audio editor for audio. In their own window with `Cmd/Ctrl+4`.
- **Smart Controls** (`B`) - eight knobs for the selected track's sound.
- **Library** (`Y`) and **Loop Browser** (`O`) - on the right.
- **Browser** (`F`) - audio files on your computer.
- **Toolbar** - a row of keys for common edits (split, join, repeat, quantize, bounce, freeze...).
- **Status bar** - hints along the bottom.

**View > Show All Panels** and **View > Hide All Panels** switch them all at once. Hide All leaves just the tracks, for the biggest view.

![The Inspector beside the tracks](img/inspector.jpg)

## Zooming and moving around

- `Cmd/Ctrl+Right` and `Cmd/Ctrl+Left` zoom in and out in time; `Cmd/Ctrl+Down` and `Cmd/Ctrl+Up` make the tracks taller or shorter.
- `Z` fits the whole song in the window.
- Scroll with the mouse wheel or trackpad; hold `Shift` with a mouse wheel to scroll sideways.
- `,` and `.` move one bar back and forward; `Return` goes to the start.
- Click in the ruler to move the playhead there.
