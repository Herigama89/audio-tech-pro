tab: More
title: Troubleshooting
---
# Troubleshooting

## The Mac says the app "can't be opened" or "is damaged"

macOS blocks apps that aren't from the App Store. Open **Terminal** and paste this one line, press Return, and type your password (nothing shows while you type):

```
APP="/Applications/Audio Tech Pro.app"; sudo xattr -cr "$APP"; sudo codesign --force --deep --sign - "$APP"; open "$APP"
```

Do it once after each new version.

## No sound

- Click **Audio** (top right) and check your interface is chosen. The status bar says "No audio device open" when none is.
- Check the **master volume** and the track's **mute** and **solo**.
- On an instrument track, check an instrument is chosen and your MIDI keyboard is connected (play and watch the track's meter).
- On Windows, choose the interface's **ASIO** driver.

## I can't hear myself while recording

Turn on **I** (input monitoring) on the track, or use your interface's direct monitoring. If you hear yourself late, lower the buffer size.

## Clicks, pops and dropouts

Raise the buffer size, freeze heavy tracks, and see **View > Performance...** (see *Settings and Performance*).

## Recordings are out of time

Run **Calibrate Recording Latency...** once for your interface.

## The project can't find its audio

Use **File > Find Missing Audio Files...** and choose the folder where the files are now. To avoid it, use **File > Consolidate Project...** or **Save a Copy As...** before moving a project.

## A plugin doesn't appear

Run **Plug-in Manager...** to scan again. Check the format filter in **Mix > Plugin Formats in Menus**.

## Nemix's recorded instruments are silent

The Nemix Library is installed with the app. If it was removed, reinstall the app, or put the **Nemix Library** folder in your Music folder under *Audio Tech Pro*.

## Something went wrong

`Cmd/Ctrl+Z` undoes almost anything. **File > Revert to Backup...** opens an earlier save.
