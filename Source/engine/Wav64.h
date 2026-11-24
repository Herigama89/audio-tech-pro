#pragma once

#include <JuceHeader.h>

namespace riff
{
/**
    64-bit float WAV files (WAVE_FORMAT_IEEE_FLOAT, 64 bits a sample): JUCE's own WAV format reads and writes up to
    32-bit float only. Registered after it (registerAudioFormats), so it opens only the 64-bit files JUCE's refuses,
    and writes a project's recordings when its bit depth is 64-bit float.
*/
class Wav64AudioFormat final : public juce::AudioFormat
{
public:
    Wav64AudioFormat() : AudioFormat ("WAV 64-bit float", ".wav .wave .bwf") {}

    juce::Array<int> getPossibleSampleRates() override { return { 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 }; }
    juce::Array<int> getPossibleBitDepths() override { return { 64 }; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails) override;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (std::unique_ptr<juce::OutputStream>& stream,
                                                              const juce::AudioFormatWriterOptions& options) override;
};

/** The formats every part of the app opens: JUCE's basic ones (WAV, AIFF, FLAC, Ogg, MP3/CoreAudio on a Mac)
    and 64-bit float WAV. */
void registerAudioFormats (juce::AudioFormatManager& manager);

/** A writer for a recording or a render in one of the project's formats: `fileType` "wav", "aiff" or "flac";
    `bits` 16, 24, 32 (float in WAV; integer in AIFF) or 64 (float WAV). Null if the format can't hold it. */
std::unique_ptr<juce::AudioFormatWriter> createAudioWriter (const juce::String& fileType, int bits, double sampleRate,
                                                            int numChannels, std::unique_ptr<juce::OutputStream>& stream);

/** The file extension of a project file type ("wav" -> ".wav", "aiff" -> ".aif", "flac" -> ".flac"). */
juce::String extensionForFileType (const juce::String& fileType);
}
