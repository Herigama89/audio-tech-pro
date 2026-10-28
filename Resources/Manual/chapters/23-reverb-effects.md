# Reverb Effects

Reverb puts the sound in a space - a room, a hall, a plate. **How to use it:** put one or two reverbs on aux buses and send tracks to them. Short rooms add life without distance; long halls and plates make big, smooth tails. Predelay (20-60 ms) keeps a vocal clear in front of its reverb; cut the reverb's lows to avoid mud.

In this chapter: **Reverb**, **Studio Reverb**, **Plate Reverb**, **Spring Reverb**, **Space Designer**, **PlatinumVerb**, **EnVerb**, **SilverVerb**, **Room Simulator**.

## Reverb

![Reverb](img/fx-reverb.jpg)

The simple reverb: room size, damping (darker tail), stereo width and mix.

### Controls

**Size** (0 to 1) · **Damping** (0 to 1) · **Width** (0 to 1) · **Mix** (0 to 1)

## Studio Reverb

![Studio Reverb](img/fx-studioReverb.jpg)

Studio Reverb: an algorithmic reverb with eight room types (Room, Chamber, Concert Hall, Theater, Synth Hall, Digital, Dark Room, Dense Room). Size, decay, predelay (or a note), attack, distance, density, modulation, width and the early/late balance; a four-band decay EQ (how long each range rings), Freeze, a mono maker for the lows and a five-band output EQ.

> **How to use it:** Choose a Room for drums, a Plate or Chamber for vocals, a Hall for strings and pads. Turn down the low decay for a cleaner mix.

### Controls

**Room Type** (Room, Chamber, Concert Hall, Theater, Synth Hall, Digital, Dark Room, Dense Room, Smooth Space, Vocal Hall, ...) · **Size** (0 to 100 %) · **Decay** (0.1 to 20 s) · **Predelay** (0 to 2000 ms) · **Low Cut** (10 to 1000 Hz) · **High Cut** (1000 to 20000 Hz) · **Density** (0 to 100 %) · **Mod Depth** (0 to 100 %) · **Width** (0 to 200 %) · **Wet** (0 to 100 %) · **Freeze** (Off, On) · **Attack** (0 to 100 %) · **Distance** (0 to 100 %) · **Dry** (0 to 100 %) · **Predelay Sync** (Off, On) · **Predelay Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Decay Sync** (Off, On) · **Decay Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Low Freq** (20 to 1000 Hz) · **Low Ratio** (0.1 to 1.9 x) · **Low Mid Freq** (100 to 5000 Hz) · **Low Mid Ratio** (0.1 to 1.9 x) · **Low Mid Q** (0.1 to 4) · **High Mid Freq** (500 to 15000 Hz) · **High Mid Ratio** (0.1 to 1.9 x) · **High Mid Q** (0.1 to 4) · **High Freq** (1000 to 20000 Hz) · **High Ratio** (0.1 to 1.9 x) · **Quality** (Low, High, Ultra) · **Mod Speed** (0.05 to 10 Hz) · **Mod Source** (Sine, Random, Noise) · **Smoothing** (0 to 100 %) · **Early/Late Mix** (0 to 100 %) · **Mono Maker** (Off, On) · **Mono Maker Freq** (20 to 1000 Hz) · **Output EQ** (Off, On) · **EQ Low Cut** (Off, On) · **EQ Low Shelf** (Off, On) · **Low Shelf Freq** (20 to 2000 Hz) · **Low Shelf Gain** (-24 to 24 dB) · **EQ Low Mid** (Off, On) · **Low Mid Freq** (50 to 10000 Hz) · **Low Mid Gain** (-24 to 24 dB) · **Low Mid Q** (0.1 to 10) · **EQ High Mid** (Off, On) · **High Mid Freq** (200 to 18000 Hz) · **High Mid Gain** (-24 to 24 dB) · **High Mid Q** (0.1 to 10) · **EQ High Shelf** (Off, On) · **High Shelf Freq** (1000 to 20000 Hz) · **High Shelf Gain** (-24 to 24 dB) · **EQ High Cut** (Off, On) · **View** (Main, Details)

## Plate Reverb

![Plate Reverb](img/fx-plateReverb.jpg)

The classic plate (Jon Dattorro's 1997 design): a figure-of-eight tank with modulated diffusers.

> **How to use it:** The smooth, bright reverb of countless vocal and snare sounds.

### Controls

**Pre-Delay** (0 to 200 ms) · **Decay** (0 to 100 %) · **Size** (0 to 100 %) · **Bandwidth** (0 to 100 %) · **Damping** (0 to 100 %) · **Modulation** (0 to 100 %) · **Low Cut** (10 to 1000 Hz) · **Width** (0 to 100 %) · **Mix** (0 to 100 %)

## Spring Reverb

![Spring Reverb](img/fx-springReverb.jpg)

A spring tank: a chain of all-passes spreads each sound into the spring's chirp, round a feedback loop.

### Controls

**Springs** (1, 2, 3) · **Tension** (0 to 100 %) · **Decay** (0 to 100 %) · **Dispersion** (0 to 100 %) · **Tone** (1000 to 10000 Hz) · **Drip** (0 to 100 %) · **Mix** (0 to 100 %)

## Space Designer

![Space Designer](img/fx-convSpace.jpg)

Convolution with an impulse response made from the knobs (built again, off the audio thread, when they move).

> **How to use it:** Real spaces from impulse responses: load your own IR (Load IR...) or shape one with the knobs.

### Controls

**Length** (0.1 to 20 s) · **Pre-Delay** (0 to 500 ms) · **Early Reflections** (0 to 100 %) · **Bright Start** (1000 to 20000 Hz) · **Bright End** (200 to 20000 Hz) · **Density** (0 to 100 %) · **Spread** (0 to 200 %) · **Reverse** (Off, On) · **Reverb** (0 to 100 %) · **Low Cut** (10 to 1000 Hz) · **IR Mode** (Sampled IR, Synthesized IR) · **Quality** (Lo-Fi, Low, Medium, High) · **Size** (25 to 200 %) · **Volume Env** (Off, On) · **Init Level** (0 to 100 %) · **Attack** (0 to 2000 ms) · **Decay** (0.05 to 20 s) · **End Level** (0 to 100 %) · **Decay Shape** (Lin, Exp) · **Filter Env** (Off, On) · **Filter Type** (LP 6dB, LP 12dB, BP, HP) · **Resonance** (0 to 100 %) · **Init Freq** (50 to 20000 Hz) · **Filter Attack** (0 to 2000 ms) · **Break Level** (50 to 20000 Hz) · **Filter Decay** (0.05 to 20 s) · **End Freq** (50 to 20000 Hz) · **Density Env** (Off, On) · **Init Density** (0 to 100 %) · **Ramp Time** (0 to 2000 ms) · **End Density** (0 to 100 %) · **Reflection Shape** (0 to 100 %) · **Output EQ** (Off, On) · **EQ Low Cut** (Off, On) · **Low Cut Freq** (20 to 2000 Hz) · **EQ Low Shelf** (Off, On) · **Low Shelf Freq** (20 to 2000 Hz) · **Low Shelf Gain** (-24 to 24 dB) · **EQ Low Mid** (Off, On) · **Low Mid Freq** (50 to 10000 Hz) · **Low Mid Gain** (-24 to 24 dB) · **Low Mid Q** (0.1 to 10) · **EQ High Mid** (Off, On) · **High Mid Freq** (200 to 18000 Hz) · **High Mid Gain** (-24 to 24 dB) · **High Mid Q** (0.1 to 10) · **EQ High Shelf** (Off, On) · **High Shelf Freq** (1000 to 20000 Hz) · **High Shelf Gain** (-24 to 24 dB) · **EQ High Cut** (Off, On) · **High Cut Freq** (1000 to 20000 Hz) · **Input** (Stereo, Mono, XStereo) · **Direct** (-60 to 6 dB) · **X-Over** (0 to 1000 Hz) · **View** (Volume Env, Filter Env, Density Env, Output EQ)

## PlatinumVerb

![PlatinumVerb](img/fx-platinumVerb.jpg)

PlatinumVerb: early reflections from a room of 3 to 7 corners, its size, the stereo base of the two microphones and an ER scale; then the tail - initial delay, spread, two bands (crossover, low ratio, low level), high cut, density, diffusion, reverb time - and the balance.

### Controls

**Predelay** (0 to 200 ms) · **Room Shape** (3, 4, 5, 6, 7) · **Room Size** (1 to 70 m) · **Stereo Base** (0 to 2 m) · **ER Scale** (50 to 200 %) · **Initial Delay** (0 to 200 ms) · **Spread** (0 to 200 %) · **Crossover** (50 to 2000 Hz) · **Low Ratio** (20 to 200 %) · **Low Freq Level** (-30 to 6 dB) · **High Cut** (200 to 20000 Hz) · **Density** (0 to 100 %) · **Diffusion** (0 to 100 %) · **Reverb Time** (0.3 to 20 s) · **Balance ER/Reverb** (-100 to 100 %) · **Dry** (0 to 100 %) · **Wet** (0 to 100 %)

## EnVerb

![EnVerb](img/fx-enVerb.jpg)

EnVerb: a reverb shaped by an envelope - attack, decay to the sustain level, hold, then release - started by each new phrase; the dry signal can be delayed; density, spread, high cut, a crossover with the low band's level, dry and wet.

### Controls

**Attack** (0 to 2000 ms) · **Decay** (0 to 3000 ms) · **Sustain** (0 to 100 %) · **Hold** (0 to 10000 ms) · **Release** (0.05 to 20 s) · **Dry Signal Delay** (0 to 500 ms) · **Density** (0 to 100 %) · **Spread** (0 to 200 %) · **High Cut** (200 to 20000 Hz) · **Crossover** (50 to 2000 Hz) · **Low Freq Level** (-30 to 6 dB) · **Dry** (0 to 100 %) · **Wet** (0 to 100 %)

## SilverVerb

![SilverVerb](img/fx-silverVerb.jpg)

SilverVerb: predelay, reflectivity, size, density/time, low and high cut, and an LFO (on/off, rate, left-right phase, intensity) that wobbles the tail; dry and wet.

### Controls

**Predelay** (0 to 200 ms) · **Reflectivity** (0 to 100 %) · **Size** (0 to 100 %) · **Density/Time** (0 to 100 %) · **Low Cut** (10 to 2000 Hz) · **High Cut** (1000 to 20000 Hz) · **Modulation** (Off, On) · **Rate** (0.05 to 10 Hz) · **Phase** (0 to 360 °) · **Intensity** (0 to 100 %) · **Dry** (0 to 100 %) · **Wet** (0 to 100 %)

## Room Simulator

![Room Simulator](img/fx-roomSim.jpg)

Room Simulator: a classic digital room algorithm (Classic or Modern). Reverb time, room size, density, predelay, the first reflections' level, delay and spread, high cut, a bass band (gain and crossover), correlation patterns for each side, Freeze, and the dry, early and reverb levels.

### Controls

**Model** (Classic, Modern) · **Reverb Time** (0.07 to 400 s) · **Freeze** (Off, On) · **Add** (Off, On) · **Clear** (Off, On) · **Mode** (Complex, Medium, Simple) · **Enhance** (Off, On) · **Low** (0.1 to 10 x) · **Low Xover** (50 to 1000 Hz) · **High** (0.1 to 10 x) · **High Xover** (1000 to 16000 Hz) · **Parameters** (Primary, Secondary, Extended) · **Room Size** (1 to 1000000 m³) · **Reverb Density** (0 to 200 %) · **Reverb Delay** (0 to 200 ms) · **Dry Level** (-60 to 0 dB) · **1st Reflection Level** (-60 to 0 dB) · **Reverb Level** (-60 to 0 dB) · **1st Reflection Delay** (0 to 200 ms) · **1st Reflection Spread** (0 to 50 ms) · **Reverb High Cut** (1000 to 20000 Hz) · **Bass Gain** (-12 to 12 dB) · **Bass Crossover** (50 to 1000 Hz) · **Reverb Correlation** (0 to 150 %) · **Dry Source** (L R, R L) · **1st Reflection Source** (L R, R L) · **L Correlation Pattern** (Off, Pattern 1, Pattern 2, Pattern 3, Pattern 4, Pattern 5, Pattern 6, Pattern 7, Pattern 8) · **R Correlation Pattern** (Off, Pattern 1, Pattern 2, Pattern 3, Pattern 4, Pattern 5, Pattern 6, Pattern 7, Pattern 8) · **Subsonic** (Off, On)
