#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Logic's Event List for one MIDI region: every note and controller as a
    row (position, type, number or pitch, value or velocity, length).
    Double-click a cell to type a new value; Delete removes the selected rows;
    Add puts a note at the playhead.
*/
class EventListPanel final : public juce::Component,
                             private juce::TableListBoxModel,
                             private juce::ValueTree::Listener,
                             private juce::AsyncUpdater
{
public:
    EventListPanel (juce::ValueTree region, juce::UndoManager& um, std::function<double()> playhead);
    ~EventListPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** "bar beat sixteenth tick" (ticks are 1/240 of a sixteenth) <-> seconds in the song. */
    static juce::String formatPosition (double seconds, double secondsPerBeat, int beatsPerBar);
    static std::optional<double> parsePosition (const juce::String& text, double secondsPerBeat, int beatsPerBar);

private:
    enum Column { position = 1, type, number, value, length };

    int getNumRows() override { return rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void deleteKeyPressed (int lastRowSelected) override;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    juce::String cellText (int row, int column) const;
    void setCell (int row, int column, const juce::String& text);
    double songTime (const juce::ValueTree& event) const;
    double secondsPerBeat() const;
    int beatsPerBar() const;

    juce::ValueTree region;
    juce::UndoManager& undoManager;
    std::function<double()> playheadSeconds;
    juce::Array<juce::ValueTree> rows;   // notes and controllers, in time order
    juce::TableListBox table { {}, this };
    juce::TextButton addButton { "Add Note" }, deleteButton { "Delete" };
    juce::Label title;
    std::unique_ptr<juce::TextEditor> editor;
};

} // namespace riff
