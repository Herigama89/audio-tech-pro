#include "ConsoleViews.h"
#include "ConsoleLook.h"
#include "EffectDesigns.h"
#include "Mixer.h"
#include "Theme.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/MasterBusCompressor.h"
#include "../engine/BandAnalysis.h"
#include "../engine/Loudness.h"
#include "../engine/ConsoleChannel.h"
#include "Looks.h"
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
namespace
{
    // Its colours: the console's mid-grey (dark glass) or a light frosted face (light glass)
    struct Desk
    {
        juce::Colour top, bottom, edge, lineDark, lineLight, title, caption, scale, key, keyBottom, keyText, slot, slotText, plate;
    };
    const Desk& desk()
    {
        static const Desk dark { juce::Colour (0xff4e4f54), juce::Colour (0xff3a3b3f), juce::Colours::white.withAlpha (0.13f),
                                 juce::Colours::black.withAlpha (0.45f), juce::Colours::white.withAlpha (0.06f),
                                 juce::Colour (0xffc9cacd), juce::Colour (0xffd0d1d4), juce::Colour (0xffd6d7da).withAlpha (0.75f),
                                 juce::Colour (0xff45464a), juce::Colour (0xff36373a), juce::Colour (0xffd6d7da),
                                 juce::Colours::black.withAlpha (0.35f), juce::Colours::white.withAlpha (0.35f), juce::Colours::white.withAlpha (0.08f) };
        static const Desk light { juce::Colour (0xffeceef2), juce::Colour (0xffd6d8de), juce::Colours::white.withAlpha (0.9f),
                                  juce::Colours::black.withAlpha (0.1f), juce::Colours::white.withAlpha (0.7f),
                                  juce::Colour (0xff4a4c55), juce::Colour (0xff3a3c44), juce::Colour (0xff3d3f48).withAlpha (0.85f),
                                  juce::Colour (0xfffdfdfe), juce::Colour (0xffe4e5ea), juce::Colour (0xff2a2b31),
                                  juce::Colours::black.withAlpha (0.06f), juce::Colour (0xff1e1e28).withAlpha (0.4f), juce::Colours::white.withAlpha (0.7f) };
        return theme::glassLight() ? light : dark;
    }

    // The sections' cap colours
    const juce::Colour capRed { 0xffc8312a }, capWhite { 0xffe8e8e6 }, capGate { 0xff2f6f2c }, capGrey { 0xff8d8f93 },
                       capHmf { 0xff3e9b3c }, capBlue { 0xff2c5fbf }, capBrown { 0xff4a3020 }, capGold { 0xffd9a51f };
    const juce::Colour keyAmber { 0xffffa21f }, keyGreen { 0xff3fbf3a }, keyRed { 0xffe5463b }, keyYellow { 0xffffd60a }, keyBlue { 0xff3a6df0 };

    juce::String hzText (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 || std::abs (hz / 1000.0 - std::round (hz / 1000.0)) < 0.05 ? 0 : 1) + "k";
        return juce::String (juce::roundToInt (hz));
    }
    juce::String shortNumber (double v)
    {
        if (std::abs (v) >= 1000.0)
            return hzText (v);
        if (std::abs (v - std::round (v)) < 0.001)
            return juce::String (juce::roundToInt (v));
        auto t = juce::String (v, std::abs (v) < 0.1 ? 2 : 1);
        if (t.startsWith ("0."))
            t = t.substring (1);
        if (t.startsWith ("-0."))
            t = "-" + t.substring (2);
        return t;
    }
}

AnalogKnob::AnalogKnob (juce::Colour c, const juce::String& text, bool isPan) : caption (text), cap (c), pan (isPan)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setPopupDisplayEnabled (true, true, nullptr);
    setWantsKeyboardFocus (false);
    setRotaryParameters (juce::degreesToRadians (-150.0f) + juce::MathConstants<float>::twoPi, juce::degreesToRadians (150.0f) + juce::MathConstants<float>::twoPi, true);
}

juce::Rectangle<float> AnalogKnob::knobArea() const
{
    const auto size = (float) knobSize + (pan ? 3.0f : 0.0f);
    const auto top = scale.empty() ? ((float) getHeight() - size) * 0.5f : 11.0f;
    return { ((float) getWidth() - size) * 0.5f, top, size, size };
}

juce::String AnalogKnob::captionNow() const
{
    return isMouseButtonDown() ? const_cast<AnalogKnob*> (this)->getTextFromValue (getValue()) : caption;
}

void AnalogKnob::valueChanged()
{
    if (isMouseButtonDown())
        if (auto* parent = getParentComponent())
            parent->repaint (getBounds().expanded (12, 14));
}

void AnalogKnob::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    if (auto* parent = getParentComponent())
        parent->repaint (getBounds().expanded (12, 14));
}

void AnalogKnob::paint (juce::Graphics& g)
{
    const auto area = knobArea();
    const auto c = area.getCentre();
    const auto params = getRotaryParameters();
    // its printed scale: ticks, and the numbers at the values they mark
    if (! scale.empty())
    {
        const auto& d = desk();
        const auto r0 = area.getWidth() * 0.5f + 1.5f, r1 = r0 + 2.5f, rText = r0 + 6.5f;
        g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = params.startAngleRadians + (params.endAngleRadians - params.startAngleRadians) * (float) i / 10.0f;
            g.setColour (d.scale.withMultipliedAlpha (0.6f));
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 0.8f);
        }
        for (const auto& [value, text] : scale)
        {
            if (text.isEmpty())
                continue;
            const auto prop = (float) valueToProportionOfLength (juce::jlimit (getMinimum(), getMaximum(), value));
            const auto a = params.startAngleRadians + (params.endAngleRadians - params.startAngleRadians) * prop;
            const juce::Point<float> at (c.x + rText * std::sin (a), c.y - rText * std::cos (a));
            g.setColour (d.scale);
            g.drawText (text, juce::Rectangle<float> (26.0f, 9.0f).withCentre (at), juce::Justification::centred, false);
        }
    }
    looks::drawCapKnob (g, area, (float) valueToProportionOfLength (getValue()), params.startAngleRadians, params.endAngleRadians,
                        false, isEnabled(), cap, pan);
}

void ConsoleKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto& d = desk();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    if (on)
        g.setGradientFill (juce::ColourGradient (lit.brighter (0.15f), 0.0f, r.getY(), lit.darker (0.25f), 0.0f, r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (d.key.brighter (over ? 0.08f : 0.0f), 0.0f, r.getY(), d.keyBottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (theme::glassLight() ? 0.18f : 0.6f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.0f : 0.12f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
    g.setColour (on ? (lit.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff1b1206) : juce::Colours::white) : d.keyText);
    g.setFont (juce::FontOptions (juce::jmin (10.0f, (float) getHeight() * 0.6f), juce::Font::bold));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
}

void InsertSlot::paint (juce::Graphics& g)
{
    const auto& d = desk();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (kind == 0)
        g.setColour (d.slot);
    else if (kind == 2)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3f5fae), 0.0f, r.getY(), juce::Colour (0xff2f4a8e), 0.0f, r.getBottom(), false));
    else if (kind == 3)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2f7d47), 0.0f, r.getY(), juce::Colour (0xff235f36), 0.0f, r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (d.key.brighter (0.12f), 0.0f, r.getY(), d.keyBottom.brighter (0.1f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (theme::glassLight() ? 0.12f : 0.45f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    // its lamp
    const auto lamp = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getX() + 7.0f, r.getCentreY() });
    if (kind != 0 && on)
    {
        g.setColour (juce::Colour (0xff34c759).withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (2.0f));
        g.setColour (juce::Colour (0xff34c759));
    }
    else
        g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillEllipse (lamp);
    g.setColour (kind == 0 ? d.slotText : (kind >= 2 || ! theme::glassLight() ? juce::Colour (0xfff2f2f4) : juce::Colour (0xff1c1d22)));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawFittedText (kind == 0 ? juce::String ("+") : name, getLocalBounds().withTrimmedLeft (14).withTrimmedRight (3), juce::Justification::centredLeft, 1, 0.75f);
}

//==============================================================================
struct ConsoleMeterState
{
    bands::FilterBank bank;
    LoudnessMeter loudness;
    double rate = 0.0;
    std::array<float, ConsoleMeter::bandCount> level {}, held {};
    std::array<int, ConsoleMeter::bandCount> heldFor {};
    float correlation = 1.0f;
    std::vector<juce::Point<float>> scope = std::vector<juce::Point<float>> (512);
    int scopePos = 0;
    float scopeGain = 4.0f;
};

ConsoleMeter::ConsoleMeter (AudioEngine& e, juce::ValueTree p) : engine (e), project (p), state (std::make_unique<ConsoleMeterState>())
{
    readL.resize (16384);
    readR.resize (16384);
    tab = juce::jlimit (0, 2, (int) project.getProperty ("consoleMeterTab", 0));
    bars = (bool) project.getProperty ("consoleMeterBars", false);
    int i = 0;
    for (auto* k : { &spectrumKey, &stereoKey, &loudnessKey })
    {
        k->onClick = [this, i] { setTab (i); };
        addAndMakeVisible (k);
        ++i;
    }
    curveKey.onClick = [this] { bars = false; project.setProperty ("consoleMeterBars", false, nullptr); setTab (tab); };
    barsKey.onClick = [this] { bars = true; project.setProperty ("consoleMeterBars", true, nullptr); setTab (tab); };
    addAndMakeVisible (curveKey);
    addAndMakeVisible (barsKey);
    setTab (tab);
    engine.setMasterTap (true);
    startTimerHz (30);
}

ConsoleMeter::~ConsoleMeter()
{
    engine.setMasterTap (false);
}

void ConsoleMeter::setTab (int t)
{
    tab = t;
    project.setProperty ("consoleMeterTab", t, nullptr);
    spectrumKey.setToggleState (tab == 0, juce::dontSendNotification);
    stereoKey.setToggleState (tab == 1, juce::dontSendNotification);
    loudnessKey.setToggleState (tab == 2, juce::dontSendNotification);
    curveKey.setToggleState (! bars, juce::dontSendNotification);
    barsKey.setToggleState (bars, juce::dontSendNotification);
    curveKey.setVisible (tab == 0);
    barsKey.setVisible (tab == 0);
    repaint();
}

void ConsoleMeter::resized()
{
    auto r = getLocalBounds();
    auto tabs = r.removeFromTop (18);
    const auto w = tabs.getWidth() / 3;
    spectrumKey.setBounds (tabs.removeFromLeft (w).reduced (1, 0));
    stereoKey.setBounds (tabs.removeFromLeft (w).reduced (1, 0));
    loudnessKey.setBounds (tabs.reduced (1, 0));
    r.removeFromTop (3);
    auto sub = r.removeFromTop (14).withSizeKeepingCentre (96, 14);
    curveKey.setBounds (sub.removeFromLeft (48).reduced (1, 0));
    barsKey.setBounds (sub.reduced (1, 0));
    r.removeFromTop (3);
    readouts = r.removeFromBottom (13);
    screen = r.withTrimmedBottom (2);
}

void ConsoleMeter::timerCallback()
{
    if (! isShowing())
    {
        engine.readMaster (readL.data(), readR.data(), (int) readL.size());   // (keep the tap from filling up)
        return;
    }
    auto& st = *state;
    const auto rate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    if (std::abs (rate - st.rate) > 0.5)
    {
        st.bank.prepare (rate, bandCount);
        st.loudness.prepare (rate);
        st.rate = rate;
    }
    const auto n = engine.readMaster (readL.data(), readR.data(), (int) readL.size());
    if (n > 0)
    {
        st.loudness.process (readL.data(), readR.data(), n);
        std::vector<float> mono ((size_t) n);
        double lr = 0.0, ll = 0.0, rr = 0.0;
        float loudest = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            mono[(size_t) i] = 0.5f * (readL[(size_t) i] + readR[(size_t) i]);
            lr += (double) readL[(size_t) i] * readR[(size_t) i];
            ll += (double) readL[(size_t) i] * readL[(size_t) i];
            rr += (double) readR[(size_t) i] * readR[(size_t) i];
            loudest = juce::jmax (loudest, std::abs (readL[(size_t) i]), std::abs (readR[(size_t) i]));
        }
        st.bank.process (mono.data(), n);
        if (ll > 1.0e-9 && rr > 1.0e-9)
            st.correlation += ((float) (lr / std::sqrt (ll * rr)) - st.correlation) * 0.25f;
        const auto step = juce::jmax (1, n / 256);
        for (int i = 0; i < n; i += step)
        {
            st.scope[(size_t) st.scopePos] = { (readR[(size_t) i] - readL[(size_t) i]) * 0.7071f, (readL[(size_t) i] + readR[(size_t) i]) * 0.7071f };
            st.scopePos = (st.scopePos + 1) % (int) st.scope.size();
        }
        const auto wanted = loudest > 1.0e-4f ? juce::jlimit (1.0f, 30.0f, 0.8f / loudest) : st.scopeGain;
        st.scopeGain += (wanted - st.scopeGain) * (wanted < st.scopeGain ? 0.5f : 0.05f);
    }
    std::array<float, bandCount> peak {};
    std::array<double, bandCount> ms {};
    const auto got = st.bank.count() == bandCount && st.bank.take (peak.data(), ms.data());
    for (int b = 0; b < bandCount; ++b)
    {
        const auto db = got ? juce::Decibels::gainToDecibels (peak[(size_t) b], -90.0f) : -90.0f;
        st.level[(size_t) b] = db > st.level[(size_t) b] ? db : juce::jmax (db, st.level[(size_t) b] - 1.2f);   // (falls 36 dB a second)
        if (st.level[(size_t) b] >= st.held[(size_t) b])
        {
            st.held[(size_t) b] = st.level[(size_t) b];
            st.heldFor[(size_t) b] = 0;
        }
        else if (++st.heldFor[(size_t) b] > 45)
            st.held[(size_t) b] = juce::jmax (st.level[(size_t) b], st.held[(size_t) b] - 0.8f);
    }
    repaint();
}

void ConsoleMeter::paint (juce::Graphics& g)
{
    const auto& d = desk();
    auto& st = *state;
    const auto r = screen.toFloat();
    g.setColour (juce::Colour (0xff07080a));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    const auto inner = r.reduced (3.0f);
    const auto xOf = [inner] (double hz) { return inner.getX() + inner.getWidth() * (float) bands::position (hz); };
    const auto yOf = [inner] (float db) { return inner.getY() + inner.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 66.0f); };

    if (tab == 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawVerticalLine ((int) xOf (hz), inner.getY(), inner.getBottom());
        for (const auto db : { -12.0f, -24.0f, -36.0f, -48.0f })
            g.drawHorizontalLine ((int) yOf (db), inner.getX(), inner.getRight());
        if (bars)
        {
            const auto bw = inner.getWidth() / (float) bandCount;
            const int segments = juce::jmax (8, (int) (inner.getHeight() / 4.0f));
            for (int b = 0; b < bandCount; ++b)
            {
                const auto x = inner.getX() + (float) b * bw + 1.0f;
                const auto top = yOf (st.level[(size_t) b]);
                for (int q = 0; q < segments; ++q)
                {
                    const auto y = inner.getBottom() - (float) (q + 1) * inner.getHeight() / (float) segments;
                    if (y + 3.0f < top)
                        break;
                    const auto frac = (float) q / (float) segments;
                    g.setColour (frac > 0.85f ? juce::Colour (0xffff453a) : frac > 0.7f ? juce::Colour (0xffffd60a) : juce::Colour (0xff34c759));
                    g.fillRoundedRectangle (x, y, bw - 2.0f, inner.getHeight() / (float) segments - 1.0f, 0.6f);
                }
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.fillRect (x, yOf (st.held[(size_t) b]) - 1.5f, bw - 2.0f, 1.5f);
            }
        }
        else
        {
            juce::Path curve, area;
            for (int b = 0; b < bandCount; ++b)
            {
                const auto x = xOf (bands::centre (b, bandCount));
                const auto y = yOf (st.level[(size_t) b]);
                if (b == 0) { curve.startNewSubPath (inner.getX(), y); area.startNewSubPath (inner.getX(), inner.getBottom()); area.lineTo (inner.getX(), y); }
                curve.lineTo (x, y);
                area.lineTo (x, y);
            }
            const auto lastY = yOf (st.level[(size_t) bandCount - 1]);
            curve.lineTo (inner.getRight(), lastY);
            area.lineTo (inner.getRight(), lastY);
            area.lineTo (inner.getRight(), inner.getBottom());
            area.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xbf5ac8fa), 0.0f, inner.getY(), juce::Colour (0x0d5ac8fa), 0.0f, inner.getBottom(), false));
            g.fillPath (area);
            g.setColour (juce::Colour (0xff9ee6ff));
            g.strokePath (curve, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
            juce::Path held;
            for (int b = 0; b < bandCount; ++b)
            {
                const juce::Point<float> p (xOf (bands::centre (b, bandCount)), yOf (st.held[(size_t) b]));
                if (b == 0) held.startNewSubPath (p); else held.lineTo (p);
            }
            juce::Path dashed;
            const float dash[] { 2.0f, 1.5f };
            juce::PathStrokeType (0.8f).createDashedStroke (dashed, held, dash, 2);
            g.setColour (juce::Colour (0xb3ffd60a));
            g.fillPath (dashed);
        }
        // the frequencies, on a dark backing so the bars don't hide them
        g.setFont (juce::FontOptions (7.5f));
        for (const auto& [hz, text] : { std::pair { 100.0, "100" }, std::pair { 1000.0, "1k" }, std::pair { 10000.0, "10k" } })
        {
            const auto box = juce::Rectangle<float> (18.0f, 9.0f).withPosition (xOf (hz) + 1.0f, inner.getBottom() - 9.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (box, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }
    else if (tab == 1)
    {
        // the stereo image: mid up, sides across
        const auto c = inner.getCentre();
        const auto half = juce::jmin (inner.getWidth(), inner.getHeight()) * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (c.x - half, c.y + half, c.x + half, c.y - half, 1.0f);
        g.drawLine (c.x - half, c.y - half, c.x + half, c.y + half, 1.0f);
        g.drawVerticalLine ((int) c.x, inner.getY(), inner.getBottom());
        g.setColour (juce::Colour (0xff9ee6ff).withAlpha (0.7f));
        for (const auto& p : st.scope)
        {
            const auto x = c.x + juce::jlimit (-1.0f, 1.0f, p.x * st.scopeGain) * half;
            const auto y = c.y - juce::jlimit (-1.0f, 1.0f, p.y * st.scopeGain) * half;
            g.fillRect (x, y, 1.2f, 1.2f);
        }
    }
    else
    {
        // loudness: short-term big, integrated under it, a bar from -40 to 0 LUFS
        const auto st_ = st.loudness.shortTerm(), in = st.loudness.integrated();
        const auto text = [] (double v) { return v > -70.0 ? juce::String (v, 1) : juce::String ("--"); };
        auto box = inner.reduced (6.0f);
        auto barArea = box.removeFromRight (14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("SHORT-TERM", box.removeFromTop (12.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff34c759));
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (text (st_) + " LUFS", box.removeFromTop (30.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("INTEGRATED", box.removeFromTop (12.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (text (in) + " LUFS", box.removeFromTop (20.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (barArea, 2.0f);
        const auto frac = juce::jlimit (0.0f, 1.0f, (float) ((st_ + 40.0) / 40.0));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34c759), 0.0f, barArea.getBottom(), juce::Colour (0xffff453a), 0.0f, barArea.getY(), false));
        g.fillRoundedRectangle (barArea.withTop (barArea.getBottom() - barArea.getHeight() * frac), 2.0f);
    }

    // the readouts under it
    g.setFont (juce::FontOptions (9.0f));
    const auto st2 = st.loudness.shortTerm();
    juce::AttributedString left, right;
    left.append ("Short-term ", juce::FontOptions (9.0f), d.caption.withAlpha (0.8f));
    left.append (st2 > -70.0 ? juce::String (st2, 1) + " LUFS" : juce::String ("-- LUFS"), juce::FontOptions (9.0f, juce::Font::bold), juce::Colour (0xff34c759));
    left.draw (g, readouts.toFloat().withTrimmedLeft (4.0f));
    right.append ("Corr ", juce::FontOptions (9.0f), d.caption.withAlpha (0.8f));
    right.append ((st.correlation >= 0.0f ? "+" : "") + juce::String (st.correlation, 2), juce::FontOptions (9.0f, juce::Font::bold),
                  st.correlation < 0.0f ? juce::Colour (0xffff453a) : juce::Colour (0xff34c759));
    right.setJustification (juce::Justification::centredRight);
    right.draw (g, readouts.toFloat().withTrimmedRight (4.0f));
}

//==============================================================================
AnalogConsole::AnalogConsole (ConsoleHost& h, juce::ValueTree p) : host (h), project (p)
{
    const char* const names[] { "All", "Audio", "Buses" };
    for (int i = 0; i < 3; ++i)
    {
        auto* k = filterKeys.add (new ConsoleKey (names[i], keyAmber));
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
    if (tree.hasType (ids::TRACK) && (property == ids::name || property == ids::colour || property == ids::inputFxOn))
        rebuildWanted = true;
    if (tree.hasType (ids::INSERT) && (property == ids::bypass || property == ids::effect))
        rebuildWanted = true;
    if (property.toString().startsWith ("consoleMeter"))
        return;
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
    const auto id = track[ids::id].toString();
    int y = 34;
    const int w = columnWidth;
    const int boxW = 54, boxH = 44;
    const auto left = [&] (int row) { return juce::Rectangle<int> (4, row, boxW, boxH); };
    const auto right = [&] (int row) { return juce::Rectangle<int> (w - 4 - boxW, row, boxW, boxH); };

    const auto knob = [&] (juce::Colour colour, const juce::String& caption, const juce::Identifier& property, double fallback,
                           double lo, double hi, double mid, juce::Rectangle<int> where, std::vector<std::pair<double, juce::String>> scale,
                           std::function<juce::String (double)> text, bool pan = false) -> AnalogKnob&
    {
        auto* k = new AnalogKnob (colour, caption, pan);
        k->setRange (lo, hi, 0.0);
        if (mid > lo && mid < hi)
            k->setSkewFactorFromMidPoint (mid);
        k->setDoubleClickReturnValue (true, fallback);
        k->setValue ((double) track.getProperty (property, fallback), juce::dontSendNotification);
        k->setTooltip (caption);
        k->scale = std::move (scale);
        k->textFromValueFunction = std::move (text);
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
                             juce::Colour on = keyAmber, const juce::String& tip = {})
    {
        auto* b = new ConsoleKey (text, on);
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
    const auto keys = [&] (int row) { return std::make_pair (juce::Rectangle<int> (7, row, w / 2 - 10, 17), juce::Rectangle<int> (w / 2 + 3, row, w / 2 - 10, 17)); };
    const auto wideKey = [&] (int row) { return juce::Rectangle<int> (12, row, w - 24, 17); };
    const auto section = [&] (const juce::String& title) { s.sectionTitles.push_back ({ title, juce::Rectangle<int> (0, y, w, 14) }); y += 15; };
    const auto db1 = [] (const juce::String& unit) { return [unit] (double v) { return juce::String (v, std::abs (v) < 10.0 ? 1 : 0) + unit; }; };
    const auto signedDb = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    const auto hz = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    const auto slots = [&] (const juce::String& owner, int minimum, bool inputFx)
    {
        const auto inserts = ordered (model::findChain (project, owner));
        int next = 0;
        for (const auto& insert : inserts)
            next = juce::jmax (next, model::insertSlot (insert) + 1);
        const auto count = juce::jmax (minimum, inserts.size() + 1);
        for (int i = 0; i < count; ++i)
        {
            InsertSlot* slot;
            if (i < inserts.size())
            {
                auto insert = inserts[i];
                const auto plugin = isPlugin (insert);
                slot = new InsertSlot (nameOf (insert), plugin ? 2 : (inputFx ? 3 : 1), ! (bool) insert.getProperty (ids::bypass, false),
                                       [this, insert] { if (host.openInsert) host.openInsert (insert); });
                slot->setTooltip ("Open " + nameOf (insert));
            }
            else
            {
                slot = new InsertSlot ({}, 0, false, {});
                auto* raw = slot;
                slot->click = [this, owner, next, raw] { if (host.chooseEffect) host.chooseEffect (owner, next, {}, *raw); };
                slot->setTooltip (inputFx ? "Add a plug-in to the input (before the recorder)" : "Add an insert");
            }
            slot->setBounds (7, y, w - 14, 17);
            s.addAndMakeVisible (slot);
            s.parts.add (slot);
            y += 19;
        }
        y += 4;
    };
    const auto audio = ! model::isInstrumentTrack (track) && ! model::isBusTrack (track);

    // Input
    section ("INPUT");
    {
        knob (capRed, "Gain", ids::consoleMicGain, 20.0, 20.0, 70.0, 0.0, left (y), { { 20.0, "20" }, { 45.0, "" }, { 70.0, "70" } }, db1 (" dB"));
        knob (capGrey, "Trim", ids::consoleInput, 0.0, -20.0, 20.0, 0.0, right (y), { { -20.0, "-20" }, { 0.0, "0" }, { 20.0, "+20" } }, signedDb);
        y += boxH + 12;
        auto [a, b] = keys (y);
        toggle (juce::CharPointer_UTF8 ("\xc3\x98"), ids::consolePhase, false, a, keyAmber, "Phase: the polarity turned over");
        toggle ("LINE", ids::consoleLine, true, b, keyAmber, "Line in (off: the mic preamp, its gain and colour)");
        y += 20;
        auto [c, d] = keys (y);
        toggle ("PAD", ids::consolePad, false, c, keyAmber, "Pad: -20 dB in");
        toggle ("IN", ids::stripIn, true, d, keyGreen, "The whole channel strip in");
        y += 24;
    }
    // Input FX (audio channels): what's on the input, before the recorder, and its round trip
    if (audio)
    {
        s.sectionTitles.push_back ({ "INPUT FX", juce::Rectangle<int> (0, y, w, 14) });
        const auto chain = ordered (model::findChain (project, model::inputOwner (id)));
        if (! chain.isEmpty() && (bool) track.getProperty (ids::inputFxOn, true))
            s.inputFxTime = juce::String (InputFxPanel::roundTripMs (host, project, track), 2) + " ms";
        y += 15;
        slots (model::inputOwner (id), 2, true);
    }
    section ("DYNAMICS");
    {
        knob (capWhite, "Ratio", ids::consoleCompRatio, 0.0, 0.0, 20.0, 4.0, left (y), { { 0.0, "A" }, { 4.0, "4" }, { 20.0, "20" } },
              [] (double v) { return v < 0.5 ? juce::String ("Auto") : juce::String (v, 1) + ":1"; });
        knob (capWhite, "Comp", ids::consoleDynamics, 0.0, 0.0, 10.0, 0.0, right (y), { { 0.0, "0" }, { 5.0, "5" }, { 10.0, "10" } }, db1 (""));
        y += boxH + 12;
        knob (capWhite, "Release", ids::consoleCompRelease, 0.15, 0.05, 4.0, 0.4, left (y), { { 0.05, ".05" }, { 0.4, ".4" }, { 4.0, "4" } }, db1 (" s"));
        s.grArea = right (y).reduced (14, 4).withTrimmedBottom (8);
        y += boxH + 12;
        auto [e, f] = keys (y);
        toggle ("FAST", ids::consoleCompFast, false, e);
        toggle ("KNEE", ids::consoleCompKnee, false, f, keyAmber, "Soft knee");
        y += 22;
        knob (capGate, "Range", ids::consoleGateRange, 20.0, 0.0, 40.0, 0.0, left (y), { { 0.0, "0" }, { 20.0, "20" }, { 40.0, "40" } }, db1 (" dB"));
        knob (capGate, "Gate", ids::consoleGateThreshold, -80.0, -80.0, 0.0, 0.0, right (y), { { -80.0, "off" }, { -40.0, "-40" }, { 0.0, "0" } },
              [] (double v) { return v <= -79.9 ? juce::String ("Off") : juce::String (v, 1) + " dB"; });
        y += boxH + 12;
        auto [i1, j1] = keys (y);
        toggle ("EXP", ids::consoleGateExpander, false, i1, keyAmber, "Expander instead of a gate");
        toggle ("DYN IN", ids::consoleDynIn, true, j1, keyGreen);
        y += 24;
    }
    section ("FILTERS");
    {
        knob (capGrey, "HPF", ids::consoleHPF, 0.0, 0.0, 500.0, 80.0, left (y), { { 0.0, "off" }, { 80.0, "80" }, { 500.0, "500" } },
              [hz] (double v) { return v < 1.0 ? juce::String ("Off") : hz (v); });
        knob (capGrey, "LPF", ids::consoleLPF, 0.0, 0.0, 22000.0, 6000.0, right (y), { { 0.0, "off" }, { 6000.0, "6k" }, { 22000.0, "22k" } },
              [hz] (double v) { return v < 1.0 ? juce::String ("Off") : hz (v); });
        y += boxH + 14;
    }
    section ("EQ");
    {
        const auto g = (double) ConsoleChannel::maxGainDb;
        const auto gainScale = std::vector<std::pair<double, juce::String>> { { -g, "-" + shortNumber (g) }, { 0.0, "0" }, { g, "+" + shortNumber (g) } };
        struct Band { juce::Colour colour; const char* name; juce::Identifier gain, freq; double f0, f1, fmid, fdef; };
        const Band bands_[] { { capRed, "HF", ids::consoleHF, ids::consoleHFFreq, 1500.0, 16000.0, 5000.0, 8000.0 },
                              { capHmf, "HMF", ids::consoleHMF, ids::consoleHMFFreq, 600.0, 7000.0, 2000.0, 2500.0 },
                              { capBlue, "LMF", ids::consoleLMF, ids::consoleLMFFreq, 200.0, 2500.0, 700.0, 400.0 },
                              { capBrown, "LF", ids::consoleLF, ids::consoleLFFreq, 30.0, 450.0, 120.0, 100.0 } };
        for (const auto& band : bands_)
        {
            const juce::String name (band.name);
            knob (band.colour, name, band.gain, 0.0, -g, g, 0.0, left (y), gainScale, signedDb);
            knob (band.colour, band.f1 >= 1000.0 && name != "LMF" ? "kHz" : (name == "LMF" ? "kHz" : "Hz"), band.freq, band.fdef, band.f0, band.f1, band.fmid, right (y),
                  { { band.f0, hzText (band.f0) }, { band.fmid, hzText (band.fmid) }, { band.f1, hzText (band.f1) } }, hz);
            y += boxH + 12;
            if (name == "HMF" || name == "LMF")
            {
                auto& q = knob (band.colour, "Q", name == "HMF" ? ids::consoleHMFQ : ids::consoleLMFQ, 1.0, 0.1, 4.0, 1.0,
                                juce::Rectangle<int> (w / 2 - 22, y - 4, 44, 38), { { 0.1, ".1" }, { 1.0, "1" }, { 4.0, "4" } },
                                [] (double v) { return "Q " + juce::String (v, 2); });
                q.knobSize = 18;
                y += 46;
            }
        }
        auto [c, d] = keys (y);
        toggle ("BELL", ids::consoleHFBell, false, c, keyAmber, "HF and LF as bells (else shelves)");
        toggle ("EQ IN", ids::consoleIn, true, d, keyGreen);
        y += 24;
    }
    section ("INSERTS");
    slots (id, 5, false);
    section ("SENDS");
    {
        const auto sends = model::sendsOf (model::findChain (project, id));
        const int count = juce::jmax (3, juce::jmin (6, (int) sends.size() + (sends.size() < 6 ? 0 : 0)));
        const int cols = 3, cw = (w - 8) / cols;
        for (int n = 0; n < juce::jmax (count, 3); ++n)
        {
            auto* k = new AnalogKnob (capGold, "Send " + juce::String (n + 1));
            k->setRange (minDb, 6.0, 0.0);
            k->setSkewFactorFromMidPoint (-12.0);
            k->setDoubleClickReturnValue (true, -20.0);
            k->textFromValueFunction = [] (double v) { return v <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
            k->knobSize = 22;
            k->setBounds (4 + (n % cols) * cw, y + (n / cols) * 40, cw, 30);
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
                k->caption = juce::String (n + 1);
                k->setEnabled (false);
                k->setTooltip ("No send here (add one in the mixer)");
            }
            s.addAndMakeVisible (k);
            s.parts.add (k);
        }
        y += ((juce::jmax (count, 3) + cols - 1) / cols) * 40 + 4;
    }
    // Pan and the keys
    {
        y += 4;
        knob (capGrey, "Pan", ids::pan, 0.0, -1.0, 1.0, 0.0, juce::Rectangle<int> (w / 2 - boxW / 2, y, boxW, boxH),
              { { -1.0, "L" }, { 0.0, "C" }, { 1.0, "R" } },
              [] (double v)
              {
                  const auto n = juce::roundToInt (v * 63.0);
                  return n == 0 ? juce::String ("C") : n < 0 ? juce::String (-n) + " L" : juce::String (n) + " R";
              }, true);
        y += boxH + 14;
        auto [a, b] = keys (y);
        toggle ("M", ids::mute, false, a, keyRed, "Mute");
        toggle ("S", ids::solo, false, b, keyYellow, "Solo");
        y += 20;
        if (! model::isBusTrack (track))
        {
            auto [c, d] = keys (y);
            toggle ("R", ids::armed, false, c, keyRed, "Record");
            toggle ("I", ids::monitor, false, d, keyGreen, "Input monitoring");
            y += 20;
        }
        y += 6;
    }
    juce::ignoreUnused (wideKey);
    s.faderArea = { 4, y, w - 8, 230 };
}

void AnalogConsole::buildMaster (Strip& s)
{
    auto& um = *host.undo;
    const int w = masterWidth;
    int y = 34;
    // the bus compressor's black meter at the top (painted), then its knobs
    s.compMeter = { 10, y, w - 20, juce::roundToInt ((w - 20) * 308.0 / 550.0) };
    y = s.compMeter.getBottom() + 10;
    s.sectionTitles.push_back ({ "BUS COMPRESSOR", juce::Rectangle<int> (0, y, w, 14) });
    y += 16;
    using C = MasterBusCompressor;
    struct K { const char* caption; juce::Identifier property; double fallback, lo, hi; std::vector<std::pair<double, juce::String>> scale; std::function<juce::String (double)> text; };
    const auto steps = [] (auto values, auto fmt)
    {
        std::vector<std::pair<double, juce::String>> out;
        for (size_t i = 0; i < values.size(); ++i)
            out.push_back ({ (double) i, fmt (i) });
        return out;
    };
    const K row1[] { { "Thresh", ids::busCompThreshold, -10.0, C::minThresholdDb, C::maxThresholdDb, { { -30.0, "-30" }, { -15.0, "-15" }, { 0.0, "0" } },
                       [] (double v) { return juce::String (v, 1) + " dB"; } },
                     { "Ratio", ids::busCompRatio, 1.0, 0.0, (double) C::ratios.size() - 1,
                       steps (C::ratios, [] (size_t i) { return shortNumber (C::ratios[i]); }), [] (double v) { return C::ratioText (juce::roundToInt (v)) + ":1"; } },
                     { "Make-up", ids::busCompMakeup, 0.0, 0.0, C::maxMakeupDb, { { 0.0, "0" }, { C::maxMakeupDb * 0.5, shortNumber (C::maxMakeupDb * 0.5) }, { C::maxMakeupDb, shortNumber (C::maxMakeupDb) } },
                       [] (double v) { return "+" + juce::String (v, 1) + " dB"; } } };
    const K row2[] { { "Attack", ids::busCompAttack, 5.0, 0.0, (double) C::attacksMs.size() - 1,
                       steps (C::attacksMs, [] (size_t i) { return shortNumber (C::attacksMs[i]); }), [] (double v) { return C::attackText (juce::roundToInt (v)) + " ms"; } },
                     { "Release", ids::busCompRelease, (double) C::autoRelease, 0.0, (double) C::releasesS.size() - 1,
                       steps (C::releasesS, [] (size_t i) { return (int) i == C::autoRelease ? juce::String ("A") : shortNumber (C::releasesS[i]); }),
                       [] (double v) { return juce::roundToInt (v) == C::autoRelease ? juce::String ("Auto") : C::releaseText (juce::roundToInt (v)) + " s"; } },
                     { "Mix", ids::busCompMix, 1.0, 0.0, 1.0, { { 0.0, "0" }, { 0.5, "50" }, { 1.0, "100" } },
                       [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "% wet"; } } };
    const int cw = (w - 8) / 3;
    for (const auto* row : { row1, row2 })
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto& def = row[i];
            auto* k = new AnalogKnob (capWhite, def.caption);
            const auto stepped = def.property == ids::busCompRatio || def.property == ids::busCompAttack || def.property == ids::busCompRelease;
            k->setRange (def.lo, def.hi, stepped ? 1.0 : 0.0);
            k->setDoubleClickReturnValue (true, def.fallback);
            k->textFromValueFunction = def.text;
            k->scale = def.scale;
            k->knobSize = 28;
            k->setValue ((double) project.getProperty (def.property, def.fallback), juce::dontSendNotification);
            k->setTooltip (def.caption);
            auto tree = project;
            const auto property = def.property;
            k->onDragStart = [&um] { um.beginNewTransaction ("Bus compressor"); };
            k->onValueChange = [k, tree, property, &um] { MasterBusCompressor::setInProject (tree, property, k->getValue(), &um); };
            k->setBounds (4 + i * cw, y, cw, 52);
            s.addAndMakeVisible (k);
            s.parts.add (k);
            s.bindings.push_back ({ k, project, def.property, def.fallback, false });
        }
        y += 52 + 13;
    }
    s.grArea = { 14, y, w - 28, 6 };
    y += 12;
    const auto key = [&] (const juce::String& text, const juce::Identifier& property, bool fallback, juce::Rectangle<int> where, const juce::String& tip)
    {
        auto* b = new ConsoleKey (text, keyAmber);
        b->setTooltip (tip);
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
    key ("COMP", ids::busCompOn, false, { 14, y, w / 2 - 18, 18 }, "The bus compressor in");
    key ("COLOR", "busCompColor", false, { w / 2 + 4, y, w / 2 - 18, 18 }, "Color: a small bump in the lows (+3 dB round 60 Hz)");
    y += 28;
    // the MultiMeter, right here
    s.sectionTitles.push_back ({ "MULTIMETER", juce::Rectangle<int> (0, y, w, 14) });
    y += 16;
    if (host.engine != nullptr)
    {
        meter = std::make_unique<ConsoleMeter> (*host.engine, project);
        meter->setBounds (8, y, w - 16, 150);
        s.addAndMakeVisible (*meter);
        y += 156;
    }
    s.sectionTitles.push_back ({ "MASTER INSERTS", juce::Rectangle<int> (0, y, w, 14) });
    y += 16;
    {
        const auto inserts = ordered (model::findChain (project, model::masterOwner));
        int next = 0;
        for (const auto& insert : inserts)
            next = juce::jmax (next, model::insertSlot (insert) + 1);
        for (int i = 0; i < juce::jmax (4, inserts.size() + 1); ++i)
        {
            InsertSlot* slot;
            if (i < inserts.size())
            {
                auto insert = inserts[i];
                slot = new InsertSlot (nameOf (insert), isPlugin (insert) ? 2 : 1, ! (bool) insert.getProperty (ids::bypass, false),
                                       [this, insert] { if (host.openInsert) host.openInsert (insert); });
            }
            else
            {
                slot = new InsertSlot ({}, 0, false, {});
                auto* raw = slot;
                slot->click = [this, next, raw] { if (host.chooseEffect) host.chooseEffect (model::masterOwner, next, {}, *raw); };
                slot->setTooltip ("Add a master insert");
            }
            slot->setBounds (7, y, w - 14, 17);
            s.addAndMakeVisible (slot);
            s.parts.add (slot);
            y += 19;
        }
    }
    y += 10;
    // Bounce, beside the master's level (as Logic's Bnce)
    auto* bounce = new ConsoleKey ("Bnce", juce::Colours::white);
    bounce->setClickingTogglesState (false);
    bounce->setTooltip ("Bounce: the song (or the cycle) to a file");
    bounce->onClick = [this] { if (host.bounce) host.bounce(); };
    s.addAndMakeVisible (bounce);
    s.parts.add (bounce);
    s.faderArea = { 4, y, w - 8, 230 };
}

void AnalogConsole::rebuild()
{
    rebuildWanted = false;
    meter.reset();
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
    master->number = "M";
    master->colour = juce::Colour (0xffc7c7cc);
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
        s.level.setJustificationType (juce::Justification::centred);
        s.level.setColour (juce::Label::textColourId, juce::Colour (0xff34c759));
        s.level.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
        s.level.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        s.addAndMakeVisible (s.level);
        refreshBindings (s);
    };
    for (auto* s : strips)
        setUpFader (*s);
    setUpFader (*master);
    resized();
}

void AnalogConsole::Strip::paintMeter (juce::Graphics& g, juce::Rectangle<float> area, float peak, float hold) const
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (area, 2.0f);
    const int segments = juce::jmax (10, (int) (area.getHeight() / 5.0f));
    const auto inner = area.reduced (1.0f, 2.0f);
    const auto segH = inner.getHeight() / (float) segments;
    // (the scale: -60 at the bottom to +6 at the top, as the fader's)
    const auto fracOf = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
    const auto lit = fracOf (juce::Decibels::gainToDecibels (peak, -100.0f));
    const auto held = fracOf (juce::Decibels::gainToDecibels (hold, -100.0f));
    for (int q = 0; q < segments; ++q)
    {
        const auto frac = (float) (q + 1) / (float) segments;
        const auto colour = frac > fracOf (0.0f) ? juce::Colour (0xffff3b30) : frac > fracOf (-6.0f) ? juce::Colour (0xffffcc00) : juce::Colour (0xff34c759);
        const auto r = juce::Rectangle<float> (inner.getX(), inner.getBottom() - (float) (q + 1) * segH + 0.5f, inner.getWidth(), segH - 1.0f);
        const bool on = frac <= lit + 0.5f / (float) segments;
        const bool peakMark = std::abs (frac - held) < 0.5f / (float) segments && hold > 0.0001f;
        g.setColour (on || peakMark ? colour : juce::Colours::white.withAlpha (0.07f));
        g.fillRect (r);
    }
}

void AnalogConsole::Strip::paint (juce::Graphics& g)
{
    const auto& d = desk();
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    g.setGradientFill (juce::ColourGradient (d.top.withAlpha (0.92f), 0.0f, r.getY(), d.bottom.withAlpha (0.92f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (d.edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    // the header: number, name, the track's colour under it
    auto head = getLocalBounds().reduced (8, 0).removeFromTop (28);
    g.setColour (juce::Colours::black.withAlpha (theme::glassLight() ? 0.08f : 0.3f));
    const auto numberBox = head.removeFromLeft (isMaster ? 22 : (number.length() > 1 ? 24 : 18)).withSizeKeepingCentre (isMaster ? 22 : (number.length() > 1 ? 24 : 18), 16).toFloat();
    g.fillRoundedRectangle (numberBox, 4.0f);
    g.setColour (d.title.brighter (theme::glassLight() ? 0.0f : 0.4f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (number, numberBox, juce::Justification::centred, false);
    head.removeFromLeft (6);
    g.drawFittedText (isMaster ? juce::String ("Master") : name, head, juce::Justification::centredLeft, 1, 0.75f);
    g.setColour (colour);
    g.fillRect (r.getX() + 1.0f, 28.0f, r.getWidth() - 2.0f, 2.0f);

    // the section titles and a line above each
    for (const auto& [title, area] : sectionTitles)
    {
        g.setColour (d.lineDark);
        g.fillRect (area.getX() + 3, area.getY() - 4, area.getWidth() - 6, 1);
        g.setColour (d.lineLight);
        g.fillRect (area.getX() + 3, area.getY() - 3, area.getWidth() - 6, 1);
        const bool inputFx = title == "INPUT FX";
        g.setColour (inputFx ? juce::Colour (0xff34c759) : d.title);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold).withKerningFactor (0.14f));
        if (inputFx)
        {
            g.drawText (title, area.reduced (8, 0), juce::Justification::centredLeft, false);
            g.drawText (inputFxTime, area.reduced (8, 0), juce::Justification::centredRight, false);
        }
        else
            g.drawText (title, area, juce::Justification::centred, false);
    }
    // the captions under the knobs (while one's turned: its value)
    for (auto* part : parts)
        if (auto* k = dynamic_cast<AnalogKnob*> (part))
        {
            const bool turning = k->isMouseButtonDown();
            g.setColour (turning ? juce::Colour (0xffffb340) : d.caption.withMultipliedAlpha (k->isEnabled() ? 1.0f : 0.5f));
            g.setFont (juce::FontOptions (9.0f, turning ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (k->captionNow(), juce::Rectangle<int> (k->getX() - 4, k->getBottom() - 1, k->getWidth() + 8, 11), juce::Justification::centred, 1, 0.7f);
        }
    // the compressor's gain reduction: a column of amber lights (the master's: a row)
    if (! grArea.isEmpty())
    {
        const bool row = isMaster;
        const auto lights = row ? 16 : 10;
        const auto lit = juce::jlimit (0, lights, juce::roundToInt (gr / (row ? 1.0f : 2.0f)));
        for (int i = 0; i < lights; ++i)
        {
            juce::Rectangle<float> cell;
            if (row)
            {
                const auto cw = (float) grArea.getWidth() / (float) lights;
                cell = { (float) grArea.getX() + (float) i * cw + 1.0f, (float) grArea.getY(), cw - 2.0f, (float) grArea.getHeight() };
            }
            else
            {
                const auto ch = (float) grArea.getHeight() / (float) lights;
                cell = { (float) grArea.getCentreX() - 7.0f, (float) grArea.getY() + (float) i * ch + 0.5f, 14.0f, ch - 1.5f };
            }
            g.setColour (i < lit ? juce::Colour (0xffffb340) : juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (cell, 1.0f);
        }
        if (! row)
        {
            g.setColour (d.caption);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("GR", grArea.translated (0, grArea.getHeight()).withHeight (11), juce::Justification::centred, false);
        }
    }
    // the master's compression meter: the bus compressor's black meter
    if (isMaster && ! compMeter.isEmpty())
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (compMeter.toFloat().expanded (3.0f), 8.0f);
        looks::drawStripCompMeter (g, compMeter.toFloat(), gr);
    }
    // the fader's printed scale: ticks both sides of the slot, the numbers to the left
    if (fader.getHeight() > 60)
    {
        const auto cx = (float) fader.getBounds().getCentreX();
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        for (const auto& [db, text] : { std::pair { 6.0, "+6" }, std::pair { 0.0, "0" }, std::pair { -5.0, "5" }, std::pair { -10.0, "10" },
                                        std::pair { -15.0, "15" }, std::pair { -20.0, "20" }, std::pair { -30.0, "30" }, std::pair { -40.0, "40" },
                                        std::pair { minDb, "\xe2\x88\x9e" } })
        {
            const auto y = (float) fader.getY() + const_cast<strip::Fader&> (fader).yForValue (db);
            const bool major = db == 6.0 || db == 0.0 || db == minDb;
            g.setColour (d.scale.withMultipliedAlpha (major ? 1.0f : 0.6f));
            g.fillRect (cx - 16.0f, y - 0.5f, 8.0f, major ? 1.4f : 1.0f);
            g.fillRect (cx + 8.0f, y - 0.5f, 8.0f, major ? 1.4f : 1.0f);
            g.setColour (d.scale);
            g.drawText (juce::String (juce::CharPointer_UTF8 (text)), juce::Rectangle<float> (cx - 40.0f, y - 6.0f, 21.0f, 12.0f), juce::Justification::centredRight, false);
        }
    }
    // its LED meter (left and right)
    if (! meterArea.isEmpty())
    {
        auto m = meterArea.toFloat();
        const auto half = (m.getWidth() - 2.0f) * 0.5f;
        paintMeter (g, m.removeFromLeft (half), peakL, holdL);
        m.removeFromLeft (2.0f);
        paintMeter (g, m, peakR, holdR);
    }
    // the name plate in the track's colour
    auto plate = getLocalBounds().reduced (6, 0).removeFromBottom (30).withTrimmedBottom (4).toFloat();
    g.setColour (d.plate);
    g.fillRoundedRectangle (plate, 7.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (plate.removeFromBottom (3.0f), 1.5f);
    g.setColour (d.title.brighter (theme::glassLight() ? 0.0f : 0.5f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (name, getLocalBounds().reduced (8, 0).removeFromBottom (30).withTrimmedBottom (6), juce::Justification::centred, 1, 0.7f);
}

void AnalogConsole::timerCallback()
{
    if (! isShowing() || host.engine == nullptr || host.player == nullptr)
        return;
    const auto ballistics = [] (Strip& s, float l, float r)
    {
        s.peakL = juce::jmax (l, s.peakL * 0.85f);
        s.peakR = juce::jmax (r, s.peakR * 0.85f);
        for (auto [peak, hold, ticks] : { std::tuple { s.peakL, &s.holdL, &s.holdTicksL }, std::tuple { s.peakR, &s.holdR, &s.holdTicksR } })
        {
            if (peak >= *hold) { *hold = peak; *ticks = 0; }
            else if (++*ticks > 45) *hold = juce::jmax (peak, *hold * 0.9f);
        }
        s.repaint (s.meterArea.expanded (2));
    };
    for (auto* s : strips)
    {
        const auto slot = host.player->slotOf (s->track[ids::id].toString());
        const auto [l, r] = host.engine->takeTrackLevelForConsole (slot);
        ballistics (*s, l, r);
        const auto gr = host.engine->getConsoleGainReduction (slot);
        if (std::abs (gr - s->gr) > 0.25f)
        {
            s->gr = gr;
            s->repaint (s->grArea.expanded (2, 14));
        }
    }
    if (master != nullptr)
    {
        if (host.masterLevels)
        {
            const auto [l, r] = host.masterLevels();
            ballistics (*master, l, r);
        }
        // the compression meter: a meter's swing (about 300 ms to a reading), not every peak
        const auto target = host.engine->peekBusCompGainReduction();
        master->gr += (target - master->gr) * (target > master->gr ? 0.35f : 0.12f);
        master->repaint (master->compMeter.expanded (4));
        master->repaint (master->grArea.expanded (2));
    }
}

void AnalogConsole::paint (juce::Graphics& g)
{
    const bool light = theme::glassLight();
    g.setGradientFill (juce::ColourGradient (light ? juce::Colour (0xffe9ecf3) : juce::Colour (0xff15161b), 0.0f, 0.0f,
                                             light ? juce::Colour (0xffdfe3ec) : juce::Colour (0xff0d0e12), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (light ? juce::Colour (0xff1c1d22) : theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Analog Console", 16, 0, 200, 44, juce::Justification::centredLeft, false);
    g.setColour (light ? juce::Colour (0xff1c1d22).withAlpha (0.55f) : theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("input, Input FX, dynamics, filters, EQ, inserts, sends, pan and fader on every channel", 160, 0, 620, 44, juce::Justification::centredLeft, false);
}

void AnalogConsole::resized()
{
    auto top = getLocalBounds().removeFromTop (44).reduced (12, 10);
    for (int i = filterKeys.size(); --i >= 0;)
    {
        filterKeys[i]->setBounds (top.removeFromRight (64));
        top.removeFromRight (4);
    }
    auto area = getLocalBounds().withTrimmedTop (44).reduced (6, 0).withTrimmedBottom (6);
    view.setBounds (area);
    // every fader starts at the same height (as on a desk), below the tallest channel's sections
    int faderTop = 0;
    for (auto* s : strips)
        faderTop = juce::jmax (faderTop, s->faderArea.getY());
    if (master != nullptr)
        faderTop = juce::jmax (faderTop, master->faderArea.getY());
    for (auto* s : strips)
        s->faderArea.setY (faderTop);
    if (master != nullptr)
        master->faderArea.setY (faderTop);
    const auto contentHeight = juce::jmax (faderTop + 250 + 64, area.getHeight() - view.getScrollBarThickness());
    const auto width = strips.size() * (columnWidth + 4) + masterWidth + 12;
    holder.setSize (juce::jmax (width, area.getWidth() - 2), contentHeight);
    auto placeFader = [] (Strip& s, int height)
    {
        auto f = s.faderArea.withHeight (juce::jmax (140, height - s.faderArea.getY() - 64));
        // scale numbers | slot and cap | LED meter
        const auto meterWidth = 14;
        s.meterArea = juce::Rectangle<int> (f.getRight() - meterWidth - 8, f.getY() + 4, meterWidth, f.getHeight() - 8);
        const auto faderCentre = f.getX() + (f.getWidth() - meterWidth - 8) / 2 + 12;
        s.fader.setBounds (juce::Rectangle<int> (44, f.getHeight()).withCentre ({ faderCentre, f.getCentreY() }));
        s.level.setBounds (faderCentre - 24, f.getBottom() + 4, 48, 18);
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
        for (auto* part : master->parts)
            if (auto* key = dynamic_cast<ConsoleKey*> (part); key != nullptr && key->getButtonText() == "Bnce")
                key->setBounds (masterWidth - 60, master->level.getY(), 50, 20);
    }
}

} // namespace riff
