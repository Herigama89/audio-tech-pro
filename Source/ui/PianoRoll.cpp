#include "PianoRoll.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../model/Generator.h"
#include "../model/NoteTools.h"

namespace riff
{

namespace
{
    bool isBlackKey (int pitch) { return juce::MidiMessage::isMidiNoteBlack (pitch); }
    constexpr float edgeGrab = 6.0f;
}

PianoRoll::PianoRoll (juce::UndoManager& um, AudioEngine& e)
    : undoManager (um), engine (e)
{
    setWantsKeyboardFocus (true);

    titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    quantizeButton.setTooltip ("Snap note starts to the grid (Q). Applies to the selected notes, or all notes.");
    quantizeButton.onClick = [this] { quantizeSelection(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };

    stepButton.setClickingTogglesState (true);
    stepButton.setTooltip ("Step input: play notes on your MIDI keyboard to enter them one grid step after another "
                           "(a chord goes in together), starting at the playhead. Rest moves on without a note.");
    stepButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    stepButton.onClick = [this]
    {
        stepHeld = 0;
        if (stepButton.getToggleState() && region.isValid())
            stepContent = juce::jmax ((double) region[ids::offset],
                                      snapContent (engine.getPlayheadSeconds() - (double) region[ids::start] + (double) region[ids::offset], quantizeGrid()));
    };
    restButton.onClick = [this] { if (isStepInputOn()) advanceStep(); };
    for (auto* b : { &stepButton, &restButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    scaleButton.setTooltip ("Scale Quantize: move notes outside the song's key onto the nearest note of its scale");
    scaleButton.onClick = [this] { scaleQuantizeSelection(); };
    transformButton.setTooltip ("MIDI Transform: humanize, fixed velocity, crescendo, legato, double or half speed, transpose, reverse");
    transformButton.onClick = [this] { showTransformMenu(); };

    for (auto* b : { &quantizeButton, &closeButton, &scaleButton, &transformButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" }, 1);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.setTooltip ("Quantize grid");
    strengthBox.addItemList ({ "100%", "75%", "50%", "100% swing 30%", "100% swing 60%" }, 1);
    strengthBox.setSelectedId (1, juce::dontSendNotification);
    strengthBox.setTooltip ("Quantize strength (how far notes move to the grid), and swing");
    laneBox.addItemList ({ "Velocity", "Modulation", "Pitch Bend", "Sustain", "Expression", "Volume", "Pan", "Breath" }, 1);
    laneBox.setSelectedId (1, juce::dontSendNotification);
    laneBox.setTooltip ("What the lane under the notes shows and edits");
    laneBox.onChange = [this] { repaint(); };
    viewBox.addItemList ({ "Piano Roll", "Score", "Step Editor", "Player" }, 1);
    addChildComponent (playerEditor);
    viewBox.setSelectedId (1, juce::dontSendNotification);
    viewBox.setTooltip ("The region as a piano roll, as notation (Score), or as a grid of steps (Step Editor)");
    viewBox.onChange = [this] { showView (viewBox.getSelectedId()); };
    score.setUndoManager (&undoManager);
    addChildComponent (score);
    addChildComponent (stepEditor);

    for (auto* box : { &gridBox, &strengthBox, &laneBox, &viewBox })
    {
        box->setWantsKeyboardFocus (false);
        addAndMakeVisible (box);
    }

    // FL Studio's toolbar: the tools, the stamp, the snap, scale highlighting, ghost notes and the Tools menu
    const char* toolNames[] { "Draw", "Paint", "Delete", "Mute", "Slice", "Select", "Zoom" };
    const char* toolTips[] {
        "Draw (P): click to add a note (as long as the last one), drag to set its length; drag notes to move them, their right edge to resize",
        "Paint (B): drag to paint notes along the grid", "Delete (D): click or drag over notes to delete them (right-click deletes with any tool)",
        "Mute (T): click or drag over notes to mute or unmute them", "Slice (C): drag down across notes to cut them where you started",
        "Select (E): drag to select notes in a rectangle; drag a selected note to move them all", "Zoom (Z): click to zoom in, Option-click out, drag across a stretch to fill the view with it" };
    for (int i = 0; i < 7; ++i)
    {
        auto& b = toolButtons[i];
        b.setButtonText (toolNames[i]);
        b.setTooltip (toolTips[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (701);
        b.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i] { setTool ((Tool) i); };
        addAndMakeVisible (b);
    }
    toolButtons[0].setToggleState (true, juce::dontSendNotification);

    stampBox.addItem ("No Stamp", 1);
    for (size_t i = 0; i < notetools::stamps().size(); ++i)
        stampBox.addItem (notetools::stamps()[i].name, (int) i + 2);
    stampBox.setSelectedId (1, juce::dontSendNotification);
    stampBox.setTooltip ("Stamp: Draw and Paint put in a whole chord (the clicked note its root)");
    snapBox.addItemList ({ "Snap: Auto", "Snap: None", "Snap: 1/64", "Snap: 1/32", "Snap: 1/16", "Snap: 1/8", "Snap: 1/4", "Snap: Bar",
                           "Snap: 1/16 T", "Snap: 1/8 T" }, 1);
    snapBox.setSelectedId (1, juce::dontSendNotification);
    snapBox.setTooltip ("Snap: where notes go when you draw, move and resize them (Auto follows the zoom). Cmd while dragging: no snap.");
    for (auto* box : { &stampBox, &snapBox })
    {
        box->setWantsKeyboardFocus (false);
        addAndMakeVisible (box);
    }
    scaleHighlightButton.setClickingTogglesState (true);
    scaleHighlightButton.setTooltip ("Scale highlighting: the rows of the song's key lit, the others darker (the root marked)");
    ghostButton.setClickingTogglesState (true);
    ghostButton.setToggleState (true, juce::dontSendNotification);
    ghostButton.setTooltip ("Ghost notes: the other regions' notes, faint, behind these (FL Studio's ghost channels)");
    toolsButton.setTooltip ("FL Studio's tools: Chop, Glue, Strum, Arpeggiate, Flam, Randomize, Scale Levels, Limit, Claw Machine, Articulate... Each has a key (shown in the menu); press ? for every piano roll key.");
    toolsButton.onClick = [this] { showToolsMenu(); };
    for (auto* b : { &scaleHighlightButton, &ghostButton, &toolsButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.8f));
        b->onStateChange = [this] { repaint(); };
        addAndMakeVisible (b);
    }
    toolsButton.onStateChange = nullptr;
}

void PianoRoll::setTool (Tool t)
{
    tool = t;
    for (int i = 0; i < 7; ++i)
        toolButtons[i].setToggleState (i == (int) t, juce::dontSendNotification);
    setMouseCursor (t == Tool::zoom ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

double PianoRoll::snapSeconds() const
{
    if (! region.isValid())
        return 0.25;
    const auto project = region.getParent().getParent();
    const auto beat = model::secondsPerBeat (project);
    switch (snapBox.getSelectedId())
    {
        case 2:  return 0.0;
        case 3:  return beat / 16.0;
        case 4:  return beat / 8.0;
        case 5:  return beat / 4.0;
        case 6:  return beat / 2.0;
        case 7:  return beat;
        case 8:  return beat * (int) project.getProperty (ids::timeSigNumerator, 4);
        case 9:  return beat / 6.0;
        case 10: return beat / 3.0;
        default: return gridSeconds();
    }
}

std::vector<int> PianoRoll::stampIntervals() const
{
    const auto index = stampBox.getSelectedId() - 2;
    if (juce::isPositiveAndBelow (index, (int) notetools::stamps().size()))
        return notetools::stamps()[(size_t) index].intervals;
    return { 0 };
}

juce::Array<juce::ValueTree> PianoRoll::addNotesAt (int pitch, double start, double length)
{
    juce::Array<juce::ValueTree> added;
    for (const auto interval : stampIntervals())
        if (const auto p = pitch + interval; p >= 0 && p <= 127)
            added.add (model::addNote (region, p, start, length, 100, &undoManager));
    return added;
}

void PianoRoll::moveSelection (double shift, int semitones)
{
    if (selection.isEmpty())
        return;
    undoManager.beginNewTransaction (semitones != 0 ? "Transpose notes" : "Move notes");
    for (auto n : selection)
        model::moveNote (n, juce::jmax (0.0, (double) n[ids::start] + shift), juce::jlimit (0, 127, (int) n[ids::pitch] + semitones), &undoManager);
    if (semitones != 0)
        previewNote (selection.getFirst()[ids::pitch]);
}

void PianoRoll::duplicateSelection()
{
    if (selection.isEmpty())
        return;
    // Right after the selection, in whole grid steps (FL's Ctrl+B)
    double from = std::numeric_limits<double>::max(), to = 0.0;
    for (const auto& n : selection)
    {
        from = juce::jmin (from, (double) n[ids::start]);
        to = juce::jmax (to, (double) n[ids::start] + (double) n[ids::length]);
    }
    const auto step = juce::jmax (1.0e-3, snapSeconds() > 0.0 ? snapSeconds() : gridSeconds());
    const auto shift = std::ceil ((to - from) / step - 1.0e-6) * step;
    undoManager.beginNewTransaction ("Duplicate notes");
    selection = notetools::duplicate (region, selection, shift, &undoManager);
    // The region grows to hold them.
    const double offset = region[ids::offset];
    for (const auto& n : selection)
        if ((double) n[ids::start] + (double) n[ids::length] > offset + (double) region[ids::length])
            region.setProperty (ids::length, (double) n[ids::start] + (double) n[ids::length] - offset, &undoManager);
    repaint();
}

std::function<void()> PianoRoll::runOnNotes (const juce::String& name, std::function<void (juce::Array<juce::ValueTree>)> f)
{
    return [this, name, f]
    {
        if (! region.isValid())
            return;
        undoManager.beginNewTransaction (name);
        f (selectedOrAllNotes());
        selection.clear();
        repaint();
    };
}

juce::PopupMenu PianoRoll::toolSubmenu (ToolMenu which)
{
    juce::PopupMenu menu;
    const auto step = juce::jmax (1.0e-3, quantizeGrid());
    switch (which)
    {
        case ToolMenu::strum:
            for (const auto ms : { 10, 20, 35, 60 })
            {
                menu.addItem ("Up, " + juce::String (ms) + " ms", runOnNotes ("Strum", [this, ms] (auto notes) { notetools::strum (notes, ms / 1000.0, false, &undoManager); }));
                menu.addItem ("Down, " + juce::String (ms) + " ms", runOnNotes ("Strum", [this, ms] (auto notes) { notetools::strum (notes, ms / 1000.0, true, &undoManager); }));
            }
            break;
        case ToolMenu::arpeggiate:
        {
            const char* patterns[] { "Up", "Down", "Up and Down" };
            for (int p = 0; p < 3; ++p)
                menu.addItem (juce::String (patterns[p]) + " (at the quantize grid)",
                              runOnNotes ("Arpeggiate", [this, p, step] (auto notes) { notetools::arpeggiate (region, notes, step, p, &undoManager); }));
            break;
        }
        case ToolMenu::randomize:
            menu.addItem ("Gently (5 ms, 8 velocity)", runOnNotes ("Randomize", [this] (auto notes) { notetools::randomize (notes, 0.005, 8, juce::Random::getSystemRandom().nextInt(), &undoManager); }));
            menu.addItem ("More (15 ms, 20 velocity)", runOnNotes ("Randomize", [this] (auto notes) { notetools::randomize (notes, 0.015, 20, juce::Random::getSystemRandom().nextInt(), &undoManager); }));
            menu.addItem ("Velocity only (25)", runOnNotes ("Randomize", [this] (auto notes) { notetools::randomize (notes, 0.0, 25, juce::Random::getSystemRandom().nextInt(), &undoManager); }));
            break;
        case ToolMenu::levels:
        {
            struct Level { const char* name; double percent; int offset; };
            for (const auto& l : { Level { "Compress (50%)", 50.0, 0 }, Level { "Expand (150%)", 150.0, 0 }, Level { "Louder (+10)", 100.0, 10 },
                                   Level { "Softer (-10)", 100.0, -10 }, Level { "Flatten (0%)", 0.0, 0 } })
                menu.addItem (l.name, runOnNotes ("Scale Levels", [this, l] (auto notes) { notetools::scaleVelocity (notes, l.percent, l.offset, &undoManager); }));
            break;
        }
        case ToolMenu::limit:
        {
            struct Range { const char* name; int low, high; };
            for (const auto& r : { Range { "C2 to C4", 36, 60 }, Range { "C3 to C5", 48, 72 }, Range { "C4 to C6", 60, 84 } })
                menu.addItem (r.name, runOnNotes ("Limit", [this, r] (auto notes) { notetools::limit (notes, r.low, r.high, &undoManager); }));
            break;
        }
        case ToolMenu::claw:
        {
            struct Claw { const char* name; int group, keep; };
            for (const auto& c : { Claw { "Keep 1 of every 2", 2, 1 }, Claw { "Keep 2 of every 3", 3, 2 }, Claw { "Keep 3 of every 4", 4, 3 } })
                menu.addItem (c.name, runOnNotes ("Claw Machine", [this, c] (auto notes) { notetools::claw (notes, c.group, c.keep, &undoManager); }));
            break;
        }
        case ToolMenu::articulate:
            for (const auto pc : { 25, 50, 75, 100 })
                menu.addItem (juce::String (pc) + "% of the gap", runOnNotes ("Articulate", [this, pc] (auto notes) { notetools::articulate (notes, pc, &undoManager); }));
            break;
    }
    return menu;
}

std::vector<PianoRoll::NoteShortcut> PianoRoll::noteShortcuts()
{
    // FL Studio's keys for its piano roll tools (Ctrl is Cmd on the Mac; either works). The ones with
    // choices open their menu at the mouse.
    using K = juce::KeyPress;
    constexpr auto cmd = juce::ModifierKeys::commandModifier, alt = juce::ModifierKeys::altModifier;
    const auto step = juce::jmax (1.0e-3, quantizeGrid());
    const auto popup = [this] (ToolMenu which) { return [this, which] { toolSubmenu (which).showMenuAsync (juce::PopupMenu::Options().withMousePosition()); }; };
    return {
        { K ('l', cmd, 0), "Quick Legato", runOnNotes ("Legato", [this] (auto notes) { model::transformNotes (notes, model::Transform::legato, &undoManager); }) },
        { K ('u', alt, 0), "Chop (at the quantize grid)", runOnNotes ("Chop", [this, step] (auto notes) { notetools::chop (region, notes, step, &undoManager); }) },
        { K ('g', cmd, 0), "Glue", runOnNotes ("Glue", [this] (auto notes) { notetools::glue (region, notes, &undoManager); }) },
        { K ('s', alt, 0), "Strum...", popup (ToolMenu::strum) },
        { K ('a', alt, 0), "Arpeggiate...", popup (ToolMenu::arpeggiate) },
        { K ('f', alt, 0), "Flam", runOnNotes ("Flam", [this] (auto notes) { notetools::flam (region, notes, 0.03, &undoManager); }) },
        { K ('r', alt, 0), "Randomize...", popup (ToolMenu::randomize) },
        { K ('x', alt, 0), "Scale Levels (velocity)...", popup (ToolMenu::levels) },
        { K ('k', alt, 0), "Limit (fold into a range)...", popup (ToolMenu::limit) },
        { K ('c', alt, 0), "Claw Machine (thin out)...", popup (ToolMenu::claw) },
        { K ('l', alt, 0), "Articulate (note lengths)...", popup (ToolMenu::articulate) },
        { K ('m', alt, 0), "Mute / Unmute", runOnNotes ("Mute notes", [this] (auto notes)
          {
              bool anyOn = false;
              for (const auto& n : notes)
                  anyOn = anyOn || ! notetools::isMuted (n);
              notetools::setMuted (notes, anyOn, &undoManager);
          }) },
        { K ('y', alt, 0), "Flip Horizontally (reverse)", runOnNotes ("Flip", [this] (auto notes) { model::transformNotes (notes, model::Transform::reverse, &undoManager); }) },
        { K ('y', alt | juce::ModifierKeys::shiftModifier, 0), "Flip Vertically (invert)",
          runOnNotes ("Flip", [this] (auto notes) { model::transformNotes (notes, model::Transform::invertPitch, &undoManager); }) },
        { K ('b', cmd, 0), "Duplicate (also Cmd+D)", [this] { duplicateSelection(); } },
    };
}

bool PianoRoll::runShortcut (const juce::KeyPress& key)
{
    // Ctrl and Cmd count the same, as FL Studio's Ctrl is the Mac's Cmd
    const auto normal = [] (juce::ModifierKeys m)
    {
        auto flags = m.getRawFlags() & (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier
                                        | juce::ModifierKeys::commandModifier | juce::ModifierKeys::ctrlModifier);
        if (flags & juce::ModifierKeys::ctrlModifier)
            flags = (flags & ~juce::ModifierKeys::ctrlModifier) | juce::ModifierKeys::commandModifier;
        return flags;
    };
    const auto code = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    for (auto& s : noteShortcuts())
        if (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) s.key.getKeyCode()) == code
            && normal (s.key.getModifiers()) == normal (key.getModifiers()))
        {
            s.action();
            return true;
        }
    return false;
}

juce::String PianoRoll::shortcutsText()
{
    juce::String text;
    text << "TOOLS\n"
         << "P  Draw (pencil; it remembers the last note's length)\n"
         << "B  Paint (drag to lay notes along the grid)\n"
         << "D  Delete      T  Mute      C  Slice\n"
         << "E  Select      Z  Zoom (Option-click zooms out)\n"
         << "Right-click or right-drag: delete notes, with any tool\n\n"
         << "EDITING\n"
         << "Arrows: Up/Down a semitone (Shift: an octave), Left/Right a grid step\n"
         << "Q  Quantize      Delete  delete the selected notes\n"
         << juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons() << "  Select all      Esc  deselect\n"
         << juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons() << " / "
         << juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons() << " / "
         << juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons() << "  Copy, cut, paste (at the playhead)\n"
         << juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons() << "  Duplicate\n"
         << "Option-scroll: taller or shorter rows      Cmd-scroll: zoom in time\n\n"
         << "NOTE TOOLS (on the selected notes, or all)\n";
    for (const auto& s : noteShortcuts())
        text << s.key.getTextDescriptionWithIcons() << "  " << s.name << "\n";
    return text;
}

void PianoRoll::showShortcuts()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::NoIcon)
                                      .withTitle ("Piano Roll Keys")
                                      .withMessage (shortcutsText())
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void PianoRoll::showToolsMenu()
{
    if (! region.isValid())
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (selection.isEmpty() ? "Tools (all notes)" : "Tools (selected notes)");
    const auto shortcuts = noteShortcuts();
    const auto item = [&menu, &shortcuts] (const juce::String& name)
    {
        for (const auto& s : shortcuts)
            if (s.name == name)
            {
                juce::PopupMenu::Item i (name.upToLastOccurrenceOf ("...", false, false));
                i.setAction (s.action);
                i.shortcutKeyDescription = s.key.getTextDescriptionWithIcons();
                menu.addItem (std::move (i));
                return;
            }
    };
    const auto sub = [this, &menu, &shortcuts] (const juce::String& name, ToolMenu which)
    {
        juce::String keyText;
        for (const auto& s : shortcuts)
            if (s.name == name)
                keyText = "   " + s.key.getTextDescriptionWithIcons();
        menu.addSubMenu (name.upToLastOccurrenceOf ("...", false, false) + keyText, toolSubmenu (which));
    };
    {
        juce::PopupMenu::Item q ("Quick Quantize");
        q.setAction ([this] { quantizeSelection(); });
        q.shortcutKeyDescription = "Q";
        menu.addItem (std::move (q));
    }
    item ("Quick Legato");
    item ("Chop (at the quantize grid)");
    item ("Glue");
    menu.addSeparator();
    sub ("Strum...", ToolMenu::strum);
    sub ("Arpeggiate...", ToolMenu::arpeggiate);
    item ("Flam");
    menu.addSeparator();
    sub ("Randomize...", ToolMenu::randomize);
    sub ("Scale Levels (velocity)...", ToolMenu::levels);
    sub ("Limit (fold into a range)...", ToolMenu::limit);
    sub ("Claw Machine (thin out)...", ToolMenu::claw);
    sub ("Articulate (note lengths)...", ToolMenu::articulate);
    menu.addSeparator();
    item ("Mute / Unmute");
    item ("Flip Horizontally (reverse)");
    item ("Flip Vertically (invert)");
    item ("Duplicate (also Cmd+D)");
    menu.addSeparator();
    menu.addItem ("Keyboard Shortcuts...", [this] { showShortcuts(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsButton));
}

void PianoRoll::mouseEnter (const juce::MouseEvent&)
{
    // The piano roll's keys work as soon as the mouse is over it (unless you're typing somewhere)
    if (isShowing() && ! hasKeyboardFocus (true)
        && dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr)
        grabKeyboardFocus();
}

int PianoRoll::laneController() const
{
    switch (laneBox.getSelectedId())
    {
        case 2: return 1;
        case 3: return model::pitchBend;
        case 4: return 64;
        case 5: return 11;
        case 6: return 7;
        case 7: return 10;
        case 8: return 2;
        default: return -1;
    }
}

double PianoRoll::quantizeGrid() const
{
    const auto beat = model::secondsPerBeat (region.getParent().getParent());
    switch (gridBox.getSelectedId())
    {
        case 1: return beat;
        case 2: return beat / 2.0;
        case 4: return beat / 8.0;
        case 5: return beat / 3.0;
        case 6: return beat / 6.0;
        default: return beat / 4.0;
    }
}

juce::Array<juce::ValueTree> PianoRoll::selectedOrAllNotes() const
{
    juce::Array<juce::ValueTree> notes = selection;
    if (notes.isEmpty())
        for (const auto& note : region)
            if (note.hasType (ids::NOTE))
                notes.add (note);
    return notes;
}

PianoRoll::~PianoRoll()
{
    region.removeListener (this);
}

void PianoRoll::setRegion (juce::ValueTree newRegion)
{
    region.removeListener (this);
    region = newRegion;
    region.addListener (this);
    selection.clear();

    if (! region.isValid())
        return;

    titleLabel.setText (region.getParent()[ids::name].toString() + "  -  " + region[ids::name].toString(),
                        juce::dontSendNotification);
    score.setRegion (region);
    stepEditor.setRegion (region);
    playerEditor.setRegion (region);
    // A Drummer or Session Player region opens on its player's settings.
    const bool isPlayer = region.hasProperty (generator::player::kind);
    viewBox.setItemEnabled (4, isPlayer);
    if (isPlayer != (viewBox.getSelectedId() == 4))
        viewBox.setSelectedId (isPlayer ? 4 : 1);

    scrollContent = juce::jmax (0.0, (double) region[ids::offset] - 0.1);

    // Centre on the notes, or on middle C for an empty region.
    int low = 127, high = 0;
    for (const auto& note : region)
    {
        if (! note.hasType (ids::NOTE))
            continue;
        low = juce::jmin (low, (int) note[ids::pitch]);
        high = juce::jmax (high, (int) note[ids::pitch]);
    }
    const auto centre = low <= high ? (low + high) / 2 : 60;
    const auto visible = juce::jmax (100, getHeight() - headerHeight - toolbarHeight - laneHeight - laneGap);
    scrollY = juce::jlimit (0, juce::jmax (0, 128 * rowHeight - visible), (127 - centre) * rowHeight - visible / 2);

    repaint();
}

void PianoRoll::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    selection.removeAllInstancesOf (child);
    repaint();
}

void PianoRoll::valueTreeParentChanged (juce::ValueTree& tree)
{
    // The region itself was deleted (or undone away): close.
    if (tree == region && ! region.getParent().isValid())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PianoRoll> (this)]
        {
            if (safe != nullptr && safe->onClose)
                safe->onClose();
        });
}

//==============================================================================
// Geometry
//==============================================================================
juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedTop (headerHeight + toolbarHeight).withTrimmedLeft (keyboardWidth).withTrimmedBottom (laneHeight + laneGap);
}

juce::Rectangle<int> PianoRoll::laneArea() const
{
    return getLocalBounds().removeFromBottom (laneHeight).withTrimmedLeft (keyboardWidth);
}

float PianoRoll::laneValueToY (double v) const
{
    const auto a = laneArea().reduced (0, 6);
    return (float) a.getBottom() - (float) juce::jlimit (0.0, 1.0, v) * (float) a.getHeight();
}

double PianoRoll::laneYToValue (float y) const
{
    const auto a = laneArea().reduced (0, 6);
    return juce::jlimit (0.0, 1.0, ((double) a.getBottom() - y) / (double) a.getHeight());
}

juce::ValueTree PianoRoll::controllerPointNear (juce::Point<float> p) const
{
    for (const auto& e : model::controllerEvents (region, laneController()))
        if (juce::Point<float> (contentToX (e[ids::start]), laneValueToY (e[ids::value])).getDistanceFrom (p) <= 6.0f)
            return e;
    return {};
}

float PianoRoll::contentToX (double c) const
{
    return (float) gridArea().getX() + (float) ((c - scrollContent) * pixelsPerSecond);
}

double PianoRoll::xToContent (float x) const
{
    return scrollContent + (x - (float) gridArea().getX()) / pixelsPerSecond;
}

int PianoRoll::pitchToY (int pitch) const
{
    return gridArea().getY() + (127 - pitch) * rowHeight - scrollY;
}

int PianoRoll::yToPitch (int y) const
{
    return juce::jlimit (0, 127, 127 - (y - gridArea().getY() + scrollY) / rowHeight);
}

double PianoRoll::gridSeconds() const
{
    const auto project = region.getParent().getParent();
    const auto beat = model::secondsPerBeat (project);

    if (beat / 4.0 * pixelsPerSecond >= 10.0) return beat / 4.0;   // sixteenths
    if (beat * pixelsPerSecond >= 10.0)       return beat;
    return beat * (int) project.getProperty (ids::timeSigNumerator, 4);
}

double PianoRoll::snapContent (double c, double grid) const
{
    // The grid follows the song's bars (and the tempo track), not the region's start.
    const double regionStart = region[ids::start], offset = region[ids::offset];
    const auto map = model::tempoMapOf (region.getParent().getParent());
    const auto t = regionStart + c - offset;
    const auto gridBeats = grid / map.secondsPerBeatAt (t);
    const auto snapped = map.secondsAt (std::round (map.beatAt (t) / gridBeats) * gridBeats);
    return juce::jmax (0.0, snapped - regionStart + offset);
}

juce::Rectangle<float> PianoRoll::noteBounds (const juce::ValueTree& note) const
{
    const double start = note[ids::start], length = note[ids::length];
    const auto x0 = contentToX (start);
    const auto x1 = contentToX (start + length);
    return { x0, (float) pitchToY (note[ids::pitch]) + 1.0f, juce::jmax (3.0f, x1 - x0), (float) rowHeight - 2.0f };
}

juce::ValueTree PianoRoll::noteAt (juce::Point<float> p) const
{
    for (int i = region.getNumChildren(); --i >= 0;)
    {
        const auto note = region.getChild (i);
        if (note.hasType (ids::NOTE) && noteBounds (note).expanded (0.0f, 0.5f).contains (p))
            return note;
    }
    return {};
}

bool PianoRoll::isSelected (const juce::ValueTree& note) const
{
    return selection.contains (note);
}

//==============================================================================
// Painting
//==============================================================================
void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (10);
    laneBox.setBounds (header.removeFromRight (110));
    header.removeFromRight (10);
    transformButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (4);
    scaleButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (10);
    strengthBox.setBounds (header.removeFromRight (120));
    header.removeFromRight (4);
    gridBox.setBounds (header.removeFromRight (70));
    header.removeFromRight (4);
    quantizeButton.setBounds (header.removeFromRight (76));
    header.removeFromRight (10);
    restButton.setBounds (header.removeFromRight (46));
    header.removeFromRight (4);
    stepButton.setBounds (header.removeFromRight (64));
    viewBox.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (8);
    titleLabel.setBounds (header);

    // The toolbar row
    auto bar = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (toolbarHeight).reduced (8, 3);
    for (auto& b : toolButtons)
    {
        b.setBounds (bar.removeFromLeft (58));
        bar.removeFromLeft (2);
    }
    bar.removeFromLeft (10);
    stampBox.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (6);
    snapBox.setBounds (bar.removeFromLeft (118));
    bar.removeFromLeft (10);
    scaleHighlightButton.setBounds (bar.removeFromLeft (58));
    bar.removeFromLeft (4);
    ghostButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (10);
    toolsButton.setBounds (bar.removeFromLeft (60));
    score.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    stepEditor.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    playerEditor.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
    if (viewBox.getSelectedId() == 1)
    {
        g.setColour (theme::toolbarDark);
        g.fillRect (0, headerHeight, getWidth(), toolbarHeight);
        g.setColour (theme::outline);
        g.fillRect (0, headerHeight + toolbarHeight - 1, getWidth(), 1);
    }

    if (! region.isValid())
        return;

    const auto area = gridArea();
    const auto project = region.getParent().getParent();
    const auto colour = model::trackColour (region.getParent());

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area);

        // Rows: darker for black keys, a line under every C.
        for (int p = 0; p < 128; ++p)
        {
            const auto y = pitchToY (p);
            if (y > area.getBottom() || y + rowHeight < area.getY())
                continue;

            auto rowColour = isBlackKey (p) ? theme::rollBlack : theme::rollWhite;
            if (scaleHighlightButton.getToggleState())
            {
                // Scale highlighting: the key's notes lit, the rest darker, the root marked
                const auto [root, minor] = model::keyAt (project, region[ids::start]);
                const bool inKey = model::isInScale (p, root, minor);
                rowColour = inKey ? theme::rollWhite.brighter (0.08f) : theme::rollBlack.darker (0.25f);
                if (((p - root) % 12 + 12) % 12 == 0)
                    rowColour = rowColour.interpolatedWith (theme::accent, 0.18f);
            }
            g.setColour (rowColour);
            g.fillRect (area.getX(), y, area.getWidth(), rowHeight);

            if (p % 12 == 0)
            {
                g.setColour (theme::gridBar);
                g.fillRect (area.getX(), y + rowHeight - 1, area.getWidth(), 1);
            }
        }

        // Grid lines, aligned to the song's bars.
        const double regionStart = region[ids::start], offset = region[ids::offset];
        const auto map = model::tempoMapOf (project);
        const auto gridBeats = gridSeconds() / model::secondsPerBeat (project);
        const auto beatsPerBar = (double) juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
        const auto bFrom = map.beatAt (juce::jmax (0.0, regionStart + xToContent ((float) area.getX()) - offset));
        const auto bTo = map.beatAt (juce::jmax (0.0, regionStart + xToContent ((float) area.getRight()) - offset));

        for (auto k = (juce::int64) std::floor (bFrom / gridBeats); (double) k * gridBeats <= bTo; ++k)
        {
            const auto b = (double) k * gridBeats;
            const auto x = contentToX (map.secondsAt (b) - regionStart + offset);
            const auto inBar = map.barAndBeat (b).second;
            const bool isBar = inBar < 1e-6 || std::abs (inBar - map.beatsPerBarAt (b)) < 1e-6;
            juce::ignoreUnused (beatsPerBar);
            const bool isBeat = std::abs (std::remainder (b, 1.0)) < 1e-6;
            g.setColour (isBar ? theme::gridBar.brighter (0.3f) : isBeat ? theme::gridBar : theme::gridBeat);
            g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        // Shade outside the region's window.
        const auto w0 = contentToX (offset);
        const auto w1 = contentToX (offset + (double) region[ids::length]);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getY(), w0 - (float) area.getX(), (float) area.getHeight()));
        g.fillRect (juce::Rectangle<float> (w1, (float) area.getY(), (float) area.getRight() - w1, (float) area.getHeight()));

        if (ghostButton.getToggleState())
            paintGhosts (g, area);

        // Notes, brighter for higher velocity, with their names (FL Studio style); muted ones greyed.
        g.setFont (juce::FontOptions (juce::jlimit (8.0f, 12.0f, (float) rowHeight - 3.0f)));
        for (const auto& note : region)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            const auto r = noteBounds (note);
            if (! r.intersects (area.toFloat()))
                continue;
            const auto vel = (float) (int) note[ids::velocity] / 127.0f;
            const bool muted = notetools::isMuted (note);
            const auto fill = muted ? theme::dimText.withAlpha (0.35f) : colour.withMultipliedBrightness (0.55f + 0.6f * vel);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 2.0f);
            if (! muted && r.getWidth() > 10.0f)
            {
                // the velocity as a bar along the note's bottom
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (r.getX() + 1.0f, r.getBottom() - 2.5f, (r.getWidth() - 2.0f) * vel, 1.5f);
            }
            if (rowHeight >= 10 && r.getWidth() > 24.0f)
            {
                g.setColour ((fill.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white).withAlpha (muted ? 0.4f : 0.8f));
                g.drawText (juce::MidiMessage::getMidiNoteName (note[ids::pitch], true, true, 3), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
            }

            if (isSelected (note))
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r, 2.0f, 1.5f);
            }
        }

        // Time Handles over a selection of several notes.
        if (const auto bar = timeHandleBar(); ! bar.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillRect (bar);
            g.setColour (theme::accent);
            g.fillRect (bar.withLeft (bar.getRight() - 8.0f));
        }

        // Playhead
        const auto playX = contentToX (engine.getPlayheadSeconds() - regionStart + offset);
        g.setColour (juce::Colours::white);
        g.fillRect (playX - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight());
    }

    if (! rubberBand.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (rubberBand);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRect (rubberBand, 1.0f);
    }

    paintLane (g);

    // Keyboard
    const juce::Rectangle<int> keys (0, area.getY(), keyboardWidth, area.getHeight());
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (keys);
        g.setFont (juce::FontOptions (9.5f));

        for (int p = 0; p < 128; ++p)
        {
            const auto y = pitchToY (p);
            if (y > keys.getBottom() || y + rowHeight < keys.getY())
                continue;

            const bool black = isBlackKey (p);
            g.setColour (p == keyboardNote ? theme::accent : black ? theme::keysBlack : theme::keysWhite);
            g.fillRect (0, y, black ? keyboardWidth - 16 : keyboardWidth, rowHeight - 1);

            if (p % 12 == 0)
            {
                g.setColour (juce::Colour (0xff333333));
                g.drawText (juce::MidiMessage::getMidiNoteName (p, true, true, 3), 0, y, keyboardWidth - 4, rowHeight,
                            juce::Justification::centredRight);
            }
        }
    }
}

//==============================================================================
// Mouse and keys
//==============================================================================
void PianoRoll::previewNote (int pitch)
{
    engine.sendLiveMidi (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100));
    juce::Timer::callAfterDelay (220, [safe = juce::Component::SafePointer<PianoRoll> (this), pitch]
    {
        if (safe != nullptr)
            safe->engine.sendLiveMidi (juce::MidiMessage::noteOff (1, pitch));
    });
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = Drag::none;
    originals.clear();

    if (! region.isValid() || e.y < headerHeight + toolbarHeight)
        return;

    if (laneArea().contains (e.getPosition()))
    {
        laneMouse (e, true);
        return;
    }
    if (e.y >= gridArea().getBottom())
        return;

    // Time Handles: drag the right end of the bar over the selection to stretch its timing.
    if (const auto bar = timeHandleBar(); ! bar.isEmpty() && bar.expanded (0.0f, 2.0f).contains (e.position) && e.position.x > bar.getRight() - 12.0f)
    {
        undoManager.beginNewTransaction ("Time Handles");
        drag = Drag::timeHandle;
        originals.clear();
        for (const auto& n : selection)
            originals.push_back ({ n, n[ids::start], n[ids::length], n[ids::pitch] });
        handleFrom = std::numeric_limits<double>::max();
        handleTo = 0.0;
        for (const auto& o : originals)
        {
            handleFrom = juce::jmin (handleFrom, o.start);
            handleTo = juce::jmax (handleTo, o.start + o.length);
        }
        return;
    }

    if (e.x < keyboardWidth)
    {
        keyboardNote = yToPitch (e.y);
        engine.sendLiveMidi (juce::MidiMessage::noteOn (1, keyboardNote, (juce::uint8) 100));
        drag = Drag::keyboard;
        repaint();
        return;
    }

    // FL Studio: right-click deletes (and right-drag deletes what it passes), whatever the tool.
    if (e.mods.isPopupMenu() || tool == Tool::erase)
    {
        undoManager.beginNewTransaction ("Delete notes");
        drag = Drag::erase;
        eraseAt (e.position);
        return;
    }
    if (tool == Tool::mute)
    {
        undoManager.beginNewTransaction ("Mute notes");
        drag = Drag::muting;
        const auto under = noteAt (e.position);
        muteTo = under.isValid() ? ! notetools::isMuted (under) : true;
        if (under.isValid())
            notetools::setMuted ({ under }, muteTo, &undoManager);
        return;
    }
    if (tool == Tool::slice || tool == Tool::zoom)
    {
        drag = tool == Tool::slice ? Drag::slice : Drag::zoom;
        rubberBand = {};
        return;
    }
    if (tool == Tool::paint)
    {
        undoManager.beginNewTransaction ("Paint notes");
        drag = Drag::paint;
        lastPaintStart = -1.0;
        lastPaintPitch = -1;
        paintAt (e.position);
        return;
    }

    auto note = noteAt (e.position);
    if (note.isValid())
        lastLength = note[ids::length];   // (FL: touching a note sets the pencil's length)

    if (note.isValid())
    {
        if (e.mods.isShiftDown())
        {
            if (isSelected (note)) selection.removeFirstMatchingValue (note);
            else                   selection.add (note);
        }
        else if (! isSelected (note))
        {
            selection = { note };
        }

        if (! isSelected (note))
        {
            repaint();
            return;
        }

        const auto r = noteBounds (note);
        drag = (r.getRight() - e.position.x <= edgeGrab && r.getWidth() > edgeGrab * 2.0f) ? Drag::resize : Drag::move;
        undoManager.beginNewTransaction (drag == Drag::move ? "Move notes" : "Resize notes");
        dragStartPitch = note[ids::pitch];
        lastPreviewPitch = dragStartPitch;
        previewNote (dragStartPitch);
    }
    else if (e.mods.isShiftDown() || e.mods.isAltDown() || tool == Tool::select)
    {
        // Shift or Option-drag over empty space (or the Select tool): select the notes in a rectangle.
        if (! e.mods.isShiftDown())
            selection.clear();
        drag = Drag::rubberBand;
        rubberBand = {};
        repaint();
        return;
    }
    else
    {
        // Pencil: a new note (or the stamp's chord) at the grid line before the mouse, as long as the last one.
        const auto grid = snapSeconds() > 0.0 ? snapSeconds() : gridSeconds();
        auto start = snapContent (xToContent ((float) e.x));
        if (start > xToContent ((float) e.x))
            start = juce::jmax (0.0, start - grid);

        const auto pitch = yToPitch (e.y);
        undoManager.beginNewTransaction ("Add note");
        selection = addNotesAt (pitch, start, lastLength > 0.0 ? lastLength : grid);
        if (selection.isEmpty())
            return;
        note = selection.getFirst();
        drag = Drag::resize;
        dragStartPitch = pitch;
        previewNote (pitch);
    }

    // Remember where every selected note started; the clicked one first.
    dragStartContent = xToContent ((float) e.x);
    originals.push_back ({ note, note[ids::start], note[ids::length], note[ids::pitch] });
    for (const auto& n : selection)
        if (n != note)
            originals.push_back ({ n, n[ids::start], n[ids::length], n[ids::pitch] });

    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::keyboard)
    {
        const auto pitch = yToPitch (e.y);
        if (pitch != keyboardNote)
        {
            engine.sendLiveMidi (juce::MidiMessage::noteOff (1, keyboardNote));
            keyboardNote = pitch;
            engine.sendLiveMidi (juce::MidiMessage::noteOn (1, keyboardNote, (juce::uint8) 100));
            repaint();
        }
        return;
    }

    if (drag == Drag::velocity || drag == Drag::controller)
    {
        laneMouse (e, false);
        return;
    }

    if (drag == Drag::erase)
    {
        eraseAt (e.position);
        return;
    }
    if (drag == Drag::muting)
    {
        if (const auto under = noteAt (e.position); under.isValid() && notetools::isMuted (under) != muteTo)
            notetools::setMuted ({ under }, muteTo, &undoManager);
        repaint();
        return;
    }
    if (drag == Drag::paint)
    {
        paintAt (e.position);
        return;
    }
    if (drag == Drag::slice)
    {
        rubberBand = juce::Rectangle<float> (e.mouseDownPosition.x - 0.5f, juce::jmin (e.mouseDownPosition.y, e.position.y), 1.5f,
                                             std::abs (e.position.y - e.mouseDownPosition.y) + 1.0f);
        repaint();
        return;
    }
    if (drag == Drag::zoom)
    {
        const auto area = gridArea().toFloat();
        rubberBand = juce::Rectangle<float> (juce::jmin (e.mouseDownPosition.x, e.position.x), area.getY(),
                                             std::abs (e.position.x - e.mouseDownPosition.x), area.getHeight());
        repaint();
        return;
    }

    if (drag == Drag::timeHandle)
    {
        const auto newEnd = juce::jmax (handleFrom + 0.01, snapContent (xToContent ((float) e.x)));
        const auto scale = (newEnd - handleFrom) / juce::jmax (1.0e-6, handleTo - handleFrom);
        for (const auto& o : originals)
        {
            auto n = o.note;
            n.setProperty (ids::start, handleFrom + (o.start - handleFrom) * scale, &undoManager);
            n.setProperty (ids::length, juce::jmax (0.01, o.length * scale), &undoManager);
        }
        repaint();
        return;
    }

    if (drag == Drag::rubberBand)
    {
        rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position).getIntersection (gridArea().toFloat());
        for (const auto& note : region)
            if (note.hasType (ids::NOTE) && noteBounds (note).intersects (rubberBand))
                selection.addIfNotAlreadyThere (note);
        repaint();
        return;
    }

    if (originals.empty() || (drag == Drag::move && e.getDistanceFromDragStart() < 3))
        return;

    const bool useSnap = ! e.mods.isCommandDown();
    const auto delta = xToContent ((float) e.x) - dragStartContent;

    if (drag == Drag::move)
    {
        const auto& anchor = originals.front();
        const auto anchorStart = useSnap ? snapContent (anchor.start + delta) : juce::jmax (0.0, anchor.start + delta);
        const auto shift = anchorStart - anchor.start;
        const auto pitchShift = yToPitch (e.y) - dragStartPitch;

        for (const auto& o : originals)
            model::moveNote (o.note, o.start + shift, o.pitch + pitchShift, &undoManager);

        if (anchor.pitch + pitchShift != lastPreviewPitch)
        {
            lastPreviewPitch = juce::jlimit (0, 127, anchor.pitch + pitchShift);
            previewNote (lastPreviewPitch);
        }
    }
    else if (drag == Drag::resize)
    {
        for (const auto& o : originals)
        {
            auto end = o.start + o.length + delta;
            if (useSnap)
                end = snapContent (end);
            model::resizeNote (o.note, juce::jmax (model::minRegionLength, end - o.start), &undoManager);
        }
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::slice)
    {
        // Cut the notes the line crossed, where it started
        const auto at = snapContent (xToContent (e.mouseDownPosition.x));
        const auto p0 = yToPitch ((int) e.mouseDownPosition.y), p1 = yToPitch (e.y);
        juce::Array<juce::ValueTree> crossed;
        for (const auto& n : region)
            if (n.hasType (ids::NOTE) && (int) n[ids::pitch] >= juce::jmin (p0, p1) && (int) n[ids::pitch] <= juce::jmax (p0, p1))
                crossed.add (n);
        undoManager.beginNewTransaction ("Slice notes");
        notetools::slice (region, crossed, at, &undoManager);
    }
    else if (drag == Drag::zoom)
    {
        const auto area = gridArea();
        if (e.getDistanceFromDragStart() > 6)
        {
            // Fill the view with the stretch dragged across
            const auto from = xToContent (juce::jmin (e.mouseDownPosition.x, e.position.x));
            const auto to = xToContent (juce::jmax (e.mouseDownPosition.x, e.position.x));
            pixelsPerSecond = juce::jlimit (10.0, 4000.0, (double) area.getWidth() / juce::jmax (1.0e-3, to - from));
            scrollContent = juce::jmax (0.0, from);
        }
        else
        {
            const auto anchor = xToContent ((float) e.x);
            pixelsPerSecond = juce::jlimit (10.0, 4000.0, pixelsPerSecond * (e.mods.isAltDown() ? 1.0 / 1.6 : 1.6));
            scrollContent = juce::jmax (0.0, anchor - (e.x - area.getX()) / pixelsPerSecond);
        }
    }
    else if (drag == Drag::resize && ! originals.empty())
        lastLength = originals.front().note[ids::length];   // (the pencil draws the next note this long)

    if (drag == Drag::keyboard)
    {
        engine.sendLiveMidi (juce::MidiMessage::noteOff (1, keyboardNote));
        keyboardNote = -1;
        repaint();
    }

    drag = Drag::none;
    originals.clear();
    rubberBand = {};
    dragEvent = {};
    repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const auto note = noteAt (e.position);
    const bool atEdge = note.isValid() && noteBounds (note).getRight() - e.position.x <= edgeGrab;
    setMouseCursor (atEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isAltDown())
    {
        // Option-wheel: the rows taller or shorter (vertical zoom), keeping the row under the mouse in place
        const auto pitch = yToPitch (e.y);
        rowHeight = juce::jlimit (6, 32, rowHeight + (wheel.deltaY > 0 ? 1 : -1) * juce::jmax (1, rowHeight / 6));
        const auto maxScroll = juce::jmax (0, 128 * rowHeight - gridArea().getHeight());
        scrollY = juce::jlimit (0, maxScroll, (127 - pitch) * rowHeight - (e.y - gridArea().getY()));
    }
    else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const auto anchor = xToContent ((float) e.x);
        pixelsPerSecond = juce::jlimit (10.0, 4000.0, pixelsPerSecond * std::exp (wheel.deltaY * 2.0));
        scrollContent = juce::jmax (0.0, anchor - (e.x - gridArea().getX()) / pixelsPerSecond);
    }
    else
    {
        auto dx = wheel.deltaX, dy = wheel.deltaY;
        if (e.mods.isShiftDown() && juce::exactlyEqual (dx, 0.0f))
            std::swap (dx, dy);

        scrollContent = juce::jmax (0.0, scrollContent - dx * 400.0f / pixelsPerSecond);
        const auto maxScroll = juce::jmax (0, 128 * rowHeight - gridArea().getHeight());
        scrollY = juce::jlimit (0, maxScroll, scrollY - juce::roundToInt (dy * 400.0f));
    }

    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (runShortcut (key))
        return true;
    if (key.getTextCharacter() == '?')
    {
        showShortcuts();
        return true;
    }

    // FL Studio's tool keys
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        const std::pair<juce::juce_wchar, Tool> keys[] { { 'p', Tool::draw }, { 'b', Tool::paint }, { 'd', Tool::erase }, { 't', Tool::mute },
                                                         { 'c', Tool::slice }, { 'e', Tool::select }, { 'z', Tool::zoom } };
        for (const auto& [ch, t] : keys)
            if (c == ch)
            {
                setTool (t);
                return true;
            }
    }

    // Arrows: transpose (Shift: an octave), or move by the grid
    const auto step = snapSeconds() > 0.0 ? snapSeconds() : gridSeconds();
    const auto octave = key.getModifiers().isShiftDown() ? 12 : 1;
    if (key.isKeyCode (juce::KeyPress::upKey) && ! selection.isEmpty())    { moveSelection (0.0, octave); return true; }
    if (key.isKeyCode (juce::KeyPress::downKey) && ! selection.isEmpty())  { moveSelection (0.0, -octave); return true; }
    if (key.isKeyCode (juce::KeyPress::rightKey) && ! selection.isEmpty()) { moveSelection (step, 0); return true; }
    if (key.isKeyCode (juce::KeyPress::leftKey) && ! selection.isEmpty())  { moveSelection (-step, 0); return true; }

    // Duplicate (Cmd+D, or FL's Ctrl+B), copy, cut and paste
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0) || key == juce::KeyPress ('b', juce::ModifierKeys::ctrlModifier, 0))
    {
        duplicateSelection();
        return true;
    }
    static juce::Array<juce::ValueTree> clipboard;
    if ((key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0) || key == juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0))
        && ! selection.isEmpty())
    {
        clipboard.clear();
        for (const auto& n : selection)
            clipboard.add (n.createCopy());
        if (key.getKeyCode() == 'x' || key.getKeyCode() == 'X')
            deleteSelection();
        return true;
    }
    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0) && ! clipboard.isEmpty() && region.isValid())
    {
        // At the playhead (inside the region), or right after the region's notes
        double first = std::numeric_limits<double>::max();
        for (const auto& n : clipboard)
            first = juce::jmin (first, (double) n[ids::start]);
        const auto at = snapContent (engine.getPlayheadSeconds() - (double) region[ids::start] + (double) region[ids::offset]);
        undoManager.beginNewTransaction ("Paste notes");
        selection = notetools::duplicate (region, clipboard, juce::jmax (0.0, at) - first, &undoManager);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (key == juce::KeyPress ('q', 0, 0) || key == juce::KeyPress ('Q', 0, 0))
    {
        quantizeSelection();
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selection.clear();
        for (const auto& note : region)
            if (note.hasType (ids::NOTE))
                selection.add (note);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        selection.clear();
        repaint();
        return true;
    }

    return false; // let Space, Cmd+Z and the rest reach the main window
}

void PianoRoll::deleteSelection()
{
    if (selection.isEmpty())
        return;

    undoManager.beginNewTransaction ("Delete notes");
    const auto toDelete = selection;
    selection.clear();

    for (auto note : toDelete)
        model::deleteNote (note, &undoManager);
}

void PianoRoll::quantizeSelection()
{
    if (! region.isValid())
        return;

    undoManager.beginNewTransaction ("Quantize");

    // The grid, strength and swing from the header.
    const auto strength = strengthBox.getSelectedId() == 2 ? 0.75 : strengthBox.getSelectedId() == 3 ? 0.5 : 1.0;
    const auto swing = strengthBox.getSelectedId() == 4 ? 0.3 : strengthBox.getSelectedId() == 5 ? 0.6 : 0.0;
    model::quantizeNotesTo (selectedOrAllNotes(), quantizeGrid(), strength, swing,
                            (double) region[ids::start] - (double) region[ids::offset], &undoManager);
}

void PianoRoll::showView (int id)
{
    score.setVisible (id == 2);
    stepEditor.setVisible (id == 3);
    playerEditor.setVisible (id == 4);
    for (auto* c : std::initializer_list<juce::Component*> { &laneBox, &stepButton, &restButton, &stampBox, &snapBox,
                                                             &scaleHighlightButton, &ghostButton, &toolsButton })
        c->setVisible (id == 1);
    for (auto& b : toolButtons)
        b.setVisible (id == 1);
    resized();
    repaint();
}

juce::Rectangle<float> PianoRoll::timeHandleBar() const
{
    if (selection.size() < 2)
        return {};
    double from = std::numeric_limits<double>::max(), to = 0.0;
    for (const auto& n : selection)
    {
        from = juce::jmin (from, (double) n[ids::start]);
        to = juce::jmax (to, (double) n[ids::start] + (double) n[ids::length]);
    }
    const auto area = gridArea();
    return { contentToX (from), (float) area.getY(), contentToX (to) - contentToX (from), 6.0f };
}

void PianoRoll::advanceStep()
{
    stepContent += quantizeGrid();

    // Past the region's end: it grows.
    const double offset = region[ids::offset];
    if (stepContent > offset + (double) region[ids::length])
        region.setProperty (ids::length, stepContent - offset, &undoManager);

    engine.setPlayheadSeconds ((double) region[ids::start] + stepContent - offset);
    repaint();
}

void PianoRoll::stepInput (const juce::MidiMessage& message)
{
    if (! isStepInputOn())
        return;

    if (message.isNoteOn())
    {
        if (stepHeld == 0)
            undoManager.beginNewTransaction ("Step input");
        ++stepHeld;
        auto note = model::addNote (region, message.getNoteNumber(), stepContent, quantizeGrid() * 0.95, message.getVelocity(), &undoManager);
        selection = { note };
        repaint();
    }
    else if (message.isNoteOff() && stepHeld > 0)
    {
        // The chord is finished when every key is up: then the next step.
        if (--stepHeld == 0)
            advanceStep();
    }
}

void PianoRoll::scaleQuantizeSelection()
{
    if (! region.isValid())
        return;
    const auto project = region.getParent().getParent();
    undoManager.beginNewTransaction ("Scale Quantize");
    const auto [root, minor] = model::keyAt (project, region[ids::start]);   // the key where the region is (key changes count)
    model::scaleQuantize (selectedOrAllNotes(), root, minor, &undoManager);
}

void PianoRoll::showTransformMenu()
{
    using T = model::Transform;
    juce::PopupMenu menu;
    menu.addSectionHeader (selection.isEmpty() ? "Transform (all notes)" : "Transform (selected notes)");
    const std::pair<T, const char*> items[] {
        { T::humanize, "Humanize (timing and velocity)" }, { T::fixedVelocity, "Fixed Velocity (100)" },
        { T::crescendo, "Crescendo" }, { T::decrescendo, "Decrescendo" }, { T::legato, "Legato" },
        { T::doubleSpeed, "Double Speed" }, { T::halfSpeed, "Half Speed" },
        { T::transposeUp, "Transpose +12" }, { T::transposeDown, "Transpose -12" }, { T::reverse, "Reverse Position" },
        { T::randomVelocity, "Random Velocity" }, { T::invertPitch, "Pitch Inversion" }, { T::reversePitch, "Reverse Pitch Order" },
        { T::noteRepeat, "Note Repeat (at the quantize grid)" } };
    for (const auto& [t, name] : items)
        menu.addItem (name, [this, t = t, name = juce::String (name)]
        {
            undoManager.beginNewTransaction ("Transform: " + name);
            model::transformNotes (selectedOrAllNotes(), t, &undoManager, t == T::noteRepeat ? juce::roundToInt (quantizeGrid() * 1000.0) : 0);
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&transformButton));
}

void PianoRoll::paintLane (juce::Graphics& g)
{
    const auto lane = laneArea();
    g.setColour (theme::toolbarDark);
    g.fillRect (getLocalBounds().removeFromBottom (laneHeight + laneGap).removeFromTop (laneGap));
    g.setColour (theme::laneA);
    g.fillRect (lane);
    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromBottom (laneHeight).withWidth (keyboardWidth));

    const auto controller = laneController();
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (controller < 0 ? juce::String ("Velocity") : model::controllerName (controller),
                      getLocalBounds().removeFromBottom (laneHeight).withWidth (keyboardWidth).reduced (4), juce::Justification::centred, 2);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lane);

    // Guide lines: the middle (pitch bend's centre), and the top and bottom.
    g.setColour (theme::gridBeat);
    g.fillRect ((float) lane.getX(), laneValueToY (0.5), (float) lane.getWidth(), 1.0f);

    const auto colour = model::trackColour (region.getParent());
    if (controller < 0)
    {
        for (const auto& note : region)
        {
            if (! note.hasType (ids::NOTE))
                continue;
            const auto x = contentToX (note[ids::start]);
            const auto y = laneValueToY ((int) note[ids::velocity] / 127.0);
            g.setColour (isSelected (note) ? juce::Colours::white : colour.brighter (0.3f));
            g.fillRect (x, y, 2.0f, (float) lane.getBottom() - y);
            g.fillEllipse (x - 2.5f, y - 2.5f, 7.0f, 7.0f);
        }
        return;
    }

    // Controller values hold until the next point.
    const auto events = model::controllerEvents (region, controller);
    juce::Path line;
    for (int i = 0; i < events.size(); ++i)
    {
        const auto x = contentToX (events[i][ids::start]);
        const auto y = laneValueToY (events[i][ids::value]);
        if (i == 0) line.startNewSubPath (x, y);
        else        line.lineTo (x, line.getCurrentPosition().y), line.lineTo (x, y);
    }
    if (! events.isEmpty())
        line.lineTo ((float) lane.getRight(), line.getCurrentPosition().y);
    g.setColour (colour.brighter (0.3f));
    g.strokePath (line, juce::PathStrokeType (1.5f));
    for (const auto& e : events)
    {
        g.setColour (e == dragEvent ? juce::Colours::white : colour.brighter (0.6f));
        g.fillEllipse (contentToX (e[ids::start]) - 3.0f, laneValueToY (e[ids::value]) - 3.0f, 6.0f, 6.0f);
    }
}

void PianoRoll::laneMouse (const juce::MouseEvent& e, bool down)
{
    const auto controller = laneController();
    const auto value = laneYToValue ((float) e.y);

    if (controller < 0)
    {
        // Velocity: the stalks the mouse passes over (or the one clicked) take its height.
        if (down)
        {
            undoManager.beginNewTransaction ("Velocity");
            drag = Drag::velocity;
            lastLaneX = (float) e.x - 4.0f;
        }
        const auto x0 = juce::jmin (lastLaneX, (float) e.x) - 3.0f, x1 = juce::jmax (lastLaneX, (float) e.x) + 3.0f;
        for (auto note : region)
            if (note.hasType (ids::NOTE))
                if (const auto x = contentToX (note[ids::start]); x >= x0 && x <= x1 && (selection.isEmpty() || isSelected (note)))
                    model::setNoteVelocity (note, juce::jlimit (1, 127, juce::roundToInt (value * 127.0)), &undoManager);
        lastLaneX = (float) e.x;
        repaint();
        return;
    }

    const auto time = juce::jmax (0.0, xToContent ((float) e.x));
    if (down)
    {
        drag = Drag::controller;
        if (auto near = controllerPointNear (e.position); near.isValid())
        {
            if (e.mods.isAltDown())
            {
                undoManager.beginNewTransaction ("Remove controller point");
                region.removeChild (near, &undoManager);
                drag = Drag::none;
                repaint();
                return;
            }
            undoManager.beginNewTransaction ("Move controller point");
            dragEvent = near;
            laneDrawing = false;
        }
        else
        {
            undoManager.beginNewTransaction ("Draw " + model::controllerName (controller));
            dragEvent = model::addControllerEvent (region, controller, time, value, &undoManager);
            laneDrawing = true;
        }
        lastLaneX = (float) e.x;
        repaint();
        return;
    }

    // Dragging a point moves it; dragging away from it draws more (one every few pixels).
    if (dragEvent.isValid() && (! laneDrawing || std::abs ((float) e.x - lastLaneX) < 8.0f))
    {
        dragEvent.setProperty (ids::value, value, &undoManager);
        if (! e.mods.isShiftDown())
            dragEvent.setProperty (ids::start, time, &undoManager);
    }
    else
    {
        dragEvent = model::addControllerEvent (region, controller, time, value, &undoManager);
        lastLaneX = (float) e.x;
    }
    repaint();
}

void PianoRoll::eraseAt (juce::Point<float> p)
{
    if (auto under = noteAt (p); under.isValid())
    {
        selection.removeAllInstancesOf (under);
        model::deleteNote (under, &undoManager);
        repaint();
    }
}

void PianoRoll::paintAt (juce::Point<float> p)
{
    // Notes along the grid as the mouse passes (the stamp's chord each time), as long as the last note drawn
    const auto grid = snapSeconds() > 0.0 ? snapSeconds() : gridSeconds();
    const auto length = lastLength > 0.0 ? lastLength : grid;
    auto start = snapContent (xToContent (p.x), grid);
    if (start > xToContent (p.x))
        start = juce::jmax (0.0, start - grid);
    const auto pitch = yToPitch ((int) p.y);
    if (std::abs (start - lastPaintStart) < 1.0e-6 && pitch == lastPaintPitch)
        return;
    // Not on top of a note that's already there
    for (const auto& n : region)
        if (n.hasType (ids::NOTE) && (int) n[ids::pitch] == pitch && std::abs ((double) n[ids::start] - start) < 1.0e-4)
            return;
    // Painting on: a note every `length` (not a note in each grid cell under a long one)
    if (lastPaintStart >= 0.0 && pitch == lastPaintPitch && start > lastPaintStart && start < lastPaintStart + length - 1.0e-6)
        return;
    lastPaintStart = start;
    lastPaintPitch = pitch;
    selection = addNotesAt (pitch, start, length);
    if (lastPaintStart == start)
        previewNote (pitch);
    repaint();
}

void PianoRoll::paintGhosts (juce::Graphics& g, juce::Rectangle<int> area)
{
    // The other MIDI regions playing at the same time (this track's others, and the other tracks'), faint
    const auto project = region.getParent().getParent();
    const double regionStart = region[ids::start], offset = region[ids::offset];
    const auto viewFrom = regionStart + xToContent ((float) area.getX()) - offset;
    const auto viewTo = regionStart + xToContent ((float) area.getRight()) - offset;
    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        const auto ghostColour = model::trackColour (track).withAlpha (0.28f);
        for (const auto& other : track)
        {
            if (other == region || ! model::isMidiRegion (other))
                continue;
            const double otherStart = other[ids::start], otherOffset = other[ids::offset], otherLength = other[ids::length];
            if (otherStart > viewTo || otherStart + otherLength < viewFrom)
                continue;
            g.setColour (ghostColour);
            for (const auto& n : other)
            {
                if (! n.hasType (ids::NOTE))
                    continue;
                const double nStart = n[ids::start];
                if (nStart < otherOffset || nStart >= otherOffset + otherLength)
                    continue;
                const auto songTime = otherStart + nStart - otherOffset;            // where it plays
                const auto c = songTime - regionStart + offset;                      // in this region's content time
                const auto x0 = contentToX (c), x1 = contentToX (c + (double) n[ids::length]);
                if (x1 < (float) area.getX() || x0 > (float) area.getRight())
                    continue;
                g.fillRoundedRectangle (juce::Rectangle<float> (x0, (float) pitchToY (n[ids::pitch]) + 2.0f, juce::jmax (2.0f, x1 - x0), (float) rowHeight - 4.0f), 2.0f);
            }
        }
    }
}

} // namespace riff
