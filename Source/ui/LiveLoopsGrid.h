#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"

namespace riff
{

/**
    Logic's Live Loops grid: a row per track, a column per scene. Each cell
    holds a loop (a region). Click a cell to start it on the next bar (it
    loops until you stop it, and it replaces the track's timeline meanwhile);
    click it again to stop it at the next bar. The numbers on top start a
    whole scene. An empty cell takes the selected region when you click it.
*/
class LiveLoopsGrid final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::Timer
{
public:
    LiveLoopsGrid (juce::UndoManager& um, AudioEngine& engine, ProjectPlayer& player);
    ~LiveLoopsGrid() override;

    void setProject (juce::ValueTree project);

    /** The region the empty cell you click takes. */
    std::function<juce::ValueTree()> selectedRegion;
    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    static constexpr int numScenes = 8;

private:
    juce::Rectangle<int> cellBounds (int row, int scene) const;
    juce::Array<juce::ValueTree> tracks() const;
    double nextBar() const;
    void triggerCell (const juce::ValueTree& track, int scene);
    void triggerScene (int scene);
    void stopTrack (const juce::ValueTree& track);
    void showCellMenu (const juce::ValueTree& track, int scene);

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void timerCallback() override;

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    ProjectPlayer& player;
    juce::ValueTree project;
    juce::TextButton stopAllButton { "Stop All" }, captureButton { "Cycle to Scene..." }, closeButton { "Close" };
    int scrollRows = 0;
    bool wasPlaying = false;

    static constexpr int headerHeight = 30, sceneRowHeight = 26, rowHeight = 34, nameWidth = 130, cellWidth = 110;
};

} // namespace riff
