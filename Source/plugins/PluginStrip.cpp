/*
    Audio Tech Pro's channel strip as a plug-in (VST3 / AU) for other DAWs: the console channel the app has on
    every track (ConsoleChannel), in its photographed window (ConsoleStripWindow). Every knob and key is a host
    parameter (automatable). The window works on a TRACK ValueTree holding the same properties as a track in the
    app; the plug-in keeps that tree and its parameters in step. EXT keys the dynamics with the plug-in's
    side-chain input.
*/
#include <JuceHeader.h>
#include "PluginCommon.h"
#include "../engine/ConsoleChannel.h"
#include "../model/Identifiers.h"
#include "../ui/ConsoleStripWindow.h"

namespace riff::plugin
{

namespace
{
    struct StripParam
    {
        const juce::Identifier* id;
        const char* name;
        float min, max, fallback, centre;   // centre: the value at the middle of a knob (0: linear); a key: min 0, max 1, centre -1
        const char* unit;
    };

    constexpr float key = -1.0f;

    // (the panel's knobs and keys, left to right; their ranges as the strip reads them)
    const StripParam stripParams[] {
        { &ids::stripIn, "Power", 0, 1, 1, key, "" },
        { &ids::consoleInput, "Trim", -20, 20, 0, 0, "dB" },
        { &ids::consoleMicGain, "Mic Gain", 20, 70, 20, 0, "dB" },
        { &ids::consolePhantom, "48V", 0, 1, 0, key, "" },
        { &ids::consolePad, "Pad", 0, 1, 0, key, "" },
        { &ids::consolePhase, "Polarity", 0, 1, 0, key, "" },
        { &ids::consoleLine, "Line", 0, 1, 1, key, "" },
        { &ids::consoleDeEss, "De-ess In", 0, 1, 0, key, "" },
        { &ids::consoleDeEssThreshold, "De-ess Threshold", -20, 10, 0, 0, "dB" },
        { &ids::consoleDynamics, "Comp Threshold", 0, 10, 0, 0, "" },
        { &ids::consoleCompRatio, "Comp Ratio", 0, 20, 0, 4, ":1" },
        { &ids::consoleCompRelease, "Comp Release", 0.05f, 4, 0.15f, 0.5f, "s" },
        { &ids::consoleCompKnee, "Soft Knee", 0, 1, 0, key, "" },
        { &ids::consoleCompFast, "Comp Fast Attack", 0, 1, 0, key, "" },
        { &ids::consoleCompLinear, "Linear Release", 0, 1, 0, key, "" },
        { &ids::consoleDynIn, "Dynamics In", 0, 1, 1, key, "" },
        { &ids::consoleGateThreshold, "Gate Threshold", -80, 0, -80, -30, "dB" },
        { &ids::consoleGateRange, "Gate Range", 0, 40, 20, 0, "dB" },
        { &ids::consoleGateRelease, "Gate Release", 0.05f, 4, 0.25f, 0.5f, "s" },
        { &ids::consoleGateExpander, "Expander", 0, 1, 0, key, "" },
        { &ids::consoleGateFast, "Gate Fast Attack", 0, 1, 0, key, "" },
        { &ids::consoleHPF, "High Pass", 0, 500, 0, 120, "Hz" },
        { &ids::consoleLPF, "Low Pass", 0, 12000, 0, 6000, "Hz" },
        { &ids::consoleFiltersToSidechain, "Filters to Side Chain", 0, 1, 0, key, "" },
        { &ids::consoleFiltersAtInput, "Filters at Input", 0, 1, 1, key, "" },
        { &ids::consoleLF, "LF Gain", -15, 15, 0, 0, "dB" },
        { &ids::consoleLFFreq, "LF Freq", 30, 450, 100, 110, "Hz" },
        { &ids::consoleLFBell, "LF Bell", 0, 1, 0, key, "" },
        { &ids::consoleLMF, "LMF Gain", -15, 15, 0, 0, "dB" },
        { &ids::consoleLMFFreq, "LMF Freq", 200, 2500, 400, 700, "Hz" },
        { &ids::consoleLMFQ, "LMF Q", 0.1f, 4, 1, 1, "" },
        { &ids::consoleHMF, "HMF Gain", -15, 15, 0, 0, "dB" },
        { &ids::consoleHMFFreq, "HMF Freq", 600, 7000, 2500, 2000, "Hz" },
        { &ids::consoleHMFQ, "HMF Q", 0.1f, 4, 1, 1, "" },
        { &ids::consoleHF, "HF Gain", -15, 15, 0, 0, "dB" },
        { &ids::consoleHFFreq, "HF Freq", 1500, 16000, 8000, 5000, "Hz" },
        { &ids::consoleHFBell, "HF Bell", 0, 1, 0, key, "" },
        { &ids::consoleBlackEq, "Black EQ", 0, 1, 0, key, "" },
        { &ids::consoleIn, "EQ In", 0, 1, 1, key, "" },
        { &ids::consoleEqToSidechain, "EQ to Side Chain", 0, 1, 0, key, "" },
        { &ids::consoleDynPostEq, "Dynamics Post EQ", 0, 1, 1, key, "" },
        { &ids::consoleDeEssPostEq, "De-ess Post EQ", 0, 1, 1, key, "" },
        { &ids::consoleExternalKey, "External Key", 0, 1, 0, key, "" },
        { &ids::consoleLink, "Stereo Link", 0, 1, 1, key, "" },
        { &ids::consoleOutput, "Output", -20, 20, 0, 0, "dB" },
        { &ids::consoleMeterInput, "Meter Input", 0, 1, 0, key, "" },
    };
}

class StripProcessor final : public juce::AudioProcessor,
                             private juce::ValueTree::Listener,
                             private juce::Timer
{
public:
    StripProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                                 .withInput ("Side Chain", juce::AudioChannelSet::stereo(), false))
    {
        for (const auto& p : stripParams)
        {
            const juce::ParameterID pid { p.id->toString(), 1 };
            juce::RangedAudioParameter* param = nullptr;
            if (p.centre == key)
                param = new juce::AudioParameterBool (pid, p.name, p.fallback > 0.5f);
            else
            {
                juce::NormalisableRange<float> range (p.min, p.max);
                if (p.centre > p.min && p.centre < p.max && std::abs (p.centre - (p.min + p.max) * 0.5f) > 1.0e-3f)
                    range.setSkewForCentre (p.centre);
                param = new juce::AudioParameterFloat (pid, p.name, range, p.fallback, juce::AudioParameterFloatAttributes().withLabel (p.unit));
            }
            addParameter (param);
            params.push_back (param);
            track.setProperty (*p.id, p.centre == key ? juce::var (p.fallback > 0.5f) : juce::var ((double) p.fallback), nullptr);
        }
        track.addListener (this);
        startTimerHz (30);
    }

    ~StripProcessor() override { track.removeListener (this); }

    const juce::String getName() const override { return "ATP Channel Strip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        if (! (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) || layouts.getMainInputChannelSet() != out)
            return false;
        if (layouts.inputBuses.size() > 1)
        {
            const auto side = layouts.getChannelSet (true, 1);
            return side.isDisabled() || side == juce::AudioChannelSet::stereo() || side == juce::AudioChannelSet::mono();
        }
        return true;
    }

    void prepareToPlay (double sampleRate, int block) override
    {
        rate.store (sampleRate);
        channel.prepare (sampleRate);
        scratch.setSize (2, juce::jmax (block, 32));
    }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const auto settings = ConsoleChannel::fromValues ([this] (const juce::Identifier& id, double fallback)
        {
            for (size_t i = 0; i < params.size(); ++i)
                if (*stripParams[i].id == id)
                    return (double) params[i]->convertFrom0to1 (params[i]->getValue());
            return fallback;
        });
        channel.setSettings (settings);

        auto main = getBusBuffer (buffer, true, 0);
        const auto n = main.getNumSamples();
        if (scratch.getNumSamples() < n)
            scratch.setSize (2, n, false, false, true);
        auto* l = main.getWritePointer (0);
        auto* r = main.getNumChannels() > 1 ? main.getWritePointer (1) : scratch.getWritePointer (1);
        if (main.getNumChannels() < 2)
            juce::FloatVectorOperations::copy (r, l, n);

        const float* keyL = nullptr;
        const float* keyR = nullptr;
        if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
        {
            const auto side = getBusBuffer (buffer, true, 1);
            if (side.getNumChannels() > 0)
            {
                keyL = side.getReadPointer (0);
                keyR = side.getNumChannels() > 1 ? side.getReadPointer (1) : keyL;
            }
        }
        inLevel.push (l, r, n);
        channel.process (l, r, n, keyL, keyR);
        outLevel.push (l, r, n);
        tap.push (l, r, n);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("ATP_STRIP");
        for (size_t i = 0; i < params.size(); ++i)
            xml.setAttribute (stripParams[i].id->toString(), params[i]->convertFrom0to1 (params[i]->getValue()));
        copyXmlToBinary (xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (const auto xml = getXmlFromBinary (data, size))
            for (size_t i = 0; i < params.size(); ++i)
            {
                const auto name = stripParams[i].id->toString();
                if (xml->hasAttribute (name))
                    params[i]->setValueNotifyingHost (params[i]->convertTo0to1 ((float) xml->getDoubleAttribute (name)));
            }
    }

    juce::ValueTree track { ids::TRACK };
    juce::UndoManager undo;
    AnalyzerTap tap;
    PeakLevel inLevel, outLevel;
    ConsoleChannel channel;
    std::atomic<double> rate { 48000.0 };

private:
    // The window changes the tree: the parameters follow (and tell the host)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (syncing)
            return;
        for (size_t i = 0; i < params.size(); ++i)
            if (*stripParams[i].id == property)
            {
                const auto value = (float) (double) track[property];
                auto* p = params[i];
                const auto normalised = p->convertTo0to1 (value);
                if (std::abs (normalised - p->getValue()) > 1.0e-6f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (normalised);
                    p->endChangeGesture();
                }
            }
    }

    // The host (automation, a preset) changes the parameters: the tree follows, for the window
    void timerCallback() override
    {
        const juce::ScopedValueSetter<bool> guard (syncing, true);
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto value = params[i]->convertFrom0to1 (params[i]->getValue());
            const auto& id = *stripParams[i].id;
            const auto isKey = stripParams[i].centre == key;
            const juce::var wanted = isKey ? juce::var (value > 0.5f) : juce::var ((double) value);
            if (isKey ? ((bool) track[id] != (value > 0.5f)) : std::abs ((double) track[id] - value) > 1.0e-5)
                track.setProperty (id, wanted, nullptr);
        }
    }

    std::vector<juce::RangedAudioParameter*> params;
    juce::AudioBuffer<float> scratch;
    bool syncing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripProcessor)
};

//==============================================================================
class StripEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StripEditor (StripProcessor& p)
        : juce::AudioProcessorEditor (p), processor (p),
          strip (p.track, p.undo,
                 [&p] (float* dest, int count) { return p.tap.read (dest, count); },
                 [&p] { return p.rate.load(); },
                 [&p]
                 {
                     return std::array<float, 5> { p.inLevel.take(), p.outLevel.take(), p.channel.getGainReductionDb(),
                                                   p.channel.getDeEssReductionDb(), p.channel.getGateReductionDb() };
                 })
    {
        addAndMakeVisible (strip);
        setSize (strip.getWidth(), strip.getHeight());
    }

    void resized() override { strip.setBounds (getLocalBounds()); }

private:
    StripProcessor& processor;
    ConsoleStripWindow strip;
    juce::TooltipWindow tooltips { this, 600 };
};

juce::AudioProcessorEditor* StripProcessor::createEditor() { return new StripEditor (*this); }

} // namespace riff::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new riff::plugin::StripProcessor(); }
