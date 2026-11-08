// Split to Stems (as Logic's Stem Splitter): the selected audio regions separated into vocals, drums, bass and
// the rest (or six stems, adding guitar and piano), each stem on a new track under its source track, lined up
// with the original, which is muted. Done on this computer by the Demucs v4 model (engine/StemSeparator).
#include "MainComponent.h"
#include "engine/AudioEdit.h"
#include "engine/StemSeparator.h"
#include "engine/DrumSplitter.h"
#include "model/Project.h"

namespace riff
{

namespace
{
    const juce::StringArray& stemNames6()
    {
        static const juce::StringArray names = stems::sourceNames (6);
        return names;
    }

    juce::Colour stemColour (const juce::String& stem)
    {
        if (stem == "Vocals") return juce::Colour (0xffe0607e);
        if (stem == "Drums")  return juce::Colour (0xffe8a23a);
        if (stem == "Bass")   return juce::Colour (0xff5c8ee6);
        if (stem == "Guitar") return juce::Colour (0xff6cc070);
        if (stem == "Piano")  return juce::Colour (0xffb07ce0);
        if (stem == "Kick")   return juce::Colour (0xffe8743a);
        if (stem == "Snare")  return juce::Colour (0xffe8c23a);
        if (stem == "Toms")   return juce::Colour (0xffd0903a);
        if (stem == "Hi-Hat") return juce::Colour (0xff3ac8e8);
        if (stem == "Cymbals") return juce::Colour (0xff3a9ae8);
        return juce::Colour (0xff8a9aa8);   // Other
    }

    /** The choices in the dialog: the model (4 or 6 stems) and which stems to keep. */
    class StemChoices final : public juce::Component
    {
    public:
        StemChoices()
        {
            model.addItem ("4 stems: Vocals, Drums, Bass, Other", 1);
            model.addItem ("6 stems: adds Guitar and Piano", 2);
            model.addItem ("Drum parts (a drum recording): Kick, Snare, Toms, Hi-Hat, Cymbals", 3);
            model.setSelectedId (1, juce::dontSendNotification);
            model.onChange = [this] { update(); };
            addAndMakeVisible (model);
            drumParts.setToggleState (true, juce::dontSendNotification);
            drumParts.setTooltip ("The drum stem taken apart too: a track each for the kick, snare, toms, hi-hat and cymbals (as Logic's Stem Splitter)");
            drumParts.onClick = [this] { update(); };
            addAndMakeVisible (drumParts);
            for (const auto& name : stemNames6())
            {
                auto* b = boxes.add (new juce::ToggleButton (name));
                b->setToggleState (true, juce::dontSendNotification);
                addAndMakeVisible (b);
            }
            update();
            setSize (420, 112);
        }

        bool six() const { return model.getSelectedId() == 2; }
        /** Only the drum parts: the region is drums already, no stem model needed. */
        bool drumsOnly() const { return model.getSelectedId() == 3; }
        /** The drum stem split into its parts as well. */
        bool splitDrums() const { return drumsOnly() || (drumParts.getToggleState() && chosen().contains ("Drums")); }

        juce::StringArray chosen() const
        {
            juce::StringArray out;
            for (auto* b : boxes)
                if (b->isEnabled() && b->getToggleState())
                    out.add (b->getButtonText());
            return out;
        }

        void resized() override
        {
            auto area = getLocalBounds();
            model.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            auto row1 = area.removeFromTop (24), row2 = area.removeFromTop (24);
            for (int i = 0; i < boxes.size(); ++i)
            {
                auto& row = i < 3 ? row1 : row2;
                boxes[i]->setBounds (row.removeFromLeft (getWidth() / 3));
            }
            area.removeFromTop (4);
            drumParts.setBounds (area.removeFromTop (24));
        }

    private:
        void update()
        {
            for (auto* b : boxes)
                b->setEnabled (! drumsOnly() && (six() || (b->getButtonText() != "Guitar" && b->getButtonText() != "Piano")));
            drumParts.setEnabled (! drumsOnly());
        }

        juce::ComboBox model;
        juce::ToggleButton drumParts { "Split the drums into Kick, Snare, Toms, Hi-Hat and Cymbals" };
        juce::OwnedArray<juce::ToggleButton> boxes;
    };

    /** One region to split, read from the project on the message thread (the job never touches the tree). */
    struct Piece
    {
        juce::String regionId, trackId, name;
        juce::File file;
        double offset = 0.0, length = 0.0, start = 0.0;
        std::vector<std::pair<juce::String, juce::File>> stems;   // filled by the job
    };

    class StemJob final : public juce::ThreadWithProgressWindow
    {
    public:
        StemJob (juce::AudioFormatManager& f, std::vector<Piece> p, bool six, juce::StringArray keep, juce::File folder,
                 bool parts, bool onlyDrums, std::function<void (const juce::String&, std::vector<Piece>&)> done)
            : ThreadWithProgressWindow ("Split to Stems", true, true),
              formats (f), pieces (std::move (p)), sixStems (six), wanted (std::move (keep)), dest (std::move (folder)),
              drumParts (parts), drumsOnly (onlyDrums), onDone (std::move (done)) {}

        /** A drum recording (or stem) into its parts, each written and added to the piece's stems. */
        bool splitDrums (Piece& piece, const juce::AudioBuffer<float>& drums, double rate, double base, double share)
        {
            setStatusMessage ("Splitting the drums of \"" + piece.name + "\" into kick, snare, toms, hi-hat and cymbals...");
            const auto parts = drumsplit::split (drums, rate, [&] (float p)
            {
                setProgress (base + share * p);
                return ! threadShouldExit();
            });
            if (parts.empty())
            {
                if (error.isEmpty())
                    error = threadShouldExit() ? "cancelled" : "The drums were silent";
                return false;
            }
            const auto names = drumsplit::partNames();
            for (int i = 0; i < (int) parts.size(); ++i)
            {
                const auto file = dest.getNonexistentChildFile (juce::File::createLegalFileName (piece.name + " " + names[i]), ".wav", false);
                if (const auto e = write (parts[(size_t) i], file, rate); e.isNotEmpty())
                {
                    error = e;
                    return false;
                }
                piece.stems.emplace_back (names[i], file);
            }
            return true;
        }

        void run() override
        {
            if (drumsOnly)
            {
                // A drum recording: just its parts (no stem model to load)
                for (size_t i = 0; i < pieces.size() && ! threadShouldExit(); ++i)
                {
                    auto& piece = pieces[i];
                    double rate = 0.0;
                    const auto audio = audioedit::readRegion (formats, piece.file, piece.offset, piece.length, rate);
                    if (audio.getNumSamples() == 0 || rate <= 0.0)
                    {
                        error = "Couldn't read " + piece.file.getFileName();
                        return;
                    }
                    if (! splitDrums (piece, audio, rate, (double) i / (double) pieces.size(), 1.0 / (double) pieces.size()))
                        return;
                }
                if (threadShouldExit())
                    error = "cancelled";
                return;
            }
            if (! stems::Files::present (sixStems))
            {
                error = stems::download (sixStems, [this] (float p, const juce::String& what)
                {
                    setStatusMessage (what + " (first time only)");
                    setProgress (p);
                    return ! threadShouldExit();
                });
                if (error.isNotEmpty())
                    return;
            }

            setStatusMessage ("Loading the model...");
            setProgress (-1.0);
            stems::Separator separator;
            if (! separator.load (stems::Files::runtime(), stems::Files::model (sixStems), error))
            {
                // A damaged download: remove it, so the next try fetches it again.
                stems::Files::model (sixStems).deleteFile();
                return;
            }
            const auto names = stems::sourceNames (separator.sources());

            for (size_t i = 0; i < pieces.size() && ! threadShouldExit(); ++i)
            {
                auto& piece = pieces[i];
                setStatusMessage ("Splitting \"" + piece.name + "\"" + (pieces.size() > 1 ? " (" + juce::String ((int) i + 1) + " of " + juce::String ((int) pieces.size()) + ")" : juce::String()));
                setProgress (0.0);
                double rate = 0.0;
                const auto audio = audioedit::readRegion (formats, piece.file, piece.offset, piece.length, rate);
                if (audio.getNumSamples() == 0 || rate <= 0.0)
                {
                    error = "Couldn't read " + piece.file.getFileName();
                    return;
                }
                const auto mix = stems::resample (audio, rate, stems::sampleRate);
                const auto base = (double) i / (double) pieces.size(), share = 1.0 / (double) pieces.size();
                auto result = separator.separate (mix, [&] (float p)
                {
                    setProgress (base + share * p);
                    return ! threadShouldExit();
                }, error);
                if (result.empty())
                {
                    if (error.isEmpty())
                        error = "cancelled";
                    return;
                }

                for (int s = 0; s < (int) result.size() && s < names.size(); ++s)
                {
                    if (! wanted.contains (names[s]))
                        continue;
                    if (drumParts && names[s] == "Drums")
                    {
                        // (the drum stem taken apart instead: a track per part)
                        if (! splitDrums (piece, result[(size_t) s], stems::sampleRate, base + share * 0.9, share * 0.1))
                            return;
                        continue;
                    }
                    const auto file = dest.getNonexistentChildFile (juce::File::createLegalFileName (piece.name + " " + names[s]), ".wav", false);
                    if (const auto e = write (result[(size_t) s], file); e.isNotEmpty())
                    {
                        error = e;
                        return;
                    }
                    piece.stems.emplace_back (names[s], file);
                }
            }
            if (threadShouldExit())
                error = "cancelled";
        }

        void threadComplete (bool userPressedCancel) override
        {
            onDone (userPressedCancel ? juce::String ("cancelled") : error, pieces);
            delete this;
        }

    private:
        static juce::String write (const juce::AudioBuffer<float>& audio, const juce::File& file, double rate = stems::sampleRate)
        {
            if (! file.getParentDirectory().createDirectory())
                return "Couldn't create " + file.getParentDirectory().getFullPathName();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                           .withNumChannels (2).withBitsPerSample (24));
            if (writer == nullptr)
                return "Couldn't write " + file.getFullPathName();
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            return {};
        }

        juce::AudioFormatManager& formats;
        std::vector<Piece> pieces;
        bool sixStems;
        juce::StringArray wanted;
        bool drumParts = false, drumsOnly = false;
        juce::File dest;
        std::function<void (const juce::String&, std::vector<Piece>&)> onDone;
        juce::String error;
    };
}

void MainComponent::splitToStems()
{
    if (engine.isRecording())
        return;

    std::vector<Piece> pieces;
    for (const auto& r : timeline.selectedRegionTrees())
    {
        if (model::isMidiRegion (r))
            continue;
        if (model::numTakes (r) > 0)
        {
            showError ("Split to Stems", "A take folder is selected. Flatten its comp first (or choose one take).");
            return;
        }
        Piece p;
        p.regionId = r[ids::id].toString();
        p.trackId = r.getParent()[ids::id].toString();
        p.name = r[ids::name].toString().isNotEmpty() ? r[ids::name].toString() : r.getParent()[ids::name].toString();
        p.file = juce::File (r[ids::file].toString());
        p.offset = r[ids::offset];
        p.length = r[ids::length];
        p.start = r[ids::start];
        if (p.length > 0.0)
            pieces.push_back (p);
    }
    if (pieces.empty())
    {
        showError ("Split to Stems", "Select the audio regions to split.");
        return;
    }

    const auto firstTime = ! stems::Files::present (false) && ! stems::Files::present (true);
    auto* window = new juce::AlertWindow ("Split to Stems",
        "Separates " + juce::String ((int) pieces.size()) + (pieces.size() == 1 ? " region" : " regions")
            + " into stems, each on a new track under its own, lined up with it; the original is muted. "
              "It all happens on this computer: it takes up to about as long as the audio plays (less on a recent Mac)."
            + (firstTime ? "\n\nThe first time, the separation model and its runtime are downloaded (about 250 MB) and kept for next time."
                         : juce::String()),
        juce::MessageBoxIconType::NoIcon);
    auto choices = std::make_shared<StemChoices>();
    window->addCustomComponent (choices.get());
    window->addButton ("Split", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainComponent> (this), window, choices, pieces] (int result) mutable
    {
        window->removeChildComponent (choices.get());   // ours, not the window's
        const auto six = choices->six();
        const auto keep = choices->chosen();
        const auto drumsOnly = choices->drumsOnly(), drumParts = choices->splitDrums();
        if (result != 1 || safe == nullptr)
            return;
        if (keep.isEmpty() && ! drumsOnly)
        {
            safe->showError ("Split to Stems", "Choose at least one stem to keep.");
            return;
        }
        auto& self = *safe;
        const auto folder = self.recordingFolder().getChildFile ("Stems");

        auto* job = new StemJob (self.engine.getFormatManager(), std::move (pieces), six, keep, folder, drumParts, drumsOnly,
            [safe] (const juce::String& error, std::vector<Piece>& done)
        {
            if (safe == nullptr)
                return;
            auto& self = *safe;
            if (error.isNotEmpty())
            {
                for (const auto& p : done)            // a part-finished job leaves nothing behind
                    for (const auto& s : p.stems)
                        s.second.deleteFile();
                if (error != "cancelled")
                    self.showError ("Split to Stems", error);
                return;
            }

            self.undoManager.beginNewTransaction ("Split to Stems");
            std::map<juce::String, juce::ValueTree> stemTracks;   // source track id + stem -> its new track
            juce::String select;
            int made = 0;
            for (const auto& p : done)
            {
                auto source = model::findById (self.project, p.trackId);
                auto region = model::findById (self.project, p.regionId);
                if (! source.isValid())
                    continue;
                int below = self.project.indexOf (source);
                for (const auto& [stem, file] : p.stems)
                {
                    auto& track = stemTracks[p.trackId + "/" + stem];
                    if (! track.isValid())
                    {
                        // after the source track and the stem tracks already made for it
                        for (const auto& [key, t] : stemTracks)
                            if (key.startsWith (p.trackId + "/") && t.isValid())
                                below = juce::jmax (below, self.project.indexOf (t));
                        track = model::addTrack (self.project, source[ids::name].toString() + " " + stem, &self.undoManager, below + 1, model::audioKind);
                        track.setProperty (ids::colour, stemColour (stem).toString(), &self.undoManager);
                        if (select.isEmpty())
                            select = track[ids::id].toString();
                    }
                    auto r = model::addRegion (track, file, p.start, p.length, &self.undoManager);
                    r.setProperty (ids::name, p.name + " " + stem, &self.undoManager);
                    if (region.isValid())
                    {
                        r.setProperty (ids::gain, region.getProperty (ids::gain, 1.0), &self.undoManager);
                        r.setProperty (ids::fadeIn, region.getProperty (ids::fadeIn, 0.0), &self.undoManager);
                        r.setProperty (ids::fadeOut, region.getProperty (ids::fadeOut, 0.0), &self.undoManager);
                    }
                    ++made;
                }
                if (region.isValid() && ! p.stems.empty())
                    region.setProperty (ids::mute, true, &self.undoManager);
            }

            if (select.isNotEmpty())
                self.timeline.selectOnly (select);
            self.timeline.changed();
            self.player.rebuildNow();
            self.statusLabel.setText ("Split into " + juce::String (made) + (made == 1 ? " stem" : " stems"), juce::dontSendNotification);
        });
        job->launchThread();
    }), true);
}

} // namespace riff
