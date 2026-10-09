#include "AudioEditorPanel.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

AudioEditorPanel::AudioEditorPanel (juce::ValueTree r, juce::UndoManager& um, juce::AudioFormatManager& f, juce::File folder)
    : region (r), undoManager (um), formats (f), editsFolder (std::move (folder))
{
    using Op = audioedit::Operation;
    const auto add = [this] (const juce::String& name, const juce::String& tip, std::function<void()> action)
    {
        auto* b = buttons.add (new juce::TextButton (name));
        b->setTooltip (tip);
        b->onClick = std::move (action);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    };

    add ("Normalize", "Turn up so the loudest peak is just under full scale", [this] { run ({ Op::normalize }, "Normalize"); });
    add ("Gain...", "Turn the selection up or down", [this]
    {
        ask ("Gain", "Change by how many dB?", "-3", [this] (double db) { run ({ Op::gain, db }, "Gain"); });
    });
    add ("Fade In", "Fade the selection in", [this] { run ({ Op::fadeIn }, "Fade in"); });
    add ("Fade Out", "Fade the selection out", [this] { run ({ Op::fadeOut }, "Fade out"); });
    add ("Silence", "Make the selection silent", [this] { run ({ Op::silence }, "Silence"); });
    add ("Reverse", "Play the selection backwards", [this] { run ({ Op::reverse }, "Reverse"); });
    add ("Invert", "Flip the polarity (phase)", [this] { run ({ Op::invert }, "Invert"); });
    add ("Time Stretch...", "Make the region longer or shorter without changing its pitch (Flex Time)", [this]
    {
        ask ("Time Stretch", "New length as a percentage of now (e.g. 200 for twice as long, 50 for half):", "100",
             [this] (double percent) { if (percent > 0.0) run ({ Op::timeStretch, percent / 100.0 }, "Time stretch"); });
    });
    add ("Transpose...", "Change the pitch without changing the length (Flex Pitch)", [this]
    {
        ask ("Transpose", "Semitones up (or down, with a minus):", "2",
             [this] (double semis) { run ({ Op::pitchShift, semis }, "Transpose"); });
    });

    add ("Quantize (Flex)...", "Flex Time: move every hit onto the grid, the audio between stretched to fit", [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Move the hits to");
        for (auto [name, beats] : { std::pair { "Quarter notes", 1.0 }, std::pair { "Eighth notes", 0.5 }, std::pair { "Sixteenth notes", 0.25 } })
            menu.addItem (name, [this, beats = beats]
            {
                const auto project = region.getParent().getParent();
                const auto map = model::tempoMapOf (project);
                const double regionStart = region[ids::start];
                audioedit::Request request { audioedit::Operation::flexQuantize };
                request.target = [map, regionStart, beats] (double t)
                {
                    const auto beat = map.beatAt (regionStart + t);
                    return map.secondsAt (std::round (beat / beats) * beats) - regionStart;
                };
                run (request, "Flex quantize");
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    });
    add ("Detect Tempo", "Smart Tempo: find the tempo this audio was played at, and use it for the project if you like", [this]
    {
        double rate = 48000.0;
        const auto audio = audioedit::readRegion (formats, juce::File (region[ids::file].toString()), region[ids::offset], region[ids::length], rate);
        const auto bpm = audioedit::detectTempo (audio, rate);
        if (bpm <= 0.0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Detect Tempo", "There's no clear beat in this audio.");
            return;
        }
        auto project = region.getParent().getParent();
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Detect Tempo",
            "This audio is at about " + juce::String (bpm, 1) + " BPM. Set the project's tempo to " + juce::String (bpm, 1)
                + " and start the region on a bar?",
            "Set Tempo", "Cancel", this, juce::ModalCallbackFunction::create ([this, project, bpm] (int result) mutable
            {
                if (result != 1)
                    return;
                undoManager.beginNewTransaction ("Smart Tempo");
                model::setTempo (project, bpm, &undoManager);
                // The region's first hit on the nearest bar line.
                double rate = 48000.0;
                const auto audio = audioedit::readRegion (formats, juce::File (region[ids::file].toString()), region[ids::offset], region[ids::length], rate);
                const auto onsets = audioedit::detectTransients (audio, rate);
                const auto firstHit = onsets.empty() ? 0.0 : onsets.front() / rate;
                const auto map = model::tempoMapOf (project);
                const auto hitAt = (double) region[ids::start] + firstHit;
                const auto bar = map.barAndBeat (map.beatAt (hitAt));
                const auto barStart = map.secondsAt (map.beatOfBar (bar.second > map.beatsPerBarAt (map.beatAt (hitAt)) * 0.5 ? bar.first + 1 : bar.first));
                region.setProperty (ids::start, juce::jmax (0.0, barStart - firstHit), &undoManager);
            }));
    });

    flexPitchButton.setClickingTogglesState (true);
    flexPitchButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd08a1c));
    flexPitchButton.setTooltip ("Flex Pitch: shows the notes in the audio at their pitch; drag one up or down to change it");
    flexPitchButton.onClick = [this] { if (flexPitchButton.getToggleState()) findNotes(); repaint(); };
    addAndMakeVisible (flexPitchButton);

    info.setFont (juce::FontOptions (11.5f));
    info.setColour (juce::Label::textColourId, theme::dimText);
    info.setText ("Drag over the waveform to select; edits apply to the selection, or all of it. Stretch and Transpose work on the whole region.",
                  juce::dontSendNotification);
    addAndMakeVisible (info);

    thumbnail.addChangeListener (this);
    region.addListener (this);
    loadWaveform();
    setSize (1040, 330);
}

AudioEditorPanel::~AudioEditorPanel()
{
    region.removeListener (this);
    thumbnail.removeChangeListener (this);
}

void AudioEditorPanel::findNotes()
{
    juce::MouseCursor::showWaitCursor();
    const auto audio = audioedit::readRegion (formats, juce::File (region[ids::file].toString()), region[ids::offset], region[ids::length], notesRate);
    notes = audioedit::detectNotes (audio, notesRate);
    juce::MouseCursor::hideWaitCursor();
}

std::pair<double, double> AudioEditorPanel::pitchRange() const
{
    double lo = 127.0, hi = 0.0;
    for (const auto& n : notes)
        if (n.midi > 0.0)
        {
            lo = juce::jmin (lo, n.midi);
            hi = juce::jmax (hi, n.midi);
        }
    if (hi < lo)
        return { 48.0, 72.0 };
    return { std::floor (lo) - 5.0, std::ceil (hi) + 5.0 };
}

juce::Rectangle<float> AudioEditorPanel::noteBox (const audioedit::PitchedNote& n, double lowMidi, double highMidi) const
{
    const auto a = waveArea().toFloat();
    const auto x0 = (float) secondsToX (n.start / notesRate), x1 = (float) secondsToX (n.end / notesRate);
    const auto y = a.getBottom() - (float) ((n.midi - lowMidi) / (highMidi - lowMidi)) * a.getHeight();
    return { x0 + 1.0f, y - 6.0f, juce::jmax (4.0f, x1 - x0 - 2.0f), 12.0f };
}

void AudioEditorPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragNote >= 0 && dragSemitones != 0 && juce::isPositiveAndBelow (dragNote, (int) notes.size()))
    {
        const auto& n = notes[(size_t) dragNote];
        audioedit::Request request { audioedit::Operation::pitchShift, (double) dragSemitones, n.start / notesRate, n.end / notesRate };
        selFrom = selTo = 0.0;
        run (request, "Flex Pitch");
    }
    dragNote = -1;
    dragSemitones = 0;
    repaint();
}

void AudioEditorPanel::loadWaveform()
{
    thumbnail.setSource (new juce::FileInputSource (juce::File (region[ids::file].toString())));
    selFrom = selTo = 0.0;
    if (flexPitchButton.getToggleState())
        findNotes();
    repaint();
}

void AudioEditorPanel::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == region && (p == ids::file || p == ids::offset || p == ids::length))
        loadWaveform();
}

juce::Rectangle<int> AudioEditorPanel::waveArea() const
{
    return getLocalBounds().reduced (10).withTrimmedBottom (70);
}

double AudioEditorPanel::xToSeconds (int x) const
{
    const auto a = waveArea();
    return juce::jlimit (0.0, (double) region[ids::length], (x - a.getX()) / (double) a.getWidth() * (double) region[ids::length]);
}

int AudioEditorPanel::secondsToX (double s) const
{
    const auto a = waveArea();
    return a.getX() + juce::roundToInt (s / juce::jmax (1.0e-6, (double) region[ids::length]) * a.getWidth());
}

void AudioEditorPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    const auto a = waveArea();
    g.setColour (theme::lcd);
    g.fillRect (a);

    const double offset = region[ids::offset], length = region[ids::length];
    g.setColour (model::trackColour (region.getParent()).brighter (0.6f));
    if (thumbnail.getTotalLength() > 0.0)
        thumbnail.drawChannels (g, a.reduced (0, 4), offset, offset + length, (float) (double) region.getProperty (ids::gain, 1.0));

    // Flex Pitch: each note as a bar at its pitch, with its name.
    if (flexPitchButton.getToggleState())
    {
        const auto [lo, hi] = pitchRange();
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto n = notes[i];
            if (n.midi <= 0.0)
                continue;
            if ((int) i == dragNote)
                n.midi += dragSemitones;
            const auto box = noteBox (n, lo, hi);
            g.setColour (juce::Colour (0xffd08a1c).withAlpha ((int) i == dragNote ? 0.95f : 0.75f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            const auto cents = juce::roundToInt ((n.midi - std::round (n.midi)) * 100.0);
            g.drawText (juce::MidiMessage::getMidiNoteName (juce::roundToInt (n.midi), true, true, 3)
                            + (std::abs (cents) > 9 ? (cents > 0 ? " +" : " ") + juce::String (cents) : juce::String()),
                        box.translated (0.0f, -13.0f).withHeight (12.0f).withWidth (juce::jmax (60.0f, box.getWidth())), juce::Justification::left, false);
        }
    }

    if (selTo > selFrom)
    {
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (secondsToX (selFrom), a.getY(), secondsToX (selTo), a.getBottom()));
    }

    g.setColour (theme::outline);
    g.drawRect (a);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    const auto sel = selTo > selFrom ? juce::String (selFrom, 3) + " - " + juce::String (selTo, 3) + " s selected" : juce::String ("All");
    g.drawText (juce::File (region[ids::file].toString()).getFileName() + "   " + juce::String (length, 2) + " s   " + sel,
                a.getX() + 6, a.getY() + 4, a.getWidth() - 12, 16, juce::Justification::topLeft, true);
}

void AudioEditorPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto bottom = r.removeFromBottom (60);
    info.setBounds (bottom.removeFromBottom (24));
    auto row = bottom.removeFromTop (30);
    flexPitchButton.setBounds (row.removeFromRight (90).reduced (2, 0));
    const auto w = row.getWidth() / juce::jmax (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (row.removeFromLeft (w).reduced (2, 0));
}

void AudioEditorPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! waveArea().contains (e.getPosition()))
        return;

    if (flexPitchButton.getToggleState())
    {
        const auto [lo, hi] = pitchRange();
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].midi > 0.0 && noteBox (notes[i], lo, hi).expanded (0.0f, 3.0f).contains (e.position))
            {
                dragNote = (int) i;
                dragSemitones = 0;
                return;
            }
    }

    dragAnchor = xToSeconds (e.x);
    selFrom = selTo = dragAnchor;
    repaint();
}

void AudioEditorPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNote >= 0)
    {
        const auto [lo, hi] = pitchRange();
        const auto perSemitone = (double) waveArea().getHeight() / (hi - lo);
        dragSemitones = juce::jlimit (-12, 12, (int) std::round (-e.getDistanceFromDragStartY() / perSemitone));
        repaint();
        return;
    }
    const auto t = xToSeconds (e.x);
    selFrom = juce::jmin (dragAnchor, t);
    selTo = juce::jmax (dragAnchor, t);
    repaint();
}

void AudioEditorPanel::ask (const juce::String& title, const juce::String& question, const juce::String& initial,
                            std::function<void (double)> then)
{
    auto* window = new juce::AlertWindow (title, question, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("value", initial);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, then] (int result)
    {
        if (result == 1)
            then (window->getTextEditorContents ("value").getDoubleValue());
    }), true);
}

void AudioEditorPanel::run (audioedit::Request request, const juce::String& name)
{
    if (model::numTakes (region) > 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Audio File Editor",
                                                "This is a take folder. Choose a take and use Keep only this take (or flatten the comp) first.");
        return;
    }

    const bool wholeRegion = request.operation == audioedit::Operation::timeStretch || request.operation == audioedit::Operation::pitchShift
                            || request.operation == audioedit::Operation::flexQuantize;
    if (selTo > selFrom + 1.0e-3 && ! wholeRegion)
    {
        request.from = selFrom;
        request.to = selTo;
    }

    const juce::File source (region[ids::file].toString());
    const auto dest = editsFolder.getNonexistentChildFile (source.getFileNameWithoutExtension() + " " + name, ".wav", false);
    double newLength = 0.0;

    juce::MouseCursor::showWaitCursor();
    const auto error = audioedit::processFile (formats, source, region[ids::offset], region[ids::length], request, dest, newLength);
    juce::MouseCursor::hideWaitCursor();

    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't edit the audio", error);
        return;
    }

    undoManager.beginNewTransaction ("Audio: " + name);
    region.setProperty (ids::file, dest.getFullPathName(), &undoManager);
    region.setProperty (ids::offset, 0.0, &undoManager);
    region.setProperty (ids::length, newLength, &undoManager);
}

} // namespace riff
