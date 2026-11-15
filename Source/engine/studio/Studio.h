#pragma once

#include "../Instrument.h"
#include "../Effect.h"
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <map>

/**
    The studio instruments: Drums, Keys (the workstation), Bass, Guitar, the Beat Pad and the Sampler Track.

    Each is made of parts (the kit's kick, snare, toms...; the piano's close, wide and room mics; the beat pad's
    output groups), mixed inside it as HALion and Logic's kits do: every part has a fader, pan, mute and solo,
    two sends to the instrument's FX A and FX B returns, and insert slots that take our effects or any plug-in;
    each part can play into the main output or out on its own (Logic's multi-output: its own mixer channel).

    Parameters live in a STUDIO child of the track, by id; a knob changes one straight away (no graph rebuild).
*/
namespace riff::studio
{
    struct Param
    {
        juce::String id, name;
        float min = 0.0f, max = 1.0f, def = 0.0f;
        juce::StringArray choices {};   // a stepped choice (min 0, max choices-1)
    };

    /** The insert chains of an instrument's parts and returns, as the project has them (built with the graph). */
    struct PartChains
    {
        std::vector<std::vector<std::shared_ptr<Effect>>> parts;
        std::vector<std::shared_ptr<Effect>> fxA, fxB, main;
    };

    /** Which chain owner (model::insertsOf) holds a part's inserts: part n, or "fxA", "fxB", "main". */
    inline juce::String chainOwner (const juce::String& trackId, const juce::String& part) { return "part:" + trackId + ":" + part; }

    /** The instruments by name ("drums", "keys", "bass", "guitar", "beatpad", "sampler"). */
    juce::StringArray kinds();
    juce::String displayName (const juce::String& kind);

    class StudioInstrument : public Instrument
    {
    public:
        StudioInstrument (juce::String kind, std::vector<Param> ownParams, juce::StringArray partNames);
        ~StudioInstrument() override = default;

        const juce::String& getKind() const noexcept { return kind; }
        juce::String getName() const override { return displayName (kind); }

        // Parameters: the instrument's own, then each part's mix (gain dB, pan, mute, solo, send A, send B, out), the returns'
        const std::vector<Param>& params() const noexcept { return table; }
        int indexOf (const juce::String& id) const;
        void setParam (int index, float value) noexcept;
        float getParam (int index) const noexcept;
        void resetToDefaults() noexcept;
        const juce::StringArray& partNames() const noexcept { return parts; }
        int numParts() const noexcept { return parts.size(); }

        /** A part's mix parameter's index: what is "gain", "pan", "mute", "solo", "sendA", "sendB" or "out". */
        int partParam (int part, const char* what) const;

        /** The chains to use for the coming block (audio thread, from the playing graph; may be null). */
        void setPartChains (const void* chains) noexcept override { partChains = static_cast<const PartChains*> (chains); }

        void prepare (double sampleRate, int maxBlockSize) final;
        void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept final;
        void setPosition (juce::int64 samplePosition, bool playing, double bpm) noexcept override;

        int getNumExtraOutputs() const noexcept override { return parts.size(); }
        const float* getExtraOutput (int pair, int channel) const noexcept override;
        juce::String getOutputName (int pair) const override { return pair == 0 ? juce::String ("Main") : parts[pair - 1]; }

        /** A file the instrument plays (the Sampler Track's sample, a beat pad's): loaded on the message thread. */
        virtual void setSampleFile (int /*slot*/, const juce::File&) {}
        /** The same, only when it's a different file from the one loaded in that slot (message thread). */
        void useFile (int slot, const juce::String& path)
        {
            if (loadedFiles[slot] == path) return;
            loadedFiles[slot] = path;
            setSampleFile (slot, juce::File (path));
        }
        /** The beat pad's pattern, as text (the others have none). */
        virtual void setPatternText (const juce::String&) {}
        /** What the screen shows (message thread): a few lines the instrument fills in. */
        virtual juce::StringArray screenLines() const { return {}; }
        /** Notes sounding now (for the keys, strings and pads that light up). */
        std::array<std::atomic<float>, 128> noteLevel {};
        /** What last played (message thread reads): note, velocity, and the part it went to. */
        std::atomic<int> lastNote { -1 }, lastVelocity { 0 }, lastPart { -1 };
        /** A part's level now (0..1, for the MIX tab's meters). */
        float partLevel (int part) const noexcept;

    protected:
        /** Renders the voices into the parts' buffers (added in: they start cleared). */
        virtual void renderParts (float* const* partL, float* const* partR, int numSamples, const juce::MidiBuffer& midi) noexcept = 0;
        virtual void prepareVoices (double sampleRate, int maxBlockSize) = 0;
        /** A parameter of its own changed (to recompute things that follow from it; any thread). */
        virtual void paramChanged (int /*index*/) noexcept {}

        float p (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
        /** Changes a parameter's default (a part's level that starts lower, say) - in the constructor. */
        void setDefault (int index, float value) noexcept
        {
            if (! juce::isPositiveAndBelow (index, (int) table.size())) return;
            table[(size_t) index].def = value;
            values[(size_t) index].store (value, std::memory_order_relaxed);
        }
        int choice (int index) const noexcept { return juce::roundToInt (p (index)); }

        double rate = 48000.0;
        juce::int64 songPosition = 0;
        bool songPlaying = false;
        double songBpm = 120.0;

    private:
        juce::String kind;
        juce::StringArray parts;
        std::vector<Param> table;
        std::unique_ptr<std::atomic<float>[]> values;
        int firstMix = 0;
        const PartChains* partChains = nullptr;
        juce::AudioBuffer<float> partBuffers, outBuffers, sendBuffers;   // each part's L/R; its own output; FX A and B
        std::vector<juce::SmoothedValue<float>> partGainL, partGainR;
        std::vector<std::atomic<float>> levels;
        std::vector<bool> ownOut;
        int renderedSamples = 0;
        std::map<int, juce::String> loadedFiles;
    };

    /** A new instrument of a kind (nullptr for an unknown one). */
    std::unique_ptr<StudioInstrument> create (const juce::String& kind);
}
