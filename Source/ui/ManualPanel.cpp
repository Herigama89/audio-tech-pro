#include "ManualPanel.h"

namespace riff
{
namespace manual
{
juce::File folder()
{
    static const juce::File found = []
    {
        juce::Array<juce::File> places;
        const auto env = juce::SystemStats::getEnvironmentVariable ("AUDIOTECHPRO_MANUAL", {});
        if (env.isNotEmpty()) places.add (juce::File (env));
       #if JUCE_MAC
        places.add (juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/Manual"));
       #endif
        places.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Manual"));
        // (working on the app itself: the manual in the source folder, a few folders up from the program)
        auto up = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 7 && up != up.getParentDirectory(); ++i, up = up.getParentDirectory())
            places.add (up.getChildFile ("Resources/Manual"));
        for (const auto& f : places)
            if (f.getChildFile ("index.txt").existsAsFile())
                return f;
        return juce::File();
    }();
    return found;
}

const std::vector<Chapter>& chapters()
{
    static const std::vector<Chapter> list = []
    {
        std::vector<Chapter> l;
        const auto dir = folder();
        if (dir == juce::File()) return l;
        for (const auto& line : juce::StringArray::fromLines (dir.getChildFile ("index.txt").loadFileAsString()))
        {
            if (line.startsWith ("#")) continue;
            const auto f = juce::StringArray::fromTokens (line, "|", "");
            if (f.size() < 3) continue;
            const auto file = dir.getChildFile ("chapters").getChildFile (f[1]);
            l.push_back ({ f[0], f[2], file, file.loadFileAsString() });
        }
        return l;
    }();
    return list;
}

juce::StringArray tabs()
{
    juce::StringArray t;
    for (const auto& c : chapters())
        t.addIfNotAlreadyThere (c.tab);
    return t;
}

juce::File pdf() { return folder().getChildFile ("Audio Tech Pro Manual.pdf"); }

std::vector<Block> parse (const juce::String& markdown)
{
    std::vector<Block> blocks;
    juce::String paragraph;
    const auto flush = [&]
    {
        if (paragraph.trim().isNotEmpty())
            blocks.push_back ({ Block::Kind::paragraph, paragraph.trim() });
        paragraph.clear();
    };
    bool inCode = false;
    juce::String code;
    for (const auto& raw : juce::StringArray::fromLines (markdown))
    {
        if (raw.trimStart().startsWith ("```"))
        {
            if (inCode) { blocks.push_back ({ Block::Kind::code, code.trimEnd() }); code.clear(); }
            else flush();
            inCode = ! inCode;
            continue;
        }
        if (inCode) { code << raw << "\n"; continue; }

        const auto line = raw.trimEnd();
        const auto t = line.trimStart();
        const int indent = (line.length() - t.length()) / 2;
        if (t.isEmpty()) { flush(); continue; }
        if (t.startsWith ("### ")) { flush(); blocks.push_back ({ Block::Kind::h3, t.substring (4) }); continue; }
        if (t.startsWith ("## "))  { flush(); blocks.push_back ({ Block::Kind::h2, t.substring (3) }); continue; }
        if (t.startsWith ("# "))   { flush(); blocks.push_back ({ Block::Kind::h1, t.substring (2) }); continue; }
        if (t.startsWith ("![") && t.contains ("](") && t.endsWith (")"))
        {
            flush();
            Block b { Block::Kind::image, t.fromFirstOccurrenceOf ("![", false, false).upToFirstOccurrenceOf ("](", false, false) };
            b.image = t.fromFirstOccurrenceOf ("](", false, false).dropLastCharacters (1);
            blocks.push_back (b);
            continue;
        }
        if (t.startsWith ("> ")) { flush(); blocks.push_back ({ Block::Kind::quote, t.substring (2) }); continue; }
        if (t.startsWith ("- ") || t.startsWith ("* "))
        {
            flush();
            Block b { Block::Kind::bullet, t.substring (2) };
            b.level = indent;
            blocks.push_back (b);
            continue;
        }
        if (const auto dot = t.indexOf (". "); dot > 0 && dot <= 3 && t.substring (0, dot).containsOnly ("0123456789"))
        {
            flush();
            Block b { Block::Kind::number, t.substring (dot + 2) };
            b.number = t.substring (0, dot).getIntValue();
            b.level = indent;
            blocks.push_back (b);
            continue;
        }
        // A line that carries on a list item or tip
        if (paragraph.isEmpty() && ! blocks.empty() && indent > 0
            && (blocks.back().kind == Block::Kind::bullet || blocks.back().kind == Block::Kind::number))
        {
            blocks.back().text << " " << t;
            continue;
        }
        paragraph << (paragraph.isEmpty() ? "" : " ") << t;
    }
    flush();
    if (inCode && code.isNotEmpty())
        blocks.push_back ({ Block::Kind::code, code.trimEnd() });
    return blocks;
}

std::vector<Run> inlineRuns (const juce::String& line)
{
    std::vector<Run> runs;
    Run current;
    bool bold = false, italic = false, code = false;
    const auto push = [&]
    {
        if (current.text.isNotEmpty()) runs.push_back (current);
        current = {};
        current.bold = bold; current.italic = italic; current.code = code;
    };
    for (int i = 0; i < line.length(); ++i)
    {
        const auto c = line[i];
        if (c == '`') { push(); code = ! code; current.code = code; continue; }
        if (! code && c == '*' && i + 1 < line.length() && line[i + 1] == '*') { push(); bold = ! bold; current.bold = bold; ++i; continue; }
        if (! code && c == '*' && (italic || (i + 1 < line.length() && line[i + 1] != ' '))) { push(); italic = ! italic; current.italic = italic; continue; }
        current.text << juce::String::charToString (c);
    }
    push();
    return runs;
}

int matches (const Chapter& c, const juce::String& query)
{
    int total = 0;
    const auto text = c.title + "\n" + c.text;
    // The words together count most
    if (query.trim().containsChar (' '))
        for (int at = text.indexOfIgnoreCase (query.trim()); at >= 0; at = text.indexOfIgnoreCase (at + 1, query.trim()))
            total += 100;
    for (const auto& word : juce::StringArray::fromTokens (query, " ", "\""))
    {
        if (word.length() < 2) continue;
        int n = 0;
        for (int at = text.indexOfIgnoreCase (word); at >= 0; at = text.indexOfIgnoreCase (at + word.length(), word))
            ++n;
        if (n == 0) return 0;
        total += n + (c.title.containsIgnoreCase (word) ? 50 : 0);
    }
    return total;
}
} // namespace manual

//==============================================================================
namespace
{
    namespace col
    {
        const juce::Colour paper  { 0xfff6f6f7 };
        const juce::Colour ink    { 0xff1d1f24 };
        const juce::Colour dim    { 0xff62666e };
        const juce::Colour accent { 0xffe0701a };
        const juce::Colour codeBg { 0xffe7e8eb };
        const juce::Colour tipBg  { 0xfffff1e2 };
        const juce::Colour found  { 0xffffe066 };
        const juce::Colour chrome { 0xff1f2124 };
        const juce::Colour side   { 0xff26282c };
    }

    juce::Font font (float size, bool bold = false, bool italic = false, bool mono = false)
    {
        auto opts = mono ? juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)
                         : juce::FontOptions (size);
        juce::Font f (opts);
        if (bold) f = f.boldened();
        if (italic) f = f.italicised();
        return f;
    }
}

/** The chapter, drawn: its blocks laid out for the width, pictures scaled to fit. */
class ManualPanel::Page final : public juce::Component
{
public:
    void setChapter (const manual::Chapter* c, const juce::String& highlight)
    {
        chapter = c;
        words = juce::StringArray::fromTokens (highlight, " ", "\"");
        words.removeEmptyStrings();
        if (words.size() > 1)
            words.insert (0, highlight.trim());   // (the phrase first: shown whole, and looked for first)
        blocks = c != nullptr ? manual::parse (c->text) : std::vector<manual::Block>();
        images.clear();
        for (const auto& b : blocks)
            images.push_back (b.kind == manual::Block::Kind::image
                                ? juce::ImageCache::getFromFile (manual::folder().getChildFile (b.image)) : juce::Image());
        layoutFor (getWidth());
    }

    /** Lays the blocks out for a width; sets the page's height. Returns the y of the first block with a found word. */
    int layoutFor (int width)
    {
        layouts.clear();
        tops.clear();
        heights.clear();
        firstFound = -1;
        bool phraseFound = false;
        const float margin = 34.0f, textWidth = juce::jmax (200.0f, (float) width - 2.0f * margin);
        float y = 26.0f;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& b = blocks[i];
            using K = manual::Block::Kind;
            juce::TextLayout layout;
            float h = 0.0f, gapBefore = 0.0f, gapAfter = 10.0f;
            const float indent = (b.kind == K::bullet || b.kind == K::number) ? 22.0f + 20.0f * (float) b.level
                               : (b.kind == K::quote || b.kind == K::code) ? 14.0f : 0.0f;
            if (b.kind == K::image)
            {
                const auto& img = images[i];
                if (img.isValid())
                {
                    const float w = juce::jmin (textWidth, (float) img.getWidth());
                    h = w * (float) img.getHeight() / (float) img.getWidth() + 22.0f;
                }
                gapBefore = 4.0f; gapAfter = 16.0f;
            }
            else
            {
                const float size = b.kind == K::h1 ? 27.0f : b.kind == K::h2 ? 20.0f : b.kind == K::h3 ? 16.5f : b.kind == K::code ? 13.5f : 15.0f;
                const bool heading = b.kind == K::h1 || b.kind == K::h2 || b.kind == K::h3;
                juce::AttributedString s;
                s.setWordWrap (juce::AttributedString::byWord);
                s.setLineSpacing (b.kind == K::code ? 2.0f : 4.0f);
                if (b.kind == K::code)
                    addText (s, b.text, font (size, false, false, true), col::ink);
                else
                    for (const auto& r : manual::inlineRuns (b.text))
                        addText (s, r.text, r.code ? font (size - 1.0f, true, false, true) : font (size, heading || r.bold, r.italic),
                                 heading && b.kind != K::h3 ? col::ink : r.code ? juce::Colour (0xff8a3d00) : col::ink);
                layout.createLayout (s, textWidth - indent - (b.kind == K::quote || b.kind == K::code ? 14.0f : 0.0f));
                h = layout.getHeight() + (b.kind == K::quote || b.kind == K::code ? 20.0f : 0.0f);
                gapBefore = b.kind == K::h1 ? 0.0f : b.kind == K::h2 ? 18.0f : b.kind == K::h3 ? 10.0f : 0.0f;
                if ((b.kind == K::paragraph || b.kind == K::quote) && i > 0
                    && (blocks[i - 1].kind == K::bullet || blocks[i - 1].kind == K::number))
                    gapBefore = 8.0f;   // (after a list)
                gapAfter = b.kind == K::h1 ? 18.0f : heading ? 8.0f : (b.kind == K::bullet || b.kind == K::number) ? 5.0f : 12.0f;
            }
            y += gapBefore;
            tops.push_back (y);
            heights.push_back (h);
            layouts.push_back (std::move (layout));
            if (! words.isEmpty() && b.text.containsIgnoreCase (words[0]) && (firstFound < 0 || ! phraseFound))
            {
                if (firstFound < 0 || ! phraseFound) firstFound = (int) y;
                phraseFound = true;
            }
            if (firstFound < 0 && ! words.isEmpty())
                for (const auto& w : words)
                    if (w.length() >= 2 && b.text.containsIgnoreCase (w)) { firstFound = (int) y; break; }
            y += h + gapAfter;
        }
        setSize (width, (int) y + 40);
        return firstFound;
    }

    int foundAt() const noexcept { return firstFound; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::paper);
        if (chapter == nullptr)
        {
            g.setColour (col::dim);
            g.setFont (font (16.0f));
            g.drawFittedText ("The manual isn't installed with this copy of the app.", getLocalBounds().reduced (30), juce::Justification::centredTop, 3);
            return;
        }
        const float margin = 34.0f, textWidth = juce::jmax (200.0f, (float) getWidth() - 2.0f * margin);
        const auto clip = g.getClipBounds();
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& b = blocks[i];
            const float top = tops[i], h = heights[i];
            if (top > (float) clip.getBottom() || top + h < (float) clip.getY())
                continue;
            using K = manual::Block::Kind;
            const float indent = (b.kind == K::bullet || b.kind == K::number) ? 22.0f + 20.0f * (float) b.level : 0.0f;
            if (b.kind == K::image)
            {
                const auto& img = images[i];
                if (! img.isValid()) continue;
                const float w = juce::jmin (textWidth, (float) img.getWidth());
                const juce::Rectangle<float> r (margin + (textWidth - w) * 0.5f, top, w, h - 22.0f);
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.fillRect (r.translated (2.0f, 3.0f));
                g.setOpacity (1.0f);
                g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
                g.setColour (col::dim);
                g.setFont (font (12.5f, false, true));
                g.drawText (b.text, juce::Rectangle<float> (margin, r.getBottom() + 3.0f, textWidth, 18.0f), juce::Justification::centred);
                continue;
            }
            auto area = juce::Rectangle<float> (margin + indent, top, textWidth - indent, h);
            if (b.kind == K::quote || b.kind == K::code)
            {
                g.setColour (b.kind == K::quote ? col::tipBg : col::codeBg);
                g.fillRoundedRectangle (area, 5.0f);
                if (b.kind == K::quote)
                {
                    g.setColour (col::accent);
                    g.fillRect (area.withWidth (4.0f));
                }
                area = area.reduced (14.0f, 10.0f);
            }
            if (b.kind == K::bullet)
            {
                g.setColour (b.level > 0 ? col::dim : col::accent);
                g.fillEllipse (margin + indent - 14.0f, top + 7.5f, 6.0f, 6.0f);
            }
            if (b.kind == K::number)
            {
                // a numbered dot, as on the pictures
                const juce::Rectangle<float> dot (margin + indent - 22.0f, top + 0.5f, 19.0f, 19.0f);
                g.setColour (col::accent);
                g.fillEllipse (dot);
                g.setColour (juce::Colours::white);
                g.setFont (font (11.5f, true));
                g.drawText (juce::String (b.number), dot, juce::Justification::centred);
            }
            layouts[i].draw (g, area);
            if (b.kind == K::h1)
            {
                g.setColour (col::accent);
                g.fillRect (margin, top + h + 5.0f, textWidth, 3.0f);
            }
        }
    }

private:
    void addText (juce::AttributedString& s, const juce::String& text, const juce::Font& f, juce::Colour colour)
    {
        // Found words stand out (darker, on yellow is beyond a text layout: in the accent colour, bold)
        if (words.isEmpty()) { s.append (text, f, colour); return; }
        int at = 0;
        while (at < text.length())
        {
            int best = -1, len = 0;
            for (const auto& w : words)
                if (w.length() >= 2)
                    if (const auto i = text.indexOfIgnoreCase (at, w); i >= 0 && (best < 0 || i < best)) { best = i; len = w.length(); }
            if (best < 0) { s.append (text.substring (at), f, colour); break; }
            if (best > at) s.append (text.substring (at, best), f, colour);
            s.append (text.substring (best, best + len), f.boldened(), col::accent);
            at = best + len;
        }
    }

    const manual::Chapter* chapter = nullptr;
    std::vector<manual::Block> blocks;
    std::vector<juce::Image> images;
    std::vector<juce::TextLayout> layouts;
    std::vector<float> tops, heights;
    juce::StringArray words;
    int firstFound = -1;
};

//==============================================================================
ManualPanel::ManualPanel() : page (std::make_unique<Page>())
{
    for (const auto& t : manual::tabs())
    {
        auto* b = tabButtons.add (new juce::TextButton (t));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x4d41);
        b->setColour (juce::TextButton::buttonColourId, col::side);
        b->setColour (juce::TextButton::buttonOnColourId, col::accent);
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd8d9dc));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setWantsKeyboardFocus (false);
        const int index = tabButtons.size() - 1;
        b->onClick = [this, index] { if (tabButtons[index]->getToggleState()) selectTab (index); };
        addAndMakeVisible (b);
    }

    search.setTextToShowWhenEmpty ("Search the manual...", juce::Colour (0xff8a8d93));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff121315));
    search.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3d42));
    search.setFont (font (15.0f));
    search.onTextChange = [this] { applySearch(); };
    search.onEscapeKey = [this] { search.clear(); query.clear(); refreshList(); };
    addAndMakeVisible (search);

    pdfButton.setTooltip ("The same manual as a PDF, to print or read on another device");
    pdfButton.setColour (juce::TextButton::buttonColourId, col::side);
    pdfButton.onClick = [] { if (manual::pdf().existsAsFile()) manual::pdf().startAsProcess(); };
    pdfButton.setEnabled (manual::pdf().existsAsFile());
    addAndMakeVisible (pdfButton);

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, col::side);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    viewport.setViewedComponent (page.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (12);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xffb4b6bb));
    addAndMakeVisible (viewport);

    setSize (1180, 820);
    if (! tabButtons.isEmpty())
    {
        tabButtons[0]->setToggleState (true, juce::dontSendNotification);
        selectTab (0);
    }
    else
    {
        page->setChapter (nullptr, {});
    }
}

ManualPanel::~ManualPanel() = default;

void ManualPanel::showChapter (const juce::String& title)
{
    const auto& all = manual::chapters();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].title.equalsIgnoreCase (title))
        {
            search.clear();
            query.clear();
            const auto t = manual::tabs().indexOf (all[i].tab);
            if (auto* b = tabButtons[t]) b->setToggleState (true, juce::dontSendNotification);
            tab = t;
            refreshList();
            open ((int) i);
            return;
        }
    search.setText (title, false);
    applySearch();
}

void ManualPanel::applySearch()
{
    query = search.getText().trim();
    refreshList();
    if (! rows.empty()) open (rows.front());
    repaint();
}

void ManualPanel::selectTab (int index)
{
    tab = index;
    search.clear();
    query.clear();
    refreshList();
    if (! rows.empty()) open (rows.front());
}

void ManualPanel::refreshList()
{
    rows.clear();
    const auto& all = manual::chapters();
    const auto tabName = manual::tabs()[tab];
    if (query.length() >= 2)
    {
        std::vector<std::pair<int, int>> found;
        for (size_t i = 0; i < all.size(); ++i)
            if (const auto n = manual::matches (all[i], query); n > 0)
                found.push_back ({ n, (int) i });
        std::stable_sort (found.begin(), found.end(), [] (auto a, auto b) { return a.first > b.first; });
        for (const auto& f : found) rows.push_back (f.second);
    }
    else
    {
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].tab == tabName) rows.push_back ((int) i);
    }
    list.updateContent();
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r] == current) { list.selectRow ((int) r, true, true); break; }
    list.repaint();
}

void ManualPanel::open (int chapterIndex)
{
    const auto& all = manual::chapters();
    if (chapterIndex < 0 || chapterIndex >= (int) all.size()) return;
    current = chapterIndex;
    page->setChapter (&all[(size_t) chapterIndex], query);
    page->layoutFor (viewport.getMaximumVisibleWidth());
    const auto y = page->foundAt();
    viewport.setViewPosition (0, y > 0 && query.isNotEmpty() ? juce::jmax (0, y - 40) : 0);
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r] == current) list.selectRow ((int) r, true, true);
    list.repaint();
}

int ManualPanel::getNumRows() { return (int) rows.size(); }

void ManualPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& c = manual::chapters()[(size_t) rows[(size_t) row]];
    const bool on = rows[(size_t) row] == current;
    if (on)
    {
        g.setColour (col::accent.withAlpha (0.22f));
        g.fillRect (0, 0, width, height);
        g.setColour (col::accent);
        g.fillRect (0, 0, 3, height);
    }
    g.setColour (on ? juce::Colours::white : juce::Colour (0xffd0d2d6));
    g.setFont (font (14.0f, on));
    auto r = juce::Rectangle<int> (12, 0, width - 18, height);
    if (query.length() >= 2)
    {
        g.setColour (juce::Colour (0xff8a8d93));
        g.setFont (font (11.0f));
        g.drawText (c.tab, r.removeFromRight (80), juce::Justification::centredRight);
        g.setColour (on ? juce::Colours::white : juce::Colour (0xffd0d2d6));
        g.setFont (font (14.0f, on));
    }
    g.drawText (c.title, r, juce::Justification::centredLeft, true);
}

void ManualPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size()) open (rows[(size_t) row]);
}

void ManualPanel::selectedRowsChanged (int row)
{
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row] != current) open (rows[(size_t) row]);
}

void ManualPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::chrome);
    g.setColour (col::side);
    g.fillRect (0, 52, 250, getHeight() - 52);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (0, 51, getWidth(), 1);
    g.setColour (juce::Colour (0xff8a8d93));
    g.setFont (font (11.5f, true));
    g.drawText (query.length() >= 2 ? juce::String ((int) rows.size()) + (rows.size() == 1 ? " CHAPTER FOUND" : " CHAPTERS FOUND")
                                    : manual::tabs()[tab].toUpperCase(),
                14, 60, 230, 20, juce::Justification::centredLeft);
}

void ManualPanel::resized()
{
    auto top = getLocalBounds().removeFromTop (52).reduced (10, 10);
    pdfButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    search.setBounds (top.removeFromRight (juce::jmin (260, top.getWidth() / 3)));
    top.removeFromRight (12);
    const int n = tabButtons.size();
    for (int i = 0; i < n; ++i)
        tabButtons[i]->setBounds (top.removeFromLeft (top.getWidth() / (n - i)).reduced (2, 0));

    auto body = getLocalBounds().withTrimmedTop (52);
    list.setBounds (body.removeFromLeft (250).withTrimmedTop (32));
    viewport.setBounds (body);
    const auto pos = viewport.getViewPositionY();
    page->layoutFor (viewport.getMaximumVisibleWidth());
    viewport.setViewPosition (0, pos);
}

} // namespace riff
