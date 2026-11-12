/**
    One of Audio Tech Pro's own effects as a VST3 / Audio Unit plug-in for other hosts: the same engine and the same
    face as in the app. Which effect it is comes from RIFF_FX_TYPE (one plug-in target per effect: see CMakeLists.txt,
    RIFF_BUILD_PLUGINS).

    The face edits an INSERT node exactly as in the app; the node and the host's parameters are kept in step both
    ways, so the knobs automate in any host and the settings are saved with the host's session.
*/
#include <JuceHeader.h>
#include "../engine/BuiltInEffects.h"
#include "../model/Identifiers.h"
#include "../ui/InstrumentWindows.h"
#include "../ui/EffectDesigns.h"
#include "../ui/Theme.h"

#ifndef RIFF_FX_TYPE
 #error "RIFF_FX_TYPE names the effect this plug-in is"
#endif

namespace riff
{
namespace
{
    const EffectInfo& infoOf()
    {
        const auto* info = findBuiltInEffect (RIFF_FX_TYPE);
        jassert (info != nullptr);
        return *info;
    }

    std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamInfo& p, int index)
    {
        const juce::ParameterID id { p.id, 1 };
        if (p.isChoice())
        {
            juce::StringArray names;
            for (const auto& c : p.choices)
                names.add (c);
            return std::make_unique<juce::AudioParameterChoice> (id, p.name, names, juce::jlimit (0, names.size() - 1, juce::roundToInt (p.defaultValue)));
        }
        juce::NormalisableRange<float> range (p.min, p.max);
        if (p.midPoint > p.min && p.midPoint < p.max && std::abs (p.midPoint - 0.5f * (p.min + p.max)) > 1.0e-4f * (p.max - p.min))
            range.setSkewForCentre (p.midPoint);
        juce::ignoreUnused (index);
        return std::make_unique<juce::AudioParameterFloat> (id, p.name, range, p.defaultValue,
                                                            juce::AudioParameterFloatAttributes().withLabel (p.suffix.trim()));
    }
}

class FxProcessor final : public juce::AudioProcessor,
                          private juce::ValueTree::Listener,
                          private juce::AudioProcessorParameter::Listener,
                          private juce::AsyncUpdater
{
public:
    FxProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          info (infoOf())
    {
        effect = BuiltInEffect::create (RIFF_FX_TYPE);
        insert = juce::ValueTree (ids::INSERT);
        insert.setProperty (ids::effect, RIFF_FX_TYPE, nullptr);
        insert.setProperty (ids::id, "plugin", nullptr);
        for (size_t i = 0; i < info.params.size(); ++i)
        {
            auto p = makeParameter (info.params[i], (int) i);
            p->addListener (this);
            params.push_back (p.get());
            addParameter (p.release());
            insert.setProperty (juce::Identifier (info.params[i].id), info.params[i].defaultValue, nullptr);
        }
        insert.addListener (this);
    }

    ~FxProcessor() override
    {
        cancelPendingUpdate();
        for (auto* p : params)
            p->removeListener (this);
    }

    //==============================================================================
    const juce::String getName() const override { return info.name; }
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
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;
        return layouts.getMainInputChannelSet() == out;
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        blockSize = std::max (32, samplesPerBlock);
        effect->prepare (sampleRate, blockSize);
        spare.setSize (1, blockSize);
        pushParameters();
        setLatencySamples (effect->getLatencySamples());
    }

    void releaseResources() override {}

    using AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        pushParameters();
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                effect->setPosition (position->getTimeInSamples().orFallback (0), position->getIsPlaying(), position->getBpm().orFallback (120.0));
        const auto n = buffer.getNumSamples();
        auto* l = buffer.getWritePointer (0);
        const bool mono = buffer.getNumChannels() < 2;
        if (mono && spare.getNumSamples() < n)
            spare.setSize (1, n, false, false, true);
        auto* r = mono ? spare.getWritePointer (0) : buffer.getWritePointer (1);
        if (mono)
            std::copy (l, l + n, r);
        for (int at = 0; at < n; at += blockSize)
        {
            const auto count = std::min (blockSize, n - at);
            effect->process (l + at, r + at, count);
        }
        if (mono)
            for (int i = 0; i < n; ++i)
                l[i] = 0.5f * (l[i] + r[i]);
        effect->tapOutput (l, mono ? l : r, n);
    }

    //==============================================================================
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = insert.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr)
            return;
        const auto saved = juce::ValueTree::fromXml (*xml);
        if (! saved.isValid())
            return;
        for (int k = 0; k < saved.getNumProperties(); ++k)
        {
            const auto name = saved.getPropertyName (k);
            if (name == ids::effect || name == ids::id)
                continue;
            insert.setProperty (name, saved[name], nullptr);   // (the parameters follow: valueTreePropertyChanged)
        }
    }

    BuiltInEffect& getEffect() noexcept { return *effect; }
    juce::ValueTree insert;
    juce::UndoManager undoManager;

private:
    /** The parameters' values into the engine (each block: they are atomics). */
    void pushParameters() noexcept
    {
        for (size_t i = 0; i < params.size(); ++i)
            effect->setParameter ((int) i, params[i]->convertFrom0to1 (params[i]->getValue()));
    }

    // The face changed a setting: tell the host (unless it came from the host)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (fromHost)
            return;
        for (size_t i = 0; i < info.params.size(); ++i)
            if (property.toString() == info.params[i].id)
            {
                auto* p = params[i];
                const auto value = (float) (double) insert.getProperty (property);
                const auto normal = p->convertTo0to1 (value);
                if (std::abs (normal - p->getValue()) > 1.0e-6f)
                {
                    const juce::ScopedValueSetter<bool> guard (fromFace, true);
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (normal);
                    p->endChangeGesture();
                }
                return;
            }
    }

    // The host changed (or automated) a parameter: the face follows, on the message thread
    void parameterValueChanged (int, float) override
    {
        if (! fromFace)
            triggerAsyncUpdate();
    }
    void parameterGestureChanged (int, bool) override {}

    void handleAsyncUpdate() override
    {
        const juce::ScopedValueSetter<bool> guard (fromHost, true);
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto value = params[i]->convertFrom0to1 (params[i]->getValue());
            const juce::Identifier id (info.params[i].id);
            if (std::abs ((float) (double) insert.getProperty (id) - value) > 1.0e-5f * std::max (1.0f, std::abs (value)))
                insert.setProperty (id, value, nullptr);
        }
    }

    const EffectInfo& info;
    std::unique_ptr<BuiltInEffect> effect;
    std::vector<juce::RangedAudioParameter*> params;
    juce::AudioBuffer<float> spare;
    int blockSize = 512;
    bool fromHost = false, fromFace = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxProcessor)
};

//==============================================================================
/** The face, as in the app's window: with the Skin menu above it for the faces that come in skins. */
class FxEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FxEditor (FxProcessor& p)
        : AudioProcessorEditor (p), processor (p), panel (p.insert, p.undoManager)
    {
        addAndMakeVisible (panel);
        panel.setLiveValue ([&p] { return p.getEffect().displayValue(); });
        panel.setMeterSource ([&p] (float* dest, int capacity) { return p.getEffect().readMeters (dest, capacity); });
        panel.setAnalyzerSource ([&p] (float* dest, int count)
        {
            auto& e = p.getEffect();
            e.setAnalyzerOn (true);
            e.readTap (dest, std::min (count, BuiltInEffect::tapSize));
            return e.getSampleRate();
        });
        const auto* design = effectdesign::designFor (RIFF_FX_TYPE);
        if (design != nullptr && design->skins)
        {
            skinButton.onClick = [this] { showSkins(); };
            addAndMakeVisible (skinButton);
            updateSkinButton();
        }
        bar = skinButton.isVisible() ? 32 : 0;
        setSize (panel.getWidth(), panel.getHeight() + bar);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::toolbarDark); }
    void resized() override
    {
        if (skinButton.isVisible())
            skinButton.setBounds (getWidth() - 186, 4, 180, 24);
        panel.setBounds (0, bar, getWidth(), getHeight() - bar);
    }
    void childBoundsChanged (juce::Component* child) override
    {
        if (child == &panel && ! fitting)
        {
            const juce::ScopedValueSetter<bool> guard (fitting, true);
            setSize (panel.getWidth(), panel.getHeight() + bar);
            panel.setTopLeftPosition (0, bar);
        }
    }

private:
    void updateSkinButton()
    {
        const auto skin = (int) processor.insert.getProperty ("skin", effectdesign::defaultClassicSkin);
        skinButton.setButtonText ("Skin: " + effectdesign::classicSkinNames()[juce::jlimit (0, 5, skin)]);
    }
    void showSkins()
    {
        juce::PopupMenu menu;
        const auto current = (int) processor.insert.getProperty ("skin", effectdesign::defaultClassicSkin);
        const auto& names = effectdesign::classicSkinNames();
        for (int k = 0; k < names.size(); ++k)
            menu.addItem (names[k], true, k == current, [this, k]
            {
                processor.insert.setProperty ("skin", k, nullptr);
                updateSkinButton();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (skinButton));
    }

    FxProcessor& processor;
    EffectPanel panel;
    juce::TextButton skinButton;
    int bar = 0;
    bool fitting = false;
};

juce::AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new riff::FxProcessor(); }
