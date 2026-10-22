#pragma once

#include "BuiltInEffects.h"

namespace riff
{

/**
    Analog tape saturation, as a mixing console that runs every channel, bus and the master through
    a tape machine: the highs pre-emphasised before they meet the tape (so they saturate first) and
    de-emphasised after, a soft magnetic saturation with a touch of asymmetry, the low "head bump"
    and the gentle loss of the very top, which depend on the tape speed.

    The level is kept: at low levels it passes at unity, and more Drive rounds the peaks off rather
    than making it louder. No latency (the saturation is anti-aliased, not oversampled). Real-time safe.
*/
class TapeSaturator
{
public:
    enum class Speed { ips15 = 0, ips30 = 1 };

    struct Settings
    {
        bool enabled = false;
        float drive = 0.35f;   // 0..1: up to 18 dB into the tape
        int speed = 1;         // 0: 15 ips (more bump, darker), 1: 30 ips (tighter, brighter)

        bool operator== (const Settings&) const = default;
    };

    /** Reads a track's tape (tapeOn, tapeDrive 0..100, tapeSpeed). */
    static Settings fromTrack (const juce::ValueTree& track);
    /** Reads the master's (masterTapeOn, masterTapeDrive, masterTapeSpeed on the project). */
    static Settings fromProject (const juce::ValueTree& project);

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    /** The tape's frequency response at low level (head bump and top end), dB. */
    static double responseDb (const Settings& s, double frequency, double sampleRate = 48000.0);

private:
    void update() noexcept;
    float saturate (float x, float& previous, float& previousIntegral) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    Biquad preEmphasis, deEmphasis, headBump, bumpDip, topLoss;
    float inGain = 1.0f, outGain = 1.0f, bias = 0.0f;
    std::array<float, 2> previous {}, previousIntegral {};
    bool valid = false;
};

} // namespace riff
