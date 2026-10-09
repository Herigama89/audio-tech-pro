#pragma once

#include "Instrument.h"
#include <array>
#include <atomic>

namespace riff
{

/**
    The built-in drum kit: every General MIDI drum note synthesised (kick,
    snares, side stick, clap, hi-hats with the open hat choked by the closed
    one, toms, crash, ride, bell, cowbell). No samples to load. Tune, decay
    and level are atomics set from the message thread.
*/
class DrumKit final : public Instrument
{
public:
    void setSettings (float tuneSemitones, float decayScale, float level) noexcept
    {
        tune.store (tuneSemitones);
        decay.store (decayScale);
        gain.store (level);
    }

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept override;
    juce::String getName() const override { return "Drum Kit"; }

    /** What a note plays ("Kick", "Snare", ...). */
    static juce::String pieceName (int note);

private:
    enum class Piece { kick, snare, sideStick, clap, closedHat, pedalHat, openHat, tom, crash, ride, bell, cowbell, none };
    static Piece pieceFor (int note);

    struct Voice
    {
        bool active = false;
        Piece piece = Piece::none;
        int note = 0;
        float velocity = 0.0f, pan = 0.0f;
        double time = 0.0, phase = 0.0, phase2 = 0.0, baseFreq = 100.0;
        float bp1 = 0.0f, bp2 = 0.0f, hp = 0.0f, lastNoise = 0.0f;
        std::array<double, 6> metal {};
    };

    void trigger (int note, float velocity) noexcept;
    float next (Voice& v) noexcept;
    float noise() noexcept;

    std::array<Voice, 24> voices;
    double sampleRate = 48000.0;
    juce::uint32 seed = 0x12345678u;
    std::atomic<float> tune { 0.0f }, decay { 1.0f }, gain { 0.8f };
};

/**
    The built-in sampler: one audio file played across the keyboard (the root
    key plays it as recorded), with attack and release and velocity. The file
    is decoded on the message thread when it's chosen.
*/
class Sampler final : public Instrument
{
public:
    /** Message thread: the sound (null for silence) at its own rate. */
    void setSample (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double fileRate);
    void setSettings (int rootNote, float attack, float release, float level) noexcept
    {
        root.store (rootNote);
        attackSeconds.store (attack);
        releaseSeconds.store (release);
        gain.store (level);
    }

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept override;
    juce::String getName() const override { return "Sampler"; }

private:
    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        double position = 0.0, step = 1.0;
        juce::ADSR envelope;
    };

    std::array<Voice, 16> voices;
    double sampleRate = 48000.0;
    juce::SpinLock sampleLock;
    std::shared_ptr<const juce::AudioBuffer<float>> sample;   // replaced under the lock
    double sampleRateOfFile = 48000.0;
    std::atomic<int> root { 60 };
    std::atomic<float> attackSeconds { 0.002f }, releaseSeconds { 0.2f }, gain { 0.8f };
};

} // namespace riff
