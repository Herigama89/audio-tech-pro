#pragma once

#include <JuceHeader.h>
#include "ai/Assistant.h"

namespace riff
{

/**
    The Claude panel on the right of the window: the conversation, a box to
    type in, and the settings (your API key and the model).
*/
class AssistantPanel final : public juce::Component,
                             private juce::Timer
{
public:
    AssistantPanel (ai::Assistant& assistant);
    ~AssistantPanel() override;

    /** Whether an API key is set; without one the panel asks for it. */
    std::function<bool()> hasApiKey;
    std::function<void()> onOpenSettings;
    std::function<void()> onClose;

    /** Call after the transcript changes. */
    void refresh();
    void focusInput();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 380;

private:
    class Transcript;
    void sendInput();
    void timerCallback() override;

    ai::Assistant& assistant;
    juce::Label title { {}, "Claude" }, modelLabel;
    juce::TextButton settingsButton { "Settings" }, newChatButton { "New chat" }, closeButton { "Close" };
    std::unique_ptr<Transcript> transcript;
    juce::Viewport viewport;
    juce::TextEditor input;
    juce::TextButton sendButton { "Send" }, stopButton { "Stop" }, setupButton { "Add your API key..." };
    int dots = 0;
};

/** The assistant's settings: the API key and the model. */
class AssistantSettingsPanel final : public juce::Component
{
public:
    AssistantSettingsPanel (const juce::String& apiKey, const juce::String& model,
                            std::function<void (const juce::String& key, const juce::String& model)> onSave);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label keyCaption { {}, "API key" }, modelCaption { {}, "Model" }, help;
    juce::TextEditor keyEditor;
    juce::ComboBox modelBox;
    juce::HyperlinkButton consoleLink { "Get an API key at console.anthropic.com", juce::URL ("https://console.anthropic.com/settings/keys") };
    juce::TextButton saveButton { "Save" }, showButton { "Show" };
};

} // namespace riff
