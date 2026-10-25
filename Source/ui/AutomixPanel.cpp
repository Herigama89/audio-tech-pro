#include "AutomixPanel.h"
#include "Theme.h"

namespace riff
{

AutomixPanel::AutomixPanel (juce::ValueTree p) : project (std::move (p))
{
    genreBox.addItemList (automix::genres(), 1);
    const auto last = project.getProperty ("automixGenre", "Hip-Hop").toString();
    genreBox.setSelectedItemIndex (juce::jmax (0, automix::genres().indexOf (last)), juce::dontSendNotification);
    genreBox.setTooltip ("The style to balance for: how loud the kick, bass, vocal, keys... sit against each other");
    addAndMakeVisible (genreBox);

    rangeBox.addItemList ({ "Whole song", "Cycle range" }, 1);
    rangeBox.setSelectedItemIndex ((bool) project.getProperty ("cycleEnabled", false) ? 1 : 0, juce::dontSendNotification);
    rangeBox.setTooltip ("What to listen to: the whole song, or just the cycle (a chorus, say: quicker)");
    addAndMakeVisible (rangeBox);

    runButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    runButton.setTooltip ("Listen to every track and set the faders for this genre (Undo puts them back)");
    runButton.onClick = [this]
    {
        const auto genre = genreBox.getText();
        project.setProperty ("automixGenre", genre, nullptr);
        automix::Options options { fadersBox.getToggleState(), panBox.getToggleState(), pluginsBox.getToggleState() };
        project.setProperty ("automixDo", juce::String (options.faders ? "f" : "") + (options.pan ? "p" : "") + (options.plugins ? "i" : ""), nullptr);
        if (onRun)
            onRun (genre, rangeBox.getSelectedItemIndex() == 1, options);
    };
    addAndMakeVisible (runButton);

    const auto doNow = project.getProperty ("automixDo", "fpi").toString();
    fadersBox.setToggleState (doNow.containsChar ('f'), juce::dontSendNotification);
    panBox.setToggleState (doNow.containsChar ('p'), juce::dontSendNotification);
    pluginsBox.setToggleState (doNow.containsChar ('i'), juce::dontSendNotification);
    fadersBox.setTooltip ("Set each track's fader to where its part sits in the genre");
    panBox.setTooltip ("Pan each track to where its part usually sits in the genre: kick, snare, bass and lead vocal in the middle, "
                       "doubled guitars left and right, backing vocals spread...");
    pluginsBox.setTooltip ("Add the plugins each part usually gets in the genre - EQ, compression set from its own level, de-essing, "
                           "saturation on an 808, tuning on a lead vocal - ready to adjust (a track keeps any it already has)");
    for (auto* b : { &fadersBox, &panBox, &pluginsBox })
        addAndMakeVisible (b);

    report.setMultiLine (true);
    report.setReadOnly (true);
    report.setCaretVisible (false);
    report.setFont (juce::FontOptions (13.0f));
    report.setColour (juce::TextEditor::backgroundColourId, theme::lcd);
    report.setColour (juce::TextEditor::textColourId, theme::lcdText);
    report.setColour (juce::TextEditor::outlineColourId, theme::outline);
    report.setText ("Each track is measured while it plays, given a role from its name (Kick, 808, Lead Vox, Keys...) "
                    "or its sound, and its fader set to where that role sits in the genre. "
                    "Name your tracks well for the best result. It's a starting point: Undo puts the faders back.");
    addAndMakeVisible (report);
    setSize (520, 480);
}

void AutomixPanel::setBusy (bool busy)
{
    runButton.setEnabled (! busy);
    runButton.setButtonText (busy ? "Listening..." : "Automix");
}

void AutomixPanel::showResults (const std::vector<automix::Result>& results, const juce::String& genre)
{
    setBusy (false);
    if (results.empty())
    {
        report.setText ("Nothing played in that range, so nothing changed. Choose Whole song, or set the cycle over a busy part.");
        return;
    }
    juce::String text = "Balanced for " + genre + ":\n\n";
    auto db = [] (double v) { return v <= -59.9 ? juce::String ("-inf") : (v > 0.05 ? "+" : "") + juce::String (v, 1); };
    auto panText = [] (double p) { return std::abs (p) < 0.01 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (p) * 100.0)); };
    for (const auto& r : results)
    {
        text << r.name << "  (" << automix::roleName (r.role) << "):  ";
        if (fadersBox.getToggleState())
            text << db (r.fromDb) << " -> " << db (r.toDb) << " dB";
        if (panBox.getToggleState())
            text << "   pan " << panText (r.pan);
        text << "\n";
        for (const auto& a : r.added)
            text << "      + " << a << "\n";
    }
    text << "\nUndo (Cmd+Z) puts it all back.";
    report.setText (text);
}

void AutomixPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Genre", 16, 16, 80, 26, juce::Justification::centredLeft);
    g.drawText ("Listen to", 16, 50, 80, 26, juce::Justification::centredLeft);
    g.drawText ("Set", 16, 84, 80, 24, juce::Justification::centredLeft);
}

void AutomixPanel::resized()
{
    genreBox.setBounds (100, 16, getWidth() - 116, 26);
    rangeBox.setBounds (100, 50, getWidth() - 236, 26);
    runButton.setBounds (getWidth() - 126, 50, 110, 26);
    auto row = juce::Rectangle<int> (100, 84, getWidth() - 116, 24);
    fadersBox.setBounds (row.removeFromLeft (80));
    panBox.setBounds (row.removeFromLeft (90));
    pluginsBox.setBounds (row);
    report.setBounds (getLocalBounds().withTrimmedTop (116).reduced (16, 12));
}

} // namespace riff
