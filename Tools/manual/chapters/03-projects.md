tab: Start
title: Projects and Files
---
# Projects and Files

A **project** is your song: its tracks, regions, mixer, plugins and settings, saved in one file, with the audio it records in an **Audio Files** folder beside it.

## New, open and save

- **File > New** (`Cmd/Ctrl+N`) - an empty project. **File > New from Template** starts from one of your templates.
- **File > Open...** (`Cmd/Ctrl+O`) - open a saved project. Recent projects are listed in the File menu.
- **File > Save** (`Cmd/Ctrl+S`) and **Save As...** (`Cmd/Ctrl+Shift+S`).
- **File > Close Project** - asks to save changes first.

## Keeping your work safe

- **Autosave** - in **Settings** (`Cmd/Ctrl+Option/Alt+,`) choose how often the project saves itself: every minute up to every 10 minutes.
- **Backups** - each save keeps a backup (choose how many, 3 to 50, and whether more are made every few minutes). **File > Revert to Backup...** opens one.
- **Alternatives** - **File > New Alternative...** saves a copy beside the project, to try ideas. Switch between them under **File > Alternatives**.
- **Templates** - **File > Save as Template...** saves the current set-up (tracks, plugins, routing) to start new songs from.
- **Undo History** (`Cmd/Ctrl+Option/Alt+Z`) - every edit you can undo; click one to go back or forward to it.

## Audio files

- **File > Import Audio File...** (`Cmd/Ctrl+Shift+I`) - puts audio files on the selected track at the playhead. You can also drag files from your computer, the **Browser** (`F`) or the **Loop Browser** (`O`) onto the tracks.
- **File > Consolidate Project...** - copies every audio file the project uses into its own folder, so it's all in one place.
- **File > Save a Copy As...** - a copy of the project with all its audio in one folder (and a .zip if you like), to open on any computer or send to someone.
- **File > Find Missing Audio Files...** - when a project can't find its audio (it was moved), look for it in a folder you choose.

## Bringing in work from other programs

**File > Open Project from Another Program...** opens projects from Ableton Live, FL Studio, Studio One, Pro Tools, Logic, Cubase, DAWproject files and standard MIDI files. Tracks, regions, tempo and as much of the mix as the file holds come in.

From **Logic Pro** (a `.logicx` project), the tracks with their names, the audio regions, the MIDI regions with their notes, the tempo changes, the time signatures and the markers come in. Logic keeps its plugins, instruments and mix settings in a form Apple hasn't published, so instrument tracks start on the built-in synth (choose Nemix or your plugin for each) and the faders at 0 dB. In Logic you can also use **File > Export > All Tracks as Audio Files** and open the result here, to bring each track's sound exactly as you mixed it.

**File > Import Tracks from Project...** brings chosen tracks - their regions, plugins, sends and automation - from another of your projects into this one.


## Exporting

- **File > Bounce Song to WAV...** (`Cmd/Ctrl+B` or `Cmd/Ctrl+E`) - the whole song (plus two seconds for reverb tails) to a WAV file. With **Cycle** on, just the cycle range is exported.
- **File > Export All Tracks as Audio Files...** (`Cmd/Ctrl+Shift+E`) - each track and bus to its own WAV in a folder, with its plugins, fader and pan, ignoring solo. Use this to send stems to a mix engineer or to another program.
- **Track > Bounce in Place** - renders the selected track to a new audio track and mutes the original.
- **Edit > AudioSuite...** - processes selected audio regions with an effect into new files.

## The Notepad

**View > Notepad** (`Cmd/Ctrl+Option/Alt+P`) keeps lyrics, ideas and to-do notes with the project.
