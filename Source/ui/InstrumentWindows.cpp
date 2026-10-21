#include "InstrumentWindows.h"
#include "ControlMenu.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/AutomationWriter.h"

namespace riff
{

SynthPanel::SynthPanel (juce::ValueTree t, juce::UndoManager& um, const juce::String& k)
    : track (t), undoManager (um), kind (k)
{
    if (kind == "drums" || kind == "sampler")
    {
        waveLabel.setText (kind == "drums" ? "Drum Kit" : "Sound", juce::dontSendNotification);
        waveLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (waveLabel);
        if (kind == "drums")
        {
            addControl (ids::drumTune,  "Tune",   -12.0, 12.0, 0.0, " st");
            addControl (ids::drumDecay, "Decay",  0.3, 3.0, 1.0, "x");
        }
        else
        {
            soundButton.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a sound for the sampler",
                                                               juce::File (track[ids::samplerFile].toString()).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
                {
                    if (c.getResult() == juce::File())
                        return;
                    undoManager.beginNewTransaction ("Sampler sound");
                    track.setProperty (ids::samplerFile, c.getResult().getFullPathName(), &undoManager);
                });
            };
            addAndMakeVisible (soundButton);
            addControl (ids::samplerRoot,  "Root Key", 24.0, 96.0, 60.0, "");
            addControl (ids::synthAttack,  "Attack",  0.001, 2.0, 0.05, " s");
            addControl (ids::synthRelease, "Release", 0.001, 6.0, 0.5, " s");
        }
        addControl (ids::synthGain, "Volume", 0.0, 1.0, 0.5, "");
        track.addListener (this);
        refresh();
        setSize ((controls.size() + 1) * 84 + 64, 150);
        return;
    }

    waveLabel.setText ("Wave", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (waveLabel);

    waveBox.addItemList ({ "Saw", "Square", "Triangle", "Sine" }, 1);
    waveBox.onChange = [this]
    {
        undoManager.beginNewTransaction ("Synth wave");
        track.setProperty (ids::synthWave, waveBox.getSelectedId() - 1, &undoManager);
    };
    addAndMakeVisible (waveBox);

    addControl (ids::synthCutoff,    "Cutoff",    40.0, 18000.0, 1500.0, " Hz");
    addControl (ids::synthResonance, "Resonance", 0.0,  1.0,     0.5,    "");
    addControl (ids::synthAttack,    "Attack",    0.001, 4.0,    0.2,    " s");
    addControl (ids::synthDecay,     "Decay",     0.001, 4.0,    0.4,    " s");
    addControl (ids::synthSustain,   "Sustain",   0.0,  1.0,     0.5,    "");
    addControl (ids::synthRelease,   "Release",   0.001, 6.0,    0.5,    " s");
    addControl (ids::synthGain,      "Volume",    0.0,  1.0,     0.5,    "");

    track.addListener (this);
    refresh();
    setSize (8 * 84 + 24, 150);
}

SynthPanel::~SynthPanel()
{
    track.removeListener (this);
}

void SynthPanel::addControl (const juce::Identifier& id, const juce::String& name, double min, double max,
                             double mid, const juce::String& suffix)
{
    auto* c = controls.add (new Control());
    c->id = id;

    c->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    c->slider.setRange (min, max, 0.0);
    c->slider.setSkewFactorFromMidPoint (mid);
    c->slider.setNumDecimalPlacesToDisplay (max > 100.0 ? 0 : 2);
    c->slider.setTextValueSuffix (suffix);
    c->slider.onDragStart = [this, name] { undoManager.beginNewTransaction ("Synth " + name); };
    c->slider.onValueChange = [this, c] { track.setProperty (c->id, c->slider.getValue(), &undoManager); };
    addAndMakeVisible (c->slider);

    c->label.setText (name, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c->label);
}

void SynthPanel::refresh()
{
    waveBox.setSelectedId ((int) track.getProperty (ids::synthWave, 0) + 1, juce::dontSendNotification);
    for (auto* c : controls)
    {
        const auto fallback = c->id == ids::drumDecay ? 1.0 : c->id == ids::drumTune ? 0.0 : c->id == ids::samplerRoot ? 60.0
                            : c->id == ids::synthGain ? 0.8 : c->slider.getMinimum();
        c->slider.setValue (track.getProperty (c->id, fallback), juce::dontSendNotification);
        if (c->id == ids::samplerRoot)
            c->slider.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); };
    }
    if (kind == "sampler")
    {
        const juce::File file (track[ids::samplerFile].toString());
        soundButton.setButtonText (file.existsAsFile() ? file.getFileNameWithoutExtension() : juce::String ("Choose Sound..."));
    }
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SynthPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto column = area.removeFromLeft (kind == "synth" ? 84 : 140);
    waveLabel.setBounds (column.removeFromTop (20));
    if (kind == "sampler")
        soundButton.setBounds (column.removeFromTop (26).reduced (2, 0));
    else if (kind == "synth")
        waveBox.setBounds (column.removeFromTop (26).reduced (2, 0));

    for (auto* c : controls)
    {
        column = area.removeFromLeft (84);
        c->label.setBounds (column.removeFromTop (20));
        c->slider.setBounds (column);
    }
}

//==============================================================================
EffectPanel::EffectPanel (juce::ValueTree i, juce::UndoManager& um)
    : insert (i), undoManager (um)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    look = std::make_unique<effectface::FaceLook> (effectface::accentFor (info != nullptr ? info->category : juce::String()));
    design = info != nullptr ? effectdesign::designFor (info->type) : nullptr;
    if (design != nullptr)
    {
        designLook = std::make_unique<effectdesign::DesignLook> (*design);
        setLookAndFeel (designLook.get());
    }
    else
        setLookAndFeel (look.get());
    bypassButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.8f));
    bypassButton.setColour (juce::ToggleButton::tickColourId, look->accent);

    bypassButton.onClick = [this]
    {
        undoManager.beginNewTransaction ("Bypass");
        insert.setProperty (ids::bypass, bypassButton.getToggleState(), &undoManager);
    };
    addAndMakeVisible (bypassButton);

    if (info != nullptr && design != nullptr)
        buildDesign (*info);
    else if (info != nullptr)
        for (const auto& p : info->params)
            addKnob (p);

    insert.addListener (this);
    refresh();
    if (design != nullptr)
    {
        bypassButton.setVisible (false);   // (the window's bar above has Bypass)
        setSize (design->width, design->height);
    }
    else
        setSize (faceWidth, layoutFace (false));
}

EffectPanel::Knob* EffectPanel::addKnob (const ParamInfo& p)
{
    auto* k = knobs.add (new Knob());
    k->id = p.id;

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 17);
    if (p.isChoice())
    {
        // A switch (circuit, mode, note value...): the knob steps through its settings, shown by name
        k->slider.setRange (p.min, p.max, 1.0);
        k->slider.textFromValueFunction = [p] (double v) { return p.text ((float) v); };
        k->slider.valueFromTextFunction = [p] (const juce::String& t)
        {
            for (size_t c = 0; c < p.choices.size(); ++c)
                if (p.choices[c].equalsIgnoreCase (t.trim()))
                    return (double) c;
            return (double) p.defaultValue;
        };
        k->slider.setMouseDragSensitivity (juce::jmax (60, (int) p.choices.size() * 18));
    }
    else
    {
        k->slider.setRange (p.min, p.max, 0.0);
        if (p.midPoint > p.min && p.midPoint < p.max)
            k->slider.setSkewFactorFromMidPoint (p.midPoint);
        k->slider.setNumDecimalPlacesToDisplay (p.max > 100.0f ? 0 : (p.max > 10.0f ? 1 : 2));
        k->slider.setTextValueSuffix (p.suffix);
    }
    k->slider.setDoubleClickReturnValue (true, p.defaultValue);
    k->slider.updateText();   // (a switch shows its setting's name from the start)
    // Touch, Latch or Write while playing: the knob is written as automation too.
    const auto owner = [this] { return insert.getParent()[ids::owner].toString(); };
    const auto param = [this, k] { return model::insertParam (insert[ids::id].toString(), k->id); };
    k->slider.onDragStart = [this, k, owner, param, name = p.name]
    {
        undoManager.beginNewTransaction (name);
        if (auto* w = AutomationWriter::current())
            w->begin (owner(), param(), k->slider.getValue());
    };
    k->slider.onDragEnd = [k, owner, param] { juce::ignoreUnused (k); if (auto* w = AutomationWriter::current()) w->end (owner(), param()); };
    k->slider.onValueChange = [this, k, owner, param]
    {
        if (auto* w = AutomationWriter::current())
            w->move (owner(), param(), k->slider.getValue());
        insert.setProperty (juce::Identifier (k->id), k->slider.getValue(), &undoManager);
    };
    k->slider.onMenu = [this, k, owner, param, name = juce::String (p.name), def = (double) p.defaultValue]
    {
        const auto info2 = findBuiltInEffect (insert[ids::effect].toString());
        controlmenu::Target t;
        t.project = insert.getParent().getParent().getParent();
        t.owner = owner();
        t.param = param();
        t.name = (info2 != nullptr ? info2->name + " " : juce::String()) + name;
        t.um = &undoManager;
        t.value = [k] { return k->slider.getValue(); };
        t.reset = [k, def] { k->slider.setValue (def, juce::sendNotificationSync); };
        controlmenu::show (k->slider, t);
    };
    addAndMakeVisible (k->slider);

    k->label.setText (p.name, juce::dontSendNotification);
    k->label.setFont (juce::FontOptions (11.5f));
    k->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    k->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k->label);
    return k;
}

void EffectPanel::buildDesign (const EffectInfo& info)
{
    const auto paramOf = [&info] (const juce::String& id) -> const ParamInfo*
    {
        for (const auto& p : info.params)
            if (p.id == id)
                return &p;
        return nullptr;
    };
    const auto setValue = [this] (const juce::String& id, const juce::String& undoName, float value)
    {
        undoManager.beginNewTransaction (undoName);
        insert.setProperty (juce::Identifier (id), value, &undoManager);
    };
    const auto current = [this, paramOf] (const juce::String& id)
    {
        const auto* p = paramOf (id);
        return (float) (double) insert.getProperty (juce::Identifier (id), p != nullptr ? p->defaultValue : 0.0f);
    };

    for (const auto& c : design->controls)
    {
        if (c.kind == effectdesign::Kind::fileButton)
        {
            // A file for the insert (not a parameter): the button shows its name, a click picks another
            auto* b = new juce::TextButton();
            extras.add (b);
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, id = c.id]
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Choose an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                          [this, id] (const juce::FileChooser& chooser)
                                          {
                                              const auto file = chooser.getResult();
                                              if (file.existsAsFile())
                                              {
                                                  undoManager.beginNewTransaction ("Load Impulse Response");
                                                  insert.setProperty (juce::Identifier (id), file.getFullPathName(), &undoManager);
                                                  // (a sampled IR was chosen: use it)
                                                  insert.setProperty ("mode", 0.0f, &undoManager);
                                              }
                                          });
            };
            addAndMakeVisible (b);
            refreshers.push_back ([this, b, id = c.id]
            {
                const auto path = insert.getProperty (juce::Identifier (id)).toString();
                b->setButtonText (path.isEmpty() ? juce::String ("Load IR...") : juce::File (path).getFileNameWithoutExtension());
            });
            Placed placed;
            placed.control = c;
            placed.component = b;
            placedControls.push_back (placed);
            continue;
        }
        const auto* p = paramOf (c.id);
        if (p == nullptr)
            continue;
        Placed placed;
        placed.control = c;
        const auto caption = c.kind == effectdesign::Kind::toggle && c.label.startsWith ("=");
        const auto labelText = c.label == "-" || caption || c.kind == effectdesign::Kind::hotspot ? juce::String() : c.label.isNotEmpty() ? c.label : p->name;
        if (labelText.isNotEmpty())
        {
            auto* l = extraLabels.add (new juce::Label ({}, labelText));
            l->setFont (juce::FontOptions (15.0f));
            // (a control's own colour, for one that sits on a different background, e.g. on a screen)
            const bool ownColour = c.kind != effectdesign::Kind::field && ! c.colour.isTransparent();
            l->setColour (juce::Label::textColourId, ownColour ? c.colour : design->label);
            l->setJustificationType (juce::Justification::centred);
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
            placed.label = l;
        }

        switch (c.kind)
        {
            case effectdesign::Kind::knob:
            case effectdesign::Kind::field:
            case effectdesign::Kind::fader:
            {
                auto* k = addKnob (*p);
                k->label.setVisible (false);
                if (c.kind == effectdesign::Kind::field)
                {
                    k->slider.getProperties().set ("field", true);
                    k->slider.getProperties().set ("colour", c.colour.toString());
                    k->slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
                    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, c.bounds.getWidth(), c.bounds.getHeight());
                    k->slider.setMouseDragSensitivity (300);
                }
                else if (c.kind == effectdesign::Kind::fader)
                {
                    k->slider.setSliderStyle (juce::Slider::LinearVertical);
                    if (design->lcdValues || ! design->valueAbove)
                    {
                        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                        k->slider.setPopupDisplayEnabled (true, true, this);
                    }
                    else
                        k->slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, c.bounds.getWidth() + 30, 24);
                }
                else if (design->lcdValues)
                    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (96, c.bounds.getWidth()), 24);
                else if (design->valueAbove)
                    k->slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, c.bounds.getWidth(), 24);
                else
                {
                    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    k->slider.setPopupDisplayEnabled (true, true, this);
                }
                if (c.kind != effectdesign::Kind::field && ! c.colour.isTransparent())
                    k->slider.getProperties().set ("cap", c.colour.toString());   // (the knob's or fader cap's own colour)
                k->slider.lookAndFeelChanged();
                placed.component = &k->slider;
                break;
            }
            case effectdesign::Kind::segmented:
            {
                auto* row = new juce::Component();
                extras.add (row);
                const auto count = (int) p->choices.size();
                for (int n = 0; n < count; ++n)
                {
                    auto* b = new juce::TextButton (p->choices[(size_t) n]);
                    b->setRadioGroupId (1000 + extras.size());
                    b->setWantsKeyboardFocus (false);
                    b->onClick = [setValue, id = c.id, n, name = p->name] { setValue (id, name, (float) n); };
                    row->addAndMakeVisible (b);
                    extras.add (b);
                }
                addAndMakeVisible (row);
                refreshers.push_back ([row, current, id = c.id]
                {
                    const auto selected = juce::roundToInt (current (id));
                    for (int n = 0; n < row->getNumChildComponents(); ++n)
                        if (auto* b = dynamic_cast<juce::Button*> (row->getChildComponent (n)))
                            b->setToggleState (n == selected, juce::dontSendNotification);
                });
                placed.component = row;
                break;
            }
            case effectdesign::Kind::fileButton:
                break;
            case effectdesign::Kind::toggle:
            case effectdesign::Kind::hotspot:
            {
                auto* b = new juce::TextButton();
                extras.add (b);
                b->setWantsKeyboardFocus (false);
                if (c.kind == effectdesign::Kind::hotspot)
                    b->getProperties().set ("hotspot", true);
                const auto onText = caption ? c.label.substring (1) : p->choices.size() > 1 ? p->choices.back() : juce::String ("ON");
                const auto offText = caption ? c.label.substring (1) : p->choices.size() > 1 ? p->choices.front() : juce::String ("OFF");
                b->onClick = [setValue, current, id = c.id, name = p->name] { setValue (id, name, current (id) > 0.5f ? 0.0f : 1.0f); };
                addAndMakeVisible (b);
                refreshers.push_back ([b, current, id = c.id, onText, offText]
                {
                    const auto on = current (id) > 0.5f;
                    b->setToggleState (on, juce::dontSendNotification);
                    b->setButtonText ((on ? onText : offText).toUpperCase());
                });
                placed.component = b;
                break;
            }
            case effectdesign::Kind::dropdown:
            {
                auto* box = new juce::ComboBox();
                extras.add (box);
                for (size_t n = 0; n < p->choices.size(); ++n)
                    box->addItem (p->choices[n], (int) n + 1);
                box->getProperties().set ("centred", c.bounds.getHeight() >= 38);
                if (! c.colour.isTransparent())
                    box->getProperties().set ("ink", c.colour.toString());
                box->setWantsKeyboardFocus (false);
                box->onChange = [box, setValue, current, id = c.id, name = p->name]
                {
                    const auto chosen = (float) (box->getSelectedId() - 1);
                    if (box->getSelectedId() > 0 && chosen != current (id))
                        setValue (id, name, chosen);
                };
                addAndMakeVisible (box);
                refreshers.push_back ([box, current, id = c.id] { box->setSelectedId (juce::roundToInt (current (id)) + 1, juce::dontSendNotification); });
                placed.component = box;
                break;
            }
        }
        placedControls.push_back (placed);
    }

    for (const auto& graph : design->graphs)
    {
        /** Drags the curve's points: frequency across, gain up and down, the wheel for Q, double-click for on/off. */
        struct GraphEditor final : public juce::Component
        {
            GraphEditor (const effectdesign::Design::Graph& g, std::function<float (const juce::String&)> get,
                         std::function<void (const juce::String&, float, bool)> put, const EffectInfo& fx)
                : graph (g), current (std::move (get)), set (std::move (put)), info (fx) {}

            float xOf (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * (float) getWidth(); }
            double freqAt (float x) const { return juce::jlimit (10.0, 20000.0, 20.0 * std::pow (1000.0, (double) x / getWidth())); }
            // (up the side: the parameter's range, its middle at the middle)
            float yOf (const juce::String& id, double value) const
            {
                const auto* p = paramOf (id);
                if (p == nullptr) return (float) getHeight() * 0.5f;
                const auto t = (value - p->min) / std::max (1.0e-6, (double) (p->max - p->min));
                return (float) ((1.0 - t) * getHeight());
            }
            double valueAt (const juce::String& id, float y) const
            {
                const auto* p = paramOf (id);
                if (p == nullptr) return 0.0;
                return p->min + (1.0 - juce::jlimit (0.0, 1.0, (double) y / getHeight())) * (p->max - p->min);
            }

            const ParamInfo* paramOf (const juce::String& id) const
            {
                for (const auto& p : info.params)
                    if (p.id == id) return &p;
                return nullptr;
            }
            int handleAt (juce::Point<float> at) const
            {
                int best = -1;
                float bestDistance = 16.0f;
                for (size_t k = 0; k < graph.handles.size(); ++k)
                {
                    const auto& h = graph.handles[k];
                    const juce::Point<float> p (xOf (current (h.freq)), h.gain.isEmpty() ? (float) getHeight() * 0.5f : yOf (h.gain, current (h.gain)));
                    if (p.getDistanceFrom (at) < bestDistance) { bestDistance = p.getDistanceFrom (at); best = (int) k; }
                }
                return best;
            }
            void mouseDown (const juce::MouseEvent& e) override { dragging = handleAt (e.position); started = false; }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0) return;
                const auto& h = graph.handles[(size_t) dragging];
                const auto clampTo = [this] (const juce::String& id, double value)
                {
                    const auto* p = paramOf (id);
                    return (float) (p != nullptr ? juce::jlimit ((double) p->min, (double) p->max, value) : value);
                };
                set (h.freq, clampTo (h.freq, freqAt (e.position.x)), ! started);
                if (h.gain.isNotEmpty())
                    set (h.gain, clampTo (h.gain, std::round (valueAt (h.gain, e.position.y) * 100.0) / 100.0), false);
                started = true;
            }
            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                const auto k = handleAt (e.position);
                if (k >= 0 && graph.handles[(size_t) k].on.isNotEmpty())
                    set (graph.handles[(size_t) k].on, current (graph.handles[(size_t) k].on) > 0.5f ? 0.0f : 1.0f, true);
            }
            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
            {
                const auto k = handleAt (e.position);
                if (k < 0 || graph.handles[(size_t) k].q.isEmpty()) return;
                const auto& id = graph.handles[(size_t) k].q;
                const auto* p = paramOf (id);
                const auto q = current (id) * std::pow (1.15f, wheel.deltaY * 4.0f);
                set (id, p != nullptr ? juce::jlimit (p->min, p->max, q) : q, true);
            }
            void mouseMove (const juce::MouseEvent& e) override
            {
                setMouseCursor (handleAt (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            }

            const effectdesign::Design::Graph& graph;
            std::function<float (const juce::String&)> current;
            std::function<void (const juce::String&, float, bool)> set;
            const EffectInfo& info;
            int dragging = -1;
            bool started = false;
        };
        auto editor = std::make_unique<GraphEditor> (graph, current, [this, paramOf] (const juce::String& id, float value, bool newGesture)
        {
            if (newGesture)
            {
                const auto* p = paramOf (id);
                undoManager.beginNewTransaction (p != nullptr ? p->name : id);
            }
            insert.setProperty (juce::Identifier (id), value, &undoManager);
        }, info);
        editor->setBounds (graph.area);
        addAndMakeVisible (*editor);
        graphPages.push_back (graph.page);
        graphEditors.add (editor.release());
    }
}

void EffectPanel::setLiveValue (std::function<float()> source)
{
    liveValue = std::move (source);
    if (design != nullptr && design->live && liveValue != nullptr)
        startTimerHz (30);
}

void EffectPanel::setAnalyzerSource (std::function<double (float*, int)> source)
{
    analyzerSource = std::move (source);
    if (design != nullptr && design->analyzer && analyzerSource != nullptr)
        startTimerHz (30);
}

void EffectPanel::updateSpectrum()
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
    const auto rate = analyzerSource (fftData.data(), size);
    if (rate <= 0.0)
    {
        spectrum.valid = false;
        return;
    }
    for (int i = 0; i < size; ++i)
        fftData[(size_t) i] *= window[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);
    // Each point: the strongest bin around its frequency, in dB (a full-scale sine reads 0 dB); falls back gently
    const auto binHz = rate / size;
    for (int k = 0; k < effectdesign::Spectrum::points; ++k)
    {
        const auto f = effectdesign::Spectrum::frequencyOf (k);
        const auto lo = juce::jlimit (1, size / 2 - 1, (int) (f * std::pow (1000.0, -0.5 / effectdesign::Spectrum::points) / binHz));
        const auto hi = juce::jlimit (lo, size / 2 - 1, (int) std::ceil (f * std::pow (1000.0, 0.5 / effectdesign::Spectrum::points) / binHz));
        float m = 0.0f;
        if (hi - lo < 2)
        {
            // (low down, the bins are wider than the points: read between them)
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

void EffectPanel::timerCallback()
{
    if (analyzerSource != nullptr && design != nullptr && design->analyzer)
        updateSpectrum();
    // The meter: quick to rise, easing back
    const auto now = liveValue != nullptr ? liveValue() : 0.0f;
    liveShown = std::abs (now) > std::abs (liveShown) ? now : liveShown + (now - liveShown) * 0.25f;
    repaint();
}

EffectPanel::~EffectPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
    insert.removeListener (this);
}

void EffectPanel::refresh()
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    bypassButton.setToggleState ((bool) insert[ids::bypass], juce::dontSendNotification);

    if (info != nullptr)
        for (auto* k : knobs)
            for (const auto& p : info->params)
                if (p.id == k->id)
                    k->slider.setValue (insert.getProperty (juce::Identifier (k->id), p.defaultValue), juce::dontSendNotification);
    for (auto* k : knobs)
        k->slider.updateText();
    for (auto& r : refreshers)
        r();
    if (design != nullptr && design->pageParam.isNotEmpty() && info != nullptr)
    {
        // A face with pages: only the chosen page's controls (and those on every page)
        float page = 0.0f;
        for (const auto& p : info->params)
            if (p.id == design->pageParam)
                page = (float) (double) insert.getProperty (juce::Identifier (p.id), p.defaultValue);
        const auto shown = juce::roundToInt (page) + 1;
        for (auto& placed : placedControls)
        {
            const auto visible = placed.control.page == 0 || placed.control.page == shown;
            if (placed.component != nullptr) placed.component->setVisible (visible);
            if (placed.label != nullptr) placed.label->setVisible (visible);
        }
        for (int k = 0; k < graphEditors.size(); ++k)
            graphEditors[k]->setVisible (graphPages[(size_t) k] == 0 || graphPages[(size_t) k] == shown);
    }
    if (design != nullptr && design->enabled != nullptr && info != nullptr)
    {
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < info->params.size(); ++i)
            v[i] = (float) (double) insert.getProperty (juce::Identifier (info->params[i].id), info->params[i].defaultValue);
        for (auto& placed : placedControls)
        {
            const auto on = design->enabled (placed.control.id, *info, v.data());
            if (placed.component != nullptr)
            {
                placed.component->setEnabled (on);
                placed.component->setAlpha (on ? 1.0f : 0.35f);
            }
            if (placed.label != nullptr)
                placed.label->setAlpha (on ? 1.0f : 0.35f);
        }
    }
    if (design != nullptr)
        repaint();
    else if (! displayArea.isEmpty())
        repaint (displayArea);
}

void EffectPanel::paint (juce::Graphics& g)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    const auto accent = look->accent;
    if (design != nullptr && info != nullptr)
    {
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < info->params.size(); ++i)
            v[i] = (float) (double) insert.getProperty (juce::Identifier (info->params[i].id), info->params[i].defaultValue);
        effectdesign::setPaintingSpectrum (design->analyzer ? &spectrum : nullptr);
        design->paint (g, getLocalBounds(), *info, v.data(), liveShown);
        effectdesign::setPaintingSpectrum (nullptr);
        return;
    }

    // The face: brushed dark metal, a title bar with the effect's name and family
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3038), 0.0f, 0.0f, juce::Colour (0xff1a1d22), 0.0f, (float) getHeight(), false));
    g.fillAll();
    for (int y = 0; y < getHeight(); y += 3)
    {
        g.setColour (juce::Colours::white.withAlpha ((y / 3) % 2 == 0 ? 0.012f : 0.0f));
        g.fillRect (0, y, getWidth(), 1);
    }
    auto header = getLocalBounds().removeFromTop (headerHeight).toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4049), 0.0f, 0.0f, juce::Colour (0xff23272d), 0.0f, header.getBottom(), false));
    g.fillRect (header);
    g.setColour (accent);
    g.fillRect (header.removeFromBottom (2.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, (float) headerHeight, (float) getWidth(), 1.0f);
    auto title = getLocalBounds().removeFromTop (headerHeight - 2).reduced (14, 0).toFloat();
    g.setColour (accent);
    g.fillEllipse (title.getX(), title.getCentreY() - 4.0f, 8.0f, 8.0f);
    title.removeFromLeft (16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    const auto name = info != nullptr ? info->name : juce::String ("Effect");
    g.drawText (name, title, juce::Justification::centredLeft, false);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (info != nullptr ? info->category.toUpperCase() : juce::String(), title.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (juce::FontOptions (16.0f, juce::Font::bold), name) + 12.0f),
                juce::Justification::centredLeft, false);

    // The display
    if (info != nullptr && ! displayArea.isEmpty())
    {
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < info->params.size(); ++i)
            v[i] = (float) (double) insert.getProperty (juce::Identifier (info->params[i].id), info->params[i].defaultValue);
        effectface::drawDisplay (g, displayArea.toFloat(), *info, v.data(), accent);
    }

    // The sections: inset panels with their names
    for (const auto& box : sectionBoxes)
    {
        auto b = box.area.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        if (box.title.isNotEmpty())
        {
            g.setColour (accent.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (box.title.toUpperCase(), b.removeFromTop ((float) sectionTitleHeight).reduced (8.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

int EffectPanel::layoutFace (bool apply)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    if (! apply && info != nullptr)
    {
        // As wide as all the sections side by side (up to about a thousand pixels; then they wrap)
        int total = 0, index = 0;
        for (const auto& section : effectface::sectionsFor (*info))
        {
            const auto count = juce::jmin (section.count, knobs.size() - index);
            index += juce::jmax (0, count);
            total += (section.vertical ? 1 : juce::jmin (count, 12)) * cellWidth + 2 * sectionPad + sectionGap;
        }
        faceWidth = juce::jlimit (effectface::hasDisplay (*info) ? 520 : 360, 1000, total - sectionGap + 28);
    }
    const auto width = apply ? getWidth() : faceWidth;
    auto y = headerHeight + 10;
    if (apply)
    {
        bypassButton.setBounds (width - 108, (headerHeight - 24) / 2, 96, 24);
        displayArea = {};
    }
    if (info != nullptr && effectface::hasDisplay (*info))
    {
        if (apply)
            displayArea = { 14, y, width - 28, displayHeight };
        y += displayHeight + 10;
    }
    if (info == nullptr)
        return y + 10;

    // Sections flow left to right, wrapping; a long one breaks into rows of its own
    if (apply)
        sectionBoxes.clear();
    const auto inner = width - 28;
    const auto perRow = juce::jmax (1, (inner - 2 * sectionPad) / cellWidth);
    int x = 14, rowHeight = 0, index = 0, widest = 0;
    for (const auto& section : effectface::sectionsFor (*info))
    {
        const auto count = juce::jmin (section.count, knobs.size() - index);
        if (count <= 0)
            break;
        const auto across = section.vertical ? 1 : juce::jmin (count, perRow);
        const auto down = section.vertical ? count : (count + perRow - 1) / perRow;
        const auto w = across * cellWidth + 2 * sectionPad;
        const auto h = sectionTitleHeight + down * cellHeight + sectionPad;
        if (x > 14 && x + w > 14 + inner)
        {
            x = 14;
            y += rowHeight + sectionGap;
            rowHeight = 0;
        }
        if (apply)
        {
            sectionBoxes.push_back ({ section.title, { x, y, w, h } });
            for (int k = 0; k < count; ++k)
            {
                const auto col = section.vertical ? 0 : k % perRow, row = section.vertical ? k : k / perRow;
                auto cell = juce::Rectangle<int> (x + sectionPad + col * cellWidth, y + sectionTitleHeight + row * cellHeight, cellWidth, cellHeight);
                auto* knob = knobs[index + k];
                knob->label.setBounds (cell.removeFromTop (16));
                knob->slider.setBounds (cell.reduced (4, 0));
            }
        }
        index += count;
        x += w + sectionGap;
        widest = juce::jmax (widest, x - sectionGap);
        rowHeight = juce::jmax (rowHeight, h);
    }
    juce::ignoreUnused (widest);
    return y + rowHeight + 14;
}

void EffectPanel::resized()
{
    if (design == nullptr)
    {
        layoutFace (true);
        return;
    }
    for (auto& placed : placedControls)
    {
        auto bounds = placed.control.bounds;
        const bool above = placed.control.kind == effectdesign::Kind::knob || placed.control.kind == effectdesign::Kind::fader;
        if (placed.label != nullptr)
            placed.label->setBounds (above ? bounds.removeFromTop (22) : bounds.withY (bounds.getY() - 24).withHeight (22).expanded (20, 0));
        if (placed.component == nullptr)
            continue;
        placed.component->setBounds (bounds);
        if (placed.control.kind == effectdesign::Kind::segmented)
        {
            auto row = placed.component->getLocalBounds();
            const auto count = placed.component->getNumChildComponents();
            for (int n = 0; n < count; ++n)
                placed.component->getChildComponent (n)->setBounds (row.removeFromLeft (row.getWidth() / (count - n)).reduced (2, 0));
        }
    }
}

} // namespace riff
