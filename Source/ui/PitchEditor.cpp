#include "PitchEditor.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/AudioEdit.h"

namespace riff
{

namespace
{
    // Kept on the region: the recording the edits apply to, the edits, and the notes' boundaries (after splits and joins)
    const juce::Identifier pitchSource ("pitchSource"), pitchSourceOffset ("pitchSourceOffset"),
                           pitchSourceLength ("pitchSourceLength"), pitchEdits ("pitchEdits"), pitchSegments ("pitchSegments");

    constexpr int menuHeight = 30, rulerHeight = 18, regionBarHeight = 16, keyboardWidth = 44, statusHeight = 16;

    bool isBlackKey (int midi) { const auto pc = ((midi % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v)) + "%"; }

    // Scale Quantize's scales (intervals from the root)
    struct ScaleKind { const char* name; std::vector<int> steps; };
    const std::vector<ScaleKind>& scaleKinds()
    {
        static const std::vector<ScaleKind> kinds {
            { "Major", { 0, 2, 4, 5, 7, 9, 11 } }, { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
            { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } }, { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
            { "Minor Pentatonic", { 0, 3, 5, 7, 10 } }, { "Blues", { 0, 3, 5, 6, 7, 10 } }, { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } }, { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } } };
        return kinds;
    }
}

PitchEditor::PitchEditor (juce::ValueTree r, juce::UndoManager& um, juce::AudioFormatManager& f, juce::File folder)
    : region (r), undoManager (um), formats (f), editsFolder (folder)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    setupInspector();

    // The menus and tools above, as Logic's editor
    editMenu.onClick = [this] { showEditMenu(); };
    functionsMenu.onClick = [this] { showFunctionsMenu(); };
    viewMenu.onClick = [this] { showViewMenu(); };
    pointerTool.setButtonText ("Pointer");
    scissorsTool.setButtonText ("Scissors");
    glueTool.setButtonText ("Glue");
    pointerTool.setTooltip ("Pointer: select notes; drag them up or down (pitch) or sideways (time); their hot spots for drift, fine pitch, gain, vibrato and formant");
    scissorsTool.setTooltip ("Scissors: click a note to split it there");
    glueTool.setTooltip ("Glue: click to join the selected notes (or a note and the next) into one");
    pointerTool.onClick = [this] { setTool (Tool::pointer); };
    scissorsTool.onClick = [this] { setTool (Tool::scissors); };
    glueTool.onClick = [this] { setTool (Tool::glue); };
    for (auto* b : { &editMenu, &functionsMenu, &viewMenu, &pointerTool, &scissorsTool, &glueTool })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    for (auto* b : { &pointerTool, &scissorsTool, &glueTool })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
    }
    setTool (Tool::pointer);
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

//==============================================================================
void PitchEditor::setupInspector()
{
    nameLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    nameLabel.setText (region[ids::name].toString(), juce::dontSendNotification);
    notesLabel.setFont (juce::FontOptions (11.5f));
    notesLabel.setColour (juce::Label::textColourId, theme::dimText);
    for (auto* l : { &nameLabel, &notesLabel })
        addAndMakeVisible (l);

    timeGridBox.addItemList ({ "off", "1/4", "1/8", "1/16", "1/32", "1/8 Triplet", "1/16 Triplet" }, 1);
    timeGridBox.setSelectedId (4, juce::dontSendNotification);
    timeGridBox.setTooltip ("Time Quantize: the grid Q moves the selected notes (or all) onto");
    scaleKeyBox.addItem ("Off", 1);
    scaleKeyBox.addItem ("Song key", 2);
    for (int pc = 0; pc < 12; ++pc)
        scaleKeyBox.addItem (juce::MidiMessage::getMidiNoteName (pc, true, false, 3), 3 + pc);
    scaleKeyBox.setSelectedId (2, juce::dontSendNotification);
    scaleKeyBox.setTooltip ("Scale Quantize: the key the notes go to (Song key: the project's key)");
    for (int i = 0; i < (int) scaleKinds().size(); ++i)
        scaleModeBox.addItem (scaleKinds()[(size_t) i].name, i + 1);
    scaleModeBox.setSelectedId (1, juce::dontSendNotification);
    scaleModeBox.setTooltip ("Scale Quantize: the scale (with Song key: the project's own)");
    timeQButton.setTooltip ("Time Quantize the selected notes (or all), by the Strength");
    scaleQButton.setTooltip ("Scale Quantize the selected notes (or all): each onto the nearest note of the scale");
    scaleLockButton.setClickingTogglesState (true);
    scaleLockButton.setToggleState (keepScale, juce::dontSendNotification);
    scaleLockButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8bd9));
    scaleLockButton.setTooltip ("Keep in Scale: notes stay on the scale's notes - dragging, the arrow keys, Transpose and Pitch Correction "
                                "go only to notes of the key and scale chosen above; turning it on moves notes outside the scale onto it");
    scaleLockButton.onClick = [this]
    {
        keepScale = scaleLockButton.getToggleState();
        if (keepScale && scaleNotes().empty())
            status.setText ("Keep in Scale: choose a key above", juce::dontSendNotification);
        if (scaleLocked())
            commit ("Keep in Scale");
    };
    scaleKeyBox.onChange = scaleModeBox.onChange = [this] { if (scaleLocked()) commit ("Keep in Scale"); };
    timeQButton.onClick = [this] { quantizeTime(); };
    scaleQButton.onClick = [this] { quantizeScale(); };
    for (auto* c : std::initializer_list<juce::Component*> { &timeGridBox, &scaleKeyBox, &scaleModeBox, &timeQButton, &scaleQButton, &scaleLockButton })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    struct Spec { juce::Slider* slider; double lo, hi, start; const char* tip; };
    const Spec specs[] {
        { &strengthSlider, 0.0, 100.0, 100.0, "Time Quantize strength: how far the notes move to the grid" },
        { &correctionSlider, 0.0, 100.0, 0.0, "Pitch Correction: how far the selected notes (or all) go onto the nearest semitone" },
        { &gainSlider, -30.0, 12.0, 0.0, "Gain of the selected notes (or all), in dB" },
        { &driftSlider, 0.0, 100.0, 0.0, "Pitch Drift: how much of the notes' slow drift (scoops, sags) is flattened (100%: none left)" },
        { &vibratoSlider, 0.0, 200.0, 100.0, "Vibrato: 0% none, 100% as sung, 200% twice as wide" },
        { &formantSlider, -12.0, 12.0, 0.0, "Formant Shift: the voice's character darker or brighter, its pitch kept (semitones)" } };
    for (const auto& spec : specs)
    {
        auto& s = *spec.slider;
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setRange (spec.lo, spec.hi, &s == &formantSlider ? 0.1 : 1.0);
        s.setValue (spec.start, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        s.setTooltip (spec.tip);
        s.setWantsKeyboardFocus (false);
        if (&s != &strengthSlider)
        {
            s.onValueChange = [this, &s] { if (s.isMouseButtonDown()) sliderChanged (s, false); };
            s.onDragEnd = [this, &s] { sliderChanged (s, true); };
        }
        addAndMakeVisible (s);
    }
    strengthSlider.setTextValueSuffix ("");
    gainSlider.setTextValueSuffix (" dB");

    for (const auto* text : { "Time Quantize", "Strength", "Scale Quantize", "Pitch Correction", "Gain", "Pitch Drift", "Vibrato", "Formant Shift" })
    {
        auto* l = captions.add (new juce::Label ({}, text));
        l->setFont (juce::FontOptions (12.5f));
        l->setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (l);
    }
}

void PitchEditor::refreshInspector()
{
    // The values of the first selected note (or the first note)
    notesLabel.setText (ready ? (selected.empty() ? juce::String ((int) analysis.notes.size()) + " notes (all)"
                                                  : juce::String ((int) selected.size()) + " of " + juce::String ((int) analysis.notes.size()) + " notes selected")
                              : juce::String ("Finding the notes..."),
                        juce::dontSendNotification);
    const auto t = targets();
    if (t.empty() || edits.empty())
        return;
    const auto& e = edits[(size_t) t.front()];
    correctionSlider.setValue (e.snap * 100.0, juce::dontSendNotification);
    gainSlider.setValue (e.gain, juce::dontSendNotification);
    driftSlider.setValue (e.drift * 100.0, juce::dontSendNotification);
    vibratoSlider.setValue (e.vibrato * 100.0, juce::dontSendNotification);
    formantSlider.setValue (e.formant, juce::dontSendNotification);
}

void PitchEditor::sliderChanged (juce::Slider& s, bool finished)
{
    if (! ready)
        return;
    const auto v = s.getValue();
    for (const auto i : targets())
    {
        auto& e = edits[(size_t) i];
        if (&s == &correctionSlider)    e.snap = v / 100.0;
        else if (&s == &gainSlider)     e.gain = v;
        else if (&s == &driftSlider)    e.drift = v / 100.0;
        else if (&s == &vibratoSlider)  e.vibrato = v / 100.0;
        else if (&s == &formantSlider)  e.formant = v;
    }
    if (finished)
        commit (&s == &correctionSlider ? "Pitch Correction" : &s == &gainSlider ? "Note Gain" : &s == &driftSlider ? "Pitch Drift"
                : &s == &vibratoSlider ? "Vibrato" : "Formant Shift");
    else
    {
        liveDirty = true;   // (heard as the slider moves)
        repaint();
    }
}

std::vector<int> PitchEditor::scaleNotes() const
{
    const auto key = scaleKeyBox.getSelectedId();
    if (key <= 1)
        return {};
    if (key == 2 && songScale)
        return songScale();
    const auto root = juce::jmax (0, key - 3);
    std::vector<int> notes;
    for (const auto step : scaleKinds()[(size_t) juce::jlimit (0, (int) scaleKinds().size() - 1, scaleModeBox.getSelectedId() - 1)].steps)
        notes.push_back ((root + step) % 12);
    return notes;
}

void PitchEditor::quantizeScale()
{
    if (! ready || scaleKeyBox.getSelectedId() <= 1)
    {
        status.setText ("Scale Quantize: choose a key first", juce::dontSendNotification);
        return;
    }
    pitchcorrect::correct (edits, analysis, targets(), 1.0, scaleNotes());
    commit ("Scale Quantize");
}

void PitchEditor::quantizeTime()
{
    if (! ready)
        return;
    const double grids[] { 0.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    const auto grid = grids[juce::jlimit (0, 6, timeGridBox.getSelectedId() - 1)];
    const auto project = region.getParent().getParent();
    if (grid <= 0.0 || ! project.isValid())
        return;
    const auto map = model::tempoMapOf (project);
    const double regionStart = region[ids::start];
    const auto strength = strengthSlider.getValue() / 100.0;
    for (const auto i : targets())
    {
        const auto now = regionStart + analysis.frameToSeconds (analysis.notes[(size_t) i].startFrame) + edits[(size_t) i].time;
        const auto snapped = map.secondsAt (std::round (map.beatAt (now) / grid) * grid);
        edits[(size_t) i].time += strength * (snapped - now);
    }
    liveWhole = true;
    commit ("Time Quantize");
}

void PitchEditor::setForTargets (std::function<void (pitchcorrect::NoteEdit&)> change, const juce::String& undoName)
{
    for (const auto i : targets())
        change (edits[(size_t) i]);
    commit (undoName);
    refreshInspector();
}

//==============================================================================
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
        lastSegments = region[pitchSegments].toString();
    }
    else
    {
        // (cut or stretched since: its notes are found again, as it is now)
        sourceFile = juce::File (region[ids::file].toString());
        sourceOffset = region[ids::offset];
        sourceLength = region[ids::length];
        lastCommitted = {};
        lastSegments = {};
    }
    ready = false;
    ++loadGeneration;
    refreshInspector();

    struct Job final : juce::Thread
    {
        Job (PitchEditor& e) : juce::Thread ("Pitch analysis"), editor (e), generation (e.loadGeneration) {}
        void run() override
        {
            auto result = editor.analyseSource();
            if (threadShouldExit())
                return;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PitchEditor> (&editor), result = std::move (result), g = generation]() mutable
            {
                if (safe != nullptr && ! safe->ready && safe->loadGeneration == g)   // (not when analyseNow got there first)
                    safe->useAnalysis (std::move (result.first), std::move (result.second));
            });
        }
        PitchEditor& editor;
        const int generation;
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

void PitchEditor::resetSegmentsAndEdits()
{
    analysis = original;
    pitchcorrect::applySegments (analysis, lastSegments);
    edits = pitchcorrect::fromString (lastCommitted, analysis.notes.size());
    selected.clear();
}

void PitchEditor::useAnalysis (juce::AudioBuffer<float> audio, pitchcorrect::Analysis result)
{
    source = std::move (audio);
    original = std::move (result);
    resetSegmentsAndEdits();
    maxLevel = 1.0e-3f;
    for (const auto l : analysis.level)
        maxLevel = juce::jmax (maxLevel, l);

    // The waveform (a peak every 256 samples), drawn behind the notes
    peaks.assign ((size_t) (source.getNumSamples() / peakStep + 1), 0.0f);
    for (int c = 0; c < source.getNumChannels(); ++c)
        for (int i = 0; i < source.getNumSamples(); ++i)
            peaks[(size_t) (i / peakStep)] = juce::jmax (peaks[(size_t) (i / peakStep)], std::abs (source.getSample (c, i)));
    const auto loudest = juce::jmax (1.0e-4f, *std::max_element (peaks.begin(), peaks.end()));
    for (auto& p : peaks)
        p /= loudest;

    // The notes' range with a little room (at least an octave), so the rows are big enough to work on
    double lo = 127.0, hi = 0.0;
    for (const auto& n : analysis.notes)
    {
        lo = juce::jmin (lo, n.centre);
        hi = juce::jmax (hi, n.centre);
    }
    if (hi < lo)
        lo = 55.0, hi = 67.0;
    lowMidi = std::floor (lo) - 3.0;
    highMidi = std::ceil (hi) + 3.0;
    if (highMidi - lowMidi < 14.0)
    {
        const auto middle = 0.5 * (lowMidi + highMidi);
        lowMidi = std::floor (middle - 7.0);
        highMidi = lowMidi + 14.0;
    }
    ready = true;
    if (cache != nullptr && region[pitchSource].toString().isNotEmpty())
        ensureLive();   // (edited before: changes are heard at once from here)
    status.setText ("Drag a note up or down (Option: cents) or sideways (time); its hot spots: drift, fine pitch, gain, vibrato, formant. "
                    "Wheel: up/down, Option-wheel: row size, Cmd-wheel: zoom.", juce::dontSendNotification);
    refreshInspector();
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
    if (t != region || committing)
        return;
    if ((p == pitchEdits || p == pitchSegments) && ready
        && (region[pitchEdits].toString() != lastCommitted || region[pitchSegments].toString() != lastSegments))
    {
        // Undo or redo: the notes and edits as they were
        lastCommitted = region[pitchEdits].toString();
        lastSegments = region[pitchSegments].toString();
        resetSegmentsAndEdits();
        liveWhole = true;
        if (cache != nullptr && ensureLive())
        {
            applyLive (edits);
            writeLiveSoon();
        }
        refreshInspector();
        repaint();
    }
    else if ((p == ids::length || p == ids::offset) && ready && std::abs ((double) region[ids::length] - sourceLength) > 1.0e-6)
        load();   // trimmed: the notes again
    else if (p == ids::name)
        nameLabel.setText (region[ids::name].toString(), juce::dontSendNotification);
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

//==============================================================================
void PitchEditor::commit (const juce::String& undoName)
{
    if (! ready)
        return;
    if (scaleLocked())
        pitchcorrect::keepInScale (edits, analysis, scaleNotes());
    const auto text = pitchcorrect::toString (edits);
    const auto segments = analysis.notes.size() == original.notes.size() && pitchcorrect::segmentsToString (analysis) == pitchcorrect::segmentsToString (original)
                              ? juce::String() : pitchcorrect::segmentsToString (analysis);
    if (text == lastCommitted && segments == lastSegments)
    {
        repaint();
        return;
    }
    if (segments != lastSegments)
        liveWhole = true;

    if (cache != nullptr && ensureLive())
    {
        // Heard at once: the changed notes rendered into the playing audio; the file follows behind the scenes
        applyLive (edits);
        writeLiveSoon();
        lastCommitted = text;
        lastSegments = segments;
        undoManager.beginNewTransaction ("Pitch: " + undoName);
        {
            const juce::ScopedValueSetter<bool> own (committing, true);
            region.setProperty (pitchSegments, segments, &undoManager);
            region.setProperty (pitchEdits, text, &undoManager);
        }
        refreshInspector();
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
        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (analysis.sampleRate)
                                                      .withNumChannels (rendered.getNumChannels()).withBitsPerSample (24)))
            written = w->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
    }
    juce::MouseCursor::hideWaitCursor();
    if (! written)
    {
        status.setText ("Couldn't write the new audio to " + editsFolder.getFullPathName(), juce::dontSendNotification);
        return;
    }

    lastCommitted = text;
    lastSegments = segments;
    undoManager.beginNewTransaction ("Pitch: " + undoName);
    const juce::ScopedValueSetter<bool> own (committing, true);
    region.setProperty (pitchSource, sourceFile.getFullPathName(), &undoManager);
    region.setProperty (pitchSourceOffset, sourceOffset, &undoManager);
    region.setProperty (pitchSourceLength, sourceLength, &undoManager);
    region.setProperty (pitchSegments, segments, &undoManager);
    region.setProperty (pitchEdits, text, &undoManager);
    region.setProperty (ids::file, dest.getFullPathName(), &undoManager);
    region.setProperty (ids::offset, 0.0, &undoManager);
    region.setProperty (ids::length, sourceLength, &undoManager);
    refreshInspector();
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
    auto committedAnalysis = original;
    pitchcorrect::applySegments (committedAnalysis, lastSegments);
    liveEdits = pitchcorrect::fromString (lastCommitted, committedAnalysis.notes.size());
    const auto rendered = pitchcorrect::render (source, committedAnalysis, liveEdits);
    auto made = std::make_shared<DecodedAudio>();
    made->sampleRate = analysis.sampleRate;
    made->buffer.setSize (2, rendered.getNumSamples());
    for (int c = 0; c < 2; ++c)
        made->buffer.copyFrom (c, 0, rendered, juce::jmin (c, rendered.getNumChannels() - 1), 0, rendered.getNumSamples());
    live = made;
    if (committedAnalysis.notes.size() != analysis.notes.size())
        liveWhole = true;
    cache->put (liveFile, live);

    // (not an undo step of its own: the region keeps playing this file, Undo takes back the edits in it)
    const juce::ScopedValueSetter<bool> own (committing, true);
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
    // Notes split or joined (or moved in time): all of it again; else just the notes that changed
    if (liveWhole || liveEdits.size() != analysis.notes.size())
    {
        const auto all = pitchcorrect::render (source, analysis, target);
        for (int c = 0; c < live->buffer.getNumChannels(); ++c)
            live->buffer.copyFrom (c, 0, all, juce::jmin (c, all.getNumChannels() - 1), 0, juce::jmin (all.getNumSamples(), live->buffer.getNumSamples()));
        liveEdits = target;
        liveWhole = false;
        return;
    }
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
    if (dragNote >= 0 && dragSpot == Spot::none)
        for (const auto i : selected)
            if (juce::isPositiveAndBelow (i, (int) result.size()))
            {
                result[(size_t) i].shift += dragShift;
                result[(size_t) i].time += dragTime;
            }
    return result;
}

void PitchEditor::timerCallback()
{
    // While dragging or sliding: heard where it's going (moves in time once you let go)
    if (liveDirty && cache != nullptr && ready && dragAxis != Axis::time)
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
juce::Rectangle<int> PitchEditor::menuBarArea() const   { return getLocalBounds().removeFromTop (menuHeight); }
juce::Rectangle<int> PitchEditor::rulerArea() const
{
    return { inspectorWidth + keyboardWidth, menuHeight, juce::jmax (1, getWidth() - inspectorWidth - keyboardWidth), rulerHeight + regionBarHeight };
}
juce::Rectangle<int> PitchEditor::rollArea() const
{
    const auto top = menuHeight + rulerHeight + regionBarHeight;
    return { inspectorWidth + keyboardWidth, top, juce::jmax (1, getWidth() - inspectorWidth - keyboardWidth), juce::jmax (1, getHeight() - top - statusHeight) };
}
juce::Rectangle<int> PitchEditor::keyboardArea() const
{
    const auto roll = rollArea();
    return { inspectorWidth, roll.getY(), keyboardWidth, roll.getHeight() };
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
    return (float) area.getBottom() - (float) ((midi - lowMidi + 0.5) / (highMidi - lowMidi)) * (float) area.getHeight();
}

double PitchEditor::yToMidi (float y) const
{
    const auto area = rollArea();
    return lowMidi - 0.5 + ((float) area.getBottom() - y) / (float) juce::jmax (1, area.getHeight()) * (highMidi - lowMidi);
}

juce::Rectangle<float> PitchEditor::noteBox (int index, const pitchcorrect::NoteEdit& e) const
{
    const auto& n = analysis.notes[(size_t) index];
    const auto rowHeight = (float) rollArea().getHeight() / (float) (highMidi - lowMidi);
    const auto x0 = timeToX (analysis.frameToSeconds (n.startFrame) + e.time), x1 = timeToX (analysis.frameToSeconds (n.endFrame) + e.time);
    const auto y = midiToY (pitchcorrect::centreAfter (n, e));
    const auto h = juce::jmax (9.0f, rowHeight);
    return { x0, y - h * 0.5f, juce::jmax (4.0f, x1 - x0), h };
}

int PitchEditor::noteAt (juce::Point<float> pos) const
{
    const auto shown = editsWithDrag();
    for (int i = (int) analysis.notes.size(); --i >= 0;)
        if (noteBox (i, shown[(size_t) i]).expanded (0.0f, 3.0f).contains (pos))
            return i;
    return -1;
}

std::array<juce::Rectangle<float>, 6> PitchEditor::spotBoxes (int note) const
{
    // As Logic: top left, top middle, top right; bottom left, bottom middle, bottom right
    const auto b = noteBox (note, editsWithDrag()[(size_t) note]);
    constexpr float s = 8.0f;
    const auto top = b.getY() - s * 0.5f, bottom = b.getBottom() - s * 0.5f;
    return { juce::Rectangle<float> (b.getX() + 1.0f, top, s, s), juce::Rectangle<float> (b.getCentreX() - s * 0.5f, top, s, s),
             juce::Rectangle<float> (b.getRight() - s - 1.0f, top, s, s), juce::Rectangle<float> (b.getX() + 1.0f, bottom, s, s),
             juce::Rectangle<float> (b.getCentreX() - s * 0.5f, bottom, s, s), juce::Rectangle<float> (b.getRight() - s - 1.0f, bottom, s, s) };
}

PitchEditor::Spot PitchEditor::spotAt (int note, juce::Point<float> p) const
{
    if (note < 0 || noteBox (note, edits[(size_t) note]).getWidth() < 34.0f)
        return Spot::none;
    const auto boxes = spotBoxes (note);
    const Spot spots[] { Spot::driftStart, Spot::finePitch, Spot::driftEnd, Spot::gain, Spot::vibrato, Spot::formant };
    for (int i = 0; i < 6; ++i)
        if (boxes[(size_t) i].expanded (2.0f).contains (p))
            return spots[i];
    return Spot::none;
}

//==============================================================================
void PitchEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // The menu bar, and the inspector panel
    g.setColour (theme::toolbarDark);
    g.fillRect (menuBarArea());
    g.setColour (theme::panel);
    g.fillRect (0, menuHeight, inspectorWidth, getHeight() - menuHeight);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (inspectorWidth - 1, menuHeight, 1, getHeight() - menuHeight);
    for (auto* c : captions)
        if (c->getText() == "Scale Quantize" || c->getText() == "Pitch Correction" || c->getText() == "Gain")
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (6, c->getY() - 5, inspectorWidth - 12, 1);
        }

    const auto area = rollArea();
    const auto rowHeight = (float) area.getHeight() / (float) (highMidi - lowMidi);
    const auto project = region.getParent().getParent();
    const double regionStart = region[ids::start];
    const auto trackColour = region.getParent().isValid() ? model::trackColour (region.getParent()) : theme::accent;

    // Ruler (the song's bars) and the region's bar under it, as Logic
    {
        const auto r = rulerArea();
        g.setColour (theme::ruler);
        g.fillRect (r.withHeight (rulerHeight));
        if (project.isValid() && sourceLength > 0.0)
        {
            const auto map = model::tempoMapOf (project);
            const auto b0 = map.beatAt (regionStart + xToTime ((float) area.getX())), b1 = map.beatAt (regionStart + xToTime ((float) area.getRight()));
            const auto pixelsPerBeat = (double) area.getWidth() / juce::jmax (1.0e-3, b1 - b0);
            const auto step = pixelsPerBeat > 24.0 ? 1.0 : pixelsPerBeat > 6.0 ? 4.0 : 16.0;
            g.setFont (juce::FontOptions (11.0f));
            for (auto b = std::ceil (b0 / step) * step; b <= b1; b += step)
            {
                const auto x = timeToX (map.secondsAt (b) - regionStart);
                const auto [bar, beat] = map.barAndBeat (b);
                const bool isBar = beat < 1.0e-6;
                g.setColour (theme::text.withAlpha (isBar ? 0.75f : 0.35f));
                g.fillRect (x, (float) r.getY() + (isBar ? 2.0f : 11.0f), 1.0f, isBar ? 16.0f : 7.0f);
                if (isBar)
                    g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, (float) r.getY() + 1.0f, 30.0f, 13.0f), juce::Justification::left, false);
            }
        }
        auto bar = r.withTrimmedTop (rulerHeight).toFloat();
        const auto x0 = juce::jmax (bar.getX(), timeToX (0.0)), x1 = juce::jmin (bar.getRight(), timeToX (sourceLength));
        g.setColour (trackColour.brighter (0.6f).withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, bar.getY(), x1, bar.getBottom()));
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (region[ids::name].toString(), juce::Rectangle<float>::leftTopRightBottom (x0 + 6.0f, bar.getY(), x1, bar.getBottom()),
                    juce::Justification::centredLeft, true);
    }

    // The keyboard (a piano on its side) and the rows
    {
        const auto keys = keyboardArea().toFloat();
        g.setColour (juce::Colour (0xffe8e8e8));
        g.fillRect (keys);
        for (int m = (int) std::floor (lowMidi) - 1; m <= (int) std::ceil (highMidi) + 1; ++m)
        {
            const auto y0 = midiToY (m + 0.5), y1 = midiToY (m - 0.5);
            const auto row = juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth(), y1 - y0).getIntersection (area.toFloat());
            g.setColour (isBlackKey (m) ? theme::rollBlack : theme::rollWhite);
            g.fillRect (row);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect (row.getX(), row.getBottom() - 0.5f, row.getWidth(), 0.5f);
            const auto key = juce::Rectangle<float> (keys.getX(), y0, keys.getWidth(), y1 - y0).getIntersection (keys);
            if (isBlackKey (m))
            {
                g.setColour (juce::Colour (0xff1c1c1e));
                g.fillRect (key.withWidth (keys.getWidth() * 0.6f));
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                if (m % 12 == 0 || m % 12 == 5)
                    g.fillRect (key.getX(), key.getBottom() - 0.6f, key.getWidth(), 0.6f);
            }
            if (m % 12 == 0)
            {
                g.setColour (juce::Colour (0xff333333));
                g.setFont (juce::FontOptions (juce::jmin (11.0f, juce::jmax (8.0f, rowHeight * 0.95f)), juce::Font::bold));
                g.drawText (juce::MidiMessage::getMidiNoteName (m, true, true, 3), key.withTrimmedRight (3.0f), juce::Justification::centredRight, false);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (keys.getRight() - 1.0f, keys.getY(), 1.0f, keys.getHeight());
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

    // Beat and bar lines
    if (project.isValid())
    {
        const auto map = model::tempoMapOf (project);
        const auto b0 = map.beatAt (regionStart + xToTime ((float) area.getX())), b1 = map.beatAt (regionStart + xToTime ((float) area.getRight()));
        const auto pixelsPerBeat = (double) area.getWidth() / juce::jmax (1.0e-3, b1 - b0);
        const auto step = pixelsPerBeat > 60.0 ? 0.25 : pixelsPerBeat > 15.0 ? 1.0 : 4.0;
        for (auto b = std::ceil (b0 / step) * step; b <= b1; b += step)
        {
            const auto isBar = map.barAndBeat (b).second < 1.0e-6;
            g.setColour (isBar ? theme::gridBar : theme::gridBeat.withAlpha (0.6f));
            g.fillRect (timeToX (map.secondsAt (b) - regionStart), (float) area.getY(), 1.0f, (float) area.getHeight());
        }
    }

    // The waveform behind the notes, as Logic
    if (showWaveform && ! peaks.empty())
    {
        const auto mid = (float) area.getCentreY(), halfHeight = (float) area.getHeight() * 0.48f;
        g.setColour (trackColour.withAlpha (0.11f));
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto t0 = xToTime ((float) x), t1 = xToTime ((float) x + 1.0f);
            const auto i0 = juce::jmax (0, (int) (t0 * analysis.sampleRate) / peakStep), i1 = juce::jmin ((int) peaks.size() - 1, (int) (t1 * analysis.sampleRate) / peakStep);
            float p = 0.0f;
            for (int i = i0; i <= i1; ++i)
                p = juce::jmax (p, peaks[(size_t) i]);
            if (p > 0.002f)
                g.fillRect ((float) x, mid - p * halfHeight, 1.0f, p * halfHeight * 2.0f);
        }
    }

    // The notes: a box at the pitch each sounds at, its pitch curve through it
    const auto shown = editsWithDrag();
    const auto noteFill = trackColour.darker (0.35f), noteLine = trackColour.brighter (0.55f);
    for (int i = 0; i < (int) analysis.notes.size(); ++i)
    {
        const auto& n = analysis.notes[(size_t) i];
        const auto& e = shown[(size_t) i];
        const bool isSelected = selected.count (i) > 0;
        const auto box = noteBox (i, e);
        g.setColour (noteFill.withAlpha (isSelected ? 0.85f : 0.6f));
        g.fillRoundedRectangle (box, 2.5f);
        g.setColour (isSelected ? juce::Colours::white : noteLine.withAlpha (0.9f));
        g.drawRoundedRectangle (box, 2.5f, isSelected ? 1.6f : 1.0f);

        // Its level as a bar along the top (darker when turned down), as Logic's box shows it
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (box.reduced (2.0f, 0.0f).withHeight (2.0f).translated (0.0f, 2.0f));

        if (showCurves)
        {
            // Where it was sung, when it's been moved
            const auto moved = std::abs (pitchcorrect::centreAfter (n, e) - n.centre) > 0.02 || std::abs (e.time) > 1.0e-4;
            if (moved)
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                const auto o = noteBox (i, {});
                g.drawRoundedRectangle (o, 2.5f, 1.0f);
            }
            const auto curve = pitchcorrect::curveAfter (analysis, i, e);
            juce::Path p;
            bool started = false;
            for (size_t k = 0; k < curve.size(); ++k)
            {
                if (curve[k] <= 0.0f)
                {
                    started = false;
                    continue;
                }
                const juce::Point<float> pt (timeToX (analysis.frameToSeconds (n.startFrame + (int) k) + e.time), midiToY (curve[k]));
                if (! started)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
                started = true;
            }
            g.setColour (noteLine.brighter (0.4f).withAlpha (0.95f));
            g.strokePath (p, juce::PathStrokeType (1.3f));
        }

        // Its name and how far off the semitone it sounds, when there's room
        if (box.getWidth() > 26.0f)
        {
            const auto pitch = pitchcorrect::centreAfter (n, e);
            const auto cents = juce::roundToInt ((pitch - std::round (pitch)) * 100.0);
            g.setColour (theme::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::MidiMessage::getMidiNoteName (juce::roundToInt (pitch), true, true, 3)
                            + (cents != 0 ? (cents > 0 ? " +" : " ") + juce::String (cents) : juce::String()),
                        juce::Rectangle<float> (box.getX() + 2.0f, box.getY() - 14.0f, 80.0f, 12.0f), juce::Justification::centredLeft, false);
        }
    }

    // Hot spots on the note under the mouse
    if (tool == Tool::pointer && juce::isPositiveAndBelow (hoverNote, (int) analysis.notes.size()) && noteBox (hoverNote, shown[(size_t) hoverNote]).getWidth() >= 34.0f)
    {
        const auto boxes = spotBoxes (hoverNote);
        const Spot spots[] { Spot::driftStart, Spot::finePitch, Spot::driftEnd, Spot::gain, Spot::vibrato, Spot::formant };
        for (int k = 0; k < 6; ++k)
        {
            const bool hot = spots[k] == hoverSpot || spots[k] == dragSpot;
            g.setColour (hot ? theme::accent : juce::Colours::white.withAlpha (0.85f));
            g.fillRect (boxes[(size_t) k]);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.drawRect (boxes[(size_t) k], 1.0f);
        }
    }

    // The playhead
    if (shownPlayhead >= 0.0 && shownPlayhead <= sourceLength)
    {
        const auto x = timeToX (shownPlayhead);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
    }

    if (rubberBanding && ! rubberBand.isEmpty())
    {
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (rubberBand);
        g.setColour (theme::accent);
        g.drawRect (rubberBand, 1.0f);
    }

    // What a drag is changing
    if (dragReadout.isNotEmpty())
    {
        const auto at = getMouseXYRelative().toFloat();
        const auto box = juce::Rectangle<float> (at.x + 14.0f, at.y - 24.0f, 130.0f, 18.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (dragReadout, box, juce::Justification::centred, false);
    }
}

void PitchEditor::resized()
{
    auto bar = menuBarArea().reduced (6, 4);
    editMenu.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (2);
    functionsMenu.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (2);
    viewMenu.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (14);
    pointerTool.setBounds (bar.removeFromLeft (62));
    bar.removeFromLeft (2);
    scissorsTool.setBounds (bar.removeFromLeft (66));
    bar.removeFromLeft (2);
    glueTool.setBounds (bar.removeFromLeft (48));
    status.setBounds (getLocalBounds().removeFromBottom (statusHeight).withTrimmedLeft (inspectorWidth + 4));

    // The inspector, as Logic's
    auto panel = juce::Rectangle<int> (0, menuHeight, inspectorWidth, getHeight() - menuHeight).reduced (10, 6);
    nameLabel.setBounds (panel.removeFromTop (20));
    notesLabel.setBounds (panel.removeFromTop (16));
    panel.removeFromTop (6);
    const auto caption = [this] (const juce::String& text) -> juce::Label*
    {
        for (auto* c : captions)
            if (c->getText() == text)
                return c;
        return nullptr;
    };
    const auto row = 22;
    caption ("Time Quantize")->setBounds (panel.removeFromTop (18));
    {
        auto r = panel.removeFromTop (row);
        timeQButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (4);
        timeGridBox.setBounds (r);
    }
    panel.removeFromTop (3);
    {
        auto r = panel.removeFromTop (row);
        caption ("Strength")->setBounds (r.removeFromLeft (60));
        strengthSlider.setBounds (r);
    }
    panel.removeFromTop (10);
    caption ("Scale Quantize")->setBounds (panel.removeFromTop (18));
    {
        auto r = panel.removeFromTop (row);
        scaleQButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (4);
        scaleKeyBox.setBounds (r.removeFromLeft (r.getWidth() / 2 - 2));
        r.removeFromLeft (4);
        scaleModeBox.setBounds (r);
    }
    panel.removeFromTop (4);
    scaleLockButton.setBounds (panel.removeFromTop (row));
    panel.removeFromTop (10);
    for (auto [name, slider] : { std::pair { "Pitch Correction", &correctionSlider }, std::pair { "Gain", &gainSlider },
                                 std::pair { "Pitch Drift", &driftSlider }, std::pair { "Vibrato", &vibratoSlider }, std::pair { "Formant Shift", &formantSlider } })
    {
        caption (name)->setBounds (panel.removeFromTop (17));
        slider->setBounds (panel.removeFromTop (row));
        panel.removeFromTop (juce::String (name) == "Gain" ? 10 : 4);
    }
    const bool fits = panel.getHeight() >= 0;
    for (auto* c : std::initializer_list<juce::Component*> { &driftSlider, &vibratoSlider, &formantSlider, caption ("Pitch Drift"), caption ("Vibrato"), caption ("Formant Shift") })
        c->setVisible (fits);   // (a short pane: they're in the Functions menu and on the notes' hot spots)
}

//==============================================================================
void PitchEditor::setTool (Tool t)
{
    tool = t;
    pointerTool.setToggleState (t == Tool::pointer, juce::dontSendNotification);
    scissorsTool.setToggleState (t == Tool::scissors, juce::dontSendNotification);
    glueTool.setToggleState (t == Tool::glue, juce::dontSendNotification);
    setMouseCursor (t == Tool::scissors ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void PitchEditor::splitAt (int note, double seconds)
{
    if (! ready || ! juce::isPositiveAndBelow (note, (int) analysis.notes.size()))
        return;
    const auto frame = (int) std::lround ((seconds - edits[(size_t) note].time) * analysis.sampleRate / analysis.hop);
    const auto before = analysis.notes.size();
    pitchcorrect::splitNote (analysis, edits, note, frame);
    if (analysis.notes.size() == before)
        return;
    selected = { note, note + 1 };
    commit ("Split Note");
}

void PitchEditor::joinSelected()
{
    if (! ready || selected.size() < 2)
        return;
    std::vector<int> which (selected.begin(), selected.end());
    pitchcorrect::joinNotes (analysis, edits, which);
    selected = { which.front() };
    commit ("Join Notes");
}

void PitchEditor::showEditMenu()
{
    juce::PopupMenu m;
    m.addItem ("Select All", [this] { for (int i = 0; i < (int) analysis.notes.size(); ++i) selected.insert (i); refreshInspector(); repaint(); });
    m.addItem ("Deselect All", [this] { selected.clear(); refreshInspector(); repaint(); });
    m.addItem ("Select Notes Off the Semitone (more than 10 cents)", [this]
    {
        selected.clear();
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
        {
            const auto c = pitchcorrect::centreAfter (analysis.notes[(size_t) i], edits[(size_t) i]);
            if (std::abs (c - std::round (c)) > 0.1)
                selected.insert (i);
        }
        refreshInspector();
        repaint();
    });
    m.addSeparator();
    m.addItem ("Split at Playhead", [this]
    {
        const auto t = shownPlayhead;
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
        {
            const auto b = noteBox (i, edits[(size_t) i]);
            if (timeToX (t) > b.getX() && timeToX (t) < b.getRight())
            {
                splitAt (i, t);
                return;
            }
        }
    });
    m.addItem ("Join Selected Notes", selected.size() >= 2, false, [this] { joinSelected(); });
    m.addSeparator();
    m.addItem ("Reset Selected Notes (as sung)", [this] { setForTargets ([] (auto& e) { e = {}; }, "Reset Pitch"); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editMenu));
}

void PitchEditor::showFunctionsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (selected.empty() ? "All notes" : juce::String ((int) selected.size()) + " selected notes");
    m.addItem ("Set to Perfect Pitch", [this] { setForTargets ([] (auto& e) { e.snap = 1.0; }, "Set to Perfect Pitch"); });
    m.addItem ("Set Pitch Drift to 0%", [this] { setForTargets ([] (auto& e) { e.drift = 1.0; }, "Pitch Drift to 0%"); });
    m.addItem ("Set Vibrato to 0%", [this] { setForTargets ([] (auto& e) { e.vibrato = 0.0; }, "Vibrato to 0%"); });
    m.addItem ("Set Vibrato to 100% (as sung)", [this] { setForTargets ([] (auto& e) { e.vibrato = 1.0; }, "Vibrato to 100%"); });
    m.addItem ("Set Formant Shift to 0", [this] { setForTargets ([] (auto& e) { e.formant = 0.0; }, "Formant Shift to 0"); });
    m.addItem ("Set Gain to 0 dB", [this] { setForTargets ([] (auto& e) { e.gain = 0.0; }, "Gain to 0 dB"); });
    m.addItem ("Set Time Back (as sung)", [this] { liveWhole = true; setForTargets ([] (auto& e) { e.time = 0.0; }, "Time as Sung"); });
    m.addSeparator();
    m.addItem ("Scale Quantize (Q)", [this] { quantizeScale(); });
    m.addItem ("Time Quantize (Q)", [this] { quantizeTime(); });
    m.addSeparator();
    m.addItem (scaleLocked() ? "Transpose Up to the Next Note in Scale" : "Transpose Up a Semitone",
               [this] { keyPressed (juce::KeyPress (juce::KeyPress::upKey)); });
    m.addItem (scaleLocked() ? "Transpose Down to the Next Note in Scale" : "Transpose Down a Semitone",
               [this] { keyPressed (juce::KeyPress (juce::KeyPress::downKey)); });
    m.addItem ("Transpose Up an Octave", [this] { setForTargets ([] (auto& e) { e.shift += 12.0; }, "Transpose"); });
    m.addItem ("Transpose Down an Octave", [this] { setForTargets ([] (auto& e) { e.shift -= 12.0; }, "Transpose"); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&functionsMenu));
}

void PitchEditor::showViewMenu()
{
    juce::PopupMenu m;
    m.addItem ("Waveform Behind the Notes", true, showWaveform, [this] { showWaveform = ! showWaveform; repaint(); });
    m.addItem ("Pitch Curves", true, showCurves, [this] { showCurves = ! showCurves; repaint(); });
    m.addSeparator();
    m.addItem ("Zoom to Fit", [this]
    {
        zoom = 1.0;
        scroll = 0.0;
        double lo = 127.0, hi = 0.0;
        for (const auto& n : analysis.notes)
        {
            lo = juce::jmin (lo, n.centre);
            hi = juce::jmax (hi, n.centre);
        }
        if (hi >= lo)
        {
            lowMidi = std::floor (lo) - 3.0;
            highMidi = juce::jmax (lowMidi + 14.0, std::ceil (hi) + 3.0);
        }
        repaint();
    });
    m.addItem ("Zoom In", [this] { zoom = juce::jmin (64.0, zoom * 1.5); repaint(); });
    m.addItem ("Zoom Out", [this] { zoom = juce::jmax (1.0, zoom / 1.5); scroll = juce::jmin (scroll, sourceLength - sourceLength / zoom); repaint(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewMenu));
}

//==============================================================================
void PitchEditor::mouseMove (const juce::MouseEvent& e)
{
    if (! ready)
        return;
    const auto note = rollArea().contains (e.getPosition()) ? noteAt (e.position) : -1;
    const auto spot = tool == Tool::pointer ? spotAt (note, e.position) : Spot::none;
    if (note != hoverNote || spot != hoverSpot)
    {
        hoverNote = note;
        hoverSpot = spot;
        setMouseCursor (tool == Tool::scissors ? juce::MouseCursor::IBeamCursor
                        : spot != Spot::none ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PitchEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverNote >= 0)
    {
        hoverNote = -1;
        hoverSpot = Spot::none;
        repaint();
    }
}

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

    if (tool == Tool::scissors)
    {
        if (note >= 0)
            splitAt (note, xToTime (e.position.x));
        return;
    }
    if (tool == Tool::glue)
    {
        if (note >= 0 && selected.count (note) == 0 && note + 1 < (int) analysis.notes.size())
            selected = { note, note + 1 };
        joinSelected();
        return;
    }

    if (note < 0)
    {
        if (! e.mods.isShiftDown())
            selected.clear();
        rubberBanding = true;
        rubberBand = {};
        refreshInspector();
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
    selected.insert (note);
    dragNote = note;
    dragSpot = spotAt (note, e.position);
    dragAxis = Axis::undecided;
    dragShift = dragTime = 0.0;
    dragStartEdits = edits;
    refreshInspector();
    repaint();
}

void PitchEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (rubberBanding)
    {
        rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        std::set<int> inside;
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
            if (noteBox (i, edits[(size_t) i]).intersects (rubberBand))
                inside.insert (i);
        if (! e.mods.isShiftDown())
            selected = inside;
        else
            selected.insert (inside.begin(), inside.end());
        refreshInspector();
        repaint();
        return;
    }
    if (dragNote < 0)
        return;
    const auto dy = (double) -e.getDistanceFromDragStartY();

    // A hot spot: up and down changes its value, on every selected note
    if (dragSpot != Spot::none)
    {
        for (const auto i : selected)
        {
            auto& ed = edits[(size_t) i];
            const auto& from = dragStartEdits[(size_t) i];
            switch (dragSpot)
            {
                case Spot::driftStart: case Spot::driftEnd: ed.drift = juce::jlimit (0.0, 1.0, from.drift + dy / 80.0); break;
                case Spot::finePitch:  ed.shift = from.shift + (e.mods.isShiftDown() ? 0.25 : 1.0) * dy / 100.0; break;
                case Spot::gain:       ed.gain = juce::jlimit (-30.0, 12.0, from.gain + dy * 0.2); break;
                case Spot::vibrato:    ed.vibrato = juce::jlimit (0.0, 2.0, from.vibrato + dy / 80.0); break;
                case Spot::formant:    ed.formant = juce::jlimit (-12.0, 12.0, from.formant + dy * 0.05); break;
                case Spot::none: break;
            }
        }
        const auto& ed = edits[(size_t) dragNote];
        switch (dragSpot)
        {
            case Spot::driftStart: case Spot::driftEnd: dragReadout = "Pitch Drift " + percent ((1.0 - ed.drift) * 100.0); break;
            case Spot::finePitch:
            {
                const auto c = pitchcorrect::centreAfter (analysis.notes[(size_t) dragNote], ed);
                dragReadout = "Fine Pitch " + juce::String (juce::roundToInt ((c - std::round (c)) * 100.0)) + " cents";
                break;
            }
            case Spot::gain:       dragReadout = "Gain " + juce::String (ed.gain, 1) + " dB"; break;
            case Spot::vibrato:    dragReadout = "Vibrato " + percent (ed.vibrato * 100.0); break;
            case Spot::formant:    dragReadout = "Formant " + juce::String (ed.formant, 1); break;
            case Spot::none: break;
        }
        liveDirty = true;
        refreshInspector();
        repaint();
        return;
    }

    // The note itself: up and down by semitones (Option: cents), or sideways in time, whichever way it goes first
    if (dragAxis == Axis::undecided && e.getDistanceFromDragStart() > 4)
        dragAxis = std::abs (e.getDistanceFromDragStartX()) > std::abs (e.getDistanceFromDragStartY()) ? Axis::time : Axis::pitch;
    if (dragAxis == Axis::pitch)
    {
        const auto semitones = dy / ((double) rollArea().getHeight() / (highMidi - lowMidi));
        const auto from = pitchcorrect::centreAfter (analysis.notes[(size_t) dragNote], edits[(size_t) dragNote]);
        dragShift = e.mods.isAltDown() ? std::round (semitones * 100.0) / 100.0
                  : scaleLocked() ? pitchcorrect::nearestInScale (from + semitones, scaleNotes()) - std::round (from)   // (by whole semitones, onto the scale)
                                  : std::round (from + semitones) - from;
        dragReadout = juce::MidiMessage::getMidiNoteName (juce::roundToInt (from + dragShift), true, true, 3)
                      + (e.mods.isAltDown() ? "  " + juce::String (juce::roundToInt (dragShift * 100.0)) + " cents" : juce::String());
        liveDirty = true;
    }
    else if (dragAxis == Axis::time)
    {
        dragTime = xToTime (e.position.x) - xToTime (e.mouseDownPosition.x);
        dragReadout = (dragTime >= 0.0 ? "+" : "") + juce::String (dragTime * 1000.0, 0) + " ms";
    }
    repaint();
}

void PitchEditor::mouseUp (const juce::MouseEvent&)
{
    dragReadout = {};
    if (rubberBanding)
    {
        rubberBanding = false;
        rubberBand = {};
        repaint();
        return;
    }
    if (dragNote >= 0 && dragSpot != Spot::none)
    {
        dragNote = -1;
        dragSpot = Spot::none;
        commit ("Note Hot Spot");
        return;
    }
    if (dragNote >= 0 && (std::abs (dragShift) > 1.0e-4 || std::abs (dragTime) > 1.0e-4))
    {
        for (const auto i : selected)
        {
            edits[(size_t) i].shift += dragShift;
            edits[(size_t) i].time += dragTime;
        }
        if (std::abs (dragTime) > 1.0e-4)
            liveWhole = true;
        const auto name = std::abs (dragTime) > 1.0e-4 ? "Move Notes in Time" : "Move Notes";
        dragNote = -1;
        dragShift = dragTime = 0.0;
        dragAxis = Axis::undecided;
        commit (name);
        return;
    }
    dragNote = -1;
    dragShift = dragTime = 0.0;
    dragAxis = Axis::undecided;
    repaint();
}

void PitchEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto note = noteAt (e.position); note >= 0 && tool == Tool::pointer)
    {
        // Onto the semitone (Logic: Set to Perfect Pitch for the note)
        edits[(size_t) note].snap = 1.0;
        commit ("Note to Perfect Pitch");
        refreshInspector();
    }
}

void PitchEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
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
        const auto anchor = yToMidi (e.position.y);
        const auto span = juce::jlimit (8.0, 60.0, (highMidi - lowMidi) * (wheel.deltaY > 0 ? 0.85 : 1.18));
        const auto t = (anchor - lowMidi) / (highMidi - lowMidi);
        lowMidi = juce::jlimit (0.0, 127.0 - span, anchor - t * span);
        highMidi = lowMidi + span;
    }
    else
    {
        const auto span = highMidi - lowMidi;
        lowMidi = juce::jlimit (0.0, 127.0 - span, lowMidi + (wheel.deltaY > 0 ? 1.0 : -1.0));
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
        if (scaleLocked() && ! fine)
        {
            // to the scale's next note up or down
            const auto scale = scaleNotes();
            for (const auto i : targets())
            {
                const auto now = analysis.notes[(size_t) i].centre + edits[(size_t) i].shift;
                const auto semitone = (int) std::lround (now);
                edits[(size_t) i].shift += pitchcorrect::nextInScale (semitone, (int) dir, scale) - semitone;
            }
            commit ("Pitch to the Next Note in Scale");
            refreshInspector();
            return true;
        }
        setForTargets ([dir, fine] (auto& e) { e.shift += dir * (fine ? 0.1 : 1.0); }, fine ? "Pitch by Cents" : "Pitch by a Semitone");
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        setForTargets ([] (auto& e) { e = {}; }, "Reset Pitch");
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selected.clear();
        for (int i = 0; i < (int) analysis.notes.size(); ++i)
            selected.insert (i);
        refreshInspector();
        repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        selected.clear();
        setTool (Tool::pointer);
        refreshInspector();
        repaint();
        return true;
    }
    // Tools, as the T menu's letters
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (! key.getModifiers().isAnyModifierKeyDown() && (c == 't' || c == 'i' || c == 'g'))
    {
        setTool (c == 'i' ? Tool::scissors : c == 'g' ? Tool::glue : Tool::pointer);
        return true;
    }
    return false;
}

} // namespace riff
