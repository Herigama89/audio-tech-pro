#include "GeneratorPanel.h"
#include "Theme.h"

namespace riff
{

GeneratorPanel::GeneratorPanel (const generator::Settings& initial, bool useGenreTempo,
                                std::function<void (const Request&)> callback)
    : seed (initial.seed), onGenerate (std::move (callback))
{
    for (auto* l : { &genreCaption, &keyCaption, &barsCaption, &partsCaption, &variationCaption })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }

    int id = 1;
    for (const auto& g : generator::genres())
        genreBox.addItem (g.name, id++);
    addAndMakeVisible (genreBox);

    for (int k = 0; k < 12; ++k)
        keyBox.addItem (generator::keyName (k, false).upToFirstOccurrenceOf (" ", false, false), k + 1);
    addAndMakeVisible (keyBox);

    scaleBox.addItem ("Major", 1);
    scaleBox.addItem ("Minor", 2);
    addAndMakeVisible (scaleBox);

    for (int bars : { 4, 8, 16 })
        barsBox.addItem (juce::String (bars) + " bars", bars);
    addAndMakeVisible (barsBox);

    for (auto* b : { &drumsButton, &bassButton, &chordsButton, &melodyButton })
        addAndMakeVisible (b);

    variationSlider.setRange (0.0, 1.0, 0.01);
    variationSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    variationSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    variationSlider.setTooltip ("Low: a steady loop. High: more fills, rolls and changes.");
    addAndMakeVisible (variationSlider);

    tempoButton.setTooltip ("Set the project's tempo to the genre's usual tempo");
    addAndMakeVisible (tempoButton);
    replaceButton.setTooltip ("On: each part's new notes replace the region it was generated in before, on the same track and at the "
                              "same place (your instrument stays). Off: new tracks each time.");
    replaceButton.setToggleState (replaceByDefault, juce::dontSendNotification);
    replaceButton.onClick = [this] { replaceByDefault = replaceButton.getToggleState(); };
    addAndMakeVisible (replaceButton);

    about.setFont (juce::FontOptions (12.5f, juce::Font::italic));
    about.setColour (juce::Label::textColourId, theme::cream);
    addAndMakeVisible (about);

    help.setText ("Writes MIDI onto new instrument tracks, starting at the playhead's bar. "
                  "Drums use the standard General MIDI drum notes, so any drum plugin plays them: "
                  "load yours on the Drums track. The other parts start on the built-in synth until you choose your own sounds.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, theme::dimText);
    help.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (help);

    generateButton.setTooltip ("Add a new set of parts");
    generateButton.onClick = [this] { send (false); };
    addAndMakeVisible (generateButton);

    anotherButton.setTooltip ("Replace the parts you just generated with a new take");
    anotherButton.onClick = [this] { send (true); };
    addAndMakeVisible (anotherButton);

    // Initial values
    genreBox.setSelectedId (initial.genre + 1, juce::dontSendNotification);
    keyBox.setSelectedId (initial.root + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (initial.minor ? 2 : 1, juce::dontSendNotification);
    barsBox.setSelectedId (initial.bars == 4 || initial.bars == 16 ? initial.bars : 8, juce::dontSendNotification);
    drumsButton.setToggleState (initial.drums, juce::dontSendNotification);
    bassButton.setToggleState (initial.bass, juce::dontSendNotification);
    chordsButton.setToggleState (initial.chords, juce::dontSendNotification);
    melodyButton.setToggleState (initial.melody, juce::dontSendNotification);
    variationSlider.setValue (initial.variation, juce::dontSendNotification);
    tempoButton.setToggleState (useGenreTempo, juce::dontSendNotification);

    genreBox.onChange = [this] { genreChanged(); };
    genreChanged();
    scaleBox.setSelectedId (initial.minor ? 2 : 1, juce::dontSendNotification);   // keep the saved choice

    setSize (460, 430);
}

void GeneratorPanel::genreChanged()
{
    const auto index = juce::jlimit (0, (int) generator::genres().size() - 1, genreBox.getSelectedId() - 1);
    const auto& genre = generator::genres()[(size_t) index];
    about.setText (genre.about, juce::dontSendNotification);
    tempoButton.setButtonText ("Use the genre's tempo (" + juce::String (juce::roundToInt (genre.tempo)) + " BPM)");
    scaleBox.setSelectedId (genre.minor ? 2 : 1, juce::dontSendNotification);
}

generator::Settings GeneratorPanel::currentSettings() const
{
    generator::Settings s;
    s.genre = juce::jmax (0, genreBox.getSelectedId() - 1);
    s.root = juce::jmax (0, keyBox.getSelectedId() - 1);
    s.minor = scaleBox.getSelectedId() == 2;
    s.bars = juce::jmax (4, barsBox.getSelectedId());
    s.drums = drumsButton.getToggleState();
    s.bass = bassButton.getToggleState();
    s.chords = chordsButton.getToggleState();
    s.melody = melodyButton.getToggleState();
    s.variation = (float) variationSlider.getValue();
    s.seed = seed;
    return s;
}

void GeneratorPanel::send (bool replaceLast)
{
    // Every press is a new take.
    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;

    Request request;
    request.settings = currentSettings();
    request.useGenreTempo = tempoButton.getToggleState();
    request.replaceLast = replaceLast;
    request.replaceGenerated = replaceButton.getToggleState();

    if (onGenerate != nullptr)
        onGenerate (request);
}

void GeneratorPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void GeneratorPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
    constexpr int captionWidth = 76, rowHeight = 26, gap = 10;

    auto row = [&] (juce::Label& caption)
    {
        auto r = area.removeFromTop (rowHeight);
        caption.setBounds (r.removeFromLeft (captionWidth));
        area.removeFromTop (gap);
        return r;
    };

    genreBox.setBounds (row (genreCaption).removeFromLeft (200));
    about.setBounds (area.removeFromTop (34).withTrimmedLeft (captionWidth - 4));
    area.removeFromTop (4);

    {
        auto r = row (keyCaption);
        keyBox.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (8);
        scaleBox.setBounds (r.removeFromLeft (110));
    }

    barsBox.setBounds (row (barsCaption).removeFromLeft (110));

    {
        auto r = row (partsCaption);
        for (auto* b : { &drumsButton, &bassButton, &chordsButton, &melodyButton })
            b->setBounds (r.removeFromLeft (84));
    }

    variationSlider.setBounds (row (variationCaption).removeFromLeft (240));

    tempoButton.setBounds (area.removeFromTop (rowHeight).withTrimmedLeft (captionWidth - 4));
    area.removeFromTop (gap);
    replaceButton.setBounds (area.removeFromTop (rowHeight).withTrimmedLeft (captionWidth - 4));
    area.removeFromTop (gap);

    auto buttons = area.removeFromBottom (32);
    generateButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    anotherButton.setBounds (buttons.removeFromRight (120));

    area.removeFromBottom (gap);
    help.setBounds (area);
}

} // namespace riff
