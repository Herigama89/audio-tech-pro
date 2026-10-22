// StemCheck: the app's stem separator on a WAV file, for checking it against Demucs itself in CI.
//   StemCheck <onnxruntime library> <model.onnx> <in.wav (44.1 kHz stereo)> <out folder>
// Writes <out folder>/<source>.wav (32-bit float), one per source.

#include <JuceHeader.h>
#include "engine/StemSeparator.h"
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 5)
    {
        std::cerr << "usage: StemCheck <onnxruntime library> <model.onnx> <in.wav> <out folder>\n";
        return 2;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (juce::String (argv[3]))));
    if (reader == nullptr)
    {
        std::cerr << "can't read " << argv[3] << "\n";
        return 1;
    }
    juce::AudioBuffer<float> mix (2, (int) reader->lengthInSamples);
    reader->read (&mix, 0, mix.getNumSamples(), 0, true, true);

    riff::stems::Separator separator;
    juce::String error;
    if (! separator.load (juce::File (juce::String (argv[1])), juce::File (juce::String (argv[2])), error))
    {
        std::cerr << error << "\n";
        return 1;
    }
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto out = separator.separate (mix, [] (float p) { std::cout << "\r" << juce::roundToInt (p * 100.0f) << "%" << std::flush; return true; }, error);
    std::cout << "\n" << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s for " << mix.getNumSamples() / 44100.0 << " s of audio\n";
    if (out.empty())
    {
        std::cerr << error << "\n";
        return 1;
    }
    const auto names = riff::stems::sourceNames ((int) out.size());
    const juce::File folder { juce::String (argv[4]) };
    folder.createDirectory();
    for (size_t s = 0; s < out.size(); ++s)
    {
        const auto file = folder.getChildFile (names[(int) s].toLowerCase() + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 2, 32, {}, 0));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (out[s], 0, out[s].getNumSamples()))
            return 1;
    }
    return 0;
}
