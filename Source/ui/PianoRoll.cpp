#include "PianoRoll.h"
#include "Theme.h"
#include "../model/Project.h"

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
    viewBox.addItemList ({ "Piano Roll", "Score", "Step Editor" }, 1);
    viewBox.setSelectedId (1, juce::dontSendNotification);
    viewBox.setTooltip ("The region as a piano roll, as notation (Score), or as a grid of steps (Step Editor)");
    viewBox.onChange = [this] { showView (viewBox.getSelectedId()); };
    addChildComponent (score);
    addChildComponent (stepEditor);

    for (auto* box : { &gridBox, &strengthBox, &laneBox, &viewBox })
    {
        box->setWantsKeyboardFocus (false);
        addAndMakeVisible (box);
    }
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
    const auto visible = juce::jmax (100, getHeight() - headerHeight);
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
    return getLocalBounds().withTrimmedTop (headerHeight).withTrimmedLeft (keyboardWidth).withTrimmedBottom (laneHeight + laneGap);
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
    score.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    stepEditor.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);

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

            g.setColour (isBlackKey (p) ? theme::laneA.darker (0.25f) : theme::laneB);
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

        // Notes, brighter for higher velocity.
        for (const auto& note : region)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            const auto r = noteBounds (note);
            const auto vel = (float) (int) note[ids::velocity] / 127.0f;
            g.setColour (colour.withMultipliedBrightness (0.55f + 0.6f * vel));
            g.fillRoundedRectangle (r, 2.0f);

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
            g.setColour (p == keyboardNote ? theme::accent : black ? juce::Colour (0xff1a1a1a) : juce::Colour (0xffe8e8e8));
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

    if (! region.isValid() || e.y < headerHeight)
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

    auto note = noteAt (e.position);

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
    else if (e.mods.isShiftDown() || e.mods.isAltDown())
    {
        // Shift or Option-drag over empty space: select the notes in a rectangle.
        if (! e.mods.isShiftDown())
            selection.clear();
        drag = Drag::rubberBand;
        rubberBand = {};
        repaint();
        return;
    }
    else
    {
        // Pencil: a new note at the grid line before the mouse, one grid step long.
        const auto grid = gridSeconds();
        auto start = snapContent (xToContent ((float) e.x));
        if (start > xToContent ((float) e.x))
            start = juce::jmax (0.0, start - grid);

        const auto pitch = yToPitch (e.y);
        undoManager.beginNewTransaction ("Add note");
        note = model::addNote (region, pitch, start, grid, 100, &undoManager);
        selection = { note };
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

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
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
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
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
    for (auto* c : std::initializer_list<juce::Component*> { &laneBox, &stepButton, &restButton })
        c->setVisible (id == 1);
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

} // namespace riff
