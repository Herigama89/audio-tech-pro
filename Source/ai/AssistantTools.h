#pragma once

#include <JuceHeader.h>
#include "Assistant.h"
#include "../engine/ConsolePlugins.h"
#include "../engine/MixAnalysis.h"

namespace riff::ai
{
    /**
        What the assistant's tools need from the running app, beyond the
        project tree: the transport, commands, plugins and the mix
        measurement. Tests leave most of these empty.
    */
    struct AppHooks
    {
        std::function<double()> playheadSeconds;
        std::function<void (double)> setPlayhead;
        std::function<void (bool)> setPlaying;
        std::function<bool()> isPlaying;

        std::function<juce::StringArray()> commandNames;            // commands Claude may run
        std::function<bool (const juce::String&)> runCommand;        // by name; false if unknown or not possible now
        std::function<juce::String()> keyCommands;                   // "Play or Stop: Space" lines
        std::function<juce::String()> manual;                        // the help text

        std::function<void (const juce::ValueTree&)> selectTrack;

        // Plugins (console mode and inserts)
        std::function<juce::AudioProcessorParameter* (const juce::String& trackId, console::StripControl)> stripParameter;
        std::function<juce::AudioProcessorParameter* (console::Control)> busCompParameter;
        std::function<juce::AudioPluginInstance* (const juce::String& insertId)> pluginInstance;

        /** Measures the mix offline over [start, end) seconds; calls back on the message thread.
            Results: per track id (post-fader), and the master. */
        struct Measurement
        {
            std::map<juce::String, LevelStats> tracks;
            LevelStats master;
            double seconds = 0.0;
            juce::String error;
        };
        std::function<void (double, double, std::function<void (Measurement)>)> measureMix;
    };

    class AssistantTools final : public ToolHost
    {
    public:
        AssistantTools (juce::ValueTree& project, juce::UndoManager& undoManager, AppHooks hooks);

        juce::var toolDefinitions() override;
        void runTool (const juce::String& name, const juce::var& input, std::function<void (ToolResult)> done) override;
        juce::String staticContext() override;
        juce::String projectState() override;
        void beginTurn (const juce::String& request) override;
        void endTurn() override;

        /** Runs a tool that finishes at once (everything but analyze_mix); for tests. */
        ToolResult run (const juce::String& name, const juce::var& input);

        /** The whole project as JSON, as get_project returns it. */
        juce::var describeProject();

        double barToSeconds (double bar) const;
        double secondsToBar (double seconds) const;

    private:
        juce::ValueTree findTrack (const juce::var& reference, juce::String& error) const;
        juce::String trackLabel (const juce::ValueTree& track) const;
        juce::String instrumentName (const juce::ValueTree& track) const;
        juce::var describeTrack (const juce::ValueTree& track, int number);
        juce::var describeChain (const juce::String& owner);

        ToolResult setTempo (const juce::var& input);
        ToolResult addTracks (const juce::var& input);
        ToolResult updateTrack (const juce::var& input);
        ToolResult deleteTracks (const juce::var& input);
        ToolResult writeMidi (const juce::var& input);
        ToolResult getMidi (const juce::var& input);
        ToolResult deleteRegions (const juce::var& input);
        ToolResult generatePattern (const juce::var& input);
        ToolResult setConsole (const juce::var& input);
        ToolResult setBusCompressor (const juce::var& input);
        ToolResult setMasterVolume (const juce::var& input);
        ToolResult addEffect (const juce::var& input);
        ToolResult setEffect (const juce::var& input);
        ToolResult removeEffect (const juce::var& input);
        ToolResult addSend (const juce::var& input);
        ToolResult transport (const juce::var& input);
        ToolResult setCycle (const juce::var& input);
        ToolResult setMetronome (const juce::var& input);
        ToolResult runCommand (const juce::var& input);
        void analyzeMix (const juce::var& input, std::function<void (ToolResult)> done);

        juce::ValueTree& project;
        juce::UndoManager& um;
        AppHooks hooks;
    };
}
