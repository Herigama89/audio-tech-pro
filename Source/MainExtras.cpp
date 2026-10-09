// Undo History, Settings, templates, project alternatives, Autopunch and the Event List.
#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Theme.h"
#include "ui/EventList.h"
#include "AppInfo.h"

namespace riff
{

namespace
{
    /** Logic's Undo History: every step, oldest at the top; click one to go back (or forward) to just after it. */
    class UndoHistoryPanel final : public juce::Component,
                                   private juce::ListBoxModel,
                                   private juce::ChangeListener
    {
    public:
        explicit UndoHistoryPanel (juce::UndoManager& um) : undoManager (um)
        {
            list.setModel (this);
            list.setRowHeight (22);
            list.setColour (juce::ListBox::backgroundColourId, theme::background);
            addAndMakeVisible (list);
            hint.setText ("Click a step to go back to just after it. Steps below the line were undone; click one to redo up to it.",
                          juce::dontSendNotification);
            hint.setFont (juce::FontOptions (11.5f));
            hint.setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (hint);
            undoManager.addChangeListener (this);
            refresh();
            setSize (360, 480);
        }

        ~UndoHistoryPanel() override { undoManager.removeChangeListener (this); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            hint.setBounds (r.removeFromBottom (34));
            list.setBounds (r);
        }

        void paint (juce::Graphics& g) override { g.fillAll (theme::background); }

    private:
        void refresh()
        {
            done = undoManager.getUndoDescriptions();
            std::reverse (done.strings.begin(), done.strings.end());   // oldest first
            undone = undoManager.getRedoDescriptions();
            list.updateContent();
            list.repaint();
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

        int getNumRows() override { return done.size() + undone.size() + 1; }   // +1: the start, before any edit

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            const bool isStart = row == 0;
            const auto index = row - 1;
            const bool isDone = ! isStart && index < done.size();
            const bool current = (isStart && done.isEmpty()) || (isDone && index == done.size() - 1);

            g.fillAll (current ? theme::selection : (row % 2 == 0 ? theme::laneA : theme::laneB));
            g.setColour (isStart || isDone ? theme::text : theme::dimText);
            g.setFont (juce::FontOptions (12.5f, current ? juce::Font::bold : juce::Font::plain));
            const auto text = isStart ? juce::String ("(Open / start)") : isDone ? done[index] : undone[index - done.size()];
            g.drawText ((current ? "> " : "   ") + (text.isEmpty() ? juce::String ("Edit") : text), 8, 0, w - 16, h, juce::Justification::centredLeft, true);

            if (isDone && index == done.size() - 1 && ! undone.isEmpty())
            {
                g.setColour (theme::accent);
                g.fillRect (0, h - 2, w, 2);
            }
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            const auto target = row;              // steps done after the click: `target`
            auto now = done.size();
            while (now > target && undoManager.undo()) --now;
            while (now < target && undoManager.redo()) ++now;
        }

        juce::UndoManager& undoManager;
        juce::ListBox list;
        juce::Label hint;
        juce::StringArray done, undone;
    };

    /** The Settings window. */
    class PreferencesPanel final : public juce::Component
    {
    public:
        PreferencesPanel (juce::PropertiesFile& p, std::function<void()> changed) : props (p), onChange (std::move (changed))
        {
            addRow (autosave, "Autosave", { "Off", "Every minute", "Every 2 minutes", "Every 5 minutes", "Every 10 minutes" },
                    [] (int minutes) { return minutes == 0 ? 1 : minutes == 1 ? 2 : minutes == 5 ? 4 : minutes == 10 ? 5 : 3; } (props.getIntValue ("autosaveMinutes", 2)),
                    [this] (int id) { props.setValue ("autosaveMinutes", std::array<int, 5> { 0, 1, 2, 5, 10 }[(size_t) id - 1]); });
            addRow (backups, "Backups kept", { "3", "5", "10", "20" },
                    [] (int n) { return n <= 3 ? 1 : n <= 5 ? 2 : n <= 10 ? 3 : 4; } (props.getIntValue ("backupsToKeep", 10)),
                    [this] (int id) { props.setValue ("backupsToKeep", std::array<int, 4> { 3, 5, 10, 20 }[(size_t) id - 1]); });
            addRow (bitDepth, "Recording format", { "16-bit WAV", "24-bit WAV", "32-bit float WAV" },
                    [] (int b) { return b == 16 ? 1 : b == 32 ? 3 : 2; } (props.getIntValue ("recordBitDepth", 24)),
                    [this] (int id) { props.setValue ("recordBitDepth", std::array<int, 3> { 16, 24, 32 }[(size_t) id - 1]); });
            addRow (stopBehaviour, "When you stop", { "Stay where it stopped", "Go back to where play started" },
                    props.getBoolValue ("stopReturnsToStart", false) ? 2 : 1,
                    [this] (int id) { props.setValue ("stopReturnsToStart", id == 2); });

            openLast.setButtonText ("Open the last project when the app starts");
            openLast.setToggleState (props.getBoolValue ("openLastProject", false), juce::dontSendNotification);
            openLast.onClick = [this] { props.setValue ("openLastProject", openLast.getToggleState()); onChange(); };
            openLast.setColour (juce::ToggleButton::textColourId, theme::text);
            addAndMakeVisible (openLast);

            setSize (440, 70 + (int) rows.size() * 36 + 40);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::background);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (12.5f));
            for (const auto& row : rows)
                g.drawText (row.name, row.box->getBounds().withX (16).withWidth (150), juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            int y = 16;
            for (auto& row : rows)
            {
                row.box->setBounds (170, y, getWidth() - 186, 26);
                y += 36;
            }
            openLast.setBounds (16, y + 4, getWidth() - 32, 26);
        }

    private:
        struct Row { juce::String name; juce::ComboBox* box; };

        void addRow (juce::ComboBox& box, const juce::String& name, const juce::StringArray& items, int selected, std::function<void (int)> set)
        {
            box.addItemList (items, 1);
            box.setSelectedId (selected, juce::dontSendNotification);
            box.onChange = [this, &box, set] { set (box.getSelectedId()); onChange(); };
            addAndMakeVisible (box);
            rows.push_back ({ name, &box });
        }

        juce::PropertiesFile& props;
        std::function<void()> onChange;
        juce::ComboBox autosave, backups, bitDepth, stopBehaviour;
        juce::ToggleButton openLast;
        std::vector<Row> rows;
    };

    void launch (juce::Component* content, const juce::String& title, juce::Component* near)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = title;
        options.dialogBackgroundColour = theme::background;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.componentToCentreAround = near;
        options.launchAsync();
    }
}

//==============================================================================
void MainComponent::toggleAutopunch()
{
    const bool on = ! (bool) project[ids::autopunch];
    project.setProperty (ids::autopunch, on, nullptr);

    // The first time, the punch range is the cycle (or the bar at the playhead).
    if (on && (double) project.getProperty (ids::punchOut, 0.0) <= (double) project.getProperty (ids::punchIn, 0.0))
    {
        const auto bar = model::secondsPerBeat (project) * (int) project.getProperty (ids::timeSigNumerator, 4);
        const double cycleStart = project.getProperty (ids::cycleStart, 0.0), cycleEnd = project.getProperty (ids::cycleEnd, 0.0);
        const auto in = cycleEnd > cycleStart ? cycleStart : std::floor (engine.getPlayheadSeconds() / bar) * bar + bar;
        const auto out = cycleEnd > cycleStart ? cycleEnd : in + bar;
        project.setProperty (ids::punchIn, in, nullptr);
        project.setProperty (ids::punchOut, out, nullptr);
    }
    canvas.repaint();
}

void MainComponent::showUndoHistory()
{
    launch (new UndoHistoryPanel (undoManager), "Undo History", this);
}

void MainComponent::showPreferences()
{
    auto* props = appProperties.getUserSettings();
    launch (new PreferencesPanel (*props, [this, props]
    {
        autosaveMinutes = props->getIntValue ("autosaveMinutes", 2);
        engine.setRecordBitDepth (props->getIntValue ("recordBitDepth", 24));
        stopReturnsToStart = props->getBoolValue ("stopReturnsToStart", false);
        props->saveIfNeeded();
    }), "Settings", this);
}

//==============================================================================
juce::File MainComponent::templatesFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (app::name).getChildFile ("Templates");
}

void MainComponent::saveAsTemplate()
{
    auto* window = new juce::AlertWindow ("Save as Template", "A name for the template:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", project[ids::name].toString());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;

        rack.storePluginStates (project);
        effectRack.storePluginStates (model::mixerOf (project));
        templatesFolder().createDirectory();
        const auto file = templatesFolder().getChildFile (juce::File::createLegalFileName (name) + model::fileExtension);
        juce::String error;
        if (! model::save (project, file, error))
            showError ("Couldn't save the template", error);
    }), true);
}

void MainComponent::newAlternative()
{
    if (projectFile == juce::File())
    {
        showError ("Save the project first", "Alternatives are kept beside the project file, so save it once first.");
        return;
    }

    auto* window = new juce::AlertWindow ("New Alternative", "A name for this version (it's saved beside the project, and you carry on in it):",
                                          juce::MessageBoxIconType::NoIcon);
    const auto base = projectFile.getFileNameWithoutExtension().upToFirstOccurrenceOf (" - ", false, false);
    int n = 2;
    while (projectFile.getSiblingFile (base + " - Alt " + juce::String (n) + model::fileExtension).exists())
        ++n;
    window->addTextEditor ("name", base + " - Alt " + juce::String (n));
    window->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;

        // The current state is saved as it is, then copied to the new name, which becomes the open project.
        saveProject ([this, name]
        {
            const auto file = projectFile.getSiblingFile (juce::File::createLegalFileName (name) + model::fileExtension);
            projectFile = file;
            project.removeListener (this);
            project.setProperty (ids::name, name, nullptr);
            project.addListener (this);
            saveProject();
        });
    }), true);
}

void MainComponent::addTemplateAndAlternativeItems (juce::PopupMenu& menu)
{
    // New from a template.
    juce::PopupMenu templates;
    for (const auto& file : templatesFolder().findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension))
        templates.addItem (file.getFileNameWithoutExtension(), [this, file]
        {
            confirmDiscardChanges ([this, file]
            {
                juce::String error;
                auto loaded = model::load (file, error);
                if (! loaded.isValid())
                {
                    showError ("Couldn't open the template", error);
                    return;
                }
                loaded.setProperty (ids::name, "Untitled", nullptr);
                setProject (loaded, {});
            });
        });
    if (templates.getNumItems() == 0)
        templates.addItem ("(none yet: File > Save as Template)", false, false, [] {});
    menu.addSubMenu ("New from Template", templates);

    // Alternatives: the other projects beside this one with the same name start.
    juce::PopupMenu alternatives;
    if (projectFile != juce::File())
    {
        const auto base = projectFile.getFileNameWithoutExtension().upToFirstOccurrenceOf (" - ", false, false);
        for (const auto& file : projectFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension))
        {
            if (! file.getFileNameWithoutExtension().startsWith (base))
                continue;
            alternatives.addItem (file.getFileNameWithoutExtension(), file != projectFile, file == projectFile, [this, file]
            {
                confirmDiscardChanges ([this, file]
                {
                    juce::String error;
                    auto loaded = model::load (file, error);
                    if (! loaded.isValid())
                    {
                        showError ("Couldn't open the alternative", error);
                        return;
                    }
                    loaded.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
                    setProject (loaded, file);
                });
            });
        }
    }
    if (alternatives.getNumItems() <= 1)
        alternatives.addItem ("(File > New Alternative makes one)", false, false, [] {});
    menu.addSubMenu ("Alternatives", alternatives);
}

//==============================================================================
void MainComponent::showEventList()
{
    auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || ! model::isMidiRegion (region))
    {
        showError ("Event List", "Select a MIDI region first.");
        return;
    }

    const auto id = "events:" + region[ids::id].toString();
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
    editorWindows[id] = std::make_unique<EditorWindow> (region[ids::name].toString() + ": Event List",
                                                        new EventListPanel (region, undoManager, [this] { return engine.getPlayheadSeconds(); }),
                                                        true, closer);
}

} // namespace riff

namespace riff
{

//==============================================================================
// MIDI from your devices: step input and controller assignments
//==============================================================================
void MainComponent::handleIncomingMidi()
{
    for (const auto& message : engine.takeIncomingMidi())
    {
        // Step input in the piano roll.
        if ((message.isNoteOn() || message.isNoteOff()) && pianoRoll.isVisible() && pianoRoll.isStepInputOn())
        {
            pianoRoll.stepInput (message);
            continue;
        }

        if (! message.isController())
            continue;

        const auto channel = message.getChannel(), cc = message.getControllerNumber(), value = message.getControllerValue();
        auto assignments = project.getChildWithName (ids::ASSIGNMENTS);

        // Learning: this controller now drives the chosen parameter (replacing what it drove before).
        if (learningTrack.isValid())
        {
            if (! assignments.isValid())
            {
                assignments = juce::ValueTree (ids::ASSIGNMENTS);
                project.appendChild (assignments, nullptr);
            }
            for (int i = assignments.getNumChildren(); --i >= 0;)
                if ((int) assignments.getChild (i)[ids::channel] == channel && (int) assignments.getChild (i)[ids::cc] == cc)
                    assignments.removeChild (i, nullptr);

            juce::ValueTree a (ids::ASSIGN);
            a.setProperty (ids::channel, channel, nullptr);
            a.setProperty (ids::cc, cc, nullptr);
            a.setProperty (ids::target, learningTrack[ids::id].toString(), nullptr);
            a.setProperty (ids::param, learningParam, nullptr);
            assignments.appendChild (a, nullptr);
            statusLabel.setText ("CC " + juce::String (cc) + " (channel " + juce::String (channel) + ") now moves "
                                     + learningTrack[ids::name].toString() + " " + learningParam + ".", juce::dontSendNotification);
            learningTrack = {};
            markDirty();
            continue;
        }

        for (const auto& a : assignments)
        {
            if ((int) a[ids::channel] != channel || (int) a[ids::cc] != cc)
                continue;
            auto track = model::findById (project, a[ids::target].toString());
            if (! track.isValid())
                continue;

            const auto param = a[ids::param].toString();
            const auto trackId = track[ids::id].toString();
            if (param == "volume")
            {
                // The fader's own curve: -60 dB (silence) .. +6 dB, with -12 dB in the middle.
                juce::NormalisableRange<double> range (-60.0, 6.0);
                range.setSkewForCentre (-12.0);
                const auto db = range.convertFrom0to1 (value / 127.0);
                const auto gain = db <= -59.9 ? 0.0 : juce::Decibels::decibelsToGain (db);
                if (! automation.move (trackId, model::volumeParam, gain))
                    track.setProperty (ids::volume, gain, nullptr);
            }
            else if (param == "pan")
            {
                const auto pan = juce::jlimit (-1.0, 1.0, value / 63.5 - 1.0);
                if (! automation.move (trackId, model::panParam, pan))
                    track.setProperty (ids::pan, pan, nullptr);
            }
            else if (param == "mute" || param == "solo")
            {
                // A button: each press toggles.
                const auto key = trackId + param;
                const bool pressed = value >= 64;
                if (pressed && ! lastSwitchState[key])
                {
                    const auto& id = param == "mute" ? ids::mute : ids::solo;
                    track.setProperty (id, ! (bool) track[id], nullptr);
                }
                lastSwitchState[key] = pressed;
            }
        }
    }
}

void MainComponent::showControllerAssignments()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Controller assignments (click one to remove it)");
    auto assignments = project.getChildWithName (ids::ASSIGNMENTS);
    for (int i = 0; i < assignments.getNumChildren(); ++i)
    {
        const auto a = assignments.getChild (i);
        const auto track = model::findById (project, a[ids::target].toString());
        menu.addItem ("CC " + a[ids::cc].toString() + " ch " + a[ids::channel].toString() + "  ->  "
                          + (track.isValid() ? track[ids::name].toString() : juce::String ("(deleted track)")) + " " + a[ids::param].toString(),
                      [assignments, a]() mutable { assignments.removeChild (a, nullptr); });
    }
    if (assignments.getNumChildren() == 0)
        menu.addItem ("None yet: right-click a track or a strip's fader > Learn MIDI Controller", false, false, [] {});
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

} // namespace riff

namespace riff
{

void MainComponent::toggleLibrary (LibraryPanel::Page page)
{
    if (libraryPanel == nullptr)
    {
        libraryPanel = std::make_unique<LibraryPanel> (engine, *appProperties.getUserSettings());
        addChildComponent (*libraryPanel);

        libraryPanel->onClose = [this] { libraryPanel->setVisible (false); engine.stopPreview(); resized(); };
        libraryPanel->onLoadPatch = [this] (const juce::File& file)
        {
            auto track = selectedTrack();
            if (! track.isValid())
            {
                showError ("Library", "Select a track first: the patch goes on it.");
                return;
            }
            juce::String error;
            const auto patch = model::load (file, error);
            if (! patch.isValid())
            {
                showError ("Couldn't open the patch", error);
                return;
            }
            undoManager.beginNewTransaction ("Patch: " + file.getFileNameWithoutExtension());
            model::applyPatch (project, track, patch, &undoManager);
        };
        libraryPanel->onSavePatch = [this] (const juce::String& name)
        {
            auto track = selectedTrack();
            if (! track.isValid())
            {
                showError ("Library", "Select the track whose sound you want to keep first.");
                return;
            }
            rack.storePluginStates (project);
            effectRack.storePluginStates (model::mixerOf (project));
            LibraryPanel::patchesFolder().createDirectory();
            const auto file = LibraryPanel::patchesFolder().getChildFile (juce::File::createLegalFileName (name) + LibraryPanel::patchExtension);
            juce::String error;
            if (! model::save (model::makePatch (project, track), file, error))
                showError ("Couldn't save the patch", error);
        };
        libraryPanel->onAddFile = [this] (const juce::File& file)
        {
            canvas.importFiles ({ file.getFullPathName() }, selectedTrackIndex(), canvas.snap (engine.getPlayheadSeconds()));
        };
    }

    const bool showing = libraryPanel->isVisible() && libraryPanel->getPage() == page;
    libraryPanel->setVisible (! showing);
    if (! showing)
        libraryPanel->showPage (page);
    else
        engine.stopPreview();
    resized();
}

} // namespace riff

namespace riff
{

void MainComponent::toggleSmartControls()
{
    if (smartControls == nullptr)
    {
        smartControls = std::make_unique<SmartControls> (undoManager,
            [this] (const juce::String& insertId) { return effectRack.getPlugin (insertId); },
            [this] (const juce::String& trackId) { return rack.getPlugin (trackId); });
        smartControls->onClose = [this] { smartControls->setVisible (false); resized(); };
        addChildComponent (*smartControls);
    }
    smartControls->setVisible (! smartControls->isVisible());
    smartControls->setTrack (project, selectedTrack());
    resized();
}

} // namespace riff
