# Projects and Files

A **project** is your song: its tracks, regions, mixer, plugins and settings, saved in one file, with the audio it records in an **Audio Files** folder beside it.

## The New Project window

When the app opens, the **New Project** window comes up. **File > New Project...** (`Cmd/Ctrl+N`) opens it at any time, and **File > Dashboard...** opens it on its **Recent** tab.

![The New Project window: starts on the left and in the middle, the project's settings on the right](img/new-project.jpg)

**Create** - pick how the project starts, set it up on the right, and click **Create** (or double-click a start):

- **Blank Project** - **Empty** (no tracks), **Audio** (one audio track), **Instrument** (one Studio Keys track) or **Vocal Session** (a lead with EQ and compressor, two doubles, ad-libs, a vocal reverb and a vocal delay).
- **Music Production** - **Beat Starter** (Studio Drums, Beat Pad, an 808, keys, a pad), **Song Writer** (acoustic guitar, keys, vocal, drums, a reverb) and **Electronic** (beat pad, bass, lead and pad synths, an FX bus).
- **Recording** - **Live Band**: kick, snare and overheads into a drum bus, bass DI, two guitars, a vocal and a room reverb.
- **Vocals & Podcast** - **Podcast** (a host and two guests into a dialogue bus) and **Voice-over** (one voice with EQ, compressor and limiter).
- **Mixing & Mastering** - **Mixing** (16 tracks into drum, music and vocal buses, with a reverb and a delay) and **Mastering** (one stereo track with EQ, compressor and limiter).
- **My Templates** - the projects you saved with **File > Save as Template...**

The settings on the right:

- **Name** and **Location** - the project gets its own folder there (Music > Audio Tech Pro > Projects at first), saved straight away, with its recordings in the folder's **Audio Files**.
- **Sample rate** - 44.1, 48, 88.2, 96, 176.4, 192, 352.8 or 384 kHz. Your interface is switched to it whenever the project opens. If the interface can't run at that rate, you're told, and the project plays at the interface's rate.
- **Bit depth** - **16-bit**, **24-bit**, **32-bit float** or **64-bit float**: what the project's recordings are written in. 24-bit is plenty for most music; 32-bit float can't clip in the file, however hot the input; 64-bit float takes twice the space for the same safety.
- **File type** - **WAV**, **AIFF** or **FLAC**. FLAC files are smaller but hold whole numbers only (recorded at 24-bit when you choose float); AIFF has no 64-bit float (32-bit instead).
- **Interface** - keep the audio interface in use, or switch to another for this project.
- **Tempo** - type it, or click **TAP** in time a few times. **Key**, **Time signature** and **Count-in** (off, 1 bar or 2 bars) set the song up before you start.
- **Show this window when the app opens** - untick it to start on an empty project instead (or turn on *Open the last project when the app starts* in **Settings**).

The window remembers what you chose last time, so the next project starts the same way.

**Recent** - your recent projects with when they were saved, their sample rate and bit depth, how many tracks, the tempo and the key. Click one to open it. **Open Project...** opens any other.

![The Recent tab](img/new-project-recent.jpg)

## Open and save

- **File > Open...** (`Cmd/Ctrl+O`) - open a saved project. Recent projects are listed in the File menu and the New Project window.
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
- **File > Delete Unused Audio...** - clears out audio the project doesn't use any more (see below).

## Delete Unused Audio

Recording takes you didn't keep, bounces a newer one replaced, renders of regions you've since changed and freeze files of tracks you unfroze all stay in the project's folder. **File > Delete Unused Audio...** lists them, so you can get the space back.

![Delete Unused Audio](img/unused-audio.jpg)

- Each file shows **why it's unused**, its **length** and its **size**. Click the **play** button to listen before you decide; click a row to tick or untick it.
- **All**, **Recordings**, **Bounces & renders** and **Imports** filter the list. Imported files aren't ticked at first (you may want them again); **Select All** ticks everything shown.
- A file anything in the project still uses is never listed: regions, takes, a stretched region's original, freeze files, the studio instruments' samples, impulse responses. Files the project's **alternatives** use - or the saved project, when you've deleted a region but not saved yet - are shown greyed, and kept.
- **Keep audio recorded in the last 24 hours** unticks today's takes.
- **Move them to:** **Trash** - your computer's Trash (the Recycle Bin on Windows), where **Put Back** (Mac) or **Restore** (Windows) brings a file back until you empty it; or the **Deleted Audio folder** inside the project's Audio Files, which you empty from this window with **Empty Deleted Audio** (that one deletes for good).
- Afterwards the undo history starts again, so undo can't bring back a region whose audio has gone.
- Exports in the **Stems** folder are never listed.

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
