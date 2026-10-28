# Key Commands

Every command, by menu, with its keys. `Cmd/Ctrl` is Cmd on a Mac and Ctrl on Windows; `Option/Alt` is Option on a Mac and Alt on Windows. Change any of them in **Key Commands...** (`Option/Alt+K`), or choose the keys of another program there.

## File

- **New** `Cmd/Ctrl+N` - Start a new, empty project
- **Open...** `Cmd/Ctrl+O` - Open a saved project
- **Save** `Cmd/Ctrl+S` - Save the project
- **Save As...** `Cmd/Ctrl+Shift+S` - Save the project under a new name
- **Consolidate Project...** - Copy every audio file the project uses into its folder (Audio Files), so it's all in one place
- **Save as Template...** - Save this project as a template to start new ones from (File > New from Template)
- **New Alternative...** - Save a copy of the project beside it, to try ideas and switch between them (File > Alternatives)
- **Revert to Backup...** - Open one of the backups made each time you saved
- **Import Audio File...** `Cmd/Ctrl+Shift+I`, `Cmd/Ctrl+I` - Put audio files on the selected track at the playhead
- **Bounce Song to WAV...** `Cmd/Ctrl+B`, `Cmd/Ctrl+E` - Export the song, or the cycle range, to a WAV file
- **Export All Tracks as Audio Files...** `Cmd/Ctrl+Shift+E` - Each track (and bus) to its own WAV in a folder, with its plugins, fader and pan, ignoring solo
- **Import Tracks from Project...** - Bring tracks (regions, plugins, sends, automation) in from another project
- **Open Project from Another Program...** - Open an Ableton Live, FL Studio, Studio One, Pro Tools, Logic, Cubase, DAWproject or MIDI file project
- **Save a Copy As...** - A copy of the project with every audio file it uses, in one folder (and a .zip to send if you like): it opens on any computer
- **Close Project** - Closes the project (asking to save changes first)
- **Find Missing Audio Files...** - Looks for audio files the project can't find, in a folder you choose

## Edit

- **Undo** `Cmd/Ctrl+Z` - Undo the last edit
- **Redo** `Cmd/Ctrl+Shift+Z`, `Cmd/Ctrl+Y` - Redo the edit you undid
- **Delete** `Backspace`, `Delete` - Delete the selected region
- **Split at Playhead** `Cmd/Ctrl+T` - Split regions at the playhead
- **Quantize** `Q` - Quantize the selected MIDI region to 16th notes
- **Set Locators by Selection** `Cmd/Ctrl+U` - Set the cycle to the selected region and turn Cycle on
- **Cut** `Cmd/Ctrl+X` - Cut the selected regions
- **Copy** `Cmd/Ctrl+C` - Copy the selected regions
- **Paste** `Cmd/Ctrl+V` - Paste at the playhead, on the selected track
- **Select All** `Cmd/Ctrl+A` - Select every region
- **Repeat** `Cmd/Ctrl+R` - Repeat the selected regions once, right after them
- **Join Regions** `Cmd/Ctrl+J` - Join the selected regions on a track into one (audio: consolidated into one new file)
- **Consolidate Clips** `Cmd/Ctrl+Shift+J` - The selected audio regions on each track become one region playing one new file (gain, fades and transpose in it; gaps silent)
- **Align Audio to Guide...** - Lines the timing of the selected regions up with a guide region (the top one), as VocAlign and Cubase's AudioAlign do
- **Detect Song Key...** - Listens to the song (or the selected regions) and finds its key and scale, to set as the project's key
- **Split to Stems...** - Splits the selected audio regions into vocals, drums, bass and the rest (or six stems, with guitar and piano), each on a new track, as Logic's Stem Splitter does
- **Mute Regions** - Mute or unmute the selected regions
- **Nudge Left** `Option/Alt+Left` - Move the selected regions a 16th note earlier
- **Nudge Right** `Option/Alt+Right` - Move the selected regions a 16th note later
- **Trim Region Start to Playhead** `Option/Alt+[` - Move the selected regions' start to the playhead
- **Trim Region End to Playhead** `Option/Alt+]` - Move the selected regions' end to the playhead
- **Loop Regions** `L` - Repeat the selected regions up to the next region (drag the upper right edge to set how far)
- **Region Gain...** - Turn the selected audio regions up or down
- **Remove Fades** - Take the fades off the selected regions
- **Split Regions by Locators** - Split every region at the cycle's start and end
- **Cut Section Between Locators** - Remove the cycle's stretch of the song, and close the gap
- **Insert Silence Between Locators** - Open a gap the length of the cycle at its start
- **Repeat Section Between Locators** - Play the cycle's stretch twice: a copy right after it
- **Tool Menu** `T` - Choose a tool: pointer, pencil, eraser, scissors, glue, mute
- **AudioSuite...** - Process the selected audio regions offline with an effect or plugin (VST3, AU, CLAP), into new files
- **Snap to Grid** `N` - Snap on or off: regions and notes land on the grid, or anywhere (Cmd while dragging does the opposite once)

## Track

- **Bounce in Place** - Render the selected track to a new audio track and mute the original
- **New Audio Track** `Cmd/Ctrl+Option/Alt+A` - Add an audio track
- **New Software Instrument Track** `Cmd/Ctrl+Option/Alt+S` - Add an instrument (MIDI) track
- **New Aux Bus** - Add an aux bus for sends
- **Delete Track** `Cmd/Ctrl+Backspace` - Delete the selected track
- **Mute Track** `M` - Mute or unmute the selected track
- **Solo Track** `S` - Solo or unsolo the selected track
- **Record Enable Track** - Arm or disarm the selected track for recording
- **Show Automation** `A` - Show or hide automation on the selected track
- **Select Previous Track** `Up` - Select the track above
- **Select Next Track** `Down` - Select the track below
- **Hide Track** - Hide the selected tracks and select the next one (they still play)
- **Show Hidden Tracks** `H` - Show or leave out the tracks you hid
- **New Track With Duplicate Settings** `Cmd/Ctrl+D` - A new track like the selected one (instrument, fader, console, inserts, sends), without its regions
- **Track Colours...** `Option/Alt+C` - Choose a colour for the selected tracks
- **New Drummer Track** - A drummer who plays the song in a genre: set how busy and how loud, fills and swing
- **New Bass Player Track** - A session bass player, in the song's key
- **New Keyboard Player Track** - A session keyboard player playing chords in the song's key
- **Create Track Stack...** `Cmd/Ctrl+Shift+D` - Put the selected tracks in a stack: a folder stack (one fader on top of theirs) or a summing stack (they play into a bus)
- **Create Folder Stack** - Put the selected tracks in a folder stack: one fader, mute and solo for them all
- **Create Summing Stack** - Put the selected tracks in a summing stack: they play into one aux bus
- **Freeze Track** - Render the selected tracks with their instrument and plugins to audio, to save processing (again to unfreeze)
- **New VCA** - A VCA fader for the selected tracks: it moves them all, keeping their balance
- **Generate MIDI...** - Write drums, bass, chords and a melody in a genre's style

## Transport

- **Play or Stop** `Space` - Start or stop playback
- **Record** `R` - Record on armed tracks, or punch out
- **Go to Beginning** `Return` - Move the playhead to the start
- **Rewind One Bar** `,` - Move the playhead back one bar
- **Forward One Bar** `.` - Move the playhead forward one bar
- **Create Marker** `Option/Alt+'` - A marker at the playhead (double-click the marker track for one anywhere)
- **Go to Next Marker** `Option/Alt+.` - Move the playhead to the next marker
- **Go to Previous Marker** `Option/Alt+,` - Move the playhead to the previous marker
- **Delete Marker** - Delete the selected marker
- **Cycle** `C` - Turn Cycle (loop) on or off
- **Metronome** `K` - Turn the click on or off
- **Count-in** `Shift+K` - One bar of click before recording
- **Autopunch** `Cmd/Ctrl+Option/Alt+I` - Record only between the punch locators (the red bar under the cycle; drag it in the ruler)
- **Stop Recording on Clip** - Recording stops by itself if a mic or instrument input clips (hits 0 dB); the take keeps what came before
- **Replace** - A recording replaces the parts of regions it covers
- **Merge MIDI Recordings** - MIDI recorded over a MIDI region is merged into it

## Mix

- **Exclusive Solo** - Soloing a track unsolos the others (as in LUNA)
- **MultiMeter** - The master's meters: levels, loudness (LUFS), spectrum, goniometer and phase correlation
- **Automix...** - A balanced starting mix for a genre: each track measured and its fader set (one undo step)
- **Bypass All (Compare)** - Hear the mix with every channel's processing bypassed - preamps, channel strips, inserts, tape - and the master's inserts, bus compressor and tape; again to hear it all back (as one switch, to compare)
- **Reset Channels...** - Every channel (or the selected ones) and the master back to a fresh start: faders, pans, mute and solo, the channel strips, inserts and sends (as Cubase's Reset MixConsole Channels)
- **Suspend Plug-ins When Silent** - Plug-ins on a track that's silent stop working until sound comes again, to save CPU (as Cubase's VST3 suspend and FL Studio's Smart Disable)
- **Group Settings...** - Name the groups and choose what each links: volume, mute, solo, record
- **I/O Labels...** - Your own names for the interface's inputs (they show in the input menus)
- **Signal Flow** - The selected track's path from its input to the main output, with every stage

## View

- **Show Inserts in Edit Window** - Each track's insert slots beside its header (as in Pro Tools' Edit window)
- **Show I/O in Edit Window** - Each track's input and output beside its header, to choose them there (as on the mixer strips)
- **Show Comments in Mixer** - A notes field under each mixer channel (as in Pro Tools)
- **Open Mixer Window** `Cmd/Ctrl+2` - The mixer in a window of its own (resize it, put it on another screen)
- **Open Editor Window** `Cmd/Ctrl+4` - The piano roll (and the other region editors) in a window of its own
- **Theme...** - The skin (studio grey or flat dark) and its colours: hue, lightness, contrast, text, accent, meters, grid
- **Performance...** - How hard the computer works: audio load, its peak, overloads, the disk, and the heaviest tracks
- **Notepad** `Cmd/Ctrl+Option/Alt+P` - Project notes: lyrics, ideas, what to fix (saved with the project)
- **Browser** `F` - Find audio files on your computer and put them on the selected track
- **Quick Help** - A box that explains whatever is under the mouse
- **Waveform Zoom** - Draw the waveforms taller, to see quiet parts (click again for more, then back)
- **Zoom to Fit** `Z` - The whole song across the tracks area, and every track fitting the height
- **Show Control Bar** - The bar along the top (display, transport, tools): show or hide it
- **Show Toolbar** - A row of keys for the common edits (split, join, repeat, quantize, bounce, freeze...)
- **Show Status Bar** - The line of hints along the bottom
- **Draw with the Graphics Card** - The window is drawn by the graphics card (smooth meters and scrolling with many tracks) instead of the processor
- **Show Input and Output Meters** - The meters to the right of the tracks
- **Show Track Headers** - The track names, buttons and faders at the left of the tracks
- **Show All Panels** - Inspector, control bar, toolbar, Smart Controls, meters and the status bar
- **Hide All Panels** - Just the tracks: every panel around them hidden
- **Live Loops** - A grid of loops per track: start and stop cells and scenes in time, on the next bar
- **Catch Playhead** - The tracks area follows the playhead while it plays (Logic's Catch); off, the view stays where you put it
- **Eco Mode** - For slower computers or a laptop on battery: meters and the playhead drawn 15 times a second instead of 30, still while the app isn't in front, strips out of view left alone - the sound is the same (Settings has On when on battery, too)
- **Mixer** `X` - Show or hide the mixer
- **Piano Roll** `P`, `E` - Open the selected MIDI region in the piano roll, or close it
- **Zoom In** `Cmd/Ctrl+Right` - Zoom in horizontally
- **Zoom Out** `Cmd/Ctrl+Left` - Zoom out horizontally
- **Inspector** `I` - Show or hide the Inspector: the selected track's channel strip and the master's, on the left
- **Zoom Vertical In** `Cmd/Ctrl+Down` - Make every track taller
- **Zoom Vertical Out** `Cmd/Ctrl+Up` - Make every track shorter
- **Undo History...** `Cmd/Ctrl+Option/Alt+Z` - Every edit you can undo; click one to go back (or forward) to it
- **Settings...** `Cmd/Ctrl+Option/Alt+,` - Autosave, backups, recording format and more
- **Smart Controls** `B` - Eight knobs for the selected track's sound (right-click one to map it)
- **Library** `Y` - Patches: save a track's sound and put it on other tracks
- **Loop Browser** `O` - Browse a folder of loops and samples: click to hear, drag onto a track
- **Controller Assignments...** - The knobs and faders on your MIDI controller that move tracks (right-click a track > Learn MIDI Controller to add one)
- **Event List** `Cmd/Ctrl+Option/Alt+D` - The selected MIDI region's notes and controllers as a list you can edit
- **Key Commands...** `Option/Alt+K` - Show and edit the key commands
- **Audio Settings...** `Cmd/Ctrl+,` - Choose the audio interface, sample rate and buffer size
- **Calibrate Recording Latency...** - Measure the round trip of your interface
- **Plug-in Manager...** - Scan for VST3 and Audio Unit plugins
- **Combine Audio Interfaces...** - Use several audio interfaces at once, as one
- **Record Screen...** - Record the app (or the whole screen) to a video with the mix's sound; again to stop
- **Claude Assistant** - Show or hide the Claude panel: ask questions, or tell it what to do
