#pragma once

#include <JuceHeader.h>

/**
    Opening other programs' projects (Ableton Live, FL Studio, Studio One, Pro Tools,
    Logic, Cubase, DAWproject, MIDI files).

    Each reader turns its format into this neutral description first; one builder
    (ProjectImport) then makes our project from it: tempo, time signature,
    markers, tracks, regions, notes, mix settings, sends and plugins.
*/
namespace riff::interchange
{
    /** A position or length: in beats (quarter notes) or in seconds. */
    struct Time
    {
        double value = 0.0;
        bool beats = true;

        static Time inBeats (double v)   { return { v, true }; }
        static Time inSeconds (double v) { return { v, false }; }
    };

    struct Note
    {
        int pitch = 60;
        double start = 0.0, length = 0.25;   // beats, from the start of the region's content
        int velocity = 100;                  // 1..127
    };

    struct Plugin
    {
        juce::String name, vendor;
        juce::String format;                 // "VST3", "AudioUnit", "CLAP", "VST" (VST2), or empty
        juce::String uid;                    // VST3 class id (32 hex digits), AU "type subtype manufacturer", CLAP id
        bool instrument = false, bypassed = false;

        juce::MemoryBlock juceState;                       // ready for setStateInformation (an AU's preset plist, say)
        juce::MemoryBlock vst3Component, vst3Controller;   // a VST3's own two states (made into ours when it's loaded as VST3)
    };

    struct Region
    {
        juce::String name;
        bool midi = false;

        // Audio: the file (found on disk), or the name it had when it can't be found.
        juce::File file;
        juce::String missingFile;
        double offsetSeconds = 0.0;          // where in the file it starts playing
        double sourceBpm = 0.0;              // a warped / stretched clip's own tempo (0: plays as recorded)

        Time start, length;                  // on the timeline
        double offsetBeats = 0.0;            // MIDI: where in its notes it starts playing

        double gain = 1.0;                   // linear
        double fadeIn = 0.0, fadeOut = 0.0;  // seconds
        double transpose = 0.0;              // semitones
        bool muted = false;

        // Repeats its first `loopLength` (same unit as length) to fill its length.
        double loopLength = 0.0;

        std::vector<Note> notes;
    };

    struct Send
    {
        juce::String targetKey;              // the bus's `key`
        double level = 1.0;                  // linear
        bool pre = false;
    };

    struct Track
    {
        enum class Kind { audio, instrument, bus };

        juce::String key;                    // the reader's own id (for sends and groups)
        juce::String name;
        Kind kind = Kind::audio;
        juce::Colour colour;                 // transparent: our palette's
        double volume = 1.0;                 // linear
        double pan = 0.0;                    // -1..1
        bool mute = false, solo = false;
        bool mono = false;

        juce::String groupKey;               // inside this group (a summing stack), or empty
        bool isGroup = false;                // a group / folder: its members sum into it

        std::vector<Region> regions;
        std::vector<std::pair<int, Region>> cells;   // session clips: scene, clip (into our Live Loops grid)
        std::optional<Plugin> instrumentPlugin;
        juce::File samplerFile;              // no plugin: our sampler with this sound (a Simpler, an FL sampler channel)
        int samplerRoot = 60;
        bool drums = false;                  // no plugin: our drum kit (a drum rack)
        std::vector<Plugin> inserts;
        std::vector<Send> sends;
    };

    struct TempoPoint { double beat = 0.0, bpm = 120.0; };
    struct SignaturePoint { double beat = 0.0; int numerator = 4, denominator = 4; };
    struct Marker { Time time; juce::String name; };

    struct Project
    {
        juce::String name;
        juce::File folder;                   // where the project was (missing files are expected there)
        juce::String sourceFormat;           // "Ableton Live", "FL Studio"...
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        std::vector<TempoPoint> tempoChanges;          // after the first (beat > 0)
        std::vector<SignaturePoint> signatureChanges;  // after the first (beat > 0)
        std::vector<Marker> markers;
        std::vector<Track> tracks;
        double masterVolume = -1.0;          // linear; < 0: leave ours
        std::vector<Plugin> masterInserts;

        /** What didn't come across, or came across differently, in plain words (shown after opening). */
        juce::StringArray notes;

        void note (const juce::String& n) { notes.addIfNotAlreadyThere (n); }
    };

    //==============================================================================
    // Helpers the readers share

    /** A referenced media file, looked for where it was, then near the project (by relative path, then by name). */
    juce::File findMedia (const juce::String& storedPath, const juce::File& projectFolder,
                          const juce::StringArray& extraFolders = {});

    /** Seconds of audio in a file (0 if unreadable). */
    double audioFileLength (const juce::File& file);

    /** Bytes from hex text (whitespace ignored), as Ableton stores plugin states. */
    juce::MemoryBlock fromHex (const juce::String& hex);

    /** A .vstpreset's two VST3 states and its class id. */
    bool readVstPreset (const juce::MemoryBlock& data, juce::String& classId, juce::MemoryBlock& component, juce::MemoryBlock& controller);

    /** A VST3's two states as JUCE's own VST3 host stores them (for setStateInformation). */
    juce::MemoryBlock juceVst3State (const juce::MemoryBlock& component, const juce::MemoryBlock& controller);

    /** Decibels to linear, and back. */
    inline double dbToGain (double db) { return db <= -144.0 ? 0.0 : std::pow (10.0, db / 20.0); }
}
