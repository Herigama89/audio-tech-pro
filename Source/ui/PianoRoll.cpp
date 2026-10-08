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

    for (auto* b : { &quantizeButton, &closeButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
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
    return getLocalBounds().withTrimmedTop (headerHeight).withTrimmedLeft (keyboardWidth);
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
    // The grid follows the song's bars, not the region's start.
    const double regionStart = region[ids::start], offset = region[ids::offset];
    const auto t = regionStart + c - offset;
    return juce::jmax (0.0, std::round (t / grid) * grid - regionStart + offset);
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
    header.removeFromRight (6);
    quantizeButton.setBounds (header.removeFromRight (80));
    titleLabel.setBounds (header);
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
        const auto grid = gridSeconds();
        const auto beat = model::secondsPerBeat (project);
        const auto bar = beat * (int) project.getProperty (ids::timeSigNumerator, 4);
        const auto tFrom = regionStart + xToContent ((float) area.getX()) - offset;
        const auto tTo = regionStart + xToContent ((float) area.getRight()) - offset;

        for (auto k = (juce::int64) std::floor (tFrom / grid); (double) k * grid <= tTo; ++k)
        {
            const auto t = (double) k * grid;
            const auto x = contentToX (t - regionStart + offset);
            const bool isBar = std::abs (std::remainder (t, bar)) < 1e-6;
            const bool isBeat = std::abs (std::remainder (t, beat)) < 1e-6;
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

        // Playhead
        const auto playX = contentToX (engine.getPlayheadSeconds() - regionStart + offset);
        g.setColour (juce::Colours::white);
        g.fillRect (playX - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight());
    }

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

    // Sixteenth notes, whatever the zoom.
    const auto sixteenth = model::secondsPerBeat (region.getParent().getParent()) / 4.0;

    juce::Array<juce::ValueTree> notes = selection;
    if (notes.isEmpty())
        for (const auto& note : region)
            if (note.hasType (ids::NOTE))
                notes.add (note);

    for (auto note : notes)
        note.setProperty (ids::start, snapContent (note[ids::start], sixteenth), &undoManager);
}

} // namespace riff
