#include "ClaudeApi.h"

namespace riff::ai
{

class HttpTransport::Request final : public juce::Thread
{
public:
    Request (juce::String key, juce::String body, std::function<void (int, const juce::String&)> callback,
             std::shared_ptr<std::atomic<bool>> ownerAlive)
        : Thread ("Claude request"), apiKey (std::move (key)), json (std::move (body)),
          done (std::move (callback)), alive (std::move (ownerAlive))
    {
    }

    ~Request() override
    {
        cancelled = true;
        {
            const juce::ScopedLock sl (streamLock);
            if (stream != nullptr)
                stream->cancel();
        }
        stopThread (4000);
    }

    void cancel()
    {
        cancelled = true;
        const juce::ScopedLock sl (streamLock);
        if (stream != nullptr)
            stream->cancel();
    }

    void run() override
    {
        int status = 0;
        juce::String reply;

        {
            auto s = std::make_unique<juce::WebInputStream> (juce::URL (apiUrl).withPOSTData (json), true);
            s->withExtraHeaders ("x-api-key: " + apiKey + "\r\n"
                                 "anthropic-version: " + apiVersion + "\r\n"
                                 "content-type: application/json")
              .withConnectionTimeout (120000)
              .withNumRedirectsToFollow (2);

            {
                const juce::ScopedLock sl (streamLock);
                stream = s.get();
            }

            if (! cancelled && s->connect (nullptr))
            {
                status = s->getStatusCode();
                reply = s->readEntireStreamAsString();
            }
            else
            {
                status = s->getStatusCode();
            }

            const juce::ScopedLock sl (streamLock);
            stream = nullptr;
        }

        if (cancelled)
            return;

        juce::MessageManager::callAsync ([status, reply, cb = done, flag = alive]
        {
            if (flag->load() && cb)
                cb (status, reply);
        });
    }

private:
    juce::String apiKey, json;
    std::function<void (int, const juce::String&)> done;
    std::shared_ptr<std::atomic<bool>> alive;
    std::atomic<bool> cancelled { false };
    juce::CriticalSection streamLock;
    juce::WebInputStream* stream = nullptr;
};

HttpTransport::HttpTransport (std::function<juce::String()> apiKey)
    : getKey (std::move (apiKey))
{
}

HttpTransport::~HttpTransport()
{
    alive->store (false);
    current.reset();
}

void HttpTransport::post (const juce::String& jsonBody, std::function<void (int, const juce::String&)> done)
{
    // One request at a time: a new one replaces any still running. Its reply is
    // delivered only if this request is still the current one.
    current.reset();
    auto requestAlive = std::make_shared<std::atomic<bool>> (true);
    alive->store (false);
    alive = requestAlive;

    current = std::make_unique<Request> (getKey ? getKey() : juce::String(), jsonBody, std::move (done), requestAlive);
    current->startThread();
}

void HttpTransport::cancel()
{
    alive->store (false);
    if (current != nullptr)
        current->cancel();
}

juce::String errorMessage (int status, const juce::String& reply)
{
    const auto parsed = juce::JSON::parse (reply);
    if (auto* error = parsed.getProperty ("error", {}).getDynamicObject())
    {
        const auto message = error->getProperty ("message").toString();
        if (message.isNotEmpty())
        {
            if (status == 401)
                return "Your API key wasn't accepted (" + message + "). Check it in the assistant's settings.";
            return message;
        }
    }

    switch (status)
    {
        case 0:   return "Couldn't reach api.anthropic.com. Check your internet connection.";
        case 401: return "Your API key wasn't accepted. Check it in the assistant's settings.";
        case 403: return "Your API key doesn't have access to this. Check your Anthropic Console account.";
        case 404: return "That model wasn't found. Choose another model in the assistant's settings.";
        case 429: return "Too many requests or no credit left. Wait a moment, or check your Anthropic Console billing.";
        case 529: return "Claude is overloaded right now. Try again in a moment.";
        default:  return "The request failed (HTTP " + juce::String (status) + ").";
    }
}

} // namespace riff::ai
