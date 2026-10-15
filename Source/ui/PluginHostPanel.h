#pragma once

#include <JuceHeader.h>
#include "InstrumentWindows.h"
#include "InsertBar.h"

namespace riff
{

/**
    A VST3, AU or CLAP plugin's window, as in Logic: a header over the
    plugin's own editor with
      - Editor / Controls: the plugin's own face, or every parameter as a
        plain slider (right-click any of them for automation, MIDI Learn
        and default),
      - Automation: every parameter, each with Logic's control menu,
      - the last parameter you touched in the plugin's own face, with the
        same menu (a plugin's own knobs can't show our menu themselves).
*/
class PluginHostPanel final : public juce::Component,
                              private juce::ComponentListener,
                              private juce::AudioProcessorListener,
                              private juce::Timer
{
public:
    struct Context
    {
        juce::ValueTree project;
        juce::String owner;      // the track id (or "master")
        juce::String insertId;   // the insert: its parameters can be automated; empty for an instrument
        juce::UndoManager* um = nullptr;
        juce::ValueTree insert;  // the INSERT node: the header's Bypass and Side Chain keys (none for an instrument)
        /** A knob turned in the plugin's face (parameter, value 0..1): the same plugin on the other selected channels follows. */
        std::function<void (int index, float value)> onLinkedChange;
    };

    PluginHostPanel (juce::AudioPluginInstance& plugin, juce::AudioProcessorEditor* editorToOwn, Context context);
    ~PluginHostPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isResizableHost() const { return editor == nullptr || editor->isResizable() || showingControls; }

    static constexpr int headerHeight = 30;

    /** The parameter's automation key ("insert:<id>:#n"), empty when it can't be automated. */
    juce::String paramKey (int index) const;

    /** Fills a menu with one submenu per parameter (Logic's control menu for each). */
    void addParameterMenus (juce::PopupMenu& menu);
    void showParameterMenu (int index, juce::Component& target);

private:
    friend class SnapshotTool;
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { lastTouched = index; }
    void timerCallback() override;

    void showControls (bool controls);
    void fitToContent();

    juce::AudioPluginInstance& plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    Context context;

    juce::TextButton editorButton { "Editor" }, controlsButton { "Controls" }, automationButton { "Automation" };
    juce::TextButton touchedButton;
    std::unique_ptr<InsertBar> insertBar;
    std::atomic<int> lastTouched { -1 };
    int shownTouched = -2;
    float sentValue = -1.0f;   // the touched knob's value last passed to the other selected channels
    bool showingControls = false, fitting = false;

    struct Row
    {
        juce::Label name, value;
        MenuSlider slider;
    };
    juce::OwnedArray<Row> rows;
    juce::Component rowsHolder;
    juce::Viewport viewport;
};

} // namespace riff
