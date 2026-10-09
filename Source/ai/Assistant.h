#pragma once

#include <JuceHeader.h>
#include "ClaudeApi.h"
#include <vector>

/**
    The conversation with Claude inside the app: sends what you type with the
    project's state, runs the actions Claude asks for (tools), sends their
    results back, and repeats until Claude has answered.
*/
namespace riff::ai
{
    struct ToolResult
    {
        juce::String forClaude;     // what Claude reads back (often JSON)
        juce::String forUser;       // one line shown in the chat, e.g. "Set the tempo to 92 BPM" (empty: nothing shown)
        bool isError = false;
    };

    /** What Claude can see and do in the app. */
    class ToolHost
    {
    public:
        virtual ~ToolHost() = default;

        /** The tools, as the Messages API's `tools` array. */
        virtual juce::var toolDefinitions() = 0;

        /** Runs a tool and calls `done` (on the message thread; now or later). */
        virtual void runTool (const juce::String& name, const juce::var& input, std::function<void (ToolResult)> done) = 0;

        /** Extra, slowly changing context for the system prompt (help text, the key commands). */
        virtual juce::String staticContext() = 0;

        /** A short description of the project now, sent with each message. */
        virtual juce::String projectState() = 0;

        /** Around each request of yours: every edit in between becomes one undo step. */
        virtual void beginTurn (const juce::String& request) = 0;
        virtual void endTurn() = 0;
    };

    class Assistant
    {
    public:
        Assistant (Transport& transport, ToolHost& host);

        struct Entry
        {
            enum class Kind { user, assistant, action, error, info };
            Kind kind;
            juce::String text;
        };

        void send (const juce::String& text);
        void cancel();
        void clear();

        bool isBusy() const noexcept { return busy; }
        const std::vector<Entry>& getTranscript() const noexcept { return transcript; }

        juce::String model = modelChoices[0];
        int maxTokens = 4096;
        static constexpr int maxToolRounds = 16;

        /** Called whenever the transcript or the busy state changes. */
        std::function<void()> onChange;

        /** The request body for the conversation so far (public for tests). */
        juce::String buildRequest() const;

        static juce::String systemPrompt (const juce::String& staticContext);

    private:
        void request();
        void handleReply (int status, const juce::String& reply);
        void runTools (juce::Array<juce::var> calls, int index, juce::Array<juce::var> results);
        void finish();
        void add (Entry::Kind kind, const juce::String& text);
        void trimHistory();

        Transport& transport;
        ToolHost& host;
        juce::Array<juce::var> messages;
        std::vector<Entry> transcript;
        bool busy = false;
        int rounds = 0;
        int turnStartIndex = 0;      // where the current turn's messages begin (to roll back after an error)
        juce::uint32 generation = 0; // replies from a cancelled turn are ignored
    };
}
