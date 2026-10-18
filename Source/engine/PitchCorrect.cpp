#include "PitchCorrect.h"

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
juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& audio, const Analysis& a, const std::vector<NoteEdit>& edits)
{
    bool anything = false;
    for (const auto& e : edits)
        anything = anything || ! e.isNeutral();
    if (! anything || audio.getNumSamples() < 4)
        return audio;

    const int n = audio.getNumSamples();
    const int frames = (int) a.midi.size();
    const auto rate = a.sampleRate;

    // What each frame gets: its note's shift (and straightening), its note's gain; smoothed over ~25 ms
    std::vector<double> shift ((size_t) frames, 0.0), gain ((size_t) frames, 0.0);
    for (size_t i = 0; i < a.notes.size() && i < edits.size(); ++i)
    {
        const auto& note = a.notes[i];
        const auto& e = edits[i];
        for (int f = juce::jmax (0, note.startFrame); f < juce::jmin (frames, note.endFrame); ++f)
        {
            const auto m = (double) a.midi[(size_t) f];
            shift[(size_t) f] = e.shift + (m > 0.0 ? e.drift * (note.centre - m) : 0.0);
            gain[(size_t) f] = e.gain;
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

    // Pitch marks: one per period on the waveform's peaks where it's pitched, every 5 ms where it isn't
    const auto x = monoOf (audio);
    std::vector<int> marks;
    bool previousPitched = false;
    for (;;)
    {
        const int last = marks.empty() ? -1 : marks.back();
        const int probe = last < 0 ? 0 : last + juce::jmax (8, (int) (previousPitched ? periodAt (last) : rate * 0.005));
        if (probe >= n)
            break;
        int next = probe;
        if (pitchedAt (probe))
        {
            const auto period = periodAt (probe);
            const int from = previousPitched ? last + (int) (0.8 * period) : juce::jmax (0, last + 1);
            const int to = juce::jmin (n - 1, previousPitched ? last + (int) (1.2 * period) : from + (int) period);
            if (from > to)
                break;
            next = from;
            for (int s2 = from; s2 <= to; ++s2)
                if (x[(size_t) s2] > x[(size_t) next])
                    next = s2;
            previousPitched = true;
        }
        else
            previousPitched = false;
        if (next <= last)
            next = last + 8;
        if (next >= n)
            break;
        marks.push_back (next);
    }
    if (marks.size() < 3)
        return audio;

    // Synthesis: a grain (two periods, Hann) from the nearest analysis mark, placed every period / ratio
    const int channels = audio.getNumChannels();
    juce::AudioBuffer<float> out (channels, n);
    out.clear();
    std::vector<float> weight ((size_t) n, 0.0f);
    double ts = marks.front();
    size_t k = 0;
    while (ts < n)
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
        for (int j = -half; j <= half; ++j)
        {
            const auto src = a0 + j, dst = centre + j;
            if (src < 0 || src >= n || dst < 0 || dst >= n)
                continue;
            const auto w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) j / (float) half);
            for (int c = 0; c < channels; ++c)
                out.getWritePointer (c)[dst] += audio.getReadPointer (c)[src] * w * g;
            weight[(size_t) dst] += w;
        }
        ts += juce::jmax (4.0, period / ratio);
    }
    for (int c = 0; c < channels; ++c)
    {
        auto* o = out.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            o[i] /= juce::jmax (0.35f, weight[(size_t) i]);
    }
    return out;
}

//==============================================================================
juce::String toString (const std::vector<NoteEdit>& edits)
{
    juce::StringArray parts;
    for (size_t i = 0; i < edits.size(); ++i)
        if (! edits[i].isNeutral())
            parts.add (juce::String ((int) i) + ":" + juce::String (edits[i].shift, 4) + ":" + juce::String (edits[i].drift, 3)
                       + ":" + juce::String (edits[i].gain, 2));
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

} // namespace riff::pitchcorrect
