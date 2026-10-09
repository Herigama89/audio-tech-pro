#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

/**
    Talking to Claude through Anthropic's Messages API with the user's own key.
    The key never leaves the user's computer except in requests to api.anthropic.com.
*/
namespace riff::ai
{
    inline const juce::String apiUrl { "https://api.anthropic.com/v1/messages" };
    inline const juce::String apiVersion { "2023-06-01" };

    /** The models offered in the settings (the first is the default). Any other model id can be typed in. */
    inline const juce::StringArray modelChoices { "claude-sonnet-5-5", "claude-opus-5-5", "claude-haiku-5-5" };

    /** Sends a request body and calls back on the message thread with the HTTP status (0: no connection) and the reply. */
    class Transport
    {
    public:
        virtual ~Transport() = default;
        virtual void post (const juce::String& jsonBody, std::function<void (int status, const juce::String& reply)> done) = 0;
        virtual void cancel() {}
    };

    /** The real one: HTTPS on a background thread. */
    class HttpTransport final : public Transport
    {
    public:
        /** `apiKey` is asked for on every request, so a changed key takes effect at once. */
        explicit HttpTransport (std::function<juce::String()> apiKey);
        ~HttpTransport() override;

        void post (const juce::String& jsonBody, std::function<void (int, const juce::String&)> done) override;
        void cancel() override;

    private:
        class Request;
        std::function<juce::String()> getKey;
        std::unique_ptr<Request> current;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    };

    /** The error message in an API error reply, or a readable one for the status. */
    juce::String errorMessage (int status, const juce::String& reply);
}
