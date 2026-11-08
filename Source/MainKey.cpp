// Detect Song Key: the song's (or the selected regions') key and scale, heard from its audio and read from its
// MIDI notes (engine/KeyDetect), offered as the project's key.
#include "MainComponent.h"
#include "engine/AudioEdit.h"
#include "engine/KeyDetect.h"
#include "model/Project.h"

namespace riff
{

void MainComponent::detectSongKey()
{
    // What to listen to: the selected regions, or every region that plays
    auto regions = timeline.selectedRegionTrees();
    const auto fromSelection = ! regions.isEmpty();
    if (! fromSelection)
        for (const auto& track : project)
            if (track.hasType (ids::TRACK) && ! (bool) track[ids::mute] && ! model::isBusTrack (track))
                for (const auto& r : track)
                    if (r.hasType (ids::REGION) && ! model::isRegionMuted (r))
                        regions.add (r);

    struct Audio { juce::File file; double offset, length; int transpose; };
    std::vector<Audio> audio;
    keydetect::Chroma midi {};
    const auto spb = model::secondsPerBeat (project);
    for (const auto& r : regions)
    {
        const auto track = r.getParent();
        const int transpose = (int) r.getProperty (ids::transpose, 0) + (model::isMidiRegion (r) ? (int) track.getProperty (ids::transpose, 0) : 0);
        if (! model::isMidiRegion (r))
        {
            audio.push_back ({ juce::File (r[ids::file].toString()), r[ids::offset], r[ids::length], transpose });
            continue;
        }
        if ((int) track.getProperty (ids::channel, 1) == 10)
            continue;   // (drums: no key)
        // Each note as long as it sounds, about as the audio's frames count it (ten or so a second)
        for (const auto& n : r)
            if (n.hasType (ids::NOTE))
            {
                const auto pc = (((int) n[ids::pitch] + transpose) % 12 + 12) % 12;
                midi[(size_t) pc] += (double) n[ids::length] * spb * 10.8 * juce::jlimit (0.2, 1.0, (double) n.getProperty (ids::velocity, 100) / 127.0);
            }
    }
    if (audio.empty() && std::all_of (midi.begin(), midi.end(), [] (double v) { return v <= 0.0; }))
    {
        showError ("Detect Song Key", "There's nothing to listen to: no audio or MIDI regions playing.");
        return;
    }

    statusLabel.setText ("Listening for the key...", juce::dontSendNotification);
    juce::MouseCursor::showWaitCursor();
    auto& formats = engine.getFormatManager();
    juce::Thread::launch ([safe = juce::Component::SafePointer<MainComponent> (this), &formats, audio, midi, fromSelection]
    {
        auto chroma = midi;
        for (const auto& a : audio)
        {
            double rate = 44100.0;
            const auto buffer = audioedit::readRegion (formats, a.file, a.offset, a.length, rate);
            const auto c = keydetect::chromaOf (buffer, rate);
            for (int i = 0; i < 12; ++i)
                chroma[(size_t) (((i + a.transpose) % 12 + 12) % 12)] += c[(size_t) i];
        }
        const auto result = keydetect::detect (chroma);
        juce::MessageManager::callAsync ([safe, result, fromSelection]
        {
            juce::MouseCursor::hideWaitCursor();
            if (safe == nullptr)
                return;
            auto& self = *safe;
            if (! result.found)
            {
                self.statusLabel.setText ("No clear key", juce::dontSendNotification);
                self.showError ("Detect Song Key", "There's no clear key in " + juce::String (fromSelection ? "the selected regions" : "the song")
                                                   + " (mostly drums or noise?).");
                return;
            }
            const auto best = keydetect::name (result.root, result.minor);
            const auto sure = result.confidence > 0.6 ? "clearly" : result.confidence > 0.25 ? "probably" : "possibly";
            self.statusLabel.setText (juce::String (fromSelection ? "The selection" : "The song") + " is " + sure + " in " + best,
                                      juce::dontSendNotification);

            juce::PopupMenu menu;
            menu.addSectionHeader ("Key: " + best + "  (" + sure + ")");
            auto setKey = [safe] (int root, bool minor)
            {
                if (safe == nullptr)
                    return;
                safe->undoManager.beginNewTransaction ("Key");
                safe->project.setProperty (ids::keyRoot, root, &safe->undoManager);
                safe->project.setProperty (ids::keyMinor, minor, &safe->undoManager);
                safe->statusLabel.setText ("Project key: " + keydetect::name (root, minor), juce::dontSendNotification);
            };
            menu.addItem ("Set the Project Key to " + best, [setKey, result] { setKey (result.root, result.minor); });
            // its relative (the same notes) and the runner-up, in case
            const auto relRoot = result.minor ? (result.root + 3) % 12 : (result.root + 9) % 12;
            menu.addItem ("Set to its relative, " + keydetect::name (relRoot, ! result.minor), [setKey, relRoot, result] { setKey (relRoot, ! result.minor); });
            for (size_t i = 1; i < result.ranking.size(); ++i)
            {
                const auto& c = result.ranking[i];
                if (c.root == relRoot && c.minor != result.minor)
                    continue;
                menu.addItem ("Or the next likeliest, " + keydetect::name (c.root, c.minor), [setKey, c] { setKey (c.root, c.minor); });
                break;
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&self.signatureLabel));
        });
    });
}

} // namespace riff

namespace riff
{
void MainComponent::setGpuDrawing (bool on)
{
   #if RIFF_GPU_DRAWING
    if (on && openGL == nullptr)
    {
        openGL = std::make_unique<juce::OpenGLContext>();
        openGL->setComponentPaintingEnabled (true);
        openGL->setContinuousRepainting (false);   // (only what changed is drawn, as before - by the graphics card)
        openGL->setMultisamplingEnabled (false);
        openGL->attachTo (*this);
    }
    else if (! on && openGL != nullptr)
    {
        openGL->detach();
        openGL.reset();
    }
    gpuDrawing = on;
   #else
    juce::ignoreUnused (on);
    gpuDrawing = false;
   #endif
}
}
