#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/** Logic's Project Notes (the Notepad key): text kept in the project, saved with it. */
class NotepadPanel final : public juce::Component,
                           private juce::TextEditor::Listener,
                           private juce::ValueTree::Listener
{
public:
    NotepadPanel()
    {
        title.setText ("Project Notes", juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (title);

        editor.setMultiLine (true, true);
        editor.setReturnKeyStartsNewLine (true);
        editor.setFont (juce::FontOptions (14.0f));
        editor.setColour (juce::TextEditor::backgroundColourId, theme::toolbarDark);
        editor.setColour (juce::TextEditor::textColourId, theme::text);
        editor.setColour (juce::TextEditor::outlineColourId, theme::outline);
        editor.setTextToShowWhenEmpty ("Lyrics, ideas, what to fix... saved with the project.", theme::dimText);
        editor.addListener (this);
        addAndMakeVisible (editor);
    }

    ~NotepadPanel() override { project.removeListener (this); }

    void setProject (juce::ValueTree p)
    {
        project.removeListener (this);
        project = p;
        project.addListener (this);
        editor.setText (project["notes"].toString(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (juce::Colours::black);
        g.fillRect (0, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        editor.setBounds (r);
    }

    static constexpr int preferredWidth = 300;

private:
    void textEditorTextChanged (juce::TextEditor&) override
    {
        if (project.isValid())
            project.setProperty ("notes", editor.getText(), nullptr);   // (typing isn't an undo step)
    }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override
    {
        if (t == project && p.toString() == "notes" && editor.getText() != project["notes"].toString())
            editor.setText (project["notes"].toString(), juce::dontSendNotification);
    }

    juce::ValueTree project;
    juce::Label title;
    juce::TextEditor editor;
};

/** Logic's Browsers (the folder key): find audio files on your computer; double-click (or Add) to put one on the selected track at the playhead. */
class BrowserPanel final : public juce::Component,
                           private juce::FileBrowserListener
{
public:
    explicit BrowserPanel (const juce::String& audioWildcard)
        : filter (audioWildcard, "*", "Audio files"),
          browser (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::useTreeView,
                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), &filter, nullptr)
    {
        title.setText ("Browser", juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (title);
        browser.addListener (this);
        addAndMakeVisible (browser);
        addButton.onClick = [this] { addSelected(); };
        addAndMakeVisible (addButton);
    }

    ~BrowserPanel() override { browser.removeListener (this); }

    /** Put an audio file on the selected track at the playhead. */
    std::function<void (const juce::File&)> onAddFile;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (juce::Colours::black);
        g.fillRect (0, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        addButton.setBounds (r.removeFromBottom (26));
        r.removeFromBottom (6);
        browser.setBounds (r);
    }

    static constexpr int preferredWidth = 320;

private:
    void addSelected()
    {
        const auto file = browser.getHighlightedFile();
        if (file.existsAsFile() && onAddFile)
            onAddFile (file);
    }
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override
    {
        if (file.existsAsFile() && onAddFile)
            onAddFile (file);
    }
    void browserRootChanged (const juce::File&) override {}

    juce::WildcardFileFilter filter;
    juce::FileBrowserComponent browser;
    juce::Label title;
    juce::TextButton addButton { "Add to the Selected Track" };
};

} // namespace riff
