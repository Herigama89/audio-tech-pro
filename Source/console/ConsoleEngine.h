#pragma once

#include <JuceHeader.h>
#include "../engine/InterfaceControl.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Audio Tech Pro Console: a real-time input console for any audio interface, in the spirit of an
    Apollo's Console with Unison preamps. Every input of the interface gets a channel strip:

        input -> trim / polarity -> PRE slot (the "input stage" plugin) -> 3 inserts
              -> direct out (pre-fader, for recording the processed signal)
              -> cue sends A/B (pre-fader) and FX send (post-fader)
              -> pan + fader -> Main

    plus an FX bus (a reverb for the singer's headphones), two cue (headphone) mixes and the Main
    monitor mix, each with its own inserts and output pair.

    Threads: the audio callback holds `lock` while it processes (it only ever *tries* it, and plays
    silence for a block if it can't get it). The message thread takes `lock` only for the moment it
    changes the structure (adds a strip, swaps a plugin): plugins are created, prepared and deleted
    outside it. Knobs and faders are atomics, read by the audio thread every block.
*/
namespace atpconsole
{

constexpr int numSlots = 4;            // slot 0: PRE (input stage) on input strips; then 3 inserts
constexpr int numSends = 3;            // cue A, cue B, FX
constexpr int maxChannels = 8;         // channels a strip's buffer has (for plugins with side-chains)
constexpr float minusInfinityDb = -80.0f;

enum class Kind { input, fx, cue, main };
enum Send { sendCueA = 0, sendCueB = 1, sendFx = 2 };

struct Slot
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;   // swapped only under the engine's lock
    std::atomic<bool> bypass { false };
};

struct Meter
{
    std::atomic<float> left { 0.0f }, right { 0.0f };
    std::atomic<bool> clip { false };

    void push (float l, float r) noexcept
    {
        if (l > left.load (std::memory_order_relaxed))  left.store (l, std::memory_order_relaxed);
        if (r > right.load (std::memory_order_relaxed)) right.store (r, std::memory_order_relaxed);
    }
};

/** A channel strip or a bus. */
struct Channel
{
    explicit Channel (Kind k) : kind (k)
    {
        for (auto& s : sendDb) s.store (minusInfinityDb);
    }

    const Kind kind;

    // Set on the message thread (the audio thread never reads these)
    juce::String name, sourceName;
    juce::Colour colour { 0xfff5a742 };

    // Controls (any thread)
    std::atomic<float> trimDb { 0.0f }, faderDb { 0.0f }, pan { 0.0f };   // pan -1 (L) .. +1 (R)
    std::atomic<bool> mute { false }, solo { false }, polarity { false };
    std::array<std::atomic<float>, numSends> sendDb;
    std::atomic<float> fxReturnDb { minusInfinityDb };   // cue buses: how much of the FX bus they get
    std::atomic<int> output { -1 };                      // input strips: direct out; buses: first output channel (-1 none)

    std::array<Slot, numSlots> slots;
    Meter meter;

    // Structure (changed only under the engine's lock)
    int input = -1;             // input strips: the (active) input channel it reads
    bool stereo = false;        // input strips: reads `input` and `input + 1`
    bool visible = true;        // false for the second input of a stereo strip

    // Audio-thread state
    juce::AudioBuffer<float> buffer;
    float lastTrim = 1.0f, lastMainL = 0.0f, lastMainR = 0.0f, lastFxRet = 0.0f;
    std::array<std::array<float, 2>, numSends> lastSend {};
};

class Engine final : public juce::AudioIODeviceCallback,
                     private juce::ChangeListener
{
public:
    Engine();
    ~Engine() override;

    /** Opens the audio interface (the saved one, or the system default). */
    void start (const juce::XmlElement* savedDevice);
    void shutdown();

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    riff::InterfaceControl hardware;        // PreSonus Universal Control preamps (gain, 48 V), when its service runs

    // ---- structure (message thread) --------------------------------------------------------------
    int numStrips() const noexcept                  { return (int) strips.size(); }
    Channel& strip (int i)                          { return *strips[(size_t) i]; }
    Channel& bus (Kind k);                          // fx, cue A (index 0), main
    Channel& cue (int index)                        { return *cues[(size_t) index]; }
    juce::StringArray getOutputNames() const        { return outputNames; }

    void setStereo (int stripIndex, bool stereo);

    /** Creates a plugin, ready to run at the current rate (or returns nullptr and sets error). */
    std::unique_ptr<juce::AudioPluginInstance> createPlugin (const juce::PluginDescription&, juce::String& error);
    void setPlugin (Channel&, int slot, std::unique_ptr<juce::AudioPluginInstance>);

    /** Called (message thread) just before a plugin is deleted, so its editor window can be closed. */
    std::function<void (juce::AudioProcessor*)> onPluginWillBeDeleted;
    /** Called (message thread) when strips or outputs changed (another device, other channels). */
    std::function<void()> onLayoutChanged;

    // ---- talkback --------------------------------------------------------------------------------
    std::atomic<bool> talkback { false };
    std::atomic<int> talkbackInput { -1 };

    // ---- read-outs (message thread) --------------------------------------------------------------
    double getSampleRate() const noexcept    { return sampleRate; }
    int getBlockSize() const noexcept        { return blockSize; }
    double getRoundTripMs() const;
    int getXRuns() const;
    int countPlugins() const;
    int maxPluginLatencySamples() const;
    juce::String getDeviceName() const;

    // ---- sessions --------------------------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> saveSession() const;
    /** Loads a session: the device first, then each strip's and bus's settings and plugins. Problems
        (a plugin that's no longer installed) are listed in `problems`. */
    void loadSession (const juce::XmlElement&, juce::StringArray& problems, bool restoreDevice = true);
    void resetToDefaults();

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncToDevice();
    void updateVisibility();
    void allocate (Channel&);
    void applyDefaultOutputs (bool firstTime);
    void forEachChannel (const std::function<void (Channel&)>&);
    void deletePluginsOf (Channel&);

    void processStrip (Channel&, const float* const* in, int numIn, float* const* out, int numOut, int n, bool anySolo);
    void runChain (Channel&, int n);
    static void runPlugin (juce::AudioPluginInstance&, juce::AudioBuffer<float>&, juce::MidiBuffer&, int n);
    static void writeOutput (const juce::AudioBuffer<float>&, int firstOutput, float* const* out, int numOut, int n);

    static std::unique_ptr<juce::XmlElement> saveChannel (const Channel&, const juce::String& tag);
    void loadChannel (Channel&, const juce::XmlElement&, juce::StringArray& problems);

    juce::SpinLock lock;
    std::vector<std::unique_ptr<Channel>> strips;
    std::unique_ptr<Channel> fxBus, mainBus;
    std::array<std::unique_ptr<Channel>, 2> cues;
    juce::StringArray outputNames;
    juce::MidiBuffer midi;

    double sampleRate = 48000.0;
    int blockSize = 256, capacity = 0;
};

/** The console's dB scale for faders: where each value sits along the fader (0 bottom .. 1 top). */
double faderProportion (double db);
double faderValue (double proportion);

} // namespace atpconsole
