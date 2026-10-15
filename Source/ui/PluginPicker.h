#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The insert slot's plugin chooser, as in Logic and other DAWs: a search
    field at the top (type part of a name, maker, category or format), then
    the plugins grouped the way you choose: by Maker, by Category, by Format,
    or just your Favorites. Click the star beside a plugin to make it a
    favorite. Up/Down to move, Return to choose, Escape to close.
*/
class PluginPicker final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::TextEditor::Listener,
                           private juce::KeyListener
{
public:
    struct Entry
    {
        juce::String name, maker, format;   // format: "VST3", "AU", "CLAP", or empty for a built-in
        std::function<void()> choose;
        juce::String category;              // "Dynamics", "EQ", "Reverb"... (see categoryOf)
        juce::String key;                   // stays the same between sessions (for favorites)
    };

    enum class View { favorites, maker, category, format };

    struct Settings
    {
        juce::StringArray favorites;        // entries' keys
        View view = View::maker;
        juce::StringArray collapsed;        // folders (makers, categories...) closed, as in Cubase's plugin selector
    };

    PluginPicker (std::vector<Entry> entries, std::function<void()> onScan, std::function<void()> onRemove,
                  Settings settings, std::function<void (const Settings&)> onSettingsChanged = {});

    void resized() override;
    void paint (juce::Graphics& g) override;
    void visibilityChanged() override;

    /** The matches for a search: every word must be in the name, maker, category or format (any case). */
    static std::vector<int> filter (const std::vector<Entry>& entries, const juce::String& search);

    /** A plain category for a plugin from what its format says ("Fx|Dynamics", "Effect"...) and its name. */
    static juce::String categoryOf (const juce::String& pluginCategory, const juce::String& name);

private:
    friend class SnapshotTool;
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { choose (row); }
    void textEditorTextChanged (juce::TextEditor&) override { refilter(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override { choose (list.getSelectedRow()); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;

    void refilter();
    void choose (int row);
    void close();
    void setView (View v);
    juce::String groupOf (const Entry& e) const;
    bool isFavorite (const Entry& e) const { return settings.favorites.contains (e.key); }

    // Cubase-style: a column of folders (each maker, or category...) on the left to narrow the list to one;
    // folder rows in the list open and close with a click.
    struct FolderList final : juce::ListBoxModel
    {
        PluginPicker& owner;
        explicit FolderList (PluginPicker& o) : owner (o) {}
        int getNumRows() override { return (int) owner.folders.size() + 1; }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    };
    FolderList folderModel { *this };
    juce::ListBox folderList { "Folders", &folderModel };
    std::vector<std::pair<juce::String, int>> folders;   // name, how many plugins
    juce::String folderFilter;                           // empty: all folders
    void toggleFolder (const juce::String& folder);

    std::vector<Entry> entries;
    std::vector<int> shown;
    struct Row { int entry = -1; juce::String header; };   // a plugin, or a group's name over its plugins
    std::vector<Row> rows;
    void selectEntryRow (int from, int direction);

    Settings settings;
    std::function<void (const Settings&)> onSettingsChanged;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> viewButtons;   // Favorites, Maker, Category, Format
    juce::ListBox list { "Plugins", this };
    juce::TextButton scanButton { "Scan for plugins..." }, removeButton { "Remove" };
    std::function<void()> onScan, onRemove;
};

} // namespace riff
