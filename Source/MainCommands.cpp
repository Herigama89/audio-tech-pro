// The main window's commands: what each menu item and key command does, and
// the menu bar. (Split from MainComponent.cpp to keep that file readable.)

#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Commands.h"
#include "ui/KeyCommands.h"
#include "ui/Theme.h"
#include "AppInfo.h"

namespace riff
{

//==============================================================================
// Command list
//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    for (const auto& info : commands::all())
        ids.add (info.id);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto* c = commands::find (id);
    if (c == nullptr)
        return;

    info.setInfo (c->name, c->description, c->category, 0);

    for (const auto& key : commands::keysFor (keyPreset, id))
        info.addDefaultKeypress (key.getKeyCode(), key.getModifiers());

    const auto track = selectedTrack();
    const auto region = model::findById (project, timeline.selectedRegionId);
    const bool recording = engine.isRecording();

    switch (id)
    {
        case commands::undo:            info.setActive (undoManager.canUndo()); break;
        case commands::redo:            info.setActive (undoManager.canRedo()); break;
        case commands::deleteSelection: info.setActive (region.isValid()); break;
        case commands::quantize:        info.setActive (region.isValid() && model::isMidiRegion (region)); break;
        case commands::setLocatorsBySelection: info.setActive (region.isValid() && ! recording); break;
        case commands::exportSong:      info.setActive (! recording); break;

        case commands::deleteTrack:     info.setActive (track.isValid() && ! recording); break;
        case commands::muteTrack:       info.setActive (track.isValid()); info.setTicked ((bool) track[ids::mute]); break;
        case commands::soloTrack:       info.setActive (track.isValid()); info.setTicked ((bool) track[ids::solo]); break;
        case commands::armTrack:        info.setActive (track.isValid() && ! model::isBusTrack (track));
                                        info.setTicked ((bool) track[ids::armed]); break;
        case commands::showAutomation:  info.setActive (track.isValid()); info.setTicked ((bool) track[ids::showAutomation]); break;

        case commands::toggleCycle:     info.setTicked ((bool) project[ids::cycleEnabled]); info.setActive (! recording); break;
        case commands::toggleMetronome: info.setTicked ((bool) project[ids::metronome]); break;
        case commands::toggleCountIn:   info.setTicked ((bool) project[ids::countIn]); break;

        case commands::showMixer:       info.setTicked (mixer.isVisible()); break;
        case commands::showPianoRoll:   info.setTicked (pianoRoll.isVisible()); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case commands::newProject:             newProject(); break;
        case commands::openProject:            openProject(); break;
        case commands::saveProject:            saveProject(); break;
        case commands::saveProjectAs:          saveProjectAs(); break;
        case commands::importAudio:            importAudio(); break;
        case commands::exportSong:             exportSong(); break;

        case commands::undo:                   undo(); break;
        case commands::redo:                   redo(); break;
        case commands::deleteSelection:        deleteSelection(); break;
        case commands::splitAtPlayhead:        splitAtPlayhead(); break;
        case commands::quantize:               quantizeSelection(); break;
        case commands::setLocatorsBySelection: setLocatorsBySelection(); break;

        case commands::newAudioTrack:          addTrack (model::audioKind); break;
        case commands::newInstrumentTrack:     addTrack (model::instrumentKind); break;
        case commands::newAuxBus:              addTrack (model::busKind); break;
        case commands::deleteTrack:            deleteSelectedTrack(); break;
        case commands::muteTrack:              toggleTrackFlag (ids::mute, "Mute"); break;
        case commands::soloTrack:              toggleTrackFlag (ids::solo, "Solo"); break;
        case commands::armTrack:               toggleTrackFlag (ids::armed, {}); break;
        case commands::showAutomation:         toggleTrackFlag (ids::showAutomation, {}); break;
        case commands::selectPreviousTrack:    selectAdjacentTrack (-1); break;
        case commands::selectNextTrack:        selectAdjacentTrack (1); break;

        case commands::play:                   togglePlay(); break;
        case commands::record:                 toggleRecord(); break;
        case commands::goToStart:              returnToStart(); break;
        case commands::rewindBar:              moveByBars (-1); break;
        case commands::forwardBar:             moveByBars (1); break;
        case commands::toggleCycle:            toggleCycle(); break;
        case commands::toggleMetronome:        toggleMetronome(); break;
        case commands::toggleCountIn:          project.setProperty (ids::countIn, ! (bool) project[ids::countIn], nullptr); break;

        case commands::showMixer:              toggleMixer(); break;
        case commands::showPianoRoll:          togglePianoRoll(); break;
        case commands::zoomIn:                 zoomHorizontally (1.5); break;
        case commands::zoomOut:                zoomHorizontally (1.0 / 1.5); break;
        case commands::keyCommands:            showKeyCommands(); break;
        case commands::audioSettings:          showAudioSettings(); break;
        case commands::calibrateLatency:       showLatencyCalibration(); break;
        case commands::scanPlugins:            showPluginScanner(); break;
        default:                               return false;
    }

    return true;
}

//==============================================================================
// Menu bar (Logic's menu names)
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Navigate", "Record", "Mix", "View" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;
    auto add = [&] (std::initializer_list<juce::CommandID> list)
    {
        for (auto id : list)
            menu.addCommandItem (&commandManager, id);
    };

    switch (index)
    {
        case 0:
            add ({ commands::newProject, commands::openProject });
            menu.addSeparator();
            add ({ commands::saveProject, commands::saveProjectAs });
            menu.addSeparator();
            add ({ commands::importAudio, commands::exportSong });
            break;

        case 1:
            add ({ commands::undo, commands::redo });
            menu.addSeparator();
            add ({ commands::deleteSelection, commands::splitAtPlayhead, commands::quantize });
            break;

        case 2:
            add ({ commands::newAudioTrack, commands::newInstrumentTrack, commands::newAuxBus });
            menu.addSeparator();
            add ({ commands::muteTrack, commands::soloTrack, commands::armTrack, commands::showAutomation });
            menu.addSeparator();
            add ({ commands::selectPreviousTrack, commands::selectNextTrack });
            menu.addSeparator();
            add ({ commands::deleteTrack });
            break;

        case 3:
            add ({ commands::play, commands::goToStart, commands::rewindBar, commands::forwardBar });
            menu.addSeparator();
            add ({ commands::toggleCycle, commands::setLocatorsBySelection });
            break;

        case 4:
            add ({ commands::record, commands::toggleCountIn, commands::toggleMetronome });
            menu.addSeparator();
            add ({ commands::calibrateLatency });
            break;

        case 5:
            add ({ commands::showMixer });
            menu.addSeparator();
            add ({ commands::scanPlugins });
            break;

        case 6:
            add ({ commands::showPianoRoll, commands::zoomIn, commands::zoomOut });
            menu.addSeparator();
            add ({ commands::keyCommands, commands::audioSettings });
            break;

        default:
            break;
    }

    return menu;
}

//==============================================================================
// Key Commands window
//==============================================================================
void MainComponent::showKeyCommands()
{
    if (keyCommandsWindow != nullptr)
    {
        keyCommandsWindow->toFront (true);
        return;
    }

    auto* panel = new KeyCommandsPanel (*commandManager.getKeyMappings(), keyPreset,
                                        [this] (const juce::String& preset) { applyKeyPreset (preset); },
                                        stopReturnsToStart,
                                        [this] (bool on)
                                        {
                                            stopReturnsToStart = on;
                                            appProperties.getUserSettings()->setValue ("stopReturnsToStart", on);
                                            appProperties.saveIfNeeded();
                                        });

    keyCommandsWindow = std::make_unique<EditorWindow> ("Key Commands", panel, true, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->keyCommandsWindow.reset();
        });
    });
}

void MainComponent::applyKeyPreset (const juce::String& preset)
{
    keyPreset = preset;
    appProperties.getUserSettings()->setValue ("keyPreset", preset);

    // The preset's workflow comes with its keys.
    stopReturnsToStart = commands::stopReturnsToStart (preset);
    appProperties.getUserSettings()->setValue ("stopReturnsToStart", stopReturnsToStart);
    if (keyCommandsWindow != nullptr)
        if (auto* panel = dynamic_cast<KeyCommandsPanel*> (keyCommandsWindow->getContentComponent()))
            panel->setStopReturnsToStart (stopReturnsToStart);

    // Registering again picks up the preset's keys as the defaults.
    commandManager.clearCommands();
    commandManager.registerAllCommandsForTarget (this);
    commandManager.getKeyMappings()->resetToDefaultMappings();
    saveKeyMappings();
}

void MainComponent::saveKeyMappings()
{
    // Only the differences from the preset are stored.
    if (auto xml = commandManager.getKeyMappings()->createXml (true))
        appProperties.getUserSettings()->setValue ("keyMappings", xml.get());

    appProperties.saveIfNeeded();
}

//==============================================================================
// Commands that act on the selection
//==============================================================================
juce::ValueTree MainComponent::selectedTrack() const
{
    return model::findById (project, timeline.selectedTrackId);
}

void MainComponent::toggleTrackFlag (const juce::Identifier& flag, const juce::String& undoName)
{
    auto track = selectedTrack();
    if (! track.isValid())
        return;

    // Mute and solo are edits (undoable); arming and showing automation aren't.
    if (undoName.isNotEmpty())
        undoManager.beginNewTransaction (undoName);

    track.setProperty (flag, ! (bool) track[flag], undoName.isNotEmpty() ? &undoManager : nullptr);
}

void MainComponent::selectAdjacentTrack (int delta)
{
    const auto count = model::numTracks (project);
    if (count == 0)
        return;

    const auto current = selectedTrackIndex();
    const auto next = current < 0 ? (delta > 0 ? 0 : count - 1)
                                  : juce::jlimit (0, count - 1, current + delta);

    timeline.selectedTrackId = project.getChild (next)[ids::id].toString();
    timeline.selectedRegionId = {};

    // Keep it on screen.
    const auto top = next * TimelineState::trackHeight;
    const auto visible = canvas.getHeight() - TimelineState::rulerHeight;
    if (top < timeline.scrollY)
        timeline.scrollY = top;
    else if (top + TimelineState::trackHeight > timeline.scrollY + visible)
        timeline.scrollY = top + TimelineState::trackHeight - visible;

    timeline.changed();
}

void MainComponent::moveByBars (int bars)
{
    const double bpm = project.getProperty (ids::tempo, 120.0);
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    const auto barLength = 60.0 / bpm * beatsPerBar;

    // From inside a bar, "back" goes to that bar's start first, as in Logic.
    const auto now = engine.getPlayheadSeconds();
    const auto currentBar = std::floor (now / barLength + 1.0e-6);
    const auto onBarLine = std::abs (now - currentBar * barLength) < 1.0e-3;
    const auto target = bars < 0 ? (onBarLine ? currentBar - 1.0 : currentBar) : currentBar + 1.0;

    const auto time = juce::jmax (0.0, target * barLength);
    engine.setPlayheadSeconds (time);

    // Follow the playhead if it leaves the screen.
    const auto x = timeline.timeToX (time);
    if (x < 0.0f || x > (float) canvas.getWidth())
    {
        timeline.scrollSeconds = juce::jmax (0.0, time - 2.0 * barLength);
        timeline.changed();
    }

    canvas.repaint();
}

void MainComponent::quantizeSelection()
{
    auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || ! model::isMidiRegion (region))
        return;

    const double bpm = project.getProperty (ids::tempo, 120.0);
    undoManager.beginNewTransaction ("Quantize");
    model::quantizeNotes (region, 60.0 / bpm / 4.0, &undoManager);
}

void MainComponent::setLocatorsBySelection()
{
    const auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || engine.isRecording())
        return;

    model::setCycle (project, (double) region[ids::start], model::regionEnd (region), true);
}

void MainComponent::togglePianoRoll()
{
    if (pianoRoll.isVisible())
    {
        closePianoRoll();
        return;
    }

    // The selected MIDI region, or the first one on the selected track.
    auto region = model::findById (project, timeline.selectedRegionId);

    if (! region.isValid() || ! model::isMidiRegion (region))
    {
        region = {};
        for (const auto& r : selectedTrack())
            if (model::isMidiRegion (r))
            {
                region = r;
                break;
            }
    }

    if (region.isValid())
        openPianoRoll (region);
}

void MainComponent::deleteSelectedTrack()
{
    auto track = selectedTrack();
    if (! track.isValid() || engine.isRecording())
        return;

    const auto index = project.indexOf (track);
    undoManager.beginNewTransaction ("Delete track");
    model::deleteTrack (track, &undoManager);

    // Select the track that took its place, so Cmd+Delete can be pressed again.
    const auto count = model::numTracks (project);
    timeline.selectedTrackId = count > 0 ? project.getChild (juce::jmin (index, count - 1))[ids::id].toString() : juce::String();
    timeline.selectedRegionId = {};
    timeline.changed();
}

} // namespace riff
