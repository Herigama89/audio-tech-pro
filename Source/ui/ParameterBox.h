#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/**
    One of the Inspector's parameter boxes, laid out like Logic's Region and
    Track boxes: a title bar that folds the box away ("> Region: Audio
    Defaults"), then rows of "Name:" (right-aligned) and its value. Values are
    numbers you drag up and down (or double-click to type), switches, menus,
    plain text, or any component. Rows that don't apply right now are greyed
    out, and rows after beginMore() sit under a "More" disclosure.
*/
class ParameterBox final : public juce::Component,
                           private juce::ValueTree::Listener
{
public:
    explicit ParameterBox (juce::UndoManager& um) : undoManager (um)
    {
        setInterceptsMouseClicks (true, true);
    }

    ~ParameterBox() override { tree.removeListener (this); }

    /** Starts over: a new title and tree; add the rows after. */
    void reset (const juce::String& newTitle, juce::ValueTree newTree)
    {
        tree.removeListener (this);
        rows.clear();
        title = newTitle;
        tree = newTree;
        tree.addListener (this);
        addingMore = false;
    }

    /** The rows added after this go under "More" (folded away until you open it). */
    void beginMore() { addingMore = true; }

    /** The last row added is greyed out (and can't be changed) unless this says it applies. */
    void enableLastWhen (std::function<bool()> applies)
    {
        if (! rows.empty())
        {
            rows.back()->applies = std::move (applies);
            refresh();
        }
    }

    void addNumber (const juce::String& name, const juce::Identifier& property, double min, double max, double step,
                    double fallback, std::function<juce::String (double)> text,
                    std::function<double (double)> toProperty = {}, std::function<double (double)> fromProperty = {})
    {
        auto row = std::make_unique<Row>();
        row->name = name;
        auto* slider = new juce::Slider (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
        slider->setRange (min, max, step);
        slider->setTextBoxIsEditable (true);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::textBoxTextColourId, theme::text);
        slider->setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
        slider->textFromValueFunction = std::move (text);
        slider->setDoubleClickReturnValue (true, fallback);
        slider->setMouseDragSensitivity (300);
        auto toP = toProperty ? toProperty : [] (double v) { return v; };
        auto fromP = fromProperty ? fromProperty : [] (double v) { return v; };
        slider->onDragStart = [this, name] { undoManager.beginNewTransaction (name); };
        slider->onValueChange = [this, slider, property, toP] { tree.setProperty (property, toP (slider->getValue()), &undoManager); };
        row->refresh = [this, slider, property, fallback, fromP]
        {
            slider->setValue (tree.hasProperty (property) ? fromP ((double) tree[property]) : fallback, juce::dontSendNotification);
            slider->updateText();   // (the text even when the value didn't move)
        };
        row->control.reset (slider);
        add (std::move (row));
    }

    void addSwitch (const juce::String& name, const juce::Identifier& property, bool fallback, std::function<void (bool)> onSet = {})
    {
        auto row = std::make_unique<Row>();
        row->name = name;
        auto* toggle = new juce::ToggleButton();
        toggle->setColour (juce::ToggleButton::tickColourId, theme::accent);
        toggle->onClick = [this, toggle, property, name, onSet]
        {
            if (onSet)
            {
                onSet (toggle->getToggleState());
                return;
            }
            undoManager.beginNewTransaction (name);
            tree.setProperty (property, toggle->getToggleState(), &undoManager);
        };
        row->refresh = [this, toggle, property, fallback] { toggle->setToggleState ((bool) tree.getProperty (property, fallback), juce::dontSendNotification); };
        row->control.reset (toggle);
        add (std::move (row));
    }

    /** A value shown as text that opens a menu when clicked (choices are made by `showMenu`). */
    void addMenu (const juce::String& name, std::function<juce::String()> currentText, std::function<void (juce::Component&)> showMenu)
    {
        auto row = std::make_unique<Row>();
        row->name = name;
        auto* button = new MenuValue();
        button->onClick = [button, showMenu] { showMenu (*button); };
        row->refresh = [button, currentText] { button->setButtonText (currentText()); };
        row->control.reset (button);
        add (std::move (row));
    }

    /** A value you read but don't change here (e.g. the channel the track plays on). */
    void addText (const juce::String& name, std::function<juce::String()> currentText)
    {
        auto row = std::make_unique<Row>();
        row->name = name;
        auto* label = new juce::Label();
        label->setColour (juce::Label::textColourId, theme::text);
        label->setFont (juce::FontOptions (12.0f));
        label->setBorderSize ({ 0, 2, 0, 0 });
        row->refresh = [label, currentText] { label->setText (currentText(), juce::dontSendNotification); };
        row->control.reset (label);
        add (std::move (row));
    }

    /** Any control (owned by the box), `height` pixels tall. */
    void addComponent (const juce::String& name, juce::Component* control, int height, std::function<void()> refreshFn = {})
    {
        auto row = std::make_unique<Row>();
        row->name = name;
        row->height = height;
        row->refresh = std::move (refreshFn);
        row->control.reset (control);
        add (std::move (row));
    }

    void refresh()
    {
        for (auto& r : rows)
        {
            if (r->refresh)
                r->refresh();
            const bool on = ! r->applies || r->applies();
            r->control->setEnabled (on);
            r->control->setAlpha (on ? 1.0f : 0.4f);
        }
        repaint();
    }

    void setCollapsed (bool shouldBeCollapsed)
    {
        collapsed = shouldBeCollapsed;
        layoutChanged();
    }
    bool isCollapsed() const noexcept { return collapsed; }
    static constexpr int titleBarHeight() { return titleHeight; }

    int preferredHeight() const
    {
        if (collapsed)
            return titleHeight;
        int h = titleHeight + 4;
        bool anyMore = false;
        for (const auto& r : rows)
        {
            anyMore = anyMore || r->more;
            if (! r->more || moreOpen)
                h += r->height;
        }
        return h + (anyMore ? moreHeight : 0) + 4;
    }

    std::function<void()> onLayoutChanged;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        auto bar = getLocalBounds().removeFromTop (titleHeight);
        g.setColour (theme::toolbarDark);
        g.fillRect (bar);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (bar.removeFromBottom (1));
        drawDisclosure (g, juce::Rectangle<float> (8.0f, 0.0f, 10.0f, (float) titleHeight), ! collapsed, theme::text);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().removeFromTop (titleHeight).withTrimmedLeft (24), juce::Justification::centredLeft, true);

        if (collapsed)
            return;

        // "Name:" right-aligned against the values, greyed when the row doesn't apply.
        g.setFont (juce::FontOptions (12.0f));
        for (const auto& r : rows)
        {
            if (! r->control->isVisible())
                continue;
            const auto on = ! r->applies || r->applies();
            g.setColour (theme::text.withAlpha (on ? 0.75f : 0.3f));
            const auto b = r->control->getBounds();
            if (r->name.isNotEmpty())
                g.drawFittedText (r->name + ":", juce::Rectangle<int> (2, b.getY(), labelWidth() - 6, juce::jmin (b.getHeight(), rowHeight)),
                                  juce::Justification::centredRight, 1, 0.75f);
        }

        if (moreArea().getHeight() > 0)
        {
            const auto m = moreArea();
            drawDisclosure (g, m.toFloat().withX (8.0f).withWidth (10.0f), moreOpen, theme::dimText);
            g.setColour (theme::dimText);
            g.drawText ("More", m.withTrimmedLeft (24), juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        int y = titleHeight + 4;
        for (auto& r : rows)
        {
            const bool shown = ! collapsed && (! r->more || moreOpen);
            r->control->setVisible (shown);
            if (! shown)
                continue;
            r->control->setBounds (labelWidth(), y + 1, getWidth() - labelWidth() - 6, r->height - 3);
            y += r->height;
            if (r->more == false && nextIsMore (r.get()))
                y += moreHeight;   // the More row sits between them
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < titleHeight)
            setCollapsed (! collapsed);
        else if (moreArea().contains (e.getPosition()))
        {
            moreOpen = ! moreOpen;
            layoutChanged();
        }
    }

private:
    /** A menu value: its text and Logic's up-down arrows on the right. */
    struct MenuValue final : juce::Button
    {
        MenuValue() : juce::Button ({}) {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto r = getLocalBounds().toFloat();
            if (over)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 3.0f);
            }
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (getButtonText(), getLocalBounds().withTrimmedLeft (2).withTrimmedRight (16), juce::Justification::centredLeft, true);
            // The chevrons up and down
            const auto cx = r.getRight() - 8.0f, cy = r.getCentreY();
            juce::Path p;
            p.startNewSubPath (cx - 3.0f, cy - 2.0f); p.lineTo (cx, cy - 5.0f); p.lineTo (cx + 3.0f, cy - 2.0f);
            p.startNewSubPath (cx - 3.0f, cy + 2.0f); p.lineTo (cx, cy + 5.0f); p.lineTo (cx + 3.0f, cy + 2.0f);
            g.setColour (theme::dimText);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
    };

    struct Row
    {
        juce::String name;
        std::unique_ptr<juce::Component> control;
        std::function<void()> refresh;
        std::function<bool()> applies;
        bool more = false;
        int height = 21;
    };

    static void drawDisclosure (juce::Graphics& g, juce::Rectangle<float> area, bool open, juce::Colour colour)
    {
        juce::Path p;
        const auto c = area.getCentre();
        if (open) { p.startNewSubPath (c.x - 4.0f, c.y - 2.0f); p.lineTo (c.x, c.y + 2.0f); p.lineTo (c.x + 4.0f, c.y - 2.0f); }
        else      { p.startNewSubPath (c.x - 2.0f, c.y - 4.0f); p.lineTo (c.x + 2.0f, c.y); p.lineTo (c.x - 2.0f, c.y + 4.0f); }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    int labelWidth() const { return juce::jlimit (70, 110, getWidth() * 48 / 100); }

    bool nextIsMore (const Row* row) const
    {
        for (size_t i = 0; i + 1 < rows.size(); ++i)
            if (rows[i].get() == row)
                return rows[i + 1]->more;
        return false;
    }

    juce::Rectangle<int> moreArea() const
    {
        if (collapsed)
            return {};
        int y = titleHeight + 4;
        for (const auto& r : rows)
        {
            if (r->more)
                return { 0, y, getWidth(), moreHeight };
            y += r->height;
        }
        return {};
    }

    void layoutChanged()
    {
        resized();
        repaint();
        if (onLayoutChanged)
            onLayoutChanged();
    }

    void add (std::unique_ptr<Row> row)
    {
        row->more = addingMore;
        addAndMakeVisible (*row->control);
        if (row->refresh)
            row->refresh();
        rows.push_back (std::move (row));
    }

    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override
    {
        if (t == tree)
            refresh();
    }

    static constexpr int titleHeight = 24, rowHeight = 21, moreHeight = 20;
    juce::UndoManager& undoManager;
    juce::ValueTree tree;
    juce::String title;
    std::vector<std::unique_ptr<Row>> rows;
    bool collapsed = false, moreOpen = false, addingMore = false;
};

} // namespace riff
