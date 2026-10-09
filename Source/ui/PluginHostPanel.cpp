#include "PluginHostPanel.h"
#include "ControlMenu.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    constexpr int maxRows = 512;
    constexpr int rowHeight = 26;
}

PluginHostPanel::PluginHostPanel (juce::AudioPluginInstance& p, juce::AudioProcessorEditor* e, Context c)
    : plugin (p), editor (e), context (std::move (c))
{
    for (auto* b : { &editorButton, &controlsButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    editorButton.onClick = [this] { showControls (false); };
    controlsButton.onClick = [this] { showControls (true); };
    editorButton.setEnabled (editor != nullptr);

    automationButton.setTooltip ("Every parameter of this plugin: show or create its automation, learn a MIDI controller, set it to default");
    automationButton.setWantsKeyboardFocus (false);
    automationButton.onClick = [this]
    {
        juce::PopupMenu menu;
        addParameterMenus (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automationButton));
    };
    addAndMakeVisible (automationButton);

    touchedButton.setTooltip ("The last knob you touched in the plugin: click for its automation and MIDI Learn");
    touchedButton.setWantsKeyboardFocus (false);
    touchedButton.onClick = [this]
    {
        if (const int index = lastTouched.load(); index >= 0)
            showParameterMenu (index, touchedButton);
    };
    addAndMakeVisible (touchedButton);

    // The Controls view: a slider per parameter (made now, shown on request).
    const auto& params = plugin.getParameters();
    for (int i = 0; i < juce::jmin (maxRows, params.size()); ++i)
    {
        auto* row = rows.add (new Row());
        auto* param = params[i];
        row->name.setText (param->getName (48), juce::dontSendNotification);
        row->name.setColour (juce::Label::textColourId, theme::text);
        row->name.setFont (juce::FontOptions (12.5f));
        row->value.setColour (juce::Label::textColourId, theme::dimText);
        row->value.setFont (juce::FontOptions (12.0f));
        row->value.setJustificationType (juce::Justification::centredRight);
        row->slider.setSliderStyle (juce::Slider::LinearBar);
        row->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        row->slider.setRange (0.0, 1.0, 0.0);
        row->slider.setDoubleClickReturnValue (true, param->getDefaultValue());
        row->slider.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.75f));
        row->slider.setColour (juce::Slider::backgroundColourId, theme::toolbarDark);
        row->slider.onDragStart = [param] { param->beginChangeGesture(); };
        row->slider.onDragEnd = [param] { param->endChangeGesture(); };
        row->slider.onValueChange = [row, param]
        {
            param->setValueNotifyingHost ((float) row->slider.getValue());
            row->value.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
        };
        row->slider.onMenu = [this, i, row] { showParameterMenu (i, row->slider); };
        for (auto* comp : std::initializer_list<juce::Component*> { &row->name, &row->slider, &row->value })
            rowsHolder.addAndMakeVisible (comp);
    }
    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    addChildComponent (viewport);

    plugin.addListener (this);
    if (editor != nullptr)
    {
        addAndMakeVisible (*editor);
        editor->addComponentListener (this);
    }
    showControls (editor == nullptr);
    startTimerHz (15);
}

PluginHostPanel::~PluginHostPanel()
{
    plugin.removeListener (this);
    if (editor != nullptr)
        editor->removeComponentListener (this);
    editor.reset();   // before the plugin can go
}

juce::String PluginHostPanel::paramKey (int index) const
{
    if (context.insertId.isEmpty())
        return {};
    return model::insertParam (context.insertId, "#" + juce::String (index));
}

void PluginHostPanel::showParameterMenu (int index, juce::Component& target)
{
    juce::PopupMenu menu;
    const auto& params = plugin.getParameters();
    if (! juce::isPositiveAndBelow (index, params.size()))
        return;
    menu.addSectionHeader (params[index]->getName (48));
    controlmenu::Target t;
    t.project = context.project;
    t.owner = context.owner;
    t.param = paramKey (index);
    t.name = params[index]->getName (40);
    t.learnParam = t.param.isEmpty() ? juce::String ("-") : juce::String();
    t.um = context.um;
    auto* param = params[index];
    t.value = [param] { return (double) param->getValue(); };
    t.reset = [param]
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
    };
    // The lane is named after the parameter (as when you touch it).
    if (t.param.isNotEmpty())
        if (auto lane = model::laneFor (context.project, context.owner, t.param, false); lane.isValid())
            lane.setProperty (ids::name, t.name, nullptr);
    controlmenu::addItems (menu, t);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition());
}

void PluginHostPanel::addParameterMenus (juce::PopupMenu& menu)
{
    const auto& params = plugin.getParameters();
    if (params.isEmpty())
    {
        menu.addItem ("This plugin has no parameters", false, false, [] {});
        return;
    }
    for (int i = 0; i < juce::jmin (maxRows, params.size()); ++i)
    {
        auto* param = params[i];
        controlmenu::Target t;
        t.project = context.project;
        t.owner = context.owner;
        t.param = paramKey (i);
        t.name = param->getName (40);
        t.learnParam = t.param.isEmpty() ? juce::String ("-") : juce::String();
        t.um = context.um;
        t.value = [param] { return (double) param->getValue(); };
        t.reset = [param]
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        };
        juce::PopupMenu sub;
        controlmenu::addItems (sub, t);
        const auto lane = t.param.isNotEmpty() ? model::laneFor (context.project, context.owner, t.param, false) : juce::ValueTree();
        menu.addSubMenu (t.name, sub, true, nullptr, lane.isValid() && lane.getNumChildren() > 0);   // ticked: it has automation
    }
}

void PluginHostPanel::showControls (bool controls)
{
    showingControls = controls || editor == nullptr;
    editorButton.setToggleState (! showingControls, juce::dontSendNotification);
    controlsButton.setToggleState (showingControls, juce::dontSendNotification);
    if (editor != nullptr)
        editor->setVisible (! showingControls);
    viewport.setVisible (showingControls);
    fitToContent();
}

void PluginHostPanel::fitToContent()
{
    const juce::ScopedValueSetter<bool> guard (fitting, true);
    if (! showingControls && editor != nullptr)
        setSize (juce::jmax (420, editor->getWidth()), editor->getHeight() + headerHeight);
    else
        setSize (juce::jmax (getWidth(), 520), juce::jlimit (160, 640, rows.size() * rowHeight + headerHeight + 12));

    if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
        window->setResizable (isResizableHost(), false);
}

void PluginHostPanel::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    // The plugin changed its own size: the window follows.
    if (wasResized && ! fitting && ! showingControls)
        fitToContent();
}

void PluginHostPanel::timerCallback()
{
    if (const int touched = lastTouched.load(); touched != shownTouched)
    {
        shownTouched = touched;
        const auto& params = plugin.getParameters();
        touchedButton.setButtonText (juce::isPositiveAndBelow (touched, params.size()) ? "Touched: " + params[touched]->getName (32)
                                                                                         : juce::String ("Touch a knob in the plugin"));
        touchedButton.setEnabled (touched >= 0);
    }
    if (showingControls)
    {
        const auto& params = plugin.getParameters();
        for (int i = 0; i < rows.size() && i < params.size(); ++i)
            if (! rows[i]->slider.isMouseButtonDown())
            {
                rows[i]->slider.setValue (params[i]->getValue(), juce::dontSendNotification);
                rows[i]->value.setText (params[i]->getCurrentValueAsText(), juce::dontSendNotification);
            }
    }
}

void PluginHostPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::toolbarDark);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::outline);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
}

void PluginHostPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (6, 4);
    editorButton.setBounds (header.removeFromLeft (60));
    controlsButton.setBounds (header.removeFromLeft (70));
    header.removeFromLeft (10);
    automationButton.setBounds (header.removeFromLeft (92));
    header.removeFromLeft (10);
    touchedButton.setBounds (header.removeFromLeft (juce::jmin (260, header.getWidth())));

    if (editor != nullptr && ! showingControls && ! fitting)
    {
        // The window was resized by hand (a resizable plugin): the plugin follows.
        const juce::ScopedValueSetter<bool> guard (fitting, true);
        editor->setBounds (area);
    }
    else if (editor != nullptr)
        editor->setTopLeftPosition (area.getPosition());

    viewport.setBounds (area.reduced (4));
    const auto w = viewport.getWidth() - viewport.getScrollBarThickness();
    rowsHolder.setSize (w, rows.size() * rowHeight);
    for (int i = 0; i < rows.size(); ++i)
    {
        auto r = juce::Rectangle<int> (0, i * rowHeight, w, rowHeight).reduced (4, 3);
        rows[i]->name.setBounds (r.removeFromLeft (juce::jmin (180, r.getWidth() / 3)));
        rows[i]->value.setBounds (r.removeFromRight (110));
        rows[i]->slider.setBounds (r.reduced (4, 1));
    }
}

} // namespace riff
