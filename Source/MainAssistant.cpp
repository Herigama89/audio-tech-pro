// The Claude assistant in the main window: the panel, its settings, and what
// it may do in the app. (Split from MainComponent.cpp to keep that file readable.)

#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Commands.h"
#include "engine/ConsolePlugins.h"
#include "ui/AutomixPanel.h"
#include <BinaryData.h>

namespace riff
{

namespace
{
    /** Measures the mix offline on a background thread. */
    class MixMeasureJob final : public juce::Thread
    {
    public:
        MixMeasureJob (AudioEngine& e, double s, double en, std::function<void (AudioEngine::MixMeasurement)> callback)
            : Thread ("Measure mix"), engine (e), start (s), end (en), done (std::move (callback)) {}

        void run() override
        {
            auto result = engine.measureMix (start, end, [this] (double) { return ! threadShouldExit(); });
            if (threadShouldExit())
                return;

            juce::MessageManager::callAsync ([cb = done, r = std::move (result)]() mutable { cb (std::move (r)); });
        }

    private:
        AudioEngine& engine;
        double start, end;
        std::function<void (AudioEngine::MixMeasurement)> done;
    };

    /** Commands Claude shouldn't run on its own: they open file dialogs or windows, or it has a better tool. */
    bool isForClaude (juce::CommandID id)
    {
        switch (id)
        {
            case commands::newProject: case commands::openProject: case commands::saveProjectAs:
            case commands::importAudio: case commands::exportSong: case commands::keyCommands:
            case commands::audioSettings: case commands::calibrateLatency: case commands::scanPlugins:
            case commands::showAssistant: case commands::generateMidi: case commands::play: case commands::record:
            case commands::recordScreen: case commands::combineInterfaces:
            case commands::exportStems: case commands::importTracks: case commands::revertToBackup: case commands::consolidateProject:
            case commands::undoHistory: case commands::preferences: case commands::saveAsTemplate: case commands::newAlternative: case commands::eventList: case commands::controllerAssignments: case commands::groupSettings: case commands::ioLabels: case commands::signalFlow: case commands::liveLoops: case commands::openMixerWindow: case commands::openEditorWindow: case commands::themeSettings:
                return false;
            default:
                return true;
        }
    }

    juce::String plainName (const juce::String& name)
    {
        return name.upToFirstOccurrenceOf ("...", false, false).trim();
    }
}

//==============================================================================
void MainComponent::createAssistant()
{
    auto* settings = appProperties.getUserSettings();

    assistantTransport = std::make_unique<ai::HttpTransport> ([this]
    {
        return appProperties.getUserSettings()->getValue ("anthropicApiKey").trim();
    });
    assistantTools = std::make_unique<ai::AssistantTools> (project, undoManager, makeAssistantHooks());
    assistant = std::make_unique<ai::Assistant> (*assistantTransport, *assistantTools);
    assistant->model = settings->getValue ("claudeModel", ai::modelChoices[0]);

    assistantPanel = std::make_unique<AssistantPanel> (*assistant);
    assistantPanel->hasApiKey = [this] { return appProperties.getUserSettings()->getValue ("anthropicApiKey").trim().isNotEmpty(); };
    assistantPanel->onOpenSettings = [this] { showAssistantSettings(); };
    assistantPanel->onClose = [this] { toggleAssistant(); };
    assistantPanel->refresh();
    addChildComponent (*assistantPanel);

    assistant->onChange = [safe = juce::Component::SafePointer<AssistantPanel> (assistantPanel.get())]
    {
        if (safe != nullptr)
            safe->refresh();
    };
}

void MainComponent::toggleAssistant()
{
    if (assistantPanel == nullptr)
        return;

    const bool show = ! assistantPanel->isVisible();
    assistantPanel->setVisible (show);
    claudeButton.setToggleState (show, juce::dontSendNotification);
    resized();
    repaint();

    if (show)
    {
        assistantPanel->refresh();
        if (! assistantPanel->hasApiKey())
            showAssistantSettings();
        else
            assistantPanel->focusInput();
    }
    else
    {
        canvas.grabKeyboardFocus();
    }
}

void MainComponent::showAssistantSettings()
{
    if (assistantSettingsWindow != nullptr)
    {
        assistantSettingsWindow->toFront (true);
        return;
    }

    auto* settings = appProperties.getUserSettings();
    auto* panel = new AssistantSettingsPanel (settings->getValue ("anthropicApiKey"), settings->getValue ("claudeModel", ai::modelChoices[0]),
                                              [this] (const juce::String& key, const juce::String& model)
    {
        auto* s = appProperties.getUserSettings();
        s->setValue ("anthropicApiKey", key);
        s->setValue ("claudeModel", model.isNotEmpty() ? model : ai::modelChoices[0]);
        appProperties.saveIfNeeded();

        if (assistant != nullptr)
            assistant->model = s->getValue ("claudeModel");

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            safe->assistantSettingsWindow.reset();
            if (safe->assistantPanel != nullptr)
            {
                safe->assistantPanel->refresh();
                safe->assistantPanel->focusInput();
            }
        });
    });

    assistantSettingsWindow = std::make_unique<EditorWindow> ("Claude Assistant Settings", panel, false, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->assistantSettingsWindow.reset();
        });
    });
}

//==============================================================================
juce::String MainComponent::assistantManual() const
{
    // The README's user-facing parts: the introduction, "Using it" and the feature list.
    static const juce::String manual = []
    {
        const juce::String readme (BinaryData::README_md, (size_t) BinaryData::README_mdSize);
        auto section = [&readme] (const juce::String& heading)
        {
            const auto start = readme.indexOf (heading);
            if (start < 0)
                return juce::String();
            const auto end = readme.indexOf (start + heading.length(), "\n## ");
            return readme.substring (start, end < 0 ? readme.length() : end).trim();
        };

        const auto introEnd = readme.indexOf ("\n## ");
        return (introEnd > 0 ? readme.substring (0, introEnd).trim() : juce::String()) + "\n\n"
               + section ("## Using it") + "\n\n"
               + section ("### Other DAWs") + "\n\n"
               + section ("## Audio Tech Pro features");
    }();
    return manual;
}

ai::AppHooks MainComponent::makeAssistantHooks()
{
    ai::AppHooks h;

    h.playheadSeconds = [this] { return engine.getPlayheadSeconds(); };
    h.setPlayhead = [this] (double seconds) { engine.setPlayheadSeconds (seconds); };
    h.isPlaying = [this] { return engine.isPlaying(); };
    h.setPlaying = [this] (bool on)
    {
        if (on != engine.isPlaying() && ! engine.isRecording())
            togglePlay();
    };

    h.commandNames = []
    {
        juce::StringArray names;
        for (const auto& c : commands::all())
            if (isForClaude (c.id))
                names.add (plainName (c.name));
        return names;
    };

    h.runCommand = [this] (const juce::String& name)
    {
        for (const auto& c : commands::all())
        {
            if (! isForClaude (c.id) || ! plainName (c.name).equalsIgnoreCase (plainName (name)))
                continue;

            juce::ApplicationCommandInfo info (c.id);
            getCommandInfo (c.id, info);
            if ((info.flags & juce::ApplicationCommandInfo::isDisabled) != 0)
                return false;
            return commandManager.invokeDirectly (c.id, false);
        }
        return false;
    };

    h.keyCommands = [this]
    {
        juce::String text;
        text << "(Preset: " << keyPreset << ")\n";
        for (const auto& c : commands::all())
        {
            juce::StringArray keys;
            for (const auto& k : commandManager.getKeyMappings()->getKeyPressesAssignedToCommand (c.id))
                keys.add (commands::describe (k));
            text << plainName (c.name) << ": " << (keys.isEmpty() ? juce::String ("(no key; in the menus)") : keys.joinIntoString (" or ")) << "\n";
        }
        return text;
    };

    h.manual = [this] { return assistantManual(); };

    h.selectTrack = [this] (const juce::ValueTree& track)
    {
        timeline.selectedTrackId = track[ids::id].toString();
        timeline.changed();
    };

    h.stripParameter = [this] (const juce::String& trackId, console::StripControl c) -> juce::AudioProcessorParameter*
    {
        const auto insert = model::stripPlugin (project, trackId);
        return insert.isValid() ? console::parameterFor (effectRack.getPlugin (insert[ids::id].toString()), insert, c) : nullptr;
    };

    h.busCompParameter = [this] (console::Control c) -> juce::AudioProcessorParameter*
    {
        const auto insert = model::busCompPlugin (project);
        return insert.isValid() ? console::parameterFor (effectRack.getPlugin (insert[ids::id].toString()), insert, c) : nullptr;
    };

    h.pluginInstance = [this] (const juce::String& insertId) { return effectRack.getPlugin (insertId); };

    h.measureMix = [this] (double start, double end, std::function<void (ai::AppHooks::Measurement)> done)
    {
        measureMixForAssistant (start, end, std::move (done));
    };

    return h;
}

void MainComponent::measureMixForAssistant (double start, double end, std::function<void (ai::AppHooks::Measurement)> done)
{
    auto failWith = [&done] (const juce::String& error)
    {
        ai::AppHooks::Measurement m;
        m.error = error;
        done (m);
    };

    if (engine.isRecording())
        return failWith ("The person is recording. Measure the mix after they stop.");
    if (fileCache.isLoading())
        return failWith ("Audio files are still loading. Try again in a moment.");
    if (mixMeasureJob != nullptr && mixMeasureJob->isThreadRunning())
        return failWith ("A measurement is already running.");

    if (engine.isPlaying())
        togglePlay();
    player.rebuildNow();   // include this turn's changes
    engine.beginOfflineRender();

    statusLabel.setText ("Claude is measuring the mix...", juce::dontSendNotification);

    mixMeasureJob = std::make_unique<MixMeasureJob> (engine, start, end,
        [safe = juce::Component::SafePointer<MainComponent> (this), done] (AudioEngine::MixMeasurement result)
    {
        if (safe == nullptr)
            return;

        safe->engine.endOfflineRender();
        safe->statusLabel.setText ("Mix measured", juce::dontSendNotification);

        ai::AppHooks::Measurement m;
        m.master = result.master;
        m.seconds = result.seconds;
        for (int i = 0; i < model::numTracks (safe->project); ++i)
        {
            const auto id = safe->project.getChild (i)[ids::id].toString();
            const auto slot = safe->player.slotOf (id);
            if (juce::isPositiveAndBelow (slot, (int) result.tracks.size()))
                m.tracks[id] = result.tracks[(size_t) slot];
        }
        done (m);
    });
    mixMeasureJob->startThread();
}

//==============================================================================
// Automix
//==============================================================================
void MainComponent::showAutomix()
{
    const juce::String id ("automix");
    if (auto it = editorWindows.find (id); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }
    auto closer = [this, id]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), id]
        {
            if (safe != nullptr)
                safe->editorWindows.erase (id);
        });
    };
    auto* panel = new AutomixPanel (project);
    panel->onRun = [this] (const juce::String& genre, bool cycleOnly) { runAutomix (genre, cycleOnly); };
    editorWindows[id] = std::make_unique<EditorWindow> ("Automix", panel, false, closer);
}

void MainComponent::runAutomix (const juce::String& genre, bool cycleOnly)
{
    auto panelNow = [this]() -> AutomixPanel*
    {
        if (auto it = editorWindows.find ("automix"); it != editorWindows.end())
            return dynamic_cast<AutomixPanel*> (it->second->getContentComponent());
        return nullptr;
    };

    if (engine.isRecording() || fileCache.isLoading() || (mixMeasureJob != nullptr && mixMeasureJob->isThreadRunning()))
    {
        statusLabel.setText ("Automix: wait for recording, loading or another measurement to finish", juce::dontSendNotification);
        return;
    }

    double start = 0.0, end = model::projectEnd (project);
    if (cycleOnly && (double) project.getProperty (ids::cycleEnd, 0.0) > (double) project.getProperty (ids::cycleStart, 0.0))
    {
        start = project.getProperty (ids::cycleStart, 0.0);
        end = project.getProperty (ids::cycleEnd, 0.0);
    }
    if (end <= start + 0.5)
    {
        statusLabel.setText ("Automix: there's nothing to listen to yet", juce::dontSendNotification);
        return;
    }

    if (auto* panel = panelNow())
        panel->setBusy (true);
    if (engine.isPlaying())
        togglePlay();
    player.setIgnoreSolo (true);   // every track heard, whatever's soloed
    player.rebuildNow();
    engine.beginOfflineRender();
    statusLabel.setText ("Automix: listening to every track...", juce::dontSendNotification);

    mixMeasureJob = std::make_unique<MixMeasureJob> (engine, start, end,
        [safe = juce::Component::SafePointer<MainComponent> (this), genre, panelNow] (AudioEngine::MixMeasurement result)
    {
        if (safe == nullptr)
            return;
        safe->engine.endOfflineRender();
        safe->player.setIgnoreSolo (false);
        safe->player.rebuildNow();

        std::vector<automix::Track> tracks;
        for (const auto& track : safe->project)
        {
            if (! track.hasType (ids::TRACK) || model::isBusTrack (track) || model::isVcaTrack (track) || (bool) track[ids::mute])
                continue;   // (buses keep their level: the tracks feeding them are balanced)
            const auto id = track[ids::id].toString();
            const auto slot = safe->player.slotOf (id);
            if (! juce::isPositiveAndBelow (slot, (int) result.tracks.size()))
                continue;
            const auto& stats = result.tracks[(size_t) slot];
            automix::Track t;
            t.id = id;
            t.name = track[ids::name].toString();
            t.instrument = model::isInstrumentTrack (track);
            t.activeRmsDb = stats.activeRmsDb();
            t.faderDb = juce::Decibels::gainToDecibels ((double) track.getProperty (ids::volume, 1.0), -100.0);
            stats.bandShares (t.lowShare, t.midShare, t.highShare);
            tracks.push_back (t);
        }

        const auto results = automix::balance (tracks, genre);
        if (! results.empty())
        {
            safe->undoManager.beginNewTransaction ("Automix (" + genre + ")");
            for (const auto& r : results)
                if (auto track = model::findById (safe->project, r.id); track.isValid())
                    track.setProperty (ids::volume, juce::Decibels::decibelsToGain (r.toDb, -60.0), &safe->undoManager);
        }
        safe->statusLabel.setText (results.empty() ? juce::String ("Automix: nothing played, nothing changed")
                                                   : "Automix: " + juce::String ((int) results.size()) + " faders set for " + genre,
                                   juce::dontSendNotification);
        if (auto* panel = panelNow())
            panel->showResults (results, genre);
    });
    mixMeasureJob->startThread();
}

} // namespace riff
