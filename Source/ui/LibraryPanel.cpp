#include "LibraryPanel.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../AppInfo.h"
#include "../engine/AudioEdit.h"

namespace riff
{

using sampleinfo::Category;

juce::File LibraryPanel::patchesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (app::name).getChildFile ("Patches");
}

namespace
{
    juce::Colour colourOf (int category)
    {
        switch ((Category) category)
        {
            case Category::drums:      return juce::Colour (0xffe46b5a);
            case Category::bass:       return juce::Colour (0xff5aa0e4);
            case Category::keys:       return juce::Colour (0xff9a6be4);
            case Category::synth:      return juce::Colour (0xff6b7be4);
            case Category::guitar:     return juce::Colour (0xffe4b95a);
            case Category::strings:    return juce::Colour (0xff5ae49a);
            case Category::vocals:     return juce::Colour (0xffe45ab4);
            case Category::percussion: return juce::Colour (0xff5ae4e0);
            case Category::fx:         return juce::Colour (0xffb4b4b4);
            default:                   return juce::Colour (0xff8a8c93);
        }
    }

    /** The tabs' little pictures: drawn, so they're sharp at any size and take the theme's colours. */
    void drawIcon (juce::Graphics& g, int category, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        const auto c = r.getCentre();
        const auto s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        juce::Path p;
        const auto stroke = juce::PathStrokeType (juce::jmax (1.2f, s * 0.13f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        switch (category)
        {
            case -1:   // all: an asterisk
                for (int i = 0; i < 3; ++i)
                {
                    const auto a = juce::MathConstants<float>::pi * (float) i / 3.0f + 0.5f * juce::MathConstants<float>::halfPi;
                    p.startNewSubPath (c.x - std::cos (a) * s * 0.8f, c.y - std::sin (a) * s * 0.8f);
                    p.lineTo (c.x + std::cos (a) * s * 0.8f, c.y + std::sin (a) * s * 0.8f);
                }
                g.strokePath (p, stroke);
                return;
            case (int) Category::drums:   // a drum and its sticks
            {
                const juce::Rectangle<float> body (c.x - s * 0.75f, c.y - s * 0.25f, s * 1.5f, s * 0.95f);
                p.addEllipse (body.withHeight (s * 0.5f));
                p.startNewSubPath (body.getX(), body.getY() + s * 0.25f);
                p.lineTo (body.getX(), body.getBottom() - s * 0.2f);
                p.quadraticTo (c.x, body.getBottom() + s * 0.2f, body.getRight(), body.getBottom() - s * 0.2f);
                p.lineTo (body.getRight(), body.getY() + s * 0.25f);
                p.startNewSubPath (c.x - s * 0.5f, c.y - s * 0.95f); p.lineTo (c.x - s * 0.1f, c.y - s * 0.2f);
                p.startNewSubPath (c.x + s * 0.55f, c.y - s * 0.95f); p.lineTo (c.x + s * 0.15f, c.y - s * 0.2f);
                g.strokePath (p, stroke);
                return;
            }
            case (int) Category::bass:   // a bass clef
            {
                p.startNewSubPath (c.x - s * 0.6f, c.y - s * 0.2f);
                p.cubicTo (c.x - s * 0.7f, c.y - s * 0.9f, c.x + s * 0.5f, c.y - s * 0.9f, c.x + s * 0.35f, c.y - s * 0.1f);
                p.cubicTo (c.x + s * 0.25f, c.y + s * 0.4f, c.x - s * 0.2f, c.y + s * 0.75f, c.x - s * 0.7f, c.y + s * 0.85f);
                g.strokePath (p, stroke);
                g.fillEllipse (c.x - s * 0.72f, c.y - s * 0.35f, s * 0.32f, s * 0.32f);
                g.fillEllipse (c.x + s * 0.62f, c.y - s * 0.5f, s * 0.2f, s * 0.2f);
                g.fillEllipse (c.x + s * 0.62f, c.y - s * 0.05f, s * 0.2f, s * 0.2f);
                return;
            }
            case (int) Category::keys:   // piano keys
            {
                const juce::Rectangle<float> k (c.x - s * 0.85f, c.y - s * 0.6f, s * 1.7f, s * 1.2f);
                g.drawRoundedRectangle (k, s * 0.12f, stroke.getStrokeThickness() * 0.8f);
                for (int i = 1; i < 4; ++i)
                    g.drawLine (k.getX() + k.getWidth() * (float) i / 4.0f, k.getCentreY(), k.getX() + k.getWidth() * (float) i / 4.0f, k.getBottom(), stroke.getStrokeThickness() * 0.7f);
                for (const auto at : { 0.25f, 0.5f, 0.75f })
                    g.fillRect (juce::Rectangle<float> (k.getX() + k.getWidth() * at - s * 0.12f, k.getY(), s * 0.24f, k.getHeight() * 0.55f));
                return;
            }
            case (int) Category::synth:   // a sawtooth wave
                p.startNewSubPath (c.x - s * 0.9f, c.y + s * 0.5f);
                p.lineTo (c.x - s * 0.3f, c.y - s * 0.5f); p.lineTo (c.x - s * 0.3f, c.y + s * 0.5f);
                p.lineTo (c.x + s * 0.3f, c.y - s * 0.5f); p.lineTo (c.x + s * 0.3f, c.y + s * 0.5f);
                p.lineTo (c.x + s * 0.9f, c.y - s * 0.5f);
                g.strokePath (p, stroke);
                return;
            case (int) Category::guitar:   // a guitar
            {
                g.saveState();
                g.addTransform (juce::AffineTransform::rotation (-0.75f, c.x, c.y));
                p.addEllipse (c.x - s * 0.45f, c.y + s * 0.05f, s * 0.9f, s * 0.75f);
                p.addEllipse (c.x - s * 0.33f, c.y - s * 0.4f, s * 0.66f, s * 0.6f);
                g.strokePath (p, stroke);
                g.drawLine (c.x, c.y - s * 0.3f, c.x, c.y - s * 1.05f, stroke.getStrokeThickness());
                g.fillEllipse (c.x - s * 0.1f, c.y + s * 0.32f, s * 0.2f, s * 0.2f);
                g.restoreState();
                return;
            }
            case (int) Category::strings:   // a violin with its bow
            {
                p.startNewSubPath (c.x, c.y - s * 0.75f);
                p.cubicTo (c.x - s * 0.55f, c.y - s * 0.7f, c.x - s * 0.2f, c.y, c.x - s * 0.5f, c.y + s * 0.3f);
                p.cubicTo (c.x - s * 0.6f, c.y + s * 0.85f, c.x + s * 0.6f, c.y + s * 0.85f, c.x + s * 0.5f, c.y + s * 0.3f);
                p.cubicTo (c.x + s * 0.2f, c.y, c.x + s * 0.55f, c.y - s * 0.7f, c.x, c.y - s * 0.75f);
                g.strokePath (p, stroke);
                g.drawLine (c.x - s * 0.95f, c.y + s * 0.15f, c.x + s * 0.95f, c.y - s * 0.6f, stroke.getStrokeThickness() * 0.8f);
                return;
            }
            case (int) Category::vocals:   // a microphone
            {
                const juce::Rectangle<float> head (c.x - s * 0.3f, c.y - s * 0.9f, s * 0.6f, s * 0.95f);
                g.fillRoundedRectangle (head, s * 0.3f);
                p.startNewSubPath (c.x - s * 0.55f, c.y - s * 0.25f);
                p.quadraticTo (c.x - s * 0.55f, c.y + s * 0.45f, c.x, c.y + s * 0.45f);
                p.quadraticTo (c.x + s * 0.55f, c.y + s * 0.45f, c.x + s * 0.55f, c.y - s * 0.25f);
                p.startNewSubPath (c.x, c.y + s * 0.45f); p.lineTo (c.x, c.y + s * 0.9f);
                p.startNewSubPath (c.x - s * 0.35f, c.y + s * 0.9f); p.lineTo (c.x + s * 0.35f, c.y + s * 0.9f);
                g.strokePath (p, stroke);
                return;
            }
            case (int) Category::percussion:   // a conga
            {
                p.addEllipse (c.x - s * 0.45f, c.y - s * 0.85f, s * 0.9f, s * 0.35f);
                p.startNewSubPath (c.x - s * 0.45f, c.y - s * 0.67f);
                p.quadraticTo (c.x - s * 0.75f, c.y + s * 0.1f, c.x - s * 0.3f, c.y + s * 0.9f);
                p.lineTo (c.x + s * 0.3f, c.y + s * 0.9f);
                p.quadraticTo (c.x + s * 0.75f, c.y + s * 0.1f, c.x + s * 0.45f, c.y - s * 0.67f);
                g.strokePath (p, stroke);
                return;
            }
            case (int) Category::fx:   // a sparkle
            {
                p.startNewSubPath (c.x, c.y - s * 0.95f);
                p.quadraticTo (c.x + s * 0.12f, c.y - s * 0.12f, c.x + s * 0.8f, c.y);
                p.quadraticTo (c.x + s * 0.12f, c.y + s * 0.12f, c.x, c.y + s * 0.95f);
                p.quadraticTo (c.x - s * 0.12f, c.y + s * 0.12f, c.x - s * 0.8f, c.y);
                p.quadraticTo (c.x - s * 0.12f, c.y - s * 0.12f, c.x, c.y - s * 0.95f);
                p.closeSubPath();
                g.fillPath (p);
                return;
            }
            default:
                return;
        }
    }

    juce::File cacheFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (app::name).getChildFile ("Sample Info.json");
    }
}

//==============================================================================
/** Finds out what each file is (name, then sound), one at a time in the background; kept in a file between sessions. */
class LibraryPanel::Analyser final : private juce::Thread
{
public:
    Analyser() : juce::Thread ("Loop Browser analysis")
    {
        formats.registerBasicFormats();
        if (const auto saved = juce::JSON::parse (cacheFile()); auto* o = saved.getDynamicObject())
            for (const auto& [path, entry] : o->getProperties())
                known[path.toString()] = { (juce::int64) entry.getProperty ("size", 0), (juce::int64) entry.getProperty ("time", 0),
                                           sampleinfo::Info::fromVar (entry.getProperty ("info", {})) };
    }

    ~Analyser() override
    {
        stopThread (4000);
        save();
    }

    /** What's known about it (false: not yet, or the file changed since). */
    bool get (const juce::File& file, sampleinfo::Info& info)
    {
        const juce::ScopedLock lock (mutex);
        const auto it = known.find (file.getFullPathName());
        if (it == known.end() || it->second.size != file.getSize() || it->second.time != file.getLastModificationTime().toMilliseconds())
            return false;
        info = it->second.info;
        return true;
    }

    /** These, in this order (the ones not known yet). */
    void want (const juce::Array<juce::File>& files)
    {
        {
            const juce::ScopedLock lock (mutex);
            queue.clear();
            for (const auto& f : files)
            {
                const auto it = known.find (f.getFullPathName());
                if (it == known.end() || it->second.size != f.getSize() || it->second.time != f.getLastModificationTime().toMilliseconds())
                    queue.push_back (f);
            }
        }
        if (! queue.empty() && ! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    std::atomic<bool> learnt { false };   // something new since the panel looked

private:
    struct Entry { juce::int64 size = 0, time = 0; sampleinfo::Info info; };

    void run() override
    {
        int sinceSave = 0;
        while (! threadShouldExit())
        {
            juce::File next;
            {
                const juce::ScopedLock lock (mutex);
                if (! queue.empty())
                {
                    next = queue.front();
                    queue.pop_front();
                }
            }
            if (next == juce::File())
            {
                if (sinceSave > 0)
                {
                    save();
                    sinceSave = 0;
                }
                wait (500);
                continue;
            }
            const auto info = sampleinfo::analyse (next, formats);
            {
                const juce::ScopedLock lock (mutex);
                known[next.getFullPathName()] = { next.getSize(), next.getLastModificationTime().toMilliseconds(), info };
            }
            learnt = true;
            if (++sinceSave >= 50)
            {
                save();
                sinceSave = 0;
            }
        }
    }

    void save()
    {
        auto* o = new juce::DynamicObject();
        {
            const juce::ScopedLock lock (mutex);
            for (const auto& [path, e] : known)
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty ("size", e.size);
                entry->setProperty ("time", e.time);
                entry->setProperty ("info", e.info.toVar());
                o->setProperty (path, juce::var (entry));
            }
        }
        cacheFile().getParentDirectory().createDirectory();
        cacheFile().replaceWithText (juce::JSON::toString (juce::var (o), true));
    }

    juce::CriticalSection mutex;
    std::map<juce::String, Entry> known;
    std::deque<juce::File> queue;
    juce::AudioFormatManager formats;
};

//==============================================================================
class LibraryPanel::CategoryTabs final : public juce::Component
{
public:
    std::function<void (int)> onChoose;
    int chosen = -1;

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 10; ++i)
        {
            const auto cell = cellOf (i).toFloat().reduced (2.0f);
            const auto category = i - 1;
            const bool on = category == chosen;
            const bool over = isMouseOver() && cell.contains (getMouseXYRelative().toFloat());
            g.setColour (on ? theme::accent.withAlpha (0.16f) : theme::text.withAlpha (over ? 0.08f : 0.04f));
            g.fillRoundedRectangle (cell, 6.0f);
            g.setColour (on ? theme::accent : theme::text.withAlpha (0.12f));
            g.drawRoundedRectangle (cell.reduced (0.5f), 6.0f, on ? 1.2f : 1.0f);
            const auto colour = on ? theme::accent : (category < 0 ? theme::text.withAlpha (0.8f) : colourOf (category));
            drawIcon (g, category, cell.withHeight (cell.getHeight() * 0.6f).reduced (cell.getWidth() * 0.3f, 4.0f).translated (0.0f, 2.0f), colour);
            g.setColour (on ? theme::accent : theme::text.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (category < 0 ? juce::String ("All") : sampleinfo::nameOf ((Category) category),
                        cell.withTrimmedTop (cell.getHeight() * 0.58f).toNearestInt(), juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 10; ++i)
            if (cellOf (i).contains (e.getPosition()))
            {
                chosen = i - 1;
                repaint();
                if (onChoose)
                    onChoose (chosen);
            }
    }

private:
    juce::Rectangle<int> cellOf (int i) const
    {
        const auto w = getWidth() / 5, h = getHeight() / 2;
        return { (i % 5) * w, (i / 5) * h, w, h };
    }
};

//==============================================================================
class LibraryPanel::Switch final : public juce::Button
{
public:
    explicit Switch (const juce::String& name) : juce::Button (name) { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        auto pill = r.removeFromRight (30.0f).withSizeKeepingCentre (30.0f, 16.0f);
        g.setColour (getToggleState() ? theme::accent : theme::text.withAlpha (over ? 0.28f : 0.2f));
        g.fillRoundedRectangle (pill, 8.0f);
        const auto knob = getToggleState() ? pill.removeFromRight (16.0f) : pill.withWidth (16.0f);
        g.setColour (getToggleState() ? theme::background : theme::text.withAlpha (0.6f));
        g.fillEllipse (knob.reduced (2.5f));
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (getName(), r, juce::Justification::centredLeft, true);
    }
};

//==============================================================================
/** The previewed file's waveform, and how far it has played. */
class LibraryPanel::Waveform final : public juce::Component
{
public:
    explicit Waveform (AudioEngine& e) : engine (e) {}

    void setPeaks (std::vector<float> newPeaks, juce::Colour newColour)
    {
        peaks = std::move (newPeaks);
        colour = newColour;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::background.darker (0.3f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::text.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        const auto progress = engine.previewProgress();
        if (! peaks.empty())
        {
            const auto inner = r.reduced (4.0f, 5.0f);
            const auto mid = inner.getCentreY();
            if (progress >= 0.0f)
            {
                g.setColour (colour.withAlpha (0.12f));
                g.fillRect (inner.withWidth (inner.getWidth() * progress));
            }
            g.setColour (colour);
            for (int x = 0; x < (int) inner.getWidth(); ++x)
            {
                const auto p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) ((float) x / inner.getWidth() * (float) peaks.size()))];
                const auto h = juce::jmax (0.5f, p * inner.getHeight() * 0.5f);
                g.fillRect (inner.getX() + (float) x, mid - h, 1.0f, h * 2.0f);
            }
            if (progress >= 0.0f)
            {
                g.setColour (theme::text);
                g.fillRect (inner.getX() + inner.getWidth() * progress - 0.75f, r.getY(), 1.5f, r.getHeight());
            }
        }
        else
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Click a sample to hear it", r, juce::Justification::centred, false);
        }
    }

private:
    AudioEngine& engine;
    std::vector<float> peaks;
    juce::Colour colour = theme::accent;
};

//==============================================================================
LibraryPanel::LibraryPanel (AudioEngine& e, juce::PropertiesFile& s) : engine (e), settings (s)
{
    formats.registerBasicFormats();
    analyser = std::make_unique<Analyser>();

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

    search.setTextToShowWhenEmpty ("Search loops and samples", theme::dimText);
    search.onTextChange = [this] { filter(); };
    addChildComponent (search);

    // Tabs by instrument, loops / one-shots, key
    tabs = std::make_unique<CategoryTabs>();
    category = settings.getIntValue ("libCategory", -1);
    tabs->chosen = category;
    tabs->onChoose = [this] (int c) { category = c; saveSettings(); filter(); };
    addChildComponent (*tabs);

    kind = settings.getIntValue ("libKind", 2);
    int index = 0;
    for (auto* b : { &loopsOnly, &oneShots, &both })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->setToggleState (index == kind, juce::dontSendNotification);
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, index] { kind = index; loopsOnly.setToggleState (kind == 0, juce::dontSendNotification); oneShots.setToggleState (kind == 1, juce::dontSendNotification); both.setToggleState (kind == 2, juce::dontSendNotification); saveSettings(); filter(); };
        addChildComponent (b);
        ++index;
    }
    keyFilter.addItem ("Any key", 1);
    keyFilter.addItem ("Fits the song's key", 2);
    keyFilter.addSeparator();
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int k = 0; k < 12; ++k)
        keyFilter.addItem (juce::String (names[k]) + " major", 10 + k);
    for (int k = 0; k < 12; ++k)
        keyFilter.addItem (juce::String (names[k]) + " minor", 30 + k);
    keyFilter.setSelectedId (settings.getIntValue ("libKey", 1), juce::dontSendNotification);
    keyFilter.onChange = [this] { saveSettings(); filter(); };
    keyFilter.setWantsKeyboardFocus (false);
    addChildComponent (keyFilter);

    // Fitting to the song
    songLabel.setFont (juce::FontOptions (11.5f));
    songLabel.setColour (juce::Label::textColourId, theme::dimText);
    addChildComponent (songLabel);
    tempoSwitch = std::make_unique<Switch> ("Play in song tempo");
    keySwitch = std::make_unique<Switch> ("Play in song key");
    inTimeSwitch = std::make_unique<Switch> ("Start in time with the song");
    tempoSwitch->setToggleState (settings.getBoolValue ("libTempo", true), juce::dontSendNotification);
    keySwitch->setToggleState (settings.getBoolValue ("libKeySync", true), juce::dontSendNotification);
    inTimeSwitch->setToggleState (settings.getBoolValue ("libInTime", false), juce::dontSendNotification);
    tempoSwitch->setTooltip ("Loops play stretched to the song's tempo - and follow it when added to a track");
    keySwitch->setTooltip ("Loops and notes play moved to the song's key (the nearest way) - and follow it when added to a track");
    inTimeSwitch->setTooltip ("While the song plays, a preview starts on its next beat");
    for (auto* sw : { tempoSwitch.get(), keySwitch.get(), inTimeSwitch.get() })
    {
        sw->setWantsKeyboardFocus (false);
        sw->onClick = [this] { saveSettings(); list.repaint(); };
        addChildComponent (*sw);
    }

    waveform = std::make_unique<Waveform> (engine);
    addChildComponent (*waveform);
    playButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa0"));
    playButton.setTooltip ("Stop (or play the selected one again)");
    playButton.setWantsKeyboardFocus (false);
    playButton.onClick = [this]
    {
        if (engine.isPreviewing())
            stop();
        else
            play (list.getSelectedRow());
    };
    addChildComponent (playButton);
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volume.setRange (-30.0, 6.0, 0.5);
    volume.setDoubleClickReturnValue (true, -2.0);
    volume.setValue (settings.getDoubleValue ("libVolume", -2.0), juce::dontSendNotification);
    volume.onValueChange = [this]
    {
        engine.setPreviewGain (juce::Decibels::decibelsToGain ((float) volume.getValue()));
        volumeLabel.setText (juce::String (volume.getValue(), 1) + " dB", juce::dontSendNotification);
        saveSettings();
    };
    volume.setWantsKeyboardFocus (false);
    addChildComponent (volume);
    volumeLabel.setFont (juce::FontOptions (11.0f));
    volumeLabel.setColour (juce::Label::textColourId, theme::dimText);
    volumeLabel.setJustificationType (juce::Justification::centredRight);
    addChildComponent (volumeLabel);
    volume.onValueChange();

    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    addAndMakeVisible (list);

    setWantsKeyboardFocus (true);
    showPage (Page::patches);
    startTimerHz (30);
}

LibraryPanel::~LibraryPanel()
{
    ++previewGeneration;
    fitter.removeAllJobs (true, 10000);
    engine.stopPreview();
}

void LibraryPanel::saveSettings()
{
    settings.setValue ("libCategory", category);
    settings.setValue ("libKind", kind);
    settings.setValue ("libKey", keyFilter.getSelectedId());
    settings.setValue ("libTempo", tempoSwitch->getToggleState());
    settings.setValue ("libKeySync", keySwitch->getToggleState());
    settings.setValue ("libInTime", inTimeSwitch->getToggleState());
    settings.setValue ("libVolume", volume.getValue());
}

void LibraryPanel::showPage (Page newPage)
{
    page = newPage;
    patchesTab.setToggleState (page == Page::patches, juce::dontSendNotification);
    loopsTab.setToggleState (page == Page::loops, juce::dontSendNotification);
    savePatchButton.setVisible (page == Page::patches);
    for (auto* c : std::initializer_list<juce::Component*> { &folderButton, &search, tabs.get(), &loopsOnly, &oneShots, &both, &keyFilter,
                                                             &songLabel, tempoSwitch.get(), keySwitch.get(), inTimeSwitch.get(),
                                                             waveform.get(), &playButton, &volume, &volumeLabel })
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
        for (const auto& entry : juce::RangedDirectoryIterator (loopFolder, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.caf;*.m4a",
                                                                juce::File::findFiles))
        {
            allLoops.add (entry.getFile());
            if (allLoops.size() >= 5000)
                break;
        }
        std::sort (allLoops.begin(), allLoops.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        analyser->want (allLoops);
        filter();
        return;
    }
    list.updateContent();
    list.repaint();
}

sampleinfo::Info LibraryPanel::infoFor (const juce::File& file, bool* known)
{
    sampleinfo::Info info;
    if (analyser->get (file, info))
    {
        if (known != nullptr) *known = true;
        return info;
    }
    if (known != nullptr) *known = false;
    bool categoryKnown = false, loopKnown = false;
    return sampleinfo::fromName (file, categoryKnown, loopKnown);   // (until it's been listened to)
}

void LibraryPanel::filter()
{
    const auto selected = juce::isPositiveAndBelow (list.getSelectedRow(), shownLoops.size()) ? shownLoops[list.getSelectedRow()] : juce::File();
    const auto words = juce::StringArray::fromTokens (search.getText(), true);
    const auto song = songNow ? songNow() : Song();
    const auto keyChoice = keyFilter.getSelectedId();
    shownLoops.clear();
    for (const auto& f : allLoops)
    {
        bool all = true;
        for (const auto& w : words)
            all = all && f.getFullPathName().fromLastOccurrenceOf (loopFolder.getFullPathName(), false, false).containsIgnoreCase (w);
        if (! all)
            continue;
        const auto info = infoFor (f);
        if (category >= 0 && (int) info.category != category)
            continue;
        if ((kind == 0 && ! info.loop) || (kind == 1 && info.loop))
            continue;
        if (keyChoice == 2 && info.keyRoot >= 0 && ! info.keyIsNote
            && sampleinfo::sourceRootFor (info.keyRoot, info.minor, song.minor) != song.keyRoot)
            continue;   // (in the song's key or its relative; drums and notes fit any)
        if (keyChoice >= 10)
        {
            const bool wantMinor = keyChoice >= 30;
            const auto wantRoot = keyChoice - (wantMinor ? 30 : 10);
            if (info.keyRoot < 0 || info.keyIsNote || info.keyRoot != wantRoot || info.minor != wantMinor)
                continue;
        }
        shownLoops.add (f);
    }
    list.updateContent();
    const auto row = shownLoops.indexOf (selected);
    {
        const juce::ScopedValueSetter<bool> quiet (quietSelect, true);   // (kept selected, not played again)
        if (row >= 0)
            list.selectRow (row, true, true);
        else
            list.deselectAllRows();
    }
    previewRow = shownLoops.indexOf (previewing);
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

    if (page == Page::patches)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (files[row].getFileNameWithoutExtension(), 8, 0, w - 16, h, juce::Justification::centredLeft, true);
        return;
    }

    bool known = false;
    const auto info = infoFor (files[row], &known);
    const auto playing = row == previewRow && engine.isPreviewing();
    if (playing)
    {
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillRect (0, 0, w, h);
    }
    // columns: dot, name, tempo, key, length
    const auto keyW = 46, barsW = 36, bpmW = tempoSwitch->getToggleState() ? 62 : 34;
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (6, 0);
    g.setColour (colourOf ((int) info.category).withAlpha (known ? 1.0f : 0.45f));
    g.fillEllipse (r.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
    r.removeFromLeft (6);
    const auto barsArea = r.removeFromRight (barsW), keyArea = r.removeFromRight (keyW), bpmArea = r.removeFromRight (bpmW);

    g.setFont (juce::FontOptions (10.5f));
    g.setColour (theme::dimText);
    const auto song = songNow ? songNow() : Song();
    juce::String bpm = info.bpm > 0.0 ? juce::String (info.bpm, info.bpm == std::floor (info.bpm) ? 0 : 1) : juce::String (known ? juce::String::fromUTF8 ("\xe2\x80\x93") : "...");
    if (info.loop && info.bpm > 0.0 && tempoSwitch->getToggleState() && std::abs (info.bpm - song.bpm) > 0.5)
        bpm << juce::String::fromUTF8 (" \xe2\x86\x92 ") << juce::String (song.bpm, 0);
    g.drawText (bpm, bpmArea, juce::Justification::centredRight, true);
    g.setColour (theme::accent.interpolatedWith (theme::text, 0.45f).withAlpha (0.9f));
    const auto key = sampleinfo::keyName (info);
    g.drawText (key.isNotEmpty() ? key : juce::String (known ? juce::String::fromUTF8 ("\xe2\x80\x93") : "..."), keyArea, juce::Justification::centredRight, true);
    g.setColour (theme::dimText);
    g.drawText (info.loop ? (info.bars > 0.0 ? juce::String (info.bars, std::abs (info.bars - std::round (info.bars)) < 0.05 ? 0 : 1) : juce::String()) : juce::String ("1-shot"),
                barsArea, juce::Justification::centredRight, true);

    g.setColour (playing ? theme::accent : theme::text);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ((playing ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") : juce::String()) + files[row].getFileNameWithoutExtension(),
                r.withTrimmedRight (4), juce::Justification::centredLeft, true);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (page == Page::patches)
    {
        if (juce::isPositiveAndBelow (row, patches.size()) && onLoadPatch)
            onLoadPatch (patches[row]);
        return;
    }
    // (a click on the row that's already selected plays it again; a new one was played by selectedRowsChanged)
    if (std::exchange (justSelected, false))
        return;
    play (row);
}

void LibraryPanel::selectedRowsChanged (int lastRowSelected)
{
    // Click to hear it, or move with the arrow keys: each one plays as you get to it (FL Studio's way)
    if (page == Page::loops && lastRowSelected >= 0 && ! quietSelect)
    {
        justSelected = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
        play (lastRowSelected);
    }
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (page == Page::loops && juce::isPositiveAndBelow (row, shownLoops.size()) && onAddFile)
    {
        stop();
        onAddFile (shownLoops[row]);
    }
}

juce::var LibraryPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (page != Page::loops || rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], shownLoops.size()))
        return {};
    return "file:" + shownLoops[rows[0]].getFullPathName();
}

bool LibraryPanel::keyPressed (const juce::KeyPress& key)
{
    if (page == Page::loops && key == juce::KeyPress::spaceKey && ! search.hasKeyboardFocus (false))
    {
        playButton.triggerClick();
        return true;
    }
    return false;
}

void LibraryPanel::stop()
{
    ++previewGeneration;
    engine.stopPreview();
    previewRow = -1;
    previewing = juce::File();
    list.repaint();
}

LibraryPanel::Fit LibraryPanel::fitFor (const juce::File& file)
{
    Fit fit;
    const auto info = infoFor (file);
    const auto song = songNow ? songNow() : Song();
    if (tempoSwitch->getToggleState() && info.loop && info.bpm > 0.0)
        fit.sourceBpm = info.bpm;
    if (keySwitch->getToggleState() && info.keyRoot >= 0)
        fit.sourceKeyRoot = info.keyIsNote ? info.keyRoot : sampleinfo::sourceRootFor (info.keyRoot, info.minor, song.minor);
    return fit;
}

void LibraryPanel::play (int row)
{
    if (! juce::isPositiveAndBelow (row, shownLoops.size()))
        return;
    const auto file = shownLoops[row];
    const auto generation = ++previewGeneration;
    engine.stopPreview();
    previewRow = row;
    lastPlayedRow = row;
    previewing = file;
    list.repaint();

    // Read, fitted to the song (stretched to its tempo, moved to its key), in the background: then played
    const auto info = infoFor (file);
    const auto song = songNow ? songNow() : Song();
    const auto ratio = (tempoSwitch->getToggleState() && info.loop && info.bpm > 0.0 && song.bpm > 0.0) ? info.bpm / song.bpm : 1.0;
    const auto semitones = (keySwitch->getToggleState() && info.keyRoot >= 0)
        ? sampleinfo::shiftToKey (info.keyRoot, info.keyIsNote ? song.minor : info.minor, song.keyRoot, song.minor) : 0;
    const auto colour = colourOf ((int) info.category);
    juce::WeakReference<LibraryPanel> self (this);
    fitter.addJob ([this, self, file, generation, ratio, semitones, colour]
    {
        if (previewGeneration.load() != generation)
            return;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return;
        const auto rate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (rate * 30.0));
        auto audio = std::make_unique<juce::AudioBuffer<float>> (2, juce::jmax (1, length));
        reader->read (audio.get(), 0, length, 0, true, reader->numChannels > 1);
        if (reader->numChannels == 1)
            audio->copyFrom (1, 0, *audio, 0, 0, length);

        // its picture (as it is: the stretch doesn't change its shape)
        std::vector<float> peaks (400, 0.0f);
        for (int i = 0; i < length; ++i)
        {
            auto& p = peaks[(size_t) juce::jmin (399, (int) ((juce::int64) i * 400 / juce::jmax (1, length)))];
            p = juce::jmax (p, std::abs (audio->getSample (0, i)), std::abs (audio->getSample (1, i)));
        }
        float top = 0.0f;
        for (const auto p : peaks) top = juce::jmax (top, p);
        for (auto& p : peaks) p = top > 0.0f ? std::sqrt (p / top) : 0.0f;

        if (std::abs (ratio - 1.0) > 0.005 && previewGeneration.load() == generation)
            *audio = audioedit::timeStretch (*audio, ratio, rate);
        if (semitones != 0 && previewGeneration.load() == generation)
            *audio = audioedit::pitchShift (*audio, (double) semitones, rate);

        auto shared = std::make_shared<std::unique_ptr<juce::AudioBuffer<float>>> (std::move (audio));
        juce::MessageManager::callAsync ([self, shared, rate, generation, peaks = std::move (peaks), colour]() mutable
        {
            auto* panel = self.get();
            if (panel == nullptr || panel->previewGeneration.load() != generation)
                return;
            panel->waveform->setPeaks (std::move (peaks), colour);
            const auto song = panel->songNow ? panel->songNow() : Song();
            const auto delay = panel->inTimeSwitch->getToggleState() && song.playing ? song.toNextBeat : 0.0;
            panel->engine.previewAudio (std::move (*shared), rate, delay);
            panel->list.repaint();
        });
    });
}

void LibraryPanel::timerCallback()
{
    if (page != Page::loops)
        return;
    // The preview finished: drop the play mark.
    if (previewRow >= 0 && ! engine.isPreviewing() && previewGeneration.load() == shownGeneration)
    {
        previewRow = -1;
        list.repaint();
    }
    shownGeneration = previewGeneration.load();
    if (engine.isPreviewing() || waveformWasPlaying)
        waveform->repaint();
    waveformWasPlaying = engine.isPreviewing();
    playButton.setButtonText (engine.isPreviewing() ? juce::String::fromUTF8 ("\xe2\x96\xa0") : juce::String::fromUTF8 ("\xe2\x96\xb6"));

    // What the analysis learnt: shown (twice a second at most)
    if (++ticks % 15 == 0 && analyser->learnt.exchange (false))
    {
        if (category >= 0 || kind != 2 || keyFilter.getSelectedId() != 1)
            filter();
        else
            list.repaint();
    }
    if (ticks % 15 == 0 && songNow)
    {
        const auto song = songNow();
        static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        songLabel.setText ("Song: " + juce::String (song.bpm, std::abs (song.bpm - std::round (song.bpm)) < 0.05 ? 0 : 1) + " BPM  " + juce::String::fromUTF8 ("\xc2\xb7") + "  "
                           + names[(song.keyRoot % 12 + 12) % 12] + (song.minor ? " minor" : " major"), juce::dontSendNotification);
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
    {
        // the box round the song switches, and the column heads
        g.setColour (theme::text.withAlpha (0.05f));
        g.fillRoundedRectangle (syncBox.toFloat(), 7.0f);
        g.setColour (theme::text.withAlpha (0.1f));
        g.drawRoundedRectangle (syncBox.toFloat().reduced (0.5f), 7.0f, 1.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (9.5f));
        auto head = columnHeads.reduced (6, 0);
        head.removeFromLeft (16);
        const auto barsW = 36, keyW = 46, bpmW = tempoSwitch->getToggleState() ? 62 : 34;
        g.drawText ("BARS", head.removeFromRight (barsW), juce::Justification::centredRight, false);
        g.drawText ("KEY", head.removeFromRight (keyW), juce::Justification::centredRight, false);
        g.drawText ("BPM", head.removeFromRight (bpmW), juce::Justification::centredRight, false);
        g.drawText ("NAME", head, juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText ("Click to hear (arrow keys: the next one) " + juce::String::fromUTF8 ("\xc2\xb7")
                          + " double-click or drag onto a track to add", hint, juce::Justification::centred, 2);
        if (allLoops.isEmpty())
            g.drawFittedText ("No audio files in " + loopFolder.getFullPathName() + ". Click the folder button to choose where your loops and samples are.",
                              list.getBounds().reduced (12), juce::Justification::centredTop, 5);
    }
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
        list.setBounds (r);
        return;
    }

    auto row = r.removeFromTop (26);
    folderButton.setBounds (row.removeFromLeft (juce::jmin (110, row.getWidth() / 3)));
    row.removeFromLeft (6);
    search.setBounds (row);
    r.removeFromTop (6);
    tabs->setBounds (r.removeFromTop (juce::jlimit (70, 96, getWidth() / 4)));
    r.removeFromTop (6);
    row = r.removeFromTop (24);
    keyFilter.setBounds (row.removeFromRight (juce::jmin (120, row.getWidth() / 3)));
    row.removeFromRight (6);
    const auto third = (row.getWidth() - 8) / 3;
    loopsOnly.setBounds (row.removeFromLeft (third));
    row.removeFromLeft (4);
    oneShots.setBounds (row.removeFromLeft (third));
    row.removeFromLeft (4);
    both.setBounds (row);
    r.removeFromTop (6);

    syncBox = r.removeFromTop (102);
    auto box = syncBox.reduced (10, 6);
    songLabel.setBounds (box.removeFromTop (20));
    for (auto* sw : { tempoSwitch.get(), keySwitch.get(), inTimeSwitch.get() })
        sw->setBounds (box.removeFromTop (24));
    r.removeFromTop (6);

    hint = r.removeFromBottom (30);
    auto foot = r.removeFromBottom (30);
    playButton.setBounds (foot.removeFromLeft (30).withSizeKeepingCentre (30, 28));
    foot.removeFromLeft (8);
    volumeLabel.setBounds (foot.removeFromRight (56));
    volume.setBounds (foot);
    r.removeFromBottom (4);
    waveform->setBounds (r.removeFromBottom (48));
    r.removeFromBottom (6);
    columnHeads = r.removeFromTop (16);
    list.setBounds (r);
}

} // namespace riff
