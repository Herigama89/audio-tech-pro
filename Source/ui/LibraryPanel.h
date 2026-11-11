#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "../engine/SampleInfo.h"

namespace riff
{

/**
    Logic's Library and Loop Browser, down the right of the window.

    Patches: a track's whole sound (instrument, plugins, channel strip) saved by name; click one to put it on the
    selected track.

    Loops: the loops and samples in a folder you choose (and its folders), in tabs by instrument (as Cubase's
    MediaBay), with each one's tempo and key (from its name, or found by listening - in the background, kept between
    sessions). Click one to hear it (the arrow keys go to the next and play it, as FL Studio's browser); in the song's
    tempo and key if you like. Double-click, or drag it onto a track, to add it - following the song's tempo and key.
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

    /** The song now (where the playhead is): its tempo and key, and when its next beat comes while it plays. */
    struct Song { double bpm = 120.0; int keyRoot = 0; bool minor = false; bool playing = false; double toNextBeat = 0.0; };
    std::function<Song()> songNow;

    /** How a sample from here should sit in the song when added (with the switches as they are): its own tempo to
        follow the song's from (0: as it is), the key root it follows the song's key from (-1: as it is). */
    struct Fit { double sourceBpm = 0.0; int sourceKeyRoot = -1; };
    Fit fitFor (const juce::File& file);

    static juce::File patchesFolder();
    static inline const juce::String patchExtension { ".atpatch" };

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    static constexpr int preferredWidth = 340;

    class Analyser;
    class CategoryTabs;
    class Waveform;
    class Switch;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void timerCallback() override;

    void rescan();
    void filter();
    void chooseFolder();
    void play (int row);
    void stop();
    sampleinfo::Info infoFor (const juce::File& file, bool* known = nullptr);
    void saveSettings();

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    Page page = Page::patches;

    juce::TextButton patchesTab { "Patches" }, loopsTab { "Loops" }, closeButton { "Close" };
    juce::TextButton savePatchButton { "Save Track as Patch..." }, folderButton;
    juce::TextEditor search;
    juce::ListBox list;

    // Loops page
    std::unique_ptr<CategoryTabs> tabs;
    juce::TextButton loopsOnly { "Loops" }, oneShots { "One-shots" }, both { "Both" };
    juce::ComboBox keyFilter;
    std::unique_ptr<Switch> tempoSwitch, keySwitch, inTimeSwitch;
    juce::Label songLabel;
    std::unique_ptr<Waveform> waveform;
    juce::TextButton playButton;
    juce::Slider volume;
    juce::Label volumeLabel;

    int category = -1;        // -1: all
    int kind = 2;             // 0 loops, 1 one-shots, 2 both

    juce::Array<juce::File> patches, allLoops, shownLoops;
    juce::File loopFolder;
    int previewRow = -1, lastPlayedRow = -1, shownGeneration = 0, ticks = 0;
    bool waveformWasPlaying = false, justSelected = false, quietSelect = false;
    juce::Rectangle<int> syncBox, columnHeads, hint;
    juce::File previewing;
    std::atomic<int> previewGeneration { 0 };
    juce::ThreadPool fitter { 1 };
    juce::AudioFormatManager formats;

    std::unique_ptr<Analyser> analyser;
    std::unique_ptr<juce::FileChooser> chooser;

    friend class SnapshotTool;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LibraryPanel)
};

} // namespace riff
