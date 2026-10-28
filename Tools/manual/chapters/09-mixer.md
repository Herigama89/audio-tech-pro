tab: Mix
title: The Mixer
---
# The Mixer

Press `X` to show the mixer under the tracks, or `Cmd/Ctrl+2` for the mixer in its own window (resize it or put it on a second screen). Each track has a **channel strip**; aux buses are to the right, and the **master** is at the far right.

![The mixer](img/mixer.jpg)

1. **Which strips to show** - every strip, or only one kind (audio, instruments, buses...).
2. **Narrow** - thinner strips, to see more tracks.
3. **EQ/Dyn** - small pictures of each strip's EQ and dynamics.
4. **Bus Comp** - the bus compressor on the master (see *Mixing Tools*).
5. **Automix** - a balanced starting mix (see *Mixing Tools*).
6. **Comments** - a notes field under each strip.
7. **Zones** - put the buses and summing channels on the left or right side of the mixer, where they stay while the tracks scroll (right-click a channel's name for its own zone).
8. **Console** - the channel strip on every channel: the built-in one, or a channel-strip plugin you own.
9. **Close** the mixer.
10. **A track's channel strip** - part by part in the next picture.
11. **Aux (bus) strip** - receives sends; put a reverb or delay here.
12. **Master strip** - everything you hear. Its **MM** key opens the MultiMeter.

## A channel strip, part by part

![One channel strip](img/channel-strip-parts.jpg)

1. **Track number and On/Off** - switch the whole channel off.
2. **EQ** - the channel strip's EQ and dynamics, lit when they're working. Click to open the full channel strip.
3. **Pan** - left and right. Drag up/down; double-click for centre.
4. **Mute and Solo**.
5. **Setting** - copy, paste or reset this channel strip; save it as a patch or load one.
6. **Input** - what the track records or plays (an interface input, or the instrument). The round key beside it switches mono (one circle) and stereo (two).
7. **Inserts** - effects on this track, played top to bottom. Click an empty slot to add an effect, or click a used slot to open it. Right-click a slot to bypass or remove it.
8. **Sends** - send some of the track to an aux bus (for a shared reverb or delay). Click to choose the bus; the knob sets how much (`Option/Alt`-click or double-click for -20 dB).
9. **Output** - where the track goes: the Stereo Out (St Out), a bus, or other outputs of your interface (Output 3-4 and so on).
10. **Preamp (PRE)** - a class-A transformer console preamp. Its gain (0 to +30 dB) turns the channel up, and the harder it's driven the more it colours: thicker lows, warm harmonics, a softer top. Pull the fader down to taste.
11. **Tape** - runs the channel through an analog tape machine: saturation, the low-end bump, the softened top. The knob is the drive (double-click for 35%); right-click TAPE for the tape speed.
12. **Record, Input monitor, Automation mode** - R, I, and the mode (Read, Touch, Latch, Write, Off).
13. **Fader** - the track's level. `Option/Alt`-click for 0 dB.
14. **Level meter** - with the highest peak held. Red means clipping.
15. **Fader level** - in dB.
16. **Track name and colour**.

## Your interface's preamps

With an interface whose preamps are set by its maker's control app, and that app installed and running, an audio track's strip also shows the **interface's preamp gain** for its input - drag it up or down; right-click for the high-pass filter, the pad and Auto Gain - and a **48 V** key for phantom power (for condenser mics). It all happens on your computer; nothing goes over the network.

## Setting up a reverb or delay bus

1. **Track > New Aux Bus** (or choose a new bus in any strip's **Send** slot).
2. On the bus, add **Studio Reverb** or **Space Designer** (reverb) or **Stereo Delay** (delay), with its mix at 100% wet.
3. On each track, turn up the send knob for that bus: more send, more reverb.

One reverb shared by many tracks sounds more natural, and uses far less CPU, than one on every track.

## Signal flow

**Mix > Signal Flow** shows the selected track's path from its input to the main output, with every stage: input, preamp, channel strip, inserts, sends, fader, pan, tape, the bus or master.

## Groups and VCAs

- **Groups** - link the faders, mutes, solos or record keys of several tracks (**Mix > Group Settings...**).
- **VCA** - one fader that moves several tracks, keeping their balance (**Track > New VCA**).
- **Exclusive Solo** (Mix menu) - soloing a track unsolos the others.
