# Welcome to Audio Tech Pro

Audio Tech Pro is a complete studio: record audio and MIDI, edit, play instruments, mix with a full console and almost a hundred effects, and export your song. This manual explains every part of the program and every plugin that comes with it, with pictures.

> Press **F1** at any time to open this manual. Use the tabs at the top to move between its parts, the list on the left to pick a chapter, and the search box to find any word. **Help > Manual (PDF)** opens the same manual as a PDF you can print or read on your phone.

## How this manual is organised

- **Start** - installing, a first song in ten steps, the main window, projects and files.
- **Record & Edit** - recording, tracks and regions, the piano roll, audio editing, automation.
- **Mix** - the mixer, the channel strip, using effects and plugins, mixing tools, exporting.
- **Instruments** - the built-in instruments, Nemix, session players, Live Loops, and the lists of sounds.
- **Plugins** - every built-in effect, with its picture, what it does, how to use it and all its controls.
- **More** - looks and themes, settings, the Claude assistant, screen recording, every key command and troubleshooting.

Pictures with **numbered dots** have a key under them: each number explains the part with that number.

## Installing

**Mac (Apple Silicon or Intel):** open the `.dmg` and drag Audio Tech Pro to Applications, or run the `.pkg` installer. Because the app is not from the App Store, macOS may say it "can't be opened" or "is damaged". Open **Terminal** (Applications > Utilities) and paste this one line, then press Return and type your password:

```
APP="/Applications/Audio Tech Pro.app"; sudo xattr -cr "$APP"; sudo codesign --force --deep --sign - "$APP"; open "$APP"
```

You only need to do this once after each new version.

**Windows:** run `Setup.exe`. If Windows SmartScreen appears, click **More info > Run anyway**.

The recorded instruments for Nemix (the **Nemix Library**) are installed with the app; nothing else to download.

## Your first song in ten steps

1. **Choose your audio interface.** Click **Audio** at the top right (or press `Cmd/Ctrl+,`). Pick your interface, a sample rate of 48 kHz and a buffer of 128 samples. Smaller buffers feel tighter when you play; larger ones let you use more plugins.
2. **Set the tempo and key.** Double-click the tempo in the display at the top (BPM) and type a number. Click the key (for example *Cmaj*) to choose the song's key.
3. **Add a drum track.** Choose **Track > New Drummer Track**. A drummer plays along with the song. In its editor below the tracks, drag the dot in the square to make it busier or louder.
4. **Add an instrument.** Press `Cmd/Ctrl+Option/Alt+S` for a new instrument track. Click the instrument key on its track header (it shows *Tech Synth*), or the input slot on its mixer strip, and choose **Nemix (wavetable synth)**, then pick a sound in Nemix's **BROWSE** page - "Grand Piano", "Finger Bass" or "Supersaw Lead", for example.
5. **Record MIDI.** Press `K` to turn the click on, then `R` to record. Play your MIDI keyboard. Press `Space` to stop.
6. **Fix the timing.** Select the new region and press `Q` to quantize it, or double-click it to open the **piano roll** and move notes by hand.
7. **Record a voice or guitar.** Press `Cmd/Ctrl+Option/Alt+A` for an audio track, choose its input on the track (or in the Inspector), arm it with the **R** key on the track, and press `R` to record.
8. **Mix.** Press `X` for the mixer. Set the faders, add effects by clicking an empty insert slot, and send tracks to a reverb with the sends. Or try **Mix > Automix...** for a balanced start.
9. **Master.** On the master strip add **Mastering Assistant** (Metering > Mastering Assistant), play the loudest part of the song and let it set the EQ and loudness.
10. **Export.** Choose **File > Bounce Song to WAV...** (`Cmd/Ctrl+B`). Save the project with `Cmd/Ctrl+S`.

> Not sure what a button does? Turn on **Quick Help** (the **?** key in the control bar) and point at it.
