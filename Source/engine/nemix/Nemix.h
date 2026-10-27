#pragma once

#include "../Instrument.h"
#include "NemixParams.h"
#include "NemixFx.h"
#include "Wavetables.h"
#include <array>
#include <atomic>

namespace riff
{

/**
    Nemix: a wavetable synth in the manner of the big modern ones.

    Three wavetable oscillators (each up to 16 unison voices, with warp modes: sync, bend, PWM, mirror,
    quantize, FM, AM and ring mod), a sub oscillator and noise; two filters (low, high and band pass at 12 and
    24 dB, a ladder, notch, peak, combs and a formant filter) in series or parallel; four envelopes (the
    first is the amp) and four LFOs (free or in time with the song); a 32-slot modulation matrix where any
    source moves any knob; an arpeggiator; and an effects rack.

    Parameters are atomics set from the message thread (setParameter); render() is lock-free and doesn't
    allocate. The voices are worked out 32 samples at a time for the modulation, every sample for the sound.
*/
class Nemix final : public Instrument
{
public:
    Nemix();

    void setParameter (int index, float value) noexcept;
    float getParameter (int index) const noexcept;
    /** Sets every parameter to its default. */
    void resetToDefaults() noexcept;

    void setPosition (juce::int64 samplePosition, bool isPlaying, double beatsPerMinute) noexcept override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept override;
    juce::String getName() const override { return "Nemix"; }

    // For the editor (read on the message thread while it plays)
    int activeVoices() const noexcept                 { return voicesNow.load (std::memory_order_relaxed); }
    /** How far modulation moves a parameter now (in 0..1 of its range, the newest voice), for the rings round the knobs. */
    float modulationNow (int index) const noexcept    { return index >= 0 && index < nemix::count() ? modNow[(size_t) index].load (std::memory_order_relaxed) : 0.0f; }
    /** The LFOs' and envelopes' values now (newest voice), for their displays. */
    float lfoNow (int lfo) const noexcept             { return lfoShown[(size_t) juce::jlimit (0, 3, lfo)].load (std::memory_order_relaxed); }
    float envNow (int env) const noexcept             { return envShown[(size_t) juce::jlimit (0, 3, env)].load (std::memory_order_relaxed); }

    static constexpr int maxParams = 700;

private:
    struct Env
    {
        int stage = 0;          // 0 off, 1 attack, 2 hold, 3 decay, 4 sustain, 5 release
        double t = 0.0;         // seconds into the stage
        float level = 0.0f, from = 0.0f;
    };
    struct FilterState
    {
        float ic1[2][2] {}, ic2[2][2] {};       // two SVF stages, each stereo
        float ladder[2][4] {};                    // the ladder's four poles, stereo
        float formant[3][2][2] {};                // three band passes (ic1, ic2), stereo
        int combPos = 0;
        float combLow[2] {};
        std::array<std::vector<float>, 2> comb;   // the comb filters' delay (sized in prepare)
    };
    struct Voice
    {
        bool active = false, gate = false, held = false;
        int note = 60;
        float velocity = 1.0f, random = 0.0f;
        juce::uint64 age = 0;
        double pitch = 60.0, target = 60.0;      // in semitones (gliding to the target)
        std::array<Env, 4> env;
        std::array<double, 4> lfoPhase {};
        std::array<float, 4> lfoValue {}, lfoHeld {}, lfoNext {}, lfoSmooth {};
        std::array<double, 4> lfoTime {};        // since the note started (for rise)
        std::array<bool, 4> lfoDone {};
        std::array<std::array<double, nemix::maxUnison>, 3> phase {};
        std::array<float, 3> last {};             // each oscillator's last output (FM, AM, ring mod)
        double subPhase = 0.0;
        float pink[7] {}, noiseLow = 0.0f, noiseHigh = 0.0f, crackle = 0.0f;
        std::array<double, 6> metal {};
        std::array<FilterState, 2> filter;
        std::array<float, 32> offsets {};        // the modulation of each destination in use, 0..1 of its range
        float ampNow = 0.0f;
    };

    // Per block: the settings, and the modulation in use
    void readSettings() noexcept;
    float eff (const Voice& v, int index) const noexcept;
    void computeModulation (Voice& v, int samplesSinceTick) noexcept;
    void renderVoice (Voice& v, float* left, float* right, int start, int count) noexcept;

    // Notes
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void startVoice (Voice& v, int note, float velocity, bool retrigger) noexcept;
    void releaseVoice (Voice& v) noexcept;
    void allNotesOff (bool hard) noexcept;
    float advanceEnv (Env& e, int which, double seconds, const Voice& v) noexcept;

    // The arpeggiator: turns the notes held into steps
    void arpBlock (int numSamples, std::vector<std::pair<int, juce::MidiMessage>>& out) noexcept;
    int arpNoteForStep (juce::int64 step) noexcept;

    // Parameters (message thread writes, audio thread reads once per block)
    std::array<std::atomic<float>, maxParams> values;
    std::array<float, maxParams> base {}, baseNormal {};
    std::array<int, maxParams> dstSlot {};      // which of a voice's offsets moves a parameter (-1: none)
    struct Route { int src, dst, amt, slot; float amount; };
    std::array<Route, nemix::numSlots> routes {};
    int numRoutes = 0, numDestinations = 0;
    std::array<int, 32> destinations {};        // parameter index of each offset slot
    std::array<std::atomic<float>, maxParams> modNow;
    std::array<std::atomic<float>, 4> lfoShown, envShown;
    std::atomic<int> voicesNow { 0 };

    std::array<Voice, nemix::maxVoices> voices;
    juce::uint64 counter = 0;
    double sampleRate = 48000.0;
    float modWheel = 0.0f, aftertouch = 0.0f, bend = 0.0f;
    bool sustainPedal = false;
    int lastNote = -1;
    std::array<bool, 128> keyDown {};
    std::array<float, 128> keyVelocity {};
    std::vector<int> monoStack;                 // (mono: the notes held, newest last)
    juce::Random random;
    std::array<double, 4> freeLfoPhase {};      // LFOs in Free mode run for all voices together

    // Transport
    std::atomic<juce::int64> songPosition { 0 };
    std::atomic<double> songBpm { 120.0 };
    std::atomic<bool> songPlaying { false };

    // Arpeggiator state
    std::vector<std::pair<int, float>> arpHeld; // (note, velocity) in the order played
    double arpFreeSteps = 0.0;
    juce::int64 arpLastStep = -1;
    int arpSounding = -1;                       // the note it's playing (-1 none), and when it ends (in steps)
    double arpOffAt = 0.0;
    std::vector<int> arpChord;
    bool arpWasOn = false;

    nemix::fx::Rack rack;
    nemix::fx::Settings fxSettings;
    std::vector<float> scratchL, scratchR;
    std::vector<std::pair<int, juce::MidiMessage>> events, arpOut;
    std::array<int, 32> shownLast {};
    int shownCount = 0;

    // Values read per block
    struct Block
    {
        int polyphony = 12;
        bool mono = false, legato = false;
        float glide = 0.0f, bendRange = 2.0f, velAmp = 0.6f, master = 0.7f;
        bool serial = true;
    } blk;
};

} // namespace riff
