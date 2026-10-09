#include "Assistant.h"
#include "../AppInfo.h"

namespace riff::ai
{

namespace
{
    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& [name, value] : properties)
            o->setProperty (name, value);
        return juce::var (o);
    }

    juce::var textBlock (const juce::String& text) { return object ({ { "type", "text" }, { "text", text } }); }
}

Assistant::Assistant (Transport& t, ToolHost& h)
    : transport (t), host (h)
{
}

juce::String Assistant::systemPrompt (const juce::String& staticContext)
{
    return juce::String()
        + "You are Claude, the assistant built into " + app::name + ", a digital audio workstation (DAW) in the style of Logic Pro "
          "with an analog-console look. The person talking to you is making music in it right now. You can see the project and "
          "change it with the tools you have, and you answer questions about the app and about music production.\n\n"

          "How to work:\n"
          "- When they ask for something you can do with a tool, do it, then say in a sentence or two what you did. Don't ask for "
          "permission for ordinary edits: everything you do in one turn is a single undo step (Cmd+Z), and you can tell them so.\n"
          "- Ask first only when the request is unclear in a way that matters, or before deleting tracks or regions they didn't name.\n"
          "- Refer to tracks by their number and name, as the app shows them. Bars are numbered from 1; the app is in 4/4 unless the "
          "project says otherwise. Positions inside MIDI regions are in beats (quarter notes) from the region's start.\n"
          "- Each message comes with a <project_state> summary. Call get_project when you need more detail (inserts, sends, "
          "console settings), and get_midi before changing notes that are already there.\n"
          "- MIDI: middle C (C4) is note 60. Drum parts use General MIDI drum notes (kick 36, rim 37, snare 38, clap 39, closed hat 42, "
          "pedal hat 44, open hat 46, low tom 45, mid tom 47, high tom 48, crash 49, ride 51). Write musical parts: real chord "
          "voicings, sensible ranges, velocity variation, and rhythm that fits the genre and tempo. For a quick full groove in a "
          "standard genre, generate_pattern is faster; use write_midi for anything specific.\n"
          "- Mix advice: call analyze_mix to measure the tracks (it renders the song offline, which takes a few seconds), and "
          "get_project for the current settings. Base advice on the numbers (peak and RMS in dBFS, and the low/mid/high energy "
          "split), explain it in plain words, and only change settings when they ask you to. Leave headroom on the master "
          "(peaks around -6 to -3 dBFS before mastering) and don't push any track into clipping.\n"
          "- The console strip on each track has HF (8 kHz), HMF (2.5 kHz), LMF (400 Hz) and LF (100 Hz) EQ gains of -15..+15 dB and "
          "a Dynamics amount of 0..10. If a track uses the person's own channel strip plugin (console mode), set_console turns that "
          "plugin's knobs instead. The master has a bus compressor (the built-in one, or their own plugin in console mode).\n"
          "- You can't hear the audio, load or download plugins or samples, record, or change audio hardware settings; say so "
          "briefly when asked, and tell them where to do it in the app.\n"
          "- Keep replies short and practical, like a good studio assistant. Plain text: no markdown headings or tables; a short "
          "list is fine when it helps.\n\n"

          "About the app (its manual):\n"
        + staticContext;
}

juce::String Assistant::buildRequest() const
{
    auto system = textBlock (systemPrompt (host.staticContext()));
    // The system prompt and the tools rarely change: cache them.
    system.getDynamicObject()->setProperty ("cache_control", object ({ { "type", "ephemeral" } }));

    juce::Array<juce::var> systemBlocks { system };
    auto body = object ({ { "model", model },
                          { "max_tokens", maxTokens },
                          { "system", juce::var (systemBlocks) },
                          { "tools", host.toolDefinitions() },
                          { "messages", juce::var (messages) } });
    return juce::JSON::toString (body, juce::JSON::FormatOptions().withSpacing (juce::JSON::Spacing::none).withMaxDecimalPlaces (4));
}

void Assistant::send (const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || busy)
        return;

    trimHistory();
    add (Entry::Kind::user, trimmed);

    turnStartIndex = messages.size();
    const auto content = "<project_state>\n" + host.projectState() + "\n</project_state>\n\n" + trimmed;
    messages.add (object ({ { "role", "user" }, { "content", content } }));

    busy = true;
    rounds = 0;
    host.beginTurn (trimmed);
    request();
}

void Assistant::request()
{
    if (onChange)
        onChange();

    const auto thisGeneration = generation;
    transport.post (buildRequest(), [this, thisGeneration] (int status, const juce::String& reply)
    {
        if (thisGeneration == generation && busy)
            handleReply (status, reply);
    });
}

void Assistant::handleReply (int status, const juce::String& reply)
{
    const auto parsed = juce::JSON::parse (reply);

    if (status != 200 || ! parsed.isObject() || parsed.getProperty ("type", {}).toString() == "error")
    {
        add (Entry::Kind::error, errorMessage (status, reply));

        // Take back the unanswered request so the conversation stays valid for the next one.
        // (Edits already made in this turn stay, and undo as one step.)
        if (rounds == 0)
            messages.removeRange (turnStartIndex, messages.size() - turnStartIndex);
        else
            messages.add (object ({ { "role", "assistant" }, { "content", "(The request failed before I could finish.)" } }));

        finish();
        return;
    }

    juce::Array<juce::var> calls, kept;

    if (auto* blocks = parsed.getProperty ("content", {}).getArray())
    {
        for (const auto& block : *blocks)
        {
            const auto type = block.getProperty ("type", {}).toString();
            if (type == "text")
            {
                const auto text = block.getProperty ("text", {}).toString();
                if (text.trim().isNotEmpty())
                {
                    add (Entry::Kind::assistant, text.trim());
                    kept.add (textBlock (text));
                }
            }
            else if (type == "tool_use")
            {
                // Sent back as it came: each call is answered by its id.
                const auto input = block.getProperty ("input", {});
                kept.add (object ({ { "type", "tool_use" }, { "id", block.getProperty ("id", {}) }, { "name", block.getProperty ("name", {}) },
                                    { "input", input.isObject() ? input : juce::var (new juce::DynamicObject()) } }));
                calls.add (block);
            }
        }
    }

    messages.add (object ({ { "role", "assistant" }, { "content", kept.isEmpty() ? juce::var ("(no reply)") : juce::var (kept) } }));

    const auto stopReason = parsed.getProperty ("stop_reason", {}).toString();

    if (calls.isEmpty())
    {
        if (stopReason == "max_tokens")
            add (Entry::Kind::info, "(The reply was cut off. Ask me to continue.)");
        finish();
        return;
    }

    if (++rounds > maxToolRounds)
    {
        // Tool calls must get results; answer them, and stop here.
        juce::Array<juce::var> results;
        for (const auto& call : calls)
            results.add (object ({ { "type", "tool_result" }, { "tool_use_id", call.getProperty ("id", {}) },
                                   { "content", "Stopped: too many steps in one request." }, { "is_error", true } }));
        messages.add (object ({ { "role", "user" }, { "content", juce::var (results) } }));
        messages.add (object ({ { "role", "assistant" }, { "content", "(I stopped after too many steps.)" } }));
        add (Entry::Kind::info, "Stopped after " + juce::String (maxToolRounds) + " rounds of actions. Ask me to carry on if needed.");
        finish();
        return;
    }

    runTools (calls, 0, {});
}

void Assistant::runTools (juce::Array<juce::var> calls, int index, juce::Array<juce::var> results)
{
    if (index >= calls.size())
    {
        messages.add (object ({ { "role", "user" }, { "content", juce::var (results) } }));
        request();
        return;
    }

    const auto call = calls[index];
    const auto thisGeneration = generation;

    host.runTool (call.getProperty ("name", {}).toString(), call.getProperty ("input", {}),
                  [this, calls, index, results, call, thisGeneration] (ToolResult result) mutable
    {
        if (thisGeneration != generation || ! busy)
            return;

        if (result.forUser.isNotEmpty())
            add (result.isError ? Entry::Kind::error : Entry::Kind::action, result.forUser);

        auto block = object ({ { "type", "tool_result" }, { "tool_use_id", call.getProperty ("id", {}) },
                               { "content", result.forClaude.isNotEmpty() ? result.forClaude : juce::String ("Done.") } });
        if (result.isError)
            block.getDynamicObject()->setProperty ("is_error", true);
        results.add (block);

        runTools (calls, index + 1, results);
    });
}

void Assistant::finish()
{
    busy = false;
    host.endTurn();
    if (onChange)
        onChange();
}

void Assistant::cancel()
{
    if (! busy)
        return;

    ++generation;
    transport.cancel();

    // Forget the unfinished turn so the conversation stays valid (every tool call answered).
    // Anything already done stays in the project; the next message's project state shows it.
    if (messages.size() > turnStartIndex)
        messages.removeRange (turnStartIndex, messages.size() - turnStartIndex);

    add (Entry::Kind::info, "Stopped.");
    finish();
}

void Assistant::clear()
{
    if (busy)
        cancel();
    messages.clear();
    transcript.clear();
    if (onChange)
        onChange();
}

void Assistant::add (Entry::Kind kind, const juce::String& text)
{
    transcript.push_back ({ kind, text });
    if (onChange)
        onChange();
}

void Assistant::trimHistory()
{
    // Keep the conversation from growing without end: drop the oldest turns, cutting
    // only where a typed message (not a tool result) begins, so every call keeps its answer.
    constexpr int maxMessages = 60;
    if (messages.size() <= maxMessages)
        return;

    for (int i = messages.size() - maxMessages; i < messages.size(); ++i)
    {
        const auto m = messages[i];
        if (m.getProperty ("role", {}).toString() == "user" && m.getProperty ("content", {}).isString())
        {
            messages.removeRange (0, i);
            return;
        }
    }
}

} // namespace riff::ai
