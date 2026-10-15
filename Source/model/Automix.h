#pragma once

#include <JuceHeader.h>
#include <vector>

/**
    Automix: a balanced starting mix for a genre, from what each track actually sounds like.

    Each track gets a role (kick, snare, bass, lead vocal, keys...) from its name, or, when
    the name says nothing, from where its energy is (lows, mids, highs). Each genre says how
    loud each role sits in a typical mix of that style, relative to the loudest element
    (a hip-hop mix leans on the kick, 808 and vocal; a rock mix on drums, bass and guitars;
    jazz keeps everything closer together). The faders are then set so each track, measured
    while it plays, lands on its level: one undo step, a starting point to mix from.
*/
namespace riff::automix
{
    enum class Role { kick, snare, hats, drums, perc, bass, leadVocal, backingVocal, guitar, keys, synth, pad, strings, brass, fx, other };

    juce::String roleName (Role);
    juce::StringArray genres();

    /** What a track sounds like: its name, and its level and tone measured while it plays. */
    struct Track
    {
        juce::String id, name;
        bool instrument = false;      // an instrument (MIDI) track
        double activeRmsDb = -120.0;  // measured (after its fader), while it plays
        double faderDb = 0.0;         // its fader now
        double lowShare = 0.0, midShare = 0.0, highShare = 0.0;   // percent of its energy below 200 Hz, between, above 4 kHz
    };

    /** The role from the name ("Kick", "808", "Lead Vox"...), else from the tone. */
    Role guessRole (const Track&);

    /** How loud a role sits in the genre, in dB below the loudest element (0 = the loudest). */
    double targetDb (const juce::String& genre, Role);

    struct Result
    {
        juce::String id, name;
        Role role = Role::other;
        double fromDb = 0.0, toDb = 0.0;
    };

    /** New fader levels for the genre. Silent tracks are left alone. `headroomDb`: where the loudest element's
        own level lands (dBFS RMS while it plays), leaving room in the mix bus. */
    std::vector<Result> balance (const std::vector<Track>& tracks, const juce::String& genre, double headroomDb = -18.0);
}
