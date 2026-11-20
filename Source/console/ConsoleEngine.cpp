#include "ConsoleEngine.h"
#include "../engine/ClapFormat.h"
#include "../engine/PluginScanChild.h"
#include <algorithm>
#include <iterator>

namespace atpconsole
{

namespace
{
    // Fader scale: dB at each point along the fader, top (0) to bottom (1), as a console is printed.
    struct ScalePoint { double db, fromTop; };
    constexpr ScalePoint faderScale[] = { { 10.0, 0.0 }, { 5.0, 0.11 }, { 0.0, 0.23 }, { -5.0, 0.35 }, { -10.0, 0.47 },
                                          { -20.0, 0.64 }, { -30.0, 0.76 }, { -40.0, 0.86 }, { -60.0, 0.94 },
                                          { (double) minusInfinityDb, 1.0 } };

    inline float gainOf (float db) noexcept { return juce::Decibels::decibelsToGain (db, minusInfinityDb); }

    const juce::Colour stripColours[] = { juce::Colour (0xfff5a742), juce::Colour (0xffe77fb3), juce::Colour (0xff5ab4e8),
                                          juce::Colour (0xffa88bf0), juce::Colour (0xff9bd34a), juce::Colour (0xfff2d14b) };
}

double faderProportion (double db)
{
    if (db >= faderScale[0].db)
        return 1.0;
    for (size_t i = 0; i + 1 < std::size (faderScale); ++i)
    {
        const auto& a = faderScale[i];
        const auto& b = faderScale[i + 1];
        if (db <= a.db && db >= b.db)
            return 1.0 - (a.fromTop + (a.db - db) / (a.db - b.db) * (b.fromTop - a.fromTop));
    }
    return 0.0;
}

double faderValue (double proportion)
{
    const auto fromTop = 1.0 - juce::jlimit (0.0, 1.0, proportion);
    for (size_t i = 0; i + 1 < std::size (faderScale); ++i)
    {
        const auto& a = faderScale[i];
        const auto& b = faderScale[i + 1];
        if (fromTop >= a.fromTop && fromTop <= b.fromTop)
            return a.db + (fromTop - a.fromTop) / (b.fromTop - a.fromTop) * (b.db - a.db);
    }
    return minusInfinityDb;
}

//==============================================================================
Engine::Engine()
{
    juce::addDefaultFormatsToManager (formats);
    formats.addFormat (std::make_unique<riff::ClapPluginFormat>());
    knownPlugins.setCustomScanner (std::make_unique<riff::pluginscan::OutOfProcessScanner>());   // (a crashing plugin can't take the console down)

    fxBus = std::make_unique<Channel> (Kind::fx);
    fxBus->name = "FX Return";
    fxBus->colour = juce::Colour (0xff9bd34a);
    fxBus->faderDb = -6.0f;

    for (size_t i = 0; i < cues.size(); ++i)
    {
        cues[i] = std::make_unique<Channel> (Kind::cue);
        cues[i]->name = "Cue " + juce::String::charToString ((juce::juce_wchar) ('A' + i));
        cues[i]->colour = juce::Colour (0xff8c9aa1);
        cues[i]->fxReturnDb = -6.0f;
    }

    mainBus = std::make_unique<Channel> (Kind::main);
    mainBus->name = "Main";
    mainBus->colour = juce::Colours::white;

    deviceManager.addChangeListener (this);
}

Engine::~Engine()
{
    shutdown();
}

void Engine::start (const juce::XmlElement* savedDevice)
{
    // (as many channels as the interface has: every input becomes a strip)
    const auto error = deviceManager.initialise (256, 256, savedDevice, true);
    if (error.isNotEmpty())
        DBG ("Audio device: " << error);
    syncToDevice();
    deviceManager.addAudioCallback (this);
    hardware.start();
}

void Engine::shutdown()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.removeChangeListener (this);
    deviceManager.closeAudioDevice();
    hardware.stop();
    forEachChannel ([this] (Channel& c) { deletePluginsOf (c); });
}

Channel& Engine::bus (Kind k)
{
    switch (k)
    {
        case Kind::fx:   return *fxBus;
        case Kind::cue:  return *cues[0];
        case Kind::main:
        case Kind::input:
        default:         return *mainBus;
    }
}

void Engine::forEachChannel (const std::function<void (Channel&)>& f)
{
    for (auto& s : strips)
        f (*s);
    f (*fxBus);
    for (auto& c : cues)
        f (*c);
    f (*mainBus);
}

void Engine::deletePluginsOf (Channel& c)
{
    for (auto& slot : c.slots)
        if (slot.plugin != nullptr)
            setPlugin (c, (int) (&slot - c.slots.data()), nullptr);
}

void Engine::allocate (Channel& c)
{
    c.buffer.setSize (maxChannels, capacity, false, true, true);
    c.buffer.clear();
}

//==============================================================================
void Engine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncToDevice();
}

void Engine::syncToDevice()
{
    juce::StringArray inNames, outNames;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto allIn = device->getInputChannelNames(), allOut = device->getOutputChannelNames();
        const auto activeIn = device->getActiveInputChannels(), activeOut = device->getActiveOutputChannels();
        for (int i = 0; i < allIn.size(); ++i)
            if (activeIn[i])
                inNames.add (allIn[i]);
        for (int i = 0; i < allOut.size(); ++i)
            if (activeOut[i])
                outNames.add (allOut[i]);
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    const bool outputsChanged = outNames != outputNames;
    const bool firstDevice = outputNames.isEmpty();
    std::vector<std::unique_ptr<Channel>> removed;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        capacity = juce::jmax (capacity, blockSize, 512);

        while ((int) strips.size() > inNames.size())
        {
            removed.push_back (std::move (strips.back()));
            strips.pop_back();
        }
        for (int i = 0; i < inNames.size(); ++i)
        {
            const bool isNew = i >= (int) strips.size();
            if (isNew)
            {
                auto s = std::make_unique<Channel> (Kind::input);
                s->name = inNames[i];
                s->colour = stripColours[(size_t) (i / 2) % std::size (stripColours)];
                strips.push_back (std::move (s));
            }
            auto& s = *strips[(size_t) i];
            s.input = i;
            s.sourceName = inNames[i];
            if (isNew && i > 0 && inNames[i].containsIgnoreCase ("loopback") && inNames[i - 1].containsIgnoreCase ("loopback")
                && ! strips[(size_t) i - 1]->stereo && strips[(size_t) i - 1]->visible)
                strips[(size_t) i - 1]->stereo = true;   // (an interface's loopback pair is one stereo strip)
        }
        forEachChannel ([this] (Channel& c) { if (c.buffer.getNumSamples() < capacity) allocate (c); });
        updateVisibility();
        outputNames = outNames;
    }

    for (auto& r : removed)
        deletePluginsOf (*r);
    removed.clear();

    if (outputsChanged)
        applyDefaultOutputs (firstDevice);

    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

void Engine::applyDefaultOutputs (bool firstTime)
{
    const int n = outputNames.size();
    if (firstTime)
    {
        mainBus->output = n >= 2 ? 0 : -1;
        cues[0]->output = n >= 4 ? 2 : -1;   // (headphones on their own outputs where there are any)
        cues[1]->output = n >= 6 ? 4 : -1;
    }
    if (mainBus->output.load() < 0 && n >= 2)
        mainBus->output = 0;                 // (always something to listen on)
    fxBus->output = -1;                      // (the FX bus returns into the mixes)
    auto clamp = [n] (std::atomic<int>& o) { if (o.load() >= n) o.store (-1); };
    clamp (mainBus->output);
    for (auto& c : cues)
        clamp (c->output);
    for (auto& s : strips)
        clamp (s->output);
}

void Engine::updateVisibility()
{
    for (size_t i = 0; i < strips.size(); ++i)
    {
        auto& s = *strips[i];
        s.visible = ! (i > 0 && strips[i - 1]->visible && strips[i - 1]->stereo);
        if (s.stereo && (i + 1 >= strips.size()))
            s.stereo = false;   // (no partner input)
    }
}

void Engine::setStereo (int index, bool stereo)
{
    if (index < 0 || index >= numStrips())
        return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        strips[(size_t) index]->stereo = stereo;
        updateVisibility();
    }
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> Engine::createPlugin (const juce::PluginDescription& d, juce::String& error)
{
    auto plugin = formats.createPluginInstance (d, sampleRate, blockSize, error);
    if (plugin == nullptr)
        return nullptr;

    // Stereo in and out where the plugin can (a mono mic is fed to both sides)
    auto layout = plugin->getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (plugin->checkBusesLayoutSupported (layout))
        plugin->setBusesLayout (layout);

    if (juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()) > maxChannels)
    {
        error = d.name + " needs more than " + juce::String (maxChannels) + " channels.";
        return nullptr;
    }

    plugin->setNonRealtime (false);
    plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);
    return plugin;
}

void Engine::setPlugin (Channel& c, int slot, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (c.slots[(size_t) slot].plugin);
        c.slots[(size_t) slot].plugin = std::move (plugin);
        c.slots[(size_t) slot].bypass = false;
    }
    if (old != nullptr)
    {
        if (onPluginWillBeDeleted != nullptr)
            onPluginWillBeDeleted (old.get());
        old->releaseResources();
        old.reset();   // (deleted here, outside the lock)
    }
}

//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    const juce::SpinLock::ScopedLockType sl (lock);
    capacity = juce::jmax (blockSize, 512);
    forEachChannel ([this] (Channel& c)
    {
        allocate (c);
        c.lastTrim = 1.0f;
        c.lastMainL = c.lastMainR = c.lastFxRet = 0.0f;
        c.lastSend = {};
        for (auto& slot : c.slots)
            if (slot.plugin != nullptr)
            {
                slot.plugin->releaseResources();
                slot.plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
                slot.plugin->prepareToPlay (sampleRate, blockSize);
            }
    });
    midi.ensureSize (2048);
}

void Engine::audioDeviceStopped() {}

void Engine::runPlugin (juce::AudioPluginInstance& p, juce::AudioBuffer<float>& b, juce::MidiBuffer& midi, int n)
{
    const int ins = p.getTotalNumInputChannels(), outs = p.getTotalNumOutputChannels();
    const int chans = juce::jmax (ins, outs);
    if (chans <= 0 || chans > b.getNumChannels())
        return;
    for (int c = 2; c < chans; ++c)
        b.clear (c, 0, n);   // (side-chains and extra buses get silence)
    b.setNotClear();   // (the plugin writes through `view`, so `b` can't know its samples changed)
    juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), chans, n);
    midi.clear();
    p.processBlock (view, midi);
    if (outs == 1)
        b.copyFrom (1, 0, b, 0, 0, n);   // (a mono plugin: its output on both sides)
}

void Engine::runChain (Channel& c, int n)
{
    for (auto& slot : c.slots)
        if (slot.plugin != nullptr && ! slot.bypass.load (std::memory_order_relaxed))
            runPlugin (*slot.plugin, c.buffer, midi, n);
}

void Engine::writeOutput (const juce::AudioBuffer<float>& b, int first, float* const* out, int numOut, int n)
{
    if (first < 0)
        return;
    for (int c = 0; c < 2; ++c)
        if (first + c < numOut && out[first + c] != nullptr)
            juce::FloatVectorOperations::add (out[first + c], b.getReadPointer (c), n);
}

void Engine::processStrip (Channel& s, const float* const* in, int numIn, float* const* out, int numOut, int n, bool anySolo)
{
    auto& b = s.buffer;

    // 1. The input(s)
    const float* left = juce::isPositiveAndBelow (s.input, numIn) ? in[s.input] : nullptr;
    const float* right = (s.stereo && juce::isPositiveAndBelow (s.input + 1, numIn)) ? in[s.input + 1] : left;
    if (left == nullptr)
    {
        b.clear (0, n);
        return;
    }
    b.copyFrom (0, 0, left, n);
    b.copyFrom (1, 0, right != nullptr ? right : left, n);
    if (b.getMagnitude (0, 0, n) >= 0.999f || b.getMagnitude (1, 0, n) >= 0.999f)
        s.meter.clip.store (true, std::memory_order_relaxed);

    // 2. Trim and polarity, then the PRE slot and the inserts
    const float trim = gainOf (s.trimDb.load (std::memory_order_relaxed)) * (s.polarity.load (std::memory_order_relaxed) ? -1.0f : 1.0f);
    for (int c = 0; c < 2; ++c)
        b.applyGainRamp (c, 0, n, s.lastTrim, trim);
    s.lastTrim = trim;
    runChain (s, n);

    // 3. Direct out: the processed signal, before the fader (to record it)
    if (const int d = s.output.load (std::memory_order_relaxed); d >= 0)
    {
        if (d < numOut && out[d] != nullptr)
            juce::FloatVectorOperations::add (out[d], b.getReadPointer (0), n);
        if (s.stereo && d + 1 < numOut && out[d + 1] != nullptr)
            juce::FloatVectorOperations::add (out[d + 1], b.getReadPointer (1), n);
    }

    // 4. Pan (balance: the centre is unity), mute, solo, fader
    const float p = s.pan.load (std::memory_order_relaxed);
    const float panL = juce::jmin (1.0f, 1.0f - p), panR = juce::jmin (1.0f, 1.0f + p);
    const bool muted = s.mute.load (std::memory_order_relaxed);
    const bool silencedInMain = muted || (anySolo && ! s.solo.load (std::memory_order_relaxed));
    const float fader = silencedInMain ? 0.0f : gainOf (s.faderDb.load (std::memory_order_relaxed));

    // 5. Sends: the cues before the fader (the artist's mix doesn't follow yours), the FX after it
    Channel* targets[numSends] = { cues[0].get(), cues[1].get(), fxBus.get() };
    for (int i = 0; i < numSends; ++i)
    {
        float g = muted ? 0.0f : gainOf (s.sendDb[(size_t) i].load (std::memory_order_relaxed));
        if (i == sendFx)
            g *= fader;
        const float gl = g * panL, gr = g * panR;
        auto& last = s.lastSend[(size_t) i];
        targets[i]->buffer.addFromWithRamp (0, 0, b.getReadPointer (0), n, last[0], gl);
        targets[i]->buffer.addFromWithRamp (1, 0, b.getReadPointer (1), n, last[1], gr);
        last = { gl, gr };
    }

    const float gl = fader * panL, gr = fader * panR;
    mainBus->buffer.addFromWithRamp (0, 0, b.getReadPointer (0), n, s.lastMainL, gl);
    mainBus->buffer.addFromWithRamp (1, 0, b.getReadPointer (1), n, s.lastMainR, gr);
    s.lastMainL = gl;
    s.lastMainR = gr;

    s.meter.push (b.getMagnitude (0, 0, n) * (muted ? 0.0f : gainOf (s.faderDb.load()) * panL),
                  b.getMagnitude (1, 0, n) * (muted ? 0.0f : gainOf (s.faderDb.load()) * panR));
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                               int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], n);

    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || n > capacity || n <= 0)
        return;   // (the structure is being changed: one block of silence)

    const juce::ScopedNoDenormals noDenormals;

    fxBus->buffer.clear (0, n);
    mainBus->buffer.clear (0, n);
    for (auto& c : cues)
        c->buffer.clear (0, n);

    bool anySolo = false;
    for (auto& s : strips)
        anySolo = anySolo || (s->visible && s->solo.load (std::memory_order_relaxed));

    for (auto& s : strips)
        if (s->visible)
            processStrip (*s, in, numIn, out, numOut, n, anySolo);

    // The FX bus: its plugins (a reverb), then into the Main mix and each cue
    {
        auto& fx = *fxBus;
        runChain (fx, n);
        const float g = fx.mute.load() ? 0.0f : gainOf (fx.faderDb.load (std::memory_order_relaxed));
        fx.buffer.applyGainRamp (0, n, fx.lastMainL, g);
        fx.lastMainL = g;
        mainBus->buffer.addFrom (0, 0, fx.buffer, 0, 0, n);
        mainBus->buffer.addFrom (1, 0, fx.buffer, 1, 0, n);
        for (auto& c : cues)
        {
            const float r = gainOf (c->fxReturnDb.load (std::memory_order_relaxed));
            c->buffer.addFromWithRamp (0, 0, fx.buffer.getReadPointer (0), n, c->lastFxRet, r);
            c->buffer.addFromWithRamp (1, 0, fx.buffer.getReadPointer (1), n, c->lastFxRet, r);
            c->lastFxRet = r;
        }
        fx.meter.push (fx.buffer.getMagnitude (0, 0, n), fx.buffer.getMagnitude (1, 0, n));
    }

    // Talkback: the chosen mic into the cues, and the Main mix dimmed while it's held
    const bool tb = talkback.load (std::memory_order_relaxed);
    const int tbIn = talkbackInput.load (std::memory_order_relaxed);

    for (auto& cptr : cues)
    {
        auto& c = *cptr;
        if (tb && juce::isPositiveAndBelow (tbIn, numIn) && in[tbIn] != nullptr)
        {
            c.buffer.addFrom (0, 0, in[tbIn], n, 0.7f);
            c.buffer.addFrom (1, 0, in[tbIn], n, 0.7f);
        }
        runChain (c, n);
        const float g = c.mute.load() ? 0.0f : gainOf (c.faderDb.load (std::memory_order_relaxed));
        c.buffer.applyGainRamp (0, n, c.lastMainL, g);
        c.lastMainL = g;
        c.meter.push (c.buffer.getMagnitude (0, 0, n), c.buffer.getMagnitude (1, 0, n));
        writeOutput (c.buffer, c.output.load (std::memory_order_relaxed), out, numOut, n);
    }

    auto& m = *mainBus;
    runChain (m, n);
    const float g = m.mute.load() ? 0.0f : gainOf (m.faderDb.load (std::memory_order_relaxed)) * (tb ? 0.1f : 1.0f);
    m.buffer.applyGainRamp (0, n, m.lastMainL, g);
    m.lastMainL = g;
    m.meter.push (m.buffer.getMagnitude (0, 0, n), m.buffer.getMagnitude (1, 0, n));
    writeOutput (m.buffer, m.output.load (std::memory_order_relaxed), out, numOut, n);
}

//==============================================================================
double Engine::getRoundTripMs() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice(); d != nullptr && d->getCurrentSampleRate() > 0)
        return (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples())
               * 1000.0 / d->getCurrentSampleRate();
    return 0.0;
}

int Engine::getXRuns() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getXRunCount();
    return -1;
}

juce::String Engine::getDeviceName() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getTypeName() + " · " + d->getName();
    return "No audio interface";
}

int Engine::countPlugins() const
{
    int count = 0;
    auto add = [&count] (const Channel& c) { for (auto& s : c.slots) count += s.plugin != nullptr ? 1 : 0; };
    for (auto& s : strips) add (*s);
    add (*fxBus);
    for (auto& c : cues) add (*c);
    add (*mainBus);
    return count;
}

int Engine::maxPluginLatencySamples() const
{
    int worst = 0;
    auto check = [&worst] (const Channel& c)
    {
        int sum = 0;
        for (auto& s : c.slots)
            if (s.plugin != nullptr && ! s.bypass.load())
                sum += s.plugin->getLatencySamples();
        worst = juce::jmax (worst, sum);
    };
    for (auto& s : strips) check (*s);
    check (*fxBus);
    for (auto& c : cues) check (*c);
    check (*mainBus);
    return worst;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Engine::saveChannel (const Channel& c, const juce::String& tag)
{
    auto x = std::make_unique<juce::XmlElement> (tag);
    x->setAttribute ("name", c.name);
    x->setAttribute ("colour", c.colour.toString());
    x->setAttribute ("trim", c.trimDb.load());
    x->setAttribute ("fader", c.faderDb.load());
    x->setAttribute ("pan", c.pan.load());
    x->setAttribute ("mute", c.mute.load());
    x->setAttribute ("solo", c.solo.load());
    x->setAttribute ("polarity", c.polarity.load());
    x->setAttribute ("cueA", c.sendDb[0].load());
    x->setAttribute ("cueB", c.sendDb[1].load());
    x->setAttribute ("fx", c.sendDb[2].load());
    x->setAttribute ("fxReturn", c.fxReturnDb.load());
    x->setAttribute ("output", c.output.load());
    x->setAttribute ("stereo", c.stereo);

    for (int i = 0; i < numSlots; ++i)
    {
        const auto& slot = c.slots[(size_t) i];
        if (slot.plugin == nullptr)
            continue;
        auto* s = x->createNewChildElement ("SLOT");
        s->setAttribute ("index", i);
        s->setAttribute ("bypass", slot.bypass.load());
        s->addChildElement (slot.plugin->getPluginDescription().createXml().release());
        juce::MemoryBlock state;
        slot.plugin->getStateInformation (state);
        s->setAttribute ("state", state.toBase64Encoding());
    }
    return x;
}

void Engine::loadChannel (Channel& c, const juce::XmlElement& x, juce::StringArray& problems)
{
    c.name = x.getStringAttribute ("name", c.name);
    c.colour = juce::Colour::fromString (x.getStringAttribute ("colour", c.colour.toString()));
    c.trimDb = (float) x.getDoubleAttribute ("trim", 0.0);
    c.faderDb = (float) x.getDoubleAttribute ("fader", 0.0);
    c.pan = (float) x.getDoubleAttribute ("pan", 0.0);
    c.mute = x.getBoolAttribute ("mute");
    c.solo = x.getBoolAttribute ("solo");
    c.polarity = x.getBoolAttribute ("polarity");
    c.sendDb[0] = (float) x.getDoubleAttribute ("cueA", minusInfinityDb);
    c.sendDb[1] = (float) x.getDoubleAttribute ("cueB", minusInfinityDb);
    c.sendDb[2] = (float) x.getDoubleAttribute ("fx", minusInfinityDb);
    c.fxReturnDb = (float) x.getDoubleAttribute ("fxReturn", minusInfinityDb);
    const int out = x.getIntAttribute ("output", -1);
    c.output = out < outputNames.size() ? out : -1;

    for (int i = 0; i < numSlots; ++i)
        setPlugin (c, i, nullptr);

    for (auto* s : x.getChildWithTagNameIterator ("SLOT"))
    {
        const int index = s->getIntAttribute ("index", -1);
        juce::PluginDescription d;
        auto* dx = s->getChildByName ("PLUGIN");
        if (! juce::isPositiveAndBelow (index, numSlots) || dx == nullptr || ! d.loadFromXml (*dx))
            continue;
        juce::String error;
        auto plugin = createPlugin (d, error);
        if (plugin == nullptr)
        {
            problems.add (c.name + ": " + d.name + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
            continue;
        }
        juce::MemoryBlock state;
        if (state.fromBase64Encoding (s->getStringAttribute ("state")) && state.getSize() > 0)
            plugin->setStateInformation (state.getData(), (int) state.getSize());
        setPlugin (c, index, std::move (plugin));
        c.slots[(size_t) index].bypass = s->getBoolAttribute ("bypass");
    }
}

std::unique_ptr<juce::XmlElement> Engine::saveSession() const
{
    auto root = std::make_unique<juce::XmlElement> ("ATPCONSOLE");
    root->setAttribute ("version", 1);
    root->setAttribute ("talkbackInput", talkbackInput.load());
    if (auto device = deviceManager.createStateXml())
        root->createNewChildElement ("DEVICE")->addChildElement (device.release());

    for (size_t i = 0; i < strips.size(); ++i)
    {
        auto x = saveChannel (*strips[i], "STRIP");
        x->setAttribute ("input", (int) i);
        root->addChildElement (x.release());
    }
    root->addChildElement (saveChannel (*fxBus, "FXBUS").release());
    for (size_t i = 0; i < cues.size(); ++i)
    {
        auto x = saveChannel (*cues[i], "CUE");
        x->setAttribute ("index", (int) i);
        root->addChildElement (x.release());
    }
    root->addChildElement (saveChannel (*mainBus, "MAIN").release());
    return root;
}

void Engine::loadSession (const juce::XmlElement& root, juce::StringArray& problems, bool restoreDevice)
{
    if (! root.hasTagName ("ATPCONSOLE"))
        return;

    // The device first (this makes the strips)
    if (auto* d = root.getChildByName ("DEVICE"); restoreDevice && d != nullptr && d->getFirstChildElement() != nullptr)
    {
        deviceManager.initialise (256, 256, d->getFirstChildElement(), true);
        syncToDevice();
    }

    talkbackInput = root.getIntAttribute ("talkbackInput", -1);

    for (auto* x : root.getChildWithTagNameIterator ("STRIP"))
    {
        const int i = x->getIntAttribute ("input", -1);
        if (juce::isPositiveAndBelow (i, numStrips()))
        {
            loadChannel (strip (i), *x, problems);
            setStereo (i, x->getBoolAttribute ("stereo"));
        }
    }
    if (auto* x = root.getChildByName ("FXBUS"))
        loadChannel (*fxBus, *x, problems);
    for (auto* x : root.getChildWithTagNameIterator ("CUE"))
        if (const int i = x->getIntAttribute ("index", -1); juce::isPositiveAndBelow (i, (int) cues.size()))
            loadChannel (*cues[(size_t) i], *x, problems);
    if (auto* x = root.getChildByName ("MAIN"))
        loadChannel (*mainBus, *x, problems);

    applyDefaultOutputs (false);
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

void Engine::resetToDefaults()
{
    forEachChannel ([this] (Channel& c)
    {
        deletePluginsOf (c);
        c.trimDb = 0.0f;
        c.faderDb = c.kind == Kind::fx ? -6.0f : 0.0f;
        c.pan = 0.0f;
        c.mute = c.solo = c.polarity = false;
        for (auto& s : c.sendDb) s = minusInfinityDb;
        c.fxReturnDb = c.kind == Kind::cue ? -6.0f : minusInfinityDb;
        if (c.kind == Kind::input)
        {
            c.output = -1;
            c.name = c.sourceName;
        }
    });
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& s : strips) s->stereo = false;
        updateVisibility();
    }
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

} // namespace atpconsole
