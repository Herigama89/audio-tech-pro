#include "PitchCorrect.h"
#include "AudioEdit.h"

namespace riff::pitchcorrect
{

namespace
{
    std::vector<float> monoOf (const juce::AudioBuffer<float>& audio)
    {
        std::vector<float> m ((size_t) audio.getNumSamples(), 0.0f);
        const auto gain = 1.0f / (float) juce::jmax (1, audio.getNumChannels());
        for (int c = 0; c < audio.getNumChannels(); ++c)
            juce::FloatVectorOperations::addWithMultiply (m.data(), audio.getReadPointer (c), gain, audio.getNumSamples());
        return m;
    }

    double midiOf (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
    double hzOf (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0.0f;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    }
}

//==============================================================================
Analysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Analysis a;
    a.sampleRate = sampleRate;
    a.hop = juce::jmax (64, (int) std::lround (sampleRate * 0.0053));
    a.numSamples = audio.getNumSamples();
    const auto x = monoOf (audio);
    const int n = (int) x.size();
    const int frames = n / a.hop + 1;
    a.midi.assign ((size_t) frames, 0.0f);
    a.level.assign ((size_t) frames, 0.0f);

    // YIN on each frame, its difference function from an FFT correlation
    const int window = 1024;
    const int minLag = juce::jmax (2, (int) (sampleRate / 1100.0));
    const int maxLag = juce::jmin (window, (int) (sampleRate / 58.0));
    constexpr int order = 12, size = 1 << order;   // >= window + (window + maxLag)
    juce::dsp::FFT fft (order);
    std::vector<juce::dsp::Complex<float>> fa ((size_t) size), fb ((size_t) size), ta ((size_t) size), tb ((size_t) size);
    std::vector<double> d ((size_t) maxLag + 1), squares ((size_t) (window + maxLag + 1));

    for (int f = 0; f < frames; ++f)
    {
        const int start = f * a.hop - window / 2;
        const auto sample = [&] (int i) { const auto at = start + i; return at >= 0 && at < n ? x[(size_t) at] : 0.0f; };

        // Its loudness
        double rms = 0.0;
        for (int i = 0; i < window; ++i)
            rms += (double) sample (i) * sample (i);
        rms = std::sqrt (rms / window);
        a.level[(size_t) f] = (float) rms;
        if (rms < 0.002)
            continue;   // (silence: unpitched)

        std::fill (ta.begin(), ta.end(), juce::dsp::Complex<float> {});
        std::fill (tb.begin(), tb.end(), juce::dsp::Complex<float> {});
        for (int i = 0; i < window; ++i)
            ta[(size_t) i] = { sample (i), 0.0f };
        for (int i = 0; i < window + maxLag; ++i)
            tb[(size_t) i] = { sample (i), 0.0f };
        fft.perform (ta.data(), fa.data(), false);
        fft.perform (tb.data(), fb.data(), false);
        for (int i = 0; i < size; ++i)
            fa[(size_t) i] = std::conj (fa[(size_t) i]) * fb[(size_t) i];
        fft.perform (fa.data(), ta.data(), true);   // correlation(tau) = ta[tau].real (JUCE's inverse is scaled)

        squares[0] = 0.0;
        for (int i = 0; i < window + maxLag; ++i)
            squares[(size_t) i + 1] = squares[(size_t) i] + (double) sample (i) * sample (i);
        const auto e0 = squares[(size_t) window];
        double running = 0.0;
        int found = -1;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const auto e1 = squares[(size_t) (tau + window)] - squares[(size_t) tau];
            const auto diff = juce::jmax (0.0, e0 + e1 - 2.0 * (double) ta[(size_t) tau].real());
            running += diff;
            d[(size_t) tau] = running > 0.0 ? diff * tau / running : 1.0;   // the cumulative mean normalised difference
        }
        for (int tau = minLag; tau < maxLag; ++tau)
            if (d[(size_t) tau] < 0.15)
            {
                while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;
                found = tau;
                break;
            }
        if (found < 0)
        {
            int best = minLag;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (d[(size_t) tau] < d[(size_t) best])
                    best = tau;
            if (d[(size_t) best] < 0.3)
                found = best;
        }
        if (found < 0)
            continue;

        // Between samples: a parabola through the dip
        double lag = found;
        if (found > 1 && found < maxLag)
        {
            const auto y0 = d[(size_t) found - 1], y1 = d[(size_t) found], y2 = d[(size_t) found + 1];
            const auto denominator = y0 - 2.0 * y1 + y2;
            if (std::abs (denominator) > 1.0e-12)
                lag += 0.5 * (y0 - y2) / denominator;
        }
        a.midi[(size_t) f] = (float) midiOf (sampleRate / lag);
    }

    // Steady it: a median of five (unpitched frames left out), and lone pitched frames dropped
    {
        auto raw = a.midi;
        for (int f = 0; f < frames; ++f)
        {
            if (raw[(size_t) f] <= 0.0f)
                continue;
            std::vector<float> near;
            for (int k = juce::jmax (0, f - 2); k <= juce::jmin (frames - 1, f + 2); ++k)
                if (raw[(size_t) k] > 0.0f)
                    near.push_back (raw[(size_t) k]);
            a.midi[(size_t) f] = near.size() >= 3 ? median (near) : 0.0f;
        }
    }

    // Notes: runs of pitched frames, cut where the pitch moves to another note and stays there
    for (int f = 0; f < frames;)
    {
        if (a.midi[(size_t) f] <= 0.0f)
        {
            ++f;
            continue;
        }
        const int start = f;
        std::vector<float> values { a.midi[(size_t) f] };
        int lastPitched = f, away = 0, end = -1;
        for (++f; f < frames; ++f)
        {
            const auto m = a.midi[(size_t) f];
            if (m <= 0.0f)
            {
                if (f - lastPitched >= 5)
                    break;   // a gap: the note's over
                continue;
            }
            const auto recent = std::vector<float> (values.end() - (long) juce::jmin ((size_t) 12, values.size()), values.end());
            if (std::abs (m - median (recent)) > 0.75f)
            {
                if (++away >= 4)
                {
                    end = f - away + 1;   // the new note starts where it moved
                    break;
                }
                continue;
            }
            away = 0;
            values.push_back (m);
            lastPitched = f;
        }
        if (end < 0)
            end = lastPitched + 1;
        f = juce::jmax (end, start + 1);
        if (end - start < 5)
            continue;
        Note note;
        note.startFrame = start;
        note.endFrame = end;
        std::vector<float> pitches;
        double loudness = 0.0;
        for (int k = start; k < end; ++k)
        {
            if (a.midi[(size_t) k] > 0.0f)
                pitches.push_back (a.midi[(size_t) k]);
            loudness += a.level[(size_t) k];
        }
        note.centre = median (pitches);
        note.level = (float) (loudness / juce::jmax (1, end - start));
        a.notes.push_back (note);
    }
    return a;
}

//==============================================================================
namespace
{
/** The result for samples [spanFrom, spanTo) (the grains round them from a little before and after). */
juce::AudioBuffer<float> synthesise (const juce::AudioBuffer<float>& audio, const Analysis& a, const std::vector<NoteEdit>& edits,
                                     int spanFrom, int spanTo)
{
    const int n = audio.getNumSamples();
    const int frames = (int) a.midi.size();
    const auto rate = a.sampleRate;

    // What each frame gets: its note's shift (and straightening), its note's gain; smoothed over ~25 ms
    std::vector<double> shift ((size_t) frames, 0.0), gain ((size_t) frames, 0.0), formant ((size_t) frames, 0.0);
    for (size_t i = 0; i < a.notes.size() && i < edits.size(); ++i)
    {
        const auto& note = a.notes[i];
        const auto& e = edits[i];
        const auto curve = curveAfter (a, (int) i, e);
        const auto plain = centreAfter (note, e) - note.centre;
        for (int f = juce::jmax (0, note.startFrame); f < juce::jmin (frames, note.endFrame); ++f)
        {
            const auto m = (double) a.midi[(size_t) f];
            const auto shaped = (double) curve[(size_t) (f - note.startFrame)];
            shift[(size_t) f] = m > 0.0 && shaped > 0.0 ? shaped - m : plain;
            gain[(size_t) f] = e.gain;
            formant[(size_t) f] = e.formant;
        }
    }
    const auto smooth = [frames] (std::vector<double> v, int radius)
    {
        std::vector<double> out ((size_t) frames);
        for (int f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            int count = 0;
            for (int k = juce::jmax (0, f - radius); k <= juce::jmin (frames - 1, f + radius); ++k, ++count)
                sum += v[(size_t) k];
            out[(size_t) f] = sum / juce::jmax (1, count);
        }
        return out;
    };
    shift = smooth (shift, 2);
    gain = smooth (gain, 3);
    formant = smooth (formant, 3);
    const auto atSample = [&] (const std::vector<double>& v, double s)
    {
        const auto pos = juce::jlimit (0.0, (double) (frames - 1), s / a.hop);
        const auto i = (int) pos;
        const auto t = pos - i;
        return v[(size_t) i] * (1.0 - t) + v[(size_t) juce::jmin (frames - 1, i + 1)] * t;
    };
    const auto pitchedAt = [&] (int s) { const auto f = juce::jlimit (0, frames - 1, (int) std::lround ((double) s / a.hop)); return a.midi[(size_t) f] > 0.0f; };
    const auto periodAt = [&] (int s)
    {
        const auto f = juce::jlimit (0, frames - 1, (int) std::lround ((double) s / a.hop));
        return a.midi[(size_t) f] > 0.0f ? rate / hzOf (a.midi[(size_t) f]) : rate * 0.005;
    };

    // Only round the span: from a few periods before it to a few after
    const int pad = (int) (rate / 20.0);
    const int base = juce::jlimit (0, n, spanFrom - pad), end = juce::jlimit (base, n, spanTo + pad);
    std::vector<float> mono ((size_t) juce::jmax (1, end - base), 0.0f);
    for (int c = 0; c < audio.getNumChannels(); ++c)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), audio.getReadPointer (c, base), 1.0f / (float) audio.getNumChannels(), end - base);
    const auto x = [&mono, base] (int i) { return mono[(size_t) (i - base)]; };

    // Pitch marks: one per period on the waveform's peaks where it's pitched, every 5 ms where it isn't
    std::vector<int> marks;
    bool previousPitched = false;
    for (;;)
    {
        const int last = marks.empty() ? -1 : marks.back();
        const int probe = last < 0 ? base : last + juce::jmax (8, (int) (previousPitched ? periodAt (last) : rate * 0.005));
        if (probe >= end)
            break;
        int next = probe;
        if (pitchedAt (probe))
        {
            const auto period = periodAt (probe);
            const int from = previousPitched ? last + (int) (0.8 * period) : juce::jmax (base, last + 1);
            const int to = juce::jmin (end - 1, previousPitched ? last + (int) (1.2 * period) : from + (int) period);
            if (from > to)
                break;
            next = from;
            for (int s2 = from; s2 <= to; ++s2)
                if (x (s2) > x (next))
                    next = s2;
            previousPitched = true;
        }
        else
            previousPitched = false;
        if (next <= last)
            next = last + 8;
        if (next >= end)
            break;
        marks.push_back (next);
    }
    const int channels = audio.getNumChannels();
    juce::AudioBuffer<float> span (channels, juce::jmax (1, spanTo - spanFrom));
    if (marks.size() < 3)
    {
        for (int c = 0; c < channels; ++c)
            span.copyFrom (c, 0, audio, c, spanFrom, spanTo - spanFrom);
        return span;
    }

    // Synthesis: a grain (two periods, Hann) from the nearest analysis mark, placed every period / ratio
    juce::AudioBuffer<float> out (channels, end - base);
    out.clear();
    std::vector<float> weight ((size_t) (end - base), 0.0f);
    double ts = marks.front();
    size_t k = 0;
    while (ts < end)
    {
        while (k + 1 < marks.size() && std::abs (marks[k + 1] - ts) < std::abs (marks[k] - ts))
            ++k;
        const auto a0 = marks[k];
        const auto period = k + 1 < marks.size() ? marks[k + 1] - marks[k] : (k > 0 ? marks[k] - marks[k - 1] : (int) periodAt (a0));
        const auto half = juce::jmax (8, period);
        const auto pitched = pitchedAt (a0);
        const auto ratio = pitched ? std::pow (2.0, atSample (shift, ts) / 12.0) : 1.0;
        const auto g = (float) juce::Decibels::decibelsToGain (atSample (gain, ts));
        const auto centre = (int) std::lround (ts);
        // Formant shift: the grain read faster (brighter) or slower (darker), its period's spacing kept
        const auto fs = pitched ? std::pow (2.0, atSample (formant, ts) / 12.0) : 1.0;
        const bool resampled = std::abs (fs - 1.0) > 1.0e-3;
        for (int j = -half; j <= half; ++j)
        {
            const auto dst = centre + j;
            if (dst < base || dst >= end)
                continue;
            const auto w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) j / (float) half);
            if (resampled)
            {
                const auto pos = a0 + j * fs;
                const auto i0 = (int) std::floor (pos);
                if (i0 < 0 || i0 + 1 >= n)
                    continue;
                const auto t = (float) (pos - i0);
                for (int c = 0; c < channels; ++c)
                {
                    const auto* in = audio.getReadPointer (c);
                    out.getWritePointer (c)[dst - base] += (in[i0] + (in[i0 + 1] - in[i0]) * t) * w * g;
                }
            }
            else
            {
                const auto src = a0 + j;
                if (src < 0 || src >= n)
                    continue;
                for (int c = 0; c < channels; ++c)
                    out.getWritePointer (c)[dst - base] += audio.getReadPointer (c)[src] * w * g;
            }
            weight[(size_t) (dst - base)] += w;
        }
        ts += juce::jmax (4.0, period / ratio);
    }
    for (int c = 0; c < channels; ++c)
    {
        const auto* o = out.getReadPointer (c);
        auto* d = span.getWritePointer (c);
        for (int i = spanFrom; i < spanTo; ++i)
            d[i - spanFrom] = o[i - base] / juce::jmax (0.35f, weight[(size_t) (i - base)]);
    }
    return span;
}
}

juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& audio, const Analysis& a, const std::vector<NoteEdit>& edits)
{
    bool anything = false;
    for (const auto& e : edits)
        anything = anything || ! e.isNeutral();
    if (! anything || audio.getNumSamples() < 4)
        return audio;
    auto result = synthesise (audio, a, edits, 0, audio.getNumSamples());

    // Notes moved in time: every note's start and end pinned where it goes, the audio between stretched to fit
    bool moved = false;
    for (const auto& e : edits)
        moved = moved || std::abs (e.time) > 1.0e-5;
    if (moved)
    {
        std::vector<std::pair<double, double>> anchors;
        for (size_t i = 0; i < a.notes.size() && i < edits.size(); ++i)
        {
            const auto s0 = a.frameToSeconds (a.notes[i].startFrame), s1 = a.frameToSeconds (a.notes[i].endFrame);
            anchors.push_back ({ s0, s0 + edits[i].time });
            anchors.push_back ({ s1, s1 + edits[i].time });
        }
        std::sort (anchors.begin(), anchors.end());
        const auto length = audio.getNumSamples() / a.sampleRate;
        std::vector<std::pair<double, double>> kept;
        for (const auto& [from, to] : anchors)
            if (from > 0.01 && from < length - 0.01 && to > 0.005 && to < length - 0.005
                && (kept.empty() || (from > kept.back().first + 0.003 && to > kept.back().second + 0.003)))
                kept.push_back ({ from, to });
        auto warped = audioedit::flexWarp (result, a.sampleRate, kept);
        warped.setSize (warped.getNumChannels(), audio.getNumSamples(), true, true);   // (the same length: the end is pinned)
        result = std::move (warped);
    }
    return result;
}

std::pair<int, int> noteSpan (const Analysis& a, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) a.notes.size()))
        return { 0, 0 };
    const auto& note = a.notes[(size_t) index];
    const auto margin = (int) (a.sampleRate * 0.03);   // (the shift eases in and out over a few frames)
    return { juce::jmax (0, note.startFrame * a.hop - margin), juce::jmin (a.numSamples, note.endFrame * a.hop + margin) };
}

void renderInto (juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& audio, const Analysis& a,
                 const std::vector<NoteEdit>& edits, int from, int to)
{
    from = juce::jlimit (0, audio.getNumSamples(), from);
    to = juce::jlimit (from, juce::jmin (audio.getNumSamples(), dest.getNumSamples()), to);
    if (to - from < 2)
        return;

    // Notes moved in time change the whole stretch round them: all of it again
    for (const auto& e : edits)
        if (std::abs (e.time) > 1.0e-5)
        {
            const auto all = render (audio, a, edits);
            for (int c = 0; c < dest.getNumChannels(); ++c)
                dest.copyFrom (c, 0, all, juce::jmin (c, all.getNumChannels() - 1), 0, juce::jmin (dest.getNumSamples(), all.getNumSamples()));
            return;
        }

    // Nothing changed round here: the recording as it is
    bool anything = false;
    for (size_t i = 0; i < a.notes.size() && i < edits.size(); ++i)
        if (! edits[i].isNeutral())
        {
            const auto [s0, s1] = noteSpan (a, (int) i);
            anything = anything || (s1 > from && s0 < to);
        }
    juce::AudioBuffer<float> span;
    if (anything)
        span = synthesise (audio, a, edits, from, to);
    else
    {
        span.setSize (audio.getNumChannels(), to - from);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            span.copyFrom (c, 0, audio, c, from, to - from);
    }

    // In, with a short crossfade at each end into what's there
    const auto fade = juce::jmin (240, (to - from) / 4);
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        auto* d = dest.getWritePointer (c);
        const auto* s2 = span.getReadPointer (juce::jmin (c, span.getNumChannels() - 1));
        for (int i = 0; i < to - from; ++i)
        {
            const auto w = i < fade ? (float) i / (float) fade : (i >= to - from - fade ? (float) (to - from - 1 - i) / (float) fade : 1.0f);
            d[from + i] = d[from + i] * (1.0f - w) + s2[i] * w;
        }
    }
}

//==============================================================================
juce::String toString (const std::vector<NoteEdit>& edits)
{
    juce::StringArray parts;
    for (size_t i = 0; i < edits.size(); ++i)
        if (! edits[i].isNeutral())
            parts.add (juce::String ((int) i) + ":" + juce::String (edits[i].shift, 4) + ":" + juce::String (edits[i].drift, 3)
                       + ":" + juce::String (edits[i].gain, 2) + ":" + juce::String (edits[i].vibrato, 3) + ":" + juce::String (edits[i].formant, 3)
                       + ":" + juce::String (edits[i].snap, 3) + ":" + juce::String (edits[i].time, 5));
    return parts.joinIntoString (";");
}

std::vector<NoteEdit> fromString (const juce::String& text, size_t numNotes)
{
    std::vector<NoteEdit> edits (numNotes);
    for (const auto& part : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto v = juce::StringArray::fromTokens (part, ":", "");
        if (v.size() < 3)
            continue;
        const auto i = v[0].getIntValue();
        if (! juce::isPositiveAndBelow (i, (int) numNotes))
            continue;
        edits[(size_t) i].shift = v[1].getDoubleValue();
        edits[(size_t) i].drift = juce::jlimit (0.0, 1.0, v[2].getDoubleValue());
        if (v.size() >= 4)
            edits[(size_t) i].gain = v[3].getDoubleValue();
        if (v.size() >= 8)
        {
            edits[(size_t) i].vibrato = juce::jlimit (0.0, 2.0, v[4].getDoubleValue());
            edits[(size_t) i].formant = juce::jlimit (-12.0, 12.0, v[5].getDoubleValue());
            edits[(size_t) i].snap = juce::jlimit (0.0, 1.0, v[6].getDoubleValue());
            edits[(size_t) i].time = v[7].getDoubleValue();
        }
    }
    return edits;
}

void correct (std::vector<NoteEdit>& edits, const Analysis& analysis, const std::vector<int>& which, double amount, const std::vector<int>& scale)
{
    edits.resize (analysis.notes.size());
    for (const auto i : which)
    {
        if (! juce::isPositiveAndBelow (i, (int) analysis.notes.size()))
            continue;
        const auto centre = analysis.notes[(size_t) i].centre;
        const auto now = centre + edits[(size_t) i].shift;
        auto target = std::round (now);
        if (! scale.empty())
        {
            // The nearest note in the scale
            double best = 1.0e9;
            for (int step = -6; step <= 6; ++step)
            {
                const auto candidate = std::round (now) + step;
                const auto pc = ((int) candidate % 12 + 12) % 12;
                if (std::find (scale.begin(), scale.end(), pc) != scale.end() && std::abs (candidate - now) < std::abs (best - now))
                    best = candidate;
            }
            if (best < 1.0e8)
                target = best;
        }
        edits[(size_t) i].shift += amount * (target - now);
    }
}

//==============================================================================
double centreAfter (const Note& note, const NoteEdit& edit)
{
    const auto moved = note.centre + edit.shift;
    return moved + edit.snap * (std::round (moved) - moved);
}

std::vector<float> curveAfter (const Analysis& a, int index, const NoteEdit& e)
{
    std::vector<float> out;
    if (! juce::isPositiveAndBelow (index, (int) a.notes.size()))
        return out;
    const auto& note = a.notes[(size_t) index];
    const int count = juce::jmax (0, note.endFrame - note.startFrame);
    out.assign ((size_t) count, 0.0f);

    // Its slow part (a moving average over ~80 ms) is the drift; what's left round it is the vibrato
    const int radius = juce::jmax (2, (int) std::lround (0.04 * a.sampleRate / a.hop));
    std::vector<double> slow ((size_t) count, 0.0);
    for (int k = 0; k < count; ++k)
    {
        double sum = 0.0;
        int n = 0;
        for (int j = juce::jmax (0, k - radius); j <= juce::jmin (count - 1, k + radius); ++j)
            if (const auto m = a.midi[(size_t) (note.startFrame + j)]; m > 0.0f)
            {
                sum += m;
                ++n;
            }
        slow[(size_t) k] = n > 0 ? sum / n : note.centre;
    }
    const auto target = centreAfter (note, e);
    for (int k = 0; k < count; ++k)
    {
        const auto m = (double) a.midi[(size_t) (note.startFrame + k)];
        if (m <= 0.0)
            continue;
        const auto driftPart = slow[(size_t) k] - note.centre;
        const auto vibratoPart = m - slow[(size_t) k];
        out[(size_t) k] = (float) (target + driftPart * (1.0 - e.drift) + vibratoPart * e.vibrato);
    }
    return out;
}

namespace
{
    void describe (const Analysis& a, Note& note)
    {
        std::vector<float> pitches;
        double loudness = 0.0;
        for (int k = note.startFrame; k < note.endFrame; ++k)
        {
            if (a.midi[(size_t) k] > 0.0f)
                pitches.push_back (a.midi[(size_t) k]);
            loudness += a.level[(size_t) k];
        }
        if (! pitches.empty())
            note.centre = median (pitches);
        note.level = (float) (loudness / juce::jmax (1, note.endFrame - note.startFrame));
    }
}

void splitNote (Analysis& a, std::vector<NoteEdit>& edits, int index, int frame)
{
    if (! juce::isPositiveAndBelow (index, (int) a.notes.size()))
        return;
    auto& note = a.notes[(size_t) index];
    if (frame <= note.startFrame + 2 || frame >= note.endFrame - 2)
        return;
    Note second = note;
    second.startFrame = frame;
    note.endFrame = frame;
    describe (a, note);
    describe (a, second);
    a.notes.insert (a.notes.begin() + index + 1, second);
    edits.resize (a.notes.size() - 1);
    edits.insert (edits.begin() + index + 1, edits[(size_t) index]);
}

void joinNotes (Analysis& a, std::vector<NoteEdit>& edits, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    if (indices.size() < 2 || indices.front() < 0 || indices.back() >= (int) a.notes.size())
        return;
    edits.resize (a.notes.size());
    auto& first = a.notes[(size_t) indices.front()];
    first.endFrame = a.notes[(size_t) indices.back()].endFrame;
    describe (a, first);
    // Everything between the first and the last becomes part of the first
    a.notes.erase (a.notes.begin() + indices.front() + 1, a.notes.begin() + indices.back() + 1);
    edits.erase (edits.begin() + indices.front() + 1, edits.begin() + indices.back() + 1);
}

juce::String segmentsToString (const Analysis& a)
{
    juce::StringArray parts;
    for (const auto& n : a.notes)
        parts.add (juce::String (n.startFrame) + "-" + juce::String (n.endFrame));
    return parts.joinIntoString (",");
}

void applySegments (Analysis& a, const juce::String& text)
{
    if (text.isEmpty())
        return;
    std::vector<Note> notes;
    for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
    {
        Note n;
        n.startFrame = juce::jlimit (0, (int) a.midi.size(), part.upToFirstOccurrenceOf ("-", false, false).getIntValue());
        n.endFrame = juce::jlimit (n.startFrame, (int) a.midi.size(), part.fromFirstOccurrenceOf ("-", false, false).getIntValue());
        if (n.endFrame - n.startFrame < 2)
            continue;
        describe (a, n);
        notes.push_back (n);
    }
    if (! notes.empty())
        a.notes = std::move (notes);
}

} // namespace riff::pitchcorrect
