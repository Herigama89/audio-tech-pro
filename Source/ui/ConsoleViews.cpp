#include "ConsoleViews.h"
#include "ConsoleLook.h"
#include "EffectDesigns.h"
#include "Mixer.h"
#include "Theme.h"
#include "AnalogConsoleArt.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/MasterBusCompressor.h"
#include "../model/Identifiers.h"
#include "../model/Project.h"

namespace riff
{
namespace
{
    constexpr double minDb = -60.0;
    double gainToDb (double g) { return g <= 0.0 ? minDb : juce::jmax (minDb, juce::Decibels::gainToDecibels (g)); }
    double dbToGain (double db) { return db <= minDb + 0.01 ? 0.0 : juce::Decibels::decibelsToGain (db); }

    const juce::Colour amber { 0xfff2a43a }, green { 0xff3ddc84 }, pluginBlue { 0xff6e7cff };

    bool isPlugin (const juce::ValueTree& insert) { return insert[ids::kind].toString() == model::pluginKind; }

    juce::PluginDescription descriptionOf (const juce::ValueTree& insert)
    {
        juce::PluginDescription d;
        if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr)
            d.loadFromXml (*xml);
        return d;
    }

    juce::String nameOf (const juce::ValueTree& insert)
    {
        if (isPlugin (insert))
        {
            const auto d = descriptionOf (insert);
            return d.name.isNotEmpty() ? d.name : juce::String ("Plug-in");
        }
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            return info->name;
        return "?";
    }

    /** A chain's inserts in their slots' order. */
    juce::Array<juce::ValueTree> ordered (const juce::ValueTree& chain)
    {
        auto list = model::insertsOf (chain);
        std::stable_sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return model::insertSlot (a) < model::insertSlot (b); });
        return list;
    }

    /** The tracks a console shows: audio, instrument and bus channels (not VCAs), in order. */
    juce::Array<juce::ValueTree> channels (const juce::ValueTree& project)
    {
        juce::Array<juce::ValueTree> list;
        for (const auto& t : project)
            if (t.hasType (ids::TRACK) && ! model::isVcaTrack (t))
                list.add (t);
        return list;
    }

    void styleKey (juce::TextButton& b, bool slot = false)
    {
        look::key (b, slot ? look::Key::slot : look::Key::dark);
        b.setWantsKeyboardFocus (false);
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<float> r, float corner = 12.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
}

//==============================================================================
ScaledFace::ScaledFace (std::unique_ptr<juce::Component> f) : face (std::move (f))
{
    if (face != nullptr)
        addAndMakeVisible (*face);
}

int ScaledFace::heightForWidth (int width) const
{
    if (face == nullptr || face->getWidth() <= 0)
        return 60;
    return juce::roundToInt ((float) face->getHeight() * (float) width / (float) face->getWidth());
}

void ScaledFace::resized()
{
    if (face == nullptr || face->getWidth() <= 0)
        return;
    const auto scale = (float) getWidth() / (float) face->getWidth();
    face->setTopLeftPosition (0, 0);
    face->setTransform (juce::AffineTransform::scale (scale));
}

//==============================================================================
PluginCard::PluginCard (const juce::String& n, const juce::String& m, std::function<void()> o)
    : name (n), maker (m), open (std::move (o))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PluginCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2d33), r.getX(), r.getY(), juce::Colour (0xff1d1e22), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    auto icon = r.reduced (10.0f).removeFromLeft (juce::jmin (r.getHeight() - 20.0f, 40.0f)).withSizeKeepingCentre (juce::jmin (r.getHeight() - 20.0f, 40.0f), juce::jmin (r.getHeight() - 20.0f, 40.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b5cff), icon.getX(), icon.getY(), juce::Colour (0xff2d39b8), icon.getX(), icon.getBottom(), false));
    g.fillRoundedRectangle (icon, 8.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PLUG", icon, juce::Justification::centred, false);
    auto text = r.withTrimmedLeft (icon.getRight() - r.getX() + 10.0f).reduced (0.0f, 8.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (name, text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ((maker.isNotEmpty() ? maker + " - " : juce::String()) + "click to open its window", text.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
}

void PluginCard::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && open)
        open();
}

//==============================================================================
// Input FX
//==============================================================================
namespace
{
    const char* const modeNames[] { "Normal", "Low", "Ultra Low" };
    const char* const recordNames[] { "Dry", "Wet (print)", "Both" };
    const char* const recordValues[] { "dry", "wet", "both" };

    int modeOf (const juce::ValueTree& track)
    {
        if (! (bool) track.getProperty (ids::inputFxUltra, false))
            return 0;
        return (double) track.getProperty (ids::inputFxSkipMs, 1.0) > 1.5 ? 1 : 2;
    }
}

double InputFxPanel::roundTripMs (ConsoleHost& host, const juce::ValueTree& project, const juce::ValueTree& track)
{
    auto ms = host.deviceRoundTripMs ? host.deviceRoundTripMs() : 0.0;
    const auto rate = host.sampleRate ? host.sampleRate() : 48000.0;
    const auto ultra = (bool) track.getProperty (ids::inputFxUltra, false);
    const auto skip = (double) track.getProperty (ids::inputFxSkipMs, 1.0);
    if ((bool) track.getProperty (ids::inputFxOn, true))
        for (const auto& insert : model::insertsOf (model::findChain (project, model::inputOwner (track[ids::id].toString()))))
            if (! (bool) insert[ids::bypass] && host.latencyOf)
            {
                const auto pluginMs = host.latencyOf (insert[ids::id].toString()) * 1000.0 / rate;
                if (! ultra || pluginMs <= skip)
                    ms += pluginMs;
            }
    return ms;
}

InputFxPanel::InputFxPanel (ConsoleHost& h, juce::ValueTree p, juce::ValueTree t)
    : host (h), project (p), track (t)
{
    for (int i = 0; i < 3; ++i)
    {
        auto* m = modeKeys.add (new juce::TextButton (modeNames[i]));
        styleKey (*m);
        m->setClickingTogglesState (false);
        m->setColour (juce::TextButton::buttonOnColourId, amber);
        m->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        m->onClick = [this, i] { setMode (i); };
        addAndMakeVisible (m);

        auto* r = recordKeys.add (new juce::TextButton (recordNames[i]));
        styleKey (*r);
        r->setColour (juce::TextButton::buttonOnColourId, amber);
        r->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        r->onClick = [this, i] { setRecord (recordValues[i]); };
        addAndMakeVisible (r);
    }
    modeKeys[0]->setTooltip ("Normal: every plug-in on the input, whatever its latency");
    modeKeys[1]->setTooltip ("Low: while armed or monitoring, plug-ins slower than 3 ms are left out (back on when you play back)");
    modeKeys[2]->setTooltip ("Ultra Low: the interface's smallest buffer, and plug-ins slower than 1 ms left out while tracking");
    recordKeys[0]->setTooltip ("Dry: you hear the chain, the input is recorded as it comes");
    recordKeys[1]->setTooltip ("Wet: recorded through the chain (printed), as with a hardware preamp");
    recordKeys[2]->setTooltip ("Both: recorded through the chain, and the dry input kept beside it (right-click the region to swap)");

    styleKey (measureButton);
    measureButton.setTooltip ("Measure the real round trip through your interface (a cable from an output to an input)");
    measureButton.onClick = [this] { if (host.openLatencyCalibration) host.openLatencyCalibration(); };
    addAndMakeVisible (measureButton);

    styleKey (onButton);
    onButton.setClickingTogglesState (false);
    onButton.setColour (juce::TextButton::buttonOnColourId, amber);
    onButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
    onButton.onClick = [this]
    {
        host.undo->beginNewTransaction ("Input FX on/off");
        track.setProperty (ids::inputFxOn, ! (bool) track.getProperty (ids::inputFxOn, true), host.undo);
    };
    addAndMakeVisible (onButton);

    styleKey (addButton, true);
    addButton.onClick = [this]
    {
        int next = 0;
        for (const auto& insert : model::insertsOf (model::findChain (project, chainOwner())))
            next = juce::jmax (next, model::insertSlot (insert) + 1);
        if (host.chooseEffect)
            host.chooseEffect (chainOwner(), next, {}, addButton);
    };
    addAndMakeVisible (addButton);

    linkCaption.setText ("Interface preamp gain follows", juce::dontSendNotification);
    linkCaption.setColour (juce::Label::textColourId, theme::dimText);
    linkCaption.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (linkCaption);
    linkBox.onChange = [this]
    {
        const auto index = linkBox.getSelectedId() - 2;
        host.undo->beginNewTransaction ("Input FX gain link");
        if (juce::isPositiveAndBelow (index, linkChoices.size()))
            track.setProperty (ids::inputFxLink, linkChoices[index].first + "|" + linkChoices[index].second, host.undo);
        else
            track.removeProperty (ids::inputFxLink, host.undo);
    };
    addAndMakeVisible (linkBox);

    rowsView.setViewedComponent (&rowsHolder, false);
    rowsView.setScrollBarsShown (true, false);
    addAndMakeVisible (rowsView);

    project.addListener (this);
    setSize (980, 640);
    rebuild();
    startTimerHz (4);   // (the round trip, as the interface reports it)
}

InputFxPanel::~InputFxPanel()
{
    project.removeListener (this);
}

juce::String InputFxPanel::chainOwner() const { return model::inputOwner (track[ids::id].toString()); }

void InputFxPanel::setMode (int mode)
{
    host.undo->beginNewTransaction ("Input FX monitoring");
    track.setProperty (ids::inputFxUltra, mode > 0, host.undo);
    track.setProperty (ids::inputFxSkipMs, mode == 1 ? 3.0 : 1.0, host.undo);
    if (host.setUltraLowBuffer)
        host.setUltraLowBuffer (mode == 2);
}

void InputFxPanel::setRecord (const juce::String& what)
{
    host.undo->beginNewTransaction ("Input FX recording");
    track.setProperty (ids::inputFxRecord, what, host.undo);
}

void InputFxPanel::Row::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto buttons = r.removeFromRight (230);
    remove.setBounds (buttons.removeFromRight (70).withSizeKeepingCentre (70, 26));
    buttons.removeFromRight (6);
    edit.setBounds (buttons.removeFromRight (56).withSizeKeepingCentre (56, 26));
    buttons.removeFromRight (6);
    on.setBounds (buttons.removeFromRight (46).withSizeKeepingCentre (46, 26));
    buttons.removeFromRight (6);
    auto arrows = buttons.removeFromRight (40);
    up.setBounds (arrows.removeFromTop (arrows.getHeight() / 2).reduced (0, 2));
    down.setBounds (arrows.reduced (0, 2));
    latency.setBounds (r.removeFromRight (90));
    if (picture != nullptr)
    {
        auto p = r.removeFromLeft (260);
        if (auto* face = dynamic_cast<ScaledFace*> (picture.get()))
            p = p.withSizeKeepingCentre (p.getWidth(), juce::jmin (p.getHeight(), face->heightForWidth (p.getWidth())));
        picture->setBounds (p);
        r.removeFromLeft (14);
    }
    name.setBounds (r.removeFromTop (r.getHeight() / 2));
    detail.setBounds (r);
}

void InputFxPanel::Row::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));
}

void InputFxPanel::rebuild()
{
    const auto mode = modeOf (track);
    for (int i = 0; i < 3; ++i)
        modeKeys[i]->setToggleState (i == mode, juce::dontSendNotification);
    const auto record = track.getProperty (ids::inputFxRecord, "wet").toString();
    for (int i = 0; i < 3; ++i)
        recordKeys[i]->setToggleState (record == recordValues[i], juce::dontSendNotification);
    const auto on = (bool) track.getProperty (ids::inputFxOn, true);
    onButton.setToggleState (on, juce::dontSendNotification);
    onButton.setButtonText (on ? "Input FX On" : "Input FX Off");

    const auto inserts = ordered (model::findChain (project, chainOwner()));

    // The knob that turns the interface's preamp (the chain's built-in effects' continuous controls)
    linkBox.clear (juce::dontSendNotification);
    linkChoices.clear();
    linkBox.addItem ("Not linked", 1);
    const auto link = track[ids::inputFxLink].toString();
    int chosen = 1;
    for (const auto& insert : inserts)
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            for (const auto& param : info->params)
                if (! param.isChoice())
                {
                    linkChoices.add ({ insert[ids::id].toString(), param.id });
                    const auto id = linkChoices.size() + 1;
                    linkBox.addItem (info->name + ": " + param.name, id);
                    if (link == insert[ids::id].toString() + "|" + param.id)
                        chosen = id;
                }
    linkBox.setSelectedId (chosen, juce::dontSendNotification);

    rows.clear();
    const auto rate = host.sampleRate ? host.sampleRate() : 48000.0;
    const auto ultra = (bool) track.getProperty (ids::inputFxUltra, false);
    const auto skip = (double) track.getProperty (ids::inputFxSkipMs, 1.0);
    for (int i = 0; i < inserts.size(); ++i)
    {
        auto insert = inserts[i];
        auto* row = rows.add (new Row());
        row->insert = insert;
        const auto plugin = isPlugin (insert);
        if (! plugin && host.makeFace)
            if (auto face = host.makeFace (insert))
                row->picture = std::make_unique<ScaledFace> (std::move (face));
        if (row->picture == nullptr)
        {
            const auto d = descriptionOf (insert);
            row->picture = std::make_unique<PluginCard> (nameOf (insert), d.manufacturerName, [this, insert] { if (host.openInsert) host.openInsert (insert); });
        }
        row->addAndMakeVisible (*row->picture);
        row->name.setText (nameOf (insert), juce::dontSendNotification);
        row->name.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        row->name.setColour (juce::Label::textColourId, theme::text);
        const auto ms = host.latencyOf ? host.latencyOf (insert[ids::id].toString()) * 1000.0 / rate : 0.0;
        const auto skipped = ultra && ms > skip;
        juce::String detail = plugin ? descriptionOf (insert).pluginFormatName + " plug-in" : juce::String ("Built-in");
        if (skipped)
            detail << " - left out while tracking (adds " << juce::String (ms, 1) << " ms), back on when you stop";
        row->detail.setText (detail, juce::dontSendNotification);
        row->detail.setColour (juce::Label::textColourId, skipped ? amber : theme::dimText);
        row->latency.setText (juce::String (ms, 2) + " ms", juce::dontSendNotification);
        row->latency.setJustificationType (juce::Justification::centredRight);
        row->latency.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        row->latency.setColour (juce::Label::textColourId, skipped ? amber : green);
        for (auto* b : { &row->up, &row->down, &row->on, &row->edit, &row->remove })
        {
            styleKey (*b);
            row->addAndMakeVisible (b);
        }
        for (auto* l : { &row->name, &row->detail, &row->latency })
            row->addAndMakeVisible (l);
        row->on.setColour (juce::TextButton::buttonOnColourId, amber);
        row->on.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        row->on.setToggleState (! (bool) insert[ids::bypass], juce::dontSendNotification);
        row->on.onClick = [this, insert]() mutable
        {
            host.undo->beginNewTransaction ("Input FX bypass");
            insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], host.undo);
        };
        row->edit.onClick = [this, insert] { if (host.openInsert) host.openInsert (insert); };
        row->remove.onClick = [this, insert]
        {
            host.undo->beginNewTransaction ("Remove input FX");
            model::removeInsert (insert, host.undo);
        };
        // (moving: the slots swap with the neighbour's)
        const auto swapWith = [this, inserts] (int a, int b)
        {
            if (! juce::isPositiveAndBelow (a, inserts.size()) || ! juce::isPositiveAndBelow (b, inserts.size()))
                return;
            auto x = inserts[a], y = inserts[b];
            const auto sx = model::insertSlot (x), sy = model::insertSlot (y);
            host.undo->beginNewTransaction ("Move input FX");
            x.setProperty (ids::slot, sy, host.undo);
            y.setProperty (ids::slot, sx, host.undo);
            auto chain = x.getParent();
            if (const auto ix = chain.indexOf (x), iy = chain.indexOf (y); ix >= 0 && iy >= 0)
                chain.moveChild (ix, iy, host.undo);
        };
        row->up.setEnabled (i > 0);
        row->down.setEnabled (i < inserts.size() - 1);
        row->up.onClick = [swapWith, i] { swapWith (i, i - 1); };
        row->down.onClick = [swapWith, i] { swapWith (i, i + 1); };
        rowsHolder.addAndMakeVisible (row);
    }
    resized();
    repaint();
}

void InputFxPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151619));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("Input FX - " + track[ids::name].toString(), getLocalBounds().reduced (16, 0).removeFromTop (44), juce::Justification::centredLeft, false);

    const char* const titles[] { "ROUND TRIP", "MONITORING", "RECORD" };
    for (int i = 0; i < 3; ++i)
    {
        drawCard (g, cards[i].toFloat());
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (titles[i], cards[i].reduced (14, 10).removeFromTop (14), juce::Justification::centredLeft, false);
    }
    // The round trip: the interface's, and the plug-ins' (left out ones not counted)
    const auto total = roundTripMs (host, project, track);
    const auto device = host.deviceRoundTripMs ? host.deviceRoundTripMs() : 0.0;
    auto c = cards[0].reduced (14, 10);
    c.removeFromTop (16);
    g.setColour (green);
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    auto big = c.removeFromTop (48);
    g.drawText (juce::String (total, 2), big, juce::Justification::centredLeft, false);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("ms", big.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (40.0f, juce::Font::bold)), juce::String (total, 2)) + 8),
                juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("interface " + juce::String (device, 2) + " ms + plug-ins " + juce::String (juce::jmax (0.0, total - device), 2) + " ms",
                c.removeFromTop (18), juce::Justification::centredLeft, false);

    auto m = cards[1].reduced (14, 10);
    m.removeFromTop (56);
    g.setFont (juce::FontOptions (12.0f));
    const auto mode = modeOf (track);
    g.drawFittedText (mode == 2 ? "The interface's smallest buffer for the input; the session plays ahead (Process Buffer Range). "
                                  "Plug-ins slower than 1 ms left out while tracking."
                      : mode == 1 ? "Plug-ins slower than 3 ms left out while you're armed or monitoring."
                                  : "Every plug-in on the input, whatever its latency.",
                      m, juce::Justification::topLeft, 3);
}

void InputFxPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    auto top = r.removeFromTop (44);
    onButton.setBounds (top.removeFromRight (120).withSizeKeepingCentre (120, 28));
    topArea = r.removeFromTop (150);
    auto t = topArea.withTrimmedBottom (12);
    const auto w = (t.getWidth() - 24) / 3;
    cards[0] = t.removeFromLeft (w);
    t.removeFromLeft (12);
    cards[1] = t.removeFromLeft (w);
    t.removeFromLeft (12);
    cards[2] = t;
    measureButton.setBounds (cards[0].getRight() - 100, cards[0].getY() + 8, 90, 24);
    auto keys = [] (juce::OwnedArray<juce::TextButton>& list, juce::Rectangle<int> area)
    {
        const auto kw = area.getWidth() / list.size();
        for (auto* k : list)
            k->setBounds (area.removeFromLeft (kw).reduced (2, 0));
    };
    keys (modeKeys, cards[1].reduced (14, 0).withTrimmedTop (30).withHeight (28));
    keys (recordKeys, cards[2].reduced (14, 0).withTrimmedTop (30).withHeight (28));
    linkCaption.setBounds (cards[2].reduced (14, 0).withTrimmedTop (68).withHeight (20));
    linkBox.setBounds (cards[2].reduced (14, 0).withTrimmedTop (90).withHeight (26));

    addButton.setBounds (r.removeFromBottom (44).reduced (0, 6));
    rowsView.setBounds (r);
    const auto rowHeight = 96;
    rowsHolder.setSize (rowsView.getWidth() - rowsView.getScrollBarThickness(), juce::jmax (1, rows.size() * (rowHeight + 8)));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * (rowHeight + 8), rowsHolder.getWidth(), rowHeight);
}

//==============================================================================
// Console mode
//==============================================================================
ConsoleView::ConsoleView (ConsoleHost& h, juce::ValueTree p) : host (h), project (p)
{
    for (const auto n : { 4, 5, 8 })
    {
        auto* k = fitKeys.add (new juce::TextButton ("Fit " + juce::String (n)));
        styleKey (*k);
        k->setColour (juce::TextButton::buttonOnColourId, amber);
        k->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        k->onClick = [this, n] { fit = n; for (auto* f : fitKeys) f->setToggleState (f->getButtonText() == "Fit " + juce::String (n), juce::dontSendNotification); layout(); };
        k->setToggleState (n == fit, juce::dontSendNotification);
        addAndMakeVisible (k);
    }
    const char* const shows[] { "Input FX", "Strip", "Inserts" };
    for (int i = 0; i < 3; ++i)
    {
        auto* k = showKeys.add (new juce::TextButton (shows[i]));
        styleKey (*k);
        k->setColour (juce::TextButton::buttonOnColourId, amber);
        k->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        k->setToggleState (true, juce::dontSendNotification);
        k->onClick = [this, i, k]
        {
            k->setToggleState (! k->getToggleState(), juce::dontSendNotification);
            (i == 0 ? showInput : i == 1 ? showStrip : showInserts) = k->getToggleState();
            rebuild();
        };
        addAndMakeVisible (k);
    }
    view.setViewedComponent (&holder, false);
    addAndMakeVisible (view);
    project.addListener (this);
    setSize (1500, 900);
    rebuild();
}

ConsoleView::~ConsoleView()
{
    project.removeListener (this);
    for (auto* c : columns)
        if (c->strip != nullptr && host.mixer != nullptr)
            host.mixer->removeExtraStrip (c->strip.get());
}

void ConsoleView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // A track renamed, recoloured or moved; an insert added, bypassed or replaced
    if ((tree.hasType (ids::TRACK) && (property == ids::name || property == ids::colour))
        || (tree.hasType (ids::INSERT) && (property == ids::effect || property == ids::pluginDescription || property == ids::kind)))
        triggerAsyncUpdate();
}

int ConsoleView::Column::facesHeight (int width) const
{
    int h = 0;
    for (auto* s : sections)
    {
        h += 22;
        for (auto* f : s->faces)
            h += (dynamic_cast<ScaledFace*> (f) != nullptr ? static_cast<ScaledFace*> (f)->heightForWidth (width) : 64) + 6;
        if (s->add != nullptr)
            h += 30;
    }
    return h + 6;
}

void ConsoleView::Column::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e1f22), 0.0f, r.getY(), juce::Colour (0xff161719), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    auto head = getLocalBounds().reduced (12, 0).removeFromTop (36);
    g.setColour (juce::Colour (0xff2a2b30));
    g.fillRoundedRectangle (head.removeFromLeft (24).withSizeKeepingCentre (24, 22).toFloat(), 6.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (number, getLocalBounds().reduced (12, 0).removeFromTop (36).removeFromLeft (24), juce::Justification::centred, false);
    head.removeFromLeft (8);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    const auto w = juce::jmin (head.getWidth() - 20, juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), name) + 4);
    g.drawFittedText (name, head.removeFromLeft (w), juce::Justification::centredLeft, 1, 0.8f);
    head.removeFromLeft (8);
    g.setColour (colour);
    g.fillRoundedRectangle (head.withSizeKeepingCentre (head.getWidth(), 4).toFloat(), 2.0f);
}

void ConsoleView::Column::Faces::paint (juce::Graphics& g)
{
    g.setFont (juce::FontOptions (10.5f));
    for (auto* s : column->sections)
    {
        g.setColour (theme::dimText);
        g.drawText (s->title, s->titleArea, juce::Justification::centredLeft, false);
        if (s->owner.startsWith ("input:"))
        {
            g.setColour (juce::Colour (0xff3ddc84));
            g.drawText ("Input FX: before the recorder", s->titleArea, juce::Justification::centredRight, false);
        }
    }
}

void ConsoleView::rebuild()
{
    for (auto* c : columns)
        if (c->strip != nullptr && host.mixer != nullptr)
            host.mixer->removeExtraStrip (c->strip.get());
    columns.clear();
    if (host.mixer == nullptr)
        return;

    int index = 0;
    for (const auto& track : channels (project))
    {
        ++index;
        auto* column = columns.add (new Column());
        column->track = track;
        column->number = juce::String (index);
        column->name = track[ids::name].toString();
        column->colour = model::trackColour (track);
        const auto id = track[ids::id].toString();
        const auto audio = ! model::isInstrumentTrack (track) && ! model::isBusTrack (track);

        auto addSection = [this, column] (const juce::String& title, const juce::String& owner, bool canAdd)
        {
            auto* s = column->sections.add (new Column::Section());
            s->title = title;
            s->owner = owner;
            for (const auto& insert : ordered (model::findChain (project, owner)))
            {
                std::unique_ptr<juce::Component> face;
                if (! isPlugin (insert) && host.makeFace)
                    if (auto f = host.makeFace (insert))
                        face = std::make_unique<ScaledFace> (std::move (f));
                if (face == nullptr)
                    face = std::make_unique<PluginCard> (nameOf (insert), descriptionOf (insert).manufacturerName,
                                                         [this, insert] { if (host.openInsert) host.openInsert (insert); });
                column->faces.addAndMakeVisible (*face);
                s->faces.add (face.release());
                auto* t = s->titles.add (new juce::TextButton (nameOf (insert)));
                t->setTooltip ("Open " + nameOf (insert) + " in its own window");
                t->onClick = [this, insert] { if (host.openInsert) host.openInsert (insert); };
            }
            if (canAdd)
            {
                s->add = std::make_unique<juce::TextButton> ("+ add");
                styleKey (*s->add, true);
                s->add->setTooltip ("Add a plug-in here");
                auto* button = s->add.get();
                s->add->onClick = [this, owner, button]
                {
                    int next = 0;
                    for (const auto& insert : model::insertsOf (model::findChain (project, owner)))
                        next = juce::jmax (next, model::insertSlot (insert) + 1);
                    if (host.chooseEffect)
                        host.chooseEffect (owner, next, {}, *button);
                };
                column->faces.addAndMakeVisible (*s->add);
            }
        };
        if (showInput && audio)
            addSection ("INPUT FX", model::inputOwner (id), true);
        if (showStrip && model::findChain (project, model::stripOwner (id)).isValid() && ! model::insertsOf (model::findChain (project, model::stripOwner (id))).isEmpty())
            addSection ("CHANNEL STRIP", model::stripOwner (id), false);
        if (showInserts)
            addSection ("INSERTS", id, true);

        column->strip = std::make_unique<ChannelStrip> (*host.mixer, project, track);
        column->addAndMakeVisible (*column->strip);
        host.mixer->addExtraStrip (column->strip.get());
        holder.addAndMakeVisible (column);
    }
    layout();
}

void ConsoleView::layout()
{
    if (host.mixer == nullptr)
        return;
    const auto columnWidth = juce::jlimit (220, 420, (view.getWidth() - 8) / juce::jmax (1, fit));
    const auto faceWidth = columnWidth - 24;
    const auto stripHeight = host.mixer->stripMinimumHeight();
    int faces = 0;
    for (auto* c : columns)
        faces = juce::jmax (faces, c->facesHeight (faceWidth));
    juce::ignoreUnused (faces);
    // The fader in sight: the faces scroll in their own space above the strip (at least 220 high)
    const auto height = 40 + 220 + stripHeight + 12;
    holder.setSize (juce::jmax (view.getWidth() - 2, columns.size() * (columnWidth + 8)), juce::jmax (view.getHeight() - view.getScrollBarThickness(), height));
    for (int i = 0; i < columns.size(); ++i)
    {
        auto* c = columns[i];
        c->setBounds (i * (columnWidth + 8), 0, columnWidth, holder.getHeight());
        c->facesView.setBounds (0, 40, columnWidth - 3, c->getHeight() - 40 - stripHeight - 12);
        c->faces.setSize (c->facesView.getWidth(), juce::jmax (c->facesView.getHeight(), c->facesHeight (faceWidth)));
        int y = 0;
        for (auto* s : c->sections)
        {
            // (the section's title is painted by the column; its faces under it)
            s->titleArea = { 14, y + 4, faceWidth - 4, 16 };   // (in the faces' space)
            y += 22;
            for (auto* f : s->faces)
            {
                const auto h = dynamic_cast<ScaledFace*> (f) != nullptr ? static_cast<ScaledFace*> (f)->heightForWidth (faceWidth) : 64;
                f->setBounds (12, y, faceWidth, h);
                y += h + 6;
            }
            if (s->add != nullptr)
            {
                s->add->setBounds (12, y, faceWidth, 24);
                y += 30;
            }
        }
        c->strip->setBounds ((columnWidth - ChannelStrip::stripWidth) / 2, c->getHeight() - stripHeight - 6, ChannelStrip::stripWidth, stripHeight);
        c->faces.repaint();
        c->repaint();
    }
    repaint();
}

void ConsoleView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111214));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Console", 16, 0, 120, 44, juce::Justification::centredLeft, false);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("every channel's Input FX, channel strip and inserts, live, above its fader", 120, 0, 600, 44, juce::Justification::centredLeft, false);
}

void ConsoleView::resized()
{
    auto top = getLocalBounds().removeFromTop (44).reduced (12, 8);
    for (int i = fitKeys.size(); --i >= 0;)
    {
        fitKeys[i]->setBounds (top.removeFromRight (64));
        top.removeFromRight (4);
    }
    top.removeFromRight (16);
    for (int i = showKeys.size(); --i >= 0;)
    {
        showKeys[i]->setBounds (top.removeFromRight (80));
        top.removeFromRight (4);
    }
    view.setBounds (getLocalBounds().withTrimmedTop (44).reduced (6, 0).withTrimmedBottom (6));
    layout();
}

//==============================================================================
// The Analog Console
//==============================================================================
AnalogKnob::AnalogKnob (const juce::String& colour, const juce::String& c) : caption (c)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setPopupDisplayEnabled (true, true, nullptr);
    setWantsKeyboardFocus (false);
    strip = effectdesign::skinImage (("ac_knob_" + colour + "_png").toRawUTF8());
}

void AnalogKnob::paint (juce::Graphics& g)
{
    if (! strip.isValid())
        return;
    const auto frames = analogart::knobFrames;
    const auto pos = (float) valueToProportionOfLength (getValue());
    const auto k = juce::jlimit (0, frames - 1, juce::roundToInt (pos * (float) (frames - 1)));
    const auto size = juce::jmin (getWidth(), getHeight());
    const auto frame = strip.getWidth();
    const auto dest = juce::Rectangle<int> (size, size).withCentre (getLocalBounds().getCentre());
    g.setOpacity (isEnabled() ? 1.0f : 0.45f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (strip, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(), 0, k * frame, frame, frame);
}

AnalogConsole::AnalogConsole (ConsoleHost& h, juce::ValueTree p) : host (h), project (p)
{
    const char* const names[] { "All", "Audio", "Buses" };
    for (int i = 0; i < 3; ++i)
    {
        auto* k = filterKeys.add (new juce::TextButton (names[i]));
        styleKey (*k);
        k->setColour (juce::TextButton::buttonOnColourId, amber);
        k->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        k->setToggleState (i == 0, juce::dontSendNotification);
        k->onClick = [this, i] { filter = i; for (int j = 0; j < filterKeys.size(); ++j) filterKeys[j]->setToggleState (j == i, juce::dontSendNotification); rebuild(); };
        addAndMakeVisible (k);
    }
    view.setViewedComponent (&holder, false);
    addAndMakeVisible (view);
    project.addListener (this);
    setSize (1500, 980);
    rebuild();
    startTimerHz (30);
}

AnalogConsole::~AnalogConsole()
{
    project.removeListener (this);
}

void AnalogConsole::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (ids::TRACK) && (property == ids::name || property == ids::colour))
        rebuildWanted = true;
    triggerAsyncUpdate();
}

void AnalogConsole::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    rebuildWanted = rebuildWanted || parent == project || parent.hasType (ids::CHAIN) || parent.hasType (ids::MIXER);
    triggerAsyncUpdate();
}

void AnalogConsole::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    rebuildWanted = rebuildWanted || parent == project || parent.hasType (ids::CHAIN) || parent.hasType (ids::MIXER);
    triggerAsyncUpdate();
}

void AnalogConsole::handleAsyncUpdate()
{
    if (rebuildWanted)
    {
        rebuild();
        return;
    }
    for (auto* s : strips)
        refreshBindings (*s);
    if (master != nullptr)
        refreshBindings (*master);
}

void AnalogConsole::refreshBindings (Strip& s)
{
    for (auto& b : s.bindings)
    {
        const auto v = b.tree.getProperty (b.property, b.fallback);
        if (auto* slider = dynamic_cast<juce::Slider*> (b.control))
        {
            if (! slider->isMouseButtonDown())
                slider->setValue ((double) v, juce::dontSendNotification);
        }
        else if (auto* button = dynamic_cast<juce::Button*> (b.control))
            button->setToggleState ((bool) v, juce::dontSendNotification);
    }
    if (! s.isMaster && ! s.fader.isMouseButtonDown())
        s.fader.setValue (gainToDb ((double) s.track.getProperty (ids::volume, 1.0)), juce::dontSendNotification);
    if (s.isMaster && ! s.fader.isMouseButtonDown())
        s.fader.setValue (gainToDb ((double) project.getProperty (ids::masterGain, 0.8)), juce::dontSendNotification);
    const auto db = s.fader.getValue();
    s.level.setText (db <= minDb + 0.01 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
    s.repaint();
}

void AnalogConsole::buildStrip (Strip& s)
{
    auto& um = *host.undo;
    const auto track = s.track;
    int y = 34;
    const int w = columnWidth;
    const auto knob = [&] (const juce::String& colour, const juce::String& caption, const juce::Identifier& property, double fallback,
                           double lo, double hi, double mid, juce::Rectangle<int> where, const juce::String& unit = {}) -> AnalogKnob&
    {
        auto* k = new AnalogKnob (colour, caption);
        k->setRange (lo, hi, 0.0);
        if (mid > lo && mid < hi)
            k->setSkewFactorFromMidPoint (mid);
        k->setDoubleClickReturnValue (true, fallback);
        k->setValue ((double) track.getProperty (property, fallback), juce::dontSendNotification);
        k->setTooltip (caption);
        k->textFromValueFunction = [unit] (double v) { return juce::String (v, std::abs (v) < 10.0 ? 1 : 0) + unit; };
        auto tree = track;
        k->onDragStart = [&um, caption] { um.beginNewTransaction ("Analog Console: " + caption); };
        k->onValueChange = [k, tree, property, &um]() mutable { tree.setProperty (property, k->getValue(), &um); };
        k->setBounds (where);
        s.addAndMakeVisible (k);
        s.parts.add (k);
        s.bindings.push_back ({ k, track, property, fallback, false });
        return *k;
    };
    const auto toggle = [&] (const juce::String& text, const juce::Identifier& property, bool fallback, juce::Rectangle<int> where,
                             juce::Colour on = amber, const juce::String& tip = {})
    {
        auto* b = new juce::TextButton (text);
        styleKey (*b);
        b->setColour (juce::TextButton::buttonOnColourId, on);
        b->setColour (juce::TextButton::textColourOnId, on.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1a1206) : juce::Colours::white);
        b->setToggleState ((bool) track.getProperty (property, fallback), juce::dontSendNotification);
        b->setTooltip (tip.isNotEmpty() ? tip : text);
        auto tree = track;
        b->onClick = [tree, property, fallback, &um]() mutable
        {
            um.beginNewTransaction ("Analog Console");
            tree.setProperty (property, ! (bool) tree.getProperty (property, fallback), &um);
        };
        b->setBounds (where);
        s.addAndMakeVisible (b);
        s.parts.add (b);
        s.bindings.push_back ({ b, track, property, fallback ? 1.0 : 0.0, true });
    };
    const auto pair = [&] (int row) { return std::make_pair (juce::Rectangle<int> (11, row, 36, 36), juce::Rectangle<int> (w - 47, row, 36, 36)); };
    const auto keys = [&] (int row) { return std::make_pair (juce::Rectangle<int> (6, row, w / 2 - 8, 17), juce::Rectangle<int> (w / 2 + 2, row, w / 2 - 8, 17)); };
    const auto section = [&] (const juce::String& title) { s.sectionTitles.push_back ({ title, juce::Rectangle<int> (0, y, w, 14) }); y += 15; };

    // Input
    section ("INPUT");
    {
        auto [a, b] = pair (y);
        knob ("grey", "Mic gain", ids::consoleMicGain, 20.0, 20.0, 70.0, 0.0, a, " dB");
        knob ("grey", "Trim", ids::consoleInput, 0.0, -20.0, 20.0, 0.0, b, " dB");
        y += 47;
        auto [c, d] = keys (y);
        toggle (juce::CharPointer_UTF8 ("\xc3\x98"), ids::consolePhase, false, c, amber, "Phase: the polarity turned over");
        toggle ("LINE", ids::consoleLine, true, d, amber, "Line in (off: the mic preamp, its gain and colour)");
        y += 20;
        auto [e, f] = keys (y);
        toggle ("PAD", ids::consolePad, false, e, amber, "Pad: -20 dB in");
        toggle ("IN", ids::stripIn, true, f, green, "The whole channel strip in");
        y += 24;
    }
    section ("FILTERS");
    {
        auto [a, b] = pair (y);
        knob ("grey", "High-pass", ids::consoleHPF, 0.0, 0.0, 500.0, 80.0, a, " Hz");
        knob ("grey", "Low-pass", ids::consoleLPF, 0.0, 0.0, 22000.0, 6000.0, b, " Hz");
        y += 52;
    }
    section ("DYNAMICS");
    {
        auto [a, b] = pair (y);
        knob ("white", "Ratio (0: auto)", ids::consoleCompRatio, 0.0, 0.0, 20.0, 4.0, a, ":1");
        knob ("white", "Compression", ids::consoleDynamics, 0.0, 0.0, 10.0, 0.0, b);
        y += 47;
        auto [c, d] = pair (y);
        knob ("white", "Release", ids::consoleCompRelease, 0.15, 0.05, 4.0, 0.4, c, " s");
        s.grArea = d.reduced (6, 4);
        y += 47;
        auto [e, f] = keys (y);
        toggle ("FAST", ids::consoleCompFast, false, e);
        toggle ("KNEE", ids::consoleCompKnee, false, f, amber, "Soft knee");
        y += 21;
        auto [g1, h1] = pair (y);
        knob ("green", "Gate range", ids::consoleGateRange, 20.0, 0.0, 40.0, 0.0, g1, " dB");
        knob ("green", "Gate threshold (-80: off)", ids::consoleGateThreshold, -80.0, -80.0, 0.0, 0.0, h1, " dB");
        y += 47;
        auto [i1, j1] = keys (y);
        toggle ("EXP", ids::consoleGateExpander, false, i1, amber, "Expander instead of a gate");
        toggle ("DYN IN", ids::consoleDynIn, true, j1, green);
        y += 24;
    }
    section ("EQ");
    {
        struct Band { const char* colour; const char* name; juce::Identifier gain, freq; double f0, f1, fmid, fdef; };
        const Band bands[] { { "red", "HF", ids::consoleHF, ids::consoleHFFreq, 1500.0, 16000.0, 5000.0, 8000.0 },
                             { "emerald", "HMF", ids::consoleHMF, ids::consoleHMFFreq, 600.0, 7000.0, 2000.0, 2500.0 },
                             { "blue", "LMF", ids::consoleLMF, ids::consoleLMFFreq, 200.0, 2500.0, 700.0, 400.0 },
                             { "brown", "LF", ids::consoleLF, ids::consoleLFFreq, 30.0, 450.0, 120.0, 100.0 } };
        for (const auto& band : bands)
        {
            auto [a, b] = pair (y);
            knob (band.colour, juce::String (band.name) + " gain", band.gain, 0.0, -20.0, 20.0, 0.0, a, " dB");
            knob (band.colour, juce::String (band.name) + " frequency", band.freq, band.fdef, band.f0, band.f1, band.fmid, b, " Hz");
            y += 47;
            if (juce::String (band.name) == "HMF" || juce::String (band.name) == "LMF")
            {
                knob (band.colour, juce::String (band.name) + " Q", juce::String (band.name) == "HMF" ? ids::consoleHMFQ : ids::consoleLMFQ,
                      1.0, 0.1, 4.0, 1.0, juce::Rectangle<int> (w / 2 - 13, y - 2, 26, 26));
                y += 37;
            }
        }
        auto [c, d] = keys (y);
        toggle ("BELL", ids::consoleHFBell, false, c, amber, "HF and LF as bells (else shelves)");
        toggle ("EQ IN", ids::consoleIn, true, d, green);
        y += 24;
    }
    section ("INSERTS");
    {
        const auto inserts = ordered (model::findChain (project, track[ids::id].toString()));
        for (int i = 0; i < 2; ++i)
        {
            auto* b = new juce::TextButton (i < inserts.size() ? nameOf (inserts[i]) : juce::String());
            styleKey (*b, true);
            if (i < inserts.size())
            {
                b->setColour (juce::TextButton::buttonColourId, isPlugin (inserts[i]) ? pluginBlue : amber);
                b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff141006));
                auto insert = inserts[i];
                b->onClick = [this, insert] { if (host.openInsert) host.openInsert (insert); };
            }
            else
            {
                const auto owner = track[ids::id].toString();
                int next = 0;
                for (const auto& insert : inserts)
                    next = juce::jmax (next, model::insertSlot (insert) + 1);
                b->onClick = [this, owner, next, b] { if (host.chooseEffect) host.chooseEffect (owner, next, {}, *b); };
                b->setTooltip ("Add an insert");
            }
            b->setBounds (6, y, w - 12, 17);
            s.addAndMakeVisible (b);
            s.parts.add (b);
            y += 19;
        }
        y += 4;
    }
    section ("SENDS");
    {
        const auto sends = model::sendsOf (model::findChain (project, track[ids::id].toString()));
        for (int row = 0; row < 2; ++row)
        {
            for (int c = 0; c < 2; ++c)
            {
                const auto n = row * 2 + c;
                auto* k = new AnalogKnob ("gold", "Send " + juce::String (n + 1));
                k->setRange (minDb, 6.0, 0.0);
                k->setSkewFactorFromMidPoint (-12.0);
                k->setDoubleClickReturnValue (true, -20.0);
                k->textFromValueFunction = [] (double v) { return v <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
                k->setBounds (c == 0 ? 13 : w - 45, y, 32, 32);
                if (n < sends.size())
                {
                    auto send = sends[n];
                    const auto bus = model::findById (project, send[ids::target].toString());
                    k->caption = bus.isValid() ? bus[ids::name].toString() : "Send " + juce::String (n + 1);
                    k->setTooltip ("Send to " + k->caption);
                    k->setValue (gainToDb ((double) send.getProperty (ids::level, 0.0)), juce::dontSendNotification);
                    k->onDragStart = [&um] { um.beginNewTransaction ("Send level"); };
                    k->onValueChange = [k, send, &um]() mutable { send.setProperty (ids::level, dbToGain (k->getValue()), &um); };
                }
                else
                {
                    k->setEnabled (false);
                    k->setTooltip ("No send here (add one in the mixer)");
                }
                s.addAndMakeVisible (k);
                s.parts.add (k);
            }
            y += 46;
        }
    }
    // Pan and the keys
    {
        auto& pan = knob ("grey", "Pan", ids::pan, 0.0, -1.0, 1.0, 0.0, juce::Rectangle<int> (w / 2 - 17, y, 34, 34));
        pan.textFromValueFunction = [] (double v)
        {
            const auto n = juce::roundToInt (v * 63.0);
            return n == 0 ? juce::String ("C") : n < 0 ? juce::String (-n) + " L" : juce::String (n) + " R";
        };
        y += 48;
        auto [a, b] = keys (y);
        toggle ("M", ids::mute, false, a, juce::Colour (0xff3a6df0), "Mute");
        toggle ("S", ids::solo, false, b, amber, "Solo");
        y += 20;
        auto [c, d] = keys (y);
        if (! model::isBusTrack (track))
        {
            toggle ("R", ids::armed, false, c, juce::Colour (0xffd9453b), "Record");
            toggle ("I", ids::monitor, false, d, juce::Colour (0xff2fbf71), "Input monitoring");
        }
        y += 24;
    }
    s.faderArea = { 4, y, w - 8, 230 };
}

void AnalogConsole::buildMaster (Strip& s)
{
    auto& um = *host.undo;
    const int w = masterWidth;
    int y = 34;
    // the VU sits at the top (painted), then the bus compressor and monitor sections
    y += 160 + 12;
    s.sectionTitles.push_back ({ "BUS COMPRESSOR", juce::Rectangle<int> (0, y, w, 14) });
    y += 16;
    using C = MasterBusCompressor;
    struct K { const char* caption; juce::Identifier property; double fallback, lo, hi, mid; std::function<juce::String (double)> text; };
    const K row1[] { { "Threshold", ids::busCompThreshold, -10.0, C::minThresholdDb, C::maxThresholdDb, -10.0, [] (double v) { return juce::String (v, 1) + " dB"; } },
                     { "Ratio", ids::busCompRatio, 1.0, 0.0, (double) C::ratios.size() - 1, 0.0, [] (double v) { return C::ratioText (juce::roundToInt (v)) + ":1"; } },
                     { "Make-up", ids::busCompMakeup, 0.0, 0.0, C::maxMakeupDb, 0.0, [] (double v) { return "+" + juce::String (v, 1) + " dB"; } } };
    const K row2[] { { "Attack", ids::busCompAttack, 5.0, 0.0, (double) C::attacksMs.size() - 1, 0.0, [] (double v) { return C::attackText (juce::roundToInt (v)) + " ms"; } },
                     { "Release", ids::busCompRelease, (double) C::autoRelease, 0.0, (double) C::releasesS.size() - 1, 0.0,
                       [] (double v) { return juce::roundToInt (v) == C::autoRelease ? juce::String ("Auto") : C::releaseText (juce::roundToInt (v)) + " s"; } },
                     { "Mix", ids::busCompMix, 1.0, 0.0, 1.0, 0.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "% wet"; } } };
    for (const auto* row : { row1, row2 })
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto& def = row[i];
            auto* k = new AnalogKnob ("white", def.caption);
            const auto stepped = def.property == ids::busCompRatio || def.property == ids::busCompAttack || def.property == ids::busCompRelease;
            k->setRange (def.lo, def.hi, stepped ? 1.0 : 0.0);
            k->setDoubleClickReturnValue (true, def.fallback);
            k->textFromValueFunction = def.text;
            k->setValue ((double) project.getProperty (def.property, def.fallback), juce::dontSendNotification);
            k->setTooltip (def.caption);
            auto tree = project;
            const auto property = def.property;
            k->onDragStart = [&um] { um.beginNewTransaction ("Bus compressor"); };
            k->onValueChange = [k, tree, property, &um] { MasterBusCompressor::setInProject (tree, property, k->getValue(), &um); };
            k->setBounds (18 + i * ((w - 36) / 3) + ((w - 36) / 3 - 56) / 2, y, 56, 56);
            s.addAndMakeVisible (k);
            s.parts.add (k);
            s.bindings.push_back ({ k, project, def.property, def.fallback, false });
        }
        y += 76;
    }
    const auto key = [&] (const juce::String& text, const juce::Identifier& property, bool fallback, juce::Rectangle<int> where)
    {
        auto* b = new juce::TextButton (text);
        styleKey (*b);
        b->setColour (juce::TextButton::buttonOnColourId, amber);
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
        auto tree = project;
        b->onClick = [tree, property, fallback, &um]
        {
            um.beginNewTransaction ("Bus compressor");
            MasterBusCompressor::setInProject (tree, property, ! (bool) tree.getProperty (property, fallback), &um);
        };
        b->setBounds (where);
        s.addAndMakeVisible (b);
        s.parts.add (b);
        s.bindings.push_back ({ b, project, property, fallback ? 1.0 : 0.0, true });
    };
    key ("COMP IN", ids::busCompOn, false, { 30, y, 110, 22 });
    key ("COLOR", "busCompColor", false, { w - 140, y, 110, 22 });
    y += 34;
    s.faderArea = { w / 2 - 60, y + 10, 120, 220 };
}

void AnalogConsole::rebuild()
{
    rebuildWanted = false;
    strips.clear();
    master.reset();
    int index = 0;
    for (const auto& track : channels (project))
    {
        ++index;
        const auto bus = model::isBusTrack (track);
        if ((filter == 1 && bus) || (filter == 2 && ! bus))
            continue;
        auto* s = strips.add (new Strip());
        s->track = track;
        s->number = juce::String (index);
        s->name = track[ids::name].toString();
        s->colour = model::trackColour (track);
        buildStrip (*s);
        holder.addAndMakeVisible (s);
    }
    master = std::make_unique<Strip>();
    master->isMaster = true;
    master->name = "Stereo Out";
    master->number = "MASTER";
    master->colour = juce::Colour (0xffb8bcc4);
    master->vuLevel = &vuValue;
    buildMaster (*master);
    holder.addAndMakeVisible (*master);

    auto setUpFader = [this] (Strip& s)
    {
        s.fader.setRange (minDb, 6.0, 0.1);
        s.fader.setSkewFactorFromMidPoint (-12.0);
        s.fader.setDoubleClickReturnValue (true, 0.0);
        s.fader.setPopupDisplayEnabled (true, true, nullptr);
        s.fader.setTint (s.isMaster ? juce::Colour() : s.colour);
        auto* strip = &s;
        s.fader.onDragStart = [this] { host.undo->beginNewTransaction ("Volume"); };
        s.fader.onValueChange = [this, strip]
        {
            const auto gain = dbToGain (strip->fader.getValue());
            if (strip->isMaster)
                project.setProperty (ids::masterGain, gain, host.undo);
            else
                strip->track.setProperty (ids::volume, gain, host.undo);
            const auto db = strip->fader.getValue();
            strip->level.setText (db <= minDb + 0.01 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
        };
        s.addAndMakeVisible (s.fader);
        s.addAndMakeVisible (s.meter);
        s.level.setJustificationType (juce::Justification::centred);
        s.level.setColour (juce::Label::textColourId, theme::text);
        s.level.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d0d0e));
        s.level.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        s.addAndMakeVisible (s.level);
        refreshBindings (s);
    };
    for (auto* s : strips)
        setUpFader (*s);
    setUpFader (*master);
    resized();
}

void AnalogConsole::Strip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1d20), r.getX(), 0.0f, juce::Colour (0xff1c1d20), r.getRight(), 0.0f, false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (isMaster ? 0.1f : 0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    // the number (and the master's title)
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (isMaster ? 15.0f : 13.0f, juce::Font::bold));
    g.drawText (number, getLocalBounds().reduced (9, 0).removeFromTop (30), juce::Justification::centredLeft, false);
    if (! isMaster && (bool) track.getProperty (ids::inputFxOn, true)
        && ! model::insertsOf (model::findChain (track.getParent(), model::inputOwner (track[ids::id].toString()))).isEmpty())
    {
        g.setColour (juce::Colour (0xff3ddc84));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("IN FX", getLocalBounds().reduced (9, 0).removeFromTop (30), juce::Justification::centredRight, false);
    }
    // the section titles and a line between them
    g.setFont (juce::FontOptions (9.0f));
    for (const auto& [title, area] : sectionTitles)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (area.getX() + 4, area.getY() - 3, area.getWidth() - 8, 1);
        g.setColour (theme::dimText);
        g.drawText (title, area, juce::Justification::centred, false);
    }
    // the captions under the knobs
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (theme::dimText);
    for (auto* part : parts)
        if (auto* k = dynamic_cast<AnalogKnob*> (part); k != nullptr && k->getHeight() >= 30)
        {
            auto caption = k->caption.upToFirstOccurrenceOf (" (", false, false);
            static const std::pair<const char*, const char*> shortNames[] {
                { "Mic gain", "Gain" }, { "High-pass", "HPF" }, { "Low-pass", "LPF" }, { "Compression", "Comp" },
                { "Gate range", "Range" }, { "Gate threshold", "Gate" }, { "HF frequency", "kHz" }, { "HMF frequency", "kHz" },
                { "LMF frequency", "Hz" }, { "LF frequency", "Hz" }, { "HF gain", "HF" }, { "HMF gain", "HMF" }, { "LMF gain", "LMF" }, { "LF gain", "LF" } };
            for (const auto& [from, to] : shortNames)
                if (caption == from)
                    caption = to;
            g.drawFittedText (caption, juce::Rectangle<int> (k->getX() - 10, k->getBottom() + 1, k->getWidth() + 20, 11), juce::Justification::centred, 1, 0.7f);
        }
    // the fader's scale, beside it
    if (fader.getHeight() > 60)
    {
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (theme::dimText);
        for (const auto db : { 6.0, 0.0, -5.0, -10.0, -20.0, -30.0, -60.0 })
        {
            const auto y = (float) fader.getY() + const_cast<strip::Fader&> (fader).yForValue (db);
            g.fillRect ((float) fader.getRight() - 2.0f, y - 0.5f, 5.0f, 1.0f);
            g.drawText (db > 0.0 ? "+" + juce::String ((int) db) : juce::String ((int) db), juce::Rectangle<float> ((float) fader.getRight() + 4.0f, y - 6.0f, 22.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
    // the compressor's gain reduction: a column of amber lights
    if (! grArea.isEmpty())
    {
        const auto lights = 8;
        const auto lit = juce::jlimit (0, lights, juce::roundToInt (gr / 2.0f));
        const auto cell = (float) grArea.getWidth() / (float) lights;
        for (int i = 0; i < lights; ++i)
        {
            const auto h = (float) grArea.getHeight() * (0.3f + 0.7f * (float) i / (float) (lights - 1));
            g.setColour (i < lit ? juce::Colour (0xfff2a43a) : juce::Colour (0xff2a2b30));
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) grArea.getX() + (float) i * cell + 1.0f, (float) grArea.getBottom() - h, cell - 2.0f, h), 1.0f);
        }
        g.setColour (theme::dimText);
        g.drawText ("GR", grArea.translated (0, grArea.getHeight() - 2).withHeight (11), juce::Justification::centred, false);
    }
    // the master's VU (our own cream meter): the needle drawn over its face, under its hood
    if (isMaster && vuLevel != nullptr)
    {
        static const auto face = effectdesign::skinImage ("ac_vu_png"), hood = effectdesign::skinImage ("ac_vu_hood_png");
        const auto area = juce::Rectangle<float> (12.0f, 32.0f, (float) getWidth() - 24.0f, ((float) getWidth() - 24.0f) * analogart::vuHeight / analogart::vuWidth);
        const auto k = area.getWidth() / analogart::vuWidth;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (face, area);
        const auto db = juce::Decibels::gainToDecibels (*vuLevel, -60.0f) + 18.0f;   // (-18 dBFS reads 0 VU)
        const auto v = juce::jlimit (0.0f, 1.0f, std::pow (10.0f, db / 20.0f) / std::pow (10.0f, 3.0f / 20.0f));
        const auto a = juce::degreesToRadians (-analogart::vuSwing + 2.0f * analogart::vuSwing * v);
        const juce::Point<float> pivot (area.getX() + analogart::vuPivotX * k, area.getY() + analogart::vuPivotY * k), dir (std::sin (a), -std::cos (a));
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (juce::Rectangle<float> (area.getX() + analogart::vuWindow[0] * k, area.getY() + analogart::vuWindow[1] * k,
                                                        analogart::vuWindow[2] * k, analogart::vuWindow[3] * k).toNearestInt());
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.drawLine ({ pivot.translated (3.0f, 0.0f), (pivot + dir * analogart::vuReach * k).translated (3.0f, 3.5f) }, analogart::vuThick * k * 1.4f);
            g.setColour (juce::Colour (0xff0e0c0a));
            g.drawLine ({ pivot, pivot + dir * analogart::vuReach * k }, analogart::vuThin * k);
            g.drawLine ({ pivot, pivot + dir * analogart::vuThickTo * k }, analogart::vuThick * k);
        }
        g.drawImage (hood, juce::Rectangle<float> (area.getX() + analogart::vuHood[0] * k, area.getY() + analogart::vuHood[1] * k,
                                                   analogart::vuHood[2] * k, analogart::vuHood[3] * k));
    }

    // the name plate in the track's colour
    auto plate = getLocalBounds().reduced (6, 0).removeFromBottom (30).withTrimmedBottom (4).toFloat();
    g.setColour (juce::Colour (0xff232427));
    g.fillRoundedRectangle (plate, 7.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (plate.removeFromBottom (3.0f), 1.5f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (name, getLocalBounds().reduced (8, 0).removeFromBottom (30).withTrimmedBottom (6), juce::Justification::centred, 1, 0.7f);
}

void AnalogConsole::timerCallback()
{
    if (! isShowing() || host.engine == nullptr || host.player == nullptr)
        return;
    for (auto* s : strips)
    {
        const auto slot = host.player->slotOf (s->track[ids::id].toString());
        const auto [l, r] = host.engine->takeTrackLevelForConsole (slot);
        s->meter.push (l, r);
        const auto gr = host.engine->getConsoleGainReduction (slot);
        if (std::abs (gr - s->gr) > 0.25f)
        {
            s->gr = gr;
            s->repaint (s->grArea.expanded (2, 14));
        }
    }
    if (master != nullptr && host.masterLevels)
    {
        const auto [l, r] = host.masterLevels();
        master->meter.push (l, r);
        // the VU: an average with a VU's swing (about 300 ms to a reading)
        const auto level = juce::jmax (l, r) * 0.7071f;
        vuValue += (level - vuValue) * 0.12f;
        master->repaint (juce::Rectangle<int> (10, 30, masterWidth - 20, 170));
    }
}

void AnalogConsole::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101113));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Analog Console", 16, 0, 200, 44, juce::Justification::centredLeft, false);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("every channel: input, filters, dynamics, EQ, inserts, sends, pan and fader", 170, 0, 600, 44, juce::Justification::centredLeft, false);
}

void AnalogConsole::resized()
{
    auto top = getLocalBounds().removeFromTop (44).reduced (12, 8);
    for (int i = filterKeys.size(); --i >= 0;)
    {
        filterKeys[i]->setBounds (top.removeFromRight (70));
        top.removeFromRight (4);
    }
    auto area = getLocalBounds().withTrimmedTop (44).reduced (6, 0).withTrimmedBottom (6);
    view.setBounds (area);
    int contentHeight = 0;
    for (auto* s : strips)
        contentHeight = juce::jmax (contentHeight, s->faderArea.getY() + 200 + 64);
    contentHeight = juce::jmax (contentHeight, area.getHeight() - view.getScrollBarThickness());
    const auto width = strips.size() * (columnWidth + 4) + masterWidth + 12;
    holder.setSize (juce::jmax (width, area.getWidth() - 2), contentHeight);
    auto placeFader = [] (Strip& s, int height)
    {
        auto f = s.faderArea.withHeight (juce::jmax (120, height - s.faderArea.getY() - 64));
        auto meter = f.removeFromRight (18);
        s.meter.setBounds (meter.reduced (3, 6));
        f.removeFromRight (26);   // (the scale)
        s.fader.setBounds (f.withSizeKeepingCentre (juce::jmin (f.getWidth(), 44), f.getHeight()));
        s.level.setBounds (s.faderArea.getCentreX() - 30, f.getBottom() + 4, 60, 20);
    };
    for (int i = 0; i < strips.size(); ++i)
    {
        strips[i]->setBounds (i * (columnWidth + 4), 0, columnWidth, contentHeight);
        placeFader (*strips[i], contentHeight);
    }
    if (master != nullptr)
    {
        master->setBounds (holder.getWidth() - masterWidth - 2, 0, masterWidth, contentHeight);
        placeFader (*master, contentHeight);
    }
}

} // namespace riff
