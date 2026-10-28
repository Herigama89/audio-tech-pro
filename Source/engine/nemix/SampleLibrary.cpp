#include "SampleLibrary.h"

namespace riff::nemix::samples
{
namespace
{
    constexpr int maxInstruments = 256;

    struct Store
    {
        std::array<std::unique_ptr<Instrument>, maxInstruments> owned;
        std::array<std::atomic<const Instrument*>, maxInstruments> published {};
        juce::CriticalSection lock;
    };
    Store& store() { static Store s; return s; }

    void load (Instrument& ins, const juce::File& dir)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::map<juce::String, int> recordingIndex;
        std::vector<juce::String> files;
        float levelDb = 0.0f;
        for (const auto& line : juce::StringArray::fromLines (dir.getChildFile ("map.txt").loadFileAsString()))
        {
            if (line.startsWith ("#"))
            {
                const auto key = line.substring (1).upToFirstOccurrenceOf ("=", false, false);
                const auto value = line.fromFirstOccurrenceOf ("=", false, false);
                if (key == "credit") ins.credit = value;
                else if (key == "gain") levelDb = value.getFloatValue();   // (each instrument at a like level)
                else if (key == "oneShot") ins.oneShot = value.getIntValue() != 0;
                continue;
            }
            const auto f = juce::StringArray::fromTokens (line, "|", "");
            if (f.size() < 14) continue;
            Region r;
            if (recordingIndex.count (f[0]) == 0)
            {
                recordingIndex[f[0]] = (int) files.size();
                files.push_back (f[0]);
            }
            r.recording = recordingIndex[f[0]];
            r.lokey = f[1].getIntValue(); r.hikey = f[2].getIntValue(); r.root = f[3].getIntValue();
            r.lovel = f[4].getIntValue(); r.hivel = f[5].getIntValue();
            r.tuneCents = f[6].getFloatValue(); r.gainDb = f[7].getFloatValue();
            r.seq = std::max (1, f[8].getIntValue()); r.seqLength = std::max (1, f[9].getIntValue());
            r.release = f[10].getFloatValue();
            r.group = f[11].getIntValue(); r.offBy = f[12].getIntValue();
            r.pan = f[13].getFloatValue();
            ins.regions.push_back (r);
        }
        for (auto& r : ins.regions)
            r.gainDb += levelDb;
        // A melodic instrument plays across the whole keyboard (its lowest and highest recordings stretched)
        if (! ins.oneShot && ! ins.regions.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& r : ins.regions) { lo = std::min (lo, r.lokey); hi = std::max (hi, r.hikey); }
            for (auto& r : ins.regions)
            {
                if (r.lokey == lo) r.lokey = 0;
                if (r.hikey == hi) r.hikey = 127;
            }
        }
        ins.recordings.resize (files.size());
        for (size_t i = 0; i < files.size(); ++i)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (dir.getChildFile (files[i])));
            if (reader == nullptr) continue;
            auto& rec = ins.recordings[i];
            rec.channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
            rec.length = (int) std::min (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
            juce::AudioBuffer<float> buffer (rec.channels, std::max (1, rec.length));
            reader->read (&buffer, 0, rec.length, 0, true, true);
            // (a recording not at 44.1 kHz is played at its own rate: the map's pitch accounts for 44.1 kHz)
            rec.data.resize ((size_t) rec.length * (size_t) rec.channels);
            for (int s = 0; s < rec.length; ++s)
                for (int c = 0; c < rec.channels; ++c)
                    rec.data[(size_t) s * (size_t) rec.channels + (size_t) c]
                        = (int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (buffer.getSample (c, s) * 32767.0f));
        }
        // Melodic instruments: every note's loudest recording to the same peak (raw recordings jump in level from
        // note to note); the softer layers keep their distance below it. Kits keep their own balance (and #gain).
        if (! ins.oneShot)
        {
            std::vector<float> peak (ins.recordings.size(), 0.0f);
            for (size_t i = 0; i < ins.recordings.size(); ++i)
                for (const auto v : ins.recordings[i].data)
                    peak[i] = std::max (peak[i], (float) std::abs ((int) v) / 32768.0f);
            std::map<int, float> loudest;
            for (const auto& r : ins.regions)
                if (r.recording < (int) peak.size())
                    loudest[r.root] = std::max (loudest[r.root], peak[(size_t) r.recording] * std::pow (10.0f, (r.gainDb - levelDb) * 0.05f));
            for (auto& r : ins.regions)
                if (loudest[r.root] > 1.0e-5f)
                    r.gainDb += -levelDb - 6.0f - 20.0f * std::log10 (loudest[r.root]);
        }
        ins.ready.store (true, std::memory_order_release);
    }
}

const Region* Instrument::pick (int key, int velocity, int turn) const noexcept
{
    const Region* fallback = nullptr;
    for (const auto& r : regions)
    {
        if (key < r.lokey || key > r.hikey || velocity < r.lovel || velocity > r.hivel)
            continue;
        if (r.seqLength <= 1 || ((turn % r.seqLength) + 1) == r.seq)
            return &r;
        if (fallback == nullptr) fallback = &r;
    }
    return fallback;
}

juce::File folder()
{
    static const juce::File found = []
    {
        juce::Array<juce::File> places;
        const auto env = juce::SystemStats::getEnvironmentVariable ("NEMIX_LIBRARY", {});
        if (env.isNotEmpty()) places.add (juce::File (env));
        const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
       #if JUCE_MAC
        places.add (app.getChildFile ("Contents/Resources/Nemix Library"));
       #endif
        places.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Nemix Library"));
        places.add (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Audio Tech Pro").getChildFile ("Nemix Library"));
        // (working on the app itself: the library in the source folder, a few folders up from the program)
        auto up = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 7 && up != up.getParentDirectory(); ++i, up = up.getParentDirectory())
            places.add (up.getChildFile ("Resources/NemixLibrary"));
        for (const auto& f : places)
            if (f.getChildFile ("instruments.txt").existsAsFile())
                return f;
        return juce::File();
    }();
    return found;
}

const std::vector<Entry>& catalogue()
{
    static const std::vector<Entry> list = []
    {
        std::vector<Entry> l;
        const auto dir = folder();
        if (dir == juce::File()) return l;
        for (const auto& line : juce::StringArray::fromLines (dir.getChildFile ("instruments.txt").loadFileAsString()))
        {
            const auto f = juce::StringArray::fromTokens (line, "|", "");
            if (f.size() >= 3 && ! line.startsWith ("#"))
                l.push_back ({ f[0], f[1], f[2] });
        }
        return l;
    }();
    return list;
}

int indexOf (const juce::String& id)
{
    const auto& c = catalogue();
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].id == id) return (int) i;
    return -1;
}

const Instrument* find (int index) noexcept
{
    if (index < 0 || index >= maxInstruments) return nullptr;
    return store().published[(size_t) index].load (std::memory_order_acquire);
}

const Instrument* request (int index)
{
    const auto& c = catalogue();
    if (index < 0 || index >= (int) c.size() || index >= maxInstruments)
        return nullptr;
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    if (s.owned[(size_t) index] == nullptr)
    {
        auto ins = std::make_unique<Instrument>();
        ins->id = c[(size_t) index].id;
        ins->name = c[(size_t) index].name;
        ins->category = c[(size_t) index].category;
        auto* raw = ins.get();
        s.owned[(size_t) index] = std::move (ins);
        s.published[(size_t) index].store (raw, std::memory_order_release);
        const auto dir = folder().getChildFile (raw->id);
        juce::Thread::launch ([raw, dir] { load (*raw, dir); });
    }
    return s.owned[(size_t) index].get();
}

bool waitUntilLoaded (int index, int timeoutMs)
{
    const auto* ins = request (index);
    if (ins == nullptr) return false;
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! ins->ready.load() && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (5);
    return ins->ready.load();
}

} // namespace riff::nemix::samples
