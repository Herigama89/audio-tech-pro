// Renders the app's main views to PNG files without a display, so the look can
// be checked on a build machine. Build with -DRIFF_BUILD_SNAPSHOTS=ON, then:
//     Snapshots <output folder>

#include <JuceHeader.h>
#include "MainComponent.h"
#include "model/Project.h"
#include "engine/MasterBusCompressor.h"
#include "ui/KeyCommands.h"
#include "ui/ConsoleLook.h"

namespace riff
{

class SnapshotTool
{
public:
    explicit SnapshotTool (juce::File outputFolder) : out (std::move (outputFolder)) {}

    int run()
    {
        out.createDirectory();
        audioFolder = out.getChildFile ("demo-audio");
        audioFolder.createDirectory();

        MainComponent main (false);
        main.setSize (1440, 920);
        buildDemoProject (main);
        main.player.rebuildNow();

        // Play a second of the song so the meters and the compressor's needle move.
        main.engine.setPlayheadSeconds (8.0);
        main.engine.play();
        juce::AudioBuffer<float> buffer (2, 512);
        for (int i = 0; i < 90; ++i)
        {
            main.engine.render (nullptr, 0, buffer.getArrayOfWritePointers(), 2, 512);
            if (i % 9 == 0)
                pump (main, 20);
        }

        // Waveforms start loading the first time they're painted, on a background thread.
        main.createComponentSnapshot (main.getLocalBounds());
        pump (main, 2500);
        pump (main, 200);
        save (main, "arrange.png");

        main.showMixer();
        pump (main, 200);
        save (main, "mixer.png");
        if (auto* firstStrip = findById (main.mixer, "strip0"))
            save (*firstStrip, "strip.png", 3.0f);

        // Console mode, as it looks on a computer with UA's bus compressor installed.
        {
            juce::PluginDescription d;
            d.name = "UADx SSL G Bus Compressor";
            d.manufacturerName = "Universal Audio";
            d.pluginFormatName = "AudioUnit";
            auto& console = main.mixer.getConsolePanel();
            console.setCandidates ({ d });
            main.mixer.resized();
            // Names and ranges as UA's plugin reports them
            console.showPreview ({ 0.62f, 1.0f, 0.4f, 0.75f, 0.0f, 0.5f, 0.35f }, 4.0f, 0.7f, true,
                                 { juce::StringArray { "Thresh", "-15.0", "15.0" }, juce::StringArray { "Mix", "0%", "100%" },
                                   juce::StringArray { "Attack", "0.1", "30" }, juce::StringArray { "Release", "0.1", "Auto" },
                                   juce::StringArray { "SC Filter", "Off", "500" }, juce::StringArray { "Ratio", "2:1", "10:1" },
                                   juce::StringArray { "Make Up", "0.0", "15.0" } });
            pump (main, 200);
            save (main, "mixer-console.png");
            save (console, "console-panel.png", 2.0f);
            console.setCandidates ({});
            main.mixer.resized();
        }

        main.closeMixer();
        for (const auto& track : main.project)
            for (const auto& region : track)
                if (model::isMidiRegion (region))
                {
                    main.openPianoRoll (region);
                    break;
                }
        pump (main, 200);
        save (main, "pianoroll.png");
        main.closePianoRoll();

        {
            KeyCommandsPanel panel (*main.commandManager.getKeyMappings(), "Logic Pro", {});
            panel.setSize (620, 640);
            pump (main, 100);
            save (panel, "keycommands.png");
        }

        main.engine.stop();
        return 0;
    }

private:
    juce::File out, audioFolder;

    static juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->getComponentID() == id)
                return child;
            if (auto* found = findById (*child, id))
                return found;
        }
        return nullptr;
    }

    static void pump (MainComponent& main, int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            main.timerCallback();
        }
    }

    void save (juce::Component& c, const juce::String& name, float scale = 1.0f)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        auto file = out.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
    }

    /** A made-up performance: bursts of filtered noise and tones with an envelope. */
    juce::File makeAudio (const juce::String& name, double seconds, float pitchHz, float density, int seed)
    {
        const auto file = audioFolder.getChildFile (name + ".wav");
        if (file.existsAsFile())
            return file;

        constexpr double rate = 48000.0;
        const auto length = (int) (seconds * rate);
        juce::AudioBuffer<float> b (1, length);
        juce::Random random (seed);
        float lp = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const auto t = i / rate;
            const auto beat = std::fmod (t * density, 1.0);
            const auto env = (float) std::exp (-beat * 6.0) * (0.55f + 0.45f * (float) std::sin (t * 0.7 + seed));
            lp += 0.15f * (random.nextFloat() * 2.0f - 1.0f - lp);
            const auto tone = (float) std::sin (juce::MathConstants<double>::twoPi * pitchHz * t);
            b.setSample (0, i, env * (0.6f * tone + 0.8f * lp));
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                                     .withNumChannels (1)
                                                                                     .withBitsPerSample (16));
        writer->writeFromAudioSampleBuffer (b, 0, length);
        return file;
    }

    void buildDemoProject (MainComponent& main)
    {
        auto project = model::createProject ("Analog Soul");
        project.setProperty (ids::tempo, 110.0, nullptr);
        auto* um = &main.undoManager;

        struct Spec { const char* name; float pitch; float density; double start; double length; };
        const Spec specs[] = {
            { "Lead Vox",  220.0f, 0.9f,  4.4,  22.0 },
            { "Snare",     180.0f, 1.83f, 0.0,  30.0 },
            { "Drums",      60.0f, 3.66f, 0.0,  30.0 },
            { "Bass",       55.0f, 1.83f, 0.0,  30.0 },
            { "Guitars L", 330.0f, 0.9f,  2.2,  26.0 },
            { "Guitars R", 330.0f, 0.9f,  2.2,  26.0 },
        };

        int seed = 1;
        for (const auto& s : specs)
        {
            auto track = model::addTrack (project, s.name, um);
            const auto file = makeAudio (juce::String (s.name).removeCharacters (" "), s.length, s.pitch, s.density, seed++);
            auto region = model::addRegion (track, file, s.start, s.length * 0.45, um);
            model::addRegion (track, file, s.start + s.length * 0.5, s.length * 0.5, um);
            juce::ignoreUnused (region);
        }

        // An instrument with a MIDI region.
        auto keys = model::addTrack (project, "Keys", um, -1, model::instrumentKind);
        auto midi = model::addMidiRegion (keys, 4.36, 8.72, um);
        const int chord[] = { 60, 64, 67, 71, 62, 65, 69, 72 };
        for (int bar = 0; bar < 4; ++bar)
            for (int n = 0; n < 4; ++n)
                model::addNote (midi, chord[(bar % 2) * 4 + n], bar * 2.18, 1.9, 90 + n * 5, um);

        // A reverb bus with sends.
        auto bus = model::addTrack (project, "Reverb", um, -1, model::busKind);
        auto busChain = model::chainFor (project, bus[ids::id].toString());
        auto reverb = model::addBuiltinInsert (busChain, "reverb", 0, um);
        juce::ignoreUnused (reverb);

        for (int i = 0; i < 2; ++i)
        {
            auto track = project.getChild (i);
            auto chain = model::chainFor (project, track[ids::id].toString());
            model::addBuiltinInsert (chain, "eq", 0, um);
            model::addBuiltinInsert (chain, "compressor", 1, um);
            model::addSend (chain, bus[ids::id].toString(), 0.4, um);
        }

        // Volume automation on the vocal.
        auto vox = project.getChild (0);
        vox.setProperty (ids::showAutomation, true, nullptr);
        auto lane = model::laneFor (project, vox[ids::id].toString(), model::volumeParam, true);
        model::addPoint (lane, 4.0, 0.6, um);
        model::addPoint (lane, 9.0, 0.9, um);
        model::addPoint (lane, 14.0, 0.75, um);
        model::addPoint (lane, 20.0, 1.0, um);

        // A limiter on the master, and the Master Bus Compressor working.
        auto masterChain = model::chainFor (project, model::masterOwner);
        model::addBuiltinInsert (masterChain, "limiter", 0, um);
        project.setProperty (ids::busCompOn, true, nullptr);
        project.setProperty (ids::busCompThreshold, -18.0, nullptr);

        project.setProperty (ids::cycleEnabled, true, nullptr);
        project.setProperty (ids::cycleStart, 4.36, nullptr);
        project.setProperty (ids::cycleEnd, 13.09, nullptr);

        main.setProject (project, {});
        main.timeline.pixelsPerSecond = 38.0;
        main.timeline.selectedTrackId = project.getChild (0)[ids::id].toString();
        main.timeline.changed();
        main.undoManager.clearUndoHistory();
    }
};

} // namespace riff

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    riff::ConsoleLook look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    const auto folder = argc > 1 ? juce::File (juce::String (argv[1]))
                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("snapshots");
    const auto result = riff::SnapshotTool (folder).run();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return result;
}
