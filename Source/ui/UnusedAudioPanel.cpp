#include "UnusedAudioPanel.h"
#include "Theme.h"

namespace riff
{
namespace
{
    juce::Colour line()     { return theme::text.withAlpha (0.11f); }
    juce::Colour fill()     { return theme::text.withAlpha (0.045f); }
    juce::Colour onAccent() { return theme::accent.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff1a1206) : juce::Colours::white; }

    juce::String lengthText (double s)
    {
        const auto t = (int) std::round (s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    void style (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);   // (GlassDialogLook lights it)
    }

    /** A file's outline: the loudest sample in each of `n` stretches (read a little from each, so a long file is quick). */
    std::vector<float> peaksOf (const juce::File& f, juce::AudioFormatManager& formats, int n)
    {
        std::vector<float> peaks ((size_t) n, 0.0f);
        std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) };
        if (r == nullptr || r->lengthInSamples <= 0)
            return peaks;
        const auto chunk = (int) std::min<juce::int64> (2048, r->lengthInSamples / n + 1);
        juce::AudioBuffer<float> b ((int) std::max (1u, std::min (2u, r->numChannels)), chunk);
        for (int i = 0; i < n; ++i)
        {
            r->read (&b, 0, chunk, r->lengthInSamples * i / n, true, b.getNumChannels() > 1);
            peaks[(size_t) i] = b.getMagnitude (0, chunk);
        }
        const auto top = *std::max_element (peaks.begin(), peaks.end());
        if (top > 0.0f)
            for (auto& p : peaks)
                p /= top;
        return peaks;
    }
}

//==============================================================================
class UnusedAudioPanel::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Row (const unusedaudio::Item& i, juce::AudioFormatManager& formats, std::function<void()> changed_, std::function<void (Row*)> play_)
        : item (i), changed (std::move (changed_)), play (std::move (play_))
    {
        peaks = peaksOf (item.file, formats, 18);
        ticked = item.keptBy.isEmpty() && item.kind != unusedaudio::Kind::import;   // (imports: ask, they may be wanted)
        setTooltip (item.file.getFullPathName());
    }

    unusedaudio::Item item;
    bool ticked = false, playing = false;
    bool kept() const { return item.keptBy.isNotEmpty(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setOpacity (1.0f);
        g.setColour (fill());
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (line());
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
        const float alpha = kept() ? 0.45f : 1.0f;
        auto inner = r.reduced (10.0f, 0.0f);

        auto box = inner.removeFromLeft (22.0f).withSizeKeepingCentre (17.0f, 17.0f);
        if (! kept())
        {
            g.setColour (ticked ? theme::accent : theme::text.withAlpha (0.08f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (ticked ? theme::accent : line());
            g.drawRoundedRectangle (box, 4.0f, 1.0f);
            if (ticked)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                tick.lineTo (box.getX() + 7.5f, box.getBottom() - 4.5f);
                tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
                g.setColour (onAccent());
                g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        inner.removeFromLeft (10.0f);
        playArea = inner.removeFromLeft (26.0f).withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (line());
        g.drawEllipse (playArea.reduced (0.5f), 1.0f);
        g.setColour (theme::accent.withMultipliedAlpha (alpha));
        if (playing)
            g.fillRect (playArea.withSizeKeepingCentre (8.0f, 8.0f));
        else
        {
            juce::Path tri;
            tri.addTriangle (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 5.0f, playArea.getCentreX() - 3.0f, playArea.getCentreY() + 5.0f,
                             playArea.getCentreX() + 5.0f, playArea.getCentreY());
            g.fillPath (tri);
        }
        inner.removeFromLeft (12.0f);
        auto wave = inner.removeFromLeft (72.0f);
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const auto h = std::max (3.0f, peaks[i] * (wave.getHeight() - 22.0f));
            g.setColour (theme::accent.withAlpha (0.7f * alpha));
            g.fillRoundedRectangle (wave.getX() + (float) i * 4.0f, wave.getCentreY() - h * 0.5f, 2.6f, h, 1.3f);
        }
        inner.removeFromLeft (14.0f);
        auto size = inner.removeFromRight (80.0f);
        auto length = inner.removeFromRight (60.0f);
        g.setColour (theme::text.withAlpha (alpha));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (unusedaudio::sizeText (item.bytes), size, juce::Justification::centredRight);
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.drawText (lengthText (item.seconds), length, juce::Justification::centredRight);
        g.setColour (theme::text.withAlpha (alpha));
        g.drawText (item.file.getFileName(), inner.withTrimmedBottom (inner.getHeight() * 0.5f), juce::Justification::bottomLeft, true);
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (item.reason, inner.withTrimmedTop (inner.getHeight() * 0.5f + 2.0f), juce::Justification::topLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        if (playArea.contains (e.position))
        {
            play (this);
            return;
        }
        if (! kept())
        {
            ticked = ! ticked;
            repaint();
            changed();
        }
    }

private:
    std::vector<float> peaks;
    juce::Rectangle<float> playArea;
    std::function<void()> changed;
    std::function<void (Row*)> play;
};

//==============================================================================
UnusedAudioPanel::UnusedAudioPanel (std::vector<unusedaudio::Item> list, juce::String name, juce::File folder,
                                    juce::AudioFormatManager& formats, bool trash, Callbacks cb)
    : items (std::move (list)), projectName (std::move (name)), audioFolder (std::move (folder)), callbacks (std::move (cb)), toTrash (trash)
{
    setLookAndFeel (&look);
    for (const auto& item : items)
        holder.addAndMakeVisible (rows.add (new Row (item, formats, [this] { refresh(); }, [this] (Row* row)
        {
            const bool wasPlaying = row->playing;
            for (auto* r : rows)
            {
                r->playing = false;
                r->repaint();
            }
            callbacks.stopPreview();
            if (! wasPlaying)
            {
                row->playing = true;
                callbacks.preview (row->item.file);
            }
        })));
    view.setViewedComponent (&holder, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);

    int k = 0;
    for (const auto* s : { "All", "Recordings", "Bounces & renders", "Imports" })
    {
        auto* p = pills.add (new juce::TextButton (s));
        p->onClick = [this, k] { filter = k; refresh(); resized(); };
        addAndMakeVisible (p);
        ++k;
    }

    keepRecent.setColour (juce::ToggleButton::textColourId, theme::text);
    keepRecent.setColour (juce::ToggleButton::tickColourId, theme::accent);
    keepRecent.onClick = [this]
    {
        const auto dayAgo = juce::Time::getCurrentTime() - juce::RelativeTime::hours (24);
        for (auto* r : rows)
            if (! r->kept() && r->item.kind == unusedaudio::Kind::recording && r->item.modified > dayAgo)
                r->ticked = ! keepRecent.getToggleState();
        refresh();
    };
    addAndMakeVisible (keepRecent);

    trashKey.onClick = [this] { toTrash = true; refresh(); };
    folderKey.onClick = [this] { toTrash = false; refresh(); };
    trashKey.setTooltip ("Your computer's Trash (Recycle Bin on Windows): Put Back / Restore brings a file back");
    folderKey.setTooltip ("A Deleted Audio folder inside the project's audio folder: empty it from here when you're sure");
    emptyButton.onClick = [this] { callbacks.emptyDeleted(); };
    selectAll.onClick = [this]
    {
        bool all = true;
        for (auto* r : rows)
            all = all && (r->kept() || ! r->isVisible() || r->ticked);
        for (auto* r : rows)
            if (! r->kept() && r->isVisible())
                r->ticked = ! all;
        refresh();
    };
    cancel.onClick = [this] { callbacks.stopPreview(); callbacks.close(); };
    go.onClick = [this]
    {
        callbacks.stopPreview();
        const auto files = selectedFiles();
        if (! files.isEmpty())
            callbacks.remove (files, toTrash);
    };
    for (auto* b : { &trashKey, &folderKey, &emptyButton, &selectAll, &cancel, &go })
        addAndMakeVisible (b);
    summary.setColour (juce::Label::textColourId, theme::dimText);
    summary.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (summary);

    if (audioFolder.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator (audioFolder, true, "*", juce::File::findFiles))
            folderBytes += e.getFileSize();
    refresh();
    setSize (900, 680);
}

UnusedAudioPanel::~UnusedAudioPanel()
{
    setLookAndFeel (nullptr);
    if (callbacks.stopPreview)
        callbacks.stopPreview();
}

juce::Array<juce::File> UnusedAudioPanel::selectedFiles() const
{
    juce::Array<juce::File> files;
    for (auto* r : rows)
        if (r->ticked && ! r->kept() && r->isVisible())
            files.add (r->item.file);
    return files;
}

void UnusedAudioPanel::refresh()
{
    for (auto* r : rows)
    {
        const auto kind = r->item.kind;
        r->setVisible (filter == 0 || (filter == 1 && kind == unusedaudio::Kind::recording)
                       || (filter == 2 && kind == unusedaudio::Kind::render) || (filter == 3 && kind == unusedaudio::Kind::import));
        r->repaint();
    }
    for (int i = 0; i < pills.size(); ++i)
        style (*pills[i], i == filter);
    style (trashKey, toTrash);
    style (folderKey, ! toTrash);
    style (selectAll, false);
    style (cancel, false);

    juce::int64 bytes = 0;
    int count = 0;
    for (const auto& f : selectedFiles())
    {
        bytes += f.getSize();
        ++count;
    }
    style (go, count > 0);
    go.setEnabled (count > 0);
    go.setButtonText (count == 0 ? juce::String ("Nothing selected")
                                 : (toTrash ? "Move " : "Move ") + juce::String (count) + (count == 1 ? " File to " : " Files to ")
                                       + (toTrash ? "Trash" : "Deleted Audio"));
    summary.setText (count == 0 ? juce::String ("Tick the files to remove.")
                                : juce::String (count) + juce::String (juce::CharPointer_UTF8 (" selected  \xc2\xb7  frees ")) + unusedaudio::sizeText (bytes)
                                      + (toTrash ? juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  you can still get them back from the Trash"))
                                                 : juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  kept in Deleted Audio until you empty it"))),
                     juce::dontSendNotification);

    const auto deleted = unusedaudio::deletedFolder (audioFolder);
    juce::int64 deletedBytes = 0;
    if (deleted.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator (deleted, true, "*", juce::File::findFiles))
            deletedBytes += e.getFileSize();
    emptyButton.setVisible (deletedBytes > 0);
    emptyButton.setButtonText ("Empty Deleted Audio (" + unusedaudio::sizeText (deletedBytes) + ")");
    style (emptyButton, false);
    repaint();
}

void UnusedAudioPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (56);
    auto bottom = r.removeFromBottom (62).reduced (22, 14);
    go.setBounds (bottom.removeFromRight (230));
    bottom.removeFromRight (10);
    cancel.setBounds (bottom.removeFromRight (90));
    summary.setBounds (bottom);

    auto body = r.reduced (22, 14);
    auto head = body.removeFromTop (30);
    for (int i = pills.size(); --i >= 0;)
    {
        const auto w = juce::jmax (52, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f), pills[i]->getButtonText()) + 28);
        pills[i]->setBounds (head.removeFromRight (w));
        head.removeFromRight (8);
    }
    body.removeFromTop (12);
    auto options = body.removeFromBottom (78);
    options.removeFromTop (12);
    auto first = options.removeFromTop (28);
    keepRecent.setBounds (first.removeFromLeft (340));
    selectAll.setBounds (first.removeFromRight (100));
    options.removeFromTop (8);
    auto where = options.removeFromTop (28);
    where.removeFromLeft (90);
    trashKey.setBounds (where.removeFromLeft (80));
    where.removeFromLeft (6);
    folderKey.setBounds (where.removeFromLeft (170));
    emptyButton.setBounds (where.removeFromRight (240));

    view.setBounds (body);
    int y = 0;
    for (auto* row : rows)
        if (row->isVisible())
        {
            row->setBounds (0, y, view.getWidth() - 14, 54);
            y += 60;
        }
    holder.setSize (view.getMaximumVisibleWidth(), juce::jmax (y, 10));
}

void UnusedAudioPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Delete Unused Audio", 22, 0, 400, 56, juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (projectName + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  audio folder ")) + unusedaudio::sizeText (folderBytes), getWidth() - 422, 0, 400, 56, juce::Justification::centredRight);
    g.setColour (line());
    g.fillRect (0, 56, getWidth(), 1);
    g.fillRect (0, getHeight() - 62, getWidth(), 1);

    int unused = 0;
    juce::int64 bytes = 0;
    for (const auto& i : items)
        if (i.keptBy.isEmpty())
        {
            ++unused;
            bytes += i.bytes;
        }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (unused == 0 ? juce::String ("Every audio file in the project is used.")
                            : juce::String (unused) + (unused == 1 ? " file" : " files") + juce::String (juce::CharPointer_UTF8 (" not used anywhere in the project  \xc2\xb7  ")) + unusedaudio::sizeText (bytes),
                22, 70, 480, 30, juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("Move them to:", 22, trashKey.getY(), 90, trashKey.getHeight(), juce::Justification::centredLeft);
}
}
