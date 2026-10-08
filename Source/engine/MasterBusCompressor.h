#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace riff
{

/**
    The Master Bus Compressor: a stereo-linked "glue" compressor on the master,
    in the style of the classic console bus compressors. Stepped controls
    (ratio, attack, release with Auto), a threshold, make-up gain and a
    parallel mix.

    It sits after the master inserts and before the master fader, and its
    gain reduction drives the VU meter in the mixer.

    The settings come from the project (see fromProject) through the playback
    graph; process() is real-time safe.
*/
class MasterBusCompressor
{
public:
    static constexpr std::array<float, 3> ratios    { 2.0f, 4.0f, 10.0f };
    static constexpr std::array<float, 6> attacksMs { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
    static constexpr std::array<float, 5> releasesS { 0.1f, 0.3f, 0.6f, 1.2f, 0.0f };  // 0 = Auto
    static constexpr int autoRelease = 4;

    static constexpr float minThresholdDb = -30.0f, maxThresholdDb = 0.0f;
    static constexpr float maxMakeupDb = 15.0f;

    struct Settings
    {
        bool enabled = false;
        float thresholdDb = -10.0f;
        int ratio = 1;          // index into ratios: 4:1
        int attack = 5;         // 30 ms
        int release = autoRelease;
        float makeupDb = 0.0f;
        float mix = 1.0f;       // 0 = dry, 1 = fully compressed

        bool operator== (const Settings&) const = default;
    };

    /** Reads the settings from a PROJECT tree (missing properties give the defaults). */
    static Settings fromProject (const juce::ValueTree& project);

    /** Writes one setting (undoable). `name` is one of the ids in Identifiers.h starting with "busComp". */
    static void setInProject (juce::ValueTree project, const juce::Identifier& name, const juce::var& value, juce::UndoManager* um);

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Picks up new settings; call on the audio thread before process(). */
    void setSettings (const Settings& s) noexcept { settings = s; }

    void process (float* left, float* right, int numSamples) noexcept;

    /** Gain reduction in dB (positive), held for the meter. Safe from any thread. */
    float getGainReductionDb() const noexcept { return meterGainReduction.load (std::memory_order_relaxed); }

    /** Lets the meter's hold fall back (called by the UI after reading). */
    void meterRead() noexcept { meterGainReduction.store (0.0f, std::memory_order_relaxed); }

    static juce::String ratioText (int index);
    static juce::String attackText (int index);
    static juce::String releaseText (int index);

private:
    float coefficient (float seconds) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;

    float envelopeDb = 0.0f;       // current gain reduction, dB (>= 0)
    float sustained = 0.0f;        // slow average of the gain reduction, for Auto release
    float wet = 0.0f;              // smoothed IN switch (0 = bypassed)
    float wetStep = 0.0f;

    std::atomic<float> meterGainReduction { 0.0f };
};

} // namespace riff
