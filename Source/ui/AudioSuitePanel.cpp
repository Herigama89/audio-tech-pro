#include "AudioSuitePanel.h"

namespace riff
{

AudioSuitePanel::AudioSuitePanel()
{
    pluginButton.setTooltip ("The effect to process with: built-in, VST3, AU or CLAP");
    pluginButton.onClick = [this] { if (onChooseEffect) onChooseEffect (pluginButton); };
    addAndMakeVisible (pluginButton);

    modeBox.addItem ("Clip", 1);
    modeBox.addItem ("Whole File", 2);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setTooltip ("Clip: just the selected regions (with handles). Whole File: each region's whole audio file.");
    addAndMakeVisible (modeBox);

    for (auto* s : { &handles, &tail })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        s->setRange (0.0, 10.0, 0.1);
        s->textFromValueFunction = [] (double v) { return juce::String (v, 1) + " s"; };
        addAndMakeVisible (s);
    }
    handles.setValue (2.0, juce::dontSendNotification);
    handles.setTooltip ("Handles: extra audio processed either side of each region, so you can still drag its edges out");
    tail.setValue (0.0, juce::dontSendNotification);
    handles.updateText();
    tail.updateText();
    tail.setTooltip ("Tail: extra time after each region for a reverb or delay to ring out");

    previewButton.setTooltip ("Hear the first selected region through the effect");
    previewButton.onClick = [this] { if (onPreview) onPreview(); };
    renderButton.setTooltip ("Process the selected regions into new files (Undo puts them back)");
    renderButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.2f));
    renderButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    renderButton.onClick = [this] { if (onRender) onRender(); };
    for (auto* b : { &previewButton, &renderButton })
        addAndMakeVisible (b);

    for (auto* l : { &status, &selection })
    {
        l->setColour (juce::Label::textColourId, theme::dimText);
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (l);
    }
    status.setText ("Choose an effect, set it up below, then Render.", juce::dontSendNotification);

    setSize (560, barHeight + 160);
    startTimerHz (4);
    timerCallback();
}

AudioSuitePanel::~AudioSuitePanel()
{
    if (face != nullptr)
        face->removeComponentListener (this);
    face.reset();      // (the plugin's face goes before the plugin)
    effect.reset();
}

void AudioSuitePanel::setEffect (std::shared_ptr<Effect> newEffect, const juce::String& name, juce::Component* faceToOwn,
                                 std::function<void()> prepare)
{
    if (face != nullptr)
        face->removeComponentListener (this);
    face.reset();
    effect = std::move (newEffect);
    beforeUse = std::move (prepare);
    face.reset (faceToOwn);
    pluginButton.setButtonText (name.isNotEmpty() ? name + juce::String::fromUTF8 (" \xe2\x96\xbe") : juce::String ("Choose an effect..."));
    if (face != nullptr)
    {
        addAndMakeVisible (*face);
        face->addComponentListener (this);
    }
    status.setText (effect != nullptr ? "Set it up, then Preview or Render." : "Choose an effect.", juce::dontSendNotification);
    fit();
}

void AudioSuitePanel::fit()
{
    const auto w = juce::jmax (560, face != nullptr ? face->getWidth() : 0);
    const auto h = barHeight + (face != nullptr ? face->getHeight() : 160);
    if (w != getWidth() || h != getHeight())
        setSize (w, h);
    else
        resized();
}

audiosuite::Options AudioSuitePanel::getOptions() const
{
    audiosuite::Options o;
    o.wholeFile = modeBox.getSelectedId() == 2;
    o.handleSeconds = handles.getValue();
    o.tailSeconds = tail.getValue();
    return o;
}

void AudioSuitePanel::timerCallback()
{
    if (selectionText)
        selection.setText (selectionText(), juce::dontSendNotification);
    const bool previewing = isPreviewing && isPreviewing();
    previewButton.setButtonText (previewing ? "Stop" : "Preview");
    renderButton.setEnabled (effect != nullptr);
    previewButton.setEnabled (effect != nullptr || previewing);
}

void AudioSuitePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    auto bar = getLocalBounds().removeFromTop (barHeight);
    g.setColour (theme::toolbarDark);
    g.fillRect (bar);
    g.setColour (juce::Colours::black);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (theme::text.withAlpha (0.75f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Handles", handles.getBounds().translated (-60, 0).withWidth (56), juce::Justification::centredRight, false);
    g.drawText ("Tail", tail.getBounds().translated (-40, 0).withWidth (36), juce::Justification::centredRight, false);

    if (face == nullptr)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Select audio regions in the tracks area, choose an effect above, and Render.\n"
                          "Each region gets a new file; the original stays on disk, and Undo brings it back.",
                          getLocalBounds().withTrimmedTop (barHeight).reduced (20), juce::Justification::centred, 3);
    }
}

void AudioSuitePanel::resized()
{
    auto bar = getLocalBounds().removeFromTop (barHeight).reduced (10, 8);
    auto row1 = bar.removeFromTop (28);
    pluginButton.setBounds (row1.removeFromLeft (220));
    row1.removeFromLeft (8);
    modeBox.setBounds (row1.removeFromLeft (110));
    renderButton.setBounds (row1.removeFromRight (90));
    row1.removeFromRight (6);
    previewButton.setBounds (row1.removeFromRight (80));
    bar.removeFromTop (6);
    auto row2 = bar.removeFromTop (24);
    row2.removeFromLeft (60);
    handles.setBounds (row2.removeFromLeft (180));
    row2.removeFromLeft (46);
    tail.setBounds (row2.removeFromLeft (180));
    auto row3 = bar;
    selection.setBounds (row3.removeFromLeft (row3.getWidth() / 2));
    status.setBounds (row3);

    if (face != nullptr)
        face->setTopLeftPosition (0, barHeight);
}

} // namespace riff
