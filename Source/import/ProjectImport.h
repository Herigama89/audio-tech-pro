#pragma once

#include "Interchange.h"

/**
    File > Open (and Import Project...) for other programs' projects:

      Ableton Live (.als)          tracks, clips, notes, warping, mix, sends, groups, plugins with their settings
      DAWproject (.dawproject)     Cubase 14+, Studio One 6.5+, Bitwig: the same, exactly
      FL Studio (.flp)             channels, patterns and their notes, the playlist, audio clips, tempo, markers
      Studio One (.song)           tracks, audio and note events, tempo, markers, mix
      Pro Tools (.ptx .ptf .pts)   audio tracks and regions as edited, MIDI notes
      Logic Pro (.logicx)          the project's audio files, tempo, key and signature (its arrangement can't be read)
      Cubase / Nuendo (.cpr .npr)  the audio files it uses (the rest can't be read; export DAWproject for all of it)
      MIDI files (.mid)            tracks, notes, tempo, time signatures, markers
*/
namespace riff::projectimport
{
    /** For file choosers: every format we open. */
    juce::String wildcard();

    /** Whether we can open this (a file, or a .logicx package). */
    bool canImport (const juce::File& file);

    /** The program a file comes from ("Ableton Live"...), or empty. */
    juce::String formatName (const juce::File& file);

    /** Reads a project into the neutral description. False (with `error`) if it can't. */
    bool read (const juce::File& file, interchange::Project& out, juce::String& error);

    /** The installed plugin to use for an imported one (none: it isn't installed). */
    using PluginMatcher = std::function<std::optional<juce::PluginDescription> (const interchange::Plugin&)>;

    /** Matches by format and id, then by name (any format: VST3, then AU, then CLAP). */
    std::optional<juce::PluginDescription> matchPlugin (const juce::KnownPluginList& known, const interchange::Plugin& plugin);

    /** Makes our project. What didn't come across is added to `imported.notes`. */
    juce::ValueTree build (interchange::Project& imported, const PluginMatcher& matcher);

    /** read() then build(). */
    juce::ValueTree open (const juce::File& file, const PluginMatcher& matcher, juce::StringArray& notes, juce::String& error);
}

/** The readers, one per format (Readers*.cpp). */
namespace riff::interchange
{
    bool readAbleton (const juce::File&, Project&, juce::String& error);
    bool readDawProject (const juce::File&, Project&, juce::String& error);
    bool readFlStudio (const juce::File&, Project&, juce::String& error);
    bool readStudioOne (const juce::File&, Project&, juce::String& error);
    bool readProTools (const juce::File&, Project&, juce::String& error);
    bool readLogic (const juce::File&, Project&, juce::String& error);
    bool readCubase (const juce::File&, Project&, juce::String& error);
    bool readMidiFile (const juce::File&, Project&, juce::String& error);
}
