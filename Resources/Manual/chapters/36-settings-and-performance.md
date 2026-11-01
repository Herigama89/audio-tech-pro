# Settings and Performance

## Settings

**Settings...** (`Cmd/Ctrl+Option/Alt+,`):

- **Autosave** - Off, or every 1, 2, 5 or 10 minutes.
- **Backups kept** - 3 to 50.
- **Automatic backups** - only when saving, or every 5, 10 or 15 minutes.
- **Recording format** - 16-bit, 24-bit or 32-bit float WAV.
- **When you stop** - stay where it stopped, or go back to where play started.
- **Eco mode** - Off, On (for slower computers), or When on battery.
- **Process buffer range** - Off, Small, Medium (the default) or Large. See *Steady performance* below.

## Key commands

**Key Commands...** (`Option/Alt+K`) shows every command with its keys. Choose a **preset** - Logic Pro, Pro Tools, Ableton Live, FL Studio, Cubase or LUNA - to use the keys you already know. Search by name, menu or key. Click a command and press a key to add it; **Reset to Default** puts them back.

![Key Commands](img/key-commands.jpg)

## Steady performance: the process buffer range

Like Logic Pro, Audio Tech Pro doesn't process every track inside each small audio buffer. The tracks you aren't playing live are processed **ahead of time**, in larger blocks, on their own threads across all your computer's cores. Only the **live** tracks run at the small I/O buffer:

- the instrument track your MIDI keyboard plays (the selected one),
- armed and input-monitored tracks,
- tracks sent straight to other interface outputs, and whatever buses those tracks feed.

So a heavy song no longer has to finish inside a 2-3 ms buffer: the work evens out over the larger block, the CPU meter stays steady, and you can keep a small I/O buffer (64 or 128 samples) for playing and singing without clicks.

**Settings > Process buffer range** sets how far ahead:

- **Large** - the steadiest; best for big mixes.
- **Medium** - the default.
- **Small** - changes to tracks that aren't live (a fader, a mute, a plugin knob) are heard soonest.
- **Off** - everything at the I/O buffer, as before.

Changes to tracks that aren't live are heard a few tens of milliseconds later, just as in Logic. Select a track to make it live; it stays live until you stop.

## Performance

**View > Performance...** shows how hard the computer works: the **audio** load (averaged, so it's steady) and its peak, **overloads** (clicks), the **disk**, and the **heaviest tracks**. The CPU meter in the control bar shows the same average, with a thin mark for the heaviest recent moment.

![Performance](img/performance.jpg)

If you hear clicks or dropouts:

1. Set **Process buffer range** to **Large** (Settings).
2. Raise the **I/O buffer size** in Audio Settings (256, 512 or 1024).
3. **Freeze** the heaviest tracks.
4. Turn on **Mix > Suspend Plug-ins When Silent**.
5. Put reverbs on a **bus** instead of on every track.
6. Close other programs (web browsers especially).

## Eco Mode

**View > Eco Mode** is for slower computers or a laptop on battery: meters and the playhead are drawn 15 times a second instead of 30, they stop when the app isn't in front, and other drawing is lighter - so more of the computer goes to the audio. In Settings you can choose **When on battery** so it turns on by itself when you unplug a laptop.

## Quick Help

**View > Quick Help** (the **?** key) - a box that explains whatever is under the mouse. Leave it on while you learn the app.
