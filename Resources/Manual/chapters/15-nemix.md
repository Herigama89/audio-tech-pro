# Nemix

**Nemix** is Audio Tech Pro's big synthesizer: three wavetable oscillators, a layer of **recorded instruments** (pianos, basses, guitars, drums, strings, brass, winds), a sub oscillator and noise, two filters, four envelopes, four LFOs, four macros, a 32-slot modulation matrix, an arpeggiator and eight built-in effects. It comes with hundreds of sounds for every genre - trap, drill, reggaeton, hip hop, R&B, reggae, afrobeats, jazz, rock, EDM, orchestra and more.

To use it, make an instrument track and choose **Nemix (wavetable synth)** from its instrument key. Its window opens with five pages: **SOUND**, **MATRIX**, **FX**, **ARP + VOICE** and **BROWSE**.

## Choosing a sound - BROWSE

![The BROWSE page](img/nemix-browse.jpg)

- **TYPE** - Bass, 808, Lead, Keys, Pluck, Piano, Organ, Pad, Strings, Brass, Winds, Vocal, Bells, Mallets, Guitar, Chords, Arp, Drums, Percussion, FX, and **Yours** (the sounds you saved).
- **STYLE** - the genre: Trap, Drill, Hip Hop, Reggaeton, Latin, Reggae, Dancehall, Afrobeats, Amapiano, R&B, Soul, Gospel, Jazz, Funk, Pop, Rock, Metal, EDM, House, Techno, Trance, Dubstep, Drum & Bass, Synthwave, Lo-Fi, Ambient, Cinematic, Orchestral.
- **Search** - type part of a name.
- Click a sound to load it. The **<** and **>** keys at the top step through sounds, so you can audition them while you play.

The full list of sounds is in the chapter *Nemix Sounds*.

## The SOUND page

![Nemix's SOUND page, part by part](img/nemix-sound.jpg)

1. **Logo**.
2. **< >** - the previous and next sound.
3. **INIT** - a plain starting sound, to build your own from scratch.
4. **Sound name**, its type and styles.
5. **SAVE** - save your sound (it appears under **Yours** in BROWSE).
6. **Pages** - SOUND, MATRIX, FX, ARP + VOICE, BROWSE.
7. **MASTER** - Nemix's output volume.
8. **Oscillator A**, 9. **Oscillator B**, 10. **Oscillator C** - three wavetable oscillators (see below).
11. **SAMPLE / SUB / NOISE** - the recorded instruments, a sub oscillator and noise (see below).
12. **Filter 1** and 13. **Filter 2**.
14. **Macros** - four knobs that can move many controls at once.
15. **Envelopes** - ENV 1 to 4.
16. **LFOs** - LFO 1 to 4.
17. **Other modulation sources** - VELO (velocity), NOTE (the key played), WHEEL (mod wheel), AFTER (aftertouch), BEND (pitch bend), RAND (random per note).

### The oscillators

Each oscillator plays a **wavetable**: a set of 32 waveforms you move through with **WT POS**. The picture shows the waveform; the menu chooses the table. There are 26: Basic Shapes, Pulse Width, Saw Harmonics, Square Harmonics, Harmonic Sweep, Vowels, FM Bells, Sync Sweep, Fold, Digital, Growl, Organ, Hollow, Bright Stack, Brass, Strings, Electric Piano, Choir, Spectral Noise, Phase Distortion, Reese, Bell Partials, Wobble, Sub Thump, Glass and Chip.

- **ON** - switches the oscillator on.
- **WT POS** - the position in the wavetable. Modulate it (with an LFO or envelope) for moving sounds.
- **UNISON** - up to 16 voices stacked; **DETUNE** spreads their pitch (the famous supersaw); **BLEND** balances the centre voice against the others; **WIDTH** spreads them across the stereo field.
- **OCTAVE**, **SEMI**, **FINE** - the pitch.
- **LEVEL** and **PAN**.
- **WARP** and **AMOUNT** - bends the waveform: Sync, Bend +, Bend -, PWM, Mirror, Quantize, FM, AM and Ring Mod (the last three use the next oscillator).
- **OUT** - which filter it goes to: Filter 1, Filter 2, Both, or Direct (no filter).

### Recorded instruments, sub and noise

The **SAMPLE** tab plays real recordings - the reason the acoustic sounds sound real:

![The recorded instrument layer](img/nemix-sample.jpg)

1. **ON** - switches the recorded layer on.
2. **Instrument** - choose from 34 recorded instruments, grouped by kind: Grand and Upright Piano, Rhodes and Electric Grand, six electric basses and an upright bass, nylon, steel, clean electric and hollow electric guitars, a rock drum kit, violins, violas, cellos (with pizzicato and tremolo), trumpet, French horn, trombone, tuba, flute, oboe, clarinet, bassoon, glockenspiel, marimba and harp.
3. **LEVEL**, 4. **PAN**, 5. **START** (start later in the recording, to soften the attack).
6. **SEMI** and **FINE** - tune it.
7. **Route** - *Direct* (straight out, as recorded) or through Filter 1, Filter 2 or both, so you can shape real instruments with the synth's filters and envelopes.

Recordings play with velocity layers (soft and hard notes really sound different) and round robins (repeated notes don't sound like a machine gun). The list of instruments and who recorded them is in *Recorded Instruments*.

The **SUB** tab adds a sub oscillator (Sine, Triangle, Saw, Square or Pulse, 0, -1 or -2 octaves) for weight. The **NOISE** tab adds noise (White, Pink, Vinyl, Metallic or Air) for breath, texture and crackle.

### The filters

**Filter 1** and **Filter 2** each have a type - Low Pass 12/24, Ladder, High Pass 12/24, Band Pass 12/24, Notch, Peak, Comb + and Comb - and Formant (vowel sounds) - with **CUTOFF**, **RESONANCE**, **DRIVE**, **KEY TRACK** (cutoff follows the notes), **MIX** and **PAN**. **Serial** runs Filter 1 into Filter 2; **Parallel** runs them side by side.

### Envelopes and LFOs

**ENV 1** shapes each note's volume; ENV 2 to 4 are free to modulate anything. Each has **ATTACK**, **HOLD**, **DECAY**, **SUSTAIN**, **RELEASE** and **CURVE**.

The **LFOs** wobble controls in a loop. Each has a shape (Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold, Smooth Random, Pluck, Ramp, Stairs), a rate (free, or in time with the song from 8 bars to 1/32), a mode (**Free**, **Retrigger** with each note, or **Envelope** - once only), **RATE**, **RISE** (fade in), **PHASE** and **SMOOTH**.

### Modulation: drag and drop

To make anything move, **drag a tile** - ENV 2, LFO 1, M1 (a macro), VELO, WHEEL... - by its dotted handle **onto any knob**. The knob shows its modulation as a coloured arc. **Right-click the knob** to set each modulation's amount (+100% to -100%) or remove it, or to reset the knob. Every connection also appears on the **MATRIX** page, where you can set the amount exactly.

## The MATRIX page

![The modulation matrix](img/nemix-matrix.jpg)

32 rows of **SOURCE > DESTINATION > AMOUNT**. Choose a source (envelopes, LFOs, velocity, note, mod wheel, aftertouch, pitch bend, macros, random), a destination (almost any knob), and drag the amount (negative values move it the other way). **X** clears a row.

## The FX page

![The FX page](img/nemix-fx.jpg)

Eight effects, each with **ON**:

- **DISTORTION** - Tube, Soft Clip, Hard Clip, Diode, Fold, Bitcrush, Downsample or Asymmetric; DRIVE, TONE, MIX.
- **FILTER** - low, high or band pass; CUTOFF, RESONANCE, MIX.
- **CHORUS** - RATE, DEPTH, DELAY, FEEDBACK, MIX.
- **PHASER** - RATE, DEPTH, FREQUENCY, FEEDBACK, MIX.
- **DELAY** - a time in note values (1/8 D, 1/4...), **PING-PONG** left and right; FEEDBACK, TONE, MIX.
- **MULTIBAND** - multiband compression for loud, upfront sounds: DEPTH, TIME, UPWARD, DOWNWARD, OUTPUT.
- **REVERB** - SIZE, DECAY, DAMPING, PRE-DELAY, WIDTH, MIX.
- **EQ** - LOW and HIGH shelves with their frequencies.

## The ARP + VOICE page

![The ARP + VOICE page](img/nemix-arp.jpg)

**ARPEGGIATOR** - hold a chord and it plays the notes in turn, in time with the song. **MODE** (Up, Down, Up/Down, Random, As Played, Chord), **RATE**, **OCTAVES** (1 to 4), **GATE** (note length) and **SWING**.

**VOICING** - **VOICES** (how many notes at once), **GLIDE** (notes slide into each other), **BEND** (pitch bend range), **VELOCITY** (how much velocity changes the sound), **MONO** and **LEGATO**. Mono + Legato + Glide gives the sliding 808s and leads of trap and drill.

## Recipes

- **Trap 808**: BROWSE > 808 > "808 Long Slide". Play overlapping notes for slides; raise GLIDE for longer ones.
- **Realistic piano ballad**: "Grand Piano", then on FX turn on REVERB with SIZE about halfway.
- **Supersaw lead**: "Supersaw Lead"; raise UNISON and DETUNE for a wider, thicker sound.
- **Your own pluck**: INIT, set ENV 1 SUSTAIN to 0 and DECAY short, drag ENV 2 onto Filter 1 CUTOFF.
