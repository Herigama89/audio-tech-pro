#pragma once

#include <JuceHeader.h>
#include "TimelineState.h"
#include "WaveformCache.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioFileCache.h"
#include "../model/Project.h"

namespace riff
{

/**
    The tracks area: bar ruler, track lanes, regions with waveforms and the
    playhead. Handles selecting, moving (across tracks), trimming, file drops,
    zoom and scroll.
*/
class ArrangeCanvas final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropTarget,
                            private juce::ValueTree::Listener,
                            private juce::ChangeListener
{
public:
    ArrangeCanvas (juce::UndoManager& um, TimelineState& timeline, AudioEngine& engine,
                   AudioFileCache& fileCache, WaveformCache& waveforms);
    ~ArrangeCanvas() override;

    void setProject (juce::ValueTree newProject);

    /** Imports files as regions: onto `trackIndex` (or new tracks if out of range) at `time`. */
    void importFiles (const juce::StringArray& files, int trackIndex, double time);

    /** Grid size in seconds at the current zoom: a beat, or a bar when zoomed out. */
    double gridSeconds() const;
    double gridBeats() const;
    double snap (double t) const;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Files dragged from the Loop Browser ("file:<path>").
    bool isInterestedInDragSource (const SourceDetails& details) override { return details.description.toString().startsWith ("file:"); }
    void itemDragMove (const SourceDetails& details) override { fileDragMove ({}, details.localPosition.x, details.localPosition.y); }
    void itemDragExit (const SourceDetails&) override { fileDragExit ({}); }
    void itemDropped (const SourceDetails& details) override
    {
        filesDropped ({ details.description.toString().fromFirstOccurrenceOf ("file:", false, false) }, details.localPosition.x, details.localPosition.y);
    }

    /** After regions were moved, pasted or trimmed (or deleted from `tracks`): what the Drag mode does next
        (No Overlap shortens what they cover, X-Fade crossfades, Shuffle closes the gaps). */
    void applyDragMode (const juce::Array<juce::ValueTree>& regions, juce::Array<juce::ValueTree> tracks);

    /** Region gain, as a slider in a pop-up. */
    void showGainPopup (juce::ValueTree region);

    /** Called when the user scrolls or zooms with the wheel (so scrollbars can update). */
    std::function<void()> onViewChanged;

    /** Called to open a MIDI region in the piano roll. */
    std::function<void (juce::ValueTree region)> onOpenPianoRoll;

    /** Called to open an audio region in the Audio File Editor. */
    std::function<void (juce::ValueTree region)> onOpenAudioEditor;

private:
    enum class DragMode { none, scrub, move, trimStart, trimEnd, cycleNew, cycleMove, automationPoint, rubberBand, pencilRegion, marker,
                          fadeIn, fadeOut, loopEnd, punch, marquee, zoomRect, solo, tempo, compSwipe };
    int takeLaneAt (const juce::ValueTree& region, int trackIndex, juce::Point<float> pos) const;   // -1: not on a take lane
    void paintTakeLanes (juce::Graphics& g, const juce::ValueTree& region, juce::Rectangle<float> body);
    void restoreComp (juce::ValueTree region, const std::vector<model::CompPiece>& segments, juce::UndoManager* um);
    std::vector<model::CompPiece> compBefore;
    int swipeLane = -1;
    void paintTempo (juce::Graphics& g);
    juce::ValueTree tempoChangeAt (float x) const;
    void tempoMouseDown (const juce::MouseEvent& e);
    void editTempoChange (juce::ValueTree change, double beat);
    juce::ValueTree dragTempo;
    double dragTempoBpm = 120.0;
    juce::ValueTree soloTrack;      // the Solo tool's track while the mouse is held
    bool soloWasOn = false;
    juce::Rectangle<float> punchBounds() const;
    int punchDrag = 0;   // 1: the in point, 2: the out point, 3: the whole range, 0: a new one

    /** What a press at `pos` on a region would do with the pointer: trim, fade, loop or move. */
    DragMode edgeActionAt (const juce::ValueTree& region, int trackIndex, juce::Point<float> pos) const;

    void paintMarkers (juce::Graphics& g);
    juce::ValueTree markerAt (float x) const;
    void markerMouseDown (const juce::MouseEvent& e);
    void renameMarker (juce::ValueTree marker);
    juce::ValueTree dragMarker;

    struct Dragged { juce::ValueTree region; double start; int trackIndex; };
    std::vector<Dragged> dragged;          // every region moving with the one under the mouse
    bool copyOnDrag = false, copiesMade = false;
    juce::Rectangle<float> rubberBand;
    juce::StringArray selectionBeforeRubberBand;

    void startMove (const juce::MouseEvent& e, juce::ValueTree region);
    void dragMove (const juce::MouseEvent& e, bool useSnap);
    void updateRubberBand (const juce::MouseEvent& e);
    void useTool (const juce::MouseEvent& e, juce::ValueTree region, int trackIndex);
    void showRegionMenu (juce::ValueTree region);

public:
    /** Region commands the window's menus and keys run too. */
    std::function<void()> onSplit, onDelete, onRepeat, onJoin, onMuteRegions;

private:

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override                    { repaint(); }

    juce::Rectangle<float> regionBounds (const juce::ValueTree& region, int trackIndex) const;
    juce::ValueTree regionAt (juce::Point<float> pos, int& trackIndexOut) const;
    void paintRuler (juce::Graphics& g);
    void paintGrid (juce::Graphics& g);
    void paintRegion (juce::Graphics& g, const juce::ValueTree& region, int trackIndex);
    void paintMidiNotes (juce::Graphics& g, const juce::ValueTree& region, juce::Rectangle<float> body, juce::Colour colour);
    void paintLiveTakes (juce::Graphics& g);
    void paintCycle (juce::Graphics& g);
    void showTakeMenu (juce::ValueTree region);
    void addTakeItems (juce::PopupMenu& menu, juce::ValueTree region);
    juce::Rectangle<float> cycleBounds() const;

    // Automation lanes (drawn over a track when its A button is on)
    static juce::String automationParamOf (const juce::ValueTree& track);
    double staticValueOf (const juce::ValueTree& track, const juce::String& param) const;
    float valueToY (double value, const model::ParamRange& range, int trackIndex) const;
    double yToValue (float y, const model::ParamRange& range, int trackIndex) const;
    void paintAutomation (juce::Graphics& g, const juce::ValueTree& track, int trackIndex);
    juce::ValueTree automationPointAt (const juce::ValueTree& track, int trackIndex, juce::Point<float> pos) const;
    bool handleAutomationMouseDown (const juce::MouseEvent& e, int trackIndex);
    void setPlayheadFromX (float x);
    void zoomAround (float x, double factor);

    juce::ValueTree project;
    juce::UndoManager& undoManager;
    TimelineState& timeline;
    AudioEngine& engine;
    AudioFileCache& fileCache;
    WaveformCache& waveforms;

    DragMode dragMode = DragMode::none;
    juce::ValueTree dragRegion;
    double dragOriginalStart = 0.0, dragMouseDownTime = 0.0, dragSourceLength = 0.0;
    double dragCycleStart = 0.0, dragCycleEnd = 0.0;
    bool clickedTakeHeader = false;
    juce::ValueTree dragPoint;
    int dragPointTrack = -1;
    int dropHighlightTrack = -2; // -2: none, -1 / out of range: "new track" zone
};

} // namespace riff
