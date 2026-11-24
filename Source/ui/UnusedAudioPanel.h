#pragma once

#include <JuceHeader.h>
#include "../model/UnusedAudio.h"
#include "GlassDialogLook.h"

namespace riff
{
/**
    File > Delete Unused Audio: the project's audio files nothing uses any more (see unusedaudio::find), each with
    a play button, a little waveform, why it's unused, its length and size. Filter by recordings, renders or imports;
    tick the ones to remove and move them to the Trash or to the project's Deleted Audio folder (emptied from here).
*/
class UnusedAudioPanel final : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (const juce::File&)> preview;
        std::function<void()> stopPreview;
        /** Remove these (true: to the Trash; false: to Deleted Audio). */
        std::function<void (const juce::Array<juce::File>&, bool toTrash)> remove;
        std::function<void()> emptyDeleted, close;
    };

    UnusedAudioPanel (std::vector<unusedaudio::Item> items, juce::String projectName, juce::File audioFolder,
                      juce::AudioFormatManager& formats, bool toTrash, Callbacks callbacks);
    ~UnusedAudioPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    class Pill;
    void refresh();
    juce::Array<juce::File> selectedFiles() const;

    GlassDialogLook look;   // (first: it outlives the controls)
    std::vector<unusedaudio::Item> items;
    juce::String projectName;
    juce::File audioFolder;
    Callbacks callbacks;
    int filter = 0;   // 0 all, 1 recordings, 2 renders, 3 imports
    bool toTrash = true;
    juce::int64 folderBytes = 0;

    juce::OwnedArray<Row> rows;
    juce::OwnedArray<juce::TextButton> pills;
    juce::Viewport view;
    juce::Component holder;
    juce::ToggleButton keepRecent { "Keep audio recorded in the last 24 hours" };
    juce::TextButton trashKey { "Trash" }, folderKey { "Deleted Audio folder" }, emptyButton, selectAll { "Select All" }, cancel { "Cancel" }, go;
    juce::Label summary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnusedAudioPanel)
};
}
