// Audio devices from the main window: remembering the device, and combining
// several interfaces into one. (Split from MainComponent.cpp.)

#include "MainComponent.h"

namespace riff
{

void MainComponent::saveDeviceState()
{
    if (auto xml = engine.saveDeviceState())
    {
        appProperties.getUserSettings()->setValue ("audioDevice", xml.get());
        appProperties.saveIfNeeded();
    }
    updateInputLabels();
}

void MainComponent::updateInputLabels()
{
    if (aggregateSetup.isActive())
        engine.setInputChannelLabels (aggregate::deviceName,
                                      aggregate::channelLabels (aggregateSetup, aggregate::listInterfaces(), true));
    else
        engine.setInputChannelLabels ({}, {});
    headers.repaint();
}

juce::String MainComponent::useAudioDevice (const juce::String& name, int numInputsToEnable)
{
    auto& dm = engine.getDeviceManager();
    if (auto* type = dm.getCurrentDeviceTypeObject())
        type->scanForDevices();   // a new combined device appears here

    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = name;
    setup.outputDeviceName = name;

    if (numInputsToEnable > 0)
    {
        // Every input on, so any of them can be recorded; playback on outputs 1-2.
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, numInputsToEnable, true);
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, 2, true);
    }
    else
    {
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
    }

    return dm.setAudioDeviceSetup (setup, true);
}

void MainComponent::enableHardwareOutputs (int upTo)
{
    // Every output up to `upTo` switched on (the callback only carries the ones that are), keeping any already on
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    upTo = juce::jmin (upTo, device->getOutputChannelNames().size(), AudioEngine::maxHardwareOutputs);
    const auto active = device->getActiveOutputChannels();
    bool allOn = true;
    for (int ch = 0; ch < upTo; ++ch)
        allOn = allOn && active[ch];
    if (allOn)
        return;

    auto setup = dm.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels = active;
    setup.outputChannels.setRange (0, upTo, true);
    if (const auto error = dm.setAudioDeviceSetup (setup, true); error.isNotEmpty())
        DBG ("Couldn't switch the outputs on: " << error);
    else
        saveDeviceState();
}

juce::String MainComponent::combineInterfaces (const aggregate::Setup& setup)
{
    if (engine.isRecording())
        return "Stop recording first.";

    engine.stop();

    if (auto error = aggregate::create (setup); error.isNotEmpty())
        return error;

    const auto interfaces = aggregate::listInterfaces();
    const auto inputs = aggregate::channelLabels (setup, interfaces, true);

    if (auto error = useAudioDevice (aggregate::deviceName, inputs.size()); error.isNotEmpty())
        return "The interfaces were combined, but the combined device didn't open: " + error;

    aggregateSetup = setup;
    appProperties.getUserSettings()->setValue ("combinedInterfaces", setup.toString());
    saveDeviceState();

    juce::StringArray names;
    for (const auto& uid : setup.orderedUids())
        for (const auto& i : interfaces)
            if (i.uid == uid)
                names.add (i.name);
    statusLabel.setText ("Combined " + names.joinIntoString (" + ") + ": " + juce::String (inputs.size()) + " inputs",
                         juce::dontSendNotification);
    return {};
}

void MainComponent::separateInterfaces()
{
    if (engine.isRecording())
        return;

    engine.stop();

    // Back to the clock interface on its own, then remove the combined device.
    juce::String clockName;
    for (const auto& i : aggregate::listInterfaces())
        if (i.uid == aggregateSetup.orderedUids()[0])
            clockName = i.name;

    aggregateSetup = {};
    appProperties.getUserSettings()->setValue ("combinedInterfaces", juce::String());

    if (clockName.isNotEmpty())
        useAudioDevice (clockName, 0);
    aggregate::destroy();

    if (engine.getDeviceManager().getCurrentAudioDevice() == nullptr)
        useAudioDevice ({}, 0);   // the system default

    saveDeviceState();
    statusLabel.setText (deviceStatusText(), juce::dontSendNotification);
}

void MainComponent::showCombineInterfaces()
{
    if (combineWindow != nullptr)
    {
        combineWindow->toFront (true);
        return;
    }

    const bool combinedNow = aggregateSetup.isActive()
                             && engine.getDeviceManager().getCurrentAudioDevice() != nullptr
                             && engine.getDeviceManager().getCurrentAudioDevice()->getName() == aggregate::deviceName;

    auto close = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->combineWindow.reset();
        });
    };

    auto* panel = new CombineInterfacesPanel (aggregateSetup, combinedNow,
                                              [] { return aggregate::listInterfaces(); },
                                              [this, close] (const aggregate::Setup& setup)
                                              {
                                                  if (const auto error = combineInterfaces (setup); error.isNotEmpty())
                                                      showError ("Couldn't combine the interfaces", error);
                                                  else
                                                      close();
                                              },
                                              [this, close]
                                              {
                                                  separateInterfaces();
                                                  close();
                                              });

    combineWindow = std::make_unique<EditorWindow> ("Combine Audio Interfaces", panel, false, close);
}

} // namespace riff
