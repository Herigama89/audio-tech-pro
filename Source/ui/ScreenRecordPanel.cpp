#include "ScreenRecordPanel.h"
#include "ScreenRecorder.h"
#include "Theme.h"

namespace riff
{

//==============================================================================
juce::String ScreenRecordSettings::toString() const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("wholeScreen", wholeScreen);
    o->setProperty ("maxHeight", maxHeight);
    o->setProperty ("frameRate", frameRate);
    o->setProperty ("withMic", withMic);
    o->setProperty ("micChannel", micChannel);
    o->setProperty ("micStereo", micStereo);
    o->setProperty ("micGainDb", micGainDb);
    o->setProperty ("folder", folder.getFullPathName());
    return juce::JSON::toString (juce::var (o.get()), true);
}

ScreenRecordSettings ScreenRecordSettings::fromString (const juce::String& text)
{
    ScreenRecordSettings s;
    const auto v = juce::JSON::parse (text);
    if (! v.isObject())
    {
        s.folder = defaultFolder();
        return s;
    }

    s.wholeScreen = v.getProperty ("wholeScreen", s.wholeScreen);
    s.maxHeight = v.getProperty ("maxHeight", s.maxHeight);
    s.frameRate = juce::jlimit (15, 120, (int) v.getProperty ("frameRate", s.frameRate));
    s.withMic = v.getProperty ("withMic", s.withMic);
    s.micChannel = juce::jmax (0, (int) v.getProperty ("micChannel", s.micChannel));
    s.micStereo = v.getProperty ("micStereo", s.micStereo);
    s.micGainDb = juce::jlimit (-24.0f, 12.0f, (float) (double) v.getProperty ("micGainDb", s.micGainDb));
    const auto path = v.getProperty ("folder", {}).toString();
    s.folder = juce::File::isAbsolutePath (path) ? juce::File (path) : defaultFolder();
    return s;
}

juce::File ScreenRecordSettings::nextFile (const juce::String& projectName) const
{
    auto base = juce::File::createLegalFileName (projectName.isNotEmpty() ? projectName : juce::String ("Recording"));
    base << " " << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    return (folder == juce::File() ? defaultFolder() : folder).getNonexistentChildFile (base, ".mp4", false);
}

//==============================================================================
ScreenRecordPanel::ScreenRecordPanel (const ScreenRecordSettings& initial, const juce::StringArray& inputNames,
                                      const juce::String& unavailableReason, std::function<void (const ScreenRecordSettings&)> onStart)
    : folder (initial.folder), unavailable (unavailableReason)
{
    for (auto* l : { &captureCaption, &sizeCaption, &rateCaption, &audioCaption, &micCaption, &levelCaption, &folderCaption })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }

    captureBox.addItem ("Audio Tech Pro's windows", 1);
    captureBox.addItem ("The whole screen", 2);
    captureBox.setSelectedId (initial.wholeScreen ? 2 : 1, juce::dontSendNotification);
    captureBox.setTooltip ("The app's windows: just Audio Tech Pro and its plugin windows; everything else stays private.");
    addAndMakeVisible (captureBox);

    int id = 1, selected = 1;
    for (const auto& [label, height] : ScreenRecorder::sizeChoices())
    {
        sizeBox.addItem (label, id);
        if (height == initial.maxHeight)
            selected = id;
        ++id;
    }
    sizeBox.setSelectedId (selected, juce::dontSendNotification);
    sizeBox.setTooltip ("Never larger than your screen: a smaller screen records at its own size.");
    addAndMakeVisible (sizeBox);

    rateBox.addItem ("30 fps", 30);
    rateBox.addItem ("60 fps", 60);
    rateBox.setSelectedId (initial.frameRate >= 45 ? 60 : 30, juce::dontSendNotification);
    addAndMakeVisible (rateBox);

    audioBox.addItem ("The DAW's output", 1);
    audioBox.addItem ("The DAW's output + a mic", 2);
    audioBox.setSelectedId (initial.withMic ? 2 : 1, juce::dontSendNotification);
    audioBox.onChange = [this] { updateEnablement(); };
    addAndMakeVisible (audioBox);

    // Mic: each input alone, and each pair as stereo.
    id = 1;
    selected = 1;
    for (int ch = 0; ch < inputNames.size(); ++ch)
    {
        micBox.addItem ("Input " + juce::String (ch + 1) + ": " + inputNames[ch], id);
        micChoices.add ({ ch, false });
        if (ch == initial.micChannel && ! initial.micStereo)
            selected = id;
        ++id;
    }
    for (int ch = 0; ch + 1 < inputNames.size(); ch += 2)
    {
        micBox.addItem ("Inputs " + juce::String (ch + 1) + "+" + juce::String (ch + 2) + " (stereo)", id);
        micChoices.add ({ ch, true });
        if (ch == initial.micChannel && initial.micStereo)
            selected = id;
        ++id;
    }
    if (inputNames.isEmpty())
        micBox.addItem ("No inputs: turn them on under Audio", 1);
    micBox.setSelectedId (selected, juce::dontSendNotification);
    addAndMakeVisible (micBox);

    micLevel.setSliderStyle (juce::Slider::LinearHorizontal);
    micLevel.setRange (-24.0, 12.0, 0.5);
    micLevel.setValue (initial.micGainDb, juce::dontSendNotification);
    micLevel.setTextValueSuffix (" dB");
    micLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    micLevel.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (micLevel);

    folderLabel.setFont (juce::FontOptions (12.5f));
    folderLabel.setColour (juce::Label::textColourId, theme::text);
    folderLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
    folderLabel.setTooltip (folder.getFullPathName());
    addAndMakeVisible (folderLabel);

    folderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (folderButton);

    help.setText (unavailable.isNotEmpty()
                      ? unavailable
                      : "Records an MP4 (H.264 video, AAC audio). The sound comes straight from the mix, so it's clean; "
                        "the mic is only in the video, not in your speakers. Click the Screen key again to stop. "
                        "The first time, macOS asks to allow screen recording.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, unavailable.isNotEmpty() ? juce::Colour (0xffff8a80) : theme::dimText);
    help.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (help);

    startButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb3261e));
    startButton.setEnabled (unavailable.isEmpty());
    startButton.onClick = [this, onStart]
    {
        if (onStart)
            onStart (getSettings());
    };
    addAndMakeVisible (startButton);

    updateEnablement();
    setSize (480, 350);
}

ScreenRecordSettings ScreenRecordPanel::getSettings() const
{
    ScreenRecordSettings s;
    s.wholeScreen = captureBox.getSelectedId() == 2;
    const auto sizes = ScreenRecorder::sizeChoices();
    s.maxHeight = sizes[juce::jlimit (0, sizes.size() - 1, sizeBox.getSelectedId() - 1)].second;
    s.frameRate = rateBox.getSelectedId() == 30 ? 30 : 60;
    s.withMic = audioBox.getSelectedId() == 2 && ! micChoices.isEmpty();
    const auto index = micBox.getSelectedId() - 1;
    if (juce::isPositiveAndBelow (index, micChoices.size()))
    {
        s.micChannel = micChoices[index].first;
        s.micStereo = micChoices[index].second;
    }
    s.micGainDb = (float) micLevel.getValue();
    s.folder = folder;
    return s;
}

void ScreenRecordPanel::updateEnablement()
{
    const bool mic = audioBox.getSelectedId() == 2;
    micBox.setEnabled (mic && ! micChoices.isEmpty());
    micLevel.setEnabled (mic && ! micChoices.isEmpty());
    micCaption.setAlpha (mic ? 1.0f : 0.5f);
    levelCaption.setAlpha (mic ? 1.0f : 0.5f);
}

void ScreenRecordPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Save screen recordings in", folder, juce::String(), true);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<ScreenRecordPanel> (this)] (const juce::FileChooser& c)
    {
        if (safe == nullptr || c.getResult() == juce::File())
            return;
        safe->folder = c.getResult();
        safe->folderLabel.setText (safe->folder.getFullPathName(), juce::dontSendNotification);
        safe->folderLabel.setTooltip (safe->folder.getFullPathName());
    });
}

void ScreenRecordPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void ScreenRecordPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    constexpr int captionWidth = 90, rowHeight = 26, gap = 9;

    auto row = [&] (juce::Label& caption)
    {
        auto r = area.removeFromTop (rowHeight);
        caption.setBounds (r.removeFromLeft (captionWidth));
        area.removeFromTop (gap);
        return r;
    };

    captureBox.setBounds (row (captureCaption).removeFromLeft (240));
    {
        auto r = row (sizeCaption);
        sizeBox.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (16);
        rateCaption.setBounds (r.removeFromLeft (80));
        rateBox.setBounds (r.removeFromLeft (90));
    }
    audioBox.setBounds (row (audioCaption).removeFromLeft (240));
    micBox.setBounds (row (micCaption).removeFromLeft (300));
    micLevel.setBounds (row (levelCaption).removeFromLeft (300));
    {
        auto r = row (folderCaption);
        folderButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (8);
        folderLabel.setBounds (r);
    }

    startButton.setBounds (area.removeFromBottom (34).removeFromRight (160));
    area.removeFromBottom (10);
    help.setBounds (area);
}

} // namespace riff
