#include "StudioPanel.h"
#include "../engine/studio/StudioInstruments.h"
#include "../model/Project.h"
#include "../model/StudioState.h"
#include "Looks.h"
#include "../engine/BuiltInEffects.h"
#include "Theme.h"
#include "../model/Presets.h"
#include "../engine/AutomationWriter.h"
#include "InstrumentWindows.h"
#include <BinaryData.h>

namespace riff
{

namespace
{
    const juce::Colour teal (0xff19d3d0), amber (0xffe8a028), ink (0xffeef2f6), dim (0xff98a3ae);
    const juce::Colour purple (0xffa25bff), blue (0xff3f8cff), green (0xff3fd06a), red (0xffe8231d);

    /** The silver knobs' skirts by section: teal the sound, purple the space and envelopes, amber drive, compression
        and filter, blue the amp's and the piano's tone, green the swing, red the master and volume. */
    juce::Colour sectionColour (const juce::String& kind, const juce::String& id)
    {
        if (id == "master" || id == "level" || id == "mainGain") return red;
        if (id == "swing") return green;
        if (id == "comp" || id == "drive" || id == "cutoff" || id == "reso" || id == "#Filter" || id == "#Reso") return amber;
        if (id == "overheads" || id == "room" || id == "ambience" || id == "reverb"
            || (kind == "sampler" && (id == "attack" || id == "decay" || id == "sustain" || id == "release")))
            return purple;
        if (id == "bass" || id == "mid" || id == "treble" || (kind == "keys" && (id == "tone" || id == "resonance"))) return blue;
        return teal;
    }
    juce::Font font (float size, bool bold = true) { return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain); }

    juce::Image faceImage (const juce::String& kind)
    {
        if (kind == "keys")    return juce::ImageCache::getFromMemory (BinaryData::st_keys_jpg, BinaryData::st_keys_jpgSize);
        if (kind == "bass")    return juce::ImageCache::getFromMemory (BinaryData::st_bass_jpg, BinaryData::st_bass_jpgSize);
        if (kind == "guitar")  return juce::ImageCache::getFromMemory (BinaryData::st_guitar_jpg, BinaryData::st_guitar_jpgSize);
        if (kind == "beatpad") return juce::ImageCache::getFromMemory (BinaryData::st_beatpad_jpg, BinaryData::st_beatpad_jpgSize);
        return juce::ImageCache::getFromMemory (BinaryData::st_drums_jpg, BinaryData::st_drums_jpgSize);   // (drums, and the sampler's top)
    }
}

//==============================================================================
struct StudioPanel::Control
{
    enum Type { knob, fader, button, padKey, encoder } type = knob;
    juce::Point<float> centre;        // knobs and encoders
    float radius = 20.0f;
    juce::Rectangle<float> area;      // faders (the slot), buttons and pads
    juce::String label, id;           // the parameter it works (knobs, faders, encoders)
    std::function<void()> action;     // buttons and pads
    std::function<bool()> lit;
    std::function<juce::String()> caption;
    juce::Colour colour = teal;
    int pad = -1, note = -1;
    bool labelAbove = false;
};

struct StudioPanel::Layout
{
    juce::Rectangle<int> crop;              // the part of the picture shown (picture pixels)
    float height = 0.0f;                    // the face's height (face coordinates): the picture's, or more (the sampler's editor)
    juce::Rectangle<float> screen;          // the screen (face coordinates)
    juce::Rectangle<float> mix;             // where the MIX tab covers
    juce::StringArray screenTabs;
    struct Hot { juce::Point<float> c; float r; int piece; int note; };
    std::vector<Hot> pieces;                // the drums' pieces
    juce::Rectangle<float> keys;            // the keyboard
    int lowestKey = 36, whiteKeys = 36;
    std::vector<float> strings;             // the strings' heights (bass, guitar)
    float stringLeft = 0, stringRight = 0;
    juce::Rectangle<float> editor;          // the sampler's editor
    struct Bar { juce::String label, id; };
    std::vector<Bar> bar;
};

//==============================================================================
// The MIX tab's strip
struct StudioPanel::Strip final : juce::Component
{
    /** An insert slot: a click opens it (or chooses one); a right-click: Open, Replace, Bypass, Remove (as Logic's). */
    struct Slot final : juce::TextButton
    {
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }
            juce::TextButton::mouseDown (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::TextButton::mouseUp (e);
        }
    };

    StudioPanel& panel;
    int part;                 // -1 FX A, -2 FX B, -3 main
    juce::String name, owner;
    juce::TextButton output;
    juce::OwnedArray<Slot> inserts;
    juce::Rectangle<int> bypassArea;   // (the strip's bypass key: all its inserts out, as Cubase's)
    juce::Slider sendA, sendB, pan;
    juce::TextButton mute { "M" }, solo { "S" };
    juce::Rectangle<int> faderArea, meterArea;
    float level = 0.0f;
    bool dragging = false;

    Strip (StudioPanel& p, int partIndex, juce::String n, juce::String chainOwner)
        : panel (p), part (partIndex), name (std::move (n)), owner (std::move (chainOwner))
    {
        const auto gainId = id ("gain");
        if (part >= 0)
        {
            output.onClick = [this] { showOutputMenu(); };
            addAndMakeVisible (output);
            for (auto* s : { &sendA, &sendB, &pan })
            {
                s->setColour (juce::Slider::backgroundColourId, juce::Colours::white.withAlpha (0.06f));
                s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            }
            for (auto* s : { &sendA, &sendB })
            {
                s->setSliderStyle (juce::Slider::LinearBar);
                s->setRange (-60.0, 6.0, 0.1);
                s->setColour (juce::Slider::trackColourId, teal.withAlpha (0.35f));
                s->setColour (juce::Slider::textBoxTextColourId, ink);
                addAndMakeVisible (*s);
            }
            sendA.onValueChange = [this] { panel.setValue (id ("sendA"), (float) sendA.getValue(), ! sendA.isMouseButtonDown()); };
            sendB.onValueChange = [this] { panel.setValue (id ("sendB"), (float) sendB.getValue(), ! sendB.isMouseButtonDown()); };
            pan.setSliderStyle (juce::Slider::LinearBar);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setColour (juce::Slider::trackColourId, juce::Colours::white.withAlpha (0.15f));
            pan.setColour (juce::Slider::textBoxTextColourId, ink);
            pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0)); };
            pan.onValueChange = [this] { panel.setValue (id ("pan"), (float) pan.getValue(), ! pan.isMouseButtonDown()); };
            pan.updateText();
            sendA.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("FX A  off") : "FX A  " + juce::String (v, 1); };
            sendB.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("FX B  off") : "FX B  " + juce::String (v, 1); };
            addAndMakeVisible (pan);
            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (false);
                addAndMakeVisible (*b);
            }
            mute.onClick = [this] { panel.setValue (id ("mute"), panel.value (id ("mute")) > 0.5f ? 0.0f : 1.0f, true); };
            solo.onClick = [this] { panel.setValue (id ("solo"), panel.value (id ("solo")) > 0.5f ? 0.0f : 1.0f, true); };
        }
        juce::ignoreUnused (gainId);
        for (int i = 0; i < 4; ++i)
        {
            auto* b = inserts.add (new Slot());
            b->onClick = [this, i, b]
            {
                auto insert = model::insertAtSlot (model::findChain (panel.project, owner), i);
                if (insert.isValid() && juce::ModifierKeys::currentModifiers.isAltDown())
                {
                    panel.um.beginNewTransaction ("Bypass insert");
                    insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &panel.um);
                }
                else if (insert.isValid())
                {
                    if (panel.host.openInsert) panel.host.openInsert (insert);
                }
                else if (panel.host.chooseEffect)
                    panel.host.chooseEffect (owner, i, insert, *b);
            };
            b->onMenu = [this, i, b] { showSlotMenu (i, *b); };
            addAndMakeVisible (b);
        }
        refresh();
    }

    void showSlotMenu (int i, juce::Component& target)
    {
        auto insert = model::insertAtSlot (model::findChain (panel.project, owner), i);
        juce::PopupMenu menu;
        if (insert.isValid())
        {
            menu.addItem ("Open", [this, insert] { if (panel.host.openInsert) panel.host.openInsert (insert); });
            menu.addItem ("Replace...", [this, i, insert, &target] { if (panel.host.chooseEffect) panel.host.chooseEffect (owner, i, insert, target); });
            menu.addItem ("Bypass", true, (bool) insert[ids::bypass], [this, insert]() mutable
            {
                panel.um.beginNewTransaction ("Bypass insert");
                insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &panel.um);
            });
            menu.addSeparator();
            menu.addItem ("Remove", [this, insert]
            {
                panel.um.beginNewTransaction ("Remove insert");
                model::removeInsert (insert, &panel.um);
            });
        }
        else
            menu.addItem ("Add an effect...", [this, i, &target] { if (panel.host.chooseEffect) panel.host.chooseEffect (owner, i, {}, target); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }

    /** Whether every insert of the strip is bypassed (the key lit); false with none. */
    bool allBypassed() const
    {
        const auto all = model::insertsOf (model::findChain (panel.project, owner));
        if (all.isEmpty()) return false;
        for (const auto& insert : all)
            if (! (bool) insert[ids::bypass]) return false;
        return true;
    }
    void toggleBypass()
    {
        const auto all = model::insertsOf (model::findChain (panel.project, owner));
        if (all.isEmpty()) return;
        const auto to = ! allBypassed();
        panel.um.beginNewTransaction (to ? "Bypass inserts" : "Activate inserts");
        for (auto insert : all)
            insert.setProperty (ids::bypass, to, &panel.um);
    }

    void showOutputMenu()
    {
        // Main, or its own channel in the mixer (made for it, and the part routed there: as Logic's multi-output)
        const auto own = panel.value (id ("out")) > 0.5f;
        juce::PopupMenu menu;
        menu.addItem ("Main", true, ! own, [this] { panel.setValue (id ("out"), 0.0f, true); });
        menu.addItem ("Own channel (outputs " + juce::String (part * 2 + 3) + "-" + juce::String (part * 2 + 4) + ")", true, own, [this]
        {
            panel.um.beginNewTransaction ("Multi-output");
            panel.state.setProperty (juce::Identifier (id ("out")), 1.0f, &panel.um);
            model::addMultiOutput (panel.project, panel.track, part + 1, panel.track[ids::name].toString() + " " + name, &panel.um);
            refresh();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&output));
    }

    juce::String id (const char* what) const
    {
        if (part == -1) return juce::String (what) == "gain" ? "fxAGain" : juce::String();
        if (part == -2) return juce::String (what) == "gain" ? "fxBGain" : juce::String();
        if (part == -3) return juce::String (what) == "gain" ? "mainGain" : juce::String();
        return "p" + juce::String (part) + what;
    }

    void refresh()
    {
        const auto chain = model::findChain (panel.project, owner);
        for (int i = 0; i < inserts.size(); ++i)
        {
            const auto insert = model::insertAtSlot (chain, i);
            auto* b = inserts[i];
            juce::String text;
            bool plugin = false;
            if (insert.isValid())
            {
                plugin = insert[ids::kind].toString() == model::pluginKind;
                juce::PluginDescription d;
                if (plugin)
                    if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
                        text = d.name;
                if (text.isEmpty())
                    for (const auto& info : builtInEffects())
                        if (info.type == insert[ids::effect].toString()) { text = info.name; break; }
                if (text.isEmpty()) text = insert[ids::effect].toString();
            }
            b->setButtonText (text.isEmpty() ? juce::String ("+") : text);
            b->setAlpha (insert.isValid() && (bool) insert[ids::bypass] ? 0.4f : 1.0f);
            b->setColour (juce::TextButton::buttonColourId, text.isEmpty() ? juce::Colours::white.withAlpha (0.06f) : plugin ? juce::Colour (0xd03fa8ff) : juce::Colour (0xd0d6962a));
            b->setColour (juce::TextButton::textColourOffId, text.isEmpty() ? dim : plugin ? juce::Colours::white : juce::Colour (0xff1a1206));
            b->setTooltip (text.isEmpty() ? "Add an insert: one of ours or any plug-in" : text + ": click to open, right-click to replace or remove");
        }
        if (part >= 0)
        {
            const auto outChoices = panel.layout != nullptr ? juce::StringArray { "Main", "Own channel" } : juce::StringArray();
            output.setButtonText (panel.value (id ("out")) > 0.5f ? "Own " + juce::String (part * 2 + 3) + "-" + juce::String (part * 2 + 4) + juce::String (juce::CharPointer_UTF8 (" ▾")) : juce::String (juce::String (juce::CharPointer_UTF8 ("Main ▾"))));
            juce::ignoreUnused (outChoices);
            sendA.setValue (panel.value (id ("sendA")), juce::dontSendNotification);
            sendB.setValue (panel.value (id ("sendB")), juce::dontSendNotification);
            pan.setValue (panel.value (id ("pan")), juce::dontSendNotification);
            mute.setColour (juce::TextButton::buttonColourId, panel.value (id ("mute")) > 0.5f ? amber : juce::Colours::white.withAlpha (0.08f));
            solo.setColour (juce::TextButton::buttonColourId, panel.value (id ("solo")) > 0.5f ? juce::Colour (0xffffd84a) : juce::Colours::white.withAlpha (0.08f));
            sendA.updateText();
            sendB.updateText();
            pan.updateText();
        }
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (5, 4);
        bypassArea = juce::Rectangle<int> (getWidth() - 21, 5, 14, 14);
        r.removeFromTop (18);
        if (part >= 0)
        {
            output.setBounds (r.removeFromTop (17));
            r.removeFromTop (4);
        }
        for (auto* b : inserts)
        {
            b->setBounds (r.removeFromTop (16));
            r.removeFromTop (2);
        }
        if (part >= 0)
        {
            r.removeFromTop (3);
            sendA.setBounds (r.removeFromTop (15));
            r.removeFromTop (2);
            sendB.setBounds (r.removeFromTop (15));
            r.removeFromTop (3);
            pan.setBounds (r.removeFromTop (15));
            r.removeFromTop (3);
            auto ms = r.removeFromTop (16);
            mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2 - 1));
            ms.removeFromLeft (2);
            solo.setBounds (ms);
        }
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (16);
        juce::ignoreUnused (bottom);
        faderArea = r.withWidth (r.getWidth() - 14).withTrimmedBottom (2);
        meterArea = juce::Rectangle<int> (r.getRight() - 9, r.getY() + 6, 6, r.getHeight() - 14);
    }

    float dbToY (float db) const
    {
        const auto norm = db <= -60.0f ? 0.0f : std::pow ((db + 60.0f) / 72.0f, 2.0f);
        const auto top = (float) faderArea.getY() + 20.0f, bottom = (float) faderArea.getBottom() - 20.0f;
        return bottom - norm * (bottom - top);
    }
    float yToDb (float y) const
    {
        const auto top = (float) faderArea.getY() + 20.0f, bottom = (float) faderArea.getBottom() - 20.0f;
        const auto norm = juce::jlimit (0.0f, 1.0f, (bottom - y) / (bottom - top));
        return std::sqrt (norm) * 72.0f - 60.0f;
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto tint = part == -3 ? teal : part < 0 ? juce::Colour (0xffa25bff) : juce::Colours::white;
        g.setColour (tint.withAlpha (part < 0 ? 0.08f : 0.05f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (tint.withAlpha (part < 0 ? 0.4f : 0.15f));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        g.setColour (ink);
        g.setFont (font (11.0f));
        g.drawFittedText (name.toUpperCase(), getLocalBounds().reduced (20, 0).removeFromTop (20), juce::Justification::centred, 1, 0.7f);
        {
            // the bypass key: small and round (the power sign), lit amber when the strip's inserts are out
            const auto b = bypassArea.toFloat();
            const auto on = allBypassed();
            g.setColour (on ? amber : juce::Colours::white.withAlpha (0.10f));
            g.fillEllipse (b);
            g.setColour (on ? juce::Colour (0xff1a1206) : dim);
            juce::Path power;
            power.addCentredArc (b.getCentreX(), b.getCentreY(), b.getWidth() * 0.28f, b.getWidth() * 0.28f, 0.0f,
                                 juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
            g.strokePath (power, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (b.getCentreX(), b.getY() + 3.0f, b.getCentreX(), b.getCentreY(), 1.2f);
        }
        // the slot, the scale, the meter, the white glass cap
        const auto gain = panel.value (id ("gain"));
        const auto cx = (float) faderArea.getX() + (float) faderArea.getWidth() * 0.4f;
        g.setColour (juce::Colour (0xff050608));
        g.fillRoundedRectangle (cx - 2.0f, (float) faderArea.getY() + 14.0f, 4.0f, (float) faderArea.getHeight() - 28.0f, 2.0f);
        g.setFont (font (8.5f, false));
        g.setColour (juce::Colour (0xffcfd3d8));
        for (const auto db : { 6, 0, -5, -10, -20, -30, -60 })
        {
            const auto y = dbToY ((float) db);
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (cx + 10.0f, y - 6.0f, 22.0f, 12.0f), juce::Justification::centredLeft, false);
        }
        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRect (meterArea);
        const auto m = juce::jlimit (0.0f, 1.0f, level);
        const auto mh = (float) meterArea.getHeight() * std::pow (m, 0.5f);
        g.setGradientFill (juce::ColourGradient (teal, 0.0f, (float) meterArea.getBottom(), juce::Colour (0xffff3b54), 0.0f, (float) meterArea.getY(), false));
        g.fillRect (juce::Rectangle<float> ((float) meterArea.getX(), (float) meterArea.getBottom() - mh, (float) meterArea.getWidth(), mh));
        const auto cap = looks::whiteGlassCap();
        const auto ch = 40.0f, cw = cap.isValid() ? ch * (float) cap.getWidth() / (float) cap.getHeight() : 22.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (cap.isValid())
            g.drawImage (cap, juce::Rectangle<float> (cw, ch).withCentre ({ cx, dbToY (gain) }));
        g.setColour (teal);
        g.setFont (font (10.0f, false));
        g.drawText (gain <= -59.9f ? juce::String ("-inf") : juce::String (gain, 1) + " dB", getLocalBounds().removeFromBottom (18), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (bypassArea.expanded (2).contains (e.getPosition()))
        {
            toggleBypass();
            repaint();
            return;
        }
        dragging = faderArea.expanded (6).contains (e.getPosition());
        if (dragging) panel.um.beginNewTransaction ("Instrument mix");
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging) panel.setValue (id ("gain"), yToDb ((float) e.position.y), false);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (faderArea.contains (e.getPosition())) panel.setValue (id ("gain"), 0.0f, true);
    }
};

//==============================================================================
juce::Rectangle<int> StudioPanel::naturalSize (const juce::String& kind)
{
    const auto barHeight = 96;
    if (kind == "keys")    return { 0, 0, 1180, juce::roundToInt ((620.0f + barHeight) * 1180.0f / 1408.0f) };
    if (kind == "sampler") return { 0, 0, 1180, juce::roundToInt ((690.0f + barHeight) * 1180.0f / 1408.0f) };
    if (kind == "beatpad") return { 0, 0, 1180, juce::roundToInt ((690.0f + barHeight) * 1180.0f / 1408.0f) };
    return { 0, 0, 1180, juce::roundToInt ((730.0f + barHeight) * 1180.0f / 1408.0f) };
}

StudioPanel::StudioPanel (juce::ValueTree p, juce::ValueTree t, juce::UndoManager& u, Host h)
    : project (std::move (p)), track (std::move (t)), um (u), host (std::move (h))
{
    kind = studiostate::kindOf (track);
    state = studiostate::getOrCreate (track, nullptr);
    layout = std::make_unique<Layout>();
    face = faceImage (kind);
    auto& L = *layout;
    auto inst = host.instrument ? host.instrument() : nullptr;

    const auto knobAt = [this] (float x, float y, float r, const juce::String& label, const juce::String& id)
    {
        auto c = std::make_unique<Control>();
        c->type = Control::knob; c->centre = { x, y }; c->radius = r; c->label = label; c->id = id;
        controls.push_back (std::move (c));
    };
    const auto faderAt = [this] (float x, float top, float bottom, const juce::String& label, const juce::String& id)
    {
        auto c = std::make_unique<Control>();
        c->type = Control::fader; c->area = { x - 10.0f, top, 20.0f, bottom - top }; c->label = label; c->id = id;
        controls.push_back (std::move (c));
    };
    const auto buttonAt = [this] (float x, float y, float w, float h, const juce::String& label, std::function<void()> action,
                                  std::function<bool()> lit = {}, juce::Colour colour = teal)
    {
        auto c = std::make_unique<Control>();
        c->type = Control::button; c->area = juce::Rectangle<float> (w, h).withCentre ({ x, y }); c->label = label;
        c->action = std::move (action); c->lit = std::move (lit); c->colour = colour;
        controls.push_back (std::move (c));
    };
    const auto encoderAt = [this] (float x, float y, float r, const juce::String& label, const juce::String& id)
    {
        auto c = std::make_unique<Control>();
        c->type = Control::encoder; c->centre = { x, y }; c->radius = r; c->label = label; c->id = id;
        controls.push_back (std::move (c));
    };
    const auto choiceIs = [this] (const juce::String& id, int v) { return [this, id, v] { return juce::roundToInt (value (id)) == v; }; };
    const auto setChoice = [this] (const juce::String& id, int v) { return [this, id, v] { setValue (id, (float) v, true); }; };
    const auto mixTab = [this] { showPage (page == 1 ? 0 : 1); };

    // The panel row of the drum, bass and sampler pictures: 10 knobs, 4 faders, 12 pads, the screen
    const auto panelRow = [&] (const juce::StringArray& knobLabels, const juce::StringArray& knobIds,
                               const juce::StringArray& faderLabels, const juce::StringArray& faderIds, float yOff)
    {
        const float kx[5] { 180, 260, 340, 420, 500 };
        for (int i = 0; i < 10; ++i)
            knobAt (kx[i % 5], (i < 5 ? 108.0f : 186.0f) - yOff, 21.0f, knobLabels[i], knobIds[i]);
        const float fx[4] { 606, 653, 700, 746 };
        for (int i = 0; i < 4; ++i)
            faderAt (fx[i], 92.0f - yOff, 212.0f - yOff, faderLabels[i], faderIds[i]);
        L.screen = { 1008.0f, 82.0f - yOff, 250.0f, 133.0f };
    };
    const float px[4] { 825, 871, 917, 963 }, py[3] { 101, 148, 196 };

    if (kind == "drums" || kind == "sampler")
    {
        const auto yOff = 40.0f;
        L.crop = kind == "drums" ? juce::Rectangle<int> (0, 40, 1408, 730) : juce::Rectangle<int> (0, 40, 1408, 236);
        L.height = kind == "drums" ? 730.0f : 690.0f;
        if (kind == "drums")
        {
            panelRow ({ "GAIN", "TUNE", "DAMPEN", "PAN", "TONE", "OVERHEADS", "ROOM", "AMBIENCE", "REVERB", "MASTER" },
                      { "@Gain", "@Tune", "@Damp", "@Pan", "@Tone", "overheads", "room", "ambience", "reverb", "master" },
                      { "KICK", "SNARE", "TOMS", "CYMB" }, { "p0gain", "p1gain", "p2gain", "p4gain" }, yOff);
            const juce::StringArray padLabels { "KICK", "SNARE", "TOMS", "HATS", "CRASH", "RIDE", "MIX", "ARTIC", juce::String (juce::CharPointer_UTF8 ("◀ KIT")), juce::String (juce::CharPointer_UTF8 ("KIT ▶")), "GROOVE", "EDIT" };
            const int padPiece[] { 0, 1, 2, 4, 5, 6 };
            for (int i = 0; i < 12; ++i)
            {
                std::function<void()> act;
                std::function<bool()> lit;
                if (i < 6)
                {
                    const auto piece = padPiece[i];
                    act = [this, piece] { setValue ("sel", (float) piece, true); if (host.playNote) { host.playNote (studio::StudioDrums::noteOfPiece (piece), 100, true); host.playNote (studio::StudioDrums::noteOfPiece (piece), 0, false); } };
                    lit = choiceIs ("sel", piece);
                }
                else if (i == 6) { act = mixTab; lit = [this] { return page == 1; }; }
                else if (i == 7) act = [this] { cycle ("artic", 1); };
                else if (i == 8) act = [this] { applyKit (juce::roundToInt (value ("kit")) - 1); };
                else if (i == 9) act = [this] { applyKit (juce::roundToInt (value ("kit")) + 1); };
                else if (i == 10)
                {
                    // GROOVE: a beat plays (the bar's Groove chooses which), in time with the song
                    act = [this]
                    {
                        const auto now = juce::roundToInt (value ("groove"));
                        if (now > 0) lastGroove = now;
                        setValue ("groove", now > 0 ? 0.0f : (float) juce::jmax (1, lastGroove), true);
                    };
                    lit = [this] { return value ("groove") > 0.5f; };
                }
                else { act = [this] { showPage (0); screenPage = screenPage == 1 ? 0 : 1; repaint(); }; lit = [this] { return page == 0 && screenPage == 1; }; }
                buttonAt (px[i % 4], py[i / 4] - yOff + 1.0f, 42.0f, 42.0f, padLabels[i], act, lit);
            }
            L.screenTabs = { "PLAY", "PIECE", "MIX", "ROOM", "MAP" };
            // the kit's pieces in the picture
            L.pieces = { { { 418, 360 - yOff }, 100, 5, 49 }, { { 372, 530 - yOff }, 85, 4, 42 }, { { 588, 400 - yOff }, 68, 2, 48 },
                         { { 565, 585 - yOff }, 88, 1, 38 }, { { 930, 600 - yOff }, 95, 3, 43 }, { { 915, 390 - yOff }, 100, 7, 57 },
                         { { 1113, 395 - yOff }, 118, 6, 51 }, { { 1138, 565 - yOff }, 95, 7, 52 }, { { 715, 450 - yOff }, 115, 0, 36 } };
            L.mix = { 100.0f, 236.0f, 1208.0f, 470.0f };
            L.bar = { { "Kit", "kit" }, { "Groove", "groove" }, { "Velocity", "velocity" }, { "Articulation", "artic" }, { "Hi-Hat", "hatPedal" } };
        }
        else
        {
            panelRow ({ "PITCH", "FINE", "CUTOFF", "RESO", "DRIVE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "LEVEL" },
                      { "pitch", "fine", "cutoff", "reso", "drive", "attack", "decay", "sustain", "release", "level" },
                      { "GLIDE", "LFO RT", "LFO AMT", "VEL" }, { "glide", "lfoRate", "lfoAmt", "velAmp" }, yOff);
            const juce::StringArray padLabels { "ONE SHOT", "LOOP", "ALT LOOP", "REVERSE", "WARP", "LEGATO", "SLICE", "MONO", juce::String (juce::CharPointer_UTF8 ("◀ SMP")), juce::String (juce::CharPointer_UTF8 ("SMP ▶")), "PREVIEW", "EDIT" };
            for (int i = 0; i < 12; ++i)
            {
                std::function<void()> act;
                std::function<bool()> lit;
                switch (i)
                {
                    case 0: act = setChoice ("loopMode", 0); lit = choiceIs ("loopMode", 0); break;
                    case 1: act = setChoice ("loopMode", 1); lit = choiceIs ("loopMode", 1); break;
                    case 2: act = setChoice ("loopMode", 2); lit = choiceIs ("loopMode", 2); break;
                    case 3: act = [this] { setValue ("reverse", value ("reverse") > 0.5f ? 0.0f : 1.0f, true); }; lit = choiceIs ("reverse", 1); break;
                    case 4: act = [this] { cycle ("warp", 1); }; lit = [this] { return value ("warp") > 0.5f; }; break;
                    case 5: act = [this] { setValue ("voiceMode", juce::roundToInt (value ("voiceMode")) == 2 ? 0.0f : 2.0f, true); }; lit = choiceIs ("voiceMode", 2); break;
                    case 6: act = [this] { slice(); }; break;
                    case 7: act = [this] { setValue ("voiceMode", juce::roundToInt (value ("voiceMode")) == 1 ? 0.0f : 1.0f, true); }; lit = choiceIs ("voiceMode", 1); break;
                    case 8: case 9: act = [this, i]
                    {
                        // the previous or next audio file in the sample's folder
                        const juce::File current (state[studiostate::fileProperty (0)].toString());
                        auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                        files.sort();
                        const auto at = files.indexOf (current);
                        if (! files.isEmpty())
                        {
                            const auto next = files[(at + (i == 9 ? 1 : -1) + files.size()) % files.size()];
                            um.beginNewTransaction ("Sampler: sample");
                            state.setProperty (studiostate::fileProperty (0), next.getFullPathName(), &um);
                        }
                    }; break;
                    case 10: act = [this] { if (host.playNote) { host.playNote (juce::roundToInt (value ("root")), 100, true); juce::Timer::callAfterDelay (600, [this] { host.playNote (juce::roundToInt (value ("root")), 0, false); }); } }; break;
                    default: act = mixTab; lit = [this] { return page == 1; }; break;
                }
                buttonAt (px[i % 4], py[i / 4] - yOff + 1.0f, 42.0f, 42.0f, padLabels[i], act, lit);
            }
            L.screenTabs = { "SAMPLE", "PITCH", "FILTER", "AMP", "LFO" };
            L.editor = { 20.0f, 240.0f, 1368.0f, 440.0f };
            L.mix = { 20.0f, 240.0f, 1368.0f, 440.0f };
            L.bar = { { "Playback", "loopMode" }, { "AudioWarp", "warp" }, { "Voices", "voiceMode" }, { "Filter", "filterType" } };
        }
    }
    else if (kind == "bass")
    {
        const auto yOff = 20.0f;
        L.crop = { 0, 20, 1408, 700 };
        panelRow ({ "GAIN", "TUNE", "PICKUPS", "HI-PASS", "TONE", "COMP", "DRIVE", "AMBIENCE", "REVERB", "MASTER" },
                  { "gain", "tune", "pickups", "hipass", "tone", "comp", "drive", "ambience", "reverb", "master" },
                  { "DI", "AMP", "ROOM", "MAIN" }, { "p0gain", "p1gain", "p2gain", "master" }, yOff);
        const juce::StringArray padLabels { "FINGER", "PICK", "SLAP", "MUTE", "SLIDE", "HARM", "GHOST", "HOLD", juce::String (juce::CharPointer_UTF8 ("◀ PROG")), juce::String (juce::CharPointer_UTF8 ("PROG ▶")), "PLAY", "MIX" };
        for (int i = 0; i < 12; ++i)
        {
            std::function<void()> act;
            std::function<bool()> lit;
            if (i < 8) { act = setChoice ("style", i); lit = choiceIs ("style", i); }
            else if (i == 8) act = [this] { cycle ("model", -1); };
            else if (i == 9) act = [this] { cycle ("model", 1); };
            else if (i == 10) act = [this] { if (host.playNote) { host.playNote (40, 100, true); juce::Timer::callAfterDelay (500, [this] { host.playNote (40, 0, false); }); } };
            else { act = mixTab; lit = [this] { return page == 1; }; }
            buttonAt (px[i % 4], py[i / 4] - yOff + 1.0f, 42.0f, 42.0f, padLabels[i], act, lit);
        }
        L.screenTabs = { "PLAY", "EDIT", "AMP", "MIX", "ARTIC" };
        L.strings = { 401.0f - yOff, 411.0f - yOff, 421.0f - yOff, 431.0f - yOff, 441.0f - yOff };
        L.stringLeft = 200.0f; L.stringRight = 1080.0f;
        L.mix = { 100.0f, 236.0f, 1208.0f, 440.0f };
        L.bar = { { "Model", "model" }, { "Strings", "strings" }, { "Velocity", "velocity" }, { "Mode", "mode" } };
    }
    else if (kind == "guitar")
    {
        const auto yOff = 20.0f;
        L.crop = { 0, 20, 1408, 700 };
        const float kx[4] { 180, 260, 340, 420 };
        const juce::StringArray kl { "VOLUME", "TONE", "PICKUP", "DRIVE", "BASS", "MID", "TREBLE", "REVERB" };
        const juce::StringArray ki { "volume", "tone", "pickup", "drive", "bass", "mid", "treble", "reverb" };
        for (int i = 0; i < 8; ++i)
            knobAt (kx[i % 4], (i < 4 ? 108.0f : 186.0f) - yOff, 21.0f, kl[i], ki[i]);
        const juce::StringArray sideLabels { "PLAY", "CHORDS", "AMP", "FX" };
        for (int i = 0; i < 4; ++i)
            buttonAt (505.0f, (104.0f + 45.0f * (float) i) - yOff, 36.0f, 24.0f, sideLabels[i], [this, i] { if (i == 3) showPage (page == 1 ? 0 : 1); else { screenPage = i; repaint(); } },
                      [this, i] { return i == 3 ? page == 1 : screenPage == i; });
        const juce::StringArray padLabels { "STRUM", "PICK", "MUTE", "HOLD", "POWER", "HARM", juce::String (juce::CharPointer_UTF8 ("◀ PROG")), juce::String (juce::CharPointer_UTF8 ("PROG ▶")), "PATTERN" };
        const float gx[3] { 1030, 1083, 1137 }, gy[3] { 128, 183, 238 };
        const int styleOf[] { 0, 1, 2, 3, 4, 5 };
        for (int i = 0; i < 9; ++i)
        {
            std::function<void()> act;
            std::function<bool()> lit;
            if (i < 6) { act = setChoice ("style", styleOf[i]); lit = choiceIs ("style", styleOf[i]); }
            else if (i == 6) act = [this] { cycle ("model", -1); };
            else if (i == 7) act = [this] { cycle ("model", 1); };
            else act = [this] { cycle ("pattern", 1); };
            buttonAt (gx[i % 3], gy[i / 3] - yOff, 42.0f, 42.0f, padLabels[i], act, lit);
        }
        encoderAt (1230.0f, 130.0f - yOff, 30.0f, "DATA", "pattern");
        encoderAt (1230.0f, 230.0f - yOff, 30.0f, "MASTER", "master");
        const float fx[4] { 1042, 1104, 1165, 1227 };
        const juce::StringArray fl { "DI", "AMP", "ROOM", "MAIN" }, fi { "p0gain", "p1gain", "p2gain", "master" };
        for (int i = 0; i < 4; ++i)
            faderAt (fx[i], 516.0f - yOff, 660.0f - yOff, fl[i], fi[i]);
        L.screen = { 582.0f, 88.0f - yOff, 366.0f, 160.0f };
        L.screenTabs = { "PLAY", "CHORDS", "STRUM", "AMP", "FX", "MIX" };
        L.strings = { 425.0f - yOff, 437.0f - yOff, 449.0f - yOff, 461.0f - yOff, 473.0f - yOff, 485.0f - yOff };
        L.stringLeft = 290.0f; L.stringRight = 1090.0f;
        L.mix = { 100.0f, 270.0f, 1208.0f, 410.0f };
        L.bar = { { "Model", "model" }, { "Amp", "amp" }, { "Strum", "pattern" }, { "Tuning", "tuning" }, { "Capo", "capo" } };
    }
    else if (kind == "keys")
    {
        const auto yOff = 60.0f;
        L.crop = { 0, 60, 1408, 620 };
        const juce::StringArray kl { "LID", "TUNE", "HAMMER", "RELEASE", "TONE", "RESON.", "AMBIENCE", "REVERB" };
        const juce::StringArray ki { "lid", "tune", "hammer", "release", "tone", "resonance", "ambience", "reverb" };
        for (int i = 0; i < 8; ++i)
            knobAt (338.0f + 53.6f * (float) i, 330.0f - yOff, 17.0f, kl[i], ki[i]);
        const juce::StringArray fl { "CLOSE", "WIDE", "ROOM", "LAYER", "PEDAL", "MASTER" }, fi { "p0gain", "p1gain", "p2gain", "layerLevel", "resonance", "master" };
        for (int i = 0; i < 6; ++i)
            faderAt (780.0f + 38.6f * (float) i, 288.0f - yOff, 384.0f - yOff, fl[i], fi[i]);
        const juce::StringArray side { "PLAY", "EDIT", "MIX" };
        const float sy[3] { 159, 190, 230 };
        for (int i = 0; i < 3; ++i)
            buttonAt (1043.0f, sy[i] - yOff, 30.0f, 18.0f, side[i], [this, i] { if (i == 2) showPage (page == 1 ? 0 : 1); else { showPage (0); screenPage = i; repaint(); } },
                      [this, i] { return i == 2 ? page == 1 : (page == 0 && screenPage == i); });
        buttonAt (1121.0f, 230.0f - yOff, 30.0f, 18.0f, juce::String (juce::CharPointer_UTF8 ("◀ PROG")), [this] { cycle ("program", -1); });
        buttonAt (1160.0f, 230.0f - yOff, 30.0f, 18.0f, juce::String (juce::CharPointer_UTF8 ("PROG ▶")), [this] { cycle ("program", 1); });
        // the pedals: latched (as foot switches)
        const auto pedal = [this] (int cc) { return [this, cc] { if (host.playNote) host.playNote (-cc, 127, true); }; };
        buttonAt (1043.0f, 314.0f - yOff, 30.0f, 18.0f, "SUS", pedal (64), [this] { return heldNote == -64; }, amber);
        buttonAt (1095.0f, 314.0f - yOff, 30.0f, 18.0f, "SOST", pedal (66), {}, amber);
        buttonAt (1043.0f, 346.0f - yOff, 30.0f, 18.0f, "SOFT", pedal (67), {}, amber);
        buttonAt (1095.0f, 346.0f - yOff, 30.0f, 18.0f, "LAYER", [this] { showChoiceMenu ("layer", nullptr); });
        encoderAt (1157.0f, 334.0f - yOff, 24.0f, "VALUE", "program");
        encoderAt (1225.0f, 334.0f - yOff, 24.0f, "VOLUME", "master");
        buttonAt (124.0f, 243.0f - yOff, 26.0f, 16.0f, "OCT -", [this] { setValue ("octave", value ("octave") - 1.0f, true); });
        buttonAt (201.0f, 243.0f - yOff, 26.0f, 16.0f, "OCT +", [this] { setValue ("octave", value ("octave") + 1.0f, true); });
        L.screen = { 763.0f, 80.0f, 230.0f, 111.0f };
        L.screenTabs = { "PLAY", "EDIT", "MIX", "FX", "PEDAL" };
        L.keys = { 258.0f, 418.0f - yOff, 1077.0f, 230.0f };
        L.lowestKey = 36; L.whiteKeys = 36;
        L.mix = { 100.0f, 90.0f, 1208.0f, 520.0f };
        L.bar = { { "Program", "program" }, { "Layer", "layer" }, { "Layer Mode", "layerMode" }, { "Velocity", "velocity" } };
    }
    else if (kind == "beatpad")
    {
        const auto yOff = 40.0f;
        L.crop = { 0, 40, 1408, 690 };
        const juce::StringArray kl { "TUNE", "DECAY", "FILTER", "RESO", "SWING", "VOLUME" };
        const juce::StringArray ki { "#Tune", "#Decay", "#Filter", "#Reso", "swing", "master" };
        for (int i = 0; i < 6; ++i)
            knobAt (182.0f, 138.0f + 95.0f * (float) i - yOff, 22.0f, kl[i], ki[i]);
        const juce::StringArray side { "SAMPLE", "PROGRAM", "SEQ", "MIXER" };
        for (int i = 0; i < 4; ++i)
            buttonAt (424.0f, 126.0f + 51.5f * (float) i - yOff, 34.0f, 22.0f, side[i],
                      [this, i] { if (i == 3) showPage (page == 1 ? 0 : 1); else { showPage (0); screenPage = i == 2 ? 2 : i == 1 ? 1 : 0; repaint(); } },
                      [this, i] { return i == 3 ? page == 1 : page == 0 && screenPage == (i == 2 ? 2 : i == 1 ? 1 : 3); });
        const juce::StringArray tl { "PLAY", "REPEAT", "FULL LVL", "STOP", "16 LVLS", "ERASE", juce::String (juce::CharPointer_UTF8 ("◀ BANK")), juce::String (juce::CharPointer_UTF8 ("BANK ▶")), "UNDO" };
        const float tx[3] { 990, 1052, 1114 }, ty[3] { 147, 210, 273 };
        for (int i = 0; i < 9; ++i)
        {
            std::function<void()> act;
            std::function<bool()> lit;
            switch (i)
            {
                case 0: act = [this] { setValue ("seq", 1.0f, true); }; lit = choiceIs ("seq", 1); break;
                case 1: act = [this] { cycle ("repeat", 1); }; lit = [this] { return value ("repeat") > 0.5f; }; break;
                case 2: act = [this] { setValue ("fullLevel", value ("fullLevel") > 0.5f ? 0.0f : 1.0f, true); }; lit = choiceIs ("fullLevel", 1); break;
                case 3: act = [this] { setValue ("seq", 0.0f, true); }; break;
                case 4: act = [this] { cycle ("levels16", 1); }; lit = [this] { return value ("levels16") > 0.5f; }; break;
                case 5: act = [this]
                {
                    // erase the selected pad's steps
                    const auto sel = juce::roundToInt (value ("sel"));
                    if (auto bp = std::dynamic_pointer_cast<studio::BeatPad> (host.instrument ? host.instrument() : nullptr))
                    {
                        for (int s = 0; s < studio::BeatPad::steps; ++s) bp->setStep (sel, s, 0);
                        um.beginNewTransaction ("Beat pad: erase");
                        state.setProperty (studiostate::pattern, bp->patternText(), &um);
                    }
                }; break;
                case 6: act = [this] { cycle ("bank", -1); }; break;
                case 7: act = [this] { cycle ("bank", 1); }; break;
                default: act = [this] { um.undo(); }; break;
            }
            buttonAt (tx[i % 3], ty[i / 3] - yOff, 46.0f, 46.0f, tl[i], act, lit);
        }
        encoderAt (1218.0f, 148.0f - yOff, 32.0f, "DATA", "kit");
        encoderAt (1218.0f, 264.0f - yOff, 32.0f, "MASTER", "master");
        const float fx[4] { 1040, 1102, 1164, 1226 };
        const juce::StringArray fl { "PAD LVL", "PAD TUNE", "PAD PAN", "MAIN" }, fi { "#Level", "#Tune", "#Pan", "master" };
        for (int i = 0; i < 4; ++i)
            faderAt (fx[i], 512.0f - yOff, 660.0f - yOff, fl[i], fi[i]);
        // the 18 pads
        const float cx[6] { 305, 407, 509, 611, 712, 813 }, cy[3] { 387, 489, 591 };
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 6; ++col)
            {
                auto c = std::make_unique<Control>();
                c->type = Control::padKey;
                c->area = { cx[col] + 2.0f, cy[row] - yOff + 2.0f, 84.0f, 84.0f };
                c->pad = (2 - row) * 6 + col;   // (pad 1 bottom left, as on the machines)
                controls.push_back (std::move (c));
            }
        const juce::StringArray rl { "REC", "OVERDUB", "TAP TEMPO" };
        const juce::Colour rc[] { juce::Colour (0xffff3b54), juce::Colour (0xffff8a1f), teal };
        for (int i = 0; i < 3; ++i)
            buttonAt (1008.0f + 101.0f * (float) i, 430.0f - yOff, 86.0f, 86.0f, rl[i], [this, i]
            {
                if (auto bp = std::dynamic_pointer_cast<studio::BeatPad> (host.instrument ? host.instrument() : nullptr))
                {
                    if (i < 2) bp->recording.store (! bp->recording.load());
                }
                repaint();
            }, [this, i] { auto bp = std::dynamic_pointer_cast<studio::BeatPad> (host.instrument ? host.instrument() : nullptr); return i < 2 && bp != nullptr && bp->recording.load(); }, rc[i]);
        L.screen = { 506.0f, 50.0f, 398.0f, 236.0f };
        L.screenTabs = { "MAIN", "PADS", "STEP", "SAMPLE", "MIX" };
        L.mix = { 20.0f, 330.0f, 1368.0f, 350.0f };
        L.bar = { { "Kit", "kit" }, { "Bank", "bank" }, { "Repeat", "repeat" }, { "16 Levels", "levels16" }, { "Swing", "swing" } };
    }
    juce::ignoreUnused (inst);
    if (L.height <= 0.0f)
        L.height = (float) L.crop.getHeight();

    for (auto& c : controls)
        if (c->type == Control::knob || c->type == Control::encoder)
            c->colour = c->type == Control::encoder ? (c->id == "master" ? red : teal) : sectionColour (kind, c->id);

    buildBar();
    mixView.setViewedComponent (&mixHolder, false);
    mixView.setScrollBarsShown (false, true);
    addChildComponent (mixView);
    rebuildMix();
    project.addListener (this);
    setWantsKeyboardFocus (true);
    setSize (naturalSize (kind).getWidth(), naturalSize (kind).getHeight());
    startTimerHz (30);
}

StudioPanel::~StudioPanel()
{
    project.removeListener (this);
}

//==============================================================================
float StudioPanel::value (const juce::String& idIn) const
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    auto id = idIn;
    // "@x": the drums' selected piece's; "#x": the beat pad's selected pad's
    if (id.startsWith ("@"))
        id = studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
    else if (id.startsWith ("#"))
        id = "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
    if (inst != nullptr && automatedIds.contains (idIn) && dragging == nullptr)
        if (const auto i = inst->indexOf (id); i >= 0)
            return inst->getParam (i);   // (the automation playing it)
    if (state.hasProperty (id))
        return (float) (double) state[id];
    if (inst != nullptr)
        if (const auto i = inst->indexOf (id); i >= 0)
            return inst->params()[(size_t) i].def;
    return 0.0f;
}

float StudioPanel::normalised (const juce::String& idIn) const
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    if (inst == nullptr) return 0.5f;
    auto id = idIn;
    if (id.startsWith ("@")) id = studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
    else if (id.startsWith ("#")) id = "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
    const auto i = inst->indexOf (id);
    if (i < 0) return 0.5f;
    const auto& p = inst->params()[(size_t) i];
    return (value (idIn) - p.min) / juce::jmax (1.0e-6f, p.max - p.min);
}

void StudioPanel::setValue (const juce::String& idIn, float v, bool newGesture)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    auto id = idIn;
    if (id.startsWith ("@")) id = studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
    else if (id.startsWith ("#")) id = "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
    if (inst != nullptr)
        if (const auto i = inst->indexOf (id); i >= 0)
        {
            const auto& p = inst->params()[(size_t) i];
            v = juce::jlimit (p.min, p.max, p.choices.isEmpty() ? v : (float) juce::roundToInt (v));
        }
    if (newGesture)
        um.beginNewTransaction (studio::displayName (kind));
    state.setProperty (id, v, &um);
    repaint();
}

void StudioPanel::setNormalised (const juce::String& idIn, float v, bool newGesture)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    if (inst == nullptr) return;
    auto id = idIn;
    if (id.startsWith ("@")) id = studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
    else if (id.startsWith ("#")) id = "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
    const auto i = inst->indexOf (id);
    if (i < 0) return;
    const auto& p = inst->params()[(size_t) i];
    setValue (idIn, p.min + juce::jlimit (0.0f, 1.0f, v) * (p.max - p.min), newGesture);
}

void StudioPanel::cycle (const juce::String& id, int delta)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    if (inst == nullptr) return;
    const auto i = inst->indexOf (id);
    if (i < 0) return;
    const auto& p = inst->params()[(size_t) i];
    const auto count = juce::roundToInt (p.max - p.min) + 1;
    auto v = juce::roundToInt (value (id) - p.min) + delta;
    v = ((v % count) + count) % count;
    setValue (id, p.min + (float) v, true);
}

void StudioPanel::applyKit (int kit)
{
    kit = (kit % 5 + 5) % 5;
    um.beginNewTransaction ("Drums: kit");
    for (const auto& [id, v] : studio::StudioDrums::kitSettings (kit))
        state.setProperty (id, v, &um);
    repaint();
}

void StudioPanel::showChoiceMenu (const juce::String& id, juce::Component* target)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    if (inst == nullptr) return;
    const auto i = inst->indexOf (id);
    if (i < 0) return;
    const auto& p = inst->params()[(size_t) i];
    juce::PopupMenu menu;
    const auto current = juce::roundToInt (value (id));
    if (id == "program" || id == "layer")
    {
        // the workstation's programs by category
        const auto& programs = studio::StudioKeys::programs();
        std::map<juce::String, juce::PopupMenu> byCategory;
        juce::StringArray order;
        for (int k = 0; k < (int) programs.size(); ++k)
        {
            const auto v = id == "layer" ? k + 1 : k;
            if (! order.contains (programs[(size_t) k].category)) order.add (programs[(size_t) k].category);
            byCategory[programs[(size_t) k].category].addItem (programs[(size_t) k].name, true, v == current, [this, id, v] { setValue (id, (float) v, true); });
        }
        if (id == "layer")
            menu.addItem ("None", true, current == 0, [this] { setValue ("layer", 0.0f, true); });
        for (const auto& c : order)
            menu.addSubMenu (c, byCategory[c]);
    }
    else if (id == "kit" && kind == "drums")
    {
        for (int k = 0; k < p.choices.size(); ++k)
            menu.addItem (p.choices[k], true, k == current, [this, k] { applyKit (k); });
    }
    else if (! p.choices.isEmpty())
    {
        for (int k = 0; k < p.choices.size(); ++k)
            menu.addItem (p.choices[k], true, juce::roundToInt (p.min) + k == current, [this, id, k, p] { setValue (id, p.min + (float) k, true); });
    }
    else
    {
        for (int k = juce::roundToInt (p.min); k <= juce::roundToInt (p.max) && k - juce::roundToInt (p.min) < 64; ++k)
            menu.addItem (juce::String (k), true, k == current, [this, id, k] { setValue (id, (float) k, true); });
    }
    auto options = juce::PopupMenu::Options();
    if (target != nullptr) options = options.withTargetComponent (target);
    menu.showMenuAsync (options);
}

void StudioPanel::buildBar()
{
    barButtons.clear();
    barLabels.clear();
    for (const auto& b : layout->bar)
    {
        const auto light = theme::glassLight();
        auto* label = barLabels.add (new juce::Label ({}, b.label));
        label->setFont (font (12.0f, false));
        label->setColour (juce::Label::textColourId, light ? juce::Colour (0xff3a4048) : dim);
        addAndMakeVisible (label);
        auto* button = barButtons.add (new juce::TextButton());
        const auto id = b.id;
        button->onClick = [this, id, button] { showChoiceMenu (id, button); };
        button->setColour (juce::TextButton::buttonColourId, light ? juce::Colours::white.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.08f));
        button->setColour (juce::TextButton::textColourOffId, light ? juce::Colours::black : ink);
        button->setColour (juce::TextButton::textColourOnId, light ? juce::Colours::black : ink);
        addAndMakeVisible (button);
    }
    auto* mixer = barButtons.add (new juce::TextButton ("Mixer"));
    mixer->onClick = [this] { showPage (page == 1 ? 0 : 1); };
    mixer->setColour (juce::TextButton::buttonColourId, theme::glassLight() ? teal.withAlpha (0.55f) : teal.withAlpha (0.3f));
    mixer->setColour (juce::TextButton::textColourOffId, theme::glassLight() ? juce::Colours::black : ink);
    addAndMakeVisible (mixer);
}

void StudioPanel::rebuildMix()
{
    strips.clear();
    auto inst = host.instrument ? host.instrument() : nullptr;
    if (inst == nullptr)
        return;
    const auto trackId = track[ids::id].toString();
    for (int i = 0; i < inst->numParts(); ++i)
        mixHolder.addAndMakeVisible (strips.add (new Strip (*this, i, inst->partNames()[i], studio::chainOwner (trackId, juce::String (i)))));
    mixHolder.addAndMakeVisible (strips.add (new Strip (*this, -1, "FX A", studio::chainOwner (trackId, "fxA"))));
    mixHolder.addAndMakeVisible (strips.add (new Strip (*this, -2, "FX B", studio::chainOwner (trackId, "fxB"))));
    mixHolder.addAndMakeVisible (strips.add (new Strip (*this, -3, studio::displayName (kind) + " Main", studio::chainOwner (trackId, "main"))));
    resized();
}

void StudioPanel::showPage (int p)
{
    page = p;
    mixView.setVisible (page == 1);
    for (auto* s : strips) s->refresh();
    repaint();
}

void StudioPanel::resized()
{
    const auto crop = layout->crop;
    scale = (float) getWidth() / (float) crop.getWidth();
    faceArea = { 0.0f, 0.0f, (float) getWidth(), layout->height * scale };
    barArea = { 0.0f, faceArea.getBottom(), (float) getWidth(), (float) getHeight() - faceArea.getBottom() };
    auto bar = barArea.reduced (14.0f, 12.0f).toNearestInt();
    const auto count = barButtons.size();
    if (count > 0)
    {
        const auto each = bar.getWidth() / count;
        for (int i = 0; i < count; ++i)
        {
            auto cell = bar.removeFromLeft (each).reduced (6, 0);
            if (i < barLabels.size())
                barLabels[i]->setBounds (cell.removeFromTop (cell.getHeight() / 2 - 2));
            barButtons[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), 28)));
        }
    }
    mixArea = layout->mix * scale;
    mixView.setBounds (mixArea.reduced (8.0f, 30.0f).withTrimmedBottom (-22.0f).toNearestInt());
    const auto stripWidth = juce::jmax (96, juce::roundToInt (118.0f * scale));
    mixHolder.setSize (juce::jmax (mixView.getWidth(), strips.size() * (stripWidth + 6)), juce::jmax (100, mixView.getHeight() - 10));
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * (stripWidth + 6), 0, stripWidth, mixHolder.getHeight());
}

juce::Point<float> StudioPanel::toFace (juce::Point<float> p) const
{
    return { p.x / scale, p.y / scale };
}

//==============================================================================
void StudioPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0f12));
    const auto crop = layout->crop;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (face.isValid())
        g.drawImage (face, 0, 0, getWidth(), juce::roundToInt ((float) crop.getHeight() * scale), crop.getX(), crop.getY(), crop.getWidth(), crop.getHeight());

    g.saveState();
    g.addTransform (juce::AffineTransform::scale (scale));
    auto inst = host.instrument ? host.instrument() : nullptr;

    // the knobs: the photographed cap turned (only its middle), a light ring showing the setting
    for (auto& c : controls)
    {
        if (c->type == Control::knob || c->type == Control::encoder)
        {
            // the silver knob, its skirt in its section's colour; only the middle turns, the ring shows the setting
            const auto n = juce::jlimit (0.0f, 1.0f, normalised (c->id));
            const auto r = c->radius;
            const auto bipolar = c->id.endsWith ("Pan") || c->id == "pan" || c->id == "@Pan" || c->id == "#Pan";
            const auto knobArea = juce::Rectangle<float> (r * 2.15f, r * 2.15f).withCentre (c->centre);
            g.setColour (juce::Colour (0xff15171b));
            g.fillEllipse (juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (c->centre));   // (the photographed one covered)
            if (! looks::drawSilverKnob (g, knobArea, n, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f),
                                         c->colour, c->colour, bipolar, true))
            {
                g.setColour (c->colour);
                g.fillEllipse (knobArea.reduced (r * 0.2f));
            }
            if (automatedIds.contains (c->id))
            {
                // automated: a small dot over it (as Logic's automation light)
                g.setColour (juce::Colour (0xff4ce06a));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ c->centre.x + r * 0.95f, c->centre.y - r * 0.95f }));
            }
            g.setColour (ink);
            g.setFont (font (r < 19.0f ? 9.5f : 11.0f));
            const auto labelY = c->centre.y + r * 1.08f + 3.0f;
            g.drawText (c->label, juce::Rectangle<float> (c->centre.x - 45.0f, labelY, 90.0f, 13.0f), juce::Justification::centred, false);
            if (hoverId == c->id && hoverId.isNotEmpty() && inst != nullptr)
            {
                // the value, in a bubble (dark glass, white writing)
                const auto text = juce::String (value (c->id), 2);
                const auto bubble = juce::Rectangle<float> (64.0f, 18.0f).withCentre ({ c->centre.x, c->centre.y - r - 14.0f });
                g.setColour (juce::Colour (0xf0161a20));
                g.fillRoundedRectangle (bubble, 5.0f);
                g.setColour (juce::Colours::white);
                g.setFont (font (10.5f));
                g.drawText (text, bubble, juce::Justification::centred, false);
            }
        }
        else if (c->type == Control::fader)
        {
            // the photographed fader covered: our slot and the white glass cap at the setting
            const auto a = c->area;
            g.setColour (juce::Colour (0xff111316));
            g.fillRoundedRectangle (a.expanded (2.0f, 10.0f), 5.0f);
            g.setColour (juce::Colour (0xff030405));
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, a.getHeight()).withCentre (a.getCentre()), 1.5f);
            const auto n = juce::jlimit (0.0f, 1.0f, normalised (c->id));
            const auto y = a.getBottom() - n * a.getHeight();
            const auto cap = looks::whiteGlassCap();
            if (cap.isValid())
            {
                const auto ch = 30.0f, cw = ch * (float) cap.getWidth() / (float) cap.getHeight();
                g.drawImage (cap, juce::Rectangle<float> (cw, ch).withCentre ({ a.getCentreX(), y }));
            }
            g.setColour (ink);
            g.setFont (font (8.5f));
            g.drawText (c->label, juce::Rectangle<float> (a.getCentreX() - 30.0f, a.getBottom() + 12.0f, 60.0f, 12.0f), juce::Justification::centred, false);
        }
        else if (c->type == Control::button)
        {
            const bool on = c->lit != nullptr && c->lit();
            if (on)
            {
                g.setColour (c->colour.withAlpha (0.35f));
                g.fillRoundedRectangle (c->area.reduced (2.0f), 6.0f);
                g.setColour (c->colour.withAlpha (0.8f));
                g.drawRoundedRectangle (c->area.reduced (1.0f), 6.0f, 1.5f);
            }
            g.setColour (on ? c->colour.brighter (0.4f) : juce::Colour (0xffb8c0c8));
            g.setFont (font (c->area.getHeight() > 60.0f ? 11.0f : 7.5f));
            if (c->area.getHeight() > 60.0f || c->area.getHeight() < 26.0f)
                g.drawFittedText (c->label, c->area.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            else
                g.drawText (c->label, juce::Rectangle<float> (c->area.getCentreX() - 30.0f, c->area.getBottom() + 1.0f, 60.0f, 10.0f), juce::Justification::centred, false);
        }
    }

    paintPads (g);
    if (layout->screen.getWidth() > 0.0f)
        paintScreen (g, layout->screen);
    if (kind == "sampler" && page == 0)
        paintSampler (g, layout->editor);
    g.restoreState();

    // the MIX tab: dark glass over the picture, the strips on it
    if (page == 1)
    {
        g.setColour (juce::Colour (0xf20d1015));
        g.fillRoundedRectangle (mixArea.reduced (4.0f), 16.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), 0.0f, mixArea.getY(), juce::Colours::transparentWhite, 0.0f, mixArea.getY() + 80.0f, false));
        g.fillRoundedRectangle (mixArea.reduced (5.0f), 15.0f);
    }

    // the bar under the face (glass)
    if (theme::glassLight())
    {
        g.setColour (juce::Colour (0xffe9edf2));
        g.fillRect (barArea);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, barArea.getY(), juce::Colour (0xffdfe4ea), 0.0f, barArea.getBottom(), false));
    }
    else
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), 0.0f, barArea.getY(), juce::Colours::white.withAlpha (0.04f), 0.0f, barArea.getBottom(), false));
    g.fillRoundedRectangle (barArea.reduced (8.0f, 6.0f), 18.0f);
    g.setColour (teal.withAlpha (0.35f));
    g.drawRoundedRectangle (barArea.reduced (8.0f, 6.0f), 18.0f, 1.0f);
    for (int i = 0; i < (int) layout->bar.size() && i < barButtons.size(); ++i)
    {
        auto instNow = host.instrument ? host.instrument() : nullptr;
        if (instNow == nullptr) break;
        const auto id = layout->bar[(size_t) i].id;
        const auto idx = instNow->indexOf (id);
        if (idx < 0) continue;
        const auto& p = instNow->params()[(size_t) idx];
        juce::String text;
        if (id == "program") text = studio::StudioKeys::programs()[(size_t) juce::jlimit (0, (int) studio::StudioKeys::programs().size() - 1, juce::roundToInt (value (id)))].name;
        else if (id == "layer") text = juce::roundToInt (value (id)) <= 0 ? juce::String ("None") : studio::StudioKeys::programs()[(size_t) juce::jlimit (0, (int) studio::StudioKeys::programs().size() - 1, juce::roundToInt (value (id)) - 1)].name;
        else if (! p.choices.isEmpty()) text = p.choices[juce::jlimit (0, p.choices.size() - 1, juce::roundToInt (value (id) - p.min))];
        else text = juce::String (value (id), id == "swing" ? 0 : 0) + (id == "swing" ? "%" : "");
        if (barButtons[i]->getButtonText() != text + juce::String (juce::CharPointer_UTF8 (" ▾")))
            barButtons[i]->setButtonText (text + juce::String (juce::CharPointer_UTF8 (" ▾")));
    }
}

void StudioPanel::paintPads (juce::Graphics& g)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    // drums: the selected piece ringed, the ones playing lit
    if (kind == "drums" && inst != nullptr)
    {
        const auto sel = juce::roundToInt (value ("sel"));
        for (const auto& h : layout->pieces)
        {
            const auto level = inst->noteLevel[(size_t) (h.note & 127)].load();
            if (h.piece == sel)
            {
                g.setColour (teal.withAlpha (0.85f));
                g.drawEllipse (juce::Rectangle<float> (h.r * 2.0f, h.r * 2.0f).withCentre (h.c), 3.0f);
            }
            if (level > 0.05f)
            {
                g.setColour (teal.withAlpha (juce::jmin (0.45f, level * 0.5f)));
                g.fillEllipse (juce::Rectangle<float> (h.r * 2.0f, h.r * 2.0f).withCentre (h.c));
            }
        }
    }
    // keys: the keys playing lit
    if (kind == "keys" && inst != nullptr)
        for (int note = layout->lowestKey; note < layout->lowestKey + 61; ++note)
            if (const auto level = inst->noteLevel[(size_t) note].load(); level > 0.05f)
            {
                const auto r = keyRect (note);
                g.setGradientFill (juce::ColourGradient (teal.withAlpha (0.0f), 0.0f, r.getY(), teal.withAlpha (juce::jmin (0.7f, level)), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 2.0f);
            }
    // bass and guitar: the strings sounding glow
    if ((kind == "bass" || kind == "guitar") && inst != nullptr && ! layout->strings.empty())
    {
        const auto last = inst->lastNote.load();
        float energy = 0.0f;
        for (const auto& l : inst->noteLevel) energy = juce::jmax (energy, l.load());
        if (energy > 0.05f)
        {
            int string = 0;
            if (auto bass = std::dynamic_pointer_cast<studio::StudioBass> (inst))
                string = juce::jmax (0, bass->stringAndFret (last).first - (value ("strings") > 0.5f ? 1 : 0));
            else
                string = juce::jlimit (0, 5, inst->lastPart.load());
            const auto count = (int) layout->strings.size();
            const auto y = layout->strings[(size_t) juce::jlimit (0, count - 1, kind == "bass" ? count - 2 - string : string)];
            g.setColour (teal.withAlpha (juce::jmin (0.9f, energy)));
            g.drawLine (layout->stringLeft, y, layout->stringRight, y, 2.5f);
            g.setColour (teal.withAlpha (juce::jmin (0.3f, energy * 0.4f)));
            g.drawLine (layout->stringLeft, y, layout->stringRight, y, 8.0f);
        }
    }
    // the beat pad's pads: their names, colours and hits
    if (kind == "beatpad")
        if (auto bp = std::dynamic_pointer_cast<studio::BeatPad> (inst))
        {
            const auto bank = juce::roundToInt (value ("bank"));
            const auto sel = juce::roundToInt (value ("sel"));
            for (auto& c : controls)
            {
                if (c->type != Control::padKey) continue;
                const auto pad = bank * studio::BeatPad::padsPerBank + c->pad;
                const auto colour = bp->padColour (pad);
                const auto level = inst->noteLevel[(size_t) ((36 + pad) & 127)].load();
                const auto r = c->area.reduced (2.0f);
                if (level > 0.05f)
                {
                    g.setColour (colour.withAlpha (juce::jmin (0.55f, level * 0.6f)));
                    g.fillRoundedRectangle (r, 7.0f);
                }
                g.setColour (colour.withAlpha (pad == sel ? 1.0f : 0.55f));
                g.drawRoundedRectangle (r, 7.0f, pad == sel ? 2.5f : 1.5f);
                g.setColour (dim);
                g.setFont (font (8.0f));
                g.drawText (juce::String ("ABCD"[bank]) + juce::String (c->pad + 1).paddedLeft ('0', 2), r.reduced (6.0f, 5.0f), juce::Justification::topLeft, false);
                g.setColour (ink);
                g.setFont (font (10.0f));
                g.drawText (bp->padName (pad), r.reduced (4.0f, 0.0f).withTrimmedBottom (20.0f).removeFromBottom (14.0f), juce::Justification::centred, false);
                g.setColour (dim);
                g.setFont (font (7.5f));
                g.drawText (bp->padSound (pad), r.reduced (4.0f, 0.0f).withTrimmedBottom (8.0f).removeFromBottom (11.0f), juce::Justification::centred, false);
            }
        }
}

void StudioPanel::paintScreen (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto inst = host.instrument ? host.instrument() : nullptr;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff071a22), 0.0f, area.getY(), juce::Colour (0xff04121a), 0.0f, area.getBottom(), false));
    g.fillRect (area);
    g.setColour (teal.withAlpha (0.2f));
    g.drawRect (area, 1.0f);
    const auto lines = inst != nullptr ? inst->screenLines() : juce::StringArray { "Loading..." };
    const auto mono = juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain);
    auto r = area.reduced (1.0f);
    // the top line
    auto top = r.removeFromTop (12.0f);
    g.setColour (teal.withAlpha (0.18f));
    g.fillRect (top);
    g.setColour (juce::Colour (0xffbfffff));
    g.setFont (mono);
    const auto programNo = kind == "keys" && lines.size() > 6 ? lines[6] : studio::displayName (kind).toUpperCase();
    g.drawText (programNo, top.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
    // the song's tempo (drag it up or down, as in Logic)
    {
        const auto bpm = (double) project.getProperty (ids::tempo, 120.0);
        const auto text = (std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1)) + " BPM";
        tempoArea = top.reduced (4.0f, 0.0f).removeFromRight (56.0f);
        if (tempoDragging)
        {
            g.setColour (teal.withAlpha (0.35f));
            g.fillRect (tempoArea.expanded (2.0f, 0.0f));
            g.setColour (juce::Colour (0xffbfffff));
        }
        g.drawText (text, top.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    }
    // the drums' PIECE page: the selected piece's models (click one to swap it in)
    if (kind == "drums" && screenPage == 1 && page == 0)
    {
        const auto sel = juce::roundToInt (value ("sel"));
        const auto current = juce::roundToInt (value ("@Alt"));
        auto list = r.reduced (4.0f, 2.0f).withTrimmedBottom (12.0f);
        g.setColour (juce::Colour (0xff55ffdd));
        g.setFont (mono);
        g.drawText (studio::StudioDrums::pieceName (sel).toUpperCase() + "  MODEL", list.removeFromTop (11.0f), juce::Justification::centredLeft, false);
        const auto rowH = list.getHeight() / 5.0f;
        for (int m = 0; m < 5; ++m)
        {
            const auto row = list.removeFromTop (rowH).reduced (0.0f, 0.5f);
            if (m == current)
            {
                g.setColour (teal.withAlpha (0.35f));
                g.fillRect (row);
            }
            g.setColour (m == current ? juce::Colours::white : juce::Colour (0xffbfffff));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), juce::jmin (9.5f, rowH * 0.7f), m == current ? juce::Font::bold : juce::Font::plain));
            g.drawText (juce::String (m + 1) + "  " + studio::StudioDrums::altName (sel, m), row.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
    else if (const auto browse = browserId(); browse.isNotEmpty() && page == 0)
    {
        // a browser page: the programs (keys), the models (bass), the amps (guitar), the kits (beat pad)
        const auto names = choicesOf (browse);
        const auto current = juce::roundToInt (value (browse));
        auto list = r.reduced (4.0f, 2.0f).withTrimmedBottom (12.0f);
        g.setColour (juce::Colour (0xff55ffdd));
        g.setFont (mono);
        g.drawText (paramName (browse).toUpperCase() + "   " + juce::String (current + 1) + "/" + juce::String (names.size()),
                    list.removeFromTop (11.0f), juce::Justification::centredLeft, false);
        const auto rows = 5;
        const auto first = juce::jlimit (0, juce::jmax (0, names.size() - rows), current - 2);
        const auto rowH = list.getHeight() / (float) rows;
        for (int k = first; k < juce::jmin (names.size(), first + rows); ++k)
        {
            const auto row = list.removeFromTop (rowH).reduced (0.0f, 0.5f);
            if (k == current)
            {
                g.setColour (teal.withAlpha (0.35f));
                g.fillRect (row);
            }
            g.setColour (k == current ? juce::Colours::white : juce::Colour (0xffbfffff));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), juce::jmin (9.5f, rowH * 0.7f), k == current ? juce::Font::bold : juce::Font::plain));
            g.drawText (juce::String (k + 1).paddedLeft ('0', 2) + "  " + names[k], row.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
    else
    // the beat pad's step page: the bank's pads by the 16 steps
    if (kind == "beatpad" && screenPage == 2)
        if (auto bp = std::dynamic_pointer_cast<studio::BeatPad> (inst))
        {
            const auto bank = juce::roundToInt (value ("bank"));
            const auto grid = r.reduced (4.0f, 3.0f).withTrimmedBottom (12.0f);
            const auto rowH = grid.getHeight() / 18.0f, colW = (grid.getWidth() - 56.0f) / 16.0f;
            const auto playing = bp->currentStep();
            for (int row = 0; row < 18; ++row)
            {
                const auto pad = bank * studio::BeatPad::padsPerBank + row;
                g.setColour (dim);
                g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawText (bp->padName (pad), juce::Rectangle<float> (grid.getX(), grid.getY() + rowH * (float) row, 54.0f, rowH), juce::Justification::centredLeft, false);
                for (int s = 0; s < 16; ++s)
                {
                    const auto cell = juce::Rectangle<float> (grid.getX() + 56.0f + colW * (float) s, grid.getY() + rowH * (float) row, colW - 1.0f, rowH - 1.0f);
                    const auto v = bp->getStep (pad, s);
                    g.setColour (v > 0 ? bp->padColour (pad).withAlpha (0.4f + 0.6f * (float) v / 127.0f) : juce::Colours::white.withAlpha (s % 4 == 0 ? 0.12f : 0.06f));
                    g.fillRect (cell);
                    if (s == playing)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.6f));
                        g.drawRect (cell, 1.0f);
                    }
                }
            }
            r = r.removeFromBottom (12.0f);
        }
        else {}
    else
    {
        g.setColour (juce::Colour (0xffe6ffff));
        g.setFont (font (area.getHeight() > 150.0f ? 22.0f : 16.0f));
        g.drawFittedText (lines[0], r.removeFromTop (area.getHeight() > 150.0f ? 30.0f : 21.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (juce::Colour (0xff55ffdd));
        g.setFont (mono);
        g.drawFittedText (lines[1], r.removeFromTop (11.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        // four boxes: the main settings
        juce::StringArray names;
        if (kind == "drums") names = { "GAIN", "TUNE", "DAMPEN", "ARTIC" };
        else if (kind == "keys") names = { "LID", "TUNE", "HAMMER", "RELEASE" };
        else if (kind == "bass") names = { "PICKUP", "HI-PASS", "TONE", "DRIVE" };
        else if (kind == "guitar") names = { "PICKUP", "TONE", "DRIVE", "STRUM" };
        else if (kind == "beatpad") names = { "TUNE", "DECAY", "SWING", "REPEAT" };
        else names = { "START", "LOOP", "PITCH", "FILTER" };
        auto boxes = r.removeFromTop (28.0f).reduced (4.0f, 2.0f);
        const auto bw = boxes.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto b = boxes.removeFromLeft (bw).reduced (1.5f, 0.0f);
            g.setColour (juce::Colour (0x5a8ff4ff));
            g.drawRect (b, 1.0f);
            g.setColour (juce::Colour (0xff8ff4ff));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            g.drawText (names[i], b.removeFromTop (9.0f), juce::Justification::centred, false);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
            g.drawFittedText (lines[2 + i], b.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
        // a chord on the guitar's screen: its frets
        if (kind == "guitar")
            if (auto gtr = std::dynamic_pointer_cast<studio::StudioGuitar> (inst))
            {
                const auto frets = gtr->chordFrets();
                auto d = r.removeFromLeft (70.0f).reduced (8.0f, 6.0f);
                g.setColour (juce::Colour (0xaa8ff4ff));
                for (int s = 0; s < 6; ++s)
                    g.drawVerticalLine (juce::roundToInt (d.getX() + d.getWidth() * (float) s / 5.0f), d.getY(), d.getBottom());
                for (int f = 0; f <= 4; ++f)
                    g.drawHorizontalLine (juce::roundToInt (d.getY() + d.getHeight() * (float) f / 4.0f), d.getX(), d.getRight());
                for (int s = 0; s < 6; ++s)
                    if (frets[(size_t) s] > 0)
                    {
                        const auto f = juce::jlimit (1, 4, frets[(size_t) s]);
                        g.setColour (teal);
                        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ d.getX() + d.getWidth() * (float) s / 5.0f, d.getY() + d.getHeight() * ((float) f - 0.5f) / 4.0f }));
                    }
            }
        g.setColour (juce::Colour (0xff55ffdd));
        g.setFont (mono);
        if (lines.size() > 6 && kind != "keys")
            g.drawFittedText (lines[6], r.removeFromTop (12.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }
    // the tabs along the bottom
    auto tabs = area.removeFromBottom (12.0f);
    const auto tw = tabs.getWidth() / (float) juce::jmax (1, layout->screenTabs.size());
    for (int i = 0; i < layout->screenTabs.size(); ++i)
    {
        const auto t = tabs.removeFromLeft (tw);
        const bool on = layout->screenTabs[i] == "MIX" ? page == 1 : (page == 0 && i == screenPage);
        g.setColour (on ? teal : teal.withAlpha (0.15f));
        g.fillRect (t.reduced (0.5f, 0.0f));
        g.setColour (on ? juce::Colour (0xff002222) : juce::Colour (0xffbfffff));
        g.setFont (mono);
        g.drawText (layout->screenTabs[i], t, juce::Justification::centred, false);
    }
}

void StudioPanel::paintSampler (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto sampler = std::dynamic_pointer_cast<studio::StudioSampler> (host.instrument ? host.instrument() : nullptr);
    g.setColour (juce::Colour (0xe6101418));
    g.fillRoundedRectangle (area, 16.0f);
    g.setColour (teal.withAlpha (0.3f));
    g.drawRoundedRectangle (area, 16.0f, 1.0f);
    auto wave = area.reduced (16.0f).removeFromTop (area.getHeight() * 0.55f);
    g.setColour (juce::Colour (0xff05090c));
    g.fillRoundedRectangle (wave, 10.0f);
    auto sample = sampler != nullptr ? sampler->getSample() : nullptr;
    if (sample == nullptr || sample->getNumSamples() < 4)
    {
        g.setColour (dim);
        g.setFont (font (16.0f, false));
        g.drawText ("Drop an audio file or region here: it plays across the keys", wave, juce::Justification::centred, false);
    }
    else
    {
        const auto n = sample->getNumSamples();
        const auto w = (int) wave.getWidth();
        const auto* d = sample->getReadPointer (0);
        g.setColour (teal.withAlpha (0.8f));
        for (int x = 0; x < w; ++x)
        {
            const auto from = (int) ((juce::int64) n * x / w), to = juce::jmin (n, (int) ((juce::int64) n * (x + 1) / w) + 1);
            float peak = 0.0f;
            for (int i = from; i < to; i += juce::jmax (1, (to - from) / 64)) peak = juce::jmax (peak, std::abs (d[i]));
            const auto h = peak * wave.getHeight() * 0.48f;
            g.drawVerticalLine ((int) wave.getX() + x, wave.getCentreY() - h, wave.getCentreY() + h);
        }
        // the loop's span, the markers
        const auto xOf = [wave] (float v) { return wave.getX() + v * wave.getWidth(); };
        g.setColour (juce::Colour (0x1affd84a));
        g.fillRect (juce::Rectangle<float> (xOf (value ("loopStart")), wave.getY(), xOf (value ("loopEnd")) - xOf (value ("loopStart")), wave.getHeight()));
        const struct { const char* id; const char* tag; juce::Colour c; } markers[] {
            { "start", "S", juce::Colour (0xff5cff8f) }, { "loopStart", "L", juce::Colour (0xffffd84a) }, { "loopEnd", "L", juce::Colour (0xffffd84a) }, { "end", "E", juce::Colour (0xffff3b54) } };
        for (const auto& m : markers)
        {
            const auto x = xOf (value (m.id));
            g.setColour (m.c);
            g.drawLine (x, wave.getY(), x, wave.getBottom(), 2.0f);
            g.setFont (font (11.0f));
            g.drawText (m.tag, juce::Rectangle<float> (x + 3.0f, wave.getY() + 2.0f, 14.0f, 12.0f), juce::Justification::centredLeft, false);
        }
        for (auto& ph : sampler->playheads)
            if (const auto v = ph.load(); v >= 0.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawLine (xOf (v), wave.getY(), xOf (v), wave.getBottom(), 1.0f);
            }
    }
    // the envelopes
    auto envs = area.reduced (16.0f).removeFromBottom (area.getHeight() * 0.38f);
    const struct { const char* title; juce::Colour c; const char* a; const char* d; const char* s; const char* r; } env[] {
        { "AMP ENVELOPE", teal, "attack", "decay", "sustain", "release" },
        { "FILTER ENVELOPE", juce::Colour (0xffff8a1f), "fA", "fD", "fS", "fR" } };
    for (int i = 0; i < 2; ++i)
    {
        auto e = envs.removeFromLeft (envs.getWidth() / (2.0f - (float) i) - (i == 0 ? 8.0f : 0.0f));
        if (i == 0) envs.removeFromLeft (8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (e, 10.0f);
        g.setColour (env[i].c);
        g.setFont (font (10.0f));
        g.drawText (env[i].title, e.reduced (10.0f, 6.0f).removeFromTop (12.0f), juce::Justification::centredLeft, false);
        auto plot = e.reduced (12.0f, 8.0f).withTrimmedTop (14.0f);
        const auto a = value (env[i].a), dd = value (env[i].d), s = value (env[i].s), r = value (env[i].r);
        const auto total = juce::jmax (0.5f, a + dd + r + 0.5f);
        juce::Path p;
        auto x = plot.getX();
        p.startNewSubPath (x, plot.getBottom());
        x += plot.getWidth() * a / total; p.lineTo (x, plot.getY());
        x += plot.getWidth() * dd / total; p.lineTo (x, plot.getBottom() - s * plot.getHeight());
        x += plot.getWidth() * 0.5f / total; p.lineTo (x, plot.getBottom() - s * plot.getHeight());
        x += plot.getWidth() * r / total; p.lineTo (x, plot.getBottom());
        g.strokePath (p, juce::PathStrokeType (2.0f));
        p.closeSubPath();
        g.setColour (env[i].c.withAlpha (0.15f));
        g.fillPath (p);
    }
}

void StudioPanel::paintOverChildren (juce::Graphics& g)
{
    // the MIX tab's glass behind its strips (drawn first by paint? no: the strips are children - its frame here)
    if (page == 1)
    {
        g.setColour (teal.withAlpha (0.35f));
        g.drawRoundedRectangle (mixArea.reduced (4.0f), 16.0f, 1.0f);
        g.setColour (ink);
        g.setFont (font (11.0f, false));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("MIX  ·  inserts take our effects or any plug-in  ·  Output sends a part to its own mixer channel")),
                    mixArea.reduced (18.0f, 8.0f).removeFromTop (16.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
StudioPanel::Control* StudioPanel::controlAt (juce::Point<float> p)
{
    for (auto& c : controls)
    {
        if ((c->type == Control::knob || c->type == Control::encoder) && c->centre.getDistanceFrom (p) <= c->radius * 1.2f) return c.get();
        if ((c->type == Control::fader) && c->area.expanded (8.0f, 16.0f).contains (p)) return c.get();
        if ((c->type == Control::button || c->type == Control::padKey) && c->area.contains (p)) return c.get();
    }
    return nullptr;
}

int StudioPanel::kitPieceAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 1.0e9f;
    for (size_t i = 0; i < layout->pieces.size(); ++i)
    {
        const auto& h = layout->pieces[i];
        const auto d = h.c.getDistanceFrom (p);
        if (d < h.r && d / h.r < bestDistance) { bestDistance = d / h.r; best = (int) i; }
    }
    return best;
}

juce::Rectangle<float> StudioPanel::keyRect (int note) const
{
    const auto& k = layout->keys;
    const auto ww = k.getWidth() / (float) layout->whiteKeys;
    static const int whiteIndex[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static const bool black[12] { false, true, false, true, false, false, true, false, true, false, true, false };
    const auto rel = note - layout->lowestKey;
    const auto octave = rel / 12, pc = rel % 12;
    const auto wx = k.getX() + ww * (float) (octave * 7 + whiteIndex[pc]);
    if (black[pc])
        return { wx + ww * 0.68f, k.getY(), ww * 0.62f, k.getHeight() * 0.6f };
    return { wx, k.getY(), ww - 1.0f, k.getHeight() };
}

int StudioPanel::keyAt (juce::Point<float> p) const
{
    if (! layout->keys.contains (p)) return -1;
    for (int pass = 0; pass < 2; ++pass)
        for (int note = layout->lowestKey; note < layout->lowestKey + 61; ++note)
        {
            const auto pc = (note - layout->lowestKey) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            if ((pass == 0) == black && keyRect (note).contains (p)) return note;
        }
    return -1;
}

void StudioPanel::press (Control& c, const juce::MouseEvent& e)
{
    if (c.type == Control::padKey)
    {
        const auto pad = juce::roundToInt (value ("bank")) * studio::BeatPad::padsPerBank + c.pad;
        if (e.mods.isPopupMenu())
        {
            // a pad's menu: its output, choke and mode; your own sample
            juce::PopupMenu menu;
            menu.addSectionHeader ("Pad " + juce::String ("ABCD"[pad / 18]) + juce::String (pad % 18 + 1));
            menu.addItem ("Load a sample...", [this, pad]
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Choose a sample for the pad", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser, pad] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile())
                    {
                        um.beginNewTransaction ("Beat pad: sample");
                        state.setProperty (studiostate::fileProperty (pad), fc.getResult().getFullPathName(), &um);
                    }
                });
            });
            if (state.hasProperty (studiostate::fileProperty (pad)))
                menu.addItem ("Remove the sample", [this, pad] { um.beginNewTransaction ("Beat pad: sample"); state.setProperty (studiostate::fileProperty (pad), juce::String(), &um); });
            juce::PopupMenu outs;
            for (int o = 0; o < studio::BeatPad::outputs; ++o)
                outs.addItem ("Pads " + juce::String (o + 1), true, juce::roundToInt (value ("pad" + juce::String (pad) + "Out")) == o,
                              [this, pad, o] { setValue ("pad" + juce::String (pad) + "Out", (float) o, true); });
            menu.addSubMenu ("Output (its mixer strip)", outs);
            menu.addItem ("Choke group", [this, pad] { cycle ("pad" + juce::String (pad) + "Choke", 1); });
            menu.addItem ("Hold (stops when let go)", true, value ("pad" + juce::String (pad) + "Mode") > 0.5f, [this, pad] { cycle ("pad" + juce::String (pad) + "Mode", 1); });
            menu.addItem ("Reverse", true, value ("pad" + juce::String (pad) + "Reverse") > 0.5f, [this, pad] { cycle ("pad" + juce::String (pad) + "Reverse", 1); });
            menu.showMenuAsync ({});
            return;
        }
        setValue ("sel", (float) pad, false);
        heldNote = 36 + pad;
        const auto velocity = juce::jlimit (30, 127, juce::roundToInt (127.0f * (1.0f - (toFace (e.position).y - c.area.getY()) / c.area.getHeight() * 0.5f)));
        if (host.playNote) host.playNote (heldNote, velocity, true);
        return;
    }
    if (c.type == Control::button)
    {
        if (c.action) c.action();
        repaint();
        return;
    }
    if (e.mods.isPopupMenu() && c.id.isNotEmpty())
    {
        showControlMenu (c);
        return;
    }
    dragging = &c;
    dragStart = normalised (c.id);
    dragFrom = e.position;
    lastTouched = c.id;
    um.beginNewTransaction (studio::displayName (kind));
    if (auto* writer = AutomationWriter::current())
    {
        nameLane (c.id);
        writer->begin (track[ids::id].toString(), "studio:" + resolve (c.id), dragStart);
    }
}

juce::String StudioPanel::resolve (const juce::String& id) const
{
    if (id.startsWith ("@")) return studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
    if (id.startsWith ("#")) return "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
    return id;
}

juce::String StudioPanel::browserId() const
{
    if (kind == "keys" && screenPage == 1) return "program";
    if (kind == "bass" && screenPage == 1) return "model";
    if (kind == "guitar" && screenPage == 2) return "amp";
    if (kind == "beatpad" && screenPage == 1) return "kit";
    return {};
}

juce::StringArray StudioPanel::choicesOf (const juce::String& id) const
{
    if (id == "program")
    {
        juce::StringArray names;
        for (const auto& prog : studio::StudioKeys::programs())
            names.add (prog.name);
        return names;
    }
    for (const auto& prm : studio::infoOf (kind).params)
        if (prm.id == id)
            return prm.choices;
    return {};
}

juce::String StudioPanel::paramName (const juce::String& id) const
{
    const auto& info = studio::infoOf (kind);
    const auto real = resolve (id);
    for (const auto& prm : info.params)
        if (prm.id == real)
            return prm.name;
    return real;
}

void StudioPanel::nameLane (const juce::String& id)
{
    // (the lane carries its knob's name, for the track's automation menu and the lane's header)
    const auto param = "studio:" + resolve (id);
    auto lane = model::laneFor (project, track[ids::id].toString(), param, true);
    if (! lane.hasProperty (ids::name))
        lane.setProperty (ids::name, studio::displayName (kind) + " " + paramName (id), nullptr);
}

void StudioPanel::showAutomationOf (const juce::String& id)
{
    if (id.isEmpty()) return;
    nameLane (id);
    track.setProperty (ids::automationParam, "studio:" + resolve (id), nullptr);
    track.setProperty (ids::showAutomation, true, nullptr);
}

void StudioPanel::showControlMenu (Control& c)
{
    juce::PopupMenu menu;
    const auto name = paramName (c.id);
    menu.addSectionHeader (name);
    menu.addItem ("Show Automation (" + name + ")", [this, id = c.id] { showAutomationOf (id); });
    const auto param = "studio:" + resolve (c.id);
    const auto lane = model::laneFor (project, track[ids::id].toString(), param, false);
    menu.addItem ("Clear Automation", lane.isValid() && lane.getNumChildren() > 0, false, [this, param]
    {
        um.beginNewTransaction ("Clear automation");
        model::clearAutomation (project, track[ids::id].toString(), param, &um);
    });
    menu.addSeparator();
    menu.addItem ("Reset to Default", [this, id = c.id]
    {
        if (auto inst = host.instrument ? host.instrument() : nullptr)
            if (const auto i = inst->indexOf (resolve (id)); i >= 0)
                setValue (id, inst->params()[(size_t) i].def, true);
    });
    menu.showMenuAsync ({});
}

void StudioPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toFace (e.position);
    grabKeyboardFocus();
    // the screen's tabs
    if (! tempoArea.isEmpty() && tempoArea.expanded (2.0f).contains (p) && ! e.mods.isPopupMenu())
    {
        tempoDragging = true;
        tempoStart = (double) project.getProperty (ids::tempo, 120.0);
        tempoMoved = false;
        return;
    }
    if (layout->screen.contains (p))
    {
        auto tabs = layout->screen.withTrimmedTop (layout->screen.getHeight() - 12.0f);
        if (tabs.contains (p))
        {
            const auto i = juce::jlimit (0, layout->screenTabs.size() - 1, (int) ((p.x - tabs.getX()) / (tabs.getWidth() / (float) layout->screenTabs.size())));
            if (layout->screenTabs[i] == "MIX") showPage (page == 1 ? 0 : 1);
            else { showPage (0); screenPage = i; }
            repaint();
            return;
        }
        // a browser page: click a row to choose it (the wheel scrolls it)
        if (const auto browse = browserId(); browse.isNotEmpty() && page == 0)
        {
            const auto names = choicesOf (browse);
            auto list = layout->screen.reduced (1.0f).withTrimmedTop (12.0f).reduced (4.0f, 2.0f).withTrimmedBottom (12.0f);
            list.removeFromTop (11.0f);
            const auto current = juce::roundToInt (value (browse));
            const auto first = juce::jlimit (0, juce::jmax (0, names.size() - 5), current - 2);
            const auto k = first + (int) ((p.y - list.getY()) / (list.getHeight() / 5.0f));
            if (juce::isPositiveAndBelow (k, names.size()) && list.contains (p))
                setValue (browse, (float) k, true);
            return;
        }
        // the drums' models
        if (kind == "drums" && screenPage == 1 && page == 0)
        {
            auto list = layout->screen.reduced (1.0f).withTrimmedTop (12.0f).reduced (4.0f, 2.0f).withTrimmedBottom (12.0f);
            list.removeFromTop (11.0f);
            const auto m = (int) ((p.y - list.getY()) / (list.getHeight() / 5.0f));
            if (juce::isPositiveAndBelow (m, 5))
            {
                setValue ("@Alt", (float) m, true);
                const auto note = studio::StudioDrums::noteOfPiece (juce::roundToInt (value ("sel")));
                if (host.playNote) { host.playNote (note, 100, true); host.playNote (note, 0, false); }
            }
            return;
        }
        // the beat pad's steps
        if (kind == "beatpad" && screenPage == 2)
            if (auto bp = std::dynamic_pointer_cast<studio::BeatPad> (host.instrument ? host.instrument() : nullptr))
            {
                const auto grid = layout->screen.reduced (1.0f).withTrimmedTop (12.0f).reduced (4.0f, 3.0f).withTrimmedBottom (12.0f);
                const auto rowH = grid.getHeight() / 18.0f, colW = (grid.getWidth() - 56.0f) / 16.0f;
                const auto row = (int) ((p.y - grid.getY()) / rowH), step = (int) ((p.x - grid.getX() - 56.0f) / colW);
                if (juce::isPositiveAndBelow (row, 18) && juce::isPositiveAndBelow (step, 16))
                {
                    const auto pad = juce::roundToInt (value ("bank")) * studio::BeatPad::padsPerBank + row;
                    bp->setStep (pad, step, bp->getStep (pad, step) > 0 ? 0 : 100);
                    um.beginNewTransaction ("Beat pad: step");
                    state.setProperty (studiostate::pattern, bp->patternText(), &um);
                    repaint();
                }
                return;
            }
    }
    if (auto* c = controlAt (p))
    {
        press (*c, e);
        return;
    }
    // the sampler's markers
    if (kind == "sampler" && layout->editor.contains (p))
    {
        const auto wave = layout->editor.reduced (16.0f).removeFromTop (layout->editor.getHeight() * 0.55f);
        const char* ids[] { "start", "loopStart", "loopEnd", "end" };
        float best = 12.0f;
        dragMarker = -1;
        for (int m = 0; m < 4; ++m)
            if (const auto d = std::abs (wave.getX() + value (ids[m]) * wave.getWidth() - p.x); d < best) { best = d; dragMarker = m; }
        if (dragMarker >= 0) um.beginNewTransaction ("Sampler: markers");
        return;
    }
    // the drums' pieces, the keys
    if (kind == "drums")
        if (const auto i = kitPieceAt (p); i >= 0)
        {
            const auto& h = layout->pieces[(size_t) i];
            setValue ("sel", (float) h.piece, true);
            const auto velocity = juce::jlimit (40, 127, juce::roundToInt (127.0f * (1.0f - h.c.getDistanceFrom (p) / h.r * 0.5f)));
            heldNote = h.note;
            if (host.playNote) host.playNote (h.note, velocity, true);
            return;
        }
    if (kind == "keys")
        if (const auto note = keyAt (p); note >= 0)
        {
            heldNote = note;
            const auto k = keyRect (note);
            const auto velocity = juce::jlimit (30, 127, juce::roundToInt (40.0f + 87.0f * (p.y - k.getY()) / k.getHeight()));
            if (host.playNote) host.playNote (note, velocity, true);
            return;
        }
    if ((kind == "bass" || kind == "guitar") && ! layout->strings.empty() && p.x > layout->stringLeft && p.x < layout->stringRight)
        for (size_t s = 0; s < layout->strings.size(); ++s)
            if (std::abs (layout->strings[s] - p.y) < 6.0f)
            {
                const int bassOpen[] { 43, 38, 33, 28, 23 }, gtrOpen[] { 64, 59, 55, 50, 45, 40 };
                heldNote = kind == "bass" ? bassOpen[juce::jmin ((int) s, 4)] : gtrOpen[juce::jmin ((int) s, 5)];
                if (host.playNote) host.playNote (heldNote, 100, true);
                return;
            }
}

void StudioPanel::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = toFace (e.position);
    if (dragMarker >= 0)
    {
        const auto wave = layout->editor.reduced (16.0f).removeFromTop (layout->editor.getHeight() * 0.55f);
        const char* ids[] { "start", "loopStart", "loopEnd", "end" };
        setValue (ids[dragMarker], juce::jlimit (0.0f, 1.0f, (p.x - wave.getX()) / wave.getWidth()), false);
        return;
    }
    if (tempoDragging)
    {
        const auto dy = -e.getDistanceFromDragStartY();
        if (! tempoMoved && std::abs (dy) < 3) return;
        if (! tempoMoved) { tempoMoved = true; um.beginNewTransaction ("Tempo"); }
        const auto bpm = e.mods.isShiftDown() ? std::round ((tempoStart + dy * 0.1) * 10.0) / 10.0
                                              : std::round (tempoStart) + std::round (dy / 3.0);
        if (std::abs (bpm - (double) project.getProperty (ids::tempo, 120.0)) > 1.0e-6)
            model::setTempo (project, juce::jlimit (20.0, 400.0, bpm), &um);
        repaint();
        return;
    }
    if (dragging == nullptr)
    {
        // a held key or drum: dragging onto another plays it (as a glissando)
        if (kind == "keys" && heldNote >= 0)
            if (const auto note = keyAt (p); note >= 0 && note != heldNote)
            {
                if (host.playNote) { host.playNote (heldNote, 0, false); host.playNote (note, 90, true); }
                heldNote = note;
            }
        return;
    }
    const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto travel = dragging->type == Control::fader ? dragging->area.getHeight() * scale : 160.0f;
    const auto n = juce::jlimit (0.0f, 1.0f, dragStart + (float) (dragFrom.y - e.position.y) / travel * fine);
    setNormalised (dragging->id, n, false);
    if (auto* writer = AutomationWriter::current())
        writer->move (track[ids::id].toString(), "studio:" + resolve (dragging->id), n);
}

void StudioPanel::mouseUp (const juce::MouseEvent&)
{
    if (heldNote >= 0 && host.playNote)
        host.playNote (heldNote, 0, false);
    heldNote = -1;
    if (dragging != nullptr)
        if (auto* writer = AutomationWriter::current())
            writer->end (track[ids::id].toString(), "studio:" + resolve (dragging->id));
    dragging = nullptr;
    dragMarker = -1;
    tempoDragging = false;
    repaint();
}

void StudioPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // over the screen's browser: through its list; over a knob: turns it
    const auto p = toFace (e.position);
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (std::abs (delta) < 1.0e-4f) return;
    if (const auto browse = browserId(); browse.isNotEmpty() && page == 0 && layout->screen.contains (p))
    {
        const auto count = choicesOf (browse).size();
        if (count > 0)
            setValue (browse, (float) juce::jlimit (0, count - 1, juce::roundToInt (value (browse)) + (delta < 0 ? 1 : -1)), true);
        return;
    }
    if (auto* c = controlAt (p); c != nullptr && (c->type == Control::knob || c->type == Control::encoder || c->type == Control::fader) && c->id.isNotEmpty())
        setNormalised (c->id, normalised (c->id) + delta * 0.08f, true);
}

void StudioPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    // back to its default
    if (auto* c = controlAt (toFace (e.position)); c != nullptr && c->id.isNotEmpty())
        if (auto inst = host.instrument ? host.instrument() : nullptr)
        {
            auto id = c->id;
            if (id.startsWith ("@")) id = studio::StudioDrums::pieceId (juce::roundToInt (value ("sel"))) + id.substring (1);
            else if (id.startsWith ("#")) id = "pad" + juce::String (juce::roundToInt (value ("sel"))) + id.substring (1);
            if (const auto i = inst->indexOf (id); i >= 0)
                setValue (c->id, inst->params()[(size_t) i].def, true);
        }
}

void StudioPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto* c = controlAt (toFace (e.position));
    const auto id = c != nullptr && (c->type == Control::knob || c->type == Control::encoder) ? c->id : juce::String();
    if (id != hoverId) { hoverId = id; repaint(); }
}

void StudioPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const juce::File file (files[0]);
    if (kind == "sampler")
    {
        um.beginNewTransaction ("Sampler: sample");
        state.setProperty (studiostate::fileProperty (0), file.getFullPathName(), &um);
        return;
    }
    if (kind == "beatpad")
        if (auto* c = controlAt (toFace ({ (float) x, (float) y })); c != nullptr && c->type == Control::padKey)
        {
            const auto pad = juce::roundToInt (value ("bank")) * studio::BeatPad::padsPerBank + c->pad;
            um.beginNewTransaction ("Beat pad: sample");
            state.setProperty (studiostate::fileProperty (pad), file.getFullPathName(), &um);
        }
}

void StudioPanel::slice()
{
    // the recording cut at its hits, each slice a file, onto a new beat pad's pads
    auto sampler = std::dynamic_pointer_cast<studio::StudioSampler> (host.instrument ? host.instrument() : nullptr);
    if (sampler == nullptr || sampler->getSample() == nullptr || ! host.slicesToBeatPad)
        return;
    const auto sample = sampler->getSample();
    const auto points = sampler->findSlices (0.6f);
    const auto source = sampler->getSampleFile();
    const auto folder = source.getParentDirectory().getChildFile (source.getFileNameWithoutExtension() + " slices");
    folder.createDirectory();
    juce::Array<juce::File> files;
    juce::WavAudioFormat wav;
    for (int i = 0; i < points.size() && i < studio::BeatPad::numPads; ++i)
    {
        const auto from = (int) (points[i] * (float) sample->getNumSamples());
        const auto to = i + 1 < points.size() ? (int) (points[i + 1] * (float) sample->getNumSamples()) : sample->getNumSamples();
        if (to - from < 64) continue;
        const auto file = folder.getChildFile ("slice " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampler->getSampleRate()).withNumChannels (2).withBitsPerSample (24)))
        {
            writer->writeFromAudioSampleBuffer (*sample, from, to - from);
            files.add (file);
        }
    }
    host.slicesToBeatPad (files);
}

//==============================================================================
void StudioPanel::timerCallback()
{
    for (auto* s : strips)
    {
        auto inst = host.instrument ? host.instrument() : nullptr;
        s->level = inst != nullptr && s->part >= 0 ? inst->partLevel (s->part) : 0.0f;
        if (page == 1) s->repaint();
    }
    // the parts appear once the instrument has loaded
    if (strips.isEmpty() && host.instrument && host.instrument() != nullptr)
        rebuildMix();
    // the knobs that have automation (their light; they show what it plays)
    if (++ticks % 8 == 0)
    {
        juce::StringArray now;
        const auto owner = track[ids::id].toString();
        const bool reading = track.getProperty (ids::automationMode, "read").toString() != "off";
        for (const auto& lane : model::mixerOf (project))
            if (reading && lane.hasType (ids::LANE) && lane[ids::owner].toString() == owner && lane.getNumChildren() > 0
                && lane[ids::param].toString().startsWith ("studio:"))
                for (auto& c : controls)
                    if (c->id.isNotEmpty() && "studio:" + resolve (c->id) == lane[ids::param].toString())
                        now.addIfNotAlreadyThere (c->id);
        automatedIds = now;
    }
    repaint();
}

void StudioPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree == state || tree.hasType (ids::INSERT) || tree.getParent().hasType (ids::INSERT))
        for (auto* s : strips) s->refresh();
}

//==============================================================================
namespace
{
    juce::String presetOf (const juce::ValueTree& track)
    {
        return studiostate::of (track)["presetName"].toString();
    }
}

juce::File StudioWindow::presetFolder (const juce::String& kind)
{
    return presets::root().getChildFile (juce::File::createLegalFileName ("Studio " + studio::displayName (kind)));
}

juce::Rectangle<int> StudioWindow::naturalSize (const juce::String& kind)
{
    const auto n = StudioPanel::naturalSize (kind);
    return n.withHeight (n.getHeight() + headerHeight);
}

void StudioWindow::applyDefault (juce::ValueTree track, juce::UndoManager* um)
{
    const auto file = presetFolder (studiostate::kindOf (track)).getChildFile ("__default__.xml");
    if (! file.existsAsFile())
        return;
    if (auto xml = juce::parseXML (file))
    {
        auto tree = studiostate::getOrCreate (track, um);
        const auto loaded = juce::ValueTree::fromXml (*xml);
        for (int i = 0; i < loaded.getNumProperties(); ++i)
            tree.setProperty (loaded.getPropertyName (i), loaded[loaded.getPropertyName (i)], um);
        tree.setProperty ("presetName", "Default", um);
    }
}

StudioWindow::StudioWindow (juce::ValueTree p, juce::ValueTree t, juce::UndoManager& u, StudioPanel::Host host)
    : project (p), track (t), um (u), kind (studiostate::kindOf (t)), panel (p, t, u, std::move (host))
{
    addAndMakeVisible (panel);
    for (auto* b : { &pin, &bypass, &previous, &preset, &next, &copy, &paste, &automation })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    pin.setClickingTogglesState (true);
    pin.setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.8f));
    pin.setTooltip ("Pin: the window stays above the main window while you work (Settings choose how new ones open)");
    pin.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<EditorWindow>())
            window->setPinned (pin.getToggleState());
    };
    bypass.setClickingTogglesState (true);
    bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9b949));
    bypass.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    bypass.setTooltip ("Bypass: the instrument out (silent)");
    bypass.onClick = [this]
    {
        um.beginNewTransaction (bypass.getToggleState() ? "Bypass instrument" : "Activate instrument");
        track.setProperty (ids::instrumentBypass, bypass.getToggleState(), &um);
    };
    preset.setTooltip ("Presets: the factory ones, yours, Save, Set as Default");
    preset.onClick = [this] { showPresetMenu(); };
    previous.setTooltip ("The previous preset");
    next.setTooltip ("The next preset");
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    copy.setTooltip ("Copy the whole setting (paste it into another " + studio::displayName (kind) + ")");
    copy.onClick = [this]
    {
        if (auto xml = studiostate::of (track).createXml())
            juce::SystemClipboard::copyTextToClipboard (xml->toString());
    };
    paste.setTooltip ("Paste a copied setting");
    paste.onClick = [this]
    {
        if (auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard()); xml != nullptr && xml->hasTagName ("STUDIO"))
            loadXml (*xml, "Pasted");
    };
    automation.setTooltip ("Automation: the mode, and the lane of the knob you last turned (right-click a knob for its own)");
    automation.onClick = [this] { showAutomationMenu(); };
    track.addListener (this);
    setSize (naturalSize (kind).getWidth(), naturalSize (kind).getHeight());
    refresh();
}

StudioWindow::~StudioWindow()
{
    track.removeListener (this);
}

void StudioWindow::refresh()
{
    bypass.setToggleState ((bool) track[ids::instrumentBypass], juce::dontSendNotification);
    if (auto* window = findParentComponentOfClass<EditorWindow>())
        pin.setToggleState (window->isPinned(), juce::dontSendNotification);
    const auto name = presetOf (track);
    preset.setButtonText ((name.isNotEmpty() ? name : juce::String ("Preset")) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    const auto light = theme::glassLight();
    for (auto* b : { &pin, &bypass, &previous, &preset, &next, &copy, &paste, &automation })
    {
        b->setColour (juce::TextButton::buttonColourId, light ? juce::Colours::white.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.08f));
        b->setColour (juce::TextButton::textColourOffId, light ? juce::Colours::black : ink);
    }
    const auto mode = track.getProperty (ids::automationMode, "read").toString();
    automation.setButtonText ("Automation: " + mode.substring (0, 1).toUpperCase() + mode.substring (1) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
}

void StudioWindow::paint (juce::Graphics& g)
{
    const auto header = getLocalBounds().removeFromTop (headerHeight).toFloat();
    if (theme::glassLight())
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f6f9), 0.0f, 0.0f, juce::Colour (0xffdde2e8), 0.0f, header.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0.0f, 0.0f, juce::Colour (0xff15181d), 0.0f, header.getBottom(), false));
    g.fillRect (header);
    g.setColour (theme::glassLight() ? juce::Colours::black.withAlpha (0.12f) : juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) header.getBottom() - 1, 0.0f, header.getRight());
    g.setColour (theme::glassLight() ? juce::Colour (0xff20242a) : ink);
    g.setFont (font (13.0f));
    g.drawText (studio::displayName (kind), header.toNearestInt().reduced (12, 0), juce::Justification::centredRight, false);
}

void StudioWindow::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 5);
    const auto place = [&header] (juce::Component& c, int w) { c.setBounds (header.removeFromLeft (w)); header.removeFromLeft (6); };
    place (pin, 44);
    place (bypass, 66);
    previous.setBounds (header.removeFromLeft (24)); header.removeFromLeft (2);
    preset.setBounds (header.removeFromLeft (190)); header.removeFromLeft (2);
    place (next, 24);
    place (copy, 52);
    place (paste, 52);
    place (automation, 150);
    panel.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}

juce::StringArray StudioWindow::factoryNames() const
{
    // the kits (drums, beat pad), the programs (keys), the models (bass, guitar), the sampler's playback
    const auto& info = studio::infoOf (kind);
    for (const auto* id : { "kit", "program", "model" })
        for (const auto& prm : info.params)
            if (prm.id == id)
                return prm.choices;
    return {};
}

void StudioWindow::loadFactory (int index)
{
    um.beginNewTransaction (studio::displayName (kind) + ": preset");
    auto tree = studiostate::getOrCreate (track, &um);
    // (the factory setting: everything back to its default, but the files kept)
    for (int i = tree.getNumProperties(); --i >= 0;)
        if (const auto name = tree.getPropertyName (i); ! studiostate::isFileProperty (name))
            tree.removeProperty (name, &um);
    const auto names = factoryNames();
    if (kind == "drums")
        for (const auto& [id, v] : studio::StudioDrums::kitSettings (index))
            tree.setProperty (id, v, &um);
    else
    {
        const auto& info = studio::infoOf (kind);
        for (const auto* id : { "kit", "program", "model" })
            for (const auto& prm : info.params)
                if (prm.id == id && ! tree.hasProperty (id) && prm.choices.size() == names.size())
                    tree.setProperty (id, (float) index, &um);
    }
    tree.setProperty ("presetName", names[index], &um);
}

juce::StringArray StudioWindow::presetNames() const
{
    juce::StringArray names;
    for (const auto& f : presetFolder (kind).findChildFiles (juce::File::findFiles, false, "*.xml"))
        if (f.getFileNameWithoutExtension() != "__default__")
            names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void StudioWindow::loadXml (const juce::XmlElement& xml, const juce::String& name)
{
    um.beginNewTransaction (studio::displayName (kind) + ": " + name);
    auto tree = studiostate::getOrCreate (track, &um);
    const auto loaded = juce::ValueTree::fromXml (xml);
    for (int i = tree.getNumProperties(); --i >= 0;)
        if (const auto p = tree.getPropertyName (i); ! loaded.hasProperty (p))
            tree.removeProperty (p, &um);
    for (int i = 0; i < loaded.getNumProperties(); ++i)
        tree.setProperty (loaded.getPropertyName (i), loaded[loaded.getPropertyName (i)], &um);
    tree.setProperty ("presetName", name, &um);
}

void StudioWindow::loadPreset (const juce::String& name)
{
    const auto file = presetFolder (kind).getChildFile (name == "Default" ? juce::String ("__default__.xml") : juce::File::createLegalFileName (name) + ".xml");
    if (auto xml = juce::parseXML (file))
        loadXml (*xml, name);
}

void StudioWindow::savePreset (const juce::String& name)
{
    auto folder = presetFolder (kind);
    folder.createDirectory();
    auto copyOf = studiostate::of (track).createCopy();
    copyOf.removeProperty ("presetName", nullptr);
    if (auto xml = copyOf.createXml())
        xml->writeTo (folder.getChildFile (name == "Default" ? juce::String ("__default__.xml") : juce::File::createLegalFileName (name) + ".xml"));
    studiostate::getOrCreate (track, nullptr).setProperty ("presetName", name, nullptr);
}

void StudioWindow::step (int direction)
{
    // through the factory presets, then yours
    const auto factory = factoryNames();
    auto all = factory;
    all.addArray (presetNames());
    if (all.isEmpty()) return;
    const auto at = all.indexOf (presetOf (track));
    const auto to = at < 0 ? (direction > 0 ? 0 : all.size() - 1) : (at + direction + all.size()) % all.size();
    if (to < factory.size()) loadFactory (to);
    else loadPreset (all[to]);
}

void StudioWindow::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto current = presetOf (track);
    menu.addItem ("Save Preset...", [this]
    {
        auto* box = new juce::AlertWindow ("Save Preset", "A name for this " + studio::displayName (kind) + " setting:", juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", presetOf (track) == "Default" ? juce::String() : presetOf (track));
        box->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, box] (int result)
        {
            const auto name = box->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
                savePreset (name);
        }), true);
    });
    menu.addItem ("Set as Default for " + studio::displayName (kind), [this] { savePreset ("Default"); });
    const auto hasDefault = presetFolder (kind).getChildFile ("__default__.xml").existsAsFile();
    menu.addItem ("Load Default", hasDefault, false, [this] { loadPreset ("Default"); });
    menu.addItem ("Clear Default (start from the factory settings)", hasDefault, false, [this] { presetFolder (kind).getChildFile ("__default__.xml").deleteFile(); });
    menu.addSeparator();
    const auto factory = factoryNames();
    juce::PopupMenu factoryMenu;
    for (int i = 0; i < factory.size(); ++i)
        factoryMenu.addItem (factory[i], true, factory[i] == current, [this, i] { loadFactory (i); });
    if (factory.isEmpty())
        factoryMenu.addItem ("Init", [this] { loadFactory (0); });
    menu.addSubMenu ("Factory", factoryMenu);
    const auto names = presetNames();
    if (names.isEmpty())
        menu.addItem ("(no presets of yours yet)", false, false, [] {});
    for (const auto& n : names)
        menu.addItem (n, true, n == current, [this, n] { loadPreset (n); });
    if (! names.isEmpty())
    {
        juce::PopupMenu remove;
        for (const auto& n : names)
            remove.addItem (n, [this, n] { presetFolder (kind).getChildFile (juce::File::createLegalFileName (n) + ".xml").deleteFile(); });
        menu.addSeparator();
        menu.addSubMenu ("Delete Preset", remove);
    }
    menu.addItem ("Show Presets Folder", [this]
    {
        auto folder = presetFolder (kind);
        folder.createDirectory();
        folder.revealToUser();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&preset));
}

void StudioWindow::showAutomationMenu()
{
    juce::PopupMenu menu;
    const auto mode = track.getProperty (ids::automationMode, "read").toString();
    for (auto [m, name] : { std::pair { "off", "Off" }, std::pair { "read", "Read" }, std::pair { "touch", "Touch" },
                            std::pair { "latch", "Latch" }, std::pair { "write", "Write" } })
        menu.addItem (name, true, mode == m, [this, value = juce::String (m)] { track.setProperty (ids::automationMode, value, nullptr); });
    menu.addSeparator();
    const auto last = panel.lastTouchedId();
    menu.addItem (last.isNotEmpty() ? "Show Automation of the last knob turned" : juce::String ("Show Automation (turn a knob first)"),
                  last.isNotEmpty(), false, [this, last] { panel.showAutomationOf (last); });
    menu.addItem ("Hide Automation", (bool) track[ids::showAutomation], false, [this] { track.setProperty (ids::showAutomation, false, nullptr); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automation));
}

} // namespace riff
