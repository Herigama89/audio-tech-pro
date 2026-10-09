#include "AssistantPanel.h"
#include "Theme.h"
#include "ConsoleLook.h"

namespace riff
{

namespace
{
    using Kind = ai::Assistant::Entry::Kind;

    constexpr int margin = 12, bubblePad = 9, gap = 8;

    juce::Font fontFor (Kind kind)
    {
        if (kind == Kind::action)
            return juce::Font (juce::FontOptions (12.5f));
        if (kind == Kind::info)
            return juce::Font (juce::FontOptions (12.5f, juce::Font::italic));
        return juce::Font (juce::FontOptions (14.0f));
    }

    juce::String displayText (const ai::Assistant::Entry& e)
    {
        if (e.kind == Kind::action)
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93  ")) + e.text;   // check mark
        return e.text;
    }
}

//==============================================================================
/** The conversation: bubbles for you, plain text for Claude, small lines for what it did. */
class AssistantPanel::Transcript final : public juce::Component
{
public:
    explicit Transcript (ai::Assistant& a) : assistant (a) {}

    bool thinking = false;
    int dots = 0;
    juce::String emptyText;

    void layout (int width)
    {
        layouts.clear();
        int y = margin;
        const auto& entries = assistant.getTranscript();

        for (const auto& e : entries)
        {
            const bool bubble = e.kind == Kind::user;
            const auto textWidth = (float) (width - 2 * margin - (bubble ? 2 * bubblePad + 40 : 0));

            juce::AttributedString s;
            s.setWordWrap (juce::AttributedString::byWord);
            s.append (displayText (e), fontFor (e.kind), colourFor (e.kind));

            Laid laid;
            laid.layout.createLayout (s, juce::jmax (40.0f, textWidth));
            const auto h = (int) std::ceil (laid.layout.getHeight());
            float widest = 0.0f;
            for (int i = 0; i < laid.layout.getNumLines(); ++i)
                widest = juce::jmax (widest, laid.layout.getLine (i).getLineBoundsX().getEnd());
            const auto w = (int) std::ceil (juce::jmin (widest + 1.0f, textWidth));

            if (bubble)
                laid.box = { width - margin - w - 2 * bubblePad, y, w + 2 * bubblePad, h + 2 * bubblePad };
            else
                laid.box = { margin, y, (int) textWidth, h };

            laid.kind = e.kind;
            y = laid.box.getBottom() + (e.kind == Kind::action ? 4 : gap + 2);
            layouts.push_back (std::move (laid));
        }

        thinkingY = y;
        setSize (width, juce::jmax (y + (thinking ? 30 : 0) + margin, getParentHeight()));
    }

    int getParentHeight() const { return getParentComponent() != nullptr ? getParentComponent()->getHeight() : 0; }

    void paint (juce::Graphics& g) override
    {
        if (layouts.empty() && ! thinking)
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (13.5f));
            g.drawFittedText (emptyText, getLocalBounds().reduced (24, 40).withHeight (220), juce::Justification::centredTop, 12);
            return;
        }

        for (const auto& l : layouts)
        {
            auto box = l.box.toFloat();
            if (l.kind == Kind::user)
            {
                g.setColour (theme::cream);
                g.fillRoundedRectangle (box, 10.0f);
                l.layout.draw (g, box.reduced ((float) bubblePad));
            }
            else
            {
                if (l.kind == Kind::error)
                {
                    g.setColour (theme::danger.withAlpha (0.14f));
                    g.fillRoundedRectangle (box.expanded (6.0f, 4.0f), 6.0f);
                }
                l.layout.draw (g, box);
            }
        }

        if (thinking)
        {
            g.setColour (theme::gold);
            g.setFont (juce::FontOptions (13.0f, juce::Font::italic));
            g.drawText ("Thinking" + juce::String::repeatedString (".", 1 + dots % 3),
                        juce::Rectangle<int> (margin, thinkingY, 200, 22), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return;

        const auto& entries = assistant.getTranscript();
        for (size_t i = 0; i < layouts.size() && i < entries.size(); ++i)
            if (layouts[i].box.expanded (4).contains (e.getPosition()))
            {
                const auto text = entries[i].text;
                juce::PopupMenu menu;
                menu.addItem ("Copy", [text] { juce::SystemClipboard::copyTextToClipboard (text); });
                menu.addItem ("Copy the whole conversation", [this] { juce::SystemClipboard::copyTextToClipboard (wholeConversation()); });
                menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
                return;
            }
    }

private:
    struct Laid
    {
        juce::TextLayout layout;
        juce::Rectangle<int> box;
        Kind kind = Kind::assistant;
    };

    static juce::Colour colourFor (Kind kind)
    {
        switch (kind)
        {
            case Kind::user:      return theme::ink;
            case Kind::assistant: return theme::text;
            case Kind::action:    return theme::meterGreen.withAlpha (0.9f);
            case Kind::error:     return juce::Colour (0xffff8a80);
            case Kind::info:      return theme::dimText;
        }
        return theme::text;
    }

    juce::String wholeConversation() const
    {
        juce::String text;
        for (const auto& e : assistant.getTranscript())
            text << (e.kind == Kind::user ? "You: " : e.kind == Kind::assistant ? "Claude: " : "  ") << e.text << "\n\n";
        return text.trimEnd();
    }

    ai::Assistant& assistant;
    std::vector<Laid> layouts;
    int thinkingY = 0;
};

//==============================================================================
AssistantPanel::AssistantPanel (ai::Assistant& a)
    : assistant (a), transcript (std::make_unique<Transcript> (a))
{
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::cream);
    addAndMakeVisible (title);

    modelLabel.setFont (juce::FontOptions (11.5f));
    modelLabel.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (modelLabel);

    for (auto* b : { &settingsButton, &newChatButton, &closeButton })
    {
        look::key (*b, look::Key::dark);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    newChatButton.setTooltip ("Start a new conversation (Claude forgets this one)");
    newChatButton.onClick = [this] { assistant.clear(); refresh(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };

    transcript->emptyText = "Ask me anything about your project, or tell me what to do:\n\n"
                            "\"Set the tempo to 92 and loop bars 5 to 8\"\n"
                            "\"Write a sad piano progression in F minor, 8 bars\"\n"
                            "\"Make me a trap beat in C minor\"\n"
                            "\"Analyze my mix. What would you change?\"\n"
                            "\"How do I record several takes?\"\n\n"
                            "Everything I change in one go is one Undo.";
    viewport.setViewedComponent (transcript.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    input.setMultiLine (true, true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Message Claude... (Return sends)", theme::dimText);
    input.setFont (juce::FontOptions (14.0f));
    input.setColour (juce::TextEditor::backgroundColourId, theme::lcd);
    input.setColour (juce::TextEditor::textColourId, theme::text);
    input.setColour (juce::TextEditor::outlineColourId, theme::outline);
    input.setColour (juce::TextEditor::focusedOutlineColourId, theme::gold);
    input.setBorder (juce::BorderSize<int> (6));
    input.onReturnKey = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isShiftDown())
            input.insertTextAtCaret ("\n");
        else
            sendInput();
    };
    addAndMakeVisible (input);

    for (auto* b : { &sendButton, &stopButton, &setupButton })
    {
        b->setWantsKeyboardFocus (false);
        addChildComponent (b);
    }
    sendButton.setColour (juce::TextButton::buttonColourId, theme::goldDark);
    sendButton.onClick = [this] { sendInput(); };
    stopButton.setColour (juce::TextButton::buttonColourId, theme::danger.darker (0.3f));
    stopButton.onClick = [this] { assistant.cancel(); };
    setupButton.setColour (juce::TextButton::buttonColourId, theme::goldDark);
    setupButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    sendButton.setVisible (true);

    refresh();
}

AssistantPanel::~AssistantPanel()
{
    viewport.setViewedComponent (nullptr, false);
}

void AssistantPanel::sendInput()
{
    if (hasApiKey && ! hasApiKey())
    {
        if (onOpenSettings)
            onOpenSettings();
        return;
    }

    const auto text = input.getText().trim();
    if (text.isEmpty() || assistant.isBusy())
        return;

    input.clear();
    assistant.send (text);
    refresh();
}

void AssistantPanel::focusInput()
{
    if (isShowing())
        input.grabKeyboardFocus();
}

void AssistantPanel::refresh()
{
    const bool keySet = ! hasApiKey || hasApiKey();
    const bool busy = assistant.isBusy();

    modelLabel.setText (keySet ? assistant.model : juce::String ("No API key yet"), juce::dontSendNotification);
    setupButton.setVisible (! keySet && assistant.getTranscript().empty());
    sendButton.setVisible (! busy);
    stopButton.setVisible (busy);
    input.setEnabled (keySet);

    transcript->thinking = busy;
    if (busy && ! isTimerRunning())
        startTimer (400);
    else if (! busy)
        stopTimer();

    const auto wasAtBottom = viewport.getViewPositionY() + viewport.getViewHeight() >= transcript->getHeight() - 30;
    transcript->layout (juce::jmax (100, viewport.getMaximumVisibleWidth()));
    transcript->repaint();
    if (wasAtBottom || busy)
        viewport.setViewPosition (0, juce::jmax (0, transcript->getHeight() - viewport.getViewHeight()));

    resized();
}

void AssistantPanel::timerCallback()
{
    transcript->dots = ++dots;
    transcript->repaint();
}

void AssistantPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background.darker (0.15f));
    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromTop (44));
    g.setColour (theme::outline);
    g.fillRect (0, 0, 1, getHeight());
    g.fillRect (0, 44, getWidth(), 1);
}

void AssistantPanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (1);

    auto header = area.removeFromTop (44).reduced (10, 8);
    closeButton.setBounds (header.removeFromRight (54));
    header.removeFromRight (6);
    newChatButton.setBounds (header.removeFromRight (74));
    header.removeFromRight (6);
    settingsButton.setBounds (header.removeFromRight (70));
    title.setBounds (header.removeFromTop (18).withTrimmedLeft (-2));
    modelLabel.setBounds (header);

    auto bottom = area.removeFromBottom (96).reduced (10, 10);
    auto buttons = bottom.removeFromRight (70);
    sendButton.setBounds (buttons.removeFromBottom (32));
    stopButton.setBounds (sendButton.getBounds());
    bottom.removeFromRight (8);
    input.setBounds (bottom);

    viewport.setBounds (area);
    setupButton.setBounds (area.withSizeKeepingCentre (180, 34).withY (area.getY() + 250));

    const auto width = juce::jmax (100, viewport.getMaximumVisibleWidth());
    if (transcript->getWidth() != width)
        transcript->layout (width);
}

//==============================================================================
AssistantSettingsPanel::AssistantSettingsPanel (const juce::String& apiKey, const juce::String& model,
                                                std::function<void (const juce::String&, const juce::String&)> onSave)
{
    for (auto* l : { &keyCaption, &modelCaption })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }

    keyEditor.setText (apiKey, false);
    keyEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    keyEditor.setTextToShowWhenEmpty ("sk-ant-...", theme::dimText);
    keyEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (keyEditor);

    showButton.setClickingTogglesState (true);
    showButton.onClick = [this]
    {
        keyEditor.setPasswordCharacter (showButton.getToggleState() ? 0 : (juce::juce_wchar) 0x2022);
        showButton.setButtonText (showButton.getToggleState() ? "Hide" : "Show");
    };
    addAndMakeVisible (showButton);

    modelBox.setEditableText (true);
    modelBox.addItemList (ai::modelChoices, 1);
    modelBox.setText (model.isNotEmpty() ? model : ai::modelChoices[0], juce::dontSendNotification);
    modelBox.setTooltip ("Sonnet: fast and capable (recommended). Opus: the most capable, slower and costs more. "
                         "Haiku: quickest and cheapest. You can also type any other model id.");
    addAndMakeVisible (modelBox);

    addAndMakeVisible (consoleLink);

    help.setText ("The assistant uses your own Anthropic API key, and its use is billed to your Anthropic account. "
                  "The key is kept on this computer in Audio Tech Pro's settings and is only sent to api.anthropic.com. "
                  "Each message sends Claude a description of your project (track names, settings, notes) so it can help; "
                  "your audio is never uploaded.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, theme::dimText);
    help.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (help);

    saveButton.onClick = [this, onSave]
    {
        if (onSave)
            onSave (keyEditor.getText().trim(), modelBox.getText().trim());
    };
    addAndMakeVisible (saveButton);

    setSize (500, 270);
}

void AssistantSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void AssistantSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    constexpr int captionWidth = 70;

    auto row = area.removeFromTop (28);
    keyCaption.setBounds (row.removeFromLeft (captionWidth));
    showButton.setBounds (row.removeFromRight (56));
    row.removeFromRight (6);
    keyEditor.setBounds (row);
    area.removeFromTop (4);
    consoleLink.setBounds (area.removeFromTop (22).withTrimmedLeft (captionWidth - 6).withWidth (300));
    consoleLink.setJustificationType (juce::Justification::centredLeft);
    area.removeFromTop (8);

    row = area.removeFromTop (28);
    modelCaption.setBounds (row.removeFromLeft (captionWidth));
    modelBox.setBounds (row.removeFromLeft (220));

    area.removeFromTop (12);
    saveButton.setBounds (area.removeFromBottom (30).removeFromRight (100));
    area.removeFromBottom (8);
    help.setBounds (area);
}

} // namespace riff
