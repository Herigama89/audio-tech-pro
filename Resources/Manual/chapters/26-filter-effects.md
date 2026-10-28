# Filter Effects

Filters remove frequencies above or below a cutoff, often moving: sweeps, wahs, rhythmic gating. **How to use them:** automate the cutoff for builds and drops, or sync the movement to the song.

In this chapter: **AutoFilter**, **Auto Wah**, **Fuzz-Wah**, **Spectral Gate**, **Formant Filterbank**, **Step Filter**.

## AutoFilter

![AutoFilter](img/fx-autoFilter.jpg)

AutoFilter: a resonant filter (HP, BP, LP, or a 4-pole low pass of 6 to 24 dB/oct) with Fatness and stereo Spread; its cutoff moved by an ADSR envelope (started above the threshold, scaled by Dynamic) and by an LFO (six waves, pulse width, rate or synced with phase, stereo phase, retrigger); distortion before and after the filter; dry signal and main out.

> **How to use it:** Sync the LFO to 1/4 or 1/8 for a rhythmic filter; automate the cutoff for a build.

### Controls

**Filter Type** (HP, BP, LP, LP 6, LP 12, LP 18, LP 24) · **Cutoff** (30 to 18000 Hz) · **Resonance** (0 to 100 %) · **LFO Sync** (Off, On) · **LFO Note** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **LFO Rate** (0.02 to 20 Hz) · **LFO to Cutoff** (-100 to 100 %) · **Envelope to Cutoff** (-100 to 100 %) · **Attack** (0.5 to 2000 ms) · **Release** (10 to 5000 ms) · **Pre Filter Distortion** (0 to 100 %) · **Main Out** (-24 to 12 dB) · **Stereo Phase** (0 to 360 °) · **Filter** (Off, On) · **Fatness** (0 to 100 %) · **Spread** (0 to 2 oct) · **Envelope** (Off, On) · **Threshold** (-60 to 0 dB) · **Dynamic** (0 to 100 %) · **Decay** (1 to 5000 ms) · **Sustain** (0 to 100 %) · **LFO** (Off, On) · **Sync Phase** (0 to 360 °) · **Retrigger** (Off, On) · **Waveform** (Saw Down, Saw Up, Triangle, Pulse, S&H, Smooth) · **Pulse Width** (2 to 98 %) · **Distortion** (Off, On) · **Post Filter Distortion** (0 to 100 %) · **Pre Mode** (Soft, Hard, Tube) · **Post Mode** (Soft, Hard, Tube) · **Dry Signal** (0 to 100 %)

## Auto Wah

![Auto Wah](img/fx-autoWah.jpg)

An envelope wah: the louder you play, the higher the peak sweeps.

### Controls

**Sensitivity** (0 to 36 dB) · **Range Low** (100 to 1000 Hz) · **Range High** (500 to 6000 Hz) · **Resonance** (0 to 100 %) · **Attack** (0.5 to 100 ms) · **Release** (10 to 1000 ms) · **Mode** (Band Pass, Low Pass) · **Mix** (0 to 100 %)

## Fuzz-Wah

![Fuzz-Wah](img/fx-fuzzWah.jpg)

Fuzz-Wah: a wah (Classic, Retro, Modern, Opto 1 and 2, Resonant LP/HP, Peak) moved by the playing's level (depth, attack, release, range) with relative Q and auto gain; a compressor (ratio); a fuzz (gain, tone); and the order the three come in.

### Controls

**Wah** (Off, On) · **Wah Type** (Classic Wah, Retro Wah, Modern Wah, Opto Wah 1, Opto Wah 2, Resonant LP, Resonant HP, Peak) · **Auto Gain** (Off, On) · **Relative Q** (0 to 100 %) · **Depth** (0 to 100 %) · **Attack** (0.5 to 200 ms) · **Release** (10 to 2000 ms) · **Range Low** (100 to 1000 Hz) · **Range High** (600 to 5000 Hz) · **Compressor** (Off, On) · **Ratio** (1 to 10 :1) · **Fuzz** (Off, On) · **Fuzz Gain** (0 to 48 dB) · **Tone** (0 to 100 %) · **Order** (Wah > Fuzz, Fuzz > Wah) · **Output** (-24 to 12 dB)

## Spectral Gate

![Spectral Gate](img/fx-spectralGate.jpg)

Spectral Gate: a band (centre frequency and bandwidth, both wobbled at Speed by CF and BW Modulation) split from the rest; the band's parts above the threshold (Super Energy) and below it (Sub Energy) at their own levels; the original lows and highs either side mixed back; gain.

### Controls

**Threshold** (-80 to 0 dB) · **Speed** (0 to 10 Hz) · **CF Modulation** (0 to 100 %) · **BW Modulation** (0 to 100 %) · **Center Freq** (50 to 15000 Hz) · **Bandwidth** (50 to 10000 Hz) · **Low Level** (0 to 100 %) · **Super Energy** (0 to 200 %) · **Sub Energy** (0 to 200 %) · **High Level** (0 to 100 %) · **Gain** (-24 to 24 dB)

## Formant Filterbank

![Formant Filterbank](img/fx-formantBank.jpg)

Formant Filterbank: two banks of up to 20 band-pass filters between the lowest and highest frequency, each band's level set in bank A and bank B, crossfaded (Fade AB); formant shift, resonance, slope, the lowest band band-pass or high-pass and the highest band-pass or low-pass; a Shift LFO and a Fade LFO (rate, wave, intensity); overdrive, level, stereo width.

### Controls

**Low Frequency** (20 to 2000 Hz) · **High Frequency** (1000 to 18000 Hz) · **Formant Shift** (-100 to 100 %) · **Bands** (5, 6, 7, 8, 9, 10, 11, 12, 13, 14, ...) · **Lowest** (Bandpass, Highpass) · **Highest** (Bandpass, Lowpass) · **Resonance** (0 to 100 %) · **Slope** (12 dB, 24 dB) · **Fade AB** (0 to 100 %) · **Shift LFO Intensity** (0 to 100 %) · **Shift LFO Rate** (0.02 to 20 Hz) · **Shift LFO Wave** (Saw Down, Saw Up, Triangle, Square, S&H, Smooth) · **Fade LFO Intensity** (0 to 100 %) · **Fade LFO Rate** (0.02 to 20 Hz) · **Fade LFO Wave** (Saw Down, Saw Up, Triangle, Square, S&H, Smooth) · **Overdrive** (Off, On) · **Level** (-24 to 12 dB) · **Stereo Width** (0 to 100 %)

Bank A (20, 1 to 20): Level

Bank B (20, 1 to 20): Level

## Step Filter

![Step Filter](img/fx-stepFilter.jpg)

Eight steps of cutoff, one per note value, gliding or jumping, in time with the song.

### Controls

**Step 1** (0 to 100 %) · **Step 2** (0 to 100 %) · **Step 3** (0 to 100 %) · **Step 4** (0 to 100 %) · **Step 5** (0 to 100 %) · **Step 6** (0 to 100 %) · **Step 7** (0 to 100 %) · **Step 8** (0 to 100 %) · **Step Length** (1/64, 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, ...) · **Range Low** (30 to 2000 Hz) · **Range High** (500 to 18000 Hz) · **Resonance** (0 to 100 %) · **Glide** (0 to 200 ms) · **Mix** (0 to 100 %)
