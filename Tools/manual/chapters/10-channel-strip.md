tab: Mix
title: The Channel Strip
---
# The Channel Strip

Every channel has a full console channel strip built in - input, de-esser, compressor, gate/expander, filters, a four-band EQ and routing - before its insert effects. Click the **EQ** key at the top of a mixer strip to open it. It looks and works like a classic British console channel: turn the knobs (drag up and down; the value shows while you drag), click the keys - each key's lamp shows when it's on.

![The built-in channel strip](img/channel-strip.jpg)

## Its parts, left to right

- **INPUT** - **TRIM** (-20 to +20 dB). **LINE** on (the usual: recorded and playing tracks) uses just the trim; turn it off for the **MIC** preamp: its gain (+20 to +70: 0 to 50 dB more) and its colour as you drive it. **PAD** takes 20 dB off; **Ø** turns the polarity over (for a mic picking up the other side of a drum, or two mics out of phase). **48V** only lights its lamp: no microphone here needs power.
- **DE-ESS** - **IN** switches the de-esser in; the knob sets its threshold. Loud sibilance (the "s" and "t" sounds, above about 5 kHz) is turned down there and only there; the lamps (3, 6, 10) show by how much.
- **COMP** - **T/HOLD** (threshold: turn right to compress more; all the way left is off, and make-up gain comes with it), **RATIO** (all the way left: Auto, following the threshold; up to infinite, a limiter), **REL** (release). The curve key gives a **soft knee**, **FST ATK** a fast attack, **LIN REL** a linear release (it recovers evenly, as on the console). **DYN IN** switches the compressor and gate in.
- **The meters** in the middle - the top row lights with the compressor's gain reduction (3 to 20 dB), the row under it with the gate's.
- **GATE** - **T/HOLD** (threshold: below it the sound is turned down; all the way left is off), **RANGE** (by how much), **REL** (how fast it closes), **EXP** for a gentler expander instead of a gate, **FST ATK** for a faster opening (drums).
- **FILTERS** - the **low pass** (top knob, 12 to 3 kHz; all the way left: out) and the **high pass** (20 to 500 Hz: removes rumble and low mud; 80 Hz suits vocals and guitars; all the way left: out). Their lamps light while they're in. **DYN S/C** puts the filters in the dynamics' side chain only: the compressor and gate hear the filtered sound (a high pass there stops the bass pumping the compressor) but you hear the full sound. **INPUT** on (the usual) puts them first; off, after the EQ.
- **EQ** - four bands, each with gain (- to +) and frequency:
  - **LF** (30 to 450 Hz, a shelf or a **BELL**) - weight and boom.
  - **LMF** (200 Hz to 2.5 kHz, with **Q**, its width) - body, boxiness, mud.
  - **HMF** (600 Hz to 7 kHz, with **Q**) - presence, harshness.
  - **HF** (1.5 to 16 kHz, a shelf or a **BELL**) - air and brightness.
  - **BLK** - the black-knob EQ: the bells get narrower the more you boost or cut, and the shelves a little sharper - more focused, more forward. **EQ IN** switches the EQ in. **DYN S/C** puts the EQ in the side chain only: the dynamics react to the frequencies you boost (boost the highs there and it de-esses; boost the kick's low end and it pumps with the kick) while you hear the sound un-EQ'd.
- **ROUTE** - **POST EQ DYN**: the dynamics after the EQ (the usual); off, before it, so the EQ comes after the compressor. **POST EQ DE ESS** - the same for the de-esser. **DYN S/C EXT**: another track's sound keys the compressor and gate (a bass ducking under the kick, music under a voice) - choose the track with **Key** under the panel. **LINK**: the two sides compressed together (the usual); off, each by its own level.
- **OUT** - the output level (-20 to +20 dB), with the meter beside it (24 is the top: keep it out of the red). **MTR INP** shows the input level on it instead.
- **INSERT** - **IN** switches the track's insert plug-ins in or out (they stay loaded). **OP** on (the usual) puts them after the channel strip; off, before it.
- **Power** (the round key) - the whole strip in or out.
- **Under the panel** - the sound's spectrum with the EQ curve over it, as on the other EQs. Choose the view (bars or line), the resolution, the range (dB), the speed and how long peaks hold.

## How to use it

1. Start with the **high pass**: raise it until the sound gets thin, then back off a little.
2. **Cut before you boost**: find an ugly frequency by boosting a narrow band and sweeping, then cut it.
3. Turn the compressor's **T/HOLD** until the top row of lamps lights 3 to 6 on the loud parts.
4. Use **EQ IN** and **DYN IN** to compare with and without - it should sound better, not just louder.

## The console

**Console** at the top right of the mixer chooses the channel strip on every channel: **Built-in**, or a channel-strip plugin you own (any of your plugins). The same strip then appears on every track, as on a real console. **Mix > Reset Channels...** puts every strip back to a fresh start.

## Smart Controls

**Smart Controls** (`B`) show eight knobs for the selected track's sound under the tracks: its volume, pan, instrument (cutoff, envelope...) and channel strip EQ. **Right-click a knob** to choose another parameter for it - any control of the track's plugins. **Default Mapping** puts them back.

![Smart Controls](img/smart-controls.jpg)
