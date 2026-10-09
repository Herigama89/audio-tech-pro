#include "LibraryPanel.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../AppInfo.h"

namespace riff
{

juce::File LibraryPanel::patchesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (app::name).getChildFile ("Patches");
}

LibraryPanel::LibraryPanel (AudioEngine& e, juce::PropertiesFile& s) : engine (e), settings (s)
{
    for (auto* b : { &patchesTab, &loopsTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    patchesTab.onClick = [this] { showPage (Page::patches); };
    loopsTab.onClick = [this] { showPage (Page::loops); };

    look::key (closeButton, look::Key::dark);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    savePatchButton.onClick = [this]
    {
        auto* window = new juce::AlertWindow ("Save Track as Patch", "A name for this sound:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", {});
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty() && onSavePatch)
            {
                onSavePatch (name);
                rescan();
            }
        }), true);
    };
    addChildComponent (savePatchButton);

    loopFolder = juce::File (settings.getValue ("loopFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    folderButton.onClick = [this] { chooseFolder(); };
    folderButton.setTooltip ("The folder of loops and samples to browse (its folders too)");
    addChildComponent (folderButton);

    stopButton.onClick = [this] { engine.stopPreview(); previewRow = -1; list.repaint(); };
    addChildComponent (stopButton);

    search.setTextToShowWhenEmpty ("Search", theme::dimText);
    search.onTextChange = [this] { filter(); };
    addChildComponent (search);

    list.setModel (this);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    addAndMakeVisible (list);

    showPage (Page::patches);
    startTimerHz (4);
}

LibraryPanel::~LibraryPanel()
{
    engine.stopPreview();
}

void LibraryPanel::showPage (Page newPage)
{
    page = newPage;
    patchesTab.setToggleState (page == Page::patches, juce::dontSendNotification);
    loopsTab.setToggleState (page == Page::loops, juce::dontSendNotification);
    savePatchButton.setVisible (page == Page::patches);
    for (auto* c : std::initializer_list<juce::Component*> { &folderButton, &search, &stopButton })
        c->setVisible (page == Page::loops);
    rescan();
    resized();
}

void LibraryPanel::rescan()
{
    if (page == Page::patches)
    {
        patches = patchesFolder().findChildFiles (juce::File::findFiles, false, "*" + patchExtension);
        std::sort (patches.begin(), patches.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    }
    else
    {
        folderButton.setButtonText (loopFolder.getFileName().isNotEmpty() ? loopFolder.getFileName() : loopFolder.getFullPathName());
        allLoops.clear();
        // Up to a few thousand files (a whole Music folder can be huge).
        for (const auto& entry : juce::RangedDirectoryIterator (loopFolder, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.caf",
                                                                juce::File::findFiles))
        {
            allLoops.add (entry.getFile());
            if (allLoops.size() >= 5000)
                break;
        }
        std::sort (allLoops.begin(), allLoops.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        filter();
        return;
    }
    list.updateContent();
    list.repaint();
}

void LibraryPanel::filter()
{
    const auto words = juce::StringArray::fromTokens (search.getText(), true);
    shownLoops.clear();
    for (const auto& f : allLoops)
    {
        bool all = true;
        for (const auto& w : words)
            all = all && f.getFileName().containsIgnoreCase (w);
        if (all)
            shownLoops.add (f);
    }
    previewRow = -1;
    list.updateContent();
    list.repaint();
}

void LibraryPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder of loops", loopFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& c)
    {
        if (c.getResult() == juce::File())
            return;
        loopFolder = c.getResult();
        settings.setValue ("loopFolder", loopFolder.getFullPathName());
        rescan();
    });
}

int LibraryPanel::getNumRows()
{
    return page == Page::patches ? patches.size() : shownLoops.size();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    g.fillAll (selected ? theme::selection : (row % 2 == 0 ? theme::laneA : theme::laneB));
    const auto& files = page == Page::patches ? patches : shownLoops;
    if (! juce::isPositiveAndBelow (row, files.size()))
        return;

    const auto playing = page == Page::loops && row == previewRow && engine.isPreviewing();
    g.setColour (playing ? theme::accent : theme::text);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ((playing ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") : juce::String()) + files[row].getFileNameWithoutExtension(),
                8, 0, w - 16, h, juce::Justification::centredLeft, true);

    if (page == Page::loops)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (files[row].getFileExtension().substring (1).toUpperCase(), w - 50, 0, 44, h, juce::Justification::centredRight, false);
    }
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (page == Page::patches)
    {
        if (juce::isPositiveAndBelow (row, patches.size()) && onLoadPatch)
            onLoadPatch (patches[row]);
        return;
    }

    // Click to hear it; again to stop.
    if (row == previewRow && engine.isPreviewing())
    {
        engine.stopPreview();
        previewRow = -1;
    }
    else if (juce::isPositiveAndBelow (row, shownLoops.size()) && engine.previewFile (shownLoops[row]))
    {
        previewRow = row;
    }
    list.repaint();
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (page == Page::loops && juce::isPositiveAndBelow (row, shownLoops.size()) && onAddFile)
    {
        engine.stopPreview();
        onAddFile (shownLoops[row]);
    }
}

juce::var LibraryPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (page != Page::loops || rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], shownLoops.size()))
        return {};
    return "file:" + shownLoops[rows[0]].getFullPathName();
}

void LibraryPanel::timerCallback()
{
    // The preview finished: drop the play mark.
    if (previewRow >= 0 && ! engine.isPreviewing())
    {
        previewRow = -1;
        list.repaint();
    }
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (juce::Colours::black);
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    if (page == Page::patches && patches.isEmpty())
        g.drawFittedText ("No patches yet. Select a track and click Save Track as Patch to keep its sound (instrument, plugins and channel strip).",
                          list.getBounds().reduced (12), juce::Justification::centredTop, 5);
    if (page == Page::loops)
        g.drawFittedText ("Click to hear, double-click or drag onto a track to add.", getLocalBounds().removeFromBottom (24).reduced (8, 0),
                          juce::Justification::centred, 1);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (26);
    closeButton.setBounds (top.removeFromRight (56));
    top.removeFromRight (6);
    patchesTab.setBounds (top.removeFromLeft (top.getWidth() / 2 - 2));
    top.removeFromLeft (4);
    loopsTab.setBounds (top);
    r.removeFromTop (6);

    if (page == Page::patches)
    {
        savePatchButton.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
    }
    else
    {
        auto row = r.removeFromTop (26);
        stopButton.setBounds (row.removeFromRight (50));
        row.removeFromRight (4);
        folderButton.setBounds (row);
        r.removeFromTop (6);
        search.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        r.removeFromBottom (20);
    }
    list.setBounds (r);
}

} // namespace riff
