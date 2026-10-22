#include "InterfaceControl.h"
#include <numeric>

namespace riff
{
namespace ucnet
{
    juce::MemoryBlock encode (const juce::String& code, const juce::MemoryBlock& payload, std::array<juce::uint8, 4> cbytes)
    {
        juce::MemoryOutputStream out;
        out.write ("UC\0\1", 4);
        out.writeShort ((short) (6 + payload.getSize()));   // (little-endian)
        out.write (code.toRawUTF8(), 2);
        out.write (cbytes.data(), 4);
        out.write (payload.getData(), payload.getSize());
        return out.getMemoryBlock();
    }

    std::vector<Message> Decoder::feed (const void* data, size_t size)
    {
        buffer.append (data, size);
        std::vector<Message> out;
        for (;;)
        {
            const auto* bytes = static_cast<const juce::uint8*> (buffer.getData());
            const auto n = buffer.getSize();
            size_t start = 0;
            while (start + 4 <= n && ! (bytes[start] == 'U' && bytes[start + 1] == 'C' && bytes[start + 2] == 0 && bytes[start + 3] == 1))
                ++start;
            if (start + 4 > n)
            {
                // (keep a tail that might be the start of a frame)
                juce::MemoryBlock tail (bytes + (n >= 3 ? n - 3 : 0), std::min<size_t> (n, 3));
                buffer = tail;
                return out;
            }
            if (start > 0)
            {
                buffer.removeSection (0, start);
                continue;
            }
            if (n < 6)
                return out;
            const auto frame = (size_t) (bytes[4] | (bytes[5] << 8));
            if (frame < 6)
            {
                buffer.removeSection (0, 1);   // (junk that looked like a start)
                continue;
            }
            if (n < 6 + frame)
                return out;
            Message m;
            m.code = juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes + 6), 2);
            std::copy (bytes + 8, bytes + 12, m.cbytes.begin());
            m.payload.append (bytes + 12, frame - 6);
            out.push_back (std::move (m));
            buffer.removeSection (0, 6 + frame);
        }
    }

    juce::MemoryBlock jsonPayload (const juce::var& json)
    {
        const auto text = juce::JSON::toString (json, true);
        juce::MemoryOutputStream out;
        out.writeInt ((int) text.getNumBytesAsUTF8());
        out.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
        return out.getMemoryBlock();
    }

    juce::var parseJson (const Message& m)
    {
        if (m.payload.getSize() < 4)
            return {};
        const auto* p = static_cast<const char*> (m.payload.getData());
        const auto n = std::min<size_t> ((size_t) juce::ByteOrder::littleEndianInt (p), m.payload.getSize() - 4);
        return juce::JSON::parse (juce::String::fromUTF8 (p + 4, (int) n));
    }

    juce::MemoryBlock pvPayload (const juce::String& path, float normalized)
    {
        juce::MemoryOutputStream out;
        out.write (path.toRawUTF8(), path.getNumBytesAsUTF8());
        out.writeByte (0);
        out.writeShort (0);
        out.writeFloat (normalized);
        return out.getMemoryBlock();
    }

    bool parsePv (const Message& m, juce::String& path, float& normalized)
    {
        const auto* p = static_cast<const char*> (m.payload.getData());
        const auto size = m.payload.getSize();
        size_t end = 0;
        while (end < size && p[end] != 0)
            ++end;
        if (end + 7 > size)
            return false;
        path = juce::String::fromUTF8 (p, (int) end);
        float v;
        std::memcpy (&v, p + size - 4, 4);   // (little-endian, as every Mac and PC is)
        normalized = v;
        return true;
    }

    juce::var parseState (const Message& m)
    {
        if (m.payload.getSize() <= 4)
            return {};
        juce::MemoryInputStream compressed (static_cast<const char*> (m.payload.getData()) + 4, m.payload.getSize() - 4, false);
        juce::GZIPDecompressorInputStream unzip (&compressed, false, juce::GZIPDecompressorInputStream::zlibFormat);
        juce::MemoryOutputStream text;
        text.writeFromInputStream (unzip, -1);
        return juce::JSON::parse (text.toString());
    }

    void flatten (const juce::var& node, const juce::String& prefix, std::map<juce::String, juce::var>& values,
                  std::map<juce::String, juce::var>& ranges)
    {
        const auto join = [&prefix] (const juce::Identifier& key) { return prefix.isEmpty() ? key.toString() : prefix + "/" + key.toString(); };
        if (auto* v = node.getProperty ("values", {}).getDynamicObject())
            for (const auto& p : v->getProperties())
                values[join (p.name)] = p.value;
        if (auto* r = node.getProperty ("ranges", {}).getDynamicObject())
            for (const auto& p : r->getProperties())
                ranges[join (p.name)] = p.value;
        if (auto* c = node.getProperty ("children", {}).getDynamicObject())
            for (const auto& p : c->getProperties())
                flatten (p.value, join (p.name), values, ranges);
    }
}

//==============================================================================
InterfaceControl::InterfaceControl() : juce::Thread ("Interface control")
{
    findPorts = [] { return defaultPorts(); };
}

InterfaceControl::~InterfaceControl()
{
    stop();
}

void InterfaceControl::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void InterfaceControl::stop()
{
    // Only the thread touches the socket (closing it from here raced with its reads: a crash on Windows).
    // It notices within a few tens of milliseconds, says goodbye to the service and closes it.
    stopThread (5000);
}

juce::Array<int> InterfaceControl::defaultPorts()
{
    juce::Array<int> ports;
   #if JUCE_MAC
    // The service's listening ports on this computer (they change when it restarts)
    juce::ChildProcess netstat;
    if (netstat.start ("netstat -anv -p tcp", juce::ChildProcess::wantStdOut))
    {
        const auto text = netstat.readAllProcessOutput();
        for (const auto& line : juce::StringArray::fromLines (text))
            if (line.contains ("LISTEN") && line.contains ("ucdaemon"))
            {
                const auto cols = juce::StringArray::fromTokens (line, " \t", "");
                juce::StringArray kept;
                for (const auto& c : cols) if (c.isNotEmpty()) kept.add (c);
                if (kept.size() > 3 && kept[3].startsWith ("127.0.0.1."))
                    ports.addIfNotAlreadyThere (kept[3].fromLastOccurrenceOf (".", false, false).getIntValue());
            }
    }
   #endif
    ports.addIfNotAlreadyThere (59791);
    return ports;
}

juce::File InterfaceControl::sessionCache() const
{
    if (cacheFile != juce::File())
        return cacheFile;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Audio Tech Pro").getChildFile ("interface-session.json");
}

bool InterfaceControl::tryConnect (int tryPort, juce::uint8 session, int timeoutMs)
{
    auto s = std::make_unique<juce::StreamingSocket>();
    if (! s->connect ("127.0.0.1", tryPort, std::min (timeoutMs, 1000)))
        return false;
    const std::array<juce::uint8, 4> cb { 0x6a, 0, session, 0 }, um { 0, 0, 0x69, 0 };
    const auto send = [&s] (const juce::MemoryBlock& b) { return s->write (b.getData(), (int) b.getSize()) == (int) b.getSize(); };
    juce::MemoryBlock udp;
    { juce::MemoryOutputStream o (udp, false); o.writeShort ((short) 47809); }
    auto* subscribe = new juce::DynamicObject();
    subscribe->setProperty ("id", "Subscribe");
    subscribe->setProperty ("clientName", "Audio Tech Pro");
    subscribe->setProperty ("clientInternalName", "ucapp");
    subscribe->setProperty ("clientType", "Mac");
    subscribe->setProperty ("clientDescription", "Audio Tech Pro");
    subscribe->setProperty ("clientIdentifier", "AudioTechPro");
    subscribe->setProperty ("clientOptions", "");
    subscribe->setProperty ("clientEncoding", 23117);
    if (! send (ucnet::encode ("UM", udp, um)) || ! send (ucnet::encode ("JM", ucnet::jsonPayload (juce::var (subscribe)), cb)))
        return false;

    // The right session answers with the device's state (Synchronize); a wrong one with junk or nothing
    ucnet::Decoder d;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    char buffer[8192];
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (s->waitUntilReady (true, 20) <= 0)
            continue;
        const auto got = s->read (buffer, sizeof (buffer), false);
        if (got <= 0)
            return false;
        const auto batch = d.feed (buffer, (size_t) got);
        for (size_t i = 0; i < batch.size(); ++i)
            if (const auto& m = batch[i]; m.code == "ZM" || m.code == "ZB")
            {
                const auto state = ucnet::parseState (m);
                if (state.getProperty ("id", {}).toString() != "Synchronize")
                    continue;
                std::map<juce::String, juce::var> v, r;
                ucnet::flatten (state, {}, v, r);
                if (v.find ("line/ch1/preampgain") == v.end())
                    continue;   // (a device without preamps we know)
                {
                    std::lock_guard<std::mutex> g (lock);
                    values = std::move (v);
                    ranges = std::move (r);
                }
                socket = std::move (s);
                decoder = std::move (d);
                deviceSession = cb;
                port = tryPort;
                // (what came with it: events already)
                for (size_t j = i + 1; j < batch.size(); ++j)
                    handle (batch[j]);
                return true;
            }
    }
    return false;
}

bool InterfaceControl::connect()
{
    // The last place it answered first (the session's byte changes when the service restarts, so then all of them)
    int cachedPort = port, cachedSession = deviceSession[2];
    if (auto json = juce::JSON::parse (sessionCache()); json.isObject())
    {
        cachedPort = (int) json.getProperty ("port", port);
        cachedSession = (int) json.getProperty ("session", cachedSession);
    }
    auto ports = findPorts != nullptr ? findPorts() : juce::Array<int> { port };
    ports.removeFirstMatchingValue (cachedPort);
    ports.insert (0, cachedPort);
    if (tryConnect (cachedPort, (juce::uint8) cachedSession, 600))
        return true;
    for (const auto p : ports)
    {
        // (a port that refuses: nothing there; skip its sessions)
        juce::StreamingSocket probe;
        if (! probe.connect ("127.0.0.1", p, 300))
            continue;
        probe.close();
        std::vector<int> sessions (256);
        std::iota (sessions.begin(), sessions.end(), 0);
        std::sort (sessions.begin(), sessions.end(), [cachedSession] (int a, int b) { return std::abs (a - cachedSession) < std::abs (b - cachedSession); });
        for (const auto session : sessions)
        {
            if (threadShouldExit())
                return false;
            if (tryConnect (p, (juce::uint8) session, 250))
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("port", p);
                o->setProperty ("session", session);
                sessionCache().getParentDirectory().createDirectory();
                sessionCache().replaceWithText (juce::JSON::toString (juce::var (o)));
                return true;
            }
        }
    }
    return false;
}

void InterfaceControl::run()
{
    char buffer[8192];
    while (! threadShouldExit())
    {
        if (socket == nullptr)
        {
            if (connect())
            {
                connected.store (true);
                changed();
            }
            else
            {
                // Not there (the control app isn't running): look again in a while
                for (int i = 0; i < 100 && ! threadShouldExit(); ++i)
                    juce::Thread::sleep (100);
                continue;
            }
        }

        // Writes waiting, a keepalive each second, events as they come
        std::vector<std::pair<juce::String, float>> writes;
        {
            std::lock_guard<std::mutex> g (lock);
            writes.swap (pending);
        }
        bool ok = true;
        for (const auto& [path, value] : writes)
        {
            const auto frame = ucnet::encode ("PV", ucnet::pvPayload (path, value), deviceSession);
            ok = ok && socket->write (frame.getData(), (int) frame.getSize()) == (int) frame.getSize();
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastKeepAlive > 1000)
        {
            const auto frame = ucnet::encode ("KA", {}, deviceSession);
            ok = ok && socket->write (frame.getData(), (int) frame.getSize()) == (int) frame.getSize();
            lastKeepAlive = now;
        }
        if (ok && socket->waitUntilReady (true, 30) > 0)
        {
            const auto got = socket->read (buffer, sizeof (buffer), false);
            if (got <= 0)
                ok = false;
            else
                for (const auto& m : decoder.feed (buffer, (size_t) got))
                    handle (m);
        }
        if (! ok)
        {
            socket = nullptr;
            connected.store (false);
            changed();
        }
    }
    if (socket != nullptr)
    {
        auto* bye = new juce::DynamicObject();
        bye->setProperty ("id", "Unsubscribe");
        const auto frame = ucnet::encode ("JM", ucnet::jsonPayload (juce::var (bye)), deviceSession);
        socket->write (frame.getData(), (int) frame.getSize());
        socket = nullptr;
    }
    connected.store (false);
}

void InterfaceControl::handle (const ucnet::Message& m)
{
    if (m.code == "PV" || m.code == "PL")
    {
        juce::String path;
        float v = 0.0f;
        if (m.code == "PV" ? ucnet::parsePv (m, path, v) : false)
        {
            {
                std::lock_guard<std::mutex> g (lock);
                values[path] = v;
            }
            if (path.startsWith ("line/"))
                changed();
        }
    }
    else if (m.code == "ZM" || m.code == "ZB")
    {
        const auto state = ucnet::parseState (m);
        std::map<juce::String, juce::var> v, r;
        ucnet::flatten (state, {}, v, r);
        if (! v.empty())
        {
            std::lock_guard<std::mutex> g (lock);
            for (auto& [k, x] : v) values[k] = x;
            for (auto& [k, x] : r) ranges[k] = x;
        }
        changed();
    }
}

void InterfaceControl::preview (const juce::var& tree)
{
    std::map<juce::String, juce::var> v, r;
    ucnet::flatten (tree, {}, v, r);
    {
        std::lock_guard<std::mutex> g (lock);
        values = std::move (v);
        ranges = std::move (r);
    }
    connected.store (true);
    changed();
}

void InterfaceControl::changed()
{
    juce::MessageManager::callAsync ([this, weak = std::weak_ptr<bool> (alive)]
    {
        if (! weak.expired() && onChange)
            onChange();
    });
}

float InterfaceControl::valueOf (const juce::String& path, float fallback) const
{
    std::lock_guard<std::mutex> g (lock);
    const auto it = values.find (path);
    return it != values.end() && ! it->second.isString() ? (float) (double) it->second : fallback;
}

InterfaceControl::Preamp InterfaceControl::preamp (int channel) const
{
    Preamp p;
    const auto base = "line/ch" + juce::String (channel) + "/";
    std::lock_guard<std::mutex> g (lock);
    const auto value = [&] (const char* what, float fallback) -> float
    {
        const auto it = values.find (base + what);
        return it != values.end() && ! it->second.isString() ? (float) (double) it->second : fallback;
    };
    const auto has = [&] (const char* what) { return values.find (base + what) != values.end(); };
    if (! has ("preampgain") || value ("preampactive", 1.0f) < 0.5f)
        return p;
    p.present = true;
    if (const auto r = ranges.find (base + "preampgain"); r != ranges.end())
    {
        p.minDb = (float) (double) r->second.getProperty ("min", 0.0);
        p.maxDb = (float) (double) r->second.getProperty ("max", 75.0);
    }
    p.gainDb = p.minDb + value ("preampgain", 0.0f) * (p.maxDb - p.minDb);
    p.phantom = value ("48v", 0.0f) > 0.5f;
    p.hasHighPass = has ("hpf");
    p.highPass = value ("hpf", 0.0f) > 0.5f;
    p.hasPad = has ("pad");
    p.pad = value ("pad", 0.0f) > 0.5f;
    p.hasAutoGain = has ("autogain");
    p.autoGain = value ("autogain", 0.0f) > 0.5f;
    p.clip = value ("clip", 0.0f) > 0.5f;
    return p;
}

int InterfaceControl::preampCount() const
{
    int count = 0;
    for (int ch = 1; ch <= 64; ++ch)
        if (preamp (ch).present)
            count = ch;
    return count;
}

void InterfaceControl::queueWrite (const juce::String& path, float normalized)
{
    std::lock_guard<std::mutex> g (lock);
    values[path] = normalized;   // (shown at once; the device's echo confirms it)
    for (auto& w : pending)
        if (w.first == path) { w.second = normalized; return; }
    pending.emplace_back (path, normalized);
}

void InterfaceControl::setGain (int channel, float db)
{
    const auto p = preamp (channel);
    if (! p.present)
        return;
    const auto normalized = juce::jlimit (0.0f, 1.0f, (db - p.minDb) / std::max (1.0e-3f, p.maxDb - p.minDb));
    queueWrite ("line/ch" + juce::String (channel) + "/preampgain", normalized);
    changed();
}

void InterfaceControl::setToggle (int channel, const char* what, bool on)
{
    queueWrite ("line/ch" + juce::String (channel) + "/" + what, on ? 1.0f : 0.0f);
    changed();
}

} // namespace riff
