#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Audio regions played differently from their file, as Logic's Region box
    allows: transposed (Transpose and Fine Tune, pitch kept in time), and
    stretched to follow the song's tempo (Flex with Smart Tempo: Polyphonic
    keeps the pitch, Speed changes it like tape).

    The changed audio is rendered once in the background and kept on disk
    (in the app's cache folder); until it's ready the region plays as it is.
    Also finds a file's tempo (Smart Tempo) in the background.
*/
class RegionVariants final : public juce::ChangeBroadcaster,
                             private juce::Thread,
                             private juce::AsyncUpdater
{
public:
    explicit RegionVariants (juce::File cacheFolder = {});
    ~RegionVariants() override;

    struct Spec
    {
        double semitones = 0.0;   // pitch change (transpose + fine tune / 100)
        double ratio = 1.0;       // length change (2 = twice as long, so half the tempo)
        bool varispeed = false;   // Speed: the pitch follows the speed (semitones still added)
        bool isIdentity() const noexcept { return std::abs (semitones) < 1.0e-4 && std::abs (ratio - 1.0) < 1.0e-4; }
    };

    /** The rendered file for this source and spec, or an empty File while it's being made (it's queued). */
    juce::File get (const juce::File& source, const Spec& spec);

    /** The file's tempo (Smart Tempo), or 0 while it's being found or when it has no clear beat. */
    double tempoOf (const juce::File& source);

    /** Blocks until nothing is queued (tests). */
    bool waitUntilIdle (int timeoutMs);

    /** The rendering itself (exposed for tests): from a buffer to a buffer. */
    static juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& input, double sampleRate, const Spec& spec);

private:
    struct Job { juce::File source, target; Spec spec; bool tempoOnly = false; };
    void run() override;
    void handleAsyncUpdate() override { sendChangeMessage(); }
    juce::File targetFor (const juce::File& source, const Spec& spec) const;

    juce::File folder;
    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
    std::vector<Job> queue;
    std::set<juce::String> queued;             // targets (or "tempo:" + source) waiting or in progress
    std::map<juce::String, double> tempos;     // source path -> bpm (0: none)
    std::atomic<bool> busy { false };
};

} // namespace riff
