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
    using InstrumentMenuFn = std::function<void (juce::ValueTree track, juce::Component& target)>;

    TrackHeader (juce::ValueTree track, juce::UndoManager& um, TimelineState& timeline,
                 InputNamesFn inputNames, InstrumentMenuFn instrumentMenu);
    ~TrackHeader() override;

    juce::ValueTree getTrack() const { return track; }

    static constexpr int numberWidth = 30;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** The strip along the bottom edge that resizes the track (Option: every track). */
    static constexpr int resizeEdge = 6;

private:
    bool isOnResizeEdge (const juce::MouseEvent& e) const;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();

public:
    void followAutomation (double time);

    /** Names of a hosted plugin's parameters (for the automation menu), by insert id. */
    static inline std::function<juce::StringArray (const juce::String& insertId)> pluginParameterNames;

private:
    void select();
    void showMenu();
public:
    void showColours();
private:
    void showInputMenu();
    void showAutomationMenu();
    juce::String automationParamName (const juce::ValueTree& project, const juce::String& param);

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    TimelineState& timeline;

    juce::Label nameLabel;
    InputNamesFn inputNames;
    InstrumentMenuFn instrumentMenu;
    bool isInstrument = false, isBus = false;
    bool lastClickWasPopup = false; // buttons also "click" on right-click; those open menus instead
    bool resizing = false, resizingAll = false, reordering = false, clickKeptSelection = false;
    int heightAtDragStart = 0;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, monitorButton { "I" }, automationButton { "A" };
    juce::TextButton instrumentButton;
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

    /** Shows the instrument menu for an instrument track. */
    TrackHeader::InstrumentMenuFn instrumentMenu;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    /** Opens the colour palette for the selected tracks, next to the main one's header. */
    void showColoursForSelection();

    /** Dragging headers to reorder tracks (called by the header being dragged). */
    void reorderDrag (TrackHeader& header, const juce::MouseEvent& e);
    void reorderDrop (TrackHeader& header, const juce::MouseEvent& e);

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    /** Volume and pan show what the automation is doing at `time`. */
    void followAutomation (double time)  { for (auto* h : headers) h->followAutomation (time); }

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
    int dropIndex = -1;   // where dragged tracks would land (-1: not dragging)

    int dropIndexAt (int yInList) const;
};

/** Logic's colour palette: click a colour for the selected tracks. */
class TrackColourPalette final : public juce::Component
{
public:
    explicit TrackColourPalette (std::function<void (juce::Colour)> onPick);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    int indexAt (juce::Point<int> p) const;
    juce::Rectangle<int> cell (int index) const;
    static constexpr int columns = 8, cellSize = 24, gap = 4;
    std::function<void (juce::Colour)> pick;
    int hover = -1;
};

} // namespace riff
