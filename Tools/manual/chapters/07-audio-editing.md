tab: Record & Edit
title: Audio Editing, Flex and Pitch
---
# Audio Editing, Flex and Pitch

## The audio editor

Double-click an audio region (or select it and press `P`) to open the **audio editor** under the tracks. Drag over the waveform to select a part; the buttons work on the selection, or on the whole region if nothing is selected.

![The audio editor](img/audio-editor.jpg)

- **Normalize** - turns up so the loudest peak is just under full scale.
- **Gain...** - turns the selection up or down by a number of dB.
- **Fade In** and **Fade Out**.
- **Silence** - makes the selection silent.
- **Reverse** - plays the selection backwards.
- **Invert** - flips the polarity (phase).
- **Time Stretch...** - makes the region longer or shorter without changing the pitch (200% = twice as long).
- **Transpose...** - changes the pitch without changing the length.
- **Quantize (Flex)...** - moves every hit onto the grid; the audio between is stretched to fit.
- **Detect Tempo** - finds the tempo this audio was played at, and sets the project to it if you like.

## Flex Time

Click **Flex Time** in the audio editor. Markers appear on the hits. Drag a marker sideways to move that moment; the audio on either side stretches to fit. Double-click to add a marker; right-click one to remove it. Use it to tighten a drum or bass part without cutting it up.

![Flex Time markers](img/flex-time.jpg)

## Flex Pitch - the pitch editor

Click **Flex Pitch** to see a vocal or solo instrument as notes (blobs) at their pitch, as in a pitch-correction program. Each note shows its name and how many cents it is off.

![The pitch editor](img/pitch-editor.jpg)

- **Pointer** - select notes; drag them up or down to change the pitch (`Option/Alt`: in cents) or sideways to move them in time.
- **Scissors** - click a note to split it there. **Glue** - join notes into one.
- **Time Quantize** - choose a grid and click **Q** to move notes onto it, by the **Strength**.
- **Scale Quantize** - choose a key and scale (*Song* = the project's key) and click **Q**: each note goes to the nearest note of the scale.
- **Keep in Scale** - notes stay on the scale's notes whatever you do to them.
- **Pitch Correction** - how far the notes go onto the nearest semitone (0% = as sung, 100% = exactly in tune).
- **Gain** - the level of the selected notes, from -30 to +12 dB.
- **Pitch Drift** - flattens each note's slow drift (scoops and sags); 100% leaves none.
- **Vibrato** - 0% none, 100% as sung, 200% twice as wide.
- **Formant Shift** - the voice's character darker or brighter (in semitones) while the pitch stays.

All of these work on the selected notes, or on every note when none is selected.

## AudioSuite - process regions offline

**Edit > AudioSuite...**, or the **AudioSuite** menu: click **Choose an effect...** (built-in or a plugin), set it, click **Preview** to hear it, choose **Clip** or **Whole File**, and click **Render**. The selected regions are processed into new audio files. Use it to fix one word, reverse a cymbal, or print an effect to save CPU.

![AudioSuite](img/audiosuite.jpg)

## Smart tools

- **Edit > Align Audio to Guide...** - lines up the timing of the selected regions with a guide region (the top one): doubled vocals and backing vocals that match the lead exactly.
- **Edit > Split to Stems...** - splits a mixed song into vocals, drums, bass and the rest (or six stems, adding guitar and piano), each on a new track. Great for remixes, practice and sampling.
- **Edit > Detect Song Key...** - listens to the song (or the selected regions) and finds its key, to set as the project's key.
- **Track > Bounce in Place** - the track rendered to a new audio track.
- **Track > Freeze Track** - the track rendered behind the scenes to save CPU; unfreeze to edit again.
