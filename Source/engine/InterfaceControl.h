#pragma once

#include <JuceHeader.h>
#include <map>
#include <mutex>

namespace riff
{

/**
    The audio interface's own controls from each track, as Logic's Audio Device Controls: a channel's mic
    preamp - gain, 48 V, the high-pass filter, the pad, Auto Gain - and its clip light.

    For interfaces whose preamps are set by their maker's control app (which runs a small service on the
    computer that its own app and DAW integrations talk to): we talk to that service the same way, on the
    computer itself (127.0.0.1). Nothing goes over the network. The service has to be installed and running.

    The protocol (framing, the device session, normalized parameter values and the device's parameter
    paths such as "line/ch1/preampgain") follows the open, MIT-licensed reference by João Paulo Faria
    (github.com/jpfaria/quantum-hd8), measured against the real service.
*/
namespace ucnet
{
    struct Message { juce::String code; std::array<juce::uint8, 4> cbytes {}; juce::MemoryBlock payload; };

    /** One frame: "UC" 00 01, size (uint16 LE: 6 + payload), the 2-letter code, the session's 4 bytes, the payload. */
    juce::MemoryBlock encode (const juce::String& code, const juce::MemoryBlock& payload, std::array<juce::uint8, 4> cbytes);

    /** Collects bytes as they arrive and gives back whole frames (anything before a frame's start is skipped). */
    class Decoder
    {
    public:
        std::vector<Message> feed (const void* data, size_t size);
    private:
        juce::MemoryBlock buffer;
    };

    juce::MemoryBlock jsonPayload (const juce::var& json);
    juce::var parseJson (const Message&);
    /** A parameter write or event: path, 00, a 16-bit flag (0), the value as a float32 normalized 0..1. */
    juce::MemoryBlock pvPayload (const juce::String& path, float normalized);
    bool parsePv (const Message&, juce::String& path, float& normalized);
    /** The device's state (ZM/ZB): a uint32, then a zlib stream of JSON. */
    juce::var parseState (const Message&);

    /** Every value and range in a state tree, by path ("line/ch1/preampgain"). */
    void flatten (const juce::var& node, const juce::String& prefix, std::map<juce::String, juce::var>& values,
                  std::map<juce::String, juce::var>& ranges);
}

class InterfaceControl final : private juce::Thread
{
public:
    InterfaceControl();
    ~InterfaceControl() override;

    /** Starts looking for the interface's control service (and keeps the connection). */
    void start();
    void stop();

    bool isConnected() const noexcept { return connected.load(); }

    /** A channel's preamp (1-based, the interface's input number), as the device last reported it. */
    struct Preamp
    {
        bool present = false;          // this input has a preamp the service controls
        float gainDb = 0.0f, minDb = 0.0f, maxDb = 75.0f;
        bool phantom = false, highPass = false, pad = false, autoGain = false, clip = false;
        bool hasPad = false, hasHighPass = false, hasAutoGain = false;
    };
    Preamp preamp (int channel) const;
    int preampCount() const;

    void setGain (int channel, float db);
    void setPhantom (int channel, bool on)  { setToggle (channel, "48v", on); }
    void setHighPass (int channel, bool on) { setToggle (channel, "hpf", on); }
    void setPad (int channel, bool on)      { setToggle (channel, "pad", on); }
    void setAutoGain (int channel, bool on) { setToggle (channel, "autogain", on); }

    /** Called on the message thread when the connection or a preamp changed. */
    std::function<void()> onChange;

    /** How the service's listening ports are found (tests point it at their own). */
    std::function<juce::Array<int>()> findPorts;

    /** Shows a device's state as if connected (for previews: the snapshot tool). */
    void preview (const juce::var& stateTree);

    /** Where the last place it answered is kept (tests use their own). */
    juce::File cacheFile;

    /** For tests: the state as values by path. */
    float valueOf (const juce::String& path, float fallback = 0.0f) const;

private:
    void run() override;
    bool connect();
    bool tryConnect (int port, juce::uint8 session, int timeoutMs);
    void handle (const ucnet::Message&);
    void setToggle (int channel, const char* what, bool on);
    void queueWrite (const juce::String& path, float normalized);
    void changed();
    static juce::Array<int> defaultPorts();
    juce::File sessionCache() const;

    std::unique_ptr<juce::StreamingSocket> socket;
    ucnet::Decoder decoder;
    std::array<juce::uint8, 4> deviceSession { 0x6a, 0, 0x69, 0 };
    int port = 59791;
    std::atomic<bool> connected { false };

    mutable std::mutex lock;
    std::map<juce::String, juce::var> values, ranges;
    std::vector<std::pair<juce::String, float>> pending;
    juce::uint32 lastKeepAlive = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace riff
