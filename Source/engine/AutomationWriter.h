#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ProjectPlayer.h"

namespace riff
{

/**
    Writes fader and pan moves into automation while the song plays, by each
    track's automation mode (as in Logic):

      Off    the curves are ignored
      Read   the curves play; moving a control changes nothing that's written
      Touch  writes while you hold a control, then glides back to what was there
      Latch  writes from when you touch a control until playback stops, holding the last value
      Write  writes from the start of playback (and turns into Latch afterwards)

    While a pass is being written the engine plays the control's value
    directly; the points go into the project (one undo step) when the pass ends.
    Message thread only.
*/
class AutomationWriter
{
public:
    AutomationWriter (AudioEngine& engine, ProjectPlayer& player, juce::UndoManager& um);

    void setProject (juce::ValueTree newProject);

    /** The one the app uses (for controls that have no other way to reach it), or nullptr. */
    static AutomationWriter* current();
    void makeCurrent() { currentWriter = this; }
    ~AutomationWriter();

    static juce::String modeOf (const juce::ValueTree& track) { return track.getProperty (ids::automationMode, "read").toString(); }
    static bool writes (const juce::String& mode) { return mode == "touch" || mode == "latch" || mode == "write"; }

    /** A control was grabbed / moved / let go. move() returns true when the value is being
        written as automation; the caller then leaves the track's own (static) value alone. */
    bool begin (const juce::String& trackId, const juce::String& param, double value);
    bool move (const juce::String& trackId, const juce::String& param, double value);
    void end (const juce::String& trackId, const juce::String& param);

    /** Call when playback starts or stops. */
    void transportChanged (bool nowPlaying);

    /** Call often (e.g. 30 times a second): Latch and Write keep writing the held value. */
    void tick();

    /** The value being written right now, if this control has a pass going. */
    std::optional<double> liveValue (const juce::String& trackId, const juce::String& param) const;

    /** What a control should show at a time: the live value, else the curve (unless Off), else its own value. */
    double displayedValue (const juce::ValueTree& track, const juce::String& param, double time) const;

    /** Test hook: the time "now" (otherwise the engine's playhead). */
    std::function<double()> clock;

private:
    struct Pass
    {
        juce::String trackId, param;
        double start = 0.0, lastTime = 0.0, value = 0.0;
        bool touching = false;
        std::vector<std::pair<double, double>> points;
    };

    Pass* find (const juce::String& trackId, const juce::String& param);
    Pass& startPass (const juce::String& trackId, const juce::String& param, double value);
    void addPoint (Pass& pass, double time, bool force);
    void commit (const Pass& pass);
    void setOverride (const Pass& pass, bool on);
    double now() const;
    bool isPlaying() const;

    AudioEngine& engine;
    ProjectPlayer& player;
    juce::UndoManager& undoManager;
    juce::ValueTree project;
    std::vector<Pass> passes;
    bool playing = false;

    static inline AutomationWriter* currentWriter = nullptr;
};

} // namespace riff
