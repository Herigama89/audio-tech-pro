#include "RegionVariants.h"
#include "AudioEdit.h"

namespace riff
{

RegionVariants::RegionVariants (juce::File cacheFolder)
    : juce::Thread ("Region variants"), folder (cacheFolder)
{
    if (folder == juce::File())
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AudioTechPro Variants");
    folder.createDirectory();
    formats.registerBasicFormats();
    startThread (juce::Thread::Priority::low);
}

RegionVariants::~RegionVariants()
{
    stopThread (4000);
}

juce::File RegionVariants::targetFor (const juce::File& source, const Spec& spec) const
{
    const auto key = source.getFullPathName() + "|" + juce::String (source.getLastModificationTime().toMilliseconds())
                   + "|" + juce::String (spec.semitones, 4) + "|" + juce::String (spec.ratio, 5) + (spec.varispeed ? "|v" : "");
    return folder.getChildFile (source.getFileNameWithoutExtension().substring (0, 40) + "-"
                                + juce::String::toHexString (key.hashCode64()) + ".wav");
}

juce::File RegionVariants::get (const juce::File& source, const Spec& spec)
{
    if (spec.isIdentity() || ! source.existsAsFile())
        return source;
    const auto target = targetFor (source, spec);
    if (target.existsAsFile())
        return target;

    const juce::ScopedLock sl (lock);
    if (queued.insert (target.getFullPathName()).second)
    {
        queue.push_back ({ source, target, spec, false });
        notify();
    }
    return {};
}

double RegionVariants::tempoOf (const juce::File& source)
{
    const juce::ScopedLock sl (lock);
    if (auto it = tempos.find (source.getFullPathName()); it != tempos.end())
        return it->second;
    if (queued.insert ("tempo:" + source.getFullPathName()).second)
    {
        queue.push_back ({ source, {}, {}, true });
        notify();
    }
    return 0.0;
}

bool RegionVariants::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        {
            const juce::ScopedLock sl (lock);
            if (queue.empty() && ! busy)
                return true;
        }
        juce::Thread::sleep (10);
    }
    return false;
}

juce::AudioBuffer<float> RegionVariants::render (const juce::AudioBuffer<float>& input, double sampleRate, const Spec& spec)
{
    auto audio = input;
    if (spec.varispeed)
    {
        // Tape-like: played slower or faster, so lower or higher. Then any transpose on top.
        const auto outLength = juce::jmax (1, (int) std::llround (audio.getNumSamples() * spec.ratio));
        juce::AudioBuffer<float> out (audio.getNumChannels(), outLength);
        out.clear();
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.process (1.0 / spec.ratio, audio.getReadPointer (c), out.getWritePointer (c), outLength, audio.getNumSamples(), 0);
        }
        audio = std::move (out);
    }
    else if (std::abs (spec.ratio - 1.0) > 1.0e-4)
        audio = audioedit::timeStretch (audio, spec.ratio, sampleRate);

    if (std::abs (spec.semitones) > 1.0e-4)
        audio = audioedit::pitchShift (audio, spec.semitones, sampleRate);
    return audio;
}

void RegionVariants::run()
{
    while (! threadShouldExit())
    {
        Job job;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty())
            {
                job = queue.front();
                queue.erase (queue.begin());
                busy = true;
            }
        }
        if (job.source == juce::File())
        {
            wait (500);
            continue;
        }

        double rate = 48000.0;
        if (job.tempoOnly)
        {
            // Smart Tempo: up to the first 60 seconds are enough to hear the beat.
            const auto audio = audioedit::readRegion (formats, job.source, 0.0, 60.0, rate);
            const auto bpm = audio.getNumSamples() > 1 ? audioedit::detectTempo (audio, rate) : 0.0;
            const juce::ScopedLock sl (lock);
            tempos[job.source.getFullPathName()] = bpm;
            queued.erase ("tempo:" + job.source.getFullPathName());
        }
        else
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (job.source));
            if (reader != nullptr && reader->lengthInSamples > 0 && reader->lengthInSamples < (juce::int64) 48000 * 60 * 20)
            {
                rate = reader->sampleRate;
                const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
                juce::AudioBuffer<float> audio (channels, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, channels > 1);
                reader.reset();
                const auto out = render (audio, rate, job.spec);

                // Written next to its final name, then moved there: never read half-written.
                const auto temp = job.target.getSiblingFile (job.target.getFileName() + ".part");
                temp.deleteFile();
                if (auto stream = std::make_unique<juce::FileOutputStream> (temp); stream->openedOk())
                {
                    std::unique_ptr<juce::OutputStream> os (stream.release());
                    juce::WavAudioFormat wav;
                    if (auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                   .withNumChannels (out.getNumChannels()).withBitsPerSample (32)
                                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
                    {
                        writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
                        writer.reset();
                        temp.moveFileTo (job.target);
                    }
                }
            }
            const juce::ScopedLock sl (lock);
            queued.erase (job.target.getFullPathName());
        }
        busy = false;
        triggerAsyncUpdate();
    }
}

} // namespace riff
