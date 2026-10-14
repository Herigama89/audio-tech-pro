#include "AraHost.h"
#include "EffectRack.h"
#include "../model/Project.h"

#if RIFF_ARA_ENABLED

namespace riff
{

namespace
{
    template <typename A, typename B>
    using Conv = juce::ARAHostModel::ConversionFunctions<A, B>;

    //==========================================================================
    /** One audio file the plugin can read (an ARA audio source), with its one modification on this track. */
    struct Source
    {
        juce::File file;
        juce::String persistentId, modificationId;
        juce::int64 length = 0;
        double rate = 48000.0;
        int channels = 2;
        std::unique_ptr<juce::ARAHostModel::AudioSource> source;
        std::unique_ptr<juce::ARAHostModel::AudioModification> modification;
    };

    /** One of the track's regions, as an ARA playback region. */
    struct Region
    {
        juce::String regionId, file;
        double start = 0.0, offset = 0.0, length = 0.0;
        std::unique_ptr<juce::ARAHostModel::PlaybackRegion> region;
    };

    //==========================================================================
    /** The plugin reads audio through this (from its own threads): each reader has its own file reader. */
    struct AudioAccess final : ARA::Host::AudioAccessControllerInterface
    {
        explicit AudioAccess (juce::AudioFormatManager& f) : formats (f) {}

        struct Reader
        {
            std::unique_ptr<juce::AudioFormatReader> reader;
            int channels = 2;
        };

        ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef sourceRef, bool use64BitSamples) noexcept override
        {
            if (use64BitSamples)
                return nullptr;   // (we said 32-bit samples suit us best)
            auto* src = Conv<Source*, ARA::ARAAudioSourceHostRef>::fromHostRef (sourceRef);
            auto reader = std::make_unique<Reader>();
            reader->reader.reset (formats.createReaderFor (src->file));
            reader->channels = src->channels;
            if (reader->reader == nullptr)
                return nullptr;
            auto* raw = reader.get();
            const std::lock_guard<std::mutex> lock (mutex);
            readers.emplace (raw, std::move (reader));
            return Conv<Reader*, ARA::ARAAudioReaderHostRef>::toHostRef (raw);
        }

        bool readAudioSamples (ARA::ARAAudioReaderHostRef readerRef, ARA::ARASamplePosition position,
                               ARA::ARASampleCount count, void* const* buffers) noexcept override
        {
            auto* r = Conv<Reader*, ARA::ARAAudioReaderHostRef>::fromHostRef (readerRef);
            if (r == nullptr || r->reader == nullptr || count > std::numeric_limits<int>::max())
                return false;
            auto* const* out = reinterpret_cast<float* const*> (buffers);
            // (a mono file read into stereo buffers would differ: read exactly its channels)
            return r->reader->read (out, r->channels, position, (int) count);
        }

        void destroyAudioReader (ARA::ARAAudioReaderHostRef readerRef) noexcept override
        {
            const std::lock_guard<std::mutex> lock (mutex);
            readers.erase (Conv<Reader*, ARA::ARAAudioReaderHostRef>::fromHostRef (readerRef));
        }

        juce::AudioFormatManager& formats;
        std::mutex mutex;
        std::map<Reader*, std::unique_ptr<Reader>> readers;
    };

    /** The plugin's edits go into a memory block (then base64 in the project), and come back from one. */
    struct Archiving final : ARA::Host::ArchivingControllerInterface
    {
        using ReaderConv = Conv<juce::MemoryBlock*, ARA::ARAArchiveReaderHostRef>;
        using WriterConv = Conv<juce::MemoryOutputStream*, ARA::ARAArchiveWriterHostRef>;

        ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef ref) noexcept override
        {
            return (ARA::ARASize) ReaderConv::fromHostRef (ref)->getSize();
        }
        bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef ref, ARA::ARASize position, ARA::ARASize length, ARA::ARAByte* buffer) noexcept override
        {
            auto* block = ReaderConv::fromHostRef (ref);
            if (position + length > block->getSize())
                return false;
            std::memcpy (buffer, juce::addBytesToPointer (block->getData(), position), length);
            return true;
        }
        bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef ref, ARA::ARASize position, ARA::ARASize length, const ARA::ARAByte* buffer) noexcept override
        {
            auto* stream = WriterConv::fromHostRef (ref);
            return stream->setPosition ((juce::int64) position) && stream->write (buffer, length);
        }
        void notifyDocumentArchivingProgress (float) noexcept override {}
        void notifyDocumentUnarchivingProgress (float) noexcept override {}
        ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef) noexcept override { return nullptr; }
    };

    /** The song's tempo and time signature, which the plugin asks for (Melodyne lines notes up to bars with it). */
    struct ContentAccess final : ARA::Host::ContentAccessControllerInterface
    {
        struct Tempo { double seconds, quarters; };
        std::mutex mutex;
        std::vector<Tempo> tempos { { 0.0, 0.0 }, { 0.5, 1.0 } };
        int numerator = 4;
        ARA::ARAContentTempoEntry tempoEntry {};
        ARA::ARAContentBarSignature barSignature {};

        using Kind = Conv<ARA::ARAContentType, ARA::ARAContentReaderHostRef>;

        void setSong (const juce::ValueTree& project)
        {
            const auto map = model::tempoMapOf (project);
            std::vector<Tempo> t;
            for (const auto& s : map.segments)
                t.push_back ({ s.startSeconds, s.startBeat });
            // (ARA wants at least two entries: one more, a beat after the last change)
            const auto& last = map.segments.back();
            t.push_back ({ last.startSeconds + last.secondsPerBeat, last.startBeat + 1.0 });
            const std::lock_guard<std::mutex> lock (mutex);
            tempos = std::move (t);
            numerator = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
        }

        bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type) noexcept override
        {
            return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
        }
        ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef, ARA::ARAContentType) noexcept override
        {
            return ARA::kARAContentGradeAdjusted;
        }
        ARA::ARAContentReaderHostRef createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type,
                                                                       const ARA::ARAContentTimeRange*) noexcept override
        {
            return Kind::toHostRef (type);
        }
        bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return false; }
        ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return 0; }
        ARA::ARAContentReaderHostRef createAudioSourceContentReader (ARA::ARAAudioSourceHostRef, ARA::ARAContentType,
                                                                     const ARA::ARAContentTimeRange*) noexcept override { return nullptr; }
        ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef ref) noexcept override
        {
            const std::lock_guard<std::mutex> lock (mutex);
            return Kind::fromHostRef (ref) == ARA::kARAContentTypeTempoEntries ? (ARA::ARAInt32) tempos.size() : 1;
        }
        const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef ref, ARA::ARAInt32 index) noexcept override
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (Kind::fromHostRef (ref) == ARA::kARAContentTypeTempoEntries)
            {
                const auto& t = tempos[(size_t) juce::jlimit (0, (int) tempos.size() - 1, (int) index)];
                tempoEntry.timePosition = t.seconds;
                tempoEntry.quarterPosition = t.quarters;
                return &tempoEntry;
            }
            barSignature.position = 0.0;
            barSignature.numerator = numerator;
            barSignature.denominator = 4;
            return &barSignature;
        }
        void destroyContentReader (ARA::ARAContentReaderHostRef) noexcept override {}
    };

    struct ModelUpdates final : ARA::Host::ModelUpdateControllerInterface
    {
        void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState, float) noexcept override {}
        void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
        void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
        void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}

    };

    /** Melodyne and others have their own play buttons: they ask the host. */
    struct Playback final : ARA::Host::PlaybackControllerInterface
    {
        std::function<void (bool)> start;
        std::function<void (double)> position;
        void requestStartPlayback() noexcept override { post ([this] { if (start) start (true); }); }
        void requestStopPlayback() noexcept override { post ([this] { if (start) start (false); }); }
        void requestSetPlaybackPosition (ARA::ARATimePosition t) noexcept override { post ([this, t] { if (position) position (t); }); }
        void requestSetCycleRange (ARA::ARATimePosition, ARA::ARATimeDuration) noexcept override {}
        void requestEnableCycle (bool) noexcept override {}
        void post (std::function<void()> f) { juce::MessageManager::callAsync ([alive = std::weak_ptr<bool> (this->alive), f] { if (! alive.expired()) f(); }); }
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}

//==============================================================================
struct AraDocument::Impl
{
    PluginEffect& effect;
    juce::AudioFormatManager& formats;
    std::unique_ptr<juce::ARAHostDocumentController> doc;
    ContentAccess* content = nullptr;
    Playback* playback = nullptr;
    juce::ARAHostModel::PlaybackRendererInterface playbackRenderer;
    juce::ARAHostModel::EditorRendererInterface editorRenderer;
    std::unique_ptr<juce::ARAHostModel::MusicalContext> musicalContext;
    std::unique_ptr<juce::ARAHostModel::RegionSequence> regionSequence;
    std::map<juce::String, std::unique_ptr<Source>> sources;    // by file path
    std::map<juce::String, std::unique_ptr<Region>> regions;    // by region id
    juce::String pendingArchive;
    juce::String trackName { "Track" };

    Impl (PluginEffect& e, juce::AudioFormatManager& f) : effect (e), formats (f) {}

    ARA::Host::DocumentController& dc() const { return doc->getDocumentController(); }

    Source* sourceFor (const juce::String& path, const juce::String& trackId)
    {
        if (auto it = sources.find (path); it != sources.end())
            return it->second.get();
        const juce::File file (path);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return nullptr;
        auto s = std::make_unique<Source>();
        s->file = file;
        s->persistentId = "file:" + path;
        s->modificationId = "mod:" + trackId + ":" + path;
        s->length = reader->lengthInSamples;
        s->rate = reader->sampleRate;
        s->channels = (int) juce::jlimit (1u, 2u, reader->numChannels);

        auto props = juce::ARAHostModel::AudioSource::getEmptyProperties();
        props.name = s->file.getFileName().toRawUTF8();
        props.persistentID = s->persistentId.toRawUTF8();
        props.sampleCount = s->length;
        props.sampleRate = s->rate;
        props.channelCount = s->channels;
        props.merits64BitSamples = false;
        s->source = std::make_unique<juce::ARAHostModel::AudioSource> (Conv<Source*, ARA::ARAAudioSourceHostRef>::toHostRef (s.get()), dc(), props);
        s->source->enableAudioSourceSamplesAccess (true);

        auto mod = juce::ARAHostModel::AudioModification::getEmptyProperties();
        mod.persistentID = s->modificationId.toRawUTF8();
        s->modification = std::make_unique<juce::ARAHostModel::AudioModification> (
            Conv<Source*, ARA::ARAAudioModificationHostRef>::toHostRef (s.get()), dc(), *s->source, mod);

        auto* raw = s.get();
        sources[path] = std::move (s);
        return raw;
    }

    ARA::ARAPlaybackRegionProperties regionProperties (const Region& r) const
    {
        auto p = juce::ARAHostModel::PlaybackRegion::getEmptyProperties();
        p.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
        p.startInModificationTime = r.offset;
        p.durationInModificationTime = r.length;
        p.startInPlaybackTime = r.start;
        p.durationInPlaybackTime = r.length;
        p.musicalContextRef = musicalContext->getPluginRef();
        p.regionSequenceRef = regionSequence->getPluginRef();
        p.name = nullptr;
        p.color = nullptr;
        return p;
    }
};

AraDocument::AraDocument (std::unique_ptr<Impl> i) : impl (std::move (i)) {}

AraDocument::~AraDocument()
{
    if (impl == nullptr || impl->doc == nullptr)
        return;
    if (impl->playback != nullptr)
        impl->playback->alive.reset();
    // Regions leave the plugin while it's stopped, then the model goes (regions, sources, sequence, context).
    impl->effect.setRendersRegions (false);
    impl->effect.withPluginStopped ([this]
    {
        impl->playbackRenderer = {};
        impl->editorRenderer = {};
        impl->regions.clear();
    });
    impl->sources.clear();
    impl->regionSequence.reset();
    impl->musicalContext.reset();
    impl->doc.reset();
}

void AraDocument::create (PluginEffect& effect, juce::AudioFormatManager& formats, const juce::String& name,
                          const juce::String& archive, std::function<void (std::unique_ptr<AraDocument>)> done)
{
    juce::createARAFactoryAsync (effect.getPlugin(), [&effect, &formats, name, archive, done] (juce::ARAFactoryWrapper factory)
    {
        if (factory.get() == nullptr)
        {
            done (nullptr);
            return;
        }
        auto impl = std::make_unique<Impl> (effect, formats);
        auto content = std::make_unique<ContentAccess>();
        auto playback = std::make_unique<Playback>();
        impl->content = content.get();
        impl->playback = playback.get();
        impl->doc = juce::ARAHostDocumentController::create (std::move (factory), name, std::make_unique<AudioAccess> (formats),
                                                             std::make_unique<Archiving>(), std::move (content),
                                                             std::make_unique<ModelUpdates>(), std::move (playback));
        if (impl->doc == nullptr)
        {
            done (nullptr);
            return;
        }

        const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
        auto instance = impl->doc->bindDocumentToPluginInstance (effect.getPlugin(), roles, roles);
        if (! instance.isValid())
        {
            done (nullptr);
            return;
        }
        impl->playbackRenderer = instance.getPlaybackRendererInterface();
        impl->editorRenderer = instance.getEditorRendererInterface();

        {
            const juce::ARAEditGuard guard (impl->dc());
            auto mc = juce::ARAHostModel::MusicalContext::getEmptyProperties();
            mc.name = "Song";
            mc.orderIndex = 0;
            mc.color = nullptr;
            impl->musicalContext = std::make_unique<juce::ARAHostModel::MusicalContext> (
                Conv<Impl*, ARA::ARAMusicalContextHostRef>::toHostRef (impl.get()), impl->dc(), mc);
            auto rs = juce::ARAHostModel::RegionSequence::getEmptyProperties();
            rs.name = "Track";
            rs.orderIndex = 0;
            rs.musicalContextRef = impl->musicalContext->getPluginRef();
            rs.color = nullptr;
            impl->regionSequence = std::make_unique<juce::ARAHostModel::RegionSequence> (
                Conv<Impl*, ARA::ARARegionSequenceHostRef>::toHostRef (impl.get()), impl->dc(), rs);
        }
        impl->pendingArchive = archive;
        effect.setRendersRegions (true);
        auto* playbackController = impl->playback;
        std::unique_ptr<AraDocument> document (new AraDocument (std::move (impl)));
        auto* raw = document.get();
        playbackController->start = [raw] (bool start) { if (raw->onRequestPlayback) raw->onRequestPlayback (start); };
        playbackController->position = [raw] (double t) { if (raw->onRequestPosition) raw->onRequestPosition (t); };
        done (std::move (document));
    });
}

void AraDocument::sync (const juce::ValueTree& project, const juce::ValueTree& track)
{
    auto& d = *impl;
    d.content->setSong (project);
    const auto trackId = track[ids::id].toString();

    // What the track has now: its audio regions (muted ones are left out, as the engine would).
    struct Wanted { juce::String id, file; double start, offset, length; };
    std::vector<Wanted> wanted;
    for (const auto& r : track)
        if (r.hasType (ids::REGION) && ! model::isMidiRegion (r) && ! model::isRegionMuted (r) && r[ids::file].toString().isNotEmpty())
            wanted.push_back ({ r[ids::id].toString(), r[ids::file].toString(), (double) r[ids::start], (double) r[ids::offset], (double) r[ids::length] });

    std::set<juce::String> keep;
    for (const auto& w : wanted)
        keep.insert (w.id);

    bool structural = false;
    for (const auto& [id, region] : d.regions)
        structural = structural || keep.count (id) == 0;
    for (const auto& w : wanted)
        if (auto it = d.regions.find (w.id); it == d.regions.end() || it->second->file != w.file)
            structural = true;

    const auto apply = [&]
    {
        const juce::ARAEditGuard guard (d.dc());

        // Saved edits come back first: the sources and modifications they belong to must exist.
        if (d.pendingArchive.isNotEmpty())
        {
            for (const auto& w : wanted)
                d.sourceFor (w.file, trackId);
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (d.pendingArchive))
                d.dc().restoreObjectsFromArchive (Archiving::ReaderConv::toHostRef (&block), nullptr);
            d.pendingArchive = {};
        }

        // Gone (or now from another file): out of the plugin.
        for (auto it = d.regions.begin(); it != d.regions.end();)
        {
            const auto w = std::find_if (wanted.begin(), wanted.end(), [&] (const Wanted& x) { return x.id == it->first; });
            if (w == wanted.end() || w->file != it->second->file)
                it = d.regions.erase (it);   // (it deregisters itself from both renderers)
            else
                ++it;
        }

        // New ones in; moved or trimmed ones updated.
        for (const auto& w : wanted)
        {
            auto it = d.regions.find (w.id);
            if (it == d.regions.end())
            {
                auto* source = d.sourceFor (w.file, trackId);
                if (source == nullptr)
                    continue;
                auto r = std::make_unique<Region>();
                r->regionId = w.id;
                r->file = w.file;
                r->start = w.start; r->offset = w.offset; r->length = w.length;
                r->region = std::make_unique<juce::ARAHostModel::PlaybackRegion> (Conv<Region*, ARA::ARAPlaybackRegionHostRef>::toHostRef (r.get()),
                                                                                 d.dc(), *source->modification, d.regionProperties (*r));
                d.playbackRenderer.add (*r->region);
                d.editorRenderer.add (*r->region);
                d.regions[w.id] = std::move (r);
            }
            else
            {
                auto& r = *it->second;
                if (! juce::exactlyEqual (r.start, w.start) || ! juce::exactlyEqual (r.offset, w.offset) || ! juce::exactlyEqual (r.length, w.length))
                {
                    r.start = w.start; r.offset = w.offset; r.length = w.length;
                    r.region->update (d.regionProperties (r));
                }
            }
        }
    };

    // Adding or removing regions needs the plugin stopped (a moment of silence on this track).
    if (structural || d.pendingArchive.isNotEmpty())
        d.effect.withPluginStopped (apply);
    else
        apply();
}

juce::String AraDocument::storeArchive()
{
    juce::MemoryOutputStream stream;
    if (! impl->dc().storeObjectsToArchive (Archiving::WriterConv::toHostRef (&stream), nullptr))
        return {};
    return stream.getMemoryBlock().toBase64Encoding();
}

int AraDocument::numRegions() const
{
    return (int) impl->regions.size();
}

} // namespace riff

#else

namespace riff
{
struct AraDocument::Impl {};
AraDocument::AraDocument (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
AraDocument::~AraDocument() = default;
void AraDocument::create (PluginEffect&, juce::AudioFormatManager&, const juce::String&, const juce::String&,
                          std::function<void (std::unique_ptr<AraDocument>)> done) { done (nullptr); }
void AraDocument::sync (const juce::ValueTree&, const juce::ValueTree&) {}
juce::String AraDocument::storeArchive() { return {}; }
int AraDocument::numRegions() const { return 0; }
} // namespace riff

#endif
