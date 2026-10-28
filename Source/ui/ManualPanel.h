#pragma once

#include <JuceHeader.h>

namespace riff
{

/** The manual's files: Manual/ beside the app (index.txt, chapters/*.md, img/, the PDF). */
namespace manual
{
    struct Chapter { juce::String tab, title; juce::File file; juce::String text; };

    /** Where the manual is (an empty File when it isn't installed). */
    juce::File folder();
    /** Every chapter in order, read from index.txt. */
    const std::vector<Chapter>& chapters();
    /** The tabs, in order. */
    juce::StringArray tabs();
    /** The PDF of the manual. */
    juce::File pdf();

    /** One block of a chapter: a heading, a paragraph, a list item, a tip, a code block or a picture. */
    struct Block
    {
        enum class Kind { h1, h2, h3, paragraph, bullet, number, quote, code, image } kind = Kind::paragraph;
        juce::String text;     // (an image: its caption)
        juce::String image;    // the picture's path within the manual folder
        int level = 0;         // a list item's indent
        int number = 0;        // a numbered item's number
    };
    /** A chapter's Markdown as blocks (the few things the manual uses). */
    std::vector<Block> parse (const juce::String& markdown);
    /** The inline parts of a line: **bold**, *italic* and `keys`, as runs. */
    struct Run { juce::String text; bool bold = false, italic = false, code = false; };
    std::vector<Run> inlineRuns (const juce::String& line);
    /** How often the words appear in a chapter (case ignored); 0 when not all of them do. */
    int matches (const Chapter& c, const juce::String& query);
}

/**
    The manual window (Help > Manual, F1): tabs for its parts across the top, the chapters of the tab on the left
    (or what a search found), and the chapter on the right, with its pictures. Open PDF opens the same manual as a PDF.
*/
class ManualPanel final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    ManualPanel();
    ~ManualPanel() override;

    /** Shows a chapter (by its title, or the first one that contains the words). */
    void showChapter (const juce::String& title);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Page;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int row) override;

    void selectTab (int index);
    void refreshList();
    void applySearch();
    void open (int chapterIndex);

    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextEditor search;
    juce::TextButton pdfButton { "Open PDF" };
    juce::ListBox list { "Chapters", this };
    juce::Viewport viewport;
    std::unique_ptr<Page> page;

    int tab = 0;
    int current = -1;
    std::vector<int> rows;   // the chapters in the list
    juce::String query;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ManualPanel)
};

} // namespace riff
