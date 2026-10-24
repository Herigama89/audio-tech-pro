// Stretch (the Stretch tool, Option-drag of a region's edge, or the region menu's Stretch): audio regions made
// longer or shorter with their audio stretched to fit, the pitch kept (or, as tape, following the speed); MIDI
// regions with their notes spread out. The whole original file is rendered at the new length (once: kept in the
// project's Stretched folder), so the region can still be trimmed open, and a stretch again starts from the original.
#include "MainComponent.h"
#include "engine/RegionVariants.h"
#include "model/Project.h"

namespace riff
{

namespace
{
    struct Render
    {
        juce::File source, target;
        double ratio = 1.0;
        bool tape = false;
    };

    juce::String write (const juce::AudioBuffer<float>& audio, double rate, const juce::File& file)
    {
        if (! file.getParentDirectory().createDirectory())
            return "Couldn't create " + file.getParentDirectory().getFullPathName();
        const auto temp = file.getSiblingFile (file.getFileName() + ".part");
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                           .withNumChannels (audio.getNumChannels()).withBitsPerSample (32));
            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            {
                temp.deleteFile();
                return "Couldn't write " + file.getFullPathName();
            }
        }
        return temp.moveFileTo (file) ? juce::String() : "Couldn't write " + file.getFullPathName();
    }

    /** Renders each file (on a background thread). Returns an error, or nothing. */
    juce::String renderAll (const std::vector<Render>& renders)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const auto& job : renders)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (job.source));
            if (reader == nullptr)
                return "Couldn't read " + job.source.getFullPathName();
            const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
            juce::AudioBuffer<float> audio (channels, (int) reader->lengthInSamples);
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, channels > 1);
            const auto rendered = RegionVariants::render (audio, reader->sampleRate, RegionVariants::Spec { 0.0, job.ratio, job.tape });
            if (const auto e = write (rendered, reader->sampleRate, job.target); e.isNotEmpty())
                return e;
        }
        return {};
    }
}

void MainComponent::stretchRegions (juce::Array<juce::ValueTree> regions, double factor, bool fromStart, juce::String mode)
{
    if (engine.isRecording() || stretching)
        canvas.clearPendingStretch();
    if (engine.isRecording())
        return;
    if (stretching)
    {
        statusLabel.setText ("Still stretching the last one...", juce::dontSendNotification);
        return;
    }

    // MIDI: the notes spread out (or squeezed) with the region, at once.
    bool midiDone = false;
    for (auto r : regions)
        if (model::isMidiRegion (r) && factor > 0.0 && std::abs (factor - 1.0) > 1.0e-6)
        {
            if (! midiDone)
                undoManager.beginNewTransaction ("Stretch");
            midiDone = true;
            model::stretchMidiRegion (r, factor, fromStart, &undoManager);
        }

    // Audio: what each region is rendered from, and to.
    struct Change { juce::ValueTree region; model::StretchPlan plan; juce::File rendered; };
    std::vector<Change> changes;
    std::vector<Render> renders;
    juce::StringArray queued;
    bool skippedTakes = false;
    for (auto r : regions)
    {
        if (model::isMidiRegion (r))
            continue;
        if (model::numTakes (r) > 0)
        {
            skippedTakes = true;
            continue;
        }
        const auto current = r[ids::stretchMode].toString() == model::stretchTape ? model::stretchTape : model::stretchKeepPitch;
        const auto k = factor > 0.0 ? factor : 1.0 / model::stretchOf (r);   // (0: back to the original)
        const auto plan = model::stretchPlanFor (r, k, factor > 0.0 && mode.isNotEmpty() ? mode : current);
        if (std::abs (plan.ratio - model::stretchOf (r)) < 1.0e-6 && plan.mode == current)
            continue;   // (nothing changes)

        Change c { r, plan, plan.source };
        if (plan.ratio != 1.0)
        {
            const auto tape = plan.mode == model::stretchTape;
            const auto name = plan.source.getFileNameWithoutExtension() + " " + juce::String::toHexString (plan.source.getFullPathName().hashCode())
                              + " x" + juce::String (plan.ratio, 4) + (tape ? " tape" : "") + ".wav";
            c.rendered = recordingFolder().getChildFile ("Stretched").getChildFile (juce::File::createLegalFileName (name));
            if (! c.rendered.existsAsFile() && ! queued.contains (c.rendered.getFullPathName()))
            {
                queued.add (c.rendered.getFullPathName());
                renders.push_back ({ plan.source, c.rendered, plan.ratio, tape });
            }
        }
        if (! plan.source.existsAsFile())
        {
            canvas.clearPendingStretch();
            showError ("Stretch", "The original audio file is missing:\n" + plan.source.getFullPathName());
            return;
        }
        changes.push_back (c);
    }

    auto apply = [safe = juce::Component::SafePointer<MainComponent> (this), changes, fromStart, midiDone] (const juce::String& error)
    {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        self.stretching = false;
        self.canvas.clearPendingStretch();
        if (error.isNotEmpty())
        {
            if (error != "cancelled")
                self.showError ("Stretch", error);
            return;
        }
        if (! changes.empty() && ! midiDone)
            self.undoManager.beginNewTransaction ("Stretch");
        for (const auto& c : changes)
            if (c.region.getParent().isValid())
                model::applyStretch (c.region, c.rendered, c.plan, fromStart, &self.undoManager);
        self.timeline.changed();
        self.player.rebuildNow();
        if (! changes.empty())
        {
            const auto k = model::stretchOf (changes.front().region);
            self.statusLabel.setText (std::abs (k - 1.0) < 1.0e-6 ? juce::String ("Back to the original length")
                                                                   : "Stretched to " + juce::String::charToString (0x00d7) + juce::String (k, 3)
                                                                         + " of the original" + (changes.front().plan.mode == model::stretchTape ? " (tape)" : ""),
                                      juce::dontSendNotification);
        }
    };

    if (skippedTakes)
        statusLabel.setText ("Take folders aren't stretched: flatten the comp first", juce::dontSendNotification);
    if (midiDone && changes.empty())
    {
        timeline.changed();
        player.rebuildNow();
    }
    if (renders.empty())
    {
        apply ({});
        return;
    }
    // Rendered in the background (a few seconds for a whole song's file); the region shows where it's going meanwhile.
    stretching = true;
    statusLabel.setText ("Stretching...", juce::dontSendNotification);
    juce::Thread::launch ([renders = std::move (renders), apply]
    {
        const auto error = renderAll (renders);
        if (error.isNotEmpty())
            for (const auto& r : renders)
                r.target.deleteFile();
        juce::MessageManager::callAsync ([apply, error] { apply (error); });
    });
}

} // namespace riff
