#include "StockDsp.h"

/*
    The stock EQs (Logic's EQ folder): Channel EQ, Single Band EQ, and three vintage styles - a
    British console, a ten-band graphic and a passive tube program EQ. Each one's curve is drawn in
    its window from the same filters it plays through.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** An EQ made of biquads in series, set from the knobs: the same code plays and draws the curve. */
    template <size_t N>
    struct FilterSet
    {
        std::array<Biquad, N> filters;
        float outputGain = 1.0f;

        double responseDb (double freq, double rate) const
        {
            double db = gainToDb (outputGain);
            for (const auto& f : filters)
                db += f.magnitudeDb (freq, rate);
            return db;
        }
    };

    /** Base for the EQs: recalculates the filters when a knob moved, then runs them on both channels. */
    template <size_t N, void (*Design) (const float*, double, FilterSet<N>&), bool Saturates = false>
    class EqEffectBase : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& f : set.filters)
                f.reset();
            last.fill (std::numeric_limits<float>::quiet_NaN());
        }

        void process (float* l, float* r, int n) noexcept override
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            if (v != last)
            {
                // (the states are kept: a knob turned while playing doesn't click)
                FilterSet<N> fresh;
                Design (v.data(), sampleRate, fresh);
                for (size_t i = 0; i < N; ++i)
                {
                    auto& f = set.filters[i];
                    const auto z1 = f.z1, z2 = f.z2;
                    f = fresh.filters[i];
                    f.z1 = z1;
                    f.z2 = z2;
                }
                set.outputGain = fresh.outputGain;
                drive = Saturates ? driveFrom (v.data()) : 0.0f;
                last = v;
            }
            for (int i = 0; i < n; ++i)
            {
                auto a = l[i], b = r[i];
                for (auto& f : set.filters)
                {
                    a = f.process (a, 0);
                    b = f.process (b, 1);
                }
                if constexpr (Saturates)
                {
                    if (drive > 0.0f)
                    {
                        a = saturate (a * (1.0f + drive)) / (1.0f + drive * 0.6f);
                        b = saturate (b * (1.0f + drive)) / (1.0f + drive * 0.6f);
                    }
                }
                l[i] = a * set.outputGain;
                r[i] = b * set.outputGain;
            }
        }

        static double response (const float* v, double freq, double rate)
        {
            FilterSet<N> s;
            Design (v, rate, s);
            return s.responseDb (freq, rate);
        }

        static float driveFrom (const float* v);

    private:
        FilterSet<N> set;
        std::array<float, maxParams> last {};
        float drive = 0.0f;
    };

    //==============================================================================
    // Channel EQ: low cut, low shelf, three bells, high shelf, high cut (24 dB/octave cuts), output
    void designChannel (const float* v, double rate, FilterSet<10>& s)
    {
        auto& f = s.filters;
        if (v[0] > 10.5f) { f[0].setHighPass (rate, v[0], butterQ1); f[1].setHighPass (rate, v[0], butterQ2); }
        else { f[0].setIdentity(); f[1].setIdentity(); }
        f[2].setLowShelf (rate, v[1], v[2]);
        f[3].setPeak (rate, v[3], v[4], v[5]);
        f[4].setPeak (rate, v[6], v[7], v[8]);
        f[5].setPeak (rate, v[9], v[10], v[11]);
        f[6].setHighShelf (rate, v[12], v[13]);
        if (v[14] < 19900.0f) { f[7].setLowPass (rate, v[14], butterQ1); f[8].setLowPass (rate, v[14], butterQ2); }
        else { f[7].setIdentity(); f[8].setIdentity(); }
        f[9].setIdentity();
        s.outputGain = dbToGain (v[15]);
    }

    // Single Band EQ: one filter of any kind
    void designSingle (const float* v, double rate, FilterSet<2>& s)
    {
        auto& f = s.filters;
        const auto type = juce::roundToInt (v[0]);
        const bool steep = juce::roundToInt (v[4]) == 1;
        f[1].setIdentity();
        switch (type)
        {
            case 0: f[0].setHighPass (rate, v[1], steep ? butterQ1 : v[3]); if (steep) f[1].setHighPass (rate, v[1], butterQ2); break;
            case 1: f[0].setLowPass (rate, v[1], steep ? butterQ1 : v[3]);  if (steep) f[1].setLowPass (rate, v[1], butterQ2); break;
            case 2: f[0].setLowShelf (rate, v[1], v[2], juce::jlimit (0.3f, 2.0f, v[3])); break;
            case 3: f[0].setHighShelf (rate, v[1], v[2], juce::jlimit (0.3f, 2.0f, v[3])); break;
            case 4: f[0].setPeak (rate, v[1], v[2], v[3]); break;
            case 5: f[0].setNotch (rate, v[1], v[3]); break;
            default: f[0].setBandPass (rate, v[1], v[3]); break;
        }
        s.outputGain = 1.0f;
    }

    // Console EQ (British console style): high pass, low shelf, mid bell, high shelf, drive, output
    void designConsole (const float* v, double rate, FilterSet<5>& s)
    {
        static const float hpf[] { 0.0f, 50.0f, 80.0f, 160.0f, 300.0f };
        static const float lows[] { 35.0f, 60.0f, 110.0f, 220.0f };
        static const float mids[] { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
        auto& f = s.filters;
        const auto h = juce::jlimit (0, 4, juce::roundToInt (v[0]));
        if (h > 0) { f[0].setHighPass (rate, hpf[h], 0.6); f[1].setHighPass (rate, hpf[h], 0.9); }
        else { f[0].setIdentity(); f[1].setIdentity(); }
        // (a little resonance in the shelves gives the console's bump and dip)
        f[2].setLowShelf (rate, lows[juce::jlimit (0, 3, juce::roundToInt (v[1]))], v[2], 1.0);
        // The bell gets narrower as it's turned up, as on the console
        f[3].setPeak (rate, mids[juce::jlimit (0, 5, juce::roundToInt (v[3]))], v[4], 0.7 + std::abs (v[4]) / 18.0 * 0.8);
        f[4].setHighShelf (rate, 12000.0, v[5], 0.8);
        s.outputGain = dbToGain (v[7]);
    }

    // Graphic EQ: ten octave bands
    const float graphicBands[] { 31.0f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    void designGraphic (const float* v, double rate, FilterSet<10>& s)
    {
        for (size_t i = 0; i < 10; ++i)
            s.filters[i].setPeak (rate, graphicBands[i], v[i], 1.4 + std::abs (v[i]) / 12.0 * 0.8);
        s.outputGain = dbToGain (v[10]);
    }

    // Tube EQ (passive program EQ): low boost and cut at once (the famous trick), high bell, high cut
    void designTube (const float* v, double rate, FilterSet<4>& s)
    {
        static const float lows[] { 20.0f, 30.0f, 60.0f, 100.0f };
        static const float highs[] { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
        static const float attens[] { 5000.0f, 10000.0f, 20000.0f };
        auto& f = s.filters;
        const auto lowF = lows[juce::jlimit (0, 3, juce::roundToInt (v[0]))];
        f[0].setLowShelf (rate, lowF, v[1] * 1.35, 0.5);           // boost: up to 13.5 dB
        f[1].setLowShelf (rate, lowF * 1.6, -v[2] * 1.75, 0.6);     // cut, a little higher: the dip above the boost
        const auto bandwidth = juce::jlimit (0.0f, 10.0f, v[5]);
        f[2].setPeak (rate, highs[juce::jlimit (0, 6, juce::roundToInt (v[3]))], v[4] * 1.8, 2.5 - bandwidth * 0.22);
        f[3].setHighShelf (rate, attens[juce::jlimit (0, 2, juce::roundToInt (v[6]))], -v[7] * 1.6, 0.7);
        s.outputGain = dbToGain (v[8]);
    }

    template <> float EqEffectBase<5, designConsole, true>::driveFrom (const float* v) { return v[6] * 0.25f; }
    template <> float EqEffectBase<4, designTube, true>::driveFrom (const float*)     { return 0.15f; }   // (always a little warm)
    template <> float EqEffectBase<10, designChannel, false>::driveFrom (const float*) { return 0.0f; }
    template <> float EqEffectBase<2, designSingle, false>::driveFrom (const float*)   { return 0.0f; }
    template <> float EqEffectBase<10, designGraphic, false>::driveFrom (const float*) { return 0.0f; }

    using ChannelEq = EqEffectBase<10, designChannel>;
    using SingleEq = EqEffectBase<2, designSingle>;
    using ConsoleEq = EqEffectBase<5, designConsole, true>;
    using GraphicEq = EqEffectBase<10, designGraphic>;
    using TubeEq = EqEffectBase<4, designTube, true>;

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockEqEffects()
{
    const juce::String dB (" dB"), hz (" Hz");
    static const std::vector<StockEffect> effects {
        { { "channelEq", "Channel EQ", {
              { "hpFreq",  "Low Cut",     hz, 10.0f,   2000.0f,  10.0f,    100.0f },
              { "lsFreq",  "Low Shelf",   hz, 20.0f,   1000.0f,  100.0f,   150.0f },
              { "lsGain",  "Shelf Gain",  dB, -24.0f,  24.0f,    0.0f,     0.0f },
              { "p1Freq",  "Low Mid",     hz, 20.0f,   20000.0f, 250.0f,   1000.0f },
              { "p1Gain",  "Gain",        dB, -24.0f,  24.0f,    0.0f,     0.0f },
              { "p1Q",     "Q",           "", 0.1f,    10.0f,    0.9f,     1.0f },
              { "p2Freq",  "Mid",         hz, 20.0f,   20000.0f, 1000.0f,  1000.0f },
              { "p2Gain",  "Gain",        dB, -24.0f,  24.0f,    0.0f,     0.0f },
              { "p2Q",     "Q",           "", 0.1f,    10.0f,    0.9f,     1.0f },
              { "p3Freq",  "High Mid",    hz, 20.0f,   20000.0f, 4000.0f,  1000.0f },
              { "p3Gain",  "Gain",        dB, -24.0f,  24.0f,    0.0f,     0.0f },
              { "p3Q",     "Q",           "", 0.1f,    10.0f,    0.9f,     1.0f },
              { "hsFreq",  "High Shelf",  hz, 1000.0f, 20000.0f, 8000.0f,  5000.0f },
              { "hsGain",  "Shelf Gain",  dB, -24.0f,  24.0f,    0.0f,     0.0f },
              { "lpFreq",  "High Cut",    hz, 1000.0f, 20000.0f, 20000.0f, 6000.0f },
              { "output",  "Output",      dB, -24.0f,  24.0f,    0.0f,     0.0f } }, "EQ", ChannelEq::response,
            { 1, 2, 3, 3, 3, 2, 1, 1 } },
          make<ChannelEq> },

        { { "singleEq", "Single Band EQ", {
              { "type", "Type", "", 0.0f, 6.0f, 4.0f, 3.0f, { "Low Cut", "High Cut", "Low Shelf", "High Shelf", "Bell", "Notch", "Band Pass" } },
              { "freq", "Frequency", hz, 20.0f, 20000.0f, 1000.0f, 1000.0f },
              { "gain", "Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "q", "Q", "", 0.1f, 10.0f, 0.71f, 1.0f },
              { "slope", "Slope", "", 0.0f, 1.0f, 0.0f, 0.5f, { "12 dB/oct", "24 dB/oct" } } }, "EQ", SingleEq::response },
          make<SingleEq> },

        { { "consoleEq", "Console EQ", {
              { "hpf", "High Pass", "", 0.0f, 4.0f, 0.0f, 2.0f, { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" } },
              { "lowFreq", "Low Freq", "", 0.0f, 3.0f, 1.0f, 1.5f, { "35 Hz", "60 Hz", "110 Hz", "220 Hz" } },
              { "lowGain", "Low", dB, -16.0f, 16.0f, 0.0f, 0.0f },
              { "midFreq", "Mid Freq", "", 0.0f, 5.0f, 2.0f, 2.5f, { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" } },
              { "midGain", "Mid", dB, -18.0f, 18.0f, 0.0f, 0.0f },
              { "highGain", "High (12k)", dB, -16.0f, 16.0f, 0.0f, 0.0f },
              { "drive", "Drive", "", 0.0f, 10.0f, 2.0f, 5.0f },
              { "output", "Output", dB, -18.0f, 18.0f, 0.0f, 0.0f } }, "EQ", ConsoleEq::response, { 1, 2, 2, 1, 2 } },
          make<ConsoleEq> },

        { { "graphicEq", "Graphic EQ", {
              { "b31", "31", dB, -12.0f, 12.0f, 0.0f, 0.0f }, { "b63", "63", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "b125", "125", dB, -12.0f, 12.0f, 0.0f, 0.0f }, { "b250", "250", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "b500", "500", dB, -12.0f, 12.0f, 0.0f, 0.0f }, { "b1k", "1k", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "b2k", "2k", dB, -12.0f, 12.0f, 0.0f, 0.0f }, { "b4k", "4k", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "b8k", "8k", dB, -12.0f, 12.0f, 0.0f, 0.0f }, { "b16k", "16k", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "output", "Output", dB, -12.0f, 12.0f, 0.0f, 0.0f } }, "EQ", GraphicEq::response },
          make<GraphicEq> },

        { { "tubeEq", "Tube EQ", {
              { "lowFreq", "Low Freq", "", 0.0f, 3.0f, 2.0f, 1.5f, { "20 Hz", "30 Hz", "60 Hz", "100 Hz" } },
              { "lowBoost", "Low Boost", "", 0.0f, 10.0f, 0.0f, 5.0f },
              { "lowAtten", "Low Atten", "", 0.0f, 10.0f, 0.0f, 5.0f },
              { "highFreq", "High Freq", "", 0.0f, 6.0f, 4.0f, 3.0f, { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" } },
              { "highBoost", "High Boost", "", 0.0f, 10.0f, 0.0f, 5.0f },
              { "bandwidth", "Bandwidth", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "attenFreq", "Atten Sel", "", 0.0f, 2.0f, 1.0f, 1.0f, { "5 kHz", "10 kHz", "20 kHz" } },
              { "highAtten", "High Atten", "", 0.0f, 10.0f, 0.0f, 5.0f },
              { "output", "Output", dB, -12.0f, 12.0f, 0.0f, 0.0f } }, "EQ", TubeEq::response, { 3, 3, 2, 1 } },
          make<TubeEq> },
    };
    return effects;
}

} // namespace riff
