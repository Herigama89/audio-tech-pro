// Renders the app's main views to PNG files without a display, so the look can
// be checked on a build machine. Build with -DRIFF_BUILD_SNAPSHOTS=ON, then:
//     Snapshots <output folder>

#include "../Source/ui/Skin.h"
#include "../Source/ui/ThemePack.h"
#include "../Source/ui/LiveLoopsGrid.h"
#include "../Source/ui/ToolIcons.h"
#include "../Source/ui/PluginPicker.h"
#include "../Source/ui/MultiMeter.h"
#include "../Source/ui/TrackHeaders.h"
#include "../Source/ui/ThemePanel.h"
#include "../Source/ui/AutomixPanel.h"
#include "../Source/ui/InsertBar.h"
#include "../Source/ui/Looks.h"
#include <JuceHeader.h>
#include "MainComponent.h"
#include "model/Project.h"
#include "engine/MasterBusCompressor.h"
#include "ui/KeyCommands.h"
#include "ui/Commands.h"
#include "ui/AudioSuitePanel.h"
#include "engine/BuiltInEffects.h"
#include "ui/GeneratorPanel.h"
#include "ui/InstrumentWindows.h"
#include "ui/AssistantPanel.h"
#include "ui/ScreenRecordPanel.h"
#include "ui/CombineInterfacesPanel.h"
#include "ui/ConsoleStripWindow.h"
#include "model/NoteTools.h"
#include "ui/AudioEditorPanel.h"
#include "ui/PitchEditor.h"
#include <deque>
#include "ui/ConsoleLook.h"

namespace riff
{

/** Something with a mix's balance for the analyzers' pictures: noise falling 4.5 dB an octave (as a mix does,
    on average) from 30 Hz up, with a bass line, a kick, a voice-like tone and its harmonics. Stereo, 48 kHz. */
static juce::AudioBuffer<float> mixLike (int seconds = 4)
{
    constexpr int order = 16, size = 1 << order;
    const auto length = 48000 * seconds;
    juce::AudioBuffer<float> mix (2, length);
    juce::dsp::FFT fft (order);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Random r (11 + ch);
        for (int at = 0; at < length; at += size)
        {
            std::vector<std::complex<float>> bins ((size_t) size), time ((size_t) size);
            for (int k = 1; k < size / 2; ++k)
            {
                const auto hz = (double) k * 48000.0 / size;
                const auto amp = (float) (std::pow (hz / 1000.0, -0.75) * (hz < 30.0 ? std::pow (hz / 30.0, 2.0) : 1.0));
                bins[(size_t) k] = std::polar (amp, r.nextFloat() * juce::MathConstants<float>::twoPi);
                bins[(size_t) (size - k)] = std::conj (bins[(size_t) k]);
            }
            fft.perform (bins.data(), time.data(), true);
            double sum = 0.0;
            for (const auto& v : time)
                sum += (double) v.real() * v.real();
            const auto gain = 0.2f / (float) std::sqrt (sum / size + 1.0e-30);   // about -14 dB RMS: a loud master
            for (int i = 0; i < size && at + i < length; ++i)
                mix.setSample (ch, at + i, time[(size_t) i].real() * gain);
        }
    }
    for (int i = 0; i < length; ++i)
    {
        const auto t = (double) i / 48000.0;
        const auto beat = std::fmod (t, 0.5);
        const auto kick = (float) (0.35 * std::exp (-beat * 18.0) * std::sin (juce::MathConstants<double>::twoPi * (50.0 + 60.0 * std::exp (-beat * 30.0)) * beat));
        const auto bass = (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 82.4 * t));
        float voice = 0.0f;
        for (int h = 1; h <= 10; ++h)
            voice += (float) (0.03 / h * std::sin (juce::MathConstants<double>::twoPi * 220.0 * h * t + h));
        mix.addSample (0, i, kick + bass + voice);
        mix.addSample (1, i, kick + bass + 0.8f * voice);
    }
    return mix;
}

class SnapshotTool
{
public:
    explicit SnapshotTool (juce::File outputFolder) : out (std::move (outputFolder)) {}

    /** Just the stock effects' windows (quick: snapshots fx <folder> [types...]). */
    int effectsOnly (const juce::StringArray& types)
    {
        out.createDirectory();
        juce::UndoManager um;
        auto project = model::createProject();
        auto track = model::addTrack (project, "FX", nullptr);
        auto chain = model::chainFor (project, track[ids::id].toString());
        for (const auto& spec : types)
        {
            // ("type:id=value,id=value" sets parameters; the picture is named after the whole spec)
            const auto type = spec.upToFirstOccurrenceOf (":", false, false);
            auto insert = model::addBuiltinInsert (chain, type, -1, nullptr);
            for (const auto& pair : juce::StringArray::fromTokens (spec.fromFirstOccurrenceOf (":", false, false), ",", ""))
                if (pair.contains ("="))
                    insert.setProperty (juce::Identifier (pair.upToFirstOccurrenceOf ("=", false, false)), pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
            if (type == "channelEq")
            {
                insert.setProperty ("hpFreq", 50.0f, nullptr);
                insert.setProperty ("lsGain", -2.0f, nullptr);
                insert.setProperty ("p1Freq", 330.0f, nullptr);
                insert.setProperty ("p1Gain", 3.0f, nullptr);
                insert.setProperty ("p1Q", 1.8f, nullptr);
                insert.setProperty ("p2Freq", 2060.0f, nullptr);
                insert.setProperty ("p2Gain", 2.0f, nullptr);
                insert.setProperty ("p2Q", 2.0f, nullptr);
                insert.setProperty ("p3Freq", 7100.0f, nullptr);
                insert.setProperty ("p3Gain", 2.0f, nullptr);
            }
            // (the effect itself, fed a few seconds of something music-like, for the windows that show readings)
            auto running = std::shared_ptr<BuiltInEffect> (BuiltInEffect::create (type).release());
            if (running != nullptr)
            {
                const auto& params = running->getInfo().params;
                running->prepare (48000.0, 512);
                for (size_t p = 0; p < params.size(); ++p)
                    running->setParameter ((int) p, (float) (double) insert.getProperty (juce::Identifier (params[p].id), params[p].defaultValue));
                running->setPosition (0, true, 120.0);
                static const auto music = mixLike();
                std::vector<float> a (512), b (512);
                for (int block = 0; block < music.getNumSamples() / 512; ++block)
                {
                    std::copy_n (music.getReadPointer (0, block * 512), 512, a.begin());
                    std::copy_n (music.getReadPointer (1, block * 512), 512, b.begin());
                    running->process (a.data(), b.data(), 512);
                    if (type == "multiMeter" && block % 8 == 7)   // (the window reads it as it plays)
                    {
                        std::array<float, BuiltInEffect::meterCapacity> m {};
                        juce::Thread::sleep (10);
                        running->readMeters (m.data(), (int) m.size());
                    }
                }
            }
            EffectPanel panel (insert, um);
            panel.setMeterSource ([running] (float* dest, int capacity) { return running != nullptr ? running->readMeters (dest, capacity) : 0; });
            panel.setLiveValue ([type] { return type == "pitchCorrection" ? 22.0f : 8.5f; });
            panel.setAnalyzerSource ([] (float* dest, int count)
            {
                // (a stand-in for music: a few harmonics over a falling noise floor)
                juce::Random random (1);
                float lp = 0.0f;
                for (int i = 0; i < count; ++i)
                {
                    lp += 0.08f * (random.nextFloat() * 2.0f - 1.0f - lp);
                    dest[i] = 0.25f * lp;
                    for (int h = 1; h <= 8; ++h)
                        dest[i] += 0.2f / (float) h * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) h * (float) i / 48000.0f);
                }
                return 48000.0;
            });
            panel.setSize (panel.getWidth(), panel.getHeight());
            panel.resized();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
            save (panel, "fx-" + spec.replaceCharacters (":=,", "_-_").substring (0, 120) + ".png");
        }
        return 0;
    }

    /** Imports an audio file the way the Import command does, timing each step (to find hangs). */
    int importTest (const juce::File& file)
    {
        MainComponent main (false);
        main.setSize (1440, 920);
        main.setProject (model::createProject(), {});
        auto stamp = [start = juce::Time::getMillisecondCounterHiRes()] (const char* what)
        {
            std::cout << what << " at " << (int) (juce::Time::getMillisecondCounterHiRes() - start) << " ms" << std::endl;
        };

        stamp ("start");
        main.canvas.importFiles ({ file.getFullPathName() }, main.selectedTrackIndex(), 0.0);
        stamp ("imported (region on screen)");
        main.player.rebuildNow();   // asks for the audio
        main.fileCache.waitUntilIdle (120000);
        stamp ("audio loaded and playable");
        pump (main, 500);
        main.createComponentSnapshot (main.getLocalBounds());
        stamp ("painted");
        main.showMixer();
        pump (main, 500);
        main.canvas.importFiles ({ file.getFullPathName() }, -1, 2.0);
        stamp ("imported with the mixer open");
        pump (main, 1500);
        main.createComponentSnapshot (main.getLocalBounds());
        stamp ("painted again");
        return 0;
    }

    /** Just the mixer (with the bus compressor shown, tape on the first track and the master): for quick looks. */
    int meterOnly()
    {
        out.createDirectory();
        MainComponent main (false);
        main.setSize (1440, 920);
        main.applyTheme ({});
        pump (main, 50);
        MultiMeterPanel meter (main.engine);
        const auto mix = mixLike();
        for (int block = 0; block < 90; ++block)
        {
            meter.take (mix.getReadPointer (0, block * 1600), mix.getReadPointer (1, block * 1600), 1600);
            pump (main, 20);
        }
        save (meter, "multimeter.png");
        return 0;
    }

    /** Where the time goes as a song grows (for the lag with many channels and plug-ins). */
    int perfOnly()
    {
        out.createDirectory();
        audioFolder = out.getChildFile ("demo-audio");
        audioFolder.createDirectory();
        MainComponent main (false);
        main.setSize (2560, 1440);
        main.setVisible (true);
        buildDemoProject (main);
        main.showMixer();
        pump (main, 100);
        const auto file = makeAudio ("perf", 20.0, 220.0f, 1.0f, 99);
        auto ms = [] (auto&& f) { const auto t0 = juce::Time::getMillisecondCounterHiRes(); f(); return juce::Time::getMillisecondCounterHiRes() - t0; };
        auto drain = [] { for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (1); };
        juce::Image canvasImage (juce::Image::ARGB, main.getWidth(), main.getHeight(), true);
        const char* plugins[] { "channelEq", "compressor", "studioReverb", "tapeDelay" };
        const auto rounds = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("RIFF_PERF_ROUNDS", "6").getIntValue());
        for (int round = 0; round < rounds; ++round)
        {
            double addTrack = 0, addPlugins = 0;
            for (int t = 0; t < 8; ++t)
            {
                juce::ValueTree track;
                addTrack += ms ([&] { main.undoManager.beginNewTransaction(); track = model::addTrack (main.project, "P" + juce::String (round * 8 + t), &main.undoManager);
                                      model::addRegion (track, file, 0.0, 20.0, &main.undoManager); drain(); });
                for (auto* type : plugins)
                    addPlugins += ms ([&] { model::addBuiltinInsert (model::chainFor (main.project, track[ids::id].toString()), type, -1, &main.undoManager); drain(); });
            }
            const auto n = model::numTracks (main.project);
            auto anyTrack = main.project.getChild (2);
            const auto fader = ms ([&] { for (int i = 0; i < 20; ++i) { anyTrack.setProperty (ids::volume, 0.5 + 0.01 * i, &main.undoManager); drain(); } }) / 20.0;
            const auto rebuild = ms ([&] { main.player.rebuildNow(); });
            const auto mixerTick = ms ([&] { for (int i = 0; i < 10; ++i) main.mixer.timerCallback(); }) / 10.0;
            const auto mainTick = ms ([&] { for (int i = 0; i < 10; ++i) main.timerCallback(); }) / 10.0;
            const auto paint = ms ([&] { juce::Graphics g (canvasImage); main.paintEntireComponent (g, true); });
            std::printf ("tracks %3d  add track %7.1f ms  add plugin %7.1f ms  fader move %7.1f ms  graph rebuild %7.1f ms  mixer tick %6.2f ms  main tick %6.2f ms  full paint %7.1f ms\n",
                         n, addTrack / 8.0, addPlugins / 32.0, fader, rebuild, mixerTick, mainTick, paint);
            std::fflush (stdout);
        }
        return 0;
    }

    int mixerOnly()
    {
        out.createDirectory();
        audioFolder = out.getChildFile ("demo-audio");
        audioFolder.createDirectory();
        MainComponent main (false);
        main.setSize (1440, 920);
        buildDemoProject (main);
        main.project.setProperty ("mixerBusComp", true, nullptr);
        main.project.setProperty ("busCompOn", true, nullptr);
        main.project.setProperty ("busCompColor", true, nullptr);
        main.project.setProperty ("masterTapeOn", true, nullptr);
        if (auto t0 = main.project.getChild (0); t0.isValid())
        {
            t0.setProperty ("tapeOn", true, nullptr);
            t0.setProperty ("tapeDrive", 60.0, nullptr);
        }
        if (auto t1 = main.project.getChild (1); t1.isValid())
        {
            t1.setProperty ("preOn", true, nullptr);
            t1.setProperty ("preGain", 18.0, nullptr);
        }
        if (const auto side = juce::SystemStats::getEnvironmentVariable ("RIFF_SNAP_ZONES", {}); side.isNotEmpty())
            main.project.setProperty ("mixerBusZone", side, nullptr);
        main.player.rebuildNow();
        main.showMixer();
        // The interface's preamps, from a real captured state (Tests/data/ucnet), with 48 V on input 1 and 32 dB of gain
        if (juce::SystemStats::getEnvironmentVariable ("RIFF_SNAP_PREAMP", {}).isNotEmpty())
        {
            juce::MemoryBlock rx;
            juce::File (juce::SystemStats::getEnvironmentVariable ("RIFF_SNAP_PREAMP", {})).loadFileAsData (rx);
            ucnet::Decoder d;
            for (const auto& m : d.feed (rx.getData(), rx.getSize()))
                if (m.code == "ZM")
                {
                    auto tree = ucnet::parseState (m);
                    if (auto* ch1 = tree["children"]["line"]["children"]["ch1"]["values"].getDynamicObject())
                    {
                        ch1->setProperty ("48v", 1.0);
                        ch1->setProperty ("preampgain", 32.0 / 75.0);
                    }
                    main.interfaceControl.preview (tree);
                }
            pump (main, 100);
        }
        main.mixer.relayoutStrips();
        for (int i = 0; i < 20; ++i)
        {
            main.mixer.setMasterLevels (0.7f, 0.6f);
            pump (main, 15);
        }
        // (levels on the meters, as while playing)
        float level = 0.9f;
        for (auto* st : main.mixer.strips)
        {
            st->pushLevels (level, level * 0.7f);
            st->pushGainReduction (level * 12.0f);
            level *= 0.7f;
        }
        pump (main, 30);
        save (main, "mixer-only.png");
        return 0;
    }

    int run()
    {
        out.createDirectory();
        audioFolder = out.getChildFile ("demo-audio");
        audioFolder.createDirectory();

        MainComponent main (false);
        main.setSize (1440, 920);
        buildDemoProject (main);
        main.player.rebuildNow();
        main.fileCache.waitUntilIdle (60000);   // audio decodes in the background
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

        if (juce::SystemStats::getEnvironmentVariable ("RIFF_BENCH", {}).isNotEmpty())
            return bench (main);

        main.showMixer();
        pump (main, 200);
        save (main, "mixer.png");
        main.mixer.narrowButton.setToggleState (true, juce::sendNotificationSync);
        pump (main, 100);
        save (main, "mixer-narrow.png");
        main.mixer.narrowButton.setToggleState (false, juce::sendNotificationSync);
        main.showMixer();
        pump (main, 50);
        main.mixer.narrowButton.setToggleState (false, juce::sendNotificationSync);
        pump (main, 50);
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
            console.showFaceForTesting = true;   // (as when the plugin's chosen: with Built-in, the built-in knobs show)
            juce::PluginDescription strip;
            strip.name = "UADx SSL 4000 E Channel Strip";
            strip.manufacturerName = "Universal Audio";
            strip.pluginFormatName = "AudioUnit";
            main.mixer.setStripCandidates ({ strip });
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
            console.showFaceForTesting = false;
            main.mixer.setStripCandidates ({});
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
        {
            // FL Studio-style tools: the scale shaded, the other regions as ghosts, a muted note, the Paint tool
            auto& roll = main.pianoRoll;
            roll.scaleHighlightButton.setToggleState (true, juce::sendNotification);
            roll.ghostButton.setToggleState (true, juce::sendNotification);
            for (const auto& n : roll.region)
                if (n.hasType (ids::NOTE))
                {
                    notetools::setMuted ({ n }, true, nullptr);
                    break;
                }
            roll.setTool (PianoRoll::Tool::paint);
            pump (main, 150);
            save (roll, "pianoroll-tools.png");
            out.getChildFile ("pianoroll-keys.txt").replaceWithText (roll.shortcutsText());
            {
                // The FL keys reach the tools: Option-F flams the notes (one undoable step)
                const auto before = roll.region.getNumChildren();
                roll.keyPressed (juce::KeyPress ('f', juce::ModifierKeys::altModifier, 0));
                std::cout << "Alt+F flam: " << before << " -> " << roll.region.getNumChildren() << " children" << std::endl;
                main.undoManager.undo();
            }
            for (const auto& n : roll.region)
                if (n.hasType (ids::NOTE))
                    notetools::setMuted ({ n }, false, nullptr);
            roll.setTool (PianoRoll::Tool::draw);
            roll.scaleHighlightButton.setToggleState (false, juce::sendNotification);
            roll.ghostButton.setToggleState (false, juce::sendNotification);
        }
        main.pianoRoll.viewBox.setSelectedId (2);
        pump (main, 150);
        save (main.pianoRoll, "score.png");
        main.pianoRoll.viewBox.setSelectedId (3);
        pump (main, 150);
        save (main.pianoRoll, "step-editor.png");
        main.pianoRoll.viewBox.setSelectedId (1);
        {
            auto drummer = generator::player::addPlayerTrack (main.project, "drums", 0.0, 4, nullptr);
            main.openPianoRoll (drummer.getChild (0));
            pump (main, 200);
            save (main.pianoRoll, "drummer.png");
            main.closePianoRoll();
            main.project.removeChild (drummer, nullptr);
        }
        main.closePianoRoll();

        {
            auto track = main.project.getChild (1);
            track.setProperty (ids::consoleHF, 4.0, nullptr);
            track.setProperty (ids::consoleLMF, -5.0, nullptr);
            track.setProperty (ids::consoleLMFFreq, 300.0, nullptr);
            track.setProperty (ids::consoleHPF, 80.0, nullptr);
            track.setProperty (ids::consoleDynamics, 4.0, nullptr);
            // The analyzer fed a made-up mix: falling noise and a few tones.
            auto phase = std::make_shared<double> (0.0);
            auto noise = std::make_shared<juce::Random> (3);
            auto lp = std::make_shared<float> (0.0f);
            ConsoleStripWindow panel (track, main.undoManager, [phase, noise, lp] (float* dest, int max)
            {
                const int n = juce::jmin (max, 1600);
                for (int i = 0; i < n; ++i)
                {
                    *lp += 0.08f * (noise->nextFloat() * 2.0f - 1.0f - *lp);
                    *phase += 1.0;
                    dest[i] = 0.5f * *lp + 0.05f * (noise->nextFloat() - 0.5f)
                              + 0.25f * (float) std::sin (*phase * 2.0 * juce::MathConstants<double>::pi * 110.0 / 48000.0)
                              + 0.08f * (float) std::sin (*phase * 2.0 * juce::MathConstants<double>::pi * 2500.0 / 48000.0);
                }
                return n;
            }, [] { return 48000.0; }, [] { return std::array<float, 3> { 0.55f, 0.42f, 4.5f }; });
            for (int i = 0; i < 20; ++i)
                pump (main, 40);
            save (panel, "strip-window.png");

            AudioEditorPanel editor (track.getChild (0), main.undoManager, main.engine.getFormatManager(), juce::File::getSpecialLocation (juce::File::tempDirectory));
            pump (main, 400);
            save (editor, "audio-editor.png");
            editor.flexTimeButton.setToggleState (true, juce::sendNotificationSync);
            if (editor.markers.size() > 2)
            {
                editor.dragMarker = 1;
                editor.dragTo = editor.markerLimits (1).second - 0.02;
            }
            pump (main, 50);
            save (editor, "audio-editor-flex-time.png");
            editor.dragMarker = -1;

            {
                // The pitch editor on a sung-like line (vibrato, drifting notes, a slide), one note moved and straightened
                const auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-pitch-voice.wav");
                {
                    constexpr double rate = 48000.0;
                    juce::AudioBuffer<float> voice (1, (int) (4.0 * rate));
                    voice.clear();
                    struct N { double from, to, midi, vib, drift; };
                    const N line[] { { 0.10, 0.60, 57.2, 0.25, 0.0 }, { 0.70, 1.20, 60.0, 0.35, -0.3 }, { 1.30, 2.10, 64.3, 0.5, 0.0 },
                                     { 2.20, 2.60, 62.0, 0.2, 0.2 }, { 2.70, 3.60, 59.8, 0.6, -0.2 } };
                    double phase = 0.0;
                    for (int i = 0; i < voice.getNumSamples(); ++i)
                    {
                        const auto t = i / rate;
                        for (const auto& n : line)
                            if (t >= n.from && t < n.to)
                            {
                                const auto k = (t - n.from) / (n.to - n.from);
                                const auto midi = n.midi + n.drift * k + n.vib * std::sin (juce::MathConstants<double>::twoPi * 5.2 * t) * std::min (1.0, k * 3.0);
                                phase += juce::MathConstants<double>::twoPi * 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / rate;
                                const auto env = std::min (1.0, (t - n.from) / 0.03) * std::min (1.0, (n.to - t) / 0.05) * (0.6 + 0.4 * std::sin (3.1 * k));
                                voice.setSample (0, i, (float) (env * (0.3 * std::sin (phase) + 0.15 * std::sin (2.0 * phase) + 0.07 * std::sin (3.0 * phase))));
                            }
                    }
                    wavFile.deleteFile();
                    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (wavFile);
                    juce::WavAudioFormat wav;
                    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
                    writer->writeFromAudioSampleBuffer (voice, 0, voice.getNumSamples());
                }
                auto vocal = model::addTrack (main.project, "Vocal", nullptr);
                auto take = model::addRegion (vocal, wavFile, 0.0, 4.0, nullptr);
                PitchEditor pitch (take, main.undoManager, main.engine.getFormatManager(), juce::File::getSpecialLocation (juce::File::tempDirectory));
                pitch.setSize (1200, 560);
                pitch.analyseNow();
                std::cout << "pitch editor notes: " << pitch.analysis.notes.size() << std::endl;
                if (pitch.analysis.notes.size() >= 3)
                {
                    pitch.edits[2].drift = 1.0;
                    pitch.edits[0].shift = std::round (pitch.analysis.notes[0].centre) - pitch.analysis.notes[0].centre;
                    pitch.selected = { 1 };
                    pitch.edits[1].shift = 2.0;
                    pitch.edits[1].gain = -4.0;
                    pitch.commit ("Snapshot");
                    pitch.refreshInspector();
                    pitch.hoverNote = 1;   // (its hot spots shown)
                }
                pump (main, 50);
                save (pitch, "pitch-editor.png");

                // Docked under the tracks (Logic's Audio Track Editor), the transport above, a change made live
                main.openAudioEditor (take);
                main.audioPane->showFlexPitch (true);
                if (auto* docked = main.audioPane->pitchEditor.get())
                {
                    docked->analyseNow();
                    if (docked->analysis.notes.size() >= 4)
                    {
                        docked->edits[3].shift = -1.0;
                        docked->commit ("Snapshot live");
                        std::cout << "live pitch file: " << juce::File (take[ids::file].toString()).getFileName()
                                  << " in memory: " << (int) (docked->live != nullptr) << std::endl;
                    }
                }
                main.engine.setPlayheadSeconds (1.5);
                pump (main, 120);
                save (main, "pitch-editor-docked.png");
                main.closeAudioEditor();
                main.project.removeChild (vocal, nullptr);
            }

            // The stock effects' windows: an EQ with its curve, the compressor's circuits, a synced delay
            {
                auto fxTrack = model::addTrack (main.project, "FX", nullptr);
                auto chain = model::chainFor (main.project, fxTrack[ids::id].toString());
                auto eq = model::addBuiltinInsert (chain, "channelEq", -1, nullptr);
                eq.setProperty ("hpFreq", 60.0f, nullptr);
                eq.setProperty ("p1Gain", -4.0f, nullptr);
                eq.setProperty ("p2Freq", 2500.0f, nullptr);
                eq.setProperty ("p2Gain", 5.0f, nullptr);
                eq.setProperty ("p2Q", 1.6f, nullptr);
                eq.setProperty ("hsGain", 3.0f, nullptr);
                for (const auto* type : { "channelEq", "studioComp", "stereoDelay", "tubeEq", "studioReverb", "phaser", "overdrive", "autoFilter", "noiseGate", "stepFilter", "guitarAmp", "pitchCorrection", "vocoder", "delayDesigner", "linearEq", "subBass" })
                {
                    auto insert = juce::String (type) == "channelEq" ? eq : model::addBuiltinInsert (chain, type, -1, nullptr);
                    EffectPanel panel (insert, main.undoManager);
                    panel.resized();
                    save (panel, juce::String ("fx-") + type + ".png");
                }
                main.project.removeChild (fxTrack, nullptr);
            }

            main.timeline.selectOnly (track[ids::id].toString());
            main.toggleSmartControls();
            pump (main, 200);
            save (main, "smart-controls.png");
            main.toggleSmartControls();
        }

        {
            KeyCommandsPanel panel (*main.commandManager.getKeyMappings(), "Logic Pro", {});
            panel.setSize (620, 640);
            pump (main, 100);
            save (panel, "keycommands.png");
            panel.search.setText ("split", true);
            pump (main, 50);
            save (panel, "keycommands-search.png");
        }

        // The MIDI generator: its window, and a Hip-Hop take in a fresh project.
        {
            GeneratorPanel panel (main.generatorSettings, true, {});
            pump (main, 100);
            save (panel, "generator.png");

            main.engine.stop();
            main.setProject (model::createProject(), {});
            pump (main, 200);
            GeneratorPanel::Request request;
            request.settings.genre = 0;
            request.settings.root = 9;
            request.settings.seed = 11;
            main.generateMidi (request);
            main.player.rebuildNow();
            main.engine.setPlayheadSeconds (0.0);
            main.engine.play();
            juce::AudioBuffer<float> buffer (2, 512);
            float peak = 0.0f;
            for (int i = 0; i < 200; ++i)
            {
                main.engine.render (nullptr, 0, buffer.getArrayOfWritePointers(), 2, 512);
                peak = juce::jmax (peak, buffer.getMagnitude (0, 512));
            }
            std::cout << "generated playback peak " << peak << std::endl;
            main.engine.stop();
            pump (main, 300);
            save (main, "generated.png");

            for (const auto& track : main.project)
                if (track[ids::name].toString().endsWith ("Drums"))
                    for (const auto& region : track)
                        if (model::isMidiRegion (region))
                            main.openPianoRoll (region);
            pump (main, 200);
            save (main, "generated-drums.png");
            main.closePianoRoll();

            for (const auto& track : main.project)
                if (track[ids::name].toString().endsWith ("Melody"))
                    for (const auto& region : track)
                        if (model::isMidiRegion (region))
                            main.openPianoRoll (region);
            pump (main, 200);
            save (main, "generated-melody.png");
            main.closePianoRoll();

            // Resized tracks, and one selection shared with the mixer.
            main.project.getChild (0).setProperty (ids::height, 140, nullptr);
            main.project.getChild (1).setProperty (ids::height, 40, nullptr);
            main.selectTrackFromMixer (main.project.getChild (2)[ids::id].toString());
            main.showMixer();
            pump (main, 300);
            save (main, "tracks-resized-mixer.png");
            main.closeMixer();
            pump (main, 200);
            save (main, "tracks-resized.png");

            // Several selected, the Inspector, a hidden track, a new colour.
            main.timeline.selectOnly (main.project.getChild (0)[ids::id].toString());
            main.timeline.extendTrackSelection (main.project.getChild (2)[ids::id].toString());
            main.timeline.changed();
            main.project.getChild (3).setProperty (ids::colour, model::trackPalette()[13].toString(), nullptr);
            main.project.getChild (1).setProperty (ids::hidden, true, nullptr);
            main.timeline.changed();
            main.timeline.selectedRegionId = main.project.getChild (2).getChild (0)[ids::id].toString();
            main.timeline.changed();
            main.toggleInspector();
            pump (main, 300);
            save (main, "inspector-multiselect.png");
            main.inspector.boxesWanted = 0;   // dragged all the way up: the strips take the height
            main.inspector.resized();
            pump (main, 100);
            save (main, "inspector-wide.png");
            main.inspector.boxesWanted = -1;
            main.project.getChild (1).setProperty (ids::hidden, false, nullptr);

            // An audio track with no region selected: Audio Defaults, the Toolbar row and Quick Help.
            {
                auto audio = model::addTrack (main.project, "Vocal", nullptr);
                audio.setProperty (ids::flexMode, "polyphonic", nullptr);
                main.timeline.selectOnly (audio[ids::id].toString());
                main.timeline.selectedRegionId = {};
                main.timeline.changed();
                main.commandManager.invokeDirectly (commands::toggleToolbar, false);
                main.commandManager.invokeDirectly (commands::toggleQuickHelp, false);
                main.quickHelpBox.setText ("Snap\nWhere regions land when you move them (Smart follows the zoom).", juce::dontSendNotification);
                pump (main, 200);
                main.quickHelpBox.setText ("Snap\nWhere regions land when you move them (Smart follows the zoom).", juce::dontSendNotification);
                save (main, "inspector-audio-defaults.png");
                main.commandManager.invokeDirectly (commands::toggleQuickHelp, false);
                main.commandManager.invokeDirectly (commands::toggleToolbar, false);
                main.project.removeChild (audio, nullptr);
                main.timeline.changed();
            }
            main.toggleInspector();

            // Several regions selected, one muted, the Scissors tool and No Overlap.
            main.timeline.selectRegions ({ main.project.getChild (0).getChild (0)[ids::id].toString(),
                                           main.project.getChild (2).getChild (0)[ids::id].toString() });
            main.project.getChild (3).getChild (0).setProperty (ids::mute, true, nullptr);
            main.setTool (TimelineState::Tool::scissors);
            main.dragBox.setSelectedId (2);
            main.timeline.changed();
            pump (main, 200);
            model::addMarker (main.project, 0.0, "Intro", nullptr);
            model::addMarker (main.project, 4 * 4 * model::secondsPerBeat (main.project), "Verse", nullptr);
            model::addMarker (main.project, 8 * 4 * model::secondsPerBeat (main.project), "Hook", nullptr);
            main.project.setProperty (ids::keyRoot, 9, nullptr);
            main.project.setProperty (ids::keyMinor, true, nullptr);
            main.refreshProjectControls();
            pump (main, 100);
            save (main, "regions-edit.png");
            main.setTool (TimelineState::Tool::pointer);
            {
                TrackColourPalette palette ({});
                save (palette, "colour-palette.png", 2.0f);
            }
        }

        // The Claude panel, with a scripted conversation (no network): real tools on the generated song.
        {
            struct Scripted final : ai::Transport
            {
                std::deque<juce::String> replies;
                juce::StringArray sent;
                void post (const juce::String& body, std::function<void (int, const juce::String&)> done) override
                {
                    sent.add (body);
                    const auto reply = replies.empty() ? juce::String (R"J({"content":[{"type":"text","text":"(end)"}],"stop_reason":"end_turn"})J") : replies.front();
                    if (! replies.empty())
                        replies.pop_front();
                    done (200, reply);
                }
            } fake;

            fake.replies = {
                R"J({"content":[{"type":"text","text":"I'll set up a reverb bus and send the chords and melody to it, then measure the mix."},
                    {"type":"tool_use","id":"a1","name":"add_tracks","input":{"tracks":[{"kind":"bus","name":"Reverb"}]}}],"stop_reason":"tool_use"})J",
                R"J({"content":[{"type":"tool_use","id":"a2","name":"add_effect","input":{"track":"Reverb","effect":"reverb","params":{"mix":1,"size":0.7}}},
                    {"type":"tool_use","id":"a3","name":"add_send","input":{"track":"Chords","bus":"Reverb","level_db":-10}},
                    {"type":"tool_use","id":"a4","name":"add_send","input":{"track":"Melody","bus":"Reverb","level_db":-14}}],"stop_reason":"tool_use"})J",
                R"J({"content":[{"type":"tool_use","id":"a5","name":"analyze_mix","input":{}}],"stop_reason":"tool_use"})J",
                R"J({"content":[{"type":"text","text":"Done. The reverb bus is on track 5, with the chords sent at -10 dB and the melody at -14 dB.\n\nThe mix: the master peaks around -9 dBFS, so there's healthy headroom. The bass carries most of the low end, as it should. The chords are a little loud against the melody; try pulling track 3 down 2 dB, or I can do it for you."}],"stop_reason":"end_turn"})J"
            };

            main.removeChildComponent (main.assistantPanel.get());
            main.assistantPanel.reset();
            main.assistant = std::make_unique<ai::Assistant> (fake, *main.assistantTools);
            main.assistant->model = "claude-sonnet-5-5";
            main.assistantPanel = std::make_unique<AssistantPanel> (*main.assistant);
            main.assistantPanel->hasApiKey = [] { return true; };
            main.addChildComponent (*main.assistantPanel);
            main.assistant->onChange = [&main] { main.assistantPanel->refresh(); };

            main.toggleAssistant();
            main.assistant->send ("Add a reverb bus for the chords and melody, then check my mix");
            for (int i = 0; i < 100 && main.assistant->isBusy(); ++i)
                pump (main, 50);
            pump (main, 300);

            // What analyze_mix told Claude
            const auto last = juce::JSON::parse (fake.sent[fake.sent.size() - 1]);
            const auto messages = last["messages"];
            std::cout << "analyze_mix: " << messages[messages.size() - 1]["content"][0]["content"].toString() << std::endl;
            std::cout << "busy after: " << (int) main.assistant->isBusy() << ", tracks: " << model::numTracks (main.project) << std::endl;

            save (main, "assistant.png");
            save (*main.assistantPanel, "assistant-panel.png", 2.0f);

            main.assistant->clear();
            main.toggleAssistant();
            main.removeChildComponent (main.assistantPanel.get());
            main.assistantPanel.reset();
            main.assistant.reset();   // (the fake transport goes out of scope)
        }

        {
            ScreenRecordSettings s;
            s.folder = ScreenRecordSettings::defaultFolder();
            s.withMic = true;
            ScreenRecordPanel panel (s, { "Mic/Inst 1", "Mic/Inst 2", "Line 3", "Line 4" }, {}, {});
            pump (main, 50);
            save (panel, "screen-record.png");

            // The Screen key while recording (as the timer shows it).
            main.screenButton.setName ("1:07");
            main.screenButton.setToggleState (true, juce::dontSendNotification);
            pump (main, 50);
            save (main, "screen-recording-toolbar.png");
            main.screenButton.setName ("Screen");
            main.screenButton.setToggleState (false, juce::dontSendNotification);
        }

        {
            juce::Array<aggregate::Interface> fake;
            fake.add ({ "quantum", "Quantum HD 2", 20, 24, true });
            fake.add ({ "ssl", "SSL 2 MKII", 2, 2, true });
            fake.add ({ "builtin", "MacBook Pro Speakers", 0, 2, false });
            aggregate::Setup s;
            s.uids = { "quantum", "ssl" };
            s.clockUid = "quantum";
            CombineInterfacesPanel panel (s, true, [fake] { return fake; }, {}, {});
            pump (main, 50);
            save (panel, "combine-interfaces.png");
        }

        {
            // Live Loops: fill two scenes from the song's tracks and start one cell.
            juce::Array<juce::ValueTree> filled;
            for (const auto& track : main.project)
                if (track.hasType (ids::TRACK) && track.getNumChildren() > 0 && filled.size() < 5)
                    filled.add (track);
            for (int i = 0; i < filled.size(); ++i)
            {
                model::setLiveCell (main.project, filled[i][ids::id].toString(), 0, filled[i].getChild (0), nullptr);
                if (i % 2 == 0)
                    model::setLiveCell (main.project, filled[i][ids::id].toString(), 1, filled[i].getChild (0), nullptr);
            }
            LiveLoopsGrid grid (main.undoManager, main.engine, main.player);
            grid.setProject (main.project);
            grid.setSize (LiveLoopsGrid::numScenes * 110 + 140, 300);
            if (! filled.isEmpty())
            {
                const auto id = filled[0][ids::id].toString();
                main.player.setLiveCell (id, { model::liveCell (main.project, id, 0)[ids::id].toString(), 0.0, 0.0 });
            }
            main.engine.setPlayheadSeconds (1.3);
            pump (main, 50);
            save (grid, "live-loops.png");
            main.player.clearLiveCells();
            main.project.removeChild (main.project.getChildWithName (ids::LIVELOOPS), nullptr);
        }

        {
            std::vector<PluginPicker::Entry> entries;
            for (const auto* n : { "EQ", "Compressor", "Reverb", "Delay", "Limiter" })
                entries.push_back ({ n, "Audio Tech Pro", {}, {} });
            entries.push_back ({ "Pro-Q 3", "FabFilter", "AU", {} });
            entries.push_back ({ "Pro-C 2", "FabFilter", "AU", {} });
            entries.push_back ({ "1176 Classic Limiter", "Universal Audio", "AU", {} });
            entries.push_back ({ "LA-2A Gray", "Universal Audio", "AU", {} });
            entries.push_back ({ "Valhalla VintageVerb", "Valhalla DSP", "CLAP", {} });
            PluginPicker::Settings settings;
            settings.favorites.add ("AU|FabFilter|Pro-Q 3");
            PluginPicker picker (entries, {}, {}, settings);
            pump (main, 30);
            save (picker, "plugin-picker.png");
            picker.setView (PluginPicker::View::category);
            pump (main, 30);
            save (picker, "plugin-picker-category.png");
            picker.setView (PluginPicker::View::maker);
            picker.search.setText ("comp", true);
            pump (main, 30);
            save (picker, "plugin-picker-search.png");
        }

        {
            // The tool icons, as in the menus.
            struct Sheet final : juce::Component
            {
                void paint (juce::Graphics& g) override
                {
                    g.fillAll (theme::panel);
                    int x = 8;
                    for (const auto tool : toolicons::all)
                    {
                        toolicons::draw (g, tool, juce::Rectangle<float> ((float) x, 8.0f, 28.0f, 28.0f), theme::text);
                        g.setColour (theme::text);
                        g.setFont (juce::FontOptions (10.0f));
                        g.drawText (toolicons::name (tool), x - 10, 40, 48, 12, juce::Justification::centred);
                        x += 52;
                    }
                }
            } sheet;
            sheet.setSize (52 * 11 + 8, 58);
            save (sheet, "tool-icons.png", 2.0f);
        }

        {
            // The Performance window (a made-up reading).
            {
                PerformanceReading r;
                r.cpu = 0.42f; r.peak = 0.71f; r.overloads = 2; r.disk = 0.08f;
                r.device = "Quantum HD 2, 48.0 kHz, 128 samples (2.7 ms)";
                r.tracks = { { "Lead Vox", 0.12f }, { "Drums", 0.09f }, { "Keys", 0.05f }, { "Bass", 0.02f } };
                PerformancePanel panel ([r] { return r; }, {});
                main.performanceMeter.setReading (0.42f, 0.08f);
                pump (main, 30);
                save (panel, "performance.png");
            }

            // AudioSuite with a built-in effect, and the Theme window with meter colours and presets.
            {
                AudioSuitePanel suite;
                main.audioSuiteSettings = juce::ValueTree (ids::INSERT);
                main.audioSuiteSettings.setProperty (ids::effect, builtInEffects().front().type, nullptr);
                std::shared_ptr<BuiltInEffect> fx (BuiltInEffect::create (builtInEffects().front().type).release());
                suite.setEffect (fx, fx->getName(), new EffectPanel (main.audioSuiteSettings, main.undoManager));
                suite.selectionText = [] { return juce::String ("2 regions selected"); };
                pump (main, 50);
                save (suite, "audiosuite.png");

                ThemePanel themed (theme::current(), {}, main.appProperties.getUserSettings());
                pump (main, 30);
                save (themed, "theme-panel-presets.png");
            }

            // The flat skin, and the Theme window.
            theme::Settings flat;
            flat.skin = theme::Skin::flat;
            main.applyTheme (flat);
            main.showMixer();
            pump (main, 200);
            save (main, "flat-mixer.png");
            main.closeMixer();
            pump (main, 100);
            save (main, "flat-arrange.png");
            ThemePanel panel (flat, {});
            pump (main, 30);
            save (panel, "theme-panel.png");

            {
                // The channel EQ window in the flat skin.
                auto t0 = main.project.getChild (0);
                ConsoleStripWindow eq (t0, main.undoManager, [] (float*, int) { return 0; }, [] { return 48000.0; },
                                        [] { return std::array<float, 3> { 0.5f, 0.32f, 4.5f }; });
                pump (main, 30);
                save (eq, "flat-strip-window.png");
            }

            theme::Settings warm { theme::Skin::flat, -175.0f, 1.2f, 0.0f, 0.1f, 0.0f, 30.0f, 1.0f, 1.0f };
            main.applyTheme (warm);
            main.showMixer();
            pump (main, 200);
            save (main, "flat-warm.png");
            main.closeMixer();

            // Studio White: the photo faders, knobs and LED meters; with EQ/Dyn and the bus compressor shown.
            theme::Settings white;
            white.skin = theme::Skin::white;
            main.applyTheme (white);
            main.project.setProperty ("mixerBusComp", true, nullptr);
            main.showMixer();
            main.mixer.relayoutStrips();
            for (int i = 0; i < 20; ++i)
            {
                main.mixer.setMasterLevels (0.7f, 0.6f);
                pump (main, 15);
            }
            save (main, "white-mixer.png");
            main.project.setProperty ("mixerConsole", true, nullptr);
            main.mixer.relayoutStrips();
            pump (main, 200);
            save (main, "white-mixer-console.png");
            main.project.setProperty ("mixerConsole", false, nullptr);
            main.project.setProperty ("mixerBusComp", false, nullptr);
            main.closeMixer();
            pump (main, 100);
            save (main, "white-arrange.png");
            {
                auto t0 = main.project.getChild (0);
                ConsoleStripWindow eq (t0, main.undoManager, [] (float*, int) { return 0; }, [] { return 48000.0; });
                pump (main, 30);
                save (eq, "white-strip-window.png");
            }

            // Xenon: the glass knobs, caps, lit slots, teal meters and panel texture; gradient waveforms in glass regions.
            theme::Settings xenon;
            xenon.skin = theme::Skin::xenon;
            main.applyTheme (xenon);
            main.project.setProperty ("mixerBusComp", true, nullptr);
            main.showMixer();
            main.mixer.relayoutStrips();
            for (int i = 0; i < 20; ++i)
            {
                main.mixer.setMasterLevels (0.7f, 0.6f);
                pump (main, 15);
            }
            save (main, "xenon-mixer.png");
            save (main.mixer.getBusCompPanel(), "xenon-buscomp.png", 2.0f);
            main.mixer.getConsolePanel().showFaceForTesting = true;
            main.mixer.resized();
            pump (main, 100);
            save (main.mixer.getConsolePanel(), "xenon-buscomp-face.png");
            main.mixer.getConsolePanel().showFaceForTesting = false;
            main.mixer.resized();
            main.project.setProperty ("mixerConsole", true, nullptr);
            main.mixer.relayoutStrips();
            pump (main, 200);
            save (main, "xenon-mixer-console.png");
            main.project.setProperty ("mixerConsole", false, nullptr);
            main.project.setProperty ("mixerBusComp", false, nullptr);
            main.closeMixer();
            pump (main, 100);
            save (main, "xenon-arrange.png");
            {
                auto t0 = main.project.getChild (0);
                ConsoleStripWindow eq (t0, main.undoManager, [] (float*, int) { return 0; }, [] { return 48000.0; });
                pump (main, 30);
                save (eq, "xenon-strip-window.png");
            }
            {
                ThemePanel tp (xenon, [] (const theme::Settings&) {});
                pump (main, 30);
                save (tp, "xenon-theme-panel.png");
            }

            // Xenon with Studio White's faders (Themes > Faders)
            xenon.faderStyle = 1;
            main.applyTheme (xenon);
            main.showMixer();
            pump (main, 150);
            save (main, "xenon-white-faders.png");
            main.closeMixer();
            main.applyTheme ({});
            pump (main, 50);
        }

        // Themes > Faders / Knobs / Keys / Meter: the photo pieces, mixed with skins.
        {
            // The keys and knobs up close, on and off.
            struct Board final : juce::Component
            {
                void paint (juce::Graphics& g) override
                {
                    g.fillAll (theme::stripFace);
                    const juce::Colour colours[] { theme::mute, theme::solo, juce::Colour (0xffe2463f), theme::accent };
                    for (int style = 1; style <= 3; ++style)
                    {
                        theme::Settings t = theme::current();
                        t.buttonStyle = style;
                        theme::set (t);
                        for (int i = 0; i < 4; ++i)
                            for (int on = 0; on < 2; ++on)
                            {
                                const auto r = juce::Rectangle<float> (10.0f + (float) (i * 2 + on) * 52.0f, 10.0f + (float) (style - 1) * 40.0f, 44.0f, 30.0f);
                                looks::drawButton (g, r, on == 1, colours[i], false, false);
                                g.setColour (looks::buttonTextColour (on == 1, colours[i]));
                                g.drawText ("MSRI"[i] == 'M' ? "M" : i == 1 ? "S" : i == 2 ? "R" : "I", r, juce::Justification::centred);
                            }
                    }
                    for (int k = 1; k <= 6; ++k)
                    {
                        theme::Settings t = theme::current();
                        t.knobStyle = k;
                        theme::set (t);
                        looks::drawKnob (g, juce::Rectangle<float> (10.0f + (float) (k - 1) * 70.0f, 140.0f, 60.0f, 60.0f), 0.3f,
                                         juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), false, true);
                    }
                    for (int m = 1; m <= 2; ++m)
                    {
                        theme::Settings t = theme::current();
                        t.meterStyle = m;
                        theme::set (t);
                        looks::drawMeter (g, juce::Rectangle<float> (10.0f + (float) (m - 1) * 250.0f, 215.0f, 240.0f, 135.0f), 6.0f);
                    }
                    theme::set ({});
                }
            } board;
            board.setSize (500, 360);
            save (board, "looks-board.png", 2.0f);
        }
        {
            struct Mix { const char* name; theme::Skin skin; int fader, knob, button, meter; };
            for (const auto& m : { Mix { "looks-xenon-black", theme::Skin::xenon, 5, 1, 1, 1 },
                                   Mix { "looks-flat-silver", theme::Skin::flat, 7, 3, 3, 2 },
                                   Mix { "looks-studio-red", theme::Skin::studio, 6, 2, 2, 1 },
                                   Mix { "looks-white-red", theme::Skin::white, 4, 4, 1, 2 } })
            {
                theme::Settings t;
                t.skin = m.skin;
                t.faderStyle = m.fader;
                t.knobStyle = m.knob;
                t.buttonStyle = m.button;
                t.meterStyle = m.meter;
                main.applyTheme (t);
                main.project.setProperty ("mixerBusComp", true, nullptr);
                // (some keys lit, to see them on)
                if (auto t0 = main.project.getChild (0); t0.isValid())
                {
                    t0.setProperty (ids::solo, true, nullptr);
                    t0.setProperty (ids::armed, true, nullptr);
                }
                if (auto t1 = main.project.getChild (1); t1.isValid())
                    t1.setProperty (ids::mute, true, nullptr);
                main.showMixer();
                main.mixer.relayoutStrips();
                for (int i = 0; i < 20; ++i)
                {
                    main.mixer.setMasterLevels (0.7f, 0.6f);
                    pump (main, 15);
                }
                save (main, juce::String (m.name) + "-mixer.png");
                if (auto* firstStrip = findById (main.mixer, "strip0"))
                    save (*firstStrip, juce::String (m.name) + "-strip.png", 2.0f);
                save (main.mixer.getBusCompPanel(), juce::String (m.name) + "-buscomp.png", 2.0f);
                main.mixer.getConsolePanel().showFaceForTesting = true;
                main.mixer.resized();
                pump (main, 60);
                save (main.mixer.getConsolePanel(), juce::String (m.name) + "-buscomp-face.png");
                main.mixer.getConsolePanel().showFaceForTesting = false;
                main.project.setProperty ("mixerBusComp", false, nullptr);
                main.closeMixer();
                pump (main, 80);
                save (main, juce::String (m.name) + "-arrange.png");
                if (auto t0 = main.project.getChild (0); t0.isValid())
                {
                    t0.setProperty (ids::solo, false, nullptr);
                    t0.setProperty (ids::armed, false, nullptr);
                }
                if (auto t1 = main.project.getChild (1); t1.isValid())
                    t1.setProperty (ids::mute, false, nullptr);
            }
            main.applyTheme ({});
            pump (main, 50);
        }

        // The editor with a theme of its own: Studio White tracks over a Xenon mixer.
        {
            theme::Settings editor;
            editor.skin = theme::Skin::white;
            theme::Settings t;
            t.skin = theme::Skin::xenon;
            t.editorTheme = editor.toString();
            main.applyTheme (t);
            main.showMixer();
            pump (main, 150);
            save (main, "editor-own-theme.png");
            main.closeMixer();
            main.applyTheme ({});
            pump (main, 50);
        }

        // REAPER themes (RIFF_REAPER_THEMES: a folder of .ReaperThemeZip files), each loaded as the user would.
        if (const auto dir = juce::File (juce::SystemStats::getEnvironmentVariable ("RIFF_REAPER_THEMES", {})); dir.isDirectory())
        {
            for (const auto& zip : dir.findChildFiles (juce::File::findFiles, false, "*.ReaperThemeZip"))
            {
                juce::String error;
                const auto folder = themepack::install (zip, error);
                if (folder == juce::File())
                {
                    std::cout << "REAPER theme " << zip.getFileName() << ": " << error << std::endl;
                    continue;
                }
                theme::Settings t;
                t.skin = theme::Skin::reaper;
                t.themePack = folder.getFullPathName();
                main.applyTheme (t);
                const auto name = juce::File::createLegalFileName (folder.getFileName()).replaceCharacter (' ', '-');
                main.project.setProperty ("mixerBusComp", true, nullptr);
                main.showMixer();
                main.mixer.relayoutStrips();
                for (int i = 0; i < 20; ++i)
                {
                    main.mixer.setMasterLevels (0.7f, 0.6f);
                    pump (main, 15);
                }
                save (main, "reaper-" + name + "-mixer.png");
                if (auto* firstStrip = findById (main.mixer, "strip0"))
                    save (*firstStrip, "reaper-" + name + "-strip.png", 2.0f);
                main.project.setProperty ("mixerBusComp", false, nullptr);
                main.closeMixer();
                pump (main, 100);
                save (main, "reaper-" + name + "-arrange.png");
                {
                    ThemePanel tp (t, [] (const theme::Settings&) {});
                    pump (main, 30);
                    save (tp, "reaper-theme-panel.png");
                }
            }
            main.applyTheme ({});
            pump (main, 50);
        }

        {
            // This batch: Pro Tools and Ableton style skins; inserts in the Edit window; R / I / MM and comments in the mixer;
            // the bus compressor's chooser; clip gain on a selected audio region.
            TrackHeader::showInserts = true;
            TrackHeader::showIO = true;
            main.resized();
            for (auto* c : main.headers.getChildren())
                c->resized();
            main.headers.resized();
            for (const auto& region : main.project.getChild (0))
                if (region.hasType (ids::REGION))
                {
                    main.timeline.selectOnlyRegion (region[ids::id].toString());
                    break;
                }
            main.timeline.changed();
            for (auto [skin, name] : { std::pair { theme::Skin::protools, "protools" }, std::pair { theme::Skin::ableton, "ableton" } })
            {
                theme::Settings t;
                t.skin = skin;
                main.applyTheme (t);
                pump (main, 150);
                save (main, juce::String (name) + "-arrange.png");
                main.mixer.setShowComments (true);
                main.showMixer();
                main.mixer.relayoutStrips();
                pump (main, 200);
                save (main, juce::String (name) + "-mixer.png");
                {
                    // Selecting channels and clicking the comment boxes never starts typing in them
                    if (main.mixer.onSelectTrack)
                        for (const auto& t : main.project)
                            if (t.hasType (ids::TRACK))
                                main.mixer.onSelectTrack (t[ids::id].toString(), {});
                    pump (main, 50);
                    for (auto* box : main.mixer.commentBoxes)
                    {
                        box->grabKeyboardFocus();
                        if (! box->isReadOnly())
                            std::cout << "comment focus check: FAIL (editable without a double-click)" << std::endl;
                    }
                    pump (main, 50);
                    auto* focused = juce::Component::getCurrentlyFocusedComponent();
                    std::cout << "comment focus check: " << (dynamic_cast<juce::TextEditor*> (focused) != nullptr ? "FAIL (a comment box took the keys)" : "ok") << std::endl;
                }
                main.mixer.setShowComments (false);
                main.closeMixer();
            }
            TrackHeader::showInserts = false;
            TrackHeader::showIO = false;
            main.resized();
            main.headers.resized();
            main.applyTheme ({});
            pump (main, 50);

            // The MultiMeter, fed a second of a stereo mix
            MultiMeterPanel meter (main.engine);
            const auto mix = mixLike();
            for (int block = 0; block < 90; ++block)
            {
                meter.take (mix.getReadPointer (0, block * 1600), mix.getReadPointer (1, block * 1600), 1600);
                pump (main, 20);
            }
            save (meter, "multimeter.png");
            meter.showGoniometer (true);
            pump (main, 30);
            save (meter, "multimeter-goniometer.png");
            meter.showGoniometer (false);
            {
                theme::Settings x;
                x.skin = theme::Skin::xenon;
                main.applyTheme (x);
                meter.resized();
                for (int block = 0; block < 10; ++block)
                {
                    meter.take (mix.getReadPointer (0, block * 1600), mix.getReadPointer (1, block * 1600), 1600);
                    pump (main, 20);
                }
                save (meter, "multimeter-xenon.png");
                main.applyTheme ({});
            }
            {
                // The Dark VU theme (from the mixer picture): its caps, knobs, keys, green meters and a VU on each strip
                theme::Settings dark;
                dark.skin = theme::Skin::flat;
                dark.saturation = 0.15f;
                dark.lightness = -0.12f;
                dark.accentHue = 157.0f;
                dark.meterLow = 0xff2bc98fu;
                dark.meterMid = 0xffd8c24au;
                dark.meterPeak = 0xffe0473au;
                dark.faderStyle = 8;
                dark.knobStyle = 7;
                dark.buttonStyle = 4;
                dark.meterStyle = 3;
                dark.stripVu = true;
                main.applyTheme (dark);
                main.showMixer();
                main.mixer.relayoutStrips();
                for (int i = 0; i < 12; ++i)
                {
                    for (auto* strip : main.mixer.strips)
                        strip->pushLevels (0.12f + 0.02f * (float) (i % 3), 0.1f);
                    pump (main, 30);
                }
                save (main, "dark-vu-mixer.png");
                main.mixer.getConsolePanel().showFaceForTesting = true;
                main.mixer.getConsolePanel().setBounds (0, 0, 600, 300);
                save (main.mixer.getConsolePanel(), "dark-vu-buscomp.png");
                main.mixer.getConsolePanel().showFaceForTesting = false;
                main.closeMixer();
                main.applyTheme ({});
                main.mixer.relayoutStrips();
            }
            {
                // Tracks filled with their colours, shaded bars, your own text colour; then tinted with a dotted grid
                theme::Settings filled;
                filled.trackColouring = 2;
                filled.gridStyle = 1;
                filled.textColour = 0xffffd27a;
                main.applyTheme (filled);
                pump (main, 100);
                save (main, "tracks-filled.png");
                main.showMixer();
                pump (main, 150);
                save (main, "mixer-filled.png");
                main.closeMixer();
                theme::Settings tinted;
                tinted.trackColouring = 1;
                tinted.gridStyle = 3;
                main.applyTheme (tinted);
                pump (main, 100);
                save (main, "tracks-tinted-dotted.png");
                main.applyTheme ({});
            }
            {
                // Parts with themes of their own: the MultiMeter Studio White and the toolbar Xenon, the rest as it is
                theme::Settings white, xenon, mixed;
                white.skin = theme::Skin::white;
                xenon.skin = theme::Skin::xenon;
                mixed.meterTheme = white.toString();
                mixed.toolbarTheme = xenon.toString();
                main.applyTheme (mixed);
                theme::AreaScope::tag (meter, theme::Area::meter);
                main.recolourArea (meter, theme::Area::meter);
                meter.resized();
                for (int block = 0; block < 10; ++block)
                {
                    meter.take (mix.getReadPointer (0, block * 1600), mix.getReadPointer (1, block * 1600), 1600);
                    pump (main, 20);
                }
                save (meter, "multimeter-own-theme.png");
                main.closeMixer();
                pump (main, 50);
                save (main, "toolbar-own-theme.png");
                main.applyTheme ({});
            }
        }
        {
            // Automix, after a run
            AutomixPanel panel (main.project);
            std::vector<automix::Result> results;
            int i = 0;
            for (const auto& t : main.project)
                if (t.hasType (ids::TRACK))
                {
                    automix::Track info;
                    info.name = t[ids::name].toString();
                    results.push_back ({ t[ids::id].toString(), info.name, automix::guessRole (info), 0.0, -3.0 - 2.5 * (double) (i++ % 4) });
                }
            panel.showResults (results, "Hip-Hop");
            pump (main, 30);
            save (panel, "automix.png");

            // A plugin window's Pin / Bypass / Side Chain keys
            auto track = main.project.getChild (1);
            auto chain = model::chainFor (main.project, track[ids::id].toString());
            auto insert = model::insertsOf (chain).isEmpty() ? model::addBuiltinInsert (chain, "compressor", 0, nullptr) : model::insertsOf (chain).getFirst();
            insert.setProperty (ids::sidechain, main.project.getChild (0)[ids::id].toString(), nullptr);
            InsertBar bar (insert, main.project, main.undoManager, true);
            bar.setSize (InsertBar::preferredWidth, 24);
            pump (main, 30);
            save (bar, "insert-bar.png", 2.0f);
            insert.removeProperty (ids::sidechain, nullptr);
        }

        {
            // A Pro Tools session opened (its MIDI tracks; its audio files aren't here)
            main.showImportReport = false;
            main.openImportedProject (juce::File (RIFF_SOURCE_DIR).getChildFile ("Tests/data/protools/TestPTX.ptx"));
            pump (main, 300);
            save (main, "import-protools.png");
        }

        {
            AssistantSettingsPanel settings ("sk-ant-example", "claude-sonnet-5-5", {});
            pump (main, 50);
            save (settings, "assistant-settings.png");
        }

        main.engine.stop();
        return 0;
    }

    /** How long the skins take to draw: the whole mixer, and a meter's update on each strip (what plays at 30 a second). */
    int bench (MainComponent& main)
    {
        main.project.setProperty ("mixerBusComp", true, nullptr);
        main.showMixer();
        pump (main, 200);
        struct Case { const char* name; theme::Settings s; };
        std::vector<Case> cases;
        for (auto [name, skin] : { std::pair { "studio", theme::Skin::studio }, std::pair { "flat", theme::Skin::flat },
                                   std::pair { "white", theme::Skin::white }, std::pair { "xenon", theme::Skin::xenon } })
        {
            theme::Settings s;
            s.skin = skin;
            cases.push_back ({ name, s });
        }
        for (const auto& c : cases)
        {
            for (bool console : { false, true })
            {
                main.applyTheme (c.s);
                main.project.setProperty ("mixerConsole", console, nullptr);
                main.mixer.relayoutStrips();
                pump (main, 100);
                juce::Image image (juce::Image::ARGB, main.mixer.getWidth(), main.mixer.getHeight(), true);
                auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 10; ++i)
                {
                    juce::Graphics g (image);
                    main.mixer.paintEntireComponent (g, false);
                }
                const auto full = (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0;

                // A meter's repaint on every strip: the strip drawn behind it, then the meter.
                t0 = juce::Time::getMillisecondCounterHiRes();
                int meters = 0;
                for (int i = 0; i < 30; ++i)
                    for (auto* strip : main.mixer.strips)
                    {
                        juce::Graphics g (image);
                        g.reduceClipRegion (strip->meter.getBoundsInParent());
                        if (strip->meter.isOpaque())
                            strip->meter.paintEntireComponent (g, false);   // (what an opaque meter's repaint costs)
                        else
                            strip->paintEntireComponent (g, false);
                        ++meters;
                    }
                const auto perSecond = (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0;
                if (std::getenv ("RIFF_BENCH_PARTS") != nullptr)
                {
                    std::function<void (juce::Component&, int)> parts = [&] (juce::Component& comp, int depth)
                    {
                        for (auto* child : comp.getChildren())
                        {
                            if (! child->isVisible() || child->getWidth() <= 0) continue;
                            juce::Image im (juce::Image::ARGB, child->getWidth(), child->getHeight(), true);
                            const auto t = juce::Time::getMillisecondCounterHiRes();
                            for (int i = 0; i < 5; ++i) { juce::Graphics g (im); child->paintEntireComponent (g, false); }
                            const auto ms = (juce::Time::getMillisecondCounterHiRes() - t) / 5.0;
                            if (ms > 0.8)
                            {
                                std::cout << "  PART " << juce::String::repeatedString ("  ", depth) << typeid (*child).name() << " " << child->getComponentID()
                                          << " " << juce::String (ms, 2) << " ms buffered=" << (child->getCachedComponentImage() != nullptr) << std::endl;
                                if (depth < 2) parts (*child, depth + 1);
                            }
                        }
                    };
                    const auto t = juce::Time::getMillisecondCounterHiRes();
                    for (int i = 0; i < 5; ++i) { juce::Graphics g (image); main.mixer.paint (g); }
                    std::cout << "  PART mixer own paint " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t) / 5.0, 2) << std::endl;
                    parts (main.mixer, 0);
                }
                std::cout << "BENCH " << c.name << (console ? " console" : " compact") << ": full mixer " << juce::String (full, 1)
                          << " ms, meters for one frame " << juce::String (perSecond, 2) << " ms (" << main.mixer.strips.size() << " strips)" << std::endl;
            }
        }

        // The edit window: the whole window, the control bar alone (it repaints as the clock runs), and the tracks.
        main.closeMixer();
        pump (main, 100);
        for (const auto& c : cases)
        {
            main.applyTheme (c.s);
            pump (main, 100);
            auto time = [&main] (juce::Component& comp, juce::Rectangle<int> clip, int runs)
            {
                juce::Image image (juce::Image::ARGB, juce::jmax (1, comp.getWidth()), juce::jmax (1, comp.getHeight()), true);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < runs; ++i)
                {
                    juce::Graphics g (image);
                    g.reduceClipRegion (clip);
                    comp.paintEntireComponent (g, false);
                }
                return (juce::Time::getMillisecondCounterHiRes() - t0) / runs;
            };
            const auto window = time (main, main.getLocalBounds(), 5);
            const auto clockArea = main.clockLabel.getBounds().getUnion (main.positionLabel.getBounds());
            const auto clock = time (main, clockArea, 20);
            const auto canvas = time (main.canvas, main.canvas.getLocalBounds(), 5);
            const auto playhead = time (main.canvas, main.canvas.getLocalBounds().withWidth (4).withX (main.canvas.getWidth() / 2), 20);
            std::cout << "BENCH " << c.name << " edit: window " << juce::String (window, 1) << " ms, clock repaint " << juce::String (clock, 2)
                      << " ms, tracks " << juce::String (canvas, 1) << " ms, playhead strip " << juce::String (playhead, 2) << " ms" << std::endl;
        }
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
            auto second = model::addRegion (track, file, s.start + s.length * 0.5, s.length * 0.5, um);
            if (seed == 3)   // the guitars: a fade in and a fade out
            {
                model::setFade (region, true, 2.0, um);
                model::setFade (second, false, 3.0, um);
                second.setProperty (ids::gain, 0.7, um);
            }
            juce::ignoreUnused (region);
        }

        // An instrument with a MIDI region.
        auto keys = model::addTrack (project, "Keys", um, -1, model::instrumentKind);
        auto midi = model::addMidiRegion (keys, 4.36, 8.72, um);
        model::setLoop (midi, true, um);
        model::addTempoChange (project, 32.0, 96.0, um);
        for (int i = 0; i < 6 && i < model::numTracks (project); ++i)
            project.getChild (i).setProperty (ids::icon, std::array<int, 6> { 1, 4, 4, 3, 2, 2 }[(size_t) i], nullptr);   // a slower last section, on the tempo track
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
    const auto folder = (argc > 1 && juce::String (argv[1]) != "--import") ? juce::File (juce::String (argv[1]))
                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("snapshots");
    if (argc > 2 && juce::String (argv[1]) == "fx")
    {
        juce::StringArray types;
        for (int a = 3; a < argc; ++a)
            types.add (argv[a]);
        const auto r = riff::SnapshotTool (juce::File (juce::String (argv[2]))).effectsOnly (types);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return r;
    }
    if (argc > 2 && juce::String (argv[1]) == "perf")
    {
        const auto r = riff::SnapshotTool (juce::File (juce::String (argv[2]))).perfOnly();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return r;
    }
    if (argc > 2 && juce::String (argv[1]) == "meter")
    {
        const auto r = riff::SnapshotTool (juce::File (juce::String (argv[2]))).meterOnly();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return r;
    }
    if (argc > 2 && juce::String (argv[1]) == "mixer")
    {
        const auto r = riff::SnapshotTool (juce::File (juce::String (argv[2]))).mixerOnly();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return r;
    }
    const auto result = (argc > 2 && juce::String (argv[1]) == "--import")
                       ? riff::SnapshotTool (juce::File::getCurrentWorkingDirectory()).importTest (juce::File (juce::String (argv[2])))
                       : riff::SnapshotTool (folder).run();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return result;
}
