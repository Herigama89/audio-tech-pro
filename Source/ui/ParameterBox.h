#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/**
    One of the Inspector's parameter boxes (Logic's Region and Track boxes):
    a title bar that folds the box away, and rows of name + value. Values are
    numbers you drag up and down (or double-click to type), switches, or menus,
    each bound to a property of a ValueTree.
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
        slider->setColour (juce::Slider::trackColourId, theme::selection);
        slider->setColour (juce::Slider::textBoxTextColourId, theme::text);
        slider->setColour (juce::Slider::backgroundColourId, theme::slotDark);
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
        auto* button = new juce::TextButton();
        button->setColour (juce::TextButton::buttonColourId, theme::slotDark);
        button->setColour (juce::TextButton::textColourOffId, theme::text);
        button->onClick = [button, showMenu] { showMenu (*button); };
        row->refresh = [button, currentText] { button->setButtonText (currentText() + juce::String::fromUTF8 ("  \xe2\x96\xbe")); };
        row->control.reset (button);
        add (std::move (row));
    }

    void refresh()
    {
        for (auto& r : rows)
            if (r->refresh)
                r->refresh();
    }

    int preferredHeight() const { return titleHeight + (collapsed ? 0 : (int) rows.size() * rowHeight + 6); }

    std::function<void()> onLayoutChanged;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        auto bar = getLocalBounds().removeFromTop (titleHeight);
        g.setColour (theme::toolbarDark);
        g.fillRect (bar);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ((collapsed ? juce::String::fromUTF8 ("\xe2\x96\xb8 ") : juce::String::fromUTF8 ("\xe2\x96\xbe ")) + title,
                    bar.reduced (6, 0), juce::Justification::centredLeft, true);

        if (collapsed)
            return;
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (theme::dimText);
        for (size_t i = 0; i < rows.size(); ++i)
            g.drawText (rows[i]->name, 6, titleHeight + 3 + (int) i * rowHeight, labelWidth - 8, rowHeight, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            rows[i]->control->setVisible (! collapsed);
            rows[i]->control->setBounds (labelWidth, titleHeight + 4 + (int) i * rowHeight, getWidth() - labelWidth - 6, rowHeight - 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < titleHeight)
        {
            collapsed = ! collapsed;
            resized();
            if (onLayoutChanged)
                onLayoutChanged();
        }
    }

private:
    struct Row
    {
        juce::String name;
        std::unique_ptr<juce::Component> control;
        std::function<void()> refresh;
    };

    void add (std::unique_ptr<Row> row)
    {
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

    static constexpr int titleHeight = 20, rowHeight = 21, labelWidth = 66;
    juce::UndoManager& undoManager;
    juce::ValueTree tree;
    juce::String title;
    std::vector<std::unique_ptr<Row>> rows;
    bool collapsed = false;
};

} // namespace riff
