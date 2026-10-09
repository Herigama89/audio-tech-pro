#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"

namespace riff
{

/**
    Logic's Library and Loop Browser, down the right of the window.

    Patches: a track's whole sound (instrument, plugins, channel strip) saved
    by name; click one to put it on the selected track.
    Loops: the audio files in a folder you choose (and its folders), with a
    search; click one to hear it, double-click (or drag it onto a track) to
    add it at the playhead.
*/
class LibraryPanel final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::Timer
{
public:
    LibraryPanel (AudioEngine& engine, juce::PropertiesFile& settings);
    ~LibraryPanel() override;

    enum class Page { patches, loops };
    void showPage (Page page);
    Page getPage() const noexcept { return page; }

    /** Put a patch file on the selected track. */
    std::function<void (const juce::File& patch)> onLoadPatch;
    /** Save the selected track's sound as a patch with this name. */
    std::function<void (const juce::String& name)> onSavePatch;
    /** Add an audio file at the playhead (selected track). */
    std::function<void (const juce::File& file)> onAddFile;
    std::function<void()> onClose;

    static juce::File patchesFolder();
    static inline const juce::String patchExtension { ".atpatch" };

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 300;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void timerCallback() override;

    void rescan();
    void filter();
    void chooseFolder();

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    Page page = Page::patches;

    juce::TextButton patchesTab { "Patches" }, loopsTab { "Loops" }, closeButton { "Close" };
    juce::TextButton savePatchButton { "Save Track as Patch..." }, folderButton, stopButton { "Stop" };
    juce::TextEditor search;
    juce::ListBox list;

    juce::Array<juce::File> patches, allLoops, shownLoops;
    juce::File loopFolder;
    int previewRow = -1;

    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace riff
