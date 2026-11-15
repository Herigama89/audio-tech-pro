#pragma once

#include "Studio.h"
#include "StudioDsp.h"
#include "SampleVoices.h"
#include "../nemix/Nemix.h"

namespace riff::studio
{
    //==============================================================================
    /** The drum kit (as Logic's Drum Kit Designer): the recorded kit's pieces, each with its own models, tuning,
        damping, tone and pan; kick, snare, toms, hats and cymbals on their own strips, and overheads and room mics. */
    class StudioDrums final : public StudioInstrument
    {
    public:
        StudioDrums();
        juce::StringArray screenLines() const override;
        /** Plays a piece (clicking it in the picture): the GM note. */
        void play (int note, int velocity) noexcept;

        static int numberOfPieces();
        static juce::String pieceName (int piece);
        static juce::String pieceId (int piece);
        static juce::String altName (int piece, int alt);
        static int noteOfPiece (int piece);
        /** A kit's settings (its pieces' models, damping, tuning, the room), to write to the track. */
        static std::vector<std::pair<juce::String, float>> kitSettings (int kit);

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;

    private:
        SampleVoices<48> voices;
        int kitIndex = -1;
        float hatPedal = 0.0f;
        juce::AudioBuffer<float> sum;
        dsp::Room room, plate;
        dsp::Svf ohFilter[2];
    };

    //==============================================================================
    /** The analog polysynth (in the style of the classic chorus-washed polys): a saw, a pulse with its width
        moved by the LFO, a sub an octave down and noise; a high pass, a resonant 24 dB low pass with its
        envelope and key follow; one envelope (or the gate) for the level; chorus I, II or both. */
    class PolySynth
    {
    public:
        struct Patch
        {
            float saw = 1, pulse = 0, pw = 0.5f, pwm = 0, sub = 0, noise = 0;
            float hpf = 0;   // 0..3 (as the stepped switch)
            float cutoff = 0.6f, res = 0.1f, envAmt = 0.3f, lfoAmt = 0, keyFollow = 0.5f;
            float a = 0.01f, d = 0.5f, s = 0.7f, r = 0.4f;
            float lfoRate = 3.0f, vibrato = 0;
            int chorus = 1;   // 0 off, 1 I, 2 II, 3 both
            bool gate = false;
            float level = 0.7f;
        };
        void prepare (double sampleRate);
        void setPatch (const Patch& p) noexcept { patch = p; }
        const Patch& getPatch() const noexcept { return patch; }
        void noteOn (int note, float velocity) noexcept;
        void noteOff (int note) noexcept;
        void allOff() noexcept;
        void pitchBend (float semis) noexcept { bend = semis; }
        void render (float* l, float* r, int n) noexcept;
        void setSustain (bool down) noexcept;

    private:
        struct Voice
        {
            int note = -1;
            bool held = false, sustained = false;
            double phase = 0.0, subPhase = 0.0;
            float velocity = 1.0f;
            dsp::Adsr env;
            dsp::Svf f1, f2;
            juce::uint64 age = 0;
            float glide = 0.0f;
            bool active() const noexcept { return env.active(); }
        };
        std::array<Voice, 16> voices;
        Patch patch;
        double rate = 48000.0;
        double lfoPhase = 0.0;
        juce::uint64 clock = 0;
        float bend = 0.0f;
        bool sustain = false;
        juce::Random noise;
        dsp::Chorus chorusA, chorusB;
        dsp::Svf hp[2];
    };

    //==============================================================================
    /** The keys workstation: programs of the recorded pianos, electric pianos, strings, brass, winds, mallets and
        guitars; the polysynth's patches; and Nemix's synths - with a layer and a split, the pedals (sustain with
        half pedal, sostenuto, soft) and the piano's lid, hammer, release and resonance. */
    class StudioKeys final : public StudioInstrument
    {
    public:
        StudioKeys();
        ~StudioKeys() override;
        juce::StringArray screenLines() const override;

        struct Program
        {
            juce::String name, category;
            int type = 0;             // 0 recorded instrument, 1 polysynth, 2 Nemix sound
            juce::String source;      // the library instrument's id, or the Nemix preset's name
            PolySynth::Patch patch;   // (polysynth)
            float tone = 0.0f, release = 0.0f, tune = 0.0f;   // its voicing on top
            float room = 0.25f;
        };
        static const std::vector<Program>& programs();
        static juce::StringArray categories();

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;
        void paramChanged (int index) noexcept override;

    private:
        struct Layer
        {
            int program = -1;
            int libraryIndex = -1;
            PolySynth poly;
            std::shared_ptr<Nemix> nemix;
            std::atomic<int> wanted { -1 };   // a program asked for (applied on the next block, audio thread)
        };
        void startNote (Layer& layer, int note, int velocity, float gainDb) noexcept;
        void stopNote (Layer& layer, int note) noexcept;
        void applyProgram (Layer& layer, int program);   // (message thread: Nemix's sound, the library's loading)
        bool inRange (int layerIndex, int note) const noexcept;

        std::array<Layer, 2> layers;   // the main program and the layer (or the split's other side)
        SampleVoices<64> voices;
        std::array<bool, 128> down {}, sostenutoHeld {};
        bool sustainDown = false, sostenutoDown = false, softDown = false;
        float pedal = 0.0f;
        juce::AudioBuffer<float> work;
        dsp::Room room, hall;
        dsp::Tilt lid[2];
        dsp::Svf hammerF[2];
        double clockPos = 0.0;
        juce::MidiBuffer layerMidi[2];
    };

    //==============================================================================
    /** Bass (as Logic's Bass Designer): the recorded basses, 4 or 5 strings, the pickups' blend, the playing
        styles (finger, pick, slap, mute, slide, harmonics, ghost notes, hold), active or passive, a DI and an amp. */
    class StudioBass final : public StudioInstrument
    {
    public:
        StudioBass();
        juce::StringArray screenLines() const override;
        static juce::StringArray models();
        static juce::StringArray styles();
        /** Where a note sits on the neck: its string (0 the lowest) and fret, for the picture. */
        std::pair<int, int> stringAndFret (int note) const noexcept;

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;
        void paramChanged (int index) noexcept override;

    private:
        SampleVoices<16> voices;
        std::array<int, 8> libraryIndex {};
        int heldNote = -1;
        std::array<bool, 128> held {};
        juce::AudioBuffer<float> di;
        dsp::Svf pickupLow[2], pickupHigh[2], hpf[2], toneF[2], cab[2], slapPeak[2];
        dsp::Room room;
        float compEnv = 0.0f;
        double harmPhase[128] {};
        struct Harm { int note = -1; double phase = 0.0; float env = 0.0f; };
        std::array<Harm, 8> harms;
    };

    //==============================================================================
    /** The electric (and acoustic) guitar: the recorded guitars; chords strummed in time with the song (or picked
        notes, palm-muted chugs, power chords, harmonics); pickups, tone, the amp's drive and EQ, a room. */
    class StudioGuitar final : public StudioInstrument
    {
    public:
        StudioGuitar();
        juce::StringArray screenLines() const override;
        static juce::StringArray models();
        static juce::StringArray styles();
        static juce::StringArray patterns();
        /** The chord being held and its frets on the six strings (-1 not played), for the screen and the picture. */
        juce::String chordName() const;
        std::array<int, 6> chordFrets() const noexcept;

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;
        void paramChanged (int index) noexcept override;

    private:
        void voiceChord() noexcept;
        void strum (bool down, float velocity, int sampleOffset) noexcept;
        SampleVoices<32> voices;
        std::array<int, 4> libraryIndex {};
        std::array<bool, 128> held {};
        std::array<std::atomic<int>, 6> frets {};
        std::atomic<int> chordRoot { -1 }, chordKind { 0 };
        std::array<int, 6> sounding {};
        juce::int64 lastStep = -1;
        double internalBeats = 0.0;
        struct Pending { int string = 0, note = 0; float velocity = 0.8f; int delay = 0; bool mute = false; };
        std::array<Pending, 32> pending;
        int numPending = 0;
        juce::AudioBuffer<float> di;
        dsp::Svf preHp[2], ampLow[2], ampMid[2], ampHigh[2], cab1[2], cab2[2], toneF[2];
        dsp::Room room;
    };

    //==============================================================================
    /** The beat pad (in the style of the classic sampling drum machines): 4 banks of 18 pads; kits made by
        synthesis in the style of the classic drum machines (606, 707, 808, 909, 505), trap, lo-fi, the acoustic
        kit's recordings and an acid bass line synth; each pad tuned, shaped, filtered, panned, choked and sent to
        one of 16 outputs; a 16-step sequencer per pad with swing, note repeat, 16 levels, and your own samples. */
    class BeatPad final : public StudioInstrument
    {
    public:
        static constexpr int padsPerBank = 18, banks = 4, numPads = padsPerBank * banks, steps = 16, outputs = 16;
        BeatPad();
        juce::StringArray screenLines() const override;
        static juce::StringArray kitNames();
        /** A pad's name in the current kit (bank*18 + pad). */
        juce::String padName (int pad) const;
        juce::String padSound (int pad) const;
        juce::Colour padColour (int pad) const;
        /** Plays a pad (a click, or a key): its note is 36 + pad. */
        void hit (int pad, float velocity) noexcept;
        void release (int pad) noexcept;
        /** The sequencer: a pad's steps (velocity 0..127 per step; 0 = off), and the step playing now. */
        int getStep (int pad, int step) const noexcept;
        void setStep (int pad, int step, int velocity) noexcept;
        int currentStep() const noexcept { return playingStep.load (std::memory_order_relaxed); }
        /** The pattern as text (for the project) and back. */
        juce::String patternText() const;
        void setPatternText (const juce::String& text) override;
        void setSampleFile (int pad, const juce::File& file) override;
        /** Recording pads into the pattern while the song plays. */
        std::atomic<bool> recording { false };

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;

    private:
        struct Voice;
        void startVoice (int pad, float velocity) noexcept;
        float renderVoice (Voice& v) noexcept;
        std::vector<Voice> voices;
        std::array<std::array<std::atomic<juce::uint8>, steps>, numPads> pattern {};
        std::atomic<int> playingStep { -1 };
        juce::int64 lastStepIndex = -1;
        int acousticIndex = -1;
        SampleVoices<24> sampleVoices;
        std::array<std::shared_ptr<juce::AudioBuffer<float>>, numPads> userSamples;
        std::array<std::atomic<juce::AudioBuffer<float>*>, numPads> userSampleNow {};
        juce::Random random;
        std::array<float, 128> heldVelocity {};
        std::array<bool, numPads> padHeld {};
        double repeatPhase = 0.0;
        // the acid bass line's slide
        float acidPitch = 36.0f, acidTarget = 36.0f;
    };

    //==============================================================================
    /** The Sampler Track (as Cubase's): one recording played across the keys - start, end and a loop
        (continuous, back and forth, or once), reversed, tuned with key follow; its time kept as it plays
        higher or lower (AudioWarp) or not; a filter with drive and envelope, amp and pitch envelopes,
        an LFO, glide, mono and legato. */
    class StudioSampler final : public StudioInstrument
    {
    public:
        StudioSampler();
        juce::StringArray screenLines() const override;
        void setSampleFile (int slot, const juce::File& file) override;
        juce::File getSampleFile() const { return file; }
        /** The recording now (for the editor's waveform), and its rate. */
        std::shared_ptr<juce::AudioBuffer<float>> getSample() const { return std::atomic_load (&sample); }
        double getSampleRate() const noexcept { return sampleRate; }
        /** Where the voices are now (0..1 of the recording) for the editor's playheads. */
        std::array<std::atomic<float>, 8> playheads {};
        /** Finds the hits in the recording (for slicing): their positions (0..1). */
        juce::Array<float> findSlices (float sensitivity) const;

    protected:
        void renderParts (float* const* l, float* const* r, int n, const juce::MidiBuffer& midi) noexcept override;
        void prepareVoices (double sampleRate, int maxBlockSize) override;

    private:
        struct Voice
        {
            int note = -1;
            bool held = false;
            double pos = 0.0, step = 1.0, targetStep = 1.0;
            int dir = 1;
            float velocity = 1.0f;
            dsp::Adsr amp, filt, pitch;
            dsp::Svf f[2];
            double grain = 0.0;     // (AudioWarp: where in the recording, moving at the song's speed)
            double grainPhase = 0.0;
            juce::uint64 age = 0;
            bool active() const noexcept { return amp.active(); }
        };
        void noteOn (int note, float velocity) noexcept;
        void noteOff (int note) noexcept;
        float readAt (const juce::AudioBuffer<float>& s, int channel, double pos) const noexcept;
        std::array<Voice, 16> voices;
        std::shared_ptr<juce::AudioBuffer<float>> sample;
        juce::File file;
        double sampleRate = 44100.0;
        double lfoPhase = 0.0;
        juce::uint64 clock = 0;
        std::vector<int> monoStack;
        float bend = 0.0f;
    };
}
