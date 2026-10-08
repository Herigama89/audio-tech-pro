#pragma once

#include <JuceHeader.h>
#include "TimelineState.h"

namespace riff
{

/** One track header: name, mute, solo, volume and pan. Edits go through the UndoManager. */
class TrackHeader final : public juce::Component,
                          private juce::ValueTree::Listener
{
public:
    using InputNamesFn = std::function<juce::StringArray()>;

    TrackHeader (juce::ValueTree track, juce::UndoManager& um, TimelineState& timeline, InputNamesFn inputNames);
    ~TrackHeader() override;

    juce::ValueTree getTrack() const { return track; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();
    void select();
    void showMenu();
    void showInputMenu();

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    TimelineState& timeline;

    juce::Label nameLabel;
    InputNamesFn inputNames;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, monitorButton { "I" };
    juce::Slider volumeSlider, panKnob;
};

/** The column of track headers, kept in sync with the project's tracks and the vertical scroll. */
class TrackHeaderList final : public juce::Component,
                              private juce::ValueTree::Listener,
                              private juce::ChangeListener
{
public:
    TrackHeaderList (juce::UndoManager& um, TimelineState& timeline);
    ~TrackHeaderList() override;

    void setProject (juce::ValueTree newProject);

    /** Supplies the names of the device's active inputs, for the input menu. */
    TrackHeader::InputNamesFn inputNames;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

private:
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override        { if (parent == project) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { if (parent == project) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override         { if (parent == project) rebuild(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override                      { resized(); repaint(); }

    void rebuild();

    juce::ValueTree project;
    juce::UndoManager& undoManager;
    TimelineState& timeline;
    juce::Component content;
    juce::OwnedArray<TrackHeader> headers;
};

} // namespace riff
