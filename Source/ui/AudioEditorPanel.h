#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEdit.h"

namespace riff
{

/**
    Logic's Audio File Editor for one audio region: its waveform, a selection
    you drag, and edits on the selection (or all of it when nothing is
    selected): Normalize, Gain, Fade In/Out, Silence, Reverse, Invert, and
    Flex-style Time Stretch and Transpose. Each edit writes a new file the
    region then plays, so the original stays and Undo brings it back.
*/
class AudioEditorPanel final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::ValueTree::Listener
{
public:
    AudioEditorPanel (juce::ValueTree region, juce::UndoManager& um, juce::AudioFormatManager& formats, juce::File editsFolder);
    ~AudioEditorPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    void run (audioedit::Request request, const juce::String& name);
    void ask (const juce::String& title, const juce::String& question, const juce::String& initial,
              std::function<void (double)> then);
    void loadWaveform();
    juce::Rectangle<int> waveArea() const;
    double xToSeconds (int x) const;
    int secondsToX (double s) const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override;

    juce::ValueTree region;
    juce::UndoManager& undoManager;
    juce::AudioFormatManager& formats;
    juce::File editsFolder;

    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail thumbnail { 256, formats, thumbCache };
    double selFrom = 0.0, selTo = 0.0, dragAnchor = 0.0;

    juce::OwnedArray<juce::TextButton> buttons;
    juce::Label info;
};

} // namespace riff
