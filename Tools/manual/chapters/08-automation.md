tab: Record & Edit
title: Automation
---
# Automation

**Automation** makes a control move by itself while the song plays: the vocal fader up in the chorus, a filter opening over a build, a delay throw on one word.

## Showing automation

- Press `A`, or click **A** on the track header, to show the selected track's automation lane. **Right-click A** to choose what to automate: volume, pan, mute, sends, or any control of the track's plugins and instrument.
- Or **right-click any knob** - on the mixer, a plugin or an instrument - and choose **Show Automation**.

## Drawing it

- **Click** on the line to make a point; **drag** points to move them.
- **Double-click** a point to remove it.
- With no automation, a dashed line shows the control's current value.
- Right-click a control > **Create Automation Point at Playhead**, or **Delete Automation**.

## Recording it (automation modes)

Each track has a mode - on its automation key, the mixer strip, or right-click a control > **Automation Mode**:

- **Off** - the automation is ignored.
- **Read** - the controls follow the automation (the usual mode).
- **Touch** - while you hold a control, your moves are recorded; let go and it returns to the automation.
- **Latch** - like Touch, but after you let go the control stays where you left it until playback stops.
- **Write** - records over everything from where you start. After one pass it turns into Latch, so the next play doesn't erase it.

Choose Touch or Latch, press play, and move the fader or knob with the mouse or your MIDI controller. Then set it back to Read.

## Tips

- **Smart Controls** and **macros** in Nemix can be automated too: one knob that moves several things at once.
- To change the whole level of an automated track, use **clip gain** on its regions or a **Gain** plugin, so the automation stays as it is.
