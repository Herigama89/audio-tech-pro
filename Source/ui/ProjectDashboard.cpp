#include "ProjectDashboard.h"
#include "Theme.h"
#include "../model/StarterTemplates.h"
#include "../model/Project.h"
#include "../model/Identifiers.h"

namespace riff
{
namespace
{
    juce::Colour line()     { return theme::text.withAlpha (0.11f); }
    juce::Colour fill()     { return theme::text.withAlpha (0.045f); }
    juce::Colour onAccent() { return theme::accent.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff1a1206) : juce::Colours::white; }

    const double rates[] { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    const int bitDepths[] { 16, 24, 32, 64 };

    juce::String rateText (double r)
    {
        const auto k = r / 1000.0;
        return (std::abs (k - std::round (k)) < 0.01 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + " kHz";
    }
    juce::String bitsText (int b) { return b == 32 ? "32-bit float" : b == 64 ? "64-bit float" : juce::String (b) + "-bit"; }

    void styleButton (juce::TextButton& b, bool primary)
    {
        b.setColour (juce::TextButton::buttonColourId, primary ? theme::accent : fill());
        b.setColour (juce::TextButton::textColourOffId, primary ? onAccent() : theme::text);
        b.setColour (juce::ComboBox::outlineColourId, line());
    }
}

//==============================================================================
/** A start: a little picture of its tracks (a bar each, in their colours), its name and what's in it. */
class ProjectDashboard::Card final : public juce::Component
{
public:
    Card (juce::String id_, juce::File file_, juce::String name_, juce::String description_, std::vector<juce::Colour> lanes_,
          std::function<void (Card*)> chosen_, std::function<void()> twice_)
        : id (std::move (id_)), file (std::move (file_)), name (std::move (name_)), description (std::move (description_)),
          lanes (std::move (lanes_)), chosen (std::move (chosen_)), twice (std::move (twice_)) {}

    juce::String id;
    juce::File file;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver() ? theme::text.withAlpha (0.08f) : fill());
        g.fillRoundedRectangle (r, 11.0f);
        g.setColour (selected ? theme::accent : line());
        g.drawRoundedRectangle (r, 11.0f, selected ? 2.0f : 1.0f);
        auto inner = r.reduced (11.0f, 10.0f);
        auto top = inner.removeFromTop (inner.getHeight() - 40.0f);
        const auto shown = (int) std::min<size_t> (lanes.size(), 5);
        for (int i = 0; i < shown; ++i)
        {
            const auto w = top.getWidth() * (0.45f + 0.5f * (float) ((i * 37 + name.length() * 13) % 100) / 100.0f);
            g.setColour (lanes[(size_t) i].withAlpha (0.8f));
            g.fillRoundedRectangle (top.getX(), top.getY() + (float) i * 11.0f, w, 7.0f, 3.0f);
        }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (name, inner.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (description, inner.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) chosen (this); }
    void mouseDoubleClick (const juce::MouseEvent&) override { chosen (this); twice(); }

private:
    juce::String name, description;
    std::vector<juce::Colour> lanes;
    std::function<void (Card*)> chosen;
    std::function<void()> twice;
};

//==============================================================================
/** A recent project: its name, when it was saved, its rate and bit depth; tracks, tempo and key on the right. */
class ProjectDashboard::RecentRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    RecentRow (const juce::File& f, std::function<void (const juce::File&)> open_) : file (f), open (std::move (open_))
    {
        juce::String error;
        const auto p = model::load (file, error);
        const auto modified = file.getLastModificationTime();
        const auto days = (juce::Time::getCurrentTime() - modified).inDays();
        const auto when = days < 1 ? "Today " + modified.toString (false, true, false)
                        : days < 2 ? juce::String ("Yesterday") : modified.formatted ("%b %d, %Y");
        sub = when;
        if (p.isValid())
        {
            if (p.hasProperty (ids::projectSampleRate))
                sub << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << rateText ((double) p[ids::projectSampleRate]) << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << bitsText ((int) p.getProperty (ids::projectBitDepth, 24));
            int tracks = 0;
            for (const auto& t : p)
                tracks += t.hasType (ids::TRACK) ? 1 : 0;
            meta << tracks << (tracks == 1 ? " track" : " tracks") << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << juce::String ((double) p.getProperty (ids::tempo, 120.0), 0) << " BPM";
            if (p.hasProperty (ids::keyRoot))
                meta << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << keyNames()[(int) p[ids::keyRoot] * 2 + ((bool) p[ids::keyMinor] ? 1 : 0)];
        }
        else
            meta = "Couldn't read it";
        setTooltip (file.getFullPathName());
    }

    juce::File file;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver() ? theme::text.withAlpha (0.08f) : fill());
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (line());
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        auto inner = r.reduced (12.0f, 8.0f);
        auto thumb = inner.removeFromLeft (60.0f).withSizeKeepingCentre (60.0f, 36.0f);
        g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.45f), thumb.getTopLeft(), theme::locators.withAlpha (0.35f), thumb.getBottomRight(), false));
        g.fillRoundedRectangle (thumb, 6.0f);
        inner.removeFromLeft (12.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (meta, inner, juce::Justification::centredRight, true);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (file.getFileNameWithoutExtension(), inner.withTrimmedBottom (inner.getHeight() * 0.5f), juce::Justification::bottomLeft, true);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (sub, inner.withTrimmedTop (inner.getHeight() * 0.5f + 2.0f), juce::Justification::topLeft, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) open (file); }

private:
    juce::String sub, meta;
    std::function<void (const juce::File&)> open;
};

//==============================================================================
/** Three keys side by side, one lit: WAV | AIFF | FLAC */
class ProjectDashboard::Segmented final : public juce::Component
{
public:
    Segmented (juce::StringArray labels_, juce::StringArray values_, std::function<void (const juce::String&)> changed_)
        : labels (std::move (labels_)), values (std::move (values_)), changed (std::move (changed_)) {}
    juce::String value;
    void set (const juce::String& v) { value = v; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (line());
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        const auto w = r.getWidth() / (float) labels.size();
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight());
            const bool on = values[i] == value;
            if (on)
            {
                g.setColour (theme::accent);
                g.fillRoundedRectangle (cell.reduced (2.0f), 5.5f);
            }
            g.setColour (on ? onAccent() : theme::dimText);
            g.setFont (juce::FontOptions (12.0f, on ? juce::Font::bold : juce::Font::plain));
            g.drawText (labels[i], cell, juce::Justification::centred);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
        set (values[i]);
        changed (value);
    }
private:
    juce::StringArray labels, values;
    std::function<void (const juce::String&)> changed;
};

//==============================================================================
juce::StringArray ProjectDashboard::keyNames()
{
    juce::StringArray names;
    for (const auto* n : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
    {
        names.add (juce::String (n) + " Major");
        names.add (juce::String (n) + " Minor");
    }
    return names;
}

ProjectDashboard::ProjectDashboard (Settings defaults, juce::StringArray devices, juce::Array<juce::File> recent,
                                    juce::File templates, bool showAtStartup, Callbacks cb)
    : settings (std::move (defaults)), callbacks (std::move (cb)), templatesFolder (std::move (templates))
{
    setLookAndFeel (&look);
    for (auto* tab : { &createTab, &recentTabButton })
        addAndMakeVisible (tab);
    createTab.onClick = [this] { showRecent (false); };
    recentTabButton.onClick = [this] { showRecent (true); };

    // The starts, by section, and the user's own templates
    auto onChoose = [this] (Card* c) { choose (c); };
    auto onTwice = [this] { createNow(); };
    for (const auto& s : starters::all())
        cards.add (new Card (s.id, {}, s.name, s.description, s.lanes, onChoose, onTwice));
    sections = starters::categories();
    const auto mine = templatesFolder.findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension);
    for (const auto& f : mine)
        cards.add (new Card ({}, f, f.getFileNameWithoutExtension(), "Your template", { theme::accent, theme::locators, theme::meterGreen }, onChoose, onTwice));
    sections.add ("My Templates");
    for (auto* c : cards)
        cardHolder.addAndMakeVisible (c);
    for (const auto& s : sections)
    {
        auto* b = sectionButtons.add (new juce::TextButton (s));
        b->getProperties().set ("list", true);
        b->onClick = [this, s, b]
        {
            showRecent (false);
            for (auto* other : sectionButtons)
                other->setToggleState (other == b, juce::dontSendNotification);
            for (const auto& [name, y] : sectionTops)
                if (name == s)
                    cardView.setViewPosition (0, y);
        };
        addAndMakeVisible (b);
    }
    cardHolder.painter = [this] (juce::Graphics& g)
    {
        for (const auto& [name, y] : sectionTops)
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (10.5f).withKerningFactor (0.12f));
            g.drawText (name.toUpperCase(), 0, y, 300, 22, juce::Justification::centredLeft);
            if (name == "My Templates")
            {
                bool any = false;
                for (auto* c : cards)
                    any = any || c->id.isEmpty();
                if (! any)
                {
                    g.setFont (juce::FontOptions (12.0f));
                    g.drawText ("File > Save as Template puts your own here.", 0, y + 26, 400, 20, juce::Justification::centredLeft);
                }
            }
        }
    };
    cardView.setViewedComponent (&cardHolder, false);
    cardView.setScrollBarsShown (true, false);
    addAndMakeVisible (cardView);

    for (const auto& f : recent)
        recentHolder.addAndMakeVisible (recentRows.add (new RecentRow (f, [this] (const juce::File& file) { callbacks.openRecent (file); })));
    recentView.setViewedComponent (&recentHolder, false);
    recentView.setScrollBarsShown (true, false);
    addChildComponent (recentView);

    // The project's settings
    nameBox.setText (settings.name, false);
    nameBox.onTextChange = [this] { settings.name = nameBox.getText().trim(); };
    tempoBox.setText (juce::String (settings.tempo, 2), false);
    tempoBox.setInputRestrictions (7, "0123456789.");
    tempoBox.onTextChange = [this] { settings.tempo = juce::jlimit (20.0, 400.0, tempoBox.getText().getDoubleValue()); };
    for (auto* box : { &nameBox, &tempoBox })
    {
        box->setColour (juce::TextEditor::backgroundColourId, theme::text.withAlpha (0.06f));
        box->setColour (juce::TextEditor::outlineColourId, line());
        box->setColour (juce::TextEditor::textColourId, theme::text);
        box->setFont (juce::FontOptions (13.0f));
        box->setJustification (juce::Justification::centredLeft);
        box->setIndents (8, 0);
        addAndMakeVisible (box);
    }
    folderButton.onClick = [this] { chooseFolder(); };
    tapButton.onClick = [this] { tap(); };
    for (auto* b : { &folderButton, &tapButton })
    {
        styleButton (*b, false);
        addAndMakeVisible (b);
    }
    tapButton.setColour (juce::TextButton::textColourOffId, theme::accent);
    tapButton.getProperties().set ("ownText", true);
    updateFolderButton();

    int i = 1;
    for (auto r : rates)
        rateBox.addItem (rateText (r), i++);
    i = 1;
    for (auto b : bitDepths)
        bitsBox.addItem (bitsText (b), i++);
    deviceBox.addItem ("Keep the one in use", 1);
    for (int d = 0; d < devices.size(); ++d)
        deviceBox.addItem (devices[d], d + 2);
    keyBox.addItemList (keyNames(), 1);
    for (int n : { 2, 3, 4, 5, 6, 7, 9, 12 })
        sigBox.addItem (juce::String (n) + " / 4", n);
    countInBox.addItemList ({ "Off", "1 bar", "2 bars" }, 1);

    for (int k = 0; k < (int) std::size (rates); ++k)
        if (std::abs (rates[k] - settings.sampleRate) < 1.0)
            rateBox.setSelectedId (k + 1, juce::dontSendNotification);
    if (rateBox.getSelectedId() == 0)
        rateBox.setSelectedId (2, juce::dontSendNotification);
    for (int k = 0; k < 4; ++k)
        if (bitDepths[k] == settings.bitDepth)
            bitsBox.setSelectedId (k + 1, juce::dontSendNotification);
    if (bitsBox.getSelectedId() == 0)
        bitsBox.setSelectedId (3, juce::dontSendNotification);
    deviceBox.setSelectedId (juce::jmax (1, devices.indexOf (settings.device) + 2), juce::dontSendNotification);
    keyBox.setSelectedId (settings.keyRoot * 2 + (settings.keyMinor ? 1 : 0) + 1, juce::dontSendNotification);
    sigBox.setSelectedId (settings.timeSig, juce::dontSendNotification);
    countInBox.setSelectedId (settings.countInBars + 1, juce::dontSendNotification);

    rateBox.onChange = [this] { settings.sampleRate = rates[(size_t) juce::jlimit (0, 7, rateBox.getSelectedId() - 1)]; };
    bitsBox.onChange = [this] { settings.bitDepth = bitDepths[(size_t) juce::jlimit (0, 3, bitsBox.getSelectedId() - 1)]; updateFileTypeLimits(); };
    deviceBox.onChange = [this] { settings.device = deviceBox.getSelectedId() > 1 ? deviceBox.getText() : juce::String(); };
    keyBox.onChange = [this] { settings.keyRoot = (keyBox.getSelectedId() - 1) / 2; settings.keyMinor = ((keyBox.getSelectedId() - 1) % 2) == 1; };
    sigBox.onChange = [this] { settings.timeSig = sigBox.getSelectedId(); };
    countInBox.onChange = [this] { settings.countInBars = countInBox.getSelectedId() - 1; };
    for (auto* box : { &rateBox, &bitsBox, &deviceBox, &keyBox, &sigBox, &countInBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, theme::text.withAlpha (0.06f));
        box->setColour (juce::ComboBox::outlineColourId, line());
        box->setColour (juce::ComboBox::textColourId, theme::text);
        addAndMakeVisible (box);
    }

    fileType = std::make_unique<Segmented> (juce::StringArray { "WAV", "AIFF", "FLAC" }, juce::StringArray { "wav", "aiff", "flac" },
                                            [this] (const juce::String& v) { settings.fileType = v; updateFileTypeLimits(); });
    fileType->set (settings.fileType);
    addAndMakeVisible (*fileType);

    note.setColour (juce::Label::textColourId, theme::dimText);
    note.setFont (juce::FontOptions (11.5f));
    note.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (note);
    updateFileTypeLimits();

    showAtStartupToggle.setToggleState (showAtStartup, juce::dontSendNotification);
    showAtStartupToggle.setColour (juce::ToggleButton::textColourId, theme::text);
    showAtStartupToggle.setColour (juce::ToggleButton::tickColourId, theme::accent);
    showAtStartupToggle.onClick = [this] { if (callbacks.setShowAtStartup) callbacks.setShowAtStartup (showAtStartupToggle.getToggleState()); };
    addAndMakeVisible (showAtStartupToggle);

    styleButton (openButton, false);
    styleButton (cancelButton, false);
    styleButton (createButton, true);
    createButton.getProperties().set ("primary", true);
    openButton.onClick = [this] { callbacks.openOther(); };
    cancelButton.onClick = [this] { callbacks.cancel(); };
    createButton.onClick = [this] { createNow(); };
    for (auto* b : { &openButton, &cancelButton, &createButton })
        addAndMakeVisible (b);

    choose (cards.isEmpty() ? nullptr : cards[0]);
    for (auto* c : cards)
        if (c->id == settings.starter && settings.starter.isNotEmpty())
            choose (c);
    labels = { { "Name", &nameBox }, { "Location", &folderButton }, { "Sample rate", &rateBox }, { "Bit depth", &bitsBox },
               { "File type", fileType.get() }, { "Interface", &deviceBox }, { "Tempo", &tempoBox }, { "Key", &keyBox },
               { "Time signature", &sigBox }, { "Count-in", &countInBox } };
    showRecent (false);
    setSize (1120, 700);
}

ProjectDashboard::~ProjectDashboard()
{
    setLookAndFeel (nullptr);
}

void ProjectDashboard::showRecent (bool recent)
{
    recentTab = recent;
    cardView.setVisible (! recent);
    recentView.setVisible (recent);
    createTab.setToggleState (! recent, juce::dontSendNotification);
    recentTabButton.setToggleState (recent, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &nameBox, &tempoBox, &folderButton, &tapButton, &rateBox, &bitsBox, &deviceBox,
                                                             &keyBox, &sigBox, &countInBox, fileType.get(), &note, &createButton })
        c->setVisible (! recent);
    resized();
    repaint();
}

void ProjectDashboard::choose (Card* card)
{
    chosen = card;
    for (auto* c : cards)
    {
        c->selected = c == card;
        c->repaint();
    }
    if (card != nullptr)
    {
        settings.starter = card->id;
        settings.templateFile = card->file;
    }
}

void ProjectDashboard::updateFolderButton()
{
    if (settings.folder == juce::File())
        settings.folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Audio Tech Pro").getChildFile ("Projects");
    folderButton.setButtonText (settings.folder.getParentDirectory().getFileName() + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xb8 ")) + settings.folder.getFileName());
    folderButton.setTooltip (settings.folder.getFullPathName());
}

void ProjectDashboard::updateFileTypeLimits()
{
    // FLAC holds whole numbers only (16 or 24-bit); AIFF no 64-bit float
    juce::String text;
    if (settings.fileType == "flac" && settings.bitDepth > 24)
        text = "FLAC can't hold float audio: recordings will be 24-bit.";
    else if (settings.fileType == "aiff" && settings.bitDepth == 64)
        text = "AIFF can't hold 64-bit float: recordings will be 32-bit.";
    else if (settings.fileType == "aiff" && settings.bitDepth == 32)
        text = "AIFF records 32-bit as whole numbers (not float).";
    note.setText (text, juce::dontSendNotification);
}

void ProjectDashboard::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where to save the project", settings.folder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
    {
        if (const auto f = fc.getResult(); f.isDirectory())
        {
            settings.folder = f;
            updateFolderButton();
        }
    });
}

void ProjectDashboard::tap()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.0)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 8)
        taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const auto bpm = 60.0 * (double) (taps.size() - 1) / (taps.back() - taps.front());
        settings.tempo = juce::jlimit (20.0, 400.0, bpm);
        tempoBox.setText (juce::String (settings.tempo, 2), false);
    }
}

void ProjectDashboard::createNow()
{
    if (settings.name.isEmpty())
        settings.name = "Untitled";
    settings.tempo = juce::jlimit (20.0, 400.0, tempoBox.getText().getDoubleValue() > 0.0 ? tempoBox.getText().getDoubleValue() : 120.0);
    if (settings.fileType == "flac" && settings.bitDepth > 24)
        settings.bitDepth = 24;
    if (settings.fileType == "aiff" && settings.bitDepth == 64)
        settings.bitDepth = 32;
    callbacks.create (settings);
}

void ProjectDashboard::layoutCards()
{
    const int width = cardView.getWidth() - 14;   // (room for the scroll bar)
    const int gap = 12, columns = 4, cardW = (width - gap * (columns - 1)) / columns, cardH = 128, head = 30;
    int y = 0;
    sectionTops.clear();
    for (const auto& section : sections)
    {
        sectionTops.push_back ({ section, y });
        y += head;
        int col = 0;
        bool any = false;
        for (auto* c : cards)
        {
            const bool mine = c->id.isEmpty();
            juce::String cat = "My Templates";
            if (! mine)
                for (const auto& s : starters::all())
                    if (s.id == c->id)
                        cat = s.category;
            if (cat != section)
                continue;
            c->setBounds (col * (cardW + gap), y, cardW, cardH);
            any = true;
            if (++col == columns)
            {
                col = 0;
                y += cardH + gap;
            }
        }
        if (col != 0)
            y += cardH + gap;
        if (! any)
            y += 24;   // ("Save a project as a template to see it here")
        y += 8;
    }
    cardHolder.setSize (width, y);
}

void ProjectDashboard::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56);
    createTab.setBounds (top.withSizeKeepingCentre (220, 32).removeFromLeft (110));
    recentTabButton.setBounds (top.withSizeKeepingCentre (220, 32).removeFromRight (110));

    auto bottom = r.removeFromBottom (62).reduced (20, 14);
    createButton.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (10);
    cancelButton.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (10);
    openButton.setBounds (bottom.removeFromRight (130));
    showAtStartupToggle.setBounds (bottom.removeFromLeft (320));

    auto left = r.removeFromLeft (230).reduced (12, 14);
    left.removeFromTop (24);
    for (auto* b : sectionButtons)
    {
        b->setBounds (left.removeFromTop (32));
        left.removeFromTop (2);
    }

    if (! recentTab)
    {
        auto right = r.removeFromRight (330).reduced (18, 16);
        auto row = [&right] { auto x = right.removeFromTop (28); right.removeFromTop (9); return x.removeFromRight (176); };
        right.removeFromTop (26);   // "PROJECT"
        nameBox.setBounds (row());
        folderButton.setBounds (row());
        right.removeFromTop (30);   // "AUDIO"
        rateBox.setBounds (row());
        bitsBox.setBounds (row());
        fileType->setBounds (row());
        deviceBox.setBounds (row());
        note.setBounds (right.removeFromTop (30).withTrimmedLeft (0));
        right.removeFromTop (26);   // "SONG"
        auto tempoRow = row();
        tapButton.setBounds (tempoRow.removeFromRight (46));
        tempoRow.removeFromRight (6);
        tempoBox.setBounds (tempoRow.withWidth (tempoRow.getWidth()));
        keyBox.setBounds (row());
        sigBox.setBounds (row());
        countInBox.setBounds (row());
    }

    auto mid = r.reduced (20, 16);
    mid.removeFromTop (0);
    cardView.setBounds (mid);
    recentView.setBounds (mid.withTrimmedTop (28));
    layoutCards();
    int y = 0;
    for (auto* row : recentRows)
    {
        row->setBounds (0, y, recentView.getMaximumVisibleWidth(), 58);
        y += 64;
    }
    recentHolder.setSize (recentView.getMaximumVisibleWidth(), y);
}

void ProjectDashboard::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("New Project", top.reduced (20, 0), juce::Justification::centredLeft);
    g.setColour (line());
    g.fillRect (0, 56, getWidth(), 1);
    g.fillRect (0, getHeight() - 62, getWidth(), 1);
    g.fillRect (230, 56, 1, getHeight() - 118);
    if (! recentTab)
        g.fillRect (getWidth() - 330, 56, 1, getHeight() - 118);

    auto caption = [&g] (const juce::String& s, juce::Rectangle<int> at)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.5f).withKerningFactor (0.12f));
        g.drawText (s.toUpperCase(), at, juce::Justification::centredLeft);
    };
    caption ("Start with", { 20, 66, 200, 20 });

    if (recentTab)
    {
        caption ("Recent projects", { recentView.getX(), recentView.getY() - 28, 300, 22 });
        if (recentRows.isEmpty())
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("No recent projects yet.", recentView.getBounds().removeFromTop (40), juce::Justification::centredLeft);
        }
        return;
    }

    // the settings' labels, beside their controls, and the groups' captions above them
    const int x = getWidth() - 330 + 18;
    for (const auto& [text, c] : labels)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (13.0f));
        const auto left = c == &tempoBox ? tempoBox.getX() : c->getX();
        g.drawText (text, x, c->getY(), left - x - 8, c->getHeight(), juce::Justification::centredLeft, true);
    }
    caption ("Project", { x, nameBox.getY() - 24, 200, 20 });
    caption ("Audio", { x, rateBox.getY() - 24, 200, 20 });
    caption ("Song", { x, tempoBox.getY() - 24, 200, 20 });
}
}
