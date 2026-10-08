#pragma once

#include <JuceHeader.h>
#include "TimelineState.h"
#include "WaveformCache.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioFileCache.h"

namespace riff
{

/**
    The tracks area: bar ruler, track lanes, regions with waveforms and the
    playhead. Handles selecting, moving (across tracks), trimming, file drops,
    zoom and scroll.
*/
class ArrangeCanvas final : public juce::Component,
                            public juce::FileDragAndDropTarget,
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
    double snap (double t) const;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Called when the user scrolls or zooms with the wheel (so scrollbars can update). */
    std::function<void()> onViewChanged;

private:
    enum class DragMode { none, scrub, move, trimStart, trimEnd };

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
    int dropHighlightTrack = -2; // -2: none, -1 / out of range: "new track" zone
};

} // namespace riff
