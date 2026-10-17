#include "NoteTools.h"
#include "Project.h"
#include <map>

namespace riff::notetools
{

namespace
{
    double startOf (const juce::ValueTree& n)  { return n[ids::start]; }
    double lengthOf (const juce::ValueTree& n) { return n[ids::length]; }
    int pitchOf (const juce::ValueTree& n)     { return n[ids::pitch]; }
    int velocityOf (const juce::ValueTree& n)  { return n[ids::velocity]; }

    juce::Array<juce::ValueTree> sortedByTime (juce::Array<juce::ValueTree> notes)
    {
        std::sort (notes.begin(), notes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            return startOf (a) != startOf (b) ? startOf (a) < startOf (b) : pitchOf (a) < pitchOf (b);
        });
        return notes;
    }

    /** Chords: notes starting together (within a few ms). */
    std::vector<juce::Array<juce::ValueTree>> chords (const juce::Array<juce::ValueTree>& notes)
    {
        std::vector<juce::Array<juce::ValueTree>> result;
        for (const auto& n : sortedByTime (notes))
        {
            if (result.empty() || std::abs (startOf (result.back().getFirst()) - startOf (n)) > 0.004)
                result.push_back ({});
            result.back().add (n);
        }
        return result;
    }

    juce::ValueTree copyNote (juce::ValueTree region, const juce::ValueTree& n, double start, double length, int pitch, int velocity, juce::UndoManager* um)
    {
        auto c = model::addNote (region, pitch, start, juce::jmax (0.005, length), velocity, um);
        if (isMuted (n))
            c.setProperty (ids::mute, true, um);
        return c;
    }
}

void chop (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double step, juce::UndoManager* um)
{
    if (step <= 0.0)
        return;
    for (auto n : notes)
    {
        const auto start = startOf (n), length = lengthOf (n);
        if (length <= step * 1.01)
            continue;
        const auto pitch = pitchOf (n), velocity = velocityOf (n);
        model::resizeNote (n, step, um);
        for (auto t = start + step; t < start + length - 1.0e-6; t += step)
            copyNote (region, n, t, juce::jmin (step, start + length - t), pitch, velocity, um);
    }
}

void glue (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, juce::UndoManager* um)
{
    std::map<int, juce::Array<juce::ValueTree>> byPitch;
    for (const auto& n : sortedByTime (notes))
        byPitch[pitchOf (n)].add (n);
    for (auto& [pitch, row] : byPitch)
    {
        juce::ignoreUnused (pitch);
        for (int i = 0; i + 1 < row.size();)
        {
            auto a = row[i];
            const auto b = row[i + 1];
            if (startOf (b) <= startOf (a) + lengthOf (a) + 0.002)
            {
                model::resizeNote (a, juce::jmax (lengthOf (a), startOf (b) + lengthOf (b) - startOf (a)), um);
                region.removeChild (b, um);
                row.remove (i + 1);
            }
            else
                ++i;
        }
    }
}

void slice (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double at, juce::UndoManager* um)
{
    for (auto n : notes)
    {
        const auto start = startOf (n), end = start + lengthOf (n);
        if (at <= start + 0.002 || at >= end - 0.002)
            continue;
        model::resizeNote (n, at - start, um);
        copyNote (region, n, at, end - at, pitchOf (n), velocityOf (n), um);
    }
}

void strum (const juce::Array<juce::ValueTree>& notes, double spread, bool downwards, juce::UndoManager* um)
{
    for (auto chord : chords (notes))
    {
        if (chord.size() < 2)
            continue;
        std::sort (chord.begin(), chord.end(), [downwards] (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            return downwards ? pitchOf (a) > pitchOf (b) : pitchOf (a) < pitchOf (b);
        });
        const auto start = startOf (chord.getFirst());
        for (int i = 1; i < chord.size(); ++i)
        {
            auto n = chord[i];
            const auto end = startOf (n) + lengthOf (n);
            const auto newStart = start + spread * i;
            n.setProperty (ids::start, newStart, um);
            n.setProperty (ids::length, juce::jmax (0.01, end - newStart), um);
        }
    }
}

void flam (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double before, juce::UndoManager* um)
{
    for (const auto& n : notes)
    {
        const auto at = juce::jmax (0.0, startOf (n) - before);
        if (at < startOf (n))
            copyNote (region, n, at, juce::jmin (before, lengthOf (n)), pitchOf (n), juce::jmax (1, velocityOf (n) * 6 / 10), um);
    }
}

void arpeggiate (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double step, int pattern, juce::UndoManager* um)
{
    if (step <= 0.0)
        return;
    for (auto chord : chords (notes))
    {
        std::sort (chord.begin(), chord.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b) { return pitchOf (a) < pitchOf (b); });
        std::vector<int> order;
        for (int i = 0; i < chord.size(); ++i)
            order.push_back (i);
        if (pattern == 1)
            std::reverse (order.begin(), order.end());
        else if (pattern == 2)
            for (int i = chord.size() - 2; i > 0; --i)
                order.push_back (i);

        const auto start = startOf (chord.getFirst());
        double end = start;
        for (const auto& n : chord)
            end = juce::jmax (end, startOf (n) + lengthOf (n));
        std::vector<std::tuple<int, int>> picked;   // (pitch, velocity)
        for (const auto& n : chord)
            picked.emplace_back (pitchOf (n), velocityOf (n));
        const auto first = chord.getFirst();
        for (auto n : chord)
            region.removeChild (n, um);
        int k = 0;
        for (auto t = start; t < end - 1.0e-6; t += step, ++k)
        {
            const auto [pitch, velocity] = picked[(size_t) order[(size_t) k % order.size()]];
            copyNote (region, first, t, juce::jmin (step, end - t) * 0.95, pitch, velocity, um);
        }
    }
}

void randomize (const juce::Array<juce::ValueTree>& notes, double timing, int velocity, int seed, juce::UndoManager* um)
{
    juce::Random random (seed);
    for (auto n : notes)
    {
        if (timing > 0.0)
            n.setProperty (ids::start, juce::jmax (0.0, startOf (n) + (random.nextDouble() * 2.0 - 1.0) * timing), um);
        if (velocity > 0)
            model::setNoteVelocity (n, juce::jlimit (1, 127, velocityOf (n) + random.nextInt ({ -velocity, velocity + 1 })), um);
    }
}

void scaleVelocity (const juce::Array<juce::ValueTree>& notes, double percent, int offset, juce::UndoManager* um)
{
    if (notes.isEmpty())
        return;
    double mean = 0.0;
    for (const auto& n : notes)
        mean += velocityOf (n);
    mean /= notes.size();
    for (auto n : notes)
        model::setNoteVelocity (n, juce::jlimit (1, 127, juce::roundToInt (mean + (velocityOf (n) - mean) * percent / 100.0) + offset), um);
}

void limit (const juce::Array<juce::ValueTree>& notes, int lowest, int highest, juce::UndoManager* um)
{
    if (highest - lowest < 11)
        return;   // (an octave at least, or some notes could never fit)
    for (auto n : notes)
    {
        auto p = pitchOf (n);
        while (p < lowest) p += 12;
        while (p > highest) p -= 12;
        if (p != pitchOf (n))
            n.setProperty (ids::pitch, p, um);
    }
}

void claw (const juce::Array<juce::ValueTree>& notes, int group, int keep, juce::UndoManager* um)
{
    if (group < 1 || keep >= group)
        return;
    const auto sorted = sortedByTime (notes);
    for (int i = 0; i < sorted.size(); ++i)
        if (i % group >= keep)
        {
            auto n = sorted[i];
            n.getParent().removeChild (n, um);
        }
}

void articulate (const juce::Array<juce::ValueTree>& notes, double percent, juce::UndoManager* um)
{
    const auto sorted = sortedByTime (notes);
    for (int i = 0; i < sorted.size(); ++i)
    {
        auto n = sorted[i];
        double next = -1.0;
        for (int j = i + 1; j < sorted.size(); ++j)
            if (startOf (sorted[j]) > startOf (n) + 0.002)
            {
                next = startOf (sorted[j]);
                break;
            }
        const auto gap = next > 0.0 ? next - startOf (n) : lengthOf (n);
        model::resizeNote (n, juce::jmax (0.01, gap * percent / 100.0), um);
    }
}

const std::vector<Stamp>& stamps()
{
    static const std::vector<Stamp> list {
        { "Major", { 0, 4, 7 } }, { "Minor", { 0, 3, 7 } }, { "Power (5th)", { 0, 7, 12 } },
        { "Sus2", { 0, 2, 7 } }, { "Sus4", { 0, 5, 7 } }, { "Diminished", { 0, 3, 6 } }, { "Augmented", { 0, 4, 8 } },
        { "Major 7", { 0, 4, 7, 11 } }, { "Minor 7", { 0, 3, 7, 10 } }, { "Dominant 7", { 0, 4, 7, 10 } },
        { "Minor 7b5", { 0, 3, 6, 10 } }, { "Major 9", { 0, 4, 7, 11, 14 } }, { "Minor 9", { 0, 3, 7, 10, 14 } },
        { "Add 9", { 0, 4, 7, 14 } }, { "6", { 0, 4, 7, 9 } }, { "Minor 6", { 0, 3, 7, 9 } }, { "Octave", { 0, 12 } } };
    return list;
}

void setMuted (const juce::Array<juce::ValueTree>& notes, bool muted, juce::UndoManager* um)
{
    for (auto n : notes)
        if (muted)
            n.setProperty (ids::mute, true, um);
        else
            n.removeProperty (ids::mute, um);
}

bool isMuted (const juce::ValueTree& note)
{
    return (bool) note.getProperty (ids::mute, false);
}

juce::Array<juce::ValueTree> duplicate (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double shift, juce::UndoManager* um)
{
    juce::Array<juce::ValueTree> copies;
    for (const auto& n : notes)
        copies.add (copyNote (region, n, startOf (n) + shift, lengthOf (n), pitchOf (n), velocityOf (n), um));
    return copies;
}

} // namespace riff::notetools
