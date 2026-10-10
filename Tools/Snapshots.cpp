// Renders the app's main views to PNG files without a display, so the look can
// be checked on a build machine. Build with -DRIFF_BUILD_SNAPSHOTS=ON, then:
//     Snapshots <output folder>

#include "../Source/ui/LiveLoopsGrid.h"
#include "../Source/ui/ToolIcons.h"
#include "../Source/ui/PluginPicker.h"
#include "../Source/ui/ThemePanel.h"
#include <JuceHeader.h>
#include "MainComponent.h"
#include "model/Project.h"
#include "engine/MasterBusCompressor.h"
#include "ui/KeyCommands.h"
#include "ui/Commands.h"
#include "ui/AudioSuitePanel.h"
#include "engine/BuiltInEffects.h"
#include "ui/GeneratorPanel.h"
#include "ui/AssistantPanel.h"
#include "ui/ScreenRecordPanel.h"
#include "ui/CombineInterfacesPanel.h"
#include "ui/ConsoleStripWindow.h"
#include "ui/AudioEditorPanel.h"
#include <deque>
#include "ui/ConsoleLook.h"

namespace riff
{

class SnapshotTool
{
public:
    explicit SnapshotTool (juce::File outputFolder) : out (std::move (outputFolder)) {}

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
            }, [] { return 48000.0; });
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
                ConsoleStripWindow eq (t0, main.undoManager, [] (float*, int) { return 0; }, [] { return 48000.0; });
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
            main.applyTheme ({});
            pump (main, 50);
        }

        {
            AssistantSettingsPanel settings ("sk-ant-example", "claude-sonnet-5-5", {});
            pump (main, 50);
            save (settings, "assistant-settings.png");
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
    const auto result = (argc > 2 && juce::String (argv[1]) == "--import")
                       ? riff::SnapshotTool (juce::File::getCurrentWorkingDirectory()).importTest (juce::File (juce::String (argv[2])))
                       : riff::SnapshotTool (folder).run();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return result;
}
