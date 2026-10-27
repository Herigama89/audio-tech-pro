#include "NemixPanel.h"
#include "../engine/nemix/Nemix.h"
#include "../engine/nemix/NemixPresets.h"
#include "../model/NemixState.h"
#include "BinaryData.h"
#include <complex>
#include <cstring>

namespace riff
{
using namespace nemix;

namespace
{
    using R = juce::Rectangle<int>;
    using RF = juce::Rectangle<float>;

    // The logo's colours: spray-paint teal, violet, orange and red on dark concrete
    namespace col
    {
        const juce::Colour teal (0xff19d3d0), purple (0xffa25bff), orange (0xffff8a1f), red (0xffff3b54), pink (0xffff4fb4),
                           yellow (0xffffd84a), green (0xff5cff8f), blue (0xff3fa8ff), text (0xffeef2f6), dim (0xff98a3ae),
                           panel (0xd80c0f14), well (0xff07090c);
    }

    juce::Font font (float size, bool bold = false) { return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain); }

    juce::Colour sourceColour (int src)
    {
        if (src >= srcEnv2 && src <= srcEnv4) return col::green;
        if (src >= srcLfo1 && src <= srcLfo4) return col::blue;
        if (src >= srcMacro1 && src <= srcMacro4) return col::yellow;
        if (src == srcEnv1) return col::orange;
        return col::pink;
    }

    /** A knob's short label ("Pos", "Cutoff"): the name without its group. */
    juce::String shortName (int index)
    {
        auto n = params()[(size_t) index].name;
        for (const char* prefix : { "Osc A ", "Osc B ", "Osc C ", "Filter 1 ", "Filter 2 ", "Env 1 ", "Env 2 ", "Env 3 ", "Env 4 ", "LFO 1 ", "LFO 2 ",
                                    "LFO 3 ", "LFO 4 ", "Sub ", "Noise ", "Distortion ", "Multiband ", "Chorus ", "Phaser ", "Delay ", "Reverb ",
                                    "EQ ", "FX Filter ", "Arp " })
            if (n.startsWith (prefix)) return n.substring ((int) std::strlen (prefix));
        return n;
    }

    juce::String valueText (int index, float v)
    {
        const auto& p = params()[(size_t) index];
        if (p.isChoice()) return p.text (v);
        if (p.suffix == " Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
        if (p.suffix == " s")  return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
        if (p.suffix.isEmpty() && p.min >= -1.0f && p.max <= 1.0f && p.min < p.max) return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        if (p.max - p.min >= 8.0f && p.midPoint == 0.5f * (p.min + p.max) && p.suffix.isEmpty()) return juce::String (juce::roundToInt (v));
        return juce::String (v, std::abs (v) < 10.0f ? 2 : 1) + p.suffix;
    }

    //==============================================================================
    /** What the controls need from the window. */
    struct Host
    {
        virtual ~Host() = default;
        virtual float get (int index) const = 0;
        virtual void set (int index, float value) = 0;
        virtual void beginGesture (const juce::String& what) = 0;
        virtual std::vector<int> routesTo (int param) const = 0;   // matrix slots (0-based)
        virtual void addRoute (int source, int param) = 0;
        virtual float liveMod (int param) const = 0;
        virtual void startSourceDrag (int source, juce::Component* from) = 0;
    };

    //==============================================================================
    class Look final : public juce::LookAndFeel_V4
    {
    public:
        Look()
        {
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff12151b));
            setColour (juce::PopupMenu::textColourId, col::text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, col::purple.withAlpha (0.55f));
            setColour (juce::PopupMenu::headerTextColourId, col::teal);
            setColour (juce::ComboBox::textColourId, col::text);
            setColour (juce::ComboBox::backgroundColourId, col::well);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.12f));
            setColour (juce::ComboBox::arrowColourId, col::dim);
            setColour (juce::TextEditor::backgroundColourId, col::well);
            setColour (juce::TextEditor::textColourId, col::text);
            setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.15f));
            setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::ScrollBar::thumbColourId, col::purple.withAlpha (0.6f));
            setColour (juce::Slider::textBoxTextColourId, col::text);
            setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0101318));
            setColour (juce::TooltipWindow::textColourId, col::text);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
        {
            const auto accent = s.getProperties().contains ("accent") ? juce::Colour::fromString (s.getProperties()["accent"].toString()) : col::teal;
            const auto area = RF ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
            const auto size = std::min (area.getWidth(), area.getHeight());
            const auto c = area.getCentre();
            const auto radius = size * 0.5f;
            // the ring: the track, then the value (from the centre for +/- knobs)
            const auto bipolar = (bool) s.getProperties()["bipolar"];
            const auto angle = start + pos * (end - start);
            const auto from = bipolar ? start + 0.5f * (end - start) : start;
            juce::Path track, value;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, std::min (from, angle), std::max (from, angle), true);
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.strokePath (track, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (0.28f));
            g.strokePath (value, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (value, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // the cap
            const auto cap = RF (size * 0.68f, size * 0.68f).withCentre (c);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f48), cap.getX(), cap.getY(), juce::Colour (0xff121419), cap.getRight(), cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawEllipse (cap, 1.2f);
            juce::Path pointer;
            pointer.addRoundedRectangle (-1.4f, -cap.getHeight() * 0.48f, 2.8f, cap.getHeight() * 0.34f, 1.4f);
            g.setColour (col::text);
            g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = RF (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
            g.setColour (col::well.withAlpha (0.9f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (box.hasKeyboardFocus (false) ? col::teal : juce::Colours::white.withAlpha (0.14f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            juce::Path arrow;
            const auto ax = (float) w - 12.0f, ay = (float) h * 0.5f;
            arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
            g.setColour (col::dim);
            g.fillPath (arrow);
        }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f, true); }
        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }
        juce::Font getPopupMenuFont() override { return font (13.5f); }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            const auto accent = b.getProperties().contains ("accent") ? juce::Colour::fromString (b.getProperties()["accent"].toString()) : col::teal;
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const auto on = b.getToggleState();
            g.setColour (on ? accent.withAlpha (0.9f) : col::well.withAlpha (down ? 1.0f : 0.85f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? accent.brighter (0.4f) : juce::Colours::white.withAlpha (over ? 0.3f : 0.14f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour (b.getToggleState() ? juce::Colours::black : col::text);
            g.setFont (font (std::min (13.0f, (float) b.getHeight() * 0.55f), true));
            g.drawText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, false);
        }
    };

    //==============================================================================
    /** A knob bound to a parameter: drop a modulation source on it, right-click for its routes. */
    class Knob final : public juce::Component,
                       public juce::DragAndDropTarget,
                       public juce::SettableTooltipClient
    {
    public:
        Knob (Host& h, int paramIndex, juce::Colour accentColour, const juce::String& labelText = {})
            : host (h), index (paramIndex), accent (accentColour), label (labelText.isNotEmpty() ? labelText : shortName (paramIndex))
        {
            const auto& p = params()[(size_t) index];
            juce::NormalisableRange<double> range (p.min, p.max);
            const auto centre = 0.5f * (p.min + p.max);
            if (p.midPoint > p.min && p.midPoint < p.max && std::abs (p.midPoint - centre) > 1.0e-4f * (p.max - p.min))
                range.setSkewForCentre (p.midPoint);
            if (! modulatable (index)) range.interval = 1.0;
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setNormalisableRange (range);
            slider.setDoubleClickReturnValue (true, p.defaultValue);
            slider.setPopupMenuEnabled (false);
            slider.setScrollWheelEnabled (true);
            slider.getProperties().set ("accent", accent.toString());
            slider.getProperties().set ("bipolar", p.min < 0.0f && p.max > 0.0f);
            slider.onDragStart = [this] { host.beginGesture (params()[(size_t) index].name); };
            slider.onValueChange = [this]
            {
                if (! updating) host.set (index, (float) slider.getValue());
                repaint();
            };
            slider.addMouseListener (this, false);
            addAndMakeVisible (slider);
            setTooltip (p.name + "  (drag a source here to modulate; right-click for its modulation; double-click resets)");
            refresh();
        }

        void refresh()
        {
            const juce::ScopedValueSetter<bool> busy (updating, true);
            slider.setValue (host.get (index), juce::dontSendNotification);
            repaint();
        }

        int getIndex() const noexcept { return index; }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (15);
            slider.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            const auto showValue = slider.isMouseOverOrDragging() || dropping;
            g.setColour (showValue ? accent : col::dim);
            g.setFont (font (11.0f, true));
            g.drawText (showValue ? valueText (index, (float) slider.getValue()) : label.toUpperCase(), getLocalBounds().removeFromBottom (15),
                        juce::Justification::centred, true);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // The modulation: an arc per route (its amount), and a dot where it is now
            const auto routes = host.routesTo (index);
            const auto area = slider.getBounds().toFloat().reduced (5.0f);
            const auto size = std::min (area.getWidth(), area.getHeight());
            const auto c = area.getCentre();
            const auto start = juce::MathConstants<float>::pi * 1.2f, end = juce::MathConstants<float>::pi * 2.8f;
            const auto base = (float) slider.valueToProportionOfLength (slider.getValue());
            auto radius = size * 0.5f + 4.5f;
            for (const auto slot : routes)
            {
                const auto src = juce::roundToInt (host.get (ix().slot[slot].src));
                const auto amount = host.get (ix().slot[slot].amt);
                const auto to = juce::jlimit (0.0f, 1.0f, base + amount);
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start + std::min (base, to) * (end - start), start + std::max (base, to) * (end - start), true);
                g.setColour (sourceColour (src));
                g.strokePath (arc, juce::PathStrokeType (2.0f));
                radius += 3.0f;
            }
            if (! routes.empty())
            {
                const auto now = juce::jlimit (0.0f, 1.0f, base + host.liveMod (index));
                const auto a = start + now * (end - start);
                const auto r = size * 0.5f;
                g.setColour (juce::Colours::white);
                g.fillEllipse (RF (5.0f, 5.0f).withCentre ({ c.x + r * std::sin (a), c.y - r * std::cos (a) }));
            }
            if (dropping)
            {
                g.setColour (col::yellow.withAlpha (0.8f));
                g.drawEllipse (RF (size + 6.0f, size + 6.0f).withCentre (c), 2.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return;
            juce::PopupMenu menu;
            menu.addSectionHeader (params()[(size_t) index].name);
            menu.addItem ("Reset to default", [this] { host.beginGesture ("Reset"); host.set (index, params()[(size_t) index].defaultValue); });
            const auto routes = host.routesTo (index);
            if (! routes.empty())
            {
                menu.addSeparator();
                for (const auto slot : routes)
                {
                    const auto src = juce::roundToInt (host.get (ix().slot[slot].src));
                    const auto amount = host.get (ix().slot[slot].amt);
                    juce::PopupMenu sub;
                    for (const auto pct : { 100, 75, 50, 35, 25, 10, 5, -5, -10, -25, -50, -75, -100 })
                        sub.addItem ((pct > 0 ? "+" : "") + juce::String (pct) + " %", true, juce::roundToInt (amount * 100.0f) == pct, [this, slot, pct]
                        {
                            host.beginGesture ("Modulation amount");
                            host.set (ix().slot[slot].amt, (float) pct / 100.0f);
                        });
                    sub.addSeparator();
                    sub.addItem ("Remove", [this, slot]
                    {
                        host.beginGesture ("Remove modulation");
                        host.set (ix().slot[slot].src, 0.0f);
                        host.set (ix().slot[slot].amt, 0.0f);
                    });
                    menu.addSubMenu (modSources()[(size_t) src] + "  " + (amount >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (amount * 100.0f)) + " %", sub);
                }
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("nmxsrc:") && modulatable (index); }
        void itemDragEnter (const SourceDetails&) override { dropping = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { dropping = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            dropping = false;
            host.addRoute (d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue(), index);
            repaint();
        }

        juce::Slider slider;

    private:
        Host& host;
        int index;
        juce::Colour accent;
        juce::String label;
        bool updating = false, dropping = false;
    };

    /** A menu bound to a choice parameter. */
    class Choice final : public juce::ComboBox
    {
    public:
        Choice (Host& h, int paramIndex) : host (h), index (paramIndex)
        {
            const auto& p = params()[(size_t) index];
            for (size_t i = 0; i < p.choices.size(); ++i)
                addItem (p.choices[i], (int) i + 1);
            setTooltip (p.name);
            onChange = [this]
            {
                if (updating || getSelectedId() <= 0) return;
                host.beginGesture (params()[(size_t) index].name);
                host.set (index, (float) (getSelectedId() - 1));
            };
            refresh();
        }
        void refresh()
        {
            const juce::ScopedValueSetter<bool> busy (updating, true);
            setSelectedId (juce::roundToInt (host.get (index)) + 1, juce::dontSendNotification);
        }
        int getIndex() const noexcept { return index; }
    private:
        Host& host;
        int index;
        bool updating = false;
    };

    /** An on/off button bound to a two-way choice. */
    class Toggle final : public juce::TextButton
    {
    public:
        Toggle (Host& h, int paramIndex, const juce::String& text, juce::Colour accent) : juce::TextButton (text), host (h), index (paramIndex)
        {
            setClickingTogglesState (true);
            getProperties().set ("accent", accent.toString());
            setTooltip (params()[(size_t) index].name);
            onClick = [this]
            {
                if (updating) return;
                host.beginGesture (params()[(size_t) index].name);
                host.set (index, getToggleState() ? 1.0f : 0.0f);
            };
            refresh();
        }
        void refresh()
        {
            const juce::ScopedValueSetter<bool> busy (updating, true);
            setToggleState (host.get (index) > 0.5f, juce::dontSendNotification);
        }
        int getIndex() const noexcept { return index; }
    private:
        Host& host;
        int index;
        bool updating = false;
    };

    /** A tile you drag onto a knob to modulate it with that source (and click to show it, for envelopes and LFOs). */
    class SourceTile final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SourceTile (Host& h, int sourceIndex, juce::String text) : host (h), source (sourceIndex), label (std::move (text))
        {
            setTooltip ("Drag onto any knob to modulate it with " + modSources()[(size_t) source]);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
        std::function<void()> onClick;
        bool selected = false;

        void paint (juce::Graphics& g) override
        {
            const auto c = sourceColour (source);
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (selected ? c.withAlpha (0.9f) : col::well.withAlpha (0.9f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (c.withAlpha (selected ? 1.0f : 0.7f));
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            // the grip: three dots
            g.setColour (selected ? juce::Colours::black : c);
            for (int k = 0; k < 3; ++k)
                g.fillEllipse (r.getX() + 6.0f, r.getCentreY() - 5.0f + (float) k * 4.0f, 2.5f, 2.5f);
            g.setFont (font (label.length() > 4 ? 10.0f : 11.5f, true));
            g.drawText (label, r.withTrimmedLeft (10.0f), juce::Justification::centred, false);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging && e.getDistanceFromDragStart() > 5)
            {
                dragging = true;
                host.startSourceDrag (source, this);
            }
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! dragging && onClick) onClick();
            dragging = false;
        }
    private:
        Host& host;
        int source;
        juce::String label;
        bool dragging = false;
    };

    //==============================================================================
    /** A page: its panels (dark glass with a neon title) drawn behind its controls. */
    class Page final : public juce::Component
    {
    public:
        struct Box { R area; juce::String title; juce::Colour colour; };
        std::vector<Box> boxes;
        void paint (juce::Graphics& g) override
        {
            for (const auto& b : boxes)
            {
                const auto r = b.area.toFloat();
                g.setColour (col::panel);
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (b.colour.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.3f);
                if (b.title.isNotEmpty())
                {
                    g.setColour (b.colour);
                    g.setFont (font (13.0f, true));
                    g.drawText (b.title, r.reduced (12.0f, 6.0f).withHeight (20.0f), juce::Justification::centredLeft, false);
                }
            }
        }
    };

    /** The wavetable: the frames as a stack going back, the one playing lit. Drag up and down to move through it. */
    class WaveView final : public juce::Component
    {
    public:
        WaveView (Host& h, int oscIndex, juce::Colour c) : host (h), osc (oscIndex), colour (c)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }
        void paint (juce::Graphics& g) override
        {
            const auto& p = ix().osc[osc];
            const auto r = getLocalBounds().toFloat();
            g.setColour (col::well);
            g.fillRoundedRectangle (r, 6.0f);
            const auto table = juce::roundToInt (host.get (p.table));
            const auto pos = juce::jlimit (0.0f, 1.0f, host.get (p.pos) + host.liveMod (p.pos));
            const auto on = host.get (p.on) > 0.5f;
            std::array<float, wt::frameSize> frame {};
            const auto draw = [&] (float position, juce::Colour c, float thickness, float dx, float dy, float scale, bool fill)
            {
                wt::frameForDisplay (table, position, frame.data());
                const auto area = r.reduced (10.0f, 12.0f);
                juce::Path line;
                for (int i = 0; i <= 128; ++i)
                {
                    const auto v = frame[(size_t) std::min (wt::frameSize - 1, i * wt::frameSize / 128)];
                    const auto x = area.getX() + dx + (float) i / 128.0f * area.getWidth() * scale;
                    const auto y = area.getCentreY() + dy - v * area.getHeight() * 0.42f * scale;
                    if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
                }
                if (fill)
                {
                    auto under = line;
                    under.lineTo (area.getX() + dx + area.getWidth() * scale, area.getCentreY() + dy);
                    under.lineTo (area.getX() + dx, area.getCentreY() + dy);
                    under.closeSubPath();
                    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), 0.0f, area.getY(), c.withAlpha (0.02f), 0.0f, area.getBottom(), false));
                    g.fillPath (under);
                }
                g.setColour (c);
                g.strokePath (line, juce::PathStrokeType (thickness));
            };
            // the frames behind, going back up to the right
            for (int k = 7; k >= 1; --k)
            {
                const auto position = (float) k / 7.0f;
                const auto t = (float) k / 7.0f;
                draw (position, colour.withAlpha (on ? 0.12f : 0.05f), 1.0f, t * 26.0f, -t * 14.0f, 0.82f, false);
            }
            draw (pos, on ? colour : colour.withAlpha (0.3f), 2.0f, 0.0f, 4.0f, 0.82f, true);
            g.setColour (col::dim);
            g.setFont (font (10.5f, true));
            g.drawText (tableNames()[(size_t) juce::jlimit (0, (int) tableNames().size() - 1, table)].toUpperCase() + "   " + juce::String (juce::roundToInt (pos * 31.0f) + 1) + "/32",
                        r.reduced (8.0f, 4.0f), juce::Justification::bottomRight, false);
        }
        void mouseDown (const juce::MouseEvent&) override
        {
            startPos = host.get (ix().osc[osc].pos);
            host.beginGesture ("Wavetable position");
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            host.set (ix().osc[osc].pos, juce::jlimit (0.0f, 1.0f, startPos - (float) e.getDistanceFromDragStartY() / 200.0f));
            repaint();
        }
    private:
        Host& host;
        int osc;
        juce::Colour colour;
        float startPos = 0.0f;
    };

    /** A filter's response. */
    class FilterView final : public juce::Component
    {
    public:
        FilterView (Host& h, int filterIndex, juce::Colour c) : host (h), filter (filterIndex), colour (c) {}
        void paint (juce::Graphics& g) override
        {
            const auto& p = ix().filter[filter];
            const auto r = getLocalBounds().toFloat();
            g.setColour (col::well);
            g.fillRoundedRectangle (r, 6.0f);
            const auto area = r.reduced (6.0f, 8.0f);
            const auto on = host.get (p.on) > 0.5f;
            const auto type = juce::roundToInt (host.get (p.type));
            const auto cutoff = fromNormal (p.cutoff, toNormal (p.cutoff, host.get (p.cutoff)) + host.liveMod (p.cutoff));
            const auto res = host.get (p.res);
            const auto q = 0.5 / std::max (0.03, 1.0 - 0.97 * (double) res);
            juce::Path curve;
            for (int x = 0; x <= (int) area.getWidth(); x += 2)
            {
                const auto f = 20.0 * std::pow (1000.0, x / (double) area.getWidth());
                const std::complex<double> s (0.0, f / std::max (1.0f, cutoff));
                const auto den = s * s + s / q + 1.0;
                std::complex<double> h;
                switch (type)
                {
                    case 0: h = 1.0 / den; break;
                    case 1: h = 1.0 / (den * den) * (1.0 + 0.0 * s); break;
                    case 2: { const auto one = 1.0 + s; h = 1.0 / (one * one * one * one + 4.0 * (double) res * 0.95); break; }
                    case 3: h = s * s / den; break;
                    case 4: h = (s * s / den) * (s * s / den); break;
                    case 5: h = (s / q) / den; break;
                    case 6: h = ((s / q) / den) * ((s / q) / den) * q; break;
                    case 7: h = (s * s + 1.0) / den; break;
                    case 8: h = (1.0 - s * s) / den; break;
                    case 9: case 10:
                    {
                        const auto w = juce::MathConstants<double>::twoPi * f / std::max (20.0f, cutoff);
                        const auto fb = (type == 9 ? 1.0 : -1.0) * (0.2 + 0.77 * res);
                        h = 1.0 / (1.0 - fb * std::polar (1.0, -w));
                        h *= 0.6;
                        break;
                    }
                    default:
                    {
                        // the formant filter: three peaks for the vowel the cutoff picks
                        static const double v[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                        const auto t = juce::jlimit (0.0, 1.0, (double) toNormal (p.cutoff, cutoff)) * 4.0;
                        const auto i = std::min (3, (int) t);
                        const auto k = t - i;
                        const double w[3] { 1.0, 0.7, 0.4 };
                        h = 0.0;
                        for (int b = 0; b < 3; ++b)
                        {
                            const auto fc = v[i][b] + k * (v[i + 1][b] - v[i][b]);
                            const std::complex<double> sb (0.0, f / fc);
                            const auto qb = 1.0 / (0.5 - 0.42 * res) * 0.5;
                            h += w[b] * (sb / qb) / (sb * sb + sb / qb + 1.0);
                        }
                        h *= 1.6;
                        break;
                    }
                }
                const auto db = juce::jlimit (-36.0, 24.0, 20.0 * std::log10 (std::max (1.0e-6, std::abs (h))));
                const auto y = area.getY() + (float) ((24.0 - db) / 60.0) * area.getHeight();
                if (x == 0) curve.startNewSubPath (area.getX(), y); else curve.lineTo (area.getX() + (float) x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.4f, area.getWidth(), 1.0f);
            auto fill = curve;
            fill.lineTo (area.getRight(), area.getBottom());
            fill.lineTo (area.getX(), area.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (on ? 0.3f : 0.08f), 0.0f, area.getY(), colour.withAlpha (0.0f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            g.setColour (colour.withAlpha (on ? 1.0f : 0.3f));
            g.strokePath (curve, juce::PathStrokeType (2.0f));
        }
    private:
        Host& host;
        int filter;
        juce::Colour colour;
    };

    /** An envelope's shape (and where the newest voice is on it). */
    class EnvView final : public juce::Component
    {
    public:
        EnvView (Host& h, std::function<float()> nowFn) : host (h), now (std::move (nowFn)) {}
        int env = 0;
        void paint (juce::Graphics& g) override
        {
            const auto& p = ix().env[env];
            const auto r = getLocalBounds().toFloat();
            g.setColour (col::well);
            g.fillRoundedRectangle (r, 6.0f);
            const auto area = r.reduced (10.0f, 10.0f);
            const auto scale = [] (float seconds) { return std::log1p (seconds * 20.0f) / std::log1p (20.0f * 4.0f); };
            const auto a = scale (host.get (p.attack)), hd = scale (host.get (p.hold)), d = scale (host.get (p.decay)), s = host.get (p.sustain), rel = scale (host.get (p.release));
            const auto total = std::max (0.6f, a + hd + d + 0.35f + rel);
            const auto xOf = [&] (float t) { return area.getX() + t / total * area.getWidth(); };
            const auto yOf = [&] (float level) { return area.getBottom() - level * area.getHeight(); };
            const auto curve = 1.0f + 4.0f * host.get (p.curve);
            juce::Path path;
            path.startNewSubPath (xOf (0.0f), yOf (0.0f));
            for (int k = 1; k <= 20; ++k) { const auto t = (float) k / 20.0f; path.lineTo (xOf (a * t), yOf (1.0f - std::pow (1.0f - t, 1.0f + 0.5f * curve))); }
            path.lineTo (xOf (a + hd), yOf (1.0f));
            for (int k = 1; k <= 30; ++k) { const auto t = (float) k / 30.0f; path.lineTo (xOf (a + hd + d * t), yOf (s + (1.0f - s) * std::pow (1.0f - t, curve))); }
            path.lineTo (xOf (a + hd + d + 0.35f), yOf (s));
            for (int k = 1; k <= 30; ++k) { const auto t = (float) k / 30.0f; path.lineTo (xOf (a + hd + d + 0.35f + rel * t), yOf (s * std::pow (1.0f - t, curve))); }
            const auto c = env == 0 ? col::orange : col::green;
            auto fill = path;
            fill.lineTo (xOf (total), yOf (0.0f));
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.3f), 0.0f, area.getY(), c.withAlpha (0.0f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (2.0f));
            const auto level = now();
            g.setColour (juce::Colours::white);
            g.fillRect (RF (area.getRight() + 2.0f, yOf (level) - 1.5f, 5.0f, 3.0f));
        }
    private:
        Host& host;
        std::function<float()> now;
    };

    /** An LFO's shape over a cycle, and its value now. */
    class LfoView final : public juce::Component
    {
    public:
        LfoView (Host& h, std::function<float()> nowFn) : host (h), now (std::move (nowFn)) {}
        int lfo = 0;
        void paint (juce::Graphics& g) override
        {
            const auto& p = ix().lfo[lfo];
            const auto r = getLocalBounds().toFloat();
            g.setColour (col::well);
            g.fillRoundedRectangle (r, 6.0f);
            const auto area = r.reduced (10.0f, 10.0f).withTrimmedRight (10.0f);
            const auto shape = juce::roundToInt (host.get (p.shape));
            juce::Random rnd (7);
            float held = rnd.nextFloat() * 2.0f - 1.0f, next = rnd.nextFloat() * 2.0f - 1.0f;
            juce::Path path;
            const auto unipolar = shape == 7 || shape == 8;
            const auto yOf = [&] (float v) { return unipolar ? area.getBottom() - v * area.getHeight() : area.getCentreY() - v * area.getHeight() * 0.5f; };
            for (int i = 0; i <= 200; ++i)
            {
                const auto t = std::fmod ((double) i / 100.0 + host.get (p.phase), 1.0);
                if (i == 100) { held = next; next = rnd.nextFloat() * 2.0f - 1.0f; }
                float v;
                const auto x = (float) t;
                switch (shape)
                {
                    case 0: v = std::sin (juce::MathConstants<float>::twoPi * x); break;
                    case 1: v = x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f; break;
                    case 2: v = 2.0f * x - 1.0f; break;
                    case 3: v = 1.0f - 2.0f * x; break;
                    case 4: v = x < 0.5f ? 1.0f : -1.0f; break;
                    case 5: v = held; break;
                    case 6: { const auto s = x * x * (3.0f - 2.0f * x); v = held + s * (next - held); break; }
                    case 7: v = std::exp (-x * 6.0f); break;
                    case 8: v = x; break;
                    default: v = std::floor (x * 4.0f) / 3.0f * 2.0f - 1.0f; break;
                }
                const auto px = area.getX() + (float) i / 200.0f * area.getWidth();
                if (i == 0) path.startNewSubPath (px, yOf (v)); else path.lineTo (px, yOf (v));
            }
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);
            g.setColour (col::blue);
            g.strokePath (path, juce::PathStrokeType (2.0f));
            g.setColour (juce::Colours::white);
            g.fillRect (RF (area.getRight() + 3.0f, yOf (now()) - 1.5f, 5.0f, 3.0f));
        }
    private:
        Host& host;
        std::function<float()> now;
    };
}

//==============================================================================
struct NemixPanel::Impl final : public Host,
                                private juce::ValueTree::Listener,
                                private juce::Timer,
                                private juce::ListBoxModel
{
    NemixPanel& owner;
    juce::ValueTree track, state;
    juce::UndoManager& um;
    std::function<std::shared_ptr<Nemix>()> live;
    Look look;
    juce::Image logo, wall;

    juce::OwnedArray<juce::Component> owned;
    std::vector<Knob*> knobs;
    std::vector<Choice*> choices;
    std::vector<Toggle*> toggles;
    std::vector<juce::Component*> animated;      // repainted while it plays
    std::array<Page, 5> pages;
    juce::OwnedArray<juce::TextButton> tabs;
    int page = 0;

    // header
    juce::Label presetLabel, infoLabel;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, initButton { "INIT" };
    Knob* masterKnob = nullptr;

    // envelopes and LFOs: which one shows
    std::array<SourceTile*, 4> envTiles {}, lfoTiles {};
    std::array<std::vector<juce::Component*>, 4> envControls, lfoControls;
    EnvView* envView = nullptr;
    LfoView* lfoView = nullptr;

    // matrix rows
    struct Row { juce::ComboBox source, dest; juce::Slider amount; juce::TextButton remove { "X" }; };
    juce::OwnedArray<Row> rows;
    juce::Viewport matrixView;
    juce::Component matrixInner;

    // browser
    juce::ListBox list { "Presets", this };
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> categoryButtons, genreButtons;
    juce::String category, genre;
    std::vector<Preset> shown;        // what the browser lists
    std::vector<Preset> all;          // factory then yours
    bool updatingRows = false;

    Impl (NemixPanel& o, juce::ValueTree t, juce::UndoManager& u, std::function<std::shared_ptr<Nemix>()> l)
        : owner (o), track (t), um (u), live (std::move (l))
    {
        state = nemixstate::getOrCreate (track, nullptr);
        state.addListener (this);
        logo = juce::ImageCache::getFromMemory (BinaryData::logo_jpg, BinaryData::logo_jpgSize);
        wall = juce::ImageCache::getFromMemory (BinaryData::wall_jpg, BinaryData::wall_jpgSize);
        owner.setLookAndFeel (&look);
        reloadPresets();
        buildHeader();
        buildSoundPage();
        buildMatrixPage();
        buildFxPage();
        buildArpPage();
        buildBrowserPage();
        for (auto& p : pages) owner.addChildComponent (p);
        setPage (0);
        refreshHeader();
        startTimerHz (30);
    }

    ~Impl() override
    {
        state.removeListener (this);
        owner.setLookAndFeel (nullptr);
    }

    //==========================================================================
    // Host
    float get (int index) const override { return nemixstate::valueOf (state, index); }
    void set (int index, float value) override { nemixstate::set (state, index, value, &um); }
    void beginGesture (const juce::String& what) override { um.beginNewTransaction ("Nemix: " + what); }

    std::vector<int> routesTo (int param) const override
    {
        std::vector<int> r;
        const auto& dests = destinations();
        for (int s = 0; s < numSlots; ++s)
        {
            if (juce::roundToInt (get (ix().slot[s].src)) <= srcNone) continue;
            const auto d = juce::roundToInt (get (ix().slot[s].dst));
            if (d >= 0 && d < (int) dests.size() && dests[(size_t) d] == param)
                r.push_back (s);
        }
        return r;
    }

    void addRoute (int source, int param) override
    {
        const auto& dests = destinations();
        const auto at = (int) (std::find (dests.begin(), dests.end(), param) - dests.begin());
        if (at >= (int) dests.size()) return;
        // (the same source to the same knob again: nothing new)
        for (const auto s : routesTo (param))
            if (juce::roundToInt (get (ix().slot[s].src)) == source) return;
        for (int s = 0; s < numSlots; ++s)
            if (juce::roundToInt (get (ix().slot[s].src)) <= srcNone)
            {
                beginGesture ("Add modulation");
                set (ix().slot[s].src, (float) source);
                set (ix().slot[s].dst, (float) at);
                set (ix().slot[s].amt, 0.35f);
                return;
            }
    }

    float liveMod (int param) const override
    {
        if (auto synth = live ? live() : nullptr)
            return synth->modulationNow (param);
        return 0.0f;
    }

    void startSourceDrag (int source, juce::Component* from) override
    {
        juce::Image image (juce::Image::ARGB, 90, 26, true);
        {
            juce::Graphics g (image);
            g.setColour (sourceColour (source).withAlpha (0.9f));
            g.fillRoundedRectangle (0.0f, 0.0f, 90.0f, 26.0f, 5.0f);
            g.setColour (juce::Colours::black);
            g.setFont (font (12.0f, true));
            g.drawText (modSources()[(size_t) source], 0, 0, 90, 26, juce::Justification::centred);
        }
        owner.startDragging ("nmxsrc:" + juce::String (source), from, juce::ScaledImage (image));
    }

    //==========================================================================
    template <typename T, typename... Args> T* make (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        return c;
    }

    Knob* knob (Page& pg, const juce::String& id, juce::Colour c, R where, const juce::String& label = {})
    {
        auto* k = make<Knob> (*this, indexOf (id), c, label);
        k->setBounds (where);
        pg.addAndMakeVisible (k);
        knobs.push_back (k);
        return k;
    }
    Choice* choice (Page& pg, const juce::String& id, R where)
    {
        auto* c = make<Choice> (*this, indexOf (id));
        c->setBounds (where);
        pg.addAndMakeVisible (c);
        choices.push_back (c);
        return c;
    }
    Toggle* toggle (Page& pg, const juce::String& id, const juce::String& text, juce::Colour c, R where)
    {
        auto* t = make<Toggle> (*this, indexOf (id), text, c);
        t->setBounds (where);
        pg.addAndMakeVisible (t);
        toggles.push_back (t);
        return t;
    }
    void label (Page& pg, const juce::String& text, R where, juce::Colour c = col::dim)
    {
        auto* l = make<juce::Label> (text, text);
        l->setFont (font (11.0f, true));
        l->setColour (juce::Label::textColourId, c);
        l->setJustificationType (juce::Justification::centredLeft);
        l->setBounds (where);
        l->setInterceptsMouseClicks (false, false);
        pg.addAndMakeVisible (l);
    }

    //==========================================================================
    static constexpr int width = 1200, height = 920, headerH = 92;

    void buildHeader()
    {
        const char* names[] { "SOUND", "MATRIX", "FX", "ARP + VOICE", "BROWSE" };
        for (int i = 0; i < 5; ++i)
        {
            auto* t = tabs.add (new juce::TextButton (names[i]));
            t->setClickingTogglesState (true);
            t->setRadioGroupId (9001);
            t->getProperties().set ("accent", (i == 4 ? col::orange : col::teal).toString());
            t->onClick = [this, i] { setPage (i); };
            owner.addAndMakeVisible (t);
        }
        presetLabel.setFont (font (24.0f, true));
        presetLabel.setColour (juce::Label::textColourId, col::text);
        presetLabel.setInterceptsMouseClicks (false, false);
        infoLabel.setFont (font (12.0f, true));
        infoLabel.setColour (juce::Label::textColourId, col::dim);
        infoLabel.setInterceptsMouseClicks (false, false);
        owner.addAndMakeVisible (presetLabel);
        owner.addAndMakeVisible (infoLabel);
        for (auto* b : { &prevButton, &nextButton, &saveButton, &initButton })
        {
            b->getProperties().set ("accent", col::purple.toString());
            owner.addAndMakeVisible (b);
        }
        prevButton.setTooltip ("The sound before (in the browser's list)");
        nextButton.setTooltip ("The next sound");
        saveButton.setTooltip ("Save this sound as your own preset");
        initButton.setTooltip ("Start from a plain saw");
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (1); };
        initButton.onClick = [this] { loadSound ({}, "Init", "", {}); };
        saveButton.onClick = [this] { saveDialog(); };
        masterKnob = make<Knob> (*this, ix().master, col::orange, "Master");
        owner.addAndMakeVisible (masterKnob);
        knobs.push_back (masterKnob);
    }

    void layoutHeader()
    {
        int x = 104;
        presetLabel.setBounds (x + 70, 14, 330, 32);
        infoLabel.setBounds (x + 70, 46, 330, 18);
        prevButton.setBounds (x, 18, 30, 28);
        nextButton.setBounds (x + 34, 18, 30, 28);
        initButton.setBounds (x, 52, 64, 22);
        saveButton.setBounds (510, 18, 66, 28);
        int tx = 592;
        for (int i = 0; i < tabs.size(); ++i)
        {
            const auto w = i == 3 ? 110 : 80;
            tabs[i]->setBounds (tx, 20, w, 30);
            tx += w + 6;
        }
        masterKnob->setBounds (width - 74, 6, 66, 80);
    }

    void refreshHeader()
    {
        const auto name = state[nemixstate::preset].toString();
        presetLabel.setText (name.isNotEmpty() ? name : juce::String ("Init"), juce::dontSendNotification);
        for (const auto& p : all)
            if (p.name == name)
            {
                infoLabel.setText (p.category.toUpperCase() + "   /   " + p.genres.joinIntoString (", ").toUpperCase(), juce::dontSendNotification);
                return;
            }
        infoLabel.setText ("NEMIX WAVETABLE SYNTH", juce::dontSendNotification);
    }

    //==========================================================================
    void buildSoundPage()
    {
        auto& pg = pages[0];
        const juce::Colour oscColours[] { col::teal, col::purple, col::orange };
        const char* oscIds[] { "a", "b", "c" };
        const char* oscTitles[] { "OSC A", "OSC B", "OSC C" };
        // Three oscillators across the top
        for (int o = 0; o < 3; ++o)
        {
            const juce::String p (oscIds[o]);
            const auto c = oscColours[o];
            const auto x = 10 + o * 300;
            pg.boxes.push_back ({ R (x, 8, 292, 352), oscTitles[o], c });
            toggle (pg, p + "On", "ON", c, R (x + 80, 12, 40, 20));
            choice (pg, p + "Table", R (x + 128, 11, 154, 22));
            auto* view = make<WaveView> (*this, o, c);
            view->setBounds (x + 10, 40, 272, 108);
            pg.addAndMakeVisible (view);
            animated.push_back (view);
            const char* row1[] { "Pos", "Unison", "Detune", "Blend", "Width" };
            const char* row2[] { "Oct", "Semi", "Fine", "Level", "Pan" };
            for (int k = 0; k < 5; ++k)
            {
                knob (pg, p + row1[k], c, R (x + 6 + k * 56, 154, 56, 66), juce::String (row1[k]) == "Pos" ? "WT Pos" : juce::String (row1[k]));
                knob (pg, p + row2[k], c, R (x + 6 + k * 56, 222, 56, 66));
            }
            label (pg, "WARP", R (x + 12, 294, 60, 14));
            choice (pg, p + "Warp", R (x + 10, 310, 96, 22));
            knob (pg, p + "WarpAmt", c, R (x + 108, 290, 56, 66), "Amount");
            label (pg, "OUT", R (x + 172, 294, 60, 14));
            choice (pg, p + "Route", R (x + 170, 310, 112, 22));
        }
        // Sub and noise
        {
            const auto x = 910;
            pg.boxes.push_back ({ R (x, 8, 280, 170), "SUB", col::red });
            toggle (pg, "subOn", "ON", col::red, R (x + 60, 12, 40, 20));
            choice (pg, "subShape", R (x + 108, 11, 90, 22));
            choice (pg, "subOct", R (x + 204, 11, 64, 22));
            knob (pg, "subLevel", col::red, R (x + 10, 44, 64, 74), "Level");
            knob (pg, "subPan", col::red, R (x + 78, 44, 64, 74), "Pan");
            label (pg, "DIRECT OUT", R (x + 152, 54, 110, 14));
            toggle (pg, "subDirect", "NO FILTER", col::red, R (x + 150, 72, 112, 24));
            pg.boxes.push_back ({ R (x, 186, 280, 174), "NOISE", col::pink });
            toggle (pg, "nzOn", "ON", col::pink, R (x + 70, 190, 40, 20));
            choice (pg, "nzType", R (x + 118, 189, 150, 22));
            knob (pg, "nzLevel", col::pink, R (x + 10, 222, 64, 74), "Level");
            knob (pg, "nzColour", col::pink, R (x + 78, 222, 64, 74), "Colour");
            knob (pg, "nzPan", col::pink, R (x + 146, 222, 64, 74), "Pan");
            choice (pg, "nzRoute", R (x + 14, 316, 140, 22));
        }
        // Two filters and the macros
        for (int f = 0; f < 2; ++f)
        {
            const juce::String p ("f" + juce::String (f + 1));
            const auto c = f == 0 ? col::teal : col::purple;
            const auto x = 10 + f * 390;
            pg.boxes.push_back ({ R (x, 368, 382, 198), "FILTER " + juce::String (f + 1), c });
            toggle (pg, p + "On", "ON", c, R (x + 92, 372, 40, 20));
            choice (pg, p + "Type", R (x + 140, 371, 130, 22));
            auto* view = make<FilterView> (*this, f, c);
            view->setBounds (x + 10, 400, 140, 156);
            pg.addAndMakeVisible (view);
            animated.push_back (view);
            const char* ids[] { "Cutoff", "Res", "Drive", "Key", "Mix", "Pan" };
            for (int k = 0; k < 6; ++k)
                knob (pg, p + ids[k], c, R (x + 156 + (k % 3) * 74, 398 + (k / 3) * 82, 70, 80));
        }
        choice (pg, "filterSerial", R (280, 371, 96, 22))->setTooltip ("Filter 1 into Filter 2, or side by side");
        {
            const auto x = 790;
            pg.boxes.push_back ({ R (x, 368, 400, 198), "MACROS", col::yellow });
            label (pg, "drag a tile onto any knob", R (x + 92, 372, 200, 18));
            for (int m = 0; m < 4; ++m)
            {
                knob (pg, "macro" + juce::String (m + 1), col::yellow, R (x + 14 + m * 96, 398, 84, 104), "Macro " + juce::String (m + 1));
                auto* tile = make<SourceTile> (*this, srcMacro1 + m, "M" + juce::String (m + 1));
                tile->setBounds (x + 26 + m * 96, 520, 60, 26);
                pg.addAndMakeVisible (tile);
            }
        }
        // Envelopes and LFOs
        pg.boxes.push_back ({ R (10, 574, 590, 236), {}, col::orange });
        envView = make<EnvView> (*this, [this] { auto s = live ? live() : nullptr; return s != nullptr ? s->envNow (envView->env) : 0.0f; });
        envView->setBounds (20, 612, 240, 186);
        pg.addAndMakeVisible (envView);
        animated.push_back (envView);
        for (int e = 0; e < 4; ++e)
        {
            auto* tile = make<SourceTile> (*this, e == 0 ? srcEnv1 : srcEnv2 + e - 1, "ENV " + juce::String (e + 1));
            tile->setBounds (20 + e * 82, 582, 76, 24);
            tile->onClick = [this, e] { showEnv (e); };
            pg.addAndMakeVisible (tile);
            envTiles[(size_t) e] = tile;
            const juce::String p ("e" + juce::String (e + 1));
            const auto c = e == 0 ? col::orange : col::green;
            const char* ids[] { "Attack", "Hold", "Decay", "Sustain", "Release", "Curve" };
            for (int k = 0; k < 6; ++k)
                envControls[(size_t) e].push_back (knob (pg, p + ids[k], c, R (272 + (k % 3) * 106, 616 + (k / 3) * 94, 96, 90)));
        }
        pg.boxes.push_back ({ R (608, 574, 582, 236), {}, col::blue });
        lfoView = make<LfoView> (*this, [this] { auto s = live ? live() : nullptr; return s != nullptr ? s->lfoNow (lfoView->lfo) : 0.0f; });
        lfoView->setBounds (618, 612, 230, 186);
        pg.addAndMakeVisible (lfoView);
        animated.push_back (lfoView);
        for (int l = 0; l < 4; ++l)
        {
            auto* tile = make<SourceTile> (*this, srcLfo1 + l, "LFO " + juce::String (l + 1));
            tile->setBounds (618 + l * 82, 582, 76, 24);
            tile->onClick = [this, l] { showLfo (l); };
            pg.addAndMakeVisible (tile);
            lfoTiles[(size_t) l] = tile;
            const juce::String p ("l" + juce::String (l + 1));
            auto& cs = lfoControls[(size_t) l];
            cs.push_back (choice (pg, p + "Shape", R (860, 618, 150, 24)));
            cs.push_back (choice (pg, p + "Sync", R (1020, 618, 76, 24)));
            cs.push_back (choice (pg, p + "Mode", R (1104, 618, 76, 24)));
            const char* ids[] { "Rate", "Rise", "Phase", "Smooth" };
            for (int k = 0; k < 4; ++k)
                cs.push_back (knob (pg, p + ids[k], col::blue, R (858 + k * 82, 664, 80, 92)));
        }
        // The other sources, to drag
        const std::pair<int, const char*> others[] { { srcVelocity, "VELO" }, { srcNote, "NOTE" }, { srcModWheel, "WHEEL" }, { srcAftertouch, "AFTER" },
                                                      { srcPitchBend, "BEND" }, { srcRandom, "RAND" } };
        for (size_t k = 0; k < std::size (others); ++k)
        {
            auto* tile = make<SourceTile> (*this, others[k].first, others[k].second);
            tile->setBounds (858 + (int) (k % 6) * 54, 770, 52, 24);
            pg.addAndMakeVisible (tile);
        }
        showEnv (0);
        showLfo (0);
    }

    void showEnv (int e)
    {
        for (int k = 0; k < 4; ++k)
        {
            envTiles[(size_t) k]->selected = k == e;
            envTiles[(size_t) k]->repaint();
            for (auto* c : envControls[(size_t) k]) c->setVisible (k == e);
        }
        envView->env = e;
        envView->repaint();
    }

    void showLfo (int l)
    {
        for (int k = 0; k < 4; ++k)
        {
            lfoTiles[(size_t) k]->selected = k == l;
            lfoTiles[(size_t) k]->repaint();
            for (auto* c : lfoControls[(size_t) k]) c->setVisible (k == l);
        }
        lfoView->lfo = l;
        lfoView->repaint();
    }

    //==========================================================================
    void buildMatrixPage()
    {
        auto& pg = pages[1];
        pg.boxes.push_back ({ R (10, 8, width - 20, height - headerH - 18), "MODULATION MATRIX", col::blue });
        label (pg, "SOURCE", R (40, 36, 200, 16));
        label (pg, "DESTINATION", R (270, 36, 300, 16));
        label (pg, "AMOUNT", R (620, 36, 300, 16));
        matrixView.setViewedComponent (&matrixInner, false);
        matrixView.setScrollBarsShown (true, false);
        matrixView.setBounds (24, 56, width - 48, height - headerH - 80);
        pg.addAndMakeVisible (matrixView);
        matrixInner.setSize (width - 64, numSlots * 34);
        const auto& dests = destinations();
        for (int s = 0; s < numSlots; ++s)
        {
            auto* row = rows.add (new Row());
            const auto y = s * 34;
            for (size_t i = 0; i < modSources().size(); ++i)
                row->source.addItem (modSources()[i], (int) i + 1);
            // destinations grouped by section
            auto* rootMenu = row->dest.getRootMenu();
            juce::String group;
            juce::PopupMenu sub;
            const auto flush = [&]
            {
                if (group.isNotEmpty()) rootMenu->addSubMenu (group, sub);
                sub = juce::PopupMenu();
            };
            for (size_t d = 0; d < dests.size(); ++d)
            {
                const auto& p = params()[(size_t) dests[d]];
                const auto g = p.name.containsChar (' ') && p.name != "Bend Range" ? p.name.upToLastOccurrenceOf (" ", false, false) : p.name;
                auto groupName = p.name.startsWith ("Osc ") ? p.name.substring (0, 5) : p.name.startsWith ("Filter ") ? p.name.substring (0, 8)
                               : p.name.startsWith ("Env ") ? p.name.substring (0, 5) : p.name.startsWith ("LFO ") ? p.name.substring (0, 5)
                               : p.name.startsWith ("Macro") ? juce::String ("Macros") : p.name.startsWith ("Sub") || p.name.startsWith ("Noise") ? juce::String ("Sub + Noise")
                               : (p.name.startsWith ("Distortion") || p.name.startsWith ("Multiband") || p.name.startsWith ("Chorus") || p.name.startsWith ("Phaser")
                                  || p.name.startsWith ("Delay") || p.name.startsWith ("Reverb") || p.name.startsWith ("EQ") || p.name.startsWith ("FX Filter")) ? juce::String ("Effects")
                               : p.name.startsWith ("Arp") ? juce::String ("Arp") : juce::String ("Global");
                juce::ignoreUnused (g);
                if (groupName != group) { flush(); group = groupName; }
                sub.addItem ((int) d + 1, p.name);
            }
            flush();
            row->amount.textFromValueFunction = [] (double v) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + " %"; };
            row->amount.setSliderStyle (juce::Slider::LinearBar);
            row->amount.setRange (-1.0, 1.0, 0.01);
            row->amount.setColour (juce::Slider::trackColourId, col::blue.withAlpha (0.6f));
            row->amount.setColour (juce::Slider::backgroundColourId, col::well);
            row->amount.setDoubleClickReturnValue (true, 0.0);
            row->remove.getProperties().set ("accent", col::red.toString());
            row->source.setBounds (10, y + 4, 220, 26);
            row->dest.setBounds (246, y + 4, 340, 26);
            row->amount.setBounds (604, y + 4, 420, 26);
            row->remove.setBounds (1040, y + 4, 30, 26);
            row->source.onChange = [this, s, row] { if (updatingRows) return; beginGesture ("Modulation source"); set (ix().slot[s].src, (float) (row->source.getSelectedId() - 1));
                                                     if (get (ix().slot[s].amt) == 0.0f) set (ix().slot[s].amt, 0.35f); };
            row->dest.onChange = [this, s, row] { if (updatingRows) return; beginGesture ("Modulation destination"); set (ix().slot[s].dst, (float) (row->dest.getSelectedId() - 1)); };
            row->amount.onDragStart = [this] { beginGesture ("Modulation amount"); };
            row->amount.onValueChange = [this, s, row] { if (! updatingRows) set (ix().slot[s].amt, (float) row->amount.getValue()); };
            row->remove.onClick = [this, s] { beginGesture ("Remove modulation"); set (ix().slot[s].src, 0.0f); set (ix().slot[s].amt, 0.0f); };
            for (auto* c : std::initializer_list<juce::Component*> { &row->source, &row->dest, &row->amount, &row->remove })
                matrixInner.addAndMakeVisible (c);
        }
        refreshRows();
    }

    void refreshRows()
    {
        const juce::ScopedValueSetter<bool> busy (updatingRows, true);
        for (int s = 0; s < rows.size(); ++s)
        {
            auto* row = rows[s];
            const auto src = juce::roundToInt (get (ix().slot[s].src));
            row->source.setSelectedId (src + 1, juce::dontSendNotification);
            if (src > 0) row->dest.setSelectedId (juce::roundToInt (get (ix().slot[s].dst)) + 1, juce::dontSendNotification);
            else row->dest.setSelectedId (0, juce::dontSendNotification);
            row->amount.setValue (get (ix().slot[s].amt), juce::dontSendNotification);
            row->amount.updateText();
            row->amount.setColour (juce::Slider::trackColourId, sourceColour (src).withAlpha (src > 0 ? 0.7f : 0.15f));
        }
    }

    //==========================================================================
    void buildFxPage()
    {
        auto& pg = pages[2];
        struct Unit { const char* title; const char* on; juce::Colour c; std::vector<const char*> knobs; const char* type; };
        const std::vector<Unit> units {
            { "DISTORTION", "dsOn", col::red, { "dsDrive", "dsTone", "dsMix" }, "dsType" },
            { "FILTER", "ffOn", col::teal, { "ffCutoff", "ffRes", "ffMix" }, "ffType" },
            { "CHORUS", "chOn", col::blue, { "chRate", "chDepth", "chDelay", "chFeedback", "chMix" }, nullptr },
            { "PHASER", "phOn", col::purple, { "phRate", "phDepth", "phFreq", "phFeedback", "phMix" }, nullptr },
            { "DELAY", "dlOn", col::green, { "dlFeedback", "dlTone", "dlMix" }, "dlTime" },
            { "MULTIBAND", "otOn", col::orange, { "otDepth", "otTime", "otUp", "otDown", "otMix" }, nullptr },
            { "REVERB", "rvOn", col::pink, { "rvSize", "rvDecay", "rvDamp", "rvPre", "rvWidth", "rvMix" }, nullptr },
            { "EQ", "eqOn", col::yellow, { "eqLow", "eqLowFreq", "eqHigh", "eqHighFreq" }, nullptr },
        };
        for (size_t u = 0; u < units.size(); ++u)
        {
            const auto& unit = units[u];
            const auto x = 10 + (int) (u % 2) * 595, y = 8 + (int) (u / 2) * 178;
            pg.boxes.push_back ({ R (x, y, 585, 170), unit.title, unit.c });
            toggle (pg, unit.on, "ON", unit.c, R (x + 130, y + 5, 40, 20));
            if (unit.type != nullptr) choice (pg, unit.type, R (x + 180, y + 4, 140, 22));
            if (juce::String (unit.on) == "dlOn") toggle (pg, "dlPing", "PING-PONG", unit.c, R (x + 330, y + 4, 100, 22));
            for (size_t k = 0; k < unit.knobs.size(); ++k)
                knob (pg, unit.knobs[k], unit.c, R (x + 14 + (int) k * 94, y + 40, 88, 112));
        }
    }

    void buildArpPage()
    {
        auto& pg = pages[3];
        pg.boxes.push_back ({ R (10, 8, 585, 300), "ARPEGGIATOR", col::green });
        toggle (pg, "arOn", "ON", col::green, R (150, 13, 40, 20));
        label (pg, "MODE", R (30, 44, 100, 14)); choice (pg, "arMode", R (28, 60, 150, 26));
        label (pg, "RATE", R (200, 44, 100, 14)); choice (pg, "arRate", R (198, 60, 110, 26));
        label (pg, "OCTAVES", R (330, 44, 100, 14)); choice (pg, "arOct", R (328, 60, 80, 26));
        knob (pg, "arGate", col::green, R (28, 110, 96, 116));
        knob (pg, "arSwing", col::green, R (134, 110, 96, 116));
        label (pg, "Hold a chord: the arpeggiator plays its notes in turn, in time with the song.", R (28, 250, 540, 18));

        pg.boxes.push_back ({ R (605, 8, 585, 300), "VOICING", col::orange });
        knob (pg, "voices", col::orange, R (625, 50, 90, 112), "Voices");
        knob (pg, "glide", col::orange, R (725, 50, 90, 112), "Glide");
        knob (pg, "bend", col::orange, R (825, 50, 90, 112), "Bend");
        knob (pg, "velAmp", col::orange, R (925, 50, 90, 112), "Velocity");
        toggle (pg, "mono", "MONO", col::orange, R (1030, 60, 120, 28));
        toggle (pg, "legato", "LEGATO", col::orange, R (1030, 100, 120, 28));
        label (pg, "Mono + Legato with Glide: notes slide into each other (808s, leads).", R (625, 250, 560, 18));
    }

    //==========================================================================
    void buildBrowserPage()
    {
        auto& pg = pages[4];
        pg.boxes.push_back ({ R (10, 8, 300, height - headerH - 18), "TYPE", col::teal });
        pg.boxes.push_back ({ R (318, 8, 872, height - headerH - 18), "SOUNDS", col::orange });
        const auto addFilterButtons = [&] (juce::OwnedArray<juce::TextButton>& into, const juce::StringArray& names, int y0, juce::Colour c, bool genres)
        {
            juce::StringArray items ("All");
            items.addArray (names);
            for (int i = 0; i < items.size(); ++i)
            {
                auto* b = into.add (new juce::TextButton (items[i]));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (genres ? 9102 : 9101);
                b->getProperties().set ("accent", c.toString());
                b->setBounds (20 + (i % 2) * 142, y0 + (i / 2) * 28, 136, 24);
                b->setToggleState (i == 0, juce::dontSendNotification);
                b->onClick = [this, b, genres, name = items[i]]
                {
                    if (! b->getToggleState()) return;
                    (genres ? genre : category) = name == "All" ? juce::String() : name;
                    filter();
                };
                pg.addAndMakeVisible (b);
            }
            return y0 + ((items.size() + 1) / 2) * 28;
        };
        const auto afterCategories = addFilterButtons (categoryButtons, presetCategories(), 36, col::teal, false);
        label (pg, "STYLE", R (22, afterCategories + 6, 200, 16), col::orange);
        addFilterButtons (genreButtons, presetGenres(), afterCategories + 26, col::orange, true);

        search.setTextToShowWhenEmpty ("Search sounds...", col::dim);
        search.setFont (font (14.0f));
        search.onTextChange = [this] { filter(); };
        search.setBounds (330, 34, 520, 28);
        pg.addAndMakeVisible (search);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setBounds (330, 70, 848, height - headerH - 92);
        pg.addAndMakeVisible (list);
        filter();
    }

    void reloadPresets()
    {
        all = factoryPresets();
        for (auto& p : userPresets())
            all.push_back (p);
    }

    void filter()
    {
        shown.clear();
        const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
        for (const auto& p : all)
        {
            if (category.isNotEmpty() && p.category != category && ! (category == "Yours" && p.user)) continue;
            if (genre.isNotEmpty() && ! p.genres.contains (genre)) continue;
            const auto text = (p.name + " " + p.category + " " + p.genres.joinIntoString (" ")).toLowerCase();
            bool ok = true;
            for (const auto& w : words) if (! text.contains (w)) { ok = false; break; }
            if (ok) shown.push_back (p);
        }
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& p = shown[(size_t) row];
        const auto current = p.name == state[nemixstate::preset].toString();
        if (selected || current)
        {
            g.setColour ((current ? col::orange : col::purple).withAlpha (0.28f));
            g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
        }
        g.setColour (col::text);
        g.setFont (font (14.5f, true));
        g.drawText (p.name, 12, 0, 330, h, juce::Justification::centredLeft, true);
        g.setColour (p.user ? col::yellow : col::teal);
        g.setFont (font (12.0f, true));
        g.drawText (p.user ? juce::String ("YOURS") : p.category.toUpperCase(), 350, 0, 150, h, juce::Justification::centredLeft, true);
        g.setColour (col::dim);
        g.drawText (p.genres.joinIntoString ("  /  ").toUpperCase(), 500, 0, w - 510, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) shown.size())
            loadPreset (shown[(size_t) row]);
    }

    void loadSound (const nemixstate::Sound& sound, const juce::String& name, const juce::String&, const juce::StringArray&)
    {
        um.beginNewTransaction ("Nemix: " + name);
        nemixstate::load (state, sound, name, &um);
        refreshAll();
    }
    void loadPreset (const Preset& p) { loadSound (p.sound, p.name, p.category, p.genres); }

    void step (int delta)
    {
        auto& from = shown.empty() ? all : shown;
        if (from.empty()) return;
        const auto name = state[nemixstate::preset].toString();
        int at = -1;
        for (size_t i = 0; i < from.size(); ++i) if (from[i].name == name) at = (int) i;
        at = (at + delta + (int) from.size()) % (int) from.size();
        loadPreset (from[(size_t) at]);
        list.selectRow (at, true, true);
    }

    void saveDialog()
    {
        auto* window = new juce::AlertWindow ("Save Sound", "A name for this sound:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", state[nemixstate::preset].toString() + " 2");
        window->addComboBox ("category", presetCategories(), "Type");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result == 1)
            {
                const auto name = window->getTextEditorContents ("name").trim();
                const auto cat = window->getComboBoxComponent ("category")->getText();
                if (name.isNotEmpty() && saveUserPreset (name, cat, nemixstate::soundOf (state)))
                {
                    state.setProperty (nemixstate::preset, name, &um);
                    reloadPresets();
                    filter();
                    refreshHeader();
                }
            }
        }), true);
    }

    //==========================================================================
    void setPage (int p)
    {
        page = p;
        for (int i = 0; i < 5; ++i)
        {
            pages[(size_t) i].setVisible (i == p);
            tabs[i]->setToggleState (i == p, juce::dontSendNotification);
        }
        if (p == 1) refreshRows();
    }

    void layout()
    {
        layoutHeader();
        for (auto& p : pages)
            p.setBounds (0, headerH, width, height - headerH);
    }

    void refreshAll()
    {
        for (auto* k : knobs) k->refresh();
        for (auto* c : choices) c->refresh();
        for (auto* t : toggles) t->refresh();
        refreshRows();
        refreshHeader();
        list.repaint();
        owner.repaint();
    }

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id) override
    {
        if (tree != state) return;
        if (id == nemixstate::preset) { refreshHeader(); list.repaint(); return; }
        const auto index = indexOf (id.toString());
        if (index < 0) return;
        for (auto* k : knobs) if (k->getIndex() == index) k->refresh();
        for (auto* c : choices) if (c->getIndex() == index) c->refresh();
        for (auto* t : toggles) if (t->getIndex() == index) t->refresh();
        if (params()[(size_t) index].id.startsWith ("m") && ! params()[(size_t) index].id.startsWith ("macro") && ! params()[(size_t) index].id.startsWith ("mono"))
        {
            for (auto* k : knobs) k->repaint();
            if (page == 1) refreshRows();
        }
        for (auto* a : animated) a->repaint();
    }
    void valueTreeRedirected (juce::ValueTree&) override { refreshAll(); }

    void timerCallback() override
    {
        auto synth = live ? live() : nullptr;
        const auto playing = synth != nullptr && synth->activeVoices() > 0;
        if (playing || wasPlaying)
        {
            if (page == 0)
                for (auto* a : animated) a->repaint();
            for (auto* k : knobs)
                if (k->isShowing() && ! routesTo (k->getIndex()).empty())
                    k->repaint();
        }
        wasPlaying = playing;
    }
    bool wasPlaying = false;
};

//==============================================================================
NemixPanel::NemixPanel (juce::ValueTree track, juce::UndoManager& um, std::function<std::shared_ptr<Nemix>()> liveSynth)
{
    setOpaque (true);
    impl = std::make_unique<Impl> (*this, track, um, std::move (liveSynth));
    setSize (Impl::width, Impl::height);
}

NemixPanel::~NemixPanel() = default;

void NemixPanel::paint (juce::Graphics& g)
{
    // The wall, darkened, filling the window
    g.fillAll (juce::Colour (0xff15171b));
    if (impl->wall.isValid())
    {
        const auto& w = impl->wall;
        const auto scale = std::max ((float) getWidth() / (float) w.getWidth(), (float) getHeight() / (float) w.getHeight());
        g.setOpacity (1.0f);
        g.drawImageTransformed (w, juce::AffineTransform::scale (scale));
    }
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (getLocalBounds());
    // The header: a dark bar with the logo sticker
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xf20a0c10), 0.0f, 0.0f, juce::Colour (0xd90a0c10), 0.0f, (float) Impl::headerH, false));
    g.fillRect (0, 0, getWidth(), Impl::headerH);
    g.setGradientFill (juce::ColourGradient (col::teal, 0.0f, 0.0f, col::red, (float) getWidth(), 0.0f, false));
    g.fillRect (0, Impl::headerH - 2, getWidth(), 2);
    if (impl->logo.isValid())
    {
        const auto box = RF (8.0f, 4.0f, 84.0f, (float) Impl::headerH - 10.0f);
        const auto& logo = impl->logo;
        const auto scale = std::min (box.getWidth() / (float) logo.getWidth(), box.getHeight() / (float) logo.getHeight());
        const auto w = (float) logo.getWidth() * scale, h = (float) logo.getHeight() * scale;
        const auto at = RF (w, h).withCentre (box.getCentre());
        juce::Graphics::ScopedSaveState keep (g);
        juce::Path clip;
        clip.addRoundedRectangle (at, 6.0f);
        g.reduceClipRegion (clip);
        g.drawImage (logo, at, juce::RectanglePlacement::stretchToFit);
    }
}

void NemixPanel::resized()
{
    impl->layout();
}

void NemixPanel::showPage (int page)
{
    impl->setPage (juce::jlimit (0, 4, page));
}

bool NemixPanel::loadPresetNamed (const juce::String& name)
{
    for (const auto& p : impl->all)
        if (p.name == name)
        {
            impl->loadPreset (p);
            return true;
        }
    return false;
}

} // namespace riff
