#pragma once

#include "Instrument.h"
#include <array>
#include <atomic>

namespace riff
{

/**
    Riff's built-in polyphonic synth: one oscillator (saw, square, triangle or
    sine), a resonant low-pass filter and an amplitude envelope, 16 voices.

    Lock-free and allocation-free in render(); parameters are atomics set
    from the message thread.
*/
class BuiltInSynth final : public Instrument
{
public:
    struct Settings
    {
        int wave = 0;               // 0 saw, 1 square, 2 triangle, 3 sine
        float cutoff = 3000.0f;     // Hz
        float resonance = 0.2f;     // 0..1
        float attack = 0.005f, decay = 0.3f, sustain = 0.6f, release = 0.3f;
        float gain = 0.5f;
    };

    BuiltInSynth();

    void setSettings (const Settings& s) noexcept;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept override;
    juce::String getName() const override { return "Tech Synth"; }

    int getNumActiveVoices() const noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        float velocity = 0.0f;
        double phase = 0.0, phaseInc = 0.0;
        juce::ADSR envelope;
        float ic1 = 0.0f, ic2 = 0.0f;   // filter state
        juce::uint64 startedAt = 0;
    };

    static constexpr int numVoices = 16;

    void handleEvent (const juce::uint8* data, int size) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff (bool immediately) noexcept;
    void renderVoices (float* left, float* right, int start, int end) noexcept;

    std::array<Voice, numVoices> voices;
    juce::uint64 noteCounter = 0;
    bool sustainPedal = false;
    std::array<bool, 128> heldBySustain {};

    // Shared with the message thread
    std::atomic<int> wave { 0 };
    std::atomic<float> cutoff { 3000.0f }, resonance { 0.2f }, attack { 0.005f }, decay { 0.3f },
                       sustain { 0.6f }, release { 0.3f }, gain { 0.5f };

    double sampleRate = 44100.0;
};

} // namespace riff
