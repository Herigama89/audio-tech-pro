# Delay Effects

Delays repeat the sound: echoes, slapback, ping-pong, rhythmic patterns. **How to use them:** put a delay on an aux bus and send to it; set the time to a note value (1/4, 1/8 dotted) so the echoes follow the tempo; use low and high cut on the echoes so they don't clutter the mix.

In this chapter: **Delay**, **Stereo Delay**, **Tape Delay**, **Modulation Delay**, **Echo**, **Sample Delay**, **Delay Designer**.

## Delay

![Delay](img/fx-delay.jpg)

The simple delay: a time, how much of each echo comes back (feedback) and the dry/wet mix.

### Controls

**Time** (1 to 2000 ms) · **Feedback** (0 to 0.95) · **Mix** (0 to 1)

## Stereo Delay

![Stereo Delay](img/fx-stereoDelay.jpg)

Stereo Delay: each side its own input (Off, Left, Right, L+R, L-R), time or note with deviation, feedback with phase, crossfeed to the other side with phase; the routing presets (Straight, Crossfeed, 90/10, 10/90, Ping Pong, Pan, Rotate) or Customized; low and high cut on the echoes; output level per side, width, dry/wet mix.

> **How to use it:** A classic vocal throw: 1/4 on the left and 1/8 dotted on the right, feedback 20-30%, low and high cut on.

### Controls

**Sync** (Off, On) · **Left Time** (1 to 4000 ms) · **Right Time** (1 to 4000 ms) · **Left Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Right Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Left Feedback** (0 to 100 %) · **Right Feedback** (0 to 100 %) · **Crossfeed L to R** (0 to 100 %) · **Low Cut** (10 to 2000 Hz) · **High Cut** (1000 to 20000 Hz) · **Mix** (0 to 100 %) · **Width** (0 to 100 %) · **Crossfeed R to L** (0 to 100 %) · **Left Feedback Phase** (Normal, Inverted) · **Right Feedback Phase** (Normal, Inverted) · **Crossfeed Phase** (Normal, Inverted) · **Left Input** (Off, Left, Right, L + R, L - R) · **Right Input** (Off, Left, Right, L + R, L - R) · **Left Deviation** (-50 to 50 %) · **Right Deviation** (-50 to 50 %) · **Left Output** (0 to 150 %) · **Right Output** (0 to 150 %) · **Routing** (Customized, Straight, Crossfeed, 90/10, 10/90, Ping Pong L/R, Pan L/R, Rotate L/R)

## Tape Delay

![Tape Delay](img/fx-tapeDelay.jpg)

Tape Delay: time or note with deviation, feedback, Freeze, low and high cut in the loop, the tape's LFO and flutter (each a rate and an intensity) evened out by Smoothing, a clip threshold for the tape's saturation, Clean or Diffuse head, spread, dry and wet.

> **How to use it:** Warm, wobbly echoes for reggae, dub and lo-fi. Raise feedback to over 100% briefly for a runaway dub echo.

### Controls

**Tempo Sync** (Off, On) · **Delay Time** (1 to 4000 ms) · **Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Deviation** (-50 to 50 %) · **Feedback** (0 to 110 %) · **Freeze** (Off, On) · **Low Cut** (10 to 2000 Hz) · **High Cut** (1000 to 20000 Hz) · **Smoothing** (0 to 100 %) · **Clip Threshold** (0 to 100 %) · **Spread** (0 to 100 %) · **Tape Head Mode** (Clean, Diffuse) · **LFO Rate** (0.05 to 10 Hz) · **LFO Intensity** (0 to 100 %) · **Flutter Rate** (0.5 to 20 Hz) · **Flutter Intensity** (0 to 100 %) · **Dry** (0 to 100 %) · **Wet** (0 to 100 %)

## Modulation Delay

![Modulation Delay](img/fx-modDelay.jpg)

Modulation Delay: a short delay swept by two LFOs (left and right rates, or linked, mixed), with feedback; De-Warble keeps the pitch steady (the sweep moves between two fixed taps instead of bending the delay), Constant Mod keeps the sweep's width the same at any rate, D-Mode's spatial filtering; stereo phase and its distribution, a low/high cut filter, volume modulation, mix.

### Controls

**Time** (0.5 to 50 ms) · **Feedback** (-95 to 95 %) · **LFO 1 Rate** (0.02 to 10 Hz) · **Mod Intensity** (0 to 100 %) · **Phase** (-180 to 180 °) · **Output Mix** (0 to 100 %) · **De-Warble** (Off, On) · **Constant Mod** (Off, On) · **D-Mode** (Off, On) · **LFO 2 Rate** (0.02 to 10 Hz) · **Link L & R** (Off, On) · **LFO Mix** (0 to 100 %) · **Distribution** (Circular, Left/Right, Random) · **Filter** (Off, On) · **Low Cut** (20 to 2000 Hz) · **High Cut** (1000 to 20000 Hz) · **Volume Mod** (0 to 100 %)

## Echo

![Echo](img/fx-echo.jpg)

A simple echo: time (or a note), repeats and a colour knob from dark to bright.

### Controls

**Sync** (Off, On) · **Time** (1 to 4000 ms) · **Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Repeats** (0 to 100 %) · **Color** (-100 to 100 %) · **Wet** (0 to 100 %) · **Dry** (0 to 100 %)

## Sample Delay

![Sample Delay](img/fx-sampleDelay.jpg)

Delays each side by a number of samples (to line up mics, or for the Haas widening trick).

### Controls

**Left** (0 to 10000 smp) · **Right** (0 to 10000 smp) · **Link L & R** (Off, On)

## Delay Designer

![Delay Designer](img/fx-delayDesigner.jpg)

Delay Designer: up to 26 taps (A to Z), each with its own time, level, pan, transposition, filter, flip and mute; taps can snap to a tempo grid with swing, and one tap feeds back.

> **How to use it:** Draw taps for rhythmic delays, each with its own pan, pitch and filter - great for glitchy and spacious effects.

### Controls

**Tap** (A, B, C, D, E, F, G, H, I, J, ...) · **Taps** (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, ...) · **Sync** (Off, On) · **Grid** (1/4, 1/8, 1/16, 1/32, 1/4 T, 1/8 T, 1/16 T, 1/32 T) · **Swing** (50 to 100 %) · **Feedback** (Off, On) · **Feedback Tap** (A, B, C, D, E, F, G, H, I, J, ...) · **Feedback Level** (0 to 100 %) · **Dry** (0 to 100 %) · **Wet** (0 to 100 %) · **View** (Cutoff, Reso, Transp, Pan, Level)

Each tap (26, A to Z): Delay, Level, Pan, Transpose, Fine, Filter, HP Cutoff, LP Cutoff, Slope, Reso, Flip, Mute
