#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

/**
    Stem separation (as Logic's Stem Splitter): a recording split into vocals, drums, bass and the rest (and,
    with the 6-stem model, guitar and piano), by the Demucs v4 hybrid transformer (Meta AI, MIT licence) run
    with ONNX Runtime on this computer.

    The network was exported to ONNX without its STFT (which ONNX can't express), as in sevagh/demucs.onnx
    (MIT): so here, around it, the same STFT and inverse as PyTorch's (normalized, centred, reflect-padded,
    periodic Hann), Demucs's own padding and cropping of frames, and its overlapping 7.8 s segments with
    triangular cross-fades.

    ONNX Runtime is loaded when first needed (a library file downloaded with the model), so nothing here
    has to be there for the app to start.
*/
namespace riff::stems
{
    constexpr int sampleRate = 44100;
    constexpr int nfft = 4096, hop = 1024, bins = nfft / 2 + 1;
    constexpr int segmentSamples = 343980;   // 7.8 s, the model's training length

    /** frames x bins, frame-major. */
    struct Spectrogram
    {
        int frames = 0, bins = 0;
        std::vector<std::complex<float>> data;
        std::complex<float>& at (int frame, int bin) { return data[(size_t) frame * (size_t) bins + (size_t) bin]; }
        std::complex<float> at (int frame, int bin) const { return data[(size_t) frame * (size_t) bins + (size_t) bin]; }
    };

    /** torch.stft (n_fft 4096, hop 1024, periodic Hann, normalized, center, reflect padding). */
    Spectrogram stft (const float* x, int length);
    /** torch.istft with the same settings, `length` samples out. */
    std::vector<float> istft (const Spectrogram& z, int length);

    /** The model's sources' names, for 4 or 6 sources (Demucs's order). */
    juce::StringArray sourceNames (int count);

    /** Where the model and ONNX Runtime are kept (downloaded the first time stems are split, from this
        app's GitHub release "stem-models-v1"), per user: <app data>/Audio Tech Pro/Stem Separation. */
    struct Files
    {
        static juce::File folder();
        static juce::String runtimeName();                  // this system's ONNX Runtime library
        static juce::String modelName (bool sixStems);
        static juce::File runtime()                { return folder().getChildFile (runtimeName()); }
        static juce::File model (bool sixStems)    { return folder().getChildFile (modelName (sixStems)); }
        static bool present (bool sixStems)        { return runtime().existsAsFile() && model (sixStems).existsAsFile(); }
        static juce::String baseUrl();
        static void setFolderForTests (const juce::File&);
    };

    /** Downloads whichever of the model and the runtime are missing (to a temporary file, then moved in place,
        so a cut-off download is never mistaken for a finished one). `progress` (0..1, what's being fetched)
        returns false to cancel. Returns an error, or empty. */
    juce::String download (bool sixStems, const std::function<bool (float, const juce::String&)>& progress);

    /** Downloads one file from `url` to `dest` (through a temporary file). */
    juce::String downloadFile (const juce::URL& url, const juce::File& dest, const std::function<bool (float)>& progress);

    /** Converts `buffer` from `fromRate` to `toRate` (windowed sinc), stereo out (mono is doubled). */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& buffer, double fromRate, double toRate);

    class Separator
    {
    public:
        Separator();
        ~Separator();

        /** Loads ONNX Runtime from `runtimeLibrary` (once per run of the app) and the model. */
        bool load (const juce::File& runtimeLibrary, const juce::File& model, juce::String& error);
        bool isLoaded() const noexcept;
        int sources() const noexcept;

        /** Separates a stereo mix at 44.1 kHz: one stereo buffer per source, the mix's length.
            `progress` (0..1) returns false to cancel (then the result is empty). */
        std::vector<juce::AudioBuffer<float>> separate (const juce::AudioBuffer<float>& mix, const std::function<bool (float)>& progress,
                                                        juce::String& error);

    private:
        struct Session;
        std::unique_ptr<Session> session;
        bool runSegment (const std::vector<float>& paddedMix, int paddedLength, int pad, std::vector<float>& out, juce::String& error);
    };
}
