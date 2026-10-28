# Dynamics Effects

Dynamics effects control the difference between loud and quiet. **How to use them:** a compressor evens a vocal or bass so it sits steady - lower the threshold until the gain reduction meter shows 3-6 dB on the loud parts, then raise the make-up gain. Slow attack keeps the punch; fast attack tames peaks. A gate cuts noise between notes; a de-esser tames harsh S sounds; a limiter stops peaks going over a ceiling (last on the master).

In this chapter: **Compressor**, **Limiter (simple)**, **Studio Compressor**, **Multipressor**, **DeEsser 2**, **Noise Gate**, **Expander**, **Enveloper**, **Transient Shaper**, **Limiter**, **Adaptive Limiter**.

## Compressor

![Compressor](img/fx-compressor.jpg)

The simple compressor: threshold, ratio, attack, release and make-up gain - all a vocal or a bass usually needs. The meter shows the gain reduction.

### Controls

**Threshold** (-60 to 0 dB) · **Ratio** (1 to 20 :1) · **Attack** (0.1 to 100 ms) · **Release** (10 to 1000 ms) · **Makeup** (0 to 24 dB)

## Limiter (simple)

![Limiter](img/fx-limiter.jpg)

The simple limiter: nothing goes past the ceiling; release sets how quickly it lets go.

### Controls

**Ceiling** (-12 to 0 dB) · **Release** (10 to 1000 ms)

## Studio Compressor

![Studio Compressor](img/fx-studioComp.jpg)

Studio Compressor: seven circuits (Platinum Digital, Studio VCA, Studio FET, Classic VCA, Vintage VCA, Vintage FET, Vintage Opto), each with its own detector, timing, knee and colour. Threshold, ratio, attack, release, knee, make-up and Auto Gain; a side-chain filter (LP, BP, HP, ParEQ, HS) with Listen, Max or Sum and Peak or RMS detection; on the way out a limiter, Soft/Hard/Clip distortion and a dry/wet Mix for parallel compression.

> **How to use it:** Vintage Opto for smooth vocals, Vintage FET for aggressive drums and rap vocals, Classic VCA for punchy drums and buses. Use Mix at 50% for parallel compression.

### Controls

**Circuit** (Platinum Digital, Studio VCA, Studio FET, Classic VCA, Vintage VCA, Vintage FET, Vintage Opto) · **Threshold** (-60 to 0 dB) · **Ratio** (1 to 30 :1) · **Attack** (0.05 to 200 ms) · **Release** (5 to 3000 ms) · **Knee** (0 to 1) · **Make Up** (-30 to 30 dB) · **Auto Gain** (Off, 0 dB, -12 dB) · **Filter Freq** (20 to 20000 Hz) · **Mix** (0 to 100 %) · **Distortion** (Off, Soft, Hard, Clip) · **Output Gain** (-30 to 30 dB) · **Limiter** (Off, On) · **Limiter Threshold** (-20 to 0 dB) · **Input Gain** (-30 to 30 dB) · **Auto Release** (Off, On) · **Detection** (Max, Sum) · **Peak/RMS** (Peak, RMS) · **Filter** (Off, On) · **Filter Mode** (LP, BP, HP, ParEQ, HS) · **Q** (0.1 to 10) · **Filter Gain** (-24 to 24 dB) · **Listen** (Off, On)

## Multipressor

![Multipressor](img/fx-multiband.jpg)

Multipressor: four bands split by three crossovers (phase-matched), each with its own compression (threshold, ratio), downward expansion (threshold, ratio, reduction), peak/RMS detection time, attack, release, make-up gain, on/off and solo; auto gain, lookahead and output.

> **How to use it:** On a mix or a bus: compress just the band that jumps out (boomy lows, harsh highs) and leave the rest alone.

### Controls

**Crossover 1** (30 to 1000 Hz) · **Crossover 2** (200 to 5000 Hz) · **Crossover 3** (1000 to 16000 Hz) · **Auto Gain** (Off, On) · **Lookahead** (0 to 20 ms) · **Out** (-24 to 24 dB) · **Low Comp Threshold** (-60 to 0 dB) · **Low Comp Ratio** (1 to 20 :1) · **Low Exp Threshold** (-100 to 0 dB) · **Low Exp Ratio** (1 to 10 :1) · **Low Exp Reduction** (0 to 60 dB) · **Low Peak/RMS** (0 to 100 ms) · **Low Attack** (0.1 to 200 ms) · **Low Release** (10 to 2000 ms) · **Low Gain** (-24 to 24 dB) · **Low Band** (Off, On) · **Low Solo** (Off, On) · **Low Mid Comp Threshold** (-60 to 0 dB) · **Low Mid Comp Ratio** (1 to 20 :1) · **Low Mid Exp Threshold** (-100 to 0 dB) · **Low Mid Exp Ratio** (1 to 10 :1) · **Low Mid Exp Reduction** (0 to 60 dB) · **Low Mid Peak/RMS** (0 to 100 ms) · **Low Mid Attack** (0.1 to 200 ms) · **Low Mid Release** (10 to 2000 ms) · **Low Mid Gain** (-24 to 24 dB) · **Low Mid Band** (Off, On) · **Low Mid Solo** (Off, On) · **High Mid Comp Threshold** (-60 to 0 dB) · **High Mid Comp Ratio** (1 to 20 :1) · **High Mid Exp Threshold** (-100 to 0 dB) · **High Mid Exp Ratio** (1 to 10 :1) · **High Mid Exp Reduction** (0 to 60 dB) · **High Mid Peak/RMS** (0 to 100 ms) · **High Mid Attack** (0.1 to 200 ms) · **High Mid Release** (10 to 2000 ms) · **High Mid Gain** (-24 to 24 dB) · **High Mid Band** (Off, On) · **High Mid Solo** (Off, On) · **High Comp Threshold** (-60 to 0 dB) · **High Comp Ratio** (1 to 20 :1) · **High Exp Threshold** (-100 to 0 dB) · **High Exp Ratio** (1 to 10 :1) · **High Exp Reduction** (0 to 60 dB) · **High Peak/RMS** (0 to 100 ms) · **High Attack** (0.1 to 200 ms) · **High Release** (10 to 2000 ms) · **High Gain** (-24 to 24 dB) · **High Band** (Off, On) · **High Solo** (Off, On)

## DeEsser 2

![DeEsser 2](img/fx-deesser.jpg)

DeEsser 2: a detection filter (high shelf or peak) at Frequency; Relative mode compares the band with the whole signal (steady whatever the level), Absolute with a fixed threshold; Max Reduction caps it; Split turns down just the band, Wide the whole signal; Filter Solo plays the band.

> **How to use it:** Find the S frequency (5-8 kHz for most voices) with Filter Solo, then lower the threshold until the S sounds are calm but not lisped.

### Controls

**Frequency** (2000 to 12000 Hz) · **Threshold** (-60 to 0 dB) · **Max Reduction** (0 to 24 dB) · **Range** (Split, Wide) · **Filter Solo** (Off, On) · **Mode** (Relative, Absolute) · **Filter** (Shelf, Peak)

## Noise Gate

![Noise Gate](img/fx-noiseGate.jpg)

Noise Gate: Gate (closes below the threshold) or Ducker (turns down while the key is loud); threshold, reduction, hysteresis, attack, hold, release, lookahead; the key's filter (band pass or band reject between its low and high cutoff) with Monitor to hear it.

> **How to use it:** Set the threshold just above the noise between notes. On a kick or snare mic, it cuts the spill of the other drums.

### Controls

**Threshold** (-80 to 0 dB) · **Reduction** (-100 to 0 dB) · **Attack** (0.01 to 100 ms) · **Hold** (0 to 500 ms) · **Release** (5 to 2000 ms) · **Hysteresis** (0 to 12 dB) · **Low Cutoff** (20 to 2000 Hz) · **High Cutoff** (500 to 20000 Hz) · **Mode** (Gate, Ducker) · **Lookahead** (0 to 20 ms) · **Filter** (Off, On) · **Characteristics** (Bandpass, Band Reject) · **Monitor** (Off, On)

## Expander

![Expander](img/fx-expander.jpg)

Expander: above the threshold the signal is made louder still (upward expansion, by the ratio) - or, Downward, below it quieter, down to the range; knee, attack, release, Peak or RMS detection, gain with Auto Gain, and an output clip (Off, Soft, Hard, Clip).

### Controls

**Threshold** (-80 to 0 dB) · **Ratio** (1 to 10 :1) · **Attack** (0.1 to 100 ms) · **Release** (10 to 2000 ms) · **Knee** (0 to 12 dB) · **Range** (-80 to 0 dB) · **Gain** (-24 to 24 dB) · **Direction** (Upward, Downward) · **Auto Gain** (Off, On) · **Peak/RMS** (Peak, RMS) · **Output Clip** (Off, Soft, Hard, Clip)

## Enveloper

![Enveloper](img/fx-enveloper.jpg)

Enveloper: turns the attack of each sound up or down, and its release up or down, whatever its level: the threshold sets what counts, Lookahead reads ahead, Release Time how long the tail lasts, Out Level the result.

### Controls

**Threshold** (-100 to 0 dB) · **Attack Gain** (-100 to 100 %) · **Attack Time** (1 to 200 ms) · **Release Gain** (-100 to 100 %) · **Release Time** (20 to 5000 ms) · **Lookahead** (0 to 20 ms) · **Out Level** (-24 to 24 dB)

## Transient Shaper

![Transient Shaper](img/fx-transient.jpg)

More or less attack and sustain, whatever the level (two envelope followers, compared).

> **How to use it:** More attack makes drums snap; less sustain makes them tight and dry.

### Controls

**Attack** (-100 to 100 %) · **Sustain** (-100 to 100 %) · **Attack Time** (1 to 50 ms) · **Release** (20 to 1000 ms) · **Output** (-24 to 24 dB)

## Limiter

![Limiter](img/fx-brickwall.jpg)

Limiter: looks ahead so peaks are caught before they arrive - nothing goes over the output level. Gain in, release, lookahead; Legacy (with an optional soft knee) or Precision (with optional true peak detection, catching the peaks between samples); a Warm character option.

> **How to use it:** Last on the master: set the output to -1 dB and raise the gain until it's as loud as you want, listening for the punch going away.

### Controls

**Gain** (0 to 24 dB) · **Output Level** (-12 to 0 dB) · **Release** (1 to 1000 ms) · **Character** (Clean, Warm) · **Lookahead** (0.1 to 10 ms) · **Mode** (Legacy, Precision) · **Soft Knee** (Off, On) · **True Peak Detection** (Off, On)

## Adaptive Limiter

![Adaptive Limiter](img/fx-adaptiveLimiter.jpg)

Adaptive Limiter: raises the level and rounds the peaks off like a well-driven analog stage, never above the ceiling; its release adapts (quick for transients, slower for dense music). Gain, Out Ceiling, Lookahead, Remove DC Offset, True Peak Detection.

> **How to use it:** A warmer, rounder limiter for the master: raise Gain, keep Out Ceiling at -1 dB.

### Controls

**Gain** (0 to 24 dB) · **Out Ceiling** (-12 to 0 dB) · **Lookahead** (0.5 to 20 ms) · **Remove DC Offset** (Off, On) · **True Peak Detection** (Off, On)
