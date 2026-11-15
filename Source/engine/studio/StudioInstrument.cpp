#include "Studio.h"
#include "StudioDsp.h"

namespace riff::studio
{

juce::StringArray kinds() { return { "drums", "keys", "bass", "guitar", "beatpad", "sampler" }; }

juce::String displayName (const juce::String& kind)
{
    if (kind == "drums")   return "Studio Drums";
    if (kind == "keys")    return "Studio Keys";
    if (kind == "bass")    return "Studio Bass";
    if (kind == "guitar")  return "Studio Guitar";
    if (kind == "beatpad") return "Beat Pad";
    if (kind == "sampler") return "Sampler";
    return kind;
}

namespace
{
    const char* const mixWhat[] { "gain", "pan", "mute", "solo", "sendA", "sendB", "out" };
    constexpr int mixCount = 7;
}

StudioInstrument::StudioInstrument (juce::String k, std::vector<Param> own, juce::StringArray partNamesIn)
    : kind (std::move (k)), parts (std::move (partNamesIn)), table (std::move (own))
{
    firstMix = (int) table.size();
    for (int i = 0; i < parts.size(); ++i)
    {
        const auto pre = "p" + juce::String (i);
        const auto name = parts[i] + " ";
        table.push_back ({ pre + "gain", name + "Level", -60.0f, 12.0f, 0.0f });
        table.push_back ({ pre + "pan", name + "Pan", -1.0f, 1.0f, 0.0f });
        table.push_back ({ pre + "mute", name + "Mute", 0.0f, 1.0f, 0.0f, { "Off", "On" } });
        table.push_back ({ pre + "solo", name + "Solo", 0.0f, 1.0f, 0.0f, { "Off", "On" } });
        table.push_back ({ pre + "sendA", name + "FX A", -60.0f, 6.0f, -60.0f });
        table.push_back ({ pre + "sendB", name + "FX B", -60.0f, 6.0f, -60.0f });
        table.push_back ({ pre + "out", name + "Output", 0.0f, 1.0f, 0.0f, { "Main", "Own channel" } });
    }
    table.push_back ({ "fxAGain", "FX A Level", -60.0f, 12.0f, 0.0f });
    table.push_back ({ "fxBGain", "FX B Level", -60.0f, 12.0f, 0.0f });
    table.push_back ({ "mainGain", "Main Level", -60.0f, 12.0f, 0.0f });
    values.reset (new std::atomic<float>[table.size()]);
    levels = std::vector<std::atomic<float>> ((size_t) parts.size());
    resetToDefaults();
}

const KindInfo& infoOf (const juce::String& kind)
{
    static std::map<juce::String, KindInfo> known;
    auto it = known.find (kind);
    if (it == known.end())
    {
        KindInfo info;
        if (auto ins = create (kind))
        {
            info.params = ins->params();
            info.numOwn = ins->numOwnParams();
            info.parts = ins->partNames();
        }
        it = known.emplace (kind, std::move (info)).first;
    }
    return it->second;
}

int StudioInstrument::indexOf (const juce::String& id) const
{
    for (size_t i = 0; i < table.size(); ++i)
        if (table[i].id == id)
            return (int) i;
    return -1;
}

int StudioInstrument::partParam (int part, const char* what) const
{
    for (int w = 0; w < mixCount; ++w)
        if (juce::String (mixWhat[w]) == what)
            return firstMix + part * mixCount + w;
    return -1;
}

void StudioInstrument::setParam (int index, float value) noexcept
{
    if (! juce::isPositiveAndBelow (index, (int) table.size()))
        return;
    const auto& p = table[(size_t) index];
    values[(size_t) index].store (juce::jlimit (p.min, p.max, value), std::memory_order_relaxed);
    if (index < firstMix)
        paramChanged (index);
}

float StudioInstrument::getParam (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) table.size()) ? values[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

void StudioInstrument::resetToDefaults() noexcept
{
    for (size_t i = 0; i < table.size(); ++i)
        values[i].store (table[i].def, std::memory_order_relaxed);
    for (int i = 0; i < firstMix; ++i)
        paramChanged (i);
}

float StudioInstrument::partLevel (int part) const noexcept
{
    return juce::isPositiveAndBelow (part, (int) levels.size()) ? levels[(size_t) part].load (std::memory_order_relaxed) : 0.0f;
}

void StudioInstrument::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate;
    preparedRate = sampleRate;
    const auto n = juce::jmax (maxBlockSize, 64);
    partBuffers.setSize (juce::jmax (2, parts.size() * 2), n);
    outBuffers.setSize (juce::jmax (2, parts.size() * 2), n);
    sendBuffers.setSize (4, n);
    partBuffers.clear();
    outBuffers.clear();
    sendBuffers.clear();
    partGainL.assign ((size_t) parts.size(), {});
    partGainR.assign ((size_t) parts.size(), {});
    for (auto& g : partGainL) g.reset (sampleRate, 0.02);
    for (auto& g : partGainR) g.reset (sampleRate, 0.02);
    ownOut.assign ((size_t) parts.size(), false);
    prepareVoices (sampleRate, n);
}

void StudioInstrument::setPosition (juce::int64 samplePosition, bool playing, double bpm) noexcept
{
    songPosition = samplePosition;
    songPlaying = playing;
    songBpm = bpm > 1.0 ? bpm : 120.0;
}

const float* StudioInstrument::getExtraOutput (int pair, int channel) const noexcept
{
    const auto part = pair - 1;
    if (! juce::isPositiveAndBelow (part, parts.size()) || ! ownOut[(size_t) part] || renderedSamples <= 0)
        return nullptr;
    return outBuffers.getReadPointer (part * 2 + juce::jlimit (0, 1, channel));
}

void StudioInstrument::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    numSamples = juce::jmin (numSamples, partBuffers.getNumSamples());
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);
    const auto numParts = parts.size();
    for (int c = 0; c < numParts * 2; ++c)
        juce::FloatVectorOperations::clear (partBuffers.getWritePointer (c), numSamples);
    for (int c = 0; c < 4; ++c)
        juce::FloatVectorOperations::clear (sendBuffers.getWritePointer (c), numSamples);

    std::array<float*, 64> pl {}, pr {};
    for (int i = 0; i < juce::jmin (numParts, 64); ++i)
    {
        pl[(size_t) i] = partBuffers.getWritePointer (i * 2);
        pr[(size_t) i] = partBuffers.getWritePointer (i * 2 + 1);
    }
    renderParts (pl.data(), pr.data(), numSamples, midi);

    const auto* chains = partChains;
    bool anySolo = false;
    for (int i = 0; i < numParts; ++i)
        anySolo = anySolo || p (partParam (i, "solo")) > 0.5f;

    auto* aL = sendBuffers.getWritePointer (0);
    auto* aR = sendBuffers.getWritePointer (1);
    auto* bL = sendBuffers.getWritePointer (2);
    auto* bR = sendBuffers.getWritePointer (3);

    for (int i = 0; i < numParts; ++i)
    {
        auto* l = pl[(size_t) i];
        auto* r = pr[(size_t) i];
        // its inserts
        if (chains != nullptr && i < (int) chains->parts.size())
            for (const auto& effect : chains->parts[(size_t) i])
                if (effect != nullptr)
                    effect->process (l, r, numSamples);

        const auto base = firstMix + i * mixCount;
        const bool heard = p (base + 2) < 0.5f && (! anySolo || p (base + 3) > 0.5f);
        // (the bottom of a fader's or send's range is off: -inf, not -60 dB)
        const auto offOrGain = [] (float db) { return db <= -59.9f ? 0.0f : dsp::dbToGain (db); };
        const auto gain = heard ? offOrGain (p (base)) : 0.0f;
        const auto pan = p (base + 1);
        partGainL[(size_t) i].setTargetValue (gain * (pan <= 0.0f ? 1.0f : 1.0f - pan));
        partGainR[(size_t) i].setTargetValue (gain * (pan >= 0.0f ? 1.0f : 1.0f + pan));
        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
        {
            l[s] *= partGainL[(size_t) i].getNextValue();
            r[s] *= partGainR[(size_t) i].getNextValue();
            peak = juce::jmax (peak, std::abs (l[s]), std::abs (r[s]));
        }
        levels[(size_t) i].store (juce::jmax (peak, levels[(size_t) i].load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

        // sends (after its fader), then where it goes: the main output or its own
        if (const auto send = offOrGain (p (base + 4)); send > 0.0f && heard)
        {
            juce::FloatVectorOperations::addWithMultiply (aL, l, send, numSamples);
            juce::FloatVectorOperations::addWithMultiply (aR, r, send, numSamples);
        }
        if (const auto send = offOrGain (p (base + 5)); send > 0.0f && heard)
        {
            juce::FloatVectorOperations::addWithMultiply (bL, l, send, numSamples);
            juce::FloatVectorOperations::addWithMultiply (bR, r, send, numSamples);
        }
        // (its own output only while a channel plays it: without one it stays in the main, so nothing goes silent)
        const bool own = p (base + 6) > 0.5f
                         && (partChains == nullptr || partChains->routed.empty()
                             || (i < (int) partChains->routed.size() && partChains->routed[(size_t) i]));
        ownOut[(size_t) i] = own;
        if (own)
        {
            juce::FloatVectorOperations::copy (outBuffers.getWritePointer (i * 2), l, numSamples);
            juce::FloatVectorOperations::copy (outBuffers.getWritePointer (i * 2 + 1), r, numSamples);
        }
        else
        {
            juce::FloatVectorOperations::add (left, l, numSamples);
            juce::FloatVectorOperations::add (right, r, numSamples);
        }
    }

    // the returns
    const auto fxReturn = [&] (float* l, float* r, const std::vector<std::shared_ptr<Effect>>* chain, float gainDb)
    {
        if (chain != nullptr)
            for (const auto& effect : *chain)
                if (effect != nullptr)
                    effect->process (l, r, numSamples);
        const auto g = gainDb <= -59.9f ? 0.0f : dsp::dbToGain (gainDb);
        juce::FloatVectorOperations::addWithMultiply (left, l, g, numSamples);
        juce::FloatVectorOperations::addWithMultiply (right, r, g, numSamples);
    };
    fxReturn (aL, aR, chains != nullptr ? &chains->fxA : nullptr, p (indexOf ("fxAGain")));
    fxReturn (bL, bR, chains != nullptr ? &chains->fxB : nullptr, p (indexOf ("fxBGain")));

    if (chains != nullptr)
        for (const auto& effect : chains->main)
            if (effect != nullptr)
                effect->process (left, right, numSamples);
    const auto mainGain = dsp::dbToGain (p ((int) table.size() - 1));
    juce::FloatVectorOperations::multiply (left, mainGain, numSamples);
    juce::FloatVectorOperations::multiply (right, mainGain, numSamples);
    renderedSamples = numSamples;
}

} // namespace riff::studio
