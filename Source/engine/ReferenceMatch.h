#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "BuiltInEffects.h"
#include "Loudness.h"

/**
    Automix's reference matching: listen to a finished, mastered song and to the mix, and work out the
    master-bus chain that takes the mix towards it - the tone (a Match EQ from the two average spectra),
    the density (bus compression from how far apart their peaks and loudness are), the width (from how
    much of each is in the sides) and the loudness (an Adaptive Limiter to the reference's level and ceiling).
*/
namespace riff::reference
{
    constexpr int bands = 31;   // third octaves, 20 Hz to 20 kHz (as Match EQ's)

    /** What a piece of music measures. */
    struct Profile
    {
        std::array<float, bands> bandDb {};   // average level per third octave (mid signal), dB
        double lufs = -100.0;                 // integrated loudness
        double peakDb = -100.0;               // sample peak
        double sideDb = -100.0;               // the sides' level against the middle's (dB; 0 = as much side as mid)
        double seconds = 0.0;
        bool valid() const noexcept { return seconds > 1.0 && lufs > -70.0; }
        /** Peak to loudness: how dense it is (a loud master ~8 LU, an unmastered mix 15 and more). */
        double density() const noexcept { return peakDb - lufs; }
    };

    /** Measures a stereo signal as it's fed in. */
    class Analyser
    {
    public:
        void prepare (double rate);
        void add (const float* left, const float* right, int n) noexcept;
        Profile result() const;

    private:
        double sampleRate = 48000.0;
        std::array<Biquad, bands> filters;
        std::array<double, bands> energy {};
        double samples = 0.0, mid = 0.0, side = 0.0;
        float peak = 0.0f;
        LoudnessMeter loudness;
    };

    /** A song from a file (any format the app imports). Empty profile and `error` set when it can't be read. */
    Profile analyseFile (const juce::File& file, juce::String& error, std::function<bool()> keepGoing = {});

    /** One plugin for the master bus. */
    struct Step
    {
        juce::String type;
        std::vector<std::pair<juce::String, float>> params;
        juce::String why;
    };

    /** The master-bus chain that takes `mix` towards `reference` (in order). */
    std::vector<Step> plan (const Profile& mix, const Profile& reference);

    /** The property Automix marks the master plugins it adds with (so running it again replaces them). */
    inline const juce::Identifier marker { "automixReference" };
}
