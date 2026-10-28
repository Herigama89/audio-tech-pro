tab: Mix
title: Mastering and Exporting
---
# Mastering and Exporting

## Mastering with the Mastering Assistant

Mastering makes the finished mix sound polished and as loud as other songs.

1. On the **master strip**, add **Mastering Assistant** (Metering category) as the last insert.
2. Play the loudest part of the song (a chorus). It listens to the first 30 seconds you play after **Reanalyze**.
3. It sets a corrective **EQ** (Auto EQ, with how much of it), the **gain** to reach the loudness you choose, and a true peak limiter at -1 dBTP.
4. Choose a **character**: **Clean**, **Valve** (warm), **Punch** (tight, punchy) or **Transparent**.
5. Adjust **Excite** (upper-mid saturation, for presence), **Width** (stereo width above 150 Hz) and the custom **EQ** (low, mid, high).
6. Turn on **Loudness Compensation** to hear it at the original loudness, for a fair comparison.

The window shows the input and output levels, the EQ curve, the loudness against your target, the PLR (peak to loudness ratio - how much punch is left) and the stereo correlation.

![Mastering Assistant](img/fx-masteringAssistant.jpg)

### Loudness targets

- Streaming (Spotify, Apple Music, YouTube): about **-14 LUFS** integrated, true peak below -1 dBTP.
- Loud genres (trap, EDM, reggaeton): often **-9 to -7 LUFS** - louder, but less punch.

You can also master by hand: **Linear Phase EQ** or **Match EQ**, then **Multipressor**, **Saturator**, and **Limiter** or **Adaptive Limiter** last. Check with the **Loudness Meter** or **MultiMeter**.

## Exporting

- **File > Bounce Song to WAV...** (`Cmd/Ctrl+B`) - the song (or the cycle) to a WAV file, with a 2-second tail for reverbs.
- **File > Export All Tracks as Audio Files...** (`Cmd/Ctrl+Shift+E`) - stems: each track and bus to its own WAV, with its plugins, fader and pan.
- **File > Save a Copy As...** - the whole project with its audio, to send.
- **View > Record Screen...** - a video of the app with the mix's sound (see *Screen Recording*).
