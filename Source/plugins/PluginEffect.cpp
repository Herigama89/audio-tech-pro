/*
    One of Audio Tech Pro's photographed EQs as a plug-in (VST3 / AU) for other DAWs: RIFF_PLUGIN_EFFECT names
    the built-in effect ("tubeEq", "graphicEq"). The sound is the app's own (BuiltInEffect); the window is its
    photographed face (effectdesign), with every knob, switch and fader a host parameter (automatable).
*/
#include <JuceHeader.h>
#include "PluginCommon.h"
#include "../engine/BuiltInEffects.h"
#include "../ui/EffectDesigns.h"

#ifndef RIFF_PLUGIN_EFFECT
 #error "RIFF_PLUGIN_EFFECT: the built-in effect's type"
#endif

namespace riff::plugin
{

class EffectProcessor final : public juce::AudioProcessor
{
public:
    EffectProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          type (RIFF_PLUGIN_EFFECT)
    {
        info = findBuiltInEffect (type);
        jassert (info != nullptr);
        effect = BuiltInEffect::create (type);
        for (const auto& p : info->params)
        {
            const juce::ParameterID pid { p.id, 1 };
            if (p.isChoice())
            {
                juce::StringArray names;
                for (const auto& c : p.choices)
                    names.add (c);
                auto* param = new juce::AudioParameterChoice (pid, p.name, names, juce::jlimit (0, names.size() - 1, juce::roundToInt (p.defaultValue)));
                addParameter (param);
                params.push_back (param);
            }
            else
            {
                juce::NormalisableRange<float> range (p.min, p.max);
                if (p.midPoint > p.min && p.midPoint < p.max)
                    range.setSkewForCentre (p.midPoint);
                auto* param = new juce::AudioParameterFloat (pid, p.name, range, p.defaultValue,
                                                             juce::AudioParameterFloatAttributes().withLabel (p.suffix));
                addParameter (param);
                params.push_back (param);
            }
        }
    }

    const juce::String getName() const override { return info != nullptr ? "ATP " + info->name : juce::String ("ATP"); }
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
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet() == out;
    }

    void prepareToPlay (double sampleRate, int block) override
    {
        rate.store (sampleRate);
        effect->prepare (sampleRate, juce::jmax (block, 32));
        scratch.setSize (2, juce::jmax (block, 32));
    }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        for (size_t i = 0; i < params.size(); ++i)
            effect->setParameter ((int) i, realValue (i));
        const auto n = buffer.getNumSamples();
        if (scratch.getNumSamples() < n)
            scratch.setSize (2, n, false, false, true);   // (a host's block bigger than it said)
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (1);
        if (buffer.getNumChannels() < 2)
            juce::FloatVectorOperations::copy (r, l, n);
        effect->process (l, r, n);
        tap.push (l, r, n);
    }

    /** A parameter's value in the effect's own units (a switch: its setting's number). */
    float realValue (size_t i) const
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (params[i]))
            return (float) c->getIndex();
        return static_cast<juce::AudioParameterFloat*> (params[i])->get();
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("ATP_EFFECT");
        xml.setAttribute ("type", type);
        for (size_t i = 0; i < params.size(); ++i)
            xml.setAttribute (info->params[i].id, realValue (i));
        copyXmlToBinary (xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (const auto xml = getXmlFromBinary (data, size))
            for (size_t i = 0; i < params.size(); ++i)
                if (xml->hasAttribute (info->params[i].id))
                {
                    const auto v = (float) xml->getDoubleAttribute (info->params[i].id);
                    params[i]->setValueNotifyingHost (params[i]->convertTo0to1 (v));
                }
    }

    const juce::String type;
    const EffectInfo* info = nullptr;
    std::unique_ptr<BuiltInEffect> effect;
    std::vector<juce::RangedAudioParameter*> params;
    AnalyzerTap tap;
    std::atomic<double> rate { 48000.0 };

private:
    juce::AudioBuffer<float> scratch;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectProcessor)
};

//==============================================================================
/** The window: the effect's photographed face, its moving parts on the host's parameters. */
class EffectEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EffectEditor (EffectProcessor& p)
        : juce::AudioProcessorEditor (p), processor (p), design (effectdesign::designFor (p.type))
    {
        jassert (design != nullptr);
        look = std::make_unique<effectdesign::DesignLook> (*design);
        for (size_t ci = 0; ci < design->controls.size(); ++ci)
        {
            const auto& c = design->controls[ci];
            const auto index = indexOf (c.id);
            if (index < 0)
                continue;
            auto& param = *processor.params[(size_t) index];
            if (c.kind == effectdesign::Kind::knob || c.kind == effectdesign::Kind::fader)
            {
                auto slider = std::make_unique<juce::Slider>();
                slider->setLookAndFeel (look.get());
                slider->setSliderStyle (c.sprite.slides ? juce::Slider::LinearHorizontal
                                        : c.kind == effectdesign::Kind::fader ? juce::Slider::LinearVertical
                                        : juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider->setPopupDisplayEnabled (true, true, this);
                slider->getProperties().set ("sprite", (int) ci);
                slider->setTooltip (param.getName (64));
                slider->setBounds (c.bounds);
                sliderAttachments.push_back (std::make_unique<juce::SliderParameterAttachment> (param, *slider));
                addAndMakeVisible (*slider);
                components.push_back (std::move (slider));
            }
            else
            {
                // (a switch or a button on the photo: on/off)
                auto button = std::make_unique<juce::TextButton>();
                button->setLookAndFeel (look.get());
                button->setClickingTogglesState (true);
                button->getProperties().set ("sprite", (int) ci);
                button->getProperties().set ("hotspot", true);
                button->setTooltip (param.getName (64));
                button->setBounds (c.bounds);
                buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (param, *button));
                addAndMakeVisible (*button);
                components.push_back (std::move (button));
            }
        }
        setSize (design->width, design->height);
        startTimerHz (30);
    }

    ~EffectEditor() override
    {
        sliderAttachments.clear();
        buttonAttachments.clear();
        for (auto& c : components)
            c->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < processor.params.size(); ++i)
            v[i] = processor.realValue (i);
        effectdesign::setPaintingSpectrum (design->analyzer ? &spectrum : nullptr);
        design->paint (g, getLocalBounds(), *processor.info, v.data(), 0.0f);
        effectdesign::setPaintingSpectrum (nullptr);
    }

private:
    int indexOf (const juce::String& id) const
    {
        for (size_t i = 0; i < processor.info->params.size(); ++i)
            if (processor.info->params[i].id == id)
                return (int) i;
        return -1;
    }

    void timerCallback() override
    {
        if (design->analyzer)
            updateSpectrum();
        repaint();
    }

    /** The output's spectrum for the screen under the face (as the app's effect windows). */
    void updateSpectrum()
    {
        constexpr int order = 12, size = 1 << order;
        if (fft == nullptr)
        {
            fft = std::make_unique<juce::dsp::FFT> (order);
            fftData.assign ((size_t) size * 2, 0.0f);
            window.resize ((size_t) size);
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));
        }
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        if (processor.tap.read (fftData.data(), size) < size)
            return;
        const auto rate = processor.rate.load();
        for (int i = 0; i < size; ++i)
            fftData[(size_t) i] *= window[(size_t) i];
        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);
        const auto binHz = rate / size;
        for (int k = 0; k < effectdesign::Spectrum::points; ++k)
        {
            const auto f = effectdesign::Spectrum::frequencyOf (k);
            const auto lo = juce::jlimit (1, size / 2 - 1, (int) (f * std::pow (1000.0, -0.5 / effectdesign::Spectrum::points) / binHz));
            const auto hi = juce::jlimit (lo, size / 2 - 1, (int) std::ceil (f * std::pow (1000.0, 0.5 / effectdesign::Spectrum::points) / binHz));
            float m = 0.0f;
            if (hi - lo < 2)
            {
                const auto exact = juce::jlimit (1.0, size / 2.0 - 2.0, f / binHz);
                const auto i = (int) exact;
                const auto t = (float) (exact - i);
                m = fftData[(size_t) i] + t * (fftData[(size_t) i + 1] - fftData[(size_t) i]);
            }
            else
                for (int b = lo; b <= hi; ++b)
                    m = std::max (m, fftData[(size_t) b]);
            const auto db = juce::Decibels::gainToDecibels (m / (size * 0.25f), -120.0f);
            auto& shown = spectrum.db[(size_t) k];
            shown = ! spectrum.valid ? db : db > shown ? db : shown - std::min (shown - db, 1.6f);
        }
        spectrum.valid = true;
    }

    EffectProcessor& processor;
    const effectdesign::Design* design;
    std::unique_ptr<effectdesign::DesignLook> look;
    std::vector<std::unique_ptr<juce::Component>> components;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    effectdesign::Spectrum spectrum;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData, window;
    juce::TooltipWindow tooltips { this, 600 };
};

juce::AudioProcessorEditor* EffectProcessor::createEditor() { return new EffectEditor (*this); }

} // namespace riff::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new riff::plugin::EffectProcessor(); }
