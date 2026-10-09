#include "EventList.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

EventListPanel::EventListPanel (juce::ValueTree r, juce::UndoManager& um, std::function<double()> playhead)
    : region (r), undoManager (um), playheadSeconds (std::move (playhead))
{
    auto& header = table.getHeader();
    header.addColumn ("Position", position, 120);
    header.addColumn ("Status", type, 110);
    header.addColumn ("Num / Pitch", number, 90);
    header.addColumn ("Val / Vel", value, 80);
    header.addColumn ("Length", length, 110);
    header.setStretchToFitActive (true);
    table.setMultipleSelectionEnabled (true);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setRowHeight (22);
    addAndMakeVisible (table);

    title.setText (region[ids::name].toString() + " - Event List", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);

    addButton.onClick = [this]
    {
        const double content = playheadSeconds() - (double) region[ids::start] + (double) region[ids::offset];
        undoManager.beginNewTransaction ("Add note");
        model::addNote (region, 60, juce::jmax ((double) region[ids::offset], content), secondsPerBeat(), 100, &undoManager);
    };
    deleteButton.onClick = [this] { deleteKeyPressed (table.getSelectedRow()); };
    addAndMakeVisible (addButton);
    addAndMakeVisible (deleteButton);

    region.addListener (this);
    handleAsyncUpdate();
    setSize (560, 460);
    setWantsKeyboardFocus (true);
}

EventListPanel::~EventListPanel()
{
    region.removeListener (this);
}

double EventListPanel::secondsPerBeat() const { return model::secondsPerBeat (region.getParent().getParent()); }
int EventListPanel::beatsPerBar() const { return juce::jmax (1, (int) region.getParent().getParent().getProperty (ids::timeSigNumerator, 4)); }

double EventListPanel::songTime (const juce::ValueTree& event) const
{
    return (double) region[ids::start] + (double) event[ids::start] - (double) region[ids::offset];
}

juce::String EventListPanel::formatPosition (double seconds, double spb, int bpb)
{
    const auto ticksTotal = (juce::int64) std::llround (seconds / spb * 4.0 * 240.0);   // 240 ticks a sixteenth
    const auto ticksPerBar = (juce::int64) bpb * 4 * 240;
    const auto bar = ticksTotal / ticksPerBar + 1;
    auto rest = ticksTotal % ticksPerBar;
    const auto beat = rest / (4 * 240) + 1;
    rest %= 4 * 240;
    const auto sixteenth = rest / 240 + 1;
    const auto tick = rest % 240 + 1;
    return juce::String (bar) + " " + juce::String (beat) + " " + juce::String (sixteenth) + " " + juce::String (tick);
}

std::optional<double> EventListPanel::parsePosition (const juce::String& text, double spb, int bpb)
{
    auto parts = juce::StringArray::fromTokens (text.trim(), " .:", {});
    parts.removeEmptyStrings();
    if (parts.isEmpty())
        return std::nullopt;

    const auto get = [&parts] (int i) { return i < parts.size() ? juce::jmax (1, parts[i].getIntValue()) : 1; };
    const auto ticks = (double) (get (0) - 1) * bpb * 960.0 + (get (1) - 1) * 960.0 + (get (2) - 1) * 240.0 + (get (3) - 1);
    return ticks / 960.0 * spb;
}

void EventListPanel::handleAsyncUpdate()
{
    rows.clear();
    for (const auto& e : region)
        if (e.hasType (ids::NOTE) || e.hasType (ids::CC))
            rows.add (e);
    std::stable_sort (rows.begin(), rows.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::start] < (double) b[ids::start]; });
    table.updateContent();
    repaint();
}

juce::String EventListPanel::cellText (int row, int column) const
{
    if (! juce::isPositiveAndBelow (row, rows.size()))
        return {};
    const auto e = rows[row];
    const bool note = e.hasType (ids::NOTE);

    switch (column)
    {
        case position: return formatPosition (songTime (e), secondsPerBeat(), beatsPerBar());
        case type:     return note ? juce::String ("Note") : model::controllerName (e[ids::controller]);
        case number:   return note ? juce::MidiMessage::getMidiNoteName (e[ids::pitch], true, true, 3)
                                   : ((int) e[ids::controller] == model::pitchBend ? juce::String ("-") : juce::String ((int) e[ids::controller]));
        case value:
            if (note)
                return juce::String ((int) e[ids::velocity]);
            if ((int) e[ids::controller] == model::pitchBend)
                return juce::String (juce::roundToInt ((double) e[ids::value] * 16383.0) - 8192);
            return juce::String (juce::roundToInt ((double) e[ids::value] * 127.0));
        case length:
        {
            if (! note)
                return {};
            // As bars, beats, sixteenths, ticks of length (0-based).
            const auto ticks = (juce::int64) std::llround ((double) e[ids::length] / secondsPerBeat() * 960.0);
            const auto perBar = (juce::int64) beatsPerBar() * 960;
            return juce::String (ticks / perBar) + " " + juce::String ((ticks % perBar) / 960) + " "
                   + juce::String ((ticks % 960) / 240) + " " + juce::String (ticks % 240);
        }
        default: return {};
    }
}

void EventListPanel::setCell (int row, int column, const juce::String& text)
{
    if (! juce::isPositiveAndBelow (row, rows.size()))
        return;
    auto e = rows[row];
    const bool note = e.hasType (ids::NOTE);
    undoManager.beginNewTransaction ("Event List edit");

    switch (column)
    {
        case position:
            if (auto t = parsePosition (text, secondsPerBeat(), beatsPerBar()))
                e.setProperty (ids::start, juce::jmax (0.0, *t - (double) region[ids::start] + (double) region[ids::offset]), &undoManager);
            break;
        case number:
            if (note)
            {
                // A note name ("C3", "F#4") or a number.
                int pitch = text.getIntValue();
                if (! text.containsOnly ("0123456789"))
                    for (int p = 0; p < 128; ++p)
                        if (juce::MidiMessage::getMidiNoteName (p, true, true, 3).equalsIgnoreCase (text.trim())
                            || juce::MidiMessage::getMidiNoteName (p, false, true, 3).equalsIgnoreCase (text.trim()))
                            pitch = p;
                e.setProperty (ids::pitch, juce::jlimit (0, 127, pitch), &undoManager);
            }
            else if ((int) e[ids::controller] != model::pitchBend)
                e.setProperty (ids::controller, juce::jlimit (0, 127, text.getIntValue()), &undoManager);
            break;
        case value:
            if (note)
                e.setProperty (ids::velocity, juce::jlimit (1, 127, text.getIntValue()), &undoManager);
            else if ((int) e[ids::controller] == model::pitchBend)
                e.setProperty (ids::value, juce::jlimit (0.0, 1.0, (text.getIntValue() + 8192) / 16383.0), &undoManager);
            else
                e.setProperty (ids::value, juce::jlimit (0.0, 1.0, text.getIntValue() / 127.0), &undoManager);
            break;
        case length:
            if (note)
            {
                auto parts = juce::StringArray::fromTokens (text.trim(), " .:", {});
                parts.removeEmptyStrings();
                const auto get = [&parts] (int i) { return i < parts.size() ? juce::jmax (0, parts[i].getIntValue()) : 0; };
                const auto ticks = (double) get (0) * beatsPerBar() * 960.0 + get (1) * 960.0 + get (2) * 240.0 + get (3);
                if (ticks > 0.0)
                    e.setProperty (ids::length, ticks / 960.0 * secondsPerBeat(), &undoManager);
            }
            break;
        default: break;
    }
}

void EventListPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::selection : (row % 2 == 0 ? theme::laneA : theme::laneB));
}

void EventListPanel::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool)
{
    g.setColour (column == type ? theme::dimText : theme::text);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (cellText (row, column), 6, 0, w - 10, h, juce::Justification::centredLeft, true);
}

void EventListPanel::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column == type)
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setText (cellText (row, column));
    editor->selectAll();
    editor->setColour (juce::TextEditor::backgroundColourId, theme::lcd);
    editor->setColour (juce::TextEditor::textColourId, theme::lcdText);
    const auto cell = table.getCellPosition (column, row, true);
    editor->setBounds (table.getBounds().getX() + cell.getX(), table.getBounds().getY() + cell.getY(), cell.getWidth(), cell.getHeight());
    const auto finish = [this, row, column] (bool apply)
    {
        if (editor == nullptr)
            return;
        const auto text = editor->getText();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EventListPanel> (this)] { if (safe != nullptr) safe->editor.reset(); });
        if (apply)
            setCell (row, column, text);
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void EventListPanel::deleteKeyPressed (int)
{
    juce::Array<juce::ValueTree> doomed;
    for (int i = 0; i < table.getNumSelectedRows(); ++i)
        if (const auto row = table.getSelectedRow (i); juce::isPositiveAndBelow (row, rows.size()))
            doomed.add (rows[row]);
    if (doomed.isEmpty())
        return;
    undoManager.beginNewTransaction ("Delete events");
    for (auto e : doomed)
        region.removeChild (e, &undoManager);
    table.deselectAllRows();
}

bool EventListPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteKeyPressed (table.getSelectedRow());
        return true;
    }
    return false;
}

void EventListPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void EventListPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (28);
    deleteButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    addButton.setBounds (top.removeFromRight (90));
    title.setBounds (top);
    r.removeFromTop (6);
    table.setBounds (r);
}

} // namespace riff
