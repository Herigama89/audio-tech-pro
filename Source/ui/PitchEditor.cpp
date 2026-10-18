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
    startTimerHz (30);
}

PitchEditor::~PitchEditor()
{
    stopTimer();
    writer.removeAllJobs (false, 20000);   // (the last version finishes writing)
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
    // The notes' range with a little room (at least an octave), so the rows are big enough to work on
    lowMidi = std::floor (lo) - 2.0;
    highMidi = std::ceil (hi) + 2.0;
    if (highMidi - lowMidi < 13.0)
    {
        const auto middle = 0.5 * (lowMidi + highMidi);
        lowMidi = std::floor (middle - 6.5);
        highMidi = lowMidi + 13.0;
    }
    ready = true;
    if (cache != nullptr && region[pitchSource].toString().isNotEmpty())
        ensureLive();   // (edited before: changes are heard at once from here)
    status.setText (juce::String ((int) analysis.notes.size()) + " notes. Drag one up or down (Option: by cents); "
                    "double-click: nearest semitone. Wheel: up/down, Option-wheel: row size, Cmd-wheel: zoom. Click the ruler to move the playhead.",
                    juce::dontSendNotification);
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
        if (cache != nullptr && ensureLive())
        {
            applyLive (edits);
            writeLiveSoon();
        }
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

    if (cache != nullptr && ensureLive())
    {
        // Heard at once: the changed notes rendered into the playing audio; the file follows behind the scenes
        applyLive (edits);
        writeLiveSoon();
        lastCommitted = text;
        undoManager.beginNewTransaction ("Pitch: " + undoName);
        region.setProperty (pitchEdits, text, &undoManager);
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
        if (auto writer2 = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (analysis.sampleRate)
                                                            .withNumChannels (rendered.getNumChannels()).withBitsPerSample (24)))
            written = writer2->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
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

bool PitchEditor::ensureLive()
{
    if (cache == nullptr || ! ready)
        return false;
    if (live != nullptr && juce::File (region[ids::file].toString()) == liveFile)
        return true;

    // The region plays the edited audio from here on: made in memory now (as the edits are), written behind the scenes
    editsFolder.createDirectory();
    liveFile = editsFolder.getChildFile (juce::File::createLegalFileName (sourceFile.getFileNameWithoutExtension() + " Pitch "
                                                                          + region[ids::id].toString().substring (0, 8)) + ".wav");
    if (const juce::File playing (region[ids::file].toString()); region[pitchSource].toString().isNotEmpty() && playing != sourceFile)
        liveFile = playing;   // (edited before: the same file again)
    auto made = std::make_shared<DecodedAudio>();
    made->sampleRate = analysis.sampleRate;
    const auto rendered = pitchcorrect::render (source, analysis, liveEdits = pitchcorrect::fromString (lastCommitted, analysis.notes.size()));
    made->buffer.setSize (2, rendered.getNumSamples());
    for (int c = 0; c < 2; ++c)
        made->buffer.copyFrom (c, 0, rendered, juce::jmin (c, rendered.getNumChannels() - 1), 0, rendered.getNumSamples());
    live = made;
    cache->put (liveFile, live);

    // (not an undo step of its own: the region keeps playing this file, Undo takes back the edits in it)
    region.setProperty (pitchSource, sourceFile.getFullPathName(), nullptr);
    region.setProperty (pitchSourceOffset, sourceOffset, nullptr);
    region.setProperty (pitchSourceLength, sourceLength, nullptr);
    region.setProperty (ids::file, liveFile.getFullPathName(), nullptr);
    region.setProperty (ids::offset, 0.0, nullptr);
    region.setProperty (ids::length, sourceLength, nullptr);
    writeLiveSoon();
    return true;
}

void PitchEditor::applyLive (const std::vector<pitchcorrect::NoteEdit>& target)
{
    if (live == nullptr)
        return;
    liveEdits.resize (analysis.notes.size());
    int from = std::numeric_limits<int>::max(), to = 0;
    for (size_t i = 0; i < analysis.notes.size() && i < target.size(); ++i)
        if (! (target[i] == liveEdits[i]))
        {
            const auto [s0, s1] = pitchcorrect::noteSpan (analysis, (int) i);
            from = juce::jmin (from, s0);
            to = juce::jmax (to, s1);
        }
    if (to <= from)
        return;
    // (written into the samples the player is reading: heard on its next pass over them, which may be at once)
    pitchcorrect::renderInto (live->buffer, source, analysis, target, from, to);
    liveEdits = target;
}

void PitchEditor::writeLiveSoon()
{
    if (live == nullptr)
        return;
    // The newest version to disk (to a temporary file, then into place), so the project keeps it
    const auto generation = ++writeGeneration;
    auto copy = std::make_shared<juce::AudioBuffer<float>> (live->buffer);
    const auto file = liveFile;
    const auto rate = analysis.sampleRate;
    writer.addJob ([this, generation, copy, file, rate]
    {
        if (generation != writeGeneration.load())
            return;   // (a newer one is coming)
        const auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + " (writing).wav");
        temp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
            juce::WavAudioFormat wav;
            if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (*copy, 0, copy->getNumSamples());
        }
        temp.moveFileTo (file);
    });
}

std::vector<pitchcorrect::NoteEdit> PitchEditor::editsWithDrag() const
{
    auto result = edits;
    if (dragNote >= 0)
        for (const auto i : selected)
            if (juce::isPositiveAndBelow (i, (int) result.size()))
                result[(size_t) i].shift += dragShift;
    return result;
}

void PitchEditor::timerCallback()
{
    // While dragging: the notes heard where they're being moved to
    if (liveDirty && cache != nullptr && ready)
    {
        liveDirty = false;
        if (ensureLive())
            applyLive (editsWithDrag());
    }

    // The playhead
    if (playheadSeconds && ready)
    {
        const auto t = playheadSeconds() - (double) region[ids::start];
        if (std::abs (t - shownPlayhead) > 1.0e-3)
        {
            const auto oldX = timeToX (shownPlayhead), newX = timeToX (t);
            shownPlayhead = t;
            // Catch: when zoomed in, the view follows it while playing
            const auto visible = sourceLength / zoom;
            if (isPlaying && isPlaying() && zoom > 1.0 && (t < scroll || t > scroll + visible * 0.95) && t >= 0.0 && t <= sourceLength)
            {
                scroll = juce::jlimit (0.0, juce::jmax (0.0, sourceLength - visible), t - visible * 0.05);
                repaint();
            }
            else
            {
                repaint (juce::Rectangle<int> ((int) oldX - 2, 0, 5, getHeight()));
                repaint (juce::Rectangle<int> ((int) newX - 2, 0, 5, getHeight()));
            }
        }
    }
}



//==============================================================================
juce::Rectangle<int> PitchEditor::rulerArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarHeight).withTrimmedLeft (keyboardWidth).withHeight (16);
}

juce::Rectangle<int> PitchEditor::rollArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarHeight + 16).withTrimmedLeft (keyboardWidth).withTrimmedBottom (18);
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

    // The ruler: the song's bars over the region
    {
        const auto ruler = rulerArea();
        g.setColour (theme::ruler);
        g.fillRect (ruler);
        const auto project = region.getParent().getParent();
        if (project.isValid() && sourceLength > 0.0)
        {
            const auto map = model::tempoMapOf (project);
            const double start = region[ids::start];
            const auto b0 = map.beatAt (start + xToTime ((float) area.getX())), b1 = map.beatAt (start + xToTime ((float) area.getRight()));
            const auto pixelsPerBeat = (double) area.getWidth() / juce::jmax (1.0e-3, b1 - b0);
            const auto step = pixelsPerBeat > 24.0 ? 1.0 : pixelsPerBeat > 6.0 ? 4.0 : 16.0;
            g.setFont (juce::FontOptions (10.0f));
            for (auto b = std::ceil (b0 / step) * step; b <= b1; b += step)
            {
                const auto x = timeToX (map.secondsAt (b) - start);
                const auto [bar, beat] = map.barAndBeat (b);
                const bool isBar = beat < 1.0e-6;
                g.setColour (theme::text.withAlpha (isBar ? 0.7f : 0.35f));
                g.fillRect (x, (float) ruler.getBottom() - (isBar ? 10.0f : 5.0f), 1.0f, isBar ? 10.0f : 5.0f);
                if (isBar)
                    g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, (float) ruler.getY(), 30.0f, 11.0f), juce::Justification::left, false);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (ruler.withTop (ruler.getBottom() - 1));
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

    // The playhead
    if (shownPlayhead >= 0.0 && shownPlayhead <= sourceLength)
    {
        const auto x = timeToX (shownPlayhead);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (x, (float) rulerArea().getY(), 1.0f, (float) (area.getBottom() - rulerArea().getY()));
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
    if (rulerArea().contains (e.getPosition()))
    {
        // The ruler: the playhead here (as in Logic)
        if (setPlayheadSeconds)
            setPlayheadSeconds ((double) region[ids::start] + juce::jlimit (0.0, sourceLength, xToTime ((float) e.x)));
        return;
    }
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
    liveDirty = true;   // (heard where it's going, on the next tick)
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
    else if (e.mods.isAltDown())
    {
        // Taller or shorter rows, round the pitch under the mouse
        const auto anchor = yToMidi (e.position.y);
        const auto span = juce::jlimit (8.0, 60.0, (highMidi - lowMidi) * (wheel.deltaY > 0 ? 0.85 : 1.18));
        const auto t = (anchor - lowMidi) / (highMidi - lowMidi);
        lowMidi = juce::jlimit (0.0, 127.0 - span, anchor - t * span);
        highMidi = lowMidi + span;
    }
    else
    {
        // Up and down through the notes
        const auto step = (wheel.deltaY > 0 ? 1.0 : -1.0);
        const auto span = highMidi - lowMidi;
        lowMidi = juce::jlimit (0.0, 127.0 - span, lowMidi + step);
        highMidi = lowMidi + span;
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
