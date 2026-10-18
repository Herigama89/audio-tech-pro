#include "PitchEditor.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/AudioEdit.h"

namespace riff
{

namespace
{
    // Kept on the region: the recording the edits apply to, and the edits
    const juce::Identifier pitchSource ("pitchSource"), pitchSourceOffset ("pitchSourceOffset"),
                           pitchSourceLength ("pitchSourceLength"), pitchEdits ("pitchEdits");

    constexpr int keyboardWidth = 38, toolbarHeight = 30;
    const juce::Colour blobColour (0xffe08a2c), blobSelected (0xffffc04a);

    bool isBlackKey (int midi) { const auto pc = ((midi % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
}

PitchEditor::PitchEditor (juce::ValueTree r, juce::UndoManager& um, juce::AudioFormatManager& f, juce::File folder)
    : region (r), undoManager (um), formats (f), editsFolder (folder)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setRange (0.0, 100.0, 1.0);
    amountSlider.setValue (100.0, juce::dontSendNotification);
    amountSlider.setTextValueSuffix ("%");
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    amountSlider.setTooltip ("How far Correct and To Key move the notes, and how much Straighten flattens them (100%: all the way)");
    addAndMakeVisible (amountSlider);

    correctButton.setTooltip ("The selected notes (or all) onto the nearest semitone, by the amount set (Logic's Pitch Correction)");
    keyButton.setTooltip ("The selected notes (or all) onto the nearest note of the song's key");
    straightenButton.setTooltip ("Flatten the selected notes' drift and vibrato towards each note's centre, by the amount set");
    resetButton.setTooltip ("The selected notes (or all) back as they were sung");
    correctButton.onClick = [this]
    {
        pitchcorrect::correct (edits, analysis, targets(), amountSlider.getValue() / 100.0);
        commit ("Correct Pitch");
    };
    keyButton.onClick = [this]
    {
        pitchcorrect::correct (edits, analysis, targets(), amountSlider.getValue() / 100.0, songScale ? songScale() : std::vector<int>());
        commit ("Correct Pitch to Key");
    };
    straightenButton.onClick = [this]
    {
        for (const auto i : targets())
            edits[(size_t) i].drift = amountSlider.getValue() / 100.0;
        commit ("Straighten Pitch");
    };
    resetButton.onClick = [this]
    {
        for (const auto i : targets())
            edits[(size_t) i] = {};
        commit ("Reset Pitch");
    };
    for (auto* b : { &correctButton, &keyButton, &straightenButton, &resetButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (status);

    region.addListener (this);
    load();
}

PitchEditor::~PitchEditor()
{
    region.removeListener (this);
    if (worker != nullptr)
        worker->stopThread (4000);
}

void PitchEditor::load()
{
    // The recording the edits apply to: the one kept from the first pitch edit (if the region is still that long),
    // else what the region plays now
    const juce::File kept (region[pitchSource].toString());
    if (kept.existsAsFile() && std::abs ((double) region.getProperty (pitchSourceLength, -1.0) - (double) region[ids::length]) < 1.0e-6)
    {
        sourceFile = kept;
        sourceOffset = region[pitchSourceOffset];
        sourceLength = region[pitchSourceLength];
        lastCommitted = region[pitchEdits].toString();
    }
    else
    {
        // (cut or stretched since: its notes are found again, as it is now)
        sourceFile = juce::File (region[ids::file].toString());
        sourceOffset = region[ids::offset];
        sourceLength = region[ids::length];
        lastCommitted = {};
    }
    ready = false;
    status.setText ("Finding the notes...", juce::dontSendNotification);

    struct Job final : juce::Thread
    {
        Job (PitchEditor& e) : juce::Thread ("Pitch analysis"), editor (e) {}
        void run() override
        {
            auto result = editor.analyseSource();
            if (threadShouldExit())
                return;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PitchEditor> (&editor), result = std::move (result)]() mutable
            {
                if (safe != nullptr)
                    safe->useAnalysis (std::move (result.first), std::move (result.second));
            });
        }
        PitchEditor& editor;
    };
    if (worker != nullptr)
        worker->stopThread (4000);
    worker = std::make_unique<Job> (*this);
    worker->startThread();
}

std::pair<juce::AudioBuffer<float>, pitchcorrect::Analysis> PitchEditor::analyseSource() const
{
    double rate = 0.0;
    auto audio = audioedit::readRegion (formats, sourceFile, sourceOffset, sourceLength, rate);
    auto result = pitchcorrect::analyse (audio, rate > 0.0 ? rate : 48000.0);
    return { std::move (audio), std::move (result) };
}

void PitchEditor::useAnalysis (juce::AudioBuffer<float> audio, pitchcorrect::Analysis result)
{
    source = std::move (audio);
    analysis = std::move (result);
    edits = pitchcorrect::fromString (lastCommitted, analysis.notes.size());
    maxLevel = 1.0e-3f;
    for (const auto l : analysis.level)
        maxLevel = juce::jmax (maxLevel, l);
    double lo = 127.0, hi = 0.0;
    for (const auto& n : analysis.notes)
    {
        lo = juce::jmin (lo, n.centre);
        hi = juce::jmax (hi, n.centre);
    }
    if (hi < lo)
        lo = 55.0, hi = 67.0;
    lowMidi = std::floor (lo) - 6.0;
    highMidi = std::ceil (hi) + 6.0;
    ready = true;
    status.setText (juce::String ((int) analysis.notes.size()) + " notes. Drag one up or down (Option: by cents); "
                    "double-click to put it on the nearest semitone.", juce::dontSendNotification);
    repaint();
}

void PitchEditor::analyseNow()
{
    if (worker != nullptr)
        worker->stopThread (10000);
    if (! ready)
    {
        auto result = analyseSource();
        useAnalysis (std::move (result.first), std::move (result.second));
    }
}

void PitchEditor::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t != region)
        return;
    if (p == pitchEdits && ready && region[pitchEdits].toString() != lastCommitted)
    {
        // Undo or redo: the edits as they were
        lastCommitted = region[pitchEdits].toString();
        edits = pitchcorrect::fromString (lastCommitted, analysis.notes.size());
        repaint();
    }
    else if ((p == ids::length || p == ids::offset) && ready && std::abs ((double) region[ids::length] - sourceLength) > 1.0e-6)
        load();   // trimmed: the notes again
}

std::vector<int> PitchEditor::targets() const
{
    std::vector<int> result;
    if (! selected.empty())
        result.assign (selected.begin(), selected.end());
    else
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
            result.push_back (i);
    return result;
}

void PitchEditor::commit (const juce::String& undoName)
{
    if (! ready)
        return;
    const auto text = pitchcorrect::toString (edits);
    if (text == lastCommitted)
    {
        repaint();
        return;
    }
    juce::MouseCursor::showWaitCursor();
    const auto rendered = pitchcorrect::render (source, analysis, edits);
    editsFolder.createDirectory();
    const auto dest = editsFolder.getNonexistentChildFile (sourceFile.getFileNameWithoutExtension() + " Pitch", ".wav", false);
    bool written = false;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dest);
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (analysis.sampleRate)
                                                           .withNumChannels (rendered.getNumChannels()).withBitsPerSample (24)))
            written = writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
    }
    juce::MouseCursor::hideWaitCursor();
    if (! written)
    {
        status.setText ("Couldn't write the new audio to " + editsFolder.getFullPathName(), juce::dontSendNotification);
        return;
    }

    lastCommitted = text;
    undoManager.beginNewTransaction ("Pitch: " + undoName);
    region.setProperty (pitchSource, sourceFile.getFullPathName(), &undoManager);
    region.setProperty (pitchSourceOffset, sourceOffset, &undoManager);
    region.setProperty (pitchSourceLength, sourceLength, &undoManager);
    region.setProperty (pitchEdits, text, &undoManager);
    region.setProperty (ids::file, dest.getFullPathName(), &undoManager);
    region.setProperty (ids::offset, 0.0, &undoManager);
    region.setProperty (ids::length, sourceLength, &undoManager);
    repaint();
}

void PitchEditor::timerCallback() {}

//==============================================================================
juce::Rectangle<int> PitchEditor::rollArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarHeight).withTrimmedLeft (keyboardWidth).withTrimmedBottom (18);
}

float PitchEditor::timeToX (double seconds) const
{
    const auto area = rollArea();
    const auto visible = juce::jmax (1.0e-3, sourceLength / zoom);
    return (float) area.getX() + (float) ((seconds - scroll) / visible * area.getWidth());
}

double PitchEditor::xToTime (float x) const
{
    const auto area = rollArea();
    return scroll + (x - (float) area.getX()) / (float) juce::jmax (1, area.getWidth()) * (sourceLength / zoom);
}

float PitchEditor::midiToY (double midi) const
{
    const auto area = rollArea();
    return (float) area.getBottom() - (float) ((midi - lowMidi) / (highMidi - lowMidi)) * (float) area.getHeight();
}

double PitchEditor::yToMidi (float y) const
{
    const auto area = rollArea();
    return lowMidi + ((float) area.getBottom() - y) / (float) juce::jmax (1, area.getHeight()) * (highMidi - lowMidi);
}

juce::Path PitchEditor::blobFor (size_t index, double shiftNow) const
{
    // Thick where it's loud: its top and bottom edges follow the loudness, round the note's (moved) centre
    const auto& note = analysis.notes[index];
    const auto rowHeight = (float) rollArea().getHeight() / (float) (highMidi - lowMidi);
    const auto y = midiToY (note.centre + shiftNow);
    juce::Path p;
    std::vector<juce::Point<float>> top, bottom;
    for (int step = note.startFrame;; step += 2)
    {
        const auto f = juce::jmin (step, note.endFrame);
        const auto x = timeToX (analysis.frameToSeconds (f));
        const auto level = analysis.level[(size_t) juce::jlimit (0, (int) analysis.level.size() - 1, f)] / maxLevel;
        auto half = 2.0f + rowHeight * 0.48f * std::sqrt (juce::jlimit (0.0f, 1.0f, level));
        if (f == note.startFrame || f >= note.endFrame)
            half *= 0.5f;
        top.push_back ({ x, y - half });
        bottom.push_back ({ x, y + half });
        if (f >= note.endFrame)
            break;
    }
    if (top.size() < 2)
        return p;
    p.startNewSubPath (top.front());
    for (size_t i = 1; i < top.size(); ++i)
        p.lineTo (top[i]);
    for (size_t i = bottom.size(); i-- > 0;)
        p.lineTo (bottom[i]);
    p.closeSubPath();
    return p.createPathWithRoundedCorners (3.0f);
}

int PitchEditor::noteAt (juce::Point<float> pos) const
{
    for (int i = (int) analysis.notes.size(); --i >= 0;)
    {
        const auto& n = analysis.notes[(size_t) i];
        const auto shift = edits[(size_t) i].shift + (selected.count (i) > 0 && dragNote >= 0 ? dragShift : 0.0);
        const auto x0 = timeToX (analysis.frameToSeconds (n.startFrame)), x1 = timeToX (analysis.frameToSeconds (n.endFrame));
        const auto y = midiToY (n.centre + shift);
        const auto rowHeight = (float) rollArea().getHeight() / (float) (highMidi - lowMidi);
        if (pos.x >= x0 && pos.x <= x1 && std::abs (pos.y - y) <= juce::jmax (6.0f, rowHeight * 0.6f))
            return i;
    }
    return -1;
}

//==============================================================================
void PitchEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    const auto area = rollArea();
    const auto rowHeight = (float) area.getHeight() / (float) (highMidi - lowMidi);

    // Toolbar strip
    g.setColour (theme::toolbarDark);
    g.fillRect (getLocalBounds().removeFromTop (toolbarHeight));

    // Rows, and the keyboard with the notes' names
    for (int m = (int) std::floor (lowMidi); m <= (int) std::ceil (highMidi); ++m)
    {
        const auto y0 = midiToY (m + 0.5), y1 = midiToY (m - 0.5);
        const auto row = juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth(), y1 - y0).getIntersection (area.toFloat());
        g.setColour (isBlackKey (m) ? theme::rollBlack : theme::rollWhite);
        g.fillRect (row);
        g.setColour (theme::gridBeat.withAlpha (0.35f));
        g.fillRect (row.getX(), row.getBottom() - 0.5f, row.getWidth(), 0.5f);
        const auto key = juce::Rectangle<float> (0.0f, y0, (float) keyboardWidth - 2.0f, y1 - y0).getIntersection (
            juce::Rectangle<float> (0.0f, (float) area.getY(), (float) keyboardWidth, (float) area.getHeight()));
        g.setColour (isBlackKey (m) ? theme::keysBlack : theme::keysWhite);
        g.fillRect (key);
        if (m % 12 == 0 || rowHeight >= 11.0f)
        {
            g.setColour (isBlackKey (m) ? theme::keysWhite : theme::keysBlack.withAlpha (0.8f));
            g.setFont (juce::FontOptions (juce::jmin (11.0f, rowHeight * 0.9f)));
            g.drawText (juce::MidiMessage::getMidiNoteName (m, true, true, 3), key.reduced (3.0f, 0.0f), juce::Justification::centredRight, false);
        }
    }

    if (! ready)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Finding the notes...", area, juce::Justification::centred, false);
        return;
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    // The notes: blobs, their pitch curves (as they'll sound), and their names
    for (size_t i = 0; i < analysis.notes.size(); ++i)
    {
        const auto& n = analysis.notes[i];
        const auto& e = edits[i];
        const bool isSelected = selected.count ((int) i) > 0;
        const auto shiftNow = e.shift + (isSelected && dragNote >= 0 ? dragShift : 0.0);
        const auto blob = blobFor (i, shiftNow);
        if (e.isNeutral() && ! (isSelected && dragNote >= 0))
            g.setColour (blobColour.withAlpha (0.78f));
        else
            g.setColour (blobColour.brighter (0.2f).withAlpha (0.9f));
        if (isSelected)
            g.setColour (blobSelected.withAlpha (0.92f));
        g.fillPath (blob);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (blob, juce::PathStrokeType (1.0f));

        // Where it was sung, when it's been moved
        if (std::abs (shiftNow) > 0.02)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            const auto y = midiToY (n.centre);
            g.drawHorizontalLine ((int) y, timeToX (analysis.frameToSeconds (n.startFrame)), timeToX (analysis.frameToSeconds (n.endFrame)));
        }

        juce::Path curve;
        bool started = false;
        for (int f = n.startFrame; f < n.endFrame; ++f)
        {
            const auto m = (double) analysis.midi[(size_t) f];
            if (m <= 0.0)
            {
                started = false;
                continue;
            }
            const auto shown = m + shiftNow + e.drift * (n.centre - m);
            const juce::Point<float> p (timeToX (analysis.frameToSeconds (f)), midiToY (shown));
            if (! started)
                curve.startNewSubPath (p);
            else
                curve.lineTo (p);
            started = true;
        }
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (1.2f));

        const auto pitch = n.centre + shiftNow;
        const auto cents = juce::roundToInt ((pitch - std::round (pitch)) * 100.0);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::MidiMessage::getMidiNoteName (juce::roundToInt (pitch), true, true, 3)
                        + (cents != 0 ? (cents > 0 ? " +" : " ") + juce::String (cents) : juce::String()),
                    juce::Rectangle<float> (timeToX (analysis.frameToSeconds (n.startFrame)), midiToY (pitch) - rowHeight * 0.5f - 15.0f, 80.0f, 13.0f),
                    juce::Justification::centredLeft, false);
    }

    if (rubberBanding && ! rubberBand.isEmpty())
    {
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (rubberBand);
        g.setColour (theme::accent);
        g.drawRect (rubberBand, 1.0f);
    }
}

void PitchEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (toolbarHeight).reduced (6, 4);
    correctButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    keyButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (4);
    straightenButton.setBounds (bar.removeFromLeft (82));
    bar.removeFromLeft (8);
    amountSlider.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (8);
    resetButton.setBounds (bar.removeFromLeft (56));
    status.setBounds (getLocalBounds().removeFromBottom (18).withTrimmedLeft (keyboardWidth));
}

//==============================================================================
void PitchEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! ready || ! rollArea().contains (e.getPosition()))
        return;
    const auto note = noteAt (e.position);
    if (note < 0)
    {
        if (! e.mods.isShiftDown())
            selected.clear();
        rubberBanding = true;
        rubberBand = {};
        repaint();
        return;
    }
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        if (selected.count (note) > 0)
            selected.erase (note);
        else
            selected.insert (note);
    }
    else if (selected.count (note) == 0)
        selected = { note };
    dragNote = note;
    dragShift = 0.0;
    repaint();
}

void PitchEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (rubberBanding)
    {
        rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        std::set<int> inside;
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
        {
            const auto& n = analysis.notes[(size_t) i];
            const auto box = juce::Rectangle<float>::leftTopRightBottom (timeToX (analysis.frameToSeconds (n.startFrame)), midiToY (n.centre + edits[(size_t) i].shift) - 4.0f,
                                                                         timeToX (analysis.frameToSeconds (n.endFrame)), midiToY (n.centre + edits[(size_t) i].shift) + 4.0f);
            if (box.intersects (rubberBand))
                inside.insert (i);
        }
        if (! e.mods.isShiftDown())
            selected = inside;
        else
            selected.insert (inside.begin(), inside.end());
        repaint();
        return;
    }
    if (dragNote < 0)
        return;
    // Semitone steps that land the dragged note on a semitone (Option: free, in cents)
    const auto semitones = -(double) e.getDistanceFromDragStartY() / ((double) rollArea().getHeight() / (highMidi - lowMidi));
    const auto& n = analysis.notes[(size_t) dragNote];
    const auto from = n.centre + edits[(size_t) dragNote].shift;
    dragShift = e.mods.isAltDown() ? std::round (semitones * 100.0) / 100.0
                                   : std::round (from + semitones) - from;
    repaint();
}

void PitchEditor::mouseUp (const juce::MouseEvent&)
{
    if (rubberBanding)
    {
        rubberBanding = false;
        rubberBand = {};
        repaint();
        return;
    }
    if (dragNote >= 0 && std::abs (dragShift) > 1.0e-4)
    {
        for (const auto i : selected)
            edits[(size_t) i].shift += dragShift;
        dragNote = -1;
        dragShift = 0.0;
        commit ("Move Notes");
        return;
    }
    dragNote = -1;
    dragShift = 0.0;
    repaint();
}

void PitchEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto note = noteAt (e.position); note >= 0)
    {
        pitchcorrect::correct (edits, analysis, { note }, 1.0);
        commit ("Note to Semitone");
    }
}

void PitchEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom in time round the mouse
        const auto anchor = xToTime (e.position.x);
        zoom = juce::jlimit (1.0, 64.0, zoom * (wheel.deltaY > 0 ? 1.25 : 0.8));
        const auto visible = sourceLength / zoom;
        scroll = juce::jlimit (0.0, juce::jmax (0.0, sourceLength - visible), anchor - (e.position.x - rollArea().getX()) / rollArea().getWidth() * visible);
    }
    else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto visible = sourceLength / zoom;
        const auto delta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : -wheel.deltaY) * visible * 0.3;
        scroll = juce::jlimit (0.0, juce::jmax (0.0, sourceLength - visible), scroll + delta);
    }
    else
    {
        const auto step = (wheel.deltaY > 0 ? 1.0 : -1.0);
        lowMidi = juce::jlimit (12.0, 110.0, lowMidi + step);
        highMidi = lowMidi + juce::jmax (12.0, highMidi - lowMidi + 0.0);
        highMidi = juce::jmin (127.0, highMidi);
    }
    repaint();
}

bool PitchEditor::keyPressed (const juce::KeyPress& key)
{
    if (! ready)
        return false;
    const auto fine = key.getModifiers().isAltDown();
    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
    {
        const auto dir = key.isKeyCode (juce::KeyPress::upKey) ? 1.0 : -1.0;
        for (const auto i : targets())
            edits[(size_t) i].shift += dir * (fine ? 0.1 : 1.0);
        commit (fine ? "Pitch by Cents" : "Pitch by a Semitone");
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        for (const auto i : targets())
            edits[(size_t) i] = {};
        commit ("Reset Pitch");
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selected.clear();
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
            selected.insert (i);
        repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        selected.clear();
        repaint();
        return true;
    }
    return false;
}

} // namespace riff
