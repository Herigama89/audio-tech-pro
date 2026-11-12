#include "../engine/BandAnalysis.h"
#include "EffectDesigns.h"
#include <numeric>
#include "../engine/StockDsp.h"
#include "EffectFace.h"
#include "CompressorFaceData.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "Looks.h"
#include "ClassicLayouts.h"
#include <cstring>
#include <map>

namespace riff::effectdesign
{
namespace
{
    using R = juce::Rectangle<int>;
    using RF = juce::Rectangle<float>;

    float valueOf (const EffectInfo& info, const float* v, const char* id, float fallback = 0.0f)
    {
        for (size_t i = 0; i < info.params.size(); ++i)
            if (info.params[i].id == id)
                return v[i];
        return fallback;
    }

    void brushed (juce::Graphics& g, R area, juce::Colour top, juce::Colour bottom, float lineAlpha)
    {
        g.setGradientFill (juce::ColourGradient (top, 0.0f, (float) area.getY(), bottom, 0.0f, (float) area.getBottom(), false));
        g.fillRect (area);
        juce::Random random (11);
        for (int y = area.getY(); y < area.getBottom(); y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (lineAlpha * random.nextFloat()));
            g.fillRect (area.getX(), y, area.getWidth(), 1);
        }
    }

    void title (juce::Graphics& g, R area, const juce::String& text, juce::Colour colour, float size, juce::Justification j = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (size));
        g.drawText (text, area, j, false);
    }

    //==============================================================================
    // Studio Compressor (as Logic's Compressor)
    //==============================================================================
    /** Each circuit's own panel and meter (the window changes with the circuit, as Logic's does). */
    struct CompressorLook { juce::Colour top, bottom, meterFace, meterInk, text; const char* badge; };
    CompressorLook compressorLook (int circuit)
    {
        switch (circuit)
        {
            case 0:  return { juce::Colour (0xff7d838b), juce::Colour (0xff535960), juce::Colour (0xffe3e5e6), juce::Colour (0xff1c1c1c), juce::Colours::white, "PLATINUM DIGITAL" };
            case 2:  return { juce::Colour (0xff4b4f55), juce::Colour (0xff2b2e32), juce::Colour (0xffdcd5c3), juce::Colour (0xff1c1c1c), juce::Colour (0xffe6e6e6), "STUDIO FET" };
            case 3:  return { juce::Colour (0xff2a2c2f), juce::Colour (0xff141517), juce::Colour (0xff121314), juce::Colour (0xffe9e9e9), juce::Colour (0xffd8d8d8), "CLASSIC VCA" };
            case 4:  return { juce::Colour (0xff5d636a), juce::Colour (0xff3a3f45), juce::Colour (0xffece7d6), juce::Colour (0xff1c1c1c), juce::Colours::white, "VINTAGE VCA" };
            case 5:  return { juce::Colour (0xff222325), juce::Colour (0xff0e0e0f), juce::Colour (0xfff2efe2), juce::Colour (0xff1a1a1a), juce::Colour (0xffcfd2d6), "VINTAGE FET" };
            case 6:  return { juce::Colour (0xff9c9a92), juce::Colour (0xff75736c), juce::Colour (0xffeadfc2), juce::Colour (0xff1e1c18), juce::Colour (0xff1f1f1f), "VINTAGE OPTO" };
            default: return { juce::Colour (0xff35537c), juce::Colour (0xff1f3352), juce::Colour (0xffd7dad8), juce::Colour (0xff222222), juce::Colours::white, "STUDIO VCA" };
        }
    }

    void paintCompressor (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float gr)
    {
        const auto circuit = juce::roundToInt (valueOf (info, v, "model", 1.0f));
        const auto look = compressorLook (circuit);
        brushed (g, bounds, look.top, look.bottom, 0.035f);

        // The meter: a face in a dark bezel, the needle showing the gain reduction
        const RF bezel (130.0f, 78.0f, 540.0f, 206.0f);
        g.setColour (juce::Colour (0xff101214));
        g.fillRect (bezel);
        g.setColour (juce::Colours::black);
        g.drawRect (bezel, 2.0f);
        auto face = bezel.reduced (16.0f, 12.0f);
        g.setGradientFill (juce::ColourGradient (look.meterFace.brighter (0.05f), face.getCentreX(), face.getY(), look.meterFace.darker (0.12f), face.getCentreX(), face.getBottom(), false));
        g.fillRect (face);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRect (face, 1.0f);
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + 250.0f);
        const auto arcRadius = 340.0f;
        const auto angleFor = [] (float db) { return juce::jmap (std::sqrt (juce::jlimit (0.0f, 50.0f, -db) / 50.0f), 0.0f, 1.0f, 0.6f, -0.6f); };
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (face.toNearestInt());
            juce::Path arc;
            arc.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, angleFor (-50.0f), angleFor (0.0f), true);
            g.setColour (look.meterInk);
            g.strokePath (arc, juce::PathStrokeType (1.8f));
            g.setFont (juce::FontOptions (22.0f));
            for (const auto db : { -50.0f, -40.0f, -30.0f, -25.0f, -20.0f, -15.0f, -10.0f, -8.0f, -6.0f, -5.0f, -4.0f, -3.0f, -2.0f, -1.0f, 0.0f })
            {
                const auto a = angleFor (db);
                const bool major = db == -50.0f || db == -30.0f || db == -20.0f || db == -10.0f || db == -5.0f || db == 0.0f;
                g.drawLine ({ pivot.getPointOnCircumference (arcRadius, a), pivot.getPointOnCircumference (arcRadius + (major ? 18.0f : 10.0f), a) }, major ? 1.8f : 1.2f);
                if (major)
                {
                    const auto at = pivot.getPointOnCircumference (arcRadius + 40.0f, a);
                    g.drawText (db == 0.0f ? "0" : juce::String ((int) db), RF (at.x - 28.0f, at.y - 13.0f, 56.0f, 26.0f), juce::Justification::centred, false);
                }
            }
            const auto needle = angleFor (-gr);
            g.setColour (circuit == 3 ? juce::Colour (0xffff6a3a) : look.meterInk);
            g.drawLine ({ pivot.getPointOnCircumference (arcRadius - 150.0f, needle), pivot.getPointOnCircumference (arcRadius + 26.0f, needle) }, 2.6f);
        }
        g.setColour (look.meterInk.withAlpha (0.8f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("GAIN REDUCTION", face.removeFromBottom (26.0f), juce::Justification::centred, false);
        title (g, R (130, 286, 540, 18), look.badge, look.text.withAlpha (0.55f), 12.0f);

        // Labels, engraved
        const auto text = look.text.withAlpha (0.92f);
        const auto label = [&] (int x, int y, int w, const char* t) { title (g, R (x, y, w, 20), t, text, 15.0f); };
        label (15, 300, 130, "INPUT GAIN");
        label (155, 300, 130, "THRESHOLD");
        label (295, 300, 130, "RATIO");
        label (435, 300, 130, "MAKE UP");
        label (570, 300, 170, "AUTO GAIN");
        label (155, 440, 130, "KNEE");
        label (295, 440, 130, "ATTACK");
        label (435, 440, 130, "RELEASE");
        label (690, 100, 120, "OUTPUT GAIN");
        // The two side sections
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (830, 30, 1, bounds.getHeight() - 60);
        g.fillRect (1070, 30, 1, bounds.getHeight() - 60);
        title (g, R (835, 26, 230, 24), "SIDE CHAIN", text, 17.0f);
        title (g, R (1075, 26, 220, 24), "OUTPUT", text, 17.0f);
        label (845, 58, 200, "DETECTION");
        label (850, 172, 95, "FILTER");
        label (955, 172, 95, "LISTEN");
        label (835, 300, 115, "FREQUENCY");
        label (950, 300, 115, "Q");
        label (892, 434, 115, "GAIN");
        label (580, 462, 120, "AUTO RELEASE");
        label (1080, 64, 100, "LIMITER");
        label (1130, 138, 105, "THRESHOLD");
        label (1080, 300, 100, "DISTORTION");
        label (1185, 300, 100, "MIX");
        const auto limiterOn = valueOf (info, v, "limiter") > 0.5f;
        g.setColour (limiterOn ? juce::Colour (0xffff3b30) : juce::Colour (0xff5a1c1a));
        g.fillEllipse (1184.0f, 102.0f, 12.0f, 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (1184.0f, 102.0f, 12.0f, 12.0f, 1.0f);
    }

    //==============================================================================
    // Channel EQ (as Logic's): navy screen, a colour per band, values to drag
    //==============================================================================
    // (as Logic's: red, orange, yellow, green, aqua, blue, purple, pink)
    const juce::Colour bandColours[] { juce::Colour (0xffff5c5c), juce::Colour (0xffff9a3c), juce::Colour (0xffe7d94a), juce::Colour (0xff3ddc6a),
                                       juce::Colour (0xff38d6c4), juce::Colour (0xff4f9bff), juce::Colour (0xffb07bff), juce::Colour (0xffff7ad0) };
    constexpr int channelOn1 = 25, channelAnalyzer = 33;

    /** A Channel EQ band on its own (the others switched off), for its own coloured curve. */
    double bandResponse (const EffectInfo& info, const float* v, int band, double f, double rate)
    {
        std::array<float, BuiltInEffect::maxParams> only {};
        std::copy (v, v + info.params.size(), only.begin());
        for (int k = 0; k < 8; ++k)
            only[(size_t) (channelOn1 + k)] = k == band ? v[channelOn1 + k] : 0.0f;
        only[15] = 0.0f;   // (no master gain)
        return info.response (only.data(), f, rate);
    }

    void paintChannelEq (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float)
    {
        const RF screen (40.0f, 48.0f, (float) bounds.getWidth() - 160.0f, 262.0f);
        if (theme::glassSkin())
        {
            // Liquid Glass: a glass card, the screen a dark glass display
            g.fillAll (theme::console);
            look::drawGlassPanel (g, bounds.toFloat().reduced (4.0f), 16.0f);
            g.setColour (juce::Colour (0xff0c0d10));
            g.fillRoundedRectangle (screen.expanded (6.0f), 12.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (screen.expanded (6.0f), 12.0f, 1.0f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2433), 0.0f, 0.0f, juce::Colour (0xff0f1620), 0.0f, (float) bounds.getBottom(), false));
            g.fillRect (bounds);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2b3e), 0.0f, screen.getY(), juce::Colour (0xff15202f), 0.0f, screen.getBottom(), false));
            g.fillRect (screen);
        }
        if (info.type == "channelEq" && bounds.getHeight() >= 600)
        {
            // the dynamic EQ's rows: their captions
            const auto ink = theme::glassSkin() ? theme::dimText : juce::Colours::white.withAlpha (0.55f);
            const auto w = (bounds.getWidth() - 160) / 8;
            g.setColour (ink);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("DYNAMIC EQ", RF (40.0f, 418.0f, (float) w, 26.0f), juce::Justification::centred, false);
            g.drawText ("ATTACK", RF (40.0f, 476.0f, (float) w, 14.0f), juce::Justification::centred, false);
            g.drawText ("RELEASE", RF (40.0f + (float) (w * 7), 476.0f, (float) w, 14.0f), juce::Justification::centred, false);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("threshold", RF (40.0f + (float) (w * 7), 418.0f, (float) w, 13.0f), juce::Justification::centred, false);
            g.drawText ("then range", RF (40.0f + (float) (w * 7), 430.0f, (float) w, 13.0f), juce::Justification::centred, false);
            g.setColour (ink.withAlpha (0.25f));
            g.fillRect (40.0f, 410.0f, (float) (w * 8), 1.0f);
        }

        const auto xOf = [screen] (double f) { return screen.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * screen.getWidth(); };
        const auto yOf = [screen] (double db) { return screen.getCentreY() - (float) (db / 24.0) * screen.getHeight() * 0.5f; };
        // Grid: decades and dB
        for (const auto f : { 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 800.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 10000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (f == 100.0 || f == 1000.0 || f == 10000.0 ? 0.09f : 0.04f));
            g.fillRect (xOf (f), screen.getY(), 1.0f, screen.getHeight());
        }
        g.setFont (juce::FontOptions (12.0f));
        for (const auto f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 800.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 10000.0, 20000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) - 18.0f, yOf (0.0) + 4.0f, 36.0f, 14.0f),
                        juce::Justification::centred, false);
        }
        for (const auto db : { -20.0, -10.0, -5.0, 5.0, 10.0, 20.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (screen.getX(), yOf (db), screen.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (juce::String ((int) std::abs (db)), RF (screen.getRight() + 4.0f, yOf (db) - 7.0f, 30.0f, 14.0f), juce::Justification::left, false);
        }
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRect (screen.getX(), yOf (0.0), screen.getWidth(), 1.0f);
        // The analyzer: the output's spectrum behind the curves
        if (info.params.size() > (size_t) channelAnalyzer && v[channelAnalyzer] > 0.5f)
            drawSpectrum (g, screen, xOf, juce::Colour (0xff7fb4e6));

        // Each band's own curve in its colour, then the whole curve over them
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (screen.toNearestInt());
        for (int band = 0; band < 8; ++band)
        {
            if (v[channelOn1 + band] < 0.5f)
                continue;
            juce::Path fill;
            fill.startNewSubPath (screen.getX(), yOf (0.0));
            for (int x = 0; x <= (int) screen.getWidth(); x += 3)
                fill.lineTo (screen.getX() + (float) x, juce::jlimit (screen.getY(), screen.getBottom(),
                                                                      yOf (bandResponse (info, v, band, 20.0 * std::pow (1000.0, x / (double) screen.getWidth()), 48000.0))));
            fill.lineTo (screen.getRight(), yOf (0.0));
            fill.closeSubPath();
            g.setColour (bandColours[band].withAlpha (0.22f));
            g.fillPath (fill);
        }
        juce::Path curve;
        for (int x = 0; x <= (int) screen.getWidth(); x += 2)
        {
            const auto y = juce::jlimit (screen.getY(), screen.getBottom(), yOf (info.response (v, 20.0 * std::pow (1000.0, x / (double) screen.getWidth()), 48000.0)));
            if (x == 0) curve.startNewSubPath (screen.getX(), y); else curve.lineTo (screen.getX() + (float) x, y);
        }
        juce::Path area (curve);
        area.lineTo (screen.getRight(), yOf (0.0));
        area.lineTo (screen.getX(), yOf (0.0));
        area.closeSubPath();
        g.setColour (juce::Colour (0xffbfd3e6).withAlpha (0.28f));
        g.fillPath (area);
        g.setColour (juce::Colour (0xffe4eef8));
        g.strokePath (curve, juce::PathStrokeType (1.8f));
        // The bands' points
        const struct { int band, freq, gain; } points[] { { 0, 0, -1 }, { 1, 1, 2 }, { 2, 3, 4 }, { 3, 6, 7 }, { 4, 9, 10 }, { 5, 16, 17 }, { 6, 12, 13 }, { 7, 14, -1 } };
        for (const auto& p : points)
        {
            const auto on = v[channelOn1 + p.band] > 0.5f;
            const auto x = xOf (juce::jlimit (20.0f, 20000.0f, v[p.freq])), y = yOf (p.gain < 0 ? 0.0f : v[p.gain]);
            g.setColour (bandColours[p.band].withAlpha (on ? 1.0f : 0.35f));
            if (on) g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            else g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.5f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.0f);
        }
    }

    void paintChannelEqIcons (juce::Graphics& g, int width, const float* v)
    {
        // The band icons above the screen, which are the bands' on/off buttons: lit in the band's colour when on
        const auto columns = 8;
        const auto step = (float) (width - 160) / (float) columns;
        for (int b = 0; b < columns; ++b)
        {
            const auto cx = 40.0f + step * ((float) b + 0.5f), cy = 24.0f;
            juce::Path p;
            switch (b)
            {
                case 0: p.startNewSubPath (cx - 14, cy + 8); p.quadraticTo (cx - 4, cy - 8, cx + 14, cy - 8); break;
                case 1: p.startNewSubPath (cx - 14, cy - 7); p.lineTo (cx - 2, cy - 7); p.lineTo (cx + 2, cy); p.lineTo (cx + 14, cy);
                        p.startNewSubPath (cx - 2, cy + 7); p.lineTo (cx + 2, cy); break;
                case 6: p.startNewSubPath (cx - 14, cy); p.lineTo (cx - 2, cy); p.lineTo (cx + 2, cy - 7); p.lineTo (cx + 14, cy - 7);
                        p.startNewSubPath (cx - 2, cy); p.lineTo (cx + 2, cy + 7); break;
                case 7: p.startNewSubPath (cx - 14, cy - 8); p.quadraticTo (cx + 4, cy - 8, cx + 14, cy + 8); break;
                default: p.startNewSubPath (cx - 14, cy); p.lineTo (cx - 7, cy); p.lineTo (cx, cy - 8); p.lineTo (cx + 7, cy); p.lineTo (cx + 14, cy);
                         p.startNewSubPath (cx - 7, cy); p.lineTo (cx, cy + 8); p.lineTo (cx + 7, cy); break;
            }
            const auto on = v[channelOn1 + b] > 0.5f;
            g.setColour (on ? bandColours[b] : juce::Colour (0xff59626e));
            g.strokePath (p, juce::PathStrokeType (on ? 1.8f : 1.4f));
        }
    }

    //==============================================================================
    // Tape Delay (as Logic's): slate blue sections, green values
    //==============================================================================
    void paintTapeDelay (juce::Graphics& g, R bounds, const EffectInfo&, const float*, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff42596f), 0.0f, 0.0f, juce::Colour (0xff2b3d52), 0.0f, (float) bounds.getBottom(), false));
        g.fillRect (bounds);
        const auto cyan = juce::Colour (0xffa9dcf2);
        title (g, R (0, 14, 390, 40), "DELAY", cyan, 28.0f);
        title (g, R (395, 14, 360, 40), "CHARACTER", cyan, 28.0f);
        title (g, R (755, 14, 245, 40), "FEEDBACK", cyan, 28.0f);
        title (g, R (0, 400, 755, 40), "MODULATION", cyan, 28.0f);
        title (g, R (755, 320, 245, 40), "OUTPUT", cyan, 28.0f);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (393, 10, 1, 380);
        g.fillRect (754, 10, 1, bounds.getHeight() - 20);
        g.fillRect (10, 392, 744, 1);
        g.fillRect (756, 310, 234, 1);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (394, 10, 1, 380);
        g.fillRect (755, 10, 1, bounds.getHeight() - 20);
        g.fillRect (10, 393, 744, 1);
    }

    //==============================================================================
    // Studio Reverb (as ChromaVerb): a dark screen with the space's colour cloud, a light panel of knobs
    //==============================================================================
    /** The reverb's damping as a decay ratio at a frequency (1: the Decay time; under 1 dies sooner). */
    double reverbRatioAt (const EffectInfo& info, const float* v, double f)
    {
        Biquad b[4];
        const auto rate = 48000.0;
        const auto part = [&] (const char* id, float fallback) { return valueOf (info, v, id, fallback); };
        const auto unit = [] (double r) { return 1.0 - 1.0 / juce::jlimit (0.1, 4.0, r); };
        b[0].setLowShelf (rate, part ("dLsFreq", 200.0f), unit (part ("dLsRatio", 1.0f)), 0.7);
        b[1].setPeak (rate, part ("dP1Freq", 600.0f), unit (part ("dP1Ratio", 1.0f)), part ("dP1Q", 0.7f));
        b[2].setPeak (rate, part ("dP2Freq", 3000.0f), unit (part ("dP2Ratio", 1.0f)), part ("dP2Q", 0.7f));
        b[3].setHighShelf (rate, part ("dHsFreq", 6000.0f), unit (part ("dHsRatio", 1.0f)), 0.7);
        double sum = 0.0;
        for (auto& q : b) sum += q.magnitudeDb (f, rate);
        return juce::jlimit (0.1, 1.9, 1.0 / std::max (0.05, 1.0 - sum));
    }

    /** The reverb's output EQ in dB at a frequency. */
    double reverbEqAt (const EffectInfo& info, const float* v, double f)
    {
        const auto rate = 48000.0;
        const auto part = [&] (const char* id, float fallback) { return valueOf (info, v, id, fallback); };
        const auto on = [&] (const char* id) { return part (id, 1.0f) > 0.5f; };
        Biquad b[6];
        if (on ("eqHpOn")) b[0].setHighPass (rate, std::max (20.0f, part ("lowCut", 20.0f)), 0.707); else b[0].setIdentity();
        if (on ("eqLsOn")) b[1].setLowShelf (rate, part ("eqLsFreq", 150.0f), part ("eqLsGain", 0.0f)); else b[1].setIdentity();
        if (on ("eqP1On")) b[2].setPeak (rate, part ("eqP1Freq", 500.0f), part ("eqP1Gain", 0.0f), part ("eqP1Q", 1.0f)); else b[2].setIdentity();
        if (on ("eqP2On")) b[3].setPeak (rate, part ("eqP2Freq", 3000.0f), part ("eqP2Gain", 0.0f), part ("eqP2Q", 1.0f)); else b[3].setIdentity();
        if (on ("eqHsOn")) b[4].setHighShelf (rate, part ("eqHsFreq", 8000.0f), part ("eqHsGain", 0.0f)); else b[4].setIdentity();
        if (on ("eqLpOn")) b[5].setLowPass (rate, part ("highCut", 20000.0f), 0.707); else b[5].setIdentity();
        double sum = 0.0;
        for (auto& q : b) sum += q.magnitudeDb (f, rate);
        return sum;
    }

    const juce::Colour reverbEqColours[] { juce::Colour (0xffff5c5c), juce::Colour (0xffff9a3c), juce::Colour (0xff3ddc6a),
                                           juce::Colour (0xff4f9bff), juce::Colour (0xffb07bff), juce::Colour (0xffff7ad0) };

    void paintStudioReverb (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float heard)
    {
        const bool details = valueOf (info, v, "view") > 0.5f;
        const R screen (0, 0, bounds.getWidth(), 420);
        const bool glass = theme::glassSkin();
        if (glass)
        {
            // Liquid Glass: a glass card, the cloud on a dark glass screen
            g.fillAll (theme::console);
            look::drawGlassPanel (g, bounds.toFloat().reduced (4.0f), 16.0f);
            g.setColour (juce::Colour (0xff0c0d10));
            g.fillRoundedRectangle (RF (24.0f, 54.0f, (float) bounds.getWidth() - 48.0f, 300.0f), 14.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (RF (24.0f, 54.0f, (float) bounds.getWidth() - 48.0f, 300.0f), 14.0f, 1.0f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26282c), 0.0f, 0.0f, juce::Colour (0xff17181b), 0.0f, (float) screen.getBottom(), false));
            g.fillRect (screen);
        }
        const RF plot (60.0f, 66.0f, (float) bounds.getWidth() - 120.0f, 236.0f);
        const auto xOf = [plot] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        g.setFont (juce::FontOptions (11.0f));
        for (const auto f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (xOf (f), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) - 18.0f, plot.getBottom() + 4.0f, 36.0f, 14.0f),
                        juce::Justification::centred, false);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);

        if (! details)
        {
            // The cloud: how long each frequency rings (its decay ratio), as coloured points, bass red to highs violet
            const auto decay = juce::jlimit (0.1f, 20.0f, valueOf (info, v, "decay", 2.0f));
            const auto size = valueOf (info, v, "size", 50.0f) * 0.01f;
            const auto level = 0.3f + 0.35f * std::min (1.0f, std::log (1.0f + decay) / std::log (21.0f)) + 0.1f * size;
            juce::Random random (42);
            // (Liquid Glass: the particles drift up and fade like a reverb's tail, and glitter as they go)
            const auto now = glass ? (float) (juce::Time::getMillisecondCounterHiRes() * 0.001) : 0.0f;
            const auto speed = 0.35f / std::max (0.3f, std::sqrt (decay));
            // (Liquid Glass: fewer, livelier particles, and none at all in silence)
            const int count = glass ? (heard > 0.002f ? 2800 : 0) : 6000;
            for (int i = 0; i < count; ++i)
            {
                const auto t = random.nextFloat();
                const auto f = 20.0 * std::pow (1000.0, (double) t);
                const auto top = juce::jlimit (0.03f, 0.95f, level * (float) reverbRatioAt (info, v, f) * 0.8f);
                auto y = std::pow (random.nextFloat(), 0.8f) * top;
                const auto seed = random.nextFloat();
                // (Liquid Glass: only while sound goes through: as many, and as high, as it is loud)
                if (glass && seed > heard * 1.15f)
                    continue;
                auto alpha = 0.7f * (1.0f - y / top) + 0.15f;
                auto size = 2.0f;
                auto x = plot.getX() + t * plot.getWidth();
                if (glass)
                {
                    // each rises from the floor to its height again and again, swaying a little
                    const auto phase = std::fmod (seed + now * speed * (0.6f + seed), 1.0f);
                    y = phase * top * juce::jlimit (0.25f, 1.0f, heard * 1.3f);
                    x += 3.0f * std::sin (now * 1.3f + seed * 40.0f);
                    alpha = 0.75f * (1.0f - phase) + 0.1f;
                    // a glint now and then
                    const auto glint = std::pow (0.5f + 0.5f * std::sin (now * (3.0f + 5.0f * seed) + seed * 97.0f), 24.0f);
                    // (louder: more of them glint, and brighter)
                    const auto sparkle = glint * juce::jlimit (0.3f, 1.0f, heard * 1.4f);
                    alpha = juce::jmin (1.0f, alpha * juce::jlimit (0.4f, 1.0f, heard * 1.5f) + sparkle * 0.9f);
                    size = 2.0f + sparkle * 2.6f;
                    if (sparkle > 0.15f)
                    {
                        // a soft glow round a glinting one
                        const auto glowSize = size * 4.0f;
                        const juce::Point<float> at (x + 1.0f, plot.getBottom() - y * plot.getHeight() + 1.0f);
                        const auto hue = juce::Colour::fromHSV (juce::jmap (t, 0.0f, 0.82f), 0.5f, 1.0f, 1.0f);
                        g.setGradientFill (juce::ColourGradient (hue.withAlpha (sparkle * 0.45f), at, hue.withAlpha (0.0f), at.translated (glowSize * 0.5f, 0.0f), true));
                        g.fillEllipse (juce::Rectangle<float> (glowSize, glowSize).withCentre (at));
                    }
                }
                g.setColour (juce::Colour::fromHSV (juce::jmap (t, 0.0f, 0.82f), glass ? 0.7f : 0.85f, 1.0f, alpha));
                g.fillEllipse (x - size * 0.5f + 1.0f, plot.getBottom() - y * plot.getHeight() - size * 0.5f + 1.0f, size, size);
            }
            // The damping EQ: ratio up the side (1.0 in the middle), its four points
            const auto yOfRatio = [plot] (double r) { return plot.getBottom() - (float) ((r - 0.1) / 1.8) * plot.getHeight(); };
            juce::Path curve;
            for (int x = 0; x <= (int) plot.getWidth(); x += 3)
            {
                const auto f = 20.0 * std::pow (1000.0, x / (double) plot.getWidth());
                const auto y = yOfRatio (reverbRatioAt (info, v, f));
                if (x == 0) curve.startNewSubPath (plot.getX(), y); else curve.lineTo (plot.getX() + (float) x, y);
            }
            g.setColour (juce::Colour (0xffff6a3d));
            g.strokePath (curve, juce::PathStrokeType (2.2f));
            const char* bands[][2] { { "dLsFreq", "dLsRatio" }, { "dP1Freq", "dP1Ratio" }, { "dP2Freq", "dP2Ratio" }, { "dHsFreq", "dHsRatio" } };
            for (const auto& b : bands)
            {
                const auto x = xOf (valueOf (info, v, b[0], 1000.0f)), y = yOfRatio (valueOf (info, v, b[1], 1.0f));
                g.setColour (juce::Colour (0xffff6a3d));
                g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.0f);
            }
            title (g, R ((int) plot.getX(), 316, 220, 18), "DAMPING EQ", juce::Colours::white.withAlpha (0.45f), 12.0f, juce::Justification::centredLeft);
        }
        else
        {
            // The output EQ: dB up the side, each band's point in its colour
            const auto eqOn = valueOf (info, v, "eqOn") > 0.5f;
            const auto yOfDb = [plot] (double db) { return plot.getCentreY() - (float) (db / 24.0) * plot.getHeight() * 0.5f; };
            juce::Path curve;
            for (int x = 0; x <= (int) plot.getWidth(); x += 2)
            {
                const auto f = 20.0 * std::pow (1000.0, x / (double) plot.getWidth());
                const auto y = juce::jlimit (plot.getY(), plot.getBottom(), yOfDb (reverbEqAt (info, v, f)));
                if (x == 0) curve.startNewSubPath (plot.getX(), y); else curve.lineTo (plot.getX() + (float) x, y);
            }
            g.setColour (juce::Colour (0xffe4eef8).withAlpha (eqOn ? 1.0f : 0.3f));
            g.strokePath (curve, juce::PathStrokeType (1.8f));
            const struct { const char* freq; const char* gain; const char* on; } bands[] {
                { "lowCut", nullptr, "eqHpOn" }, { "eqLsFreq", "eqLsGain", "eqLsOn" }, { "eqP1Freq", "eqP1Gain", "eqP1On" },
                { "eqP2Freq", "eqP2Gain", "eqP2On" }, { "eqHsFreq", "eqHsGain", "eqHsOn" }, { "highCut", nullptr, "eqLpOn" } };
            for (size_t k = 0; k < 6; ++k)
            {
                const auto on = eqOn && valueOf (info, v, bands[k].on, 1.0f) > 0.5f;
                const auto x = xOf (juce::jlimit (20.0f, 20000.0f, valueOf (info, v, bands[k].freq, 1000.0f)));
                const auto y = bands[k].gain != nullptr ? yOfDb (valueOf (info, v, bands[k].gain)) : plot.getCentreY();
                g.setColour (reverbEqColours[k].withAlpha (on ? 1.0f : 0.35f));
                g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            }
        }

        // The light panel below
        const R panel (0, 420, bounds.getWidth(), bounds.getHeight() - 420);
        if (glass)
        {
            look::drawGlassPanel (g, panel.toFloat().reduced (20.0f, 10.0f), 14.0f, true);
            return;
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe1e1e1), 0.0f, (float) panel.getY(), juce::Colour (0xffcfcfcf), 0.0f, (float) panel.getBottom(), false));
        g.fillRect (panel);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (835, panel.getY() + 22, 1, panel.getHeight() - 44);
    }

    //==============================================================================
    // Pitch Correction (as Logic's): the scale on a keyboard, a correction meter, Settings and Tuning
    //==============================================================================
    void paintPitchCorrection (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float cents)
    {
        const bool glass = theme::glassSkin();
        if (glass)
        {
            // Liquid Glass: glass sections (scale, keyboard, correction; settings, tuning)
            g.fillAll (theme::console);
            look::drawGlassPanel (g, bounds.toFloat().reduced (6.0f), 16.0f);
            for (const auto r : { RF (20.0f, 18.0f, 210.0f, 342.0f), RF (238.0f, 18.0f, 504.0f, 342.0f), RF (752.0f, 18.0f, 170.0f, 342.0f),
                                  RF (20.0f, 370.0f, 548.0f, 212.0f), RF (592.0f, 370.0f, 330.0f, 212.0f) })
                look::drawGlassPanel (g, r, 12.0f, true);
        }
        else
        {
            g.setColour (juce::Colour (0xff191d25));
            g.fillRect (bounds.withHeight (360));
            g.setColour (juce::Colour (0xff222b38));
            g.fillRect (bounds.withTrimmedTop (360));
        }
        const auto blue = glass ? theme::accent : juce::Colour (0xff4fb3e0);

        // The keyboard: the notes of the scale lit
        const auto key = juce::roundToInt (valueOf (info, v, "key"));
        const auto* mask = pitchScaleMask (juce::roundToInt (valueOf (info, v, "scale", 1.0f)));
        // (in the scale and not left out by Edit Scale; Bypass Notes marked in orange)
        const auto inScale = [&] (int pc)
        {
            return mask[((pc - key) % 12 + 12) % 12] == 1 && valueOf (info, v, ("n" + juce::String (pc + 1)).toRawUTF8(), 1.0f) > 0.5f;
        };
        const auto bypassed = [&] (int pc) { return valueOf (info, v, ("bp" + juce::String (pc + 1)).toRawUTF8()) > 0.5f; };
        const RF keys (250.0f, 120.0f, 480.0f, 200.0f);
        const auto whiteW = keys.getWidth() / 7.0f;
        static const int whites[] { 0, 2, 4, 5, 7, 9, 11 }, blacks[] { 1, 3, 6, 8, 10 };
        static const float blackAt[] { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        g.setFont (juce::FontOptions (14.0f));
        for (int w = 0; w < 7; ++w)
        {
            auto r = RF (keys.getX() + whiteW * (float) w, keys.getY(), whiteW - 3.0f, keys.getHeight());
            if (glass)
            {
                // a glass key: the scale's lighter, the others faint; lit in the accent along its top
                const auto lit = inScale (whites[w]);
                const auto light = theme::glassLight();
                g.setGradientFill (juce::ColourGradient ((light ? juce::Colours::white : juce::Colours::white.withAlpha (lit ? 0.24f : 0.06f)), 0.0f, r.getY(),
                                                         (light ? juce::Colour (0xffe9ecf2) : juce::Colours::white.withAlpha (lit ? 0.09f : 0.02f)), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 10.0f);
                if (lit)
                {
                    g.setColour (blue.withAlpha (0.9f));
                    g.fillRoundedRectangle (r.withHeight (4.0f).reduced (8.0f, 0.0f).translated (0.0f, 3.0f), 2.0f);
                }
                g.setColour ((light ? juce::Colours::black : juce::Colours::white).withAlpha (0.16f));
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
                if (bypassed (whites[w]))
                {
                    g.setColour (juce::Colour (0xffff453a));
                    g.drawRoundedRectangle (r.reduced (2.0f), 10.0f, 2.5f);
                }
                g.setColour (theme::text.withAlpha (lit ? 1.0f : 0.45f));
                g.drawText (names[whites[w]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
                continue;
            }
            g.setColour (inScale (whites[w]) ? blue : juce::Colour (0xff2c3644));
            g.fillRoundedRectangle (r, 5.0f);
            if (bypassed (whites[w]))
            {
                g.setColour (juce::Colour (0xffff9a3c));
                g.drawRoundedRectangle (r.reduced (2.0f), 5.0f, 3.0f);
            }
            g.setColour (inScale (whites[w]) ? juce::Colour (0xff0e2433) : juce::Colours::white.withAlpha (0.5f));
            g.drawText (names[whites[w]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
        }
        for (int b = 0; b < 5; ++b)
        {
            auto r = RF (keys.getX() + whiteW * blackAt[b] - whiteW * 0.33f - 1.5f, keys.getY(), whiteW * 0.66f, keys.getHeight() * 0.58f);
            g.setColour (juce::Colour (0xff111419));
            g.fillRoundedRectangle (r.expanded (2.0f, 0.0f), 5.0f);
            if (glass)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2c31), 0.0f, r.getY(), juce::Colour (0xff141518), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 8.0f);
                if (inScale (blacks[b]))
                {
                    g.setColour (blue.withAlpha (0.9f));
                    g.fillRoundedRectangle (r.withHeight (4.0f).reduced (6.0f, 0.0f).translated (0.0f, 3.0f), 2.0f);
                }
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
                if (bypassed (blacks[b]))
                {
                    g.setColour (juce::Colour (0xffff453a));
                    g.drawRoundedRectangle (r.reduced (2.0f), 8.0f, 2.5f);
                }
                g.setColour (juce::Colours::white.withAlpha (inScale (blacks[b]) ? 0.9f : 0.4f));
                g.drawText (names[blacks[b]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
                continue;
            }
            g.setColour (inScale (blacks[b]) ? blue : juce::Colour (0xff222a35));
            g.fillRoundedRectangle (r, 5.0f);
            if (bypassed (blacks[b]))
            {
                g.setColour (juce::Colour (0xffff9a3c));
                g.drawRoundedRectangle (r.reduced (2.0f), 5.0f, 3.0f);
            }
            g.setColour (inScale (blacks[b]) ? juce::Colour (0xff0e2433) : juce::Colours::white.withAlpha (0.5f));
            g.drawText (names[blacks[b]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
        }

        // The correction meter: how far the voice is being moved now
        const RF meter (848.0f, 90.0f, 8.0f, 230.0f);
        g.setColour (glass ? juce::Colours::black.withAlpha (0.45f) : juce::Colour (0xff2c3644));
        g.fillRoundedRectangle (meter, glass ? 4.0f : 3.0f);
        const auto yOf = [meter] (float c) { return meter.getCentreY() - juce::jlimit (-100.0f, 100.0f, c) / 100.0f * meter.getHeight() * 0.5f; };
        g.setColour (blue);
        const auto y0 = yOf (0.0f), y1 = yOf (cents);
        g.fillRect (meter.getX(), std::min (y0, y1), meter.getWidth(), std::max (2.0f, std::abs (y1 - y0)));
        g.setFont (juce::FontOptions (13.0f));
        const auto ink = glass ? theme::text : juce::Colours::white;
        for (const auto c : { 100, 50, 0, -50, -100 })
        {
            g.setColour (ink.withAlpha (c == 0 ? 0.9f : 0.55f));
            g.drawText ((c > 0 ? "+ " : c < 0 ? "- " : "") + juce::String (std::abs (c)), RF (790.0f, yOf ((float) c) - 8.0f, 48.0f, 16.0f), juce::Justification::right, false);
        }
        title (g, R (780, 30, 140, 20), "Correction", ink.withAlpha (0.6f), 15.0f);
        title (g, R (780, 52, 140, 22), juce::String (juce::roundToInt (cents)) + " Cent", blue, 17.0f);

        title (g, R (40, 380, 300, 30), "Settings", ink.withAlpha (0.9f), 21.0f, juce::Justification::centredLeft);
        title (g, R (610, 380, 300, 30), "Tuning", ink.withAlpha (0.9f), 21.0f, juce::Justification::centredLeft);
        if (! glass)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRect (580, 390, 1, 190);
        }
    }


    //==============================================================================
    // The standard Logic look, laid out from the effect's own sections: a blue panel, cyan section
    // titles, dark knobs with green rings and green values; a dark display on the right (curves,
    // echoes, decay, LFO), a gain-reduction meter on the left for dynamics.
    //==============================================================================
    const auto modernBlueTop = juce::Colour (0xff35506f), modernBlueBottom = juce::Colour (0xff223851);
    const auto modernCyan = juce::Colour (0xffa9dcf2), modernGreen = juce::Colour (0xff8fe39a);

    Kind kindForChoice (const ParamInfo& p)
    {
        if (p.choices.size() == 2 && (p.choices[0] == "Off" || p.choices[0] == "Normal"))
            return Kind::toggle;
        return p.choices.size() <= 4 ? Kind::segmented : Kind::dropdown;
    }

    Design makeModern (const EffectInfo& info)
    {
        Design d;
        d.glassFace = true;
        d.knobs = KnobStyle::dark;
        d.label = juce::Colour (0xffb9c8d6);
        d.value = modernGreen;
        d.accent = modernGreen;
        d.track = juce::Colour (0xff17263a);
        d.showScale = true;

        const bool display = effectface::hasDisplay (info);
        // (a gain reduction meter for every compressor, gate, limiter and de-esser, the older ones too)
        const bool meter = info.category == "Dynamics" || info.type == "silverComp" || info.type == "silverGate" || info.type == "deesserLegacy";
        d.live = meter;
        const auto sections = effectface::resolveSections (info);

        // Cells: a knob is 116 wide; a row of buttons as wide as its settings; a menu 170
        constexpr int cellH = 172, titleH = 52, gap = 14;
        const auto widthOf = [&info] (int paramIndex)
        {
            const auto& p = info.params[(size_t) paramIndex];
            if (! p.isChoice()) return 116;
            const auto k = kindForChoice (p);
            if (k == Kind::segmented)
            {
                int w = 0;
                for (const auto& c : p.choices) w += std::max (62, (int) c.length() * 9 + 22);
                return std::max (116, w + 10);
            }
            return k == Kind::toggle ? 116 : 180;
        };

        const auto left = meter ? 170 : 0;
        constexpr int displayW = 400;
        const auto maxRow = 1480 - left - (display ? displayW : 0);
        struct Box { juce::String title; R area; };
        std::vector<Box> boxes;
        int x = left + gap, y = 10, rowH = 0, widest = 0;
        for (const auto& [section, indices] : sections)
        {
            // A section's cells, wrapping inside it when there are many
            std::vector<int> widths;
            for (const auto i : indices)
                widths.push_back (widthOf (i));
            if (widths.empty())
                break;
            int lineW = 0, w = 0, lines = 1;
            for (const auto cw : widths)
            {
                if (lineW + cw > maxRow - 2 * gap && lineW > 0) { w = std::max (w, lineW); lineW = 0; ++lines; }
                lineW += cw;
            }
            w = std::max (w, lineW) + 20;
            const auto h = titleH + lines * cellH + 8;
            if (x > left + gap && x + w > left + maxRow)
            {
                x = left + gap;
                y += rowH + 6;
                rowH = 0;
            }
            boxes.push_back ({ section.title, R (x, y, w, h) });
            // Place the cells
            int cx = x + 10, cy = y + titleH;
            for (size_t k = 0; k < widths.size(); ++k)
            {
                if (cx + widths[k] > x + w - 10 + 1)
                {
                    cx = x + 10;
                    cy += cellH;
                }
                const auto& p = info.params[(size_t) indices[k]];
                const auto cell = R (cx, cy, widths[k], cellH);
                if (! p.isChoice())
                    d.controls.push_back ({ p.id, Kind::knob, cell.reduced (8, 0).withTrimmedBottom (6) });
                else
                {
                    const auto kind = kindForChoice (p);
                    if (kind == Kind::toggle)
                        d.controls.push_back ({ p.id, kind, R (cell.getCentreX() - 34, cell.getY() + 70, 68, 30) });
                    else if (kind == Kind::segmented)
                        d.controls.push_back ({ p.id, kind, R (cell.getX() + 4, cell.getY() + 70, cell.getWidth() - 8, 32) });
                    else
                        d.controls.push_back ({ p.id, kind, R (cell.getX() + 6, cell.getY() + 70, cell.getWidth() - 12, 30) });
                }
                cx += widths[k];
            }
            x += w + gap;
            widest = std::max (widest, x);
            rowH = std::max (rowH, h);
        }
        const auto contentH = y + rowH + 10;
        R displayArea;
        d.width = widest + (display ? displayW : 0);
        d.width = std::max (d.width, left + 400);
        d.height = std::max (contentH, display ? 250 : 0);
        if (display)
            displayArea = R (d.width - displayW + 12, 14, displayW - 26, d.height - 28);

        d.paint = [boxes, displayArea, meter, left] (juce::Graphics& g, R bounds, const EffectInfo& fx, const float* v, float live)
        {
            const bool glass = theme::glassSkin();
            if (glass)
            {
                // Liquid Glass: the window's ground, a glass card, a recessed glass section per group
                g.fillAll (theme::console);
                look::drawGlassPanel (g, bounds.toFloat().reduced (6.0f), 16.0f);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (modernBlueTop, 0.0f, 0.0f, modernBlueBottom, 0.0f, (float) bounds.getBottom(), false));
                g.fillRect (bounds);
            }
            for (size_t b = 0; b < boxes.size(); ++b)
            {
                const auto& box = boxes[b];
                if (glass)
                {
                    look::drawGlassPanel (g, box.area.toFloat().reduced (4.0f, 4.0f), 12.0f, true);
                    if (box.title.isNotEmpty())
                        title (g, box.area.withHeight (40), box.title.toUpperCase(), theme::text.withAlpha (0.8f), 15.0f);
                    continue;
                }
                if (box.title.isNotEmpty())
                    title (g, box.area.withHeight (46), box.title.toUpperCase(), modernCyan, 24.0f);
                // A faint line between neighbouring sections (as Logic's)
                if (b + 1 < boxes.size() && boxes[b + 1].area.getY() == box.area.getY())
                {
                    const auto lx = box.area.getRight() + 7;
                    g.setColour (juce::Colours::black.withAlpha (0.22f));
                    g.fillRect (lx, box.area.getY() + 10, 1, box.area.getHeight() - 20);
                    g.setColour (juce::Colours::white.withAlpha (0.05f));
                    g.fillRect (lx + 1, box.area.getY() + 10, 1, box.area.getHeight() - 20);
                }
            }
            if (meter)
            {
                // The gain reduction meter on the left, as Logic's dynamics show it
                const auto panel = R (0, 0, left, bounds.getHeight());
                if (glass)
                    look::drawGlassPanel (g, panel.toFloat().reduced (10.0f, 10.0f), 12.0f, true);
                else
                {
                    g.setColour (juce::Colour (0xff141c27));
                    g.fillRect (panel);
                }
                title (g, R (0, 12, left, 24), "REDUCTION", juce::Colour (0xffb9c8d6), 15.0f);
                const auto bar = RF ((float) left * 0.5f - 7.0f, 52.0f, 14.0f, (float) bounds.getHeight() - 80.0f);
                g.setColour (juce::Colour (0xff222d3b));
                g.fillRect (bar);
                g.setFont (juce::FontOptions (11.0f));
                for (int db = 0; db <= 30; db += 3)
                {
                    const auto y = bar.getY() + bar.getHeight() * (float) db / 30.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.4f));
                    g.drawText (juce::String (db), RF (bar.getX() - 40.0f, y - 6.0f, 32.0f, 12.0f), juce::Justification::right, false);
                    g.fillRect (bar.getRight() + 3.0f, y, 5.0f, 1.0f);
                }
                const auto depth = juce::jlimit (0.0f, 1.0f, live / 30.0f);
                g.setColour (modernGreen);
                g.fillRect (bar.withHeight (bar.getHeight() * depth));
                title (g, R (0, bounds.getHeight() - 26, left, 20), juce::String (live, 1) + " dB", modernGreen, 14.0f);
            }
            if (! displayArea.isEmpty())
            {
                if (glass)
                {
                    g.setColour (theme::lcd);
                    g.fillRoundedRectangle (displayArea.expanded (6, 8).toFloat(), 12.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.08f));
                    g.drawRoundedRectangle (displayArea.expanded (6, 8).toFloat(), 12.0f, 1.0f);
                }
                else
                {
                    g.setColour (juce::Colour (0xff0f1722));
                    g.fillRect (displayArea.expanded (10, 14).withRight (bounds.getRight()));
                }
                effectface::drawDisplay (g, displayArea.toFloat(), fx, v, glass ? juce::Colour (0xffff9f0a) : modernGreen);
                // (the analyzer over the display's screen, faint enough to read the curve through)
                const auto screen = displayArea.toFloat().reduced (10.0f);
                drawSpectrum (g, screen, [screen] (double f) { return screen.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * screen.getWidth(); },
                              juce::Colour (0xff7fb4e6));
            }
        };
        // The analyzer for the effects that shape the spectrum
        // (where the display is a frequency response, so the spectrum lines up with it)
        d.analyzer = info.response != nullptr && ! displayArea.isEmpty();
        // (the Multipressor's crossovers can be dragged in its display)
        if (info.type == "multiband" && ! displayArea.isEmpty())
            d.graphs.push_back ({ displayArea.reduced (10, 8), { { "x1", {}, {}, {} }, { "x2", {}, {}, {} }, { "x3", {}, {}, {} } }, 0 });
        return d;
    }

    //==============================================================================
    // Hardware-style faces (as Logic's vintage plug-ins)
    //==============================================================================
    void wood (juce::Graphics& g, R area, juce::Colour base)
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), 0.0f, (float) area.getY(), base.darker (0.25f), 0.0f, (float) area.getBottom(), false));
        g.fillRect (area);
        juce::Random random (7);
        for (int k = 0; k < 90; ++k)
        {
            const auto y = (float) area.getY() + random.nextFloat() * (float) area.getHeight();
            juce::Path grain;
            grain.startNewSubPath ((float) area.getX(), y);
            for (int x = area.getX(); x <= area.getRight(); x += 40)
                grain.lineTo ((float) x, y + std::sin ((float) x * 0.01f + (float) k) * 3.0f);
            g.setColour ((random.nextBool() ? juce::Colours::black : juce::Colours::white).withAlpha (0.04f + random.nextFloat() * 0.05f));
            g.strokePath (grain, juce::PathStrokeType (0.8f + random.nextFloat() * 1.5f));
        }
    }

    void textured (juce::Graphics& g, R area, juce::Colour top, juce::Colour bottom, float grain)
    {
        g.setGradientFill (juce::ColourGradient (top, 0.0f, (float) area.getY(), bottom, 0.0f, (float) area.getBottom(), false));
        g.fillRect (area);
        juce::Random random (3);
        for (int k = 0; k < area.getWidth() * area.getHeight() / 40; ++k)
        {
            g.setColour ((random.nextBool() ? juce::Colours::black : juce::Colours::white).withAlpha (grain * random.nextFloat()));
            g.fillRect (area.getX() + random.nextInt (area.getWidth()), area.getY() + random.nextInt (area.getHeight()), 1, 1);
        }
    }

    void screws (juce::Graphics& g, R area, float inset)
    {
        for (const auto& p : { juce::Point<float> ((float) area.getX() + inset, (float) area.getY() + inset), juce::Point<float> ((float) area.getRight() - inset, (float) area.getY() + inset),
                               juce::Point<float> ((float) area.getX() + inset, (float) area.getBottom() - inset), juce::Point<float> ((float) area.getRight() - inset, (float) area.getBottom() - inset) })
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d7d9), p.x - 5.0f, p.y - 5.0f, juce::Colour (0xff5c6064), p.x + 5.0f, p.y + 5.0f, false));
            g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
            g.setColour (juce::Colour (0xff3a3d40));
            g.drawLine (p.x - 4.0f, p.y + 4.0f, p.x + 4.0f, p.y - 4.0f, 1.6f);
        }
    }

    void filterIcon (juce::Graphics& g, RF box, int kind, juce::Colour colour)
    {
        g.setColour (juce::Colour (0xff1a1c1f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        const auto c = box.getCentre();
        juce::Path p;
        switch (kind)
        {
            case 0: p.startNewSubPath (c.x - 14, c.y + 8); p.quadraticTo (c.x - 4, c.y - 8, c.x + 14, c.y - 8); break;
            case 1: p.startNewSubPath (c.x - 14, c.y - 7); p.lineTo (c.x - 2, c.y - 7); p.lineTo (c.x + 2, c.y); p.lineTo (c.x + 14, c.y);
                    p.startNewSubPath (c.x - 2, c.y + 7); p.lineTo (c.x + 2, c.y); break;
            case 2: p.startNewSubPath (c.x - 14, c.y); p.lineTo (c.x - 7, c.y); p.lineTo (c.x, c.y - 8); p.lineTo (c.x + 7, c.y); p.lineTo (c.x + 14, c.y);
                    p.startNewSubPath (c.x - 7, c.y); p.lineTo (c.x, c.y + 8); p.lineTo (c.x + 7, c.y); break;
            default: p.startNewSubPath (c.x - 14, c.y); p.lineTo (c.x - 2, c.y); p.lineTo (c.x + 2, c.y - 7); p.lineTo (c.x + 14, c.y - 7);
                     p.startNewSubPath (c.x - 2, c.y); p.lineTo (c.x + 2, c.y + 7); break;
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    void inLamp (juce::Graphics& g, float x, float y, juce::Colour text)
    {
        title (g, R ((int) x - 30, (int) y - 34, 60, 20), "IN", text, 15.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff6b5e), x - 4.0f, y - 4.0f, juce::Colour (0xffb0120a), x + 8.0f, y + 8.0f, true));
        g.fillEllipse (x - 11.0f, y - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colour (0xffff2a1a).withAlpha (0.25f));
        g.fillEllipse (x - 18.0f, y - 18.0f, 36.0f, 36.0f);
        g.setColour (juce::Colour (0xffb9b5ae));
        g.fillRect (x - 11.0f, y + 26.0f, 22.0f, 34.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRect (x - 11.0f, y + 26.0f, 22.0f, 34.0f, 1.0f);
    }

    void paintConsoleEq (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        textured (g, b, juce::Colour (0xff35373b), juce::Colour (0xff222427), 0.05f);
        const juce::Colour blue (0xff3f86d9);
        const int xs[] { 245, 440, 635, 830 };
        for (int k = 0; k < 4; ++k)
            filterIcon (g, RF ((float) xs[k] - 28.0f, 14.0f, 56.0f, 34.0f), k, blue.brighter (0.4f));
        inLamp (g, 70.0f, 150.0f, juce::Colours::white.withAlpha (0.85f));
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.fillRect (975, 30, 1, b.getHeight() - 60);
        title (g, R (980, 22, 190, 30), "OUTPUT", juce::Colours::white.withAlpha (0.9f), 18.0f);
    }

    void paintGraphicEq (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        textured (g, b, juce::Colour (0xff2f3134), juce::Colour (0xff1d1f21), 0.05f);
        inLamp (g, 64.0f, 185.0f, juce::Colours::white.withAlpha (0.85f));
        // The blue level lines across the faders
        const float top = 120.0f, bottom = 330.0f;
        for (int k = 0; k <= 4; ++k)
        {
            const auto y = top + (bottom - top) * (float) k / 4.0f;
            g.setColour (juce::Colour (0xff2f8ac0).withAlpha (k == 2 ? 1.0f : 0.85f));
            g.fillRect (190.0f, y - (k % 2 == 0 ? 2.0f : 1.0f), 790.0f, k % 2 == 0 ? 4.0f : 2.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("+12", RF (120.0f, top - 9.0f, 50.0f, 18.0f), juce::Justification::right, false);
        g.drawText ("0", RF (120.0f, (top + bottom) * 0.5f - 9.0f, 50.0f, 18.0f), juce::Justification::right, false);
        g.drawText ("-12", RF (120.0f, bottom - 9.0f, 50.0f, 18.0f), juce::Justification::right, false);
        g.setColour (juce::Colour (0xff2f8ac0));
        g.fillRect (1040, 30, 1, b.getHeight() - 60);
        title (g, R (1045, 22, 200, 30), "OUTPUT", juce::Colours::white.withAlpha (0.8f), 18.0f);
    }

    void paintTubeEq (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        textured (g, b, juce::Colour (0xff35605f), juce::Colour (0xff254544), 0.06f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (b.withTrimmedLeft (b.getWidth() - 200).withTrimmedTop (16).withTrimmedBottom (16).withWidth (1));
        inLamp (g, 50.0f, 120.0f, juce::Colours::white.withAlpha (0.8f));
        title (g, R (b.getWidth() - 200, 20, 200, 24), "OUTPUT", juce::Colours::white.withAlpha (0.8f), 15.0f);
        screws (g, b, 14.0f);
    }

    /** Liquid Glass for the hardware-style faces: the window's ground, a glass card tinted in the unit's colour. */
    void glassFace (juce::Graphics& g, R b, juce::Colour tint, const juce::String& name = {})
    {
        g.fillAll (theme::console);
        const auto card = b.toFloat().reduced (8.0f);
        look::drawGlassPanel (g, card, 18.0f);
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (theme::glassLight() ? 0.16f : 0.2f), 0.0f, card.getY(),
                                                 tint.withAlpha (0.0f), 0.0f, card.getY() + card.getHeight() * 0.8f, false));
        g.fillRoundedRectangle (card.reduced (1.0f), 17.0f);
        if (name.isNotEmpty())
        {
            g.setColour (tint);
            g.fillEllipse (card.getX() + 20.0f, card.getY() + 20.0f, 9.0f, 9.0f);
            title (g, R ((int) card.getX() + 36, (int) card.getY() + 10, 400, 30), name, theme::text, 17.0f, juce::Justification::centredLeft);
        }
    }

    void paintDistortion (juce::Graphics& g, R b, const EffectInfo& fx, const float*, float)
    {
        if (theme::glassSkin())
        {
            const bool fuzz = fx.type == "fuzz";
            glassFace (g, b, fuzz ? juce::Colour (0xffbf5af2) : juce::Colour (0xffff6a1a), fuzz ? "FUZZ" : "DISTORTION");
            look::drawGlassPanel (g, b.reduced (34, 56).withTrimmedTop (20).toFloat(), 14.0f, true);
            return;
        }
        wood (g, b, juce::Colour (0xff7a3a1c));
        const auto panel = b.reduced (34, 46);
        textured (g, panel, juce::Colour (0xff2a2a2a), juce::Colour (0xff141414), 0.12f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRect (panel, 3);
    }

    void paintAutoWah (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        if (theme::glassSkin())
        {
            glassFace (g, b, juce::Colour (0xffff9f0a), "AUTO WAH");
            look::drawGlassPanel (g, RF (40.0f, 90.0f, 560.0f, (float) b.getHeight() - 116.0f), 14.0f, true);
            look::drawGlassPanel (g, RF (690.0f, 90.0f, 120.0f, (float) b.getHeight() - 116.0f), 14.0f, true);
            return;
        }
        wood (g, b, juce::Colour (0xffc97a3d));
        const auto inner = b.reduced (18).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRoundedRectangle (inner, 14.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRoundedRectangle (inner.translated (0.0f, 2.0f), 14.0f, 1.0f);
        // The power lamp and the name, burned into the wood
        g.setColour (juce::Colour (0xffffb020));
        g.fillEllipse (44.0f, 34.0f, 30.0f, 30.0f);
        g.setColour (juce::Colour (0xff6a3a10));
        g.drawEllipse (44.0f, 34.0f, 30.0f, 30.0f, 2.0f);
        title (g, R (86, 30, 300, 38), "AUTO WAH", juce::Colour (0xff3a1e08), 30.0f, juce::Justification::centredLeft);
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillRect (b.getWidth() - 180, 90, 1, b.getHeight() - 130);
    }

    void tweed (juce::Graphics& g, RF area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8b47a), area.getX(), area.getY(), juce::Colour (0xffa9864f), area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, 14.0f);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());
        for (float d = -area.getHeight(); d < area.getWidth(); d += 6.0f)
        {
            g.setColour (juce::Colour (0xff5a3d1a).withAlpha (0.18f));
            g.drawLine (area.getX() + d, area.getY(), area.getX() + d + area.getHeight(), area.getBottom(), 1.4f);
        }
    }

    /** Amp Designer in glass: each amp family its own colour, the cabinet's grille and the mic shown in theirs. */
    juce::Colour ampTint (int model)
    {
        if (model <= 2)  return juce::Colour (0xffd8b47a);   // tweed
        if (model == 3 || model == 5) return juce::Colour (0xff5e8bd6);   // black panel
        if (model == 4)  return juce::Colour (0xffc9ced6);   // silver panel
        if (model == 6 || model == 10) return juce::Colour (0xffb07a4a);   // brown
        if (model <= 14) return juce::Colour (0xffe8b84a);   // British: gold
        if (model <= 16) return juce::Colour (0xffff9f0a);   // sunshine
        if (model <= 18) return juce::Colour (0xffff453a);   // stadium
        if (model <= 20) return juce::Colour (0xffbf5af2);   // high gain
        if (model == 21) return juce::Colour (0xff5ac8fa);   // studio
        if (model == 22) return juce::Colour (0xff30d158);   // boutique retro
        return juce::Colour (0xff66d4cf);                     // pawnshop
    }

    void paintGuitarAmp (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float)
    {
        if (theme::glassSkin())
        {
            const auto model = juce::roundToInt (valueOf (fx, v, "model"));
            const auto cab = juce::roundToInt (valueOf (fx, v, "cab"));
            const auto mic = juce::roundToInt (valueOf (fx, v, "micType"));
            const auto tint = ampTint (model);
            const auto cabTint = ampTint (cab);
            const juce::Colour micTints[] { juce::Colour (0xffc9ced6), juce::Colour (0xffffd60a), juce::Colour (0xff64d2ff), juce::Colour (0xffff9f0a),
                                            juce::Colour (0xff30d158), juce::Colour (0xffbf5af2), juce::Colour (0xffff453a) };
            const auto micTint = micTints[juce::jlimit (0, 6, mic)];
            glassFace (g, b, tint);
            const RF amp (30.0f, 24.0f, (float) b.getWidth() - 60.0f, 420.0f);
            // the control strip: a glass panel lit in the amp's colour
            const auto strip = RF (amp.getX() + 10.0f, amp.getY() + 16.0f, amp.getWidth() - 20.0f, 210.0f);
            look::drawGlassPanel (g, strip, 14.0f, true);
            g.setColour (tint.withAlpha (0.9f));
            g.fillRoundedRectangle (strip.getX() + 18.0f, strip.getY() + 6.0f, strip.getWidth() - 36.0f, 3.0f, 1.5f);
            // the grille: glass with the cabinet's cloth lines in its colour, the amp's name on a glass plate
            const auto grille = RF (amp.getX() + 10.0f, strip.getBottom() + 22.0f, amp.getWidth() - 20.0f, amp.getBottom() - strip.getBottom() - 30.0f);
            look::drawGlassPanel (g, grille, 14.0f, true);
            {
                juce::Graphics::ScopedSaveState keep (g);
                juce::Path clip;
                clip.addRoundedRectangle (grille.reduced (2.0f), 12.0f);
                g.reduceClipRegion (clip);
                for (float y = grille.getY() + 4.0f; y < grille.getBottom(); y += 5.0f)
                {
                    g.setColour (cabTint.withAlpha (0.13f));
                    g.fillRect (grille.getX(), y, grille.getWidth(), 1.5f);
                }
                g.setGradientFill (juce::ColourGradient (cabTint.withAlpha (0.18f), grille.getCentreX(), grille.getCentreY(),
                                                         cabTint.withAlpha (0.0f), grille.getRight(), grille.getBottom(), true));
                g.fillRect (grille);
            }
            const auto plate = RF (grille.getCentreX() - 150.0f, grille.getY() + 14.0f, 300.0f, 34.0f);
            look::drawKey (g, plate, look::Key::dark, true, tint.withAlpha (0.85f), false, false);
            if (const auto* p = [&fx] { for (const auto& q : fx.params) if (q.id == "model") return &q; return (const ParamInfo*) nullptr; }())
                title (g, plate.toNearestInt(), p->text ((float) model), tint.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff1a1205) : juce::Colours::white, 16.0f);
            // the strip's section names
            const auto ink = theme::text.withAlpha (0.8f);
            title (g, R (450, 228, 170, 16), "REVERB", ink, 12.0f);
            title (g, R (630, 228, 170, 16), "TREM / VIB", ink, 12.0f);
            title (g, R (630, 186, 85, 14), "DEPTH", ink, 10.5f);
            title (g, R (720, 186, 85, 14), "SPEED", ink, 10.5f);
            title (g, R (490, 200, 90, 14), "LEVEL", ink, 10.5f);
            // the model bar: glass, the mic's colour along its top
            const auto bar = RF (20.0f, (float) b.getHeight() - 104.0f, (float) b.getWidth() - 40.0f, 90.0f);
            look::drawGlassPanel (g, bar, 14.0f, true);
            g.setColour (micTint.withAlpha (0.9f));
            g.fillRoundedRectangle (bar.getX() + 18.0f, bar.getY() + 5.0f, bar.getWidth() - 36.0f, 3.0f, 1.5f);
            return;
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2c), 0.0f, 0.0f, juce::Colour (0xff0c0c0d), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const RF amp (30.0f, 24.0f, (float) b.getWidth() - 60.0f, 420.0f);
        tweed (g, amp);
        // The chrome control strip
        const auto strip = RF (amp.getX() + 26.0f, amp.getY() + 16.0f, amp.getWidth() - 52.0f, 210.0f);
        juce::ColourGradient chrome (juce::Colour (0xffe9ebee), 0.0f, strip.getY(), juce::Colour (0xff8a9096), 0.0f, strip.getBottom(), false);
        chrome.addColour (0.5, juce::Colour (0xffc5c9ce));
        g.setGradientFill (chrome);
        g.fillRoundedRectangle (strip, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (strip, 8.0f, 1.5f);
        // The grille cloth and the name plate
        const auto grille = RF (amp.getX() + 26.0f, strip.getBottom() + 22.0f, amp.getWidth() - 52.0f, amp.getBottom() - strip.getBottom() - 48.0f);
        g.setColour (juce::Colour (0xff3a2a1e));
        g.fillRoundedRectangle (grille, 6.0f);
        for (float y = grille.getY() + 3.0f; y < grille.getBottom(); y += 4.0f)
        {
            g.setColour (juce::Colour (0xff8a6a4a).withAlpha (0.25f));
            g.fillRect (grille.getX(), y, grille.getWidth(), 1.0f);
        }
        const auto plate = RF (grille.getCentreX() - 80.0f, grille.getY() - 14.0f, 160.0f, 30.0f);
        g.setColour (juce::Colour (0xff6b5536));
        g.fillRoundedRectangle (plate, 6.0f);
        title (g, plate.toNearestInt(), "Audio Tech", juce::Colour (0xfff0dfc0), 18.0f);
        // The strip's section names
        const auto ink = juce::Colour (0xff2a2a2a);
        title (g, R (450, 228, 170, 16), "REVERB", ink, 12.0f);
        title (g, R (630, 228, 170, 16), "TREM / VIB", ink, 12.0f);
        title (g, R (630, 186, 85, 14), "DEPTH", ink, 10.5f);
        title (g, R (720, 186, 85, 14), "SPEED", ink, 10.5f);
        title (g, R (490, 200, 90, 14), "LEVEL", ink, 10.5f);
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillRect (440, 50, 1, 180);
        g.fillRect (810, 50, 1, 180);
        // The model bar below
        g.setColour (juce::Colour (0xff1a1a1c));
        g.fillRect (0, b.getHeight() - 110, b.getWidth(), 110);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (0, b.getHeight() - 110, b.getWidth(), 1);
    }

    void paintBassAmp (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2022), 0.0f, 0.0f, juce::Colour (0xff09090a), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        // A black head, a brushed aluminium front
        const RF head (40.0f, 20.0f, (float) b.getWidth() - 80.0f, 420.0f);
        g.setColour (juce::Colour (0xff161718));
        g.fillRoundedRectangle (head, 10.0f);
        title (g, R (70, 26, 300, 34), "BASS AMP", juce::Colour (0xffd8dadc), 22.0f, juce::Justification::centredLeft);
        const auto front = RF (head.getX() + 20.0f, head.getY() + 70.0f, head.getWidth() - 40.0f, head.getHeight() - 90.0f);
        juce::ColourGradient metal (juce::Colour (0xffd9dcdf), 0.0f, front.getY(), juce::Colour (0xff8e9398), 0.0f, front.getBottom(), false);
        metal.addColour (0.55, juce::Colour (0xffbfc3c7));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (front, 6.0f);
        juce::Random random (5);
        for (float y = front.getY() + 1.0f; y < front.getBottom(); y += 1.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f * random.nextFloat()));
            g.fillRect (front.getX(), y, front.getWidth(), 1.0f);
        }
        screws (g, front.toNearestInt(), 18.0f);
        g.setColour (juce::Colour (0xff1a1a1c));
        g.fillRect (0, b.getHeight() - 110, b.getWidth(), 110);
    }

    void paintRotary (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        wood (g, b, juce::Colour (0xff6a2c14));
        const auto panel = b.reduced (20).withTrimmedBottom (40);
        textured (g, panel, juce::Colour (0xff2a2a2b), juce::Colour (0xff18181a), 0.08f);
        g.setColour (juce::Colours::black);
        g.drawRect (panel, 2);
        title (g, R (40, 34, 600, 22), "-  MIC PROCESSING  -", juce::Colour (0xffd8c9a6), 13.0f);
        title (g, R (40, 214, 600, 22), "-  MOTOR  -", juce::Colour (0xffd8c9a6), 13.0f);
        // The cabinet on the right
        const RF cab ((float) b.getWidth() - 360.0f, 50.0f, 300.0f, 320.0f);
        g.setColour (juce::Colour (0xff121214));
        g.fillRect (cab.expanded (10.0f, 20.0f));
        wood (g, cab.reduced (40.0f, 30.0f).toNearestInt(), juce::Colour (0xff8a4a22));
        const auto box = cab.reduced (40.0f, 30.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int k = 0; k < 5; ++k)
            g.fillRoundedRectangle (box.getX() + 30.0f, box.getY() + 40.0f + (float) k * 22.0f, box.getWidth() - 60.0f, 8.0f, 3.0f);
        g.drawRect (box, 2.0f);
        const auto speed = juce::roundToInt (valueOf (info, v, "speed"));
        title (g, R ((int) cab.getX(), (int) cab.getBottom() + 8, (int) cab.getWidth(), 22), speed == 2 ? "FAST" : speed == 1 ? "BRAKE" : "SLOW", juce::Colour (0xffd8c9a6), 14.0f);
    }

    void paintSaturator (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35245a), 0.0f, 0.0f, juce::Colour (0xff1c1433), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        // The glowing waveforms either side of the Drive knob: more drive, more harmonics
        const auto drive = valueOf (info, v, "drive", 25.0f) * 0.01f;
        const auto mid = 210.0f;
        for (int side = 0; side < 2; ++side)
        {
            const auto x0 = side == 0 ? 20.0f : (float) b.getCentreX() + 90.0f, x1 = side == 0 ? (float) b.getCentreX() - 90.0f : (float) b.getWidth() - 20.0f;
            for (int layer = 0; layer < 2; ++layer)
            {
                juce::Path p;
                juce::Random random (11 + layer + side * 3);
                for (float x = x0; x <= x1; x += 2.0f)
                {
                    const auto t = (x - x0) / (x1 - x0);
                    const auto env = std::sin (t * juce::MathConstants<float>::pi) * (side == 0 ? t : 1.0f - t) * 2.0f;
                    auto y = std::sin (x * 0.21f + (float) layer) * 0.6f + (random.nextFloat() - 0.5f) * (0.6f + drive);
                    y = std::tanh (y * (1.0f + drive * 3.0f));
                    const auto py = mid - y * env * 46.0f;
                    if (x == x0) p.startNewSubPath (x, py); else p.lineTo (x, py);
                }
                g.setColour ((layer == 0 ? juce::Colour (0xffff8a3c) : juce::Colour (0xff9a6bff)).withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (30, 300, b.getWidth() - 60, 1);
        g.fillRect (30, 456, b.getWidth() - 60, 1);
        g.fillRect (500, 470, 1, 110);
    }

    void paintStepFilter (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff632a2c), 0.0f, 0.0f, juce::Colour (0xff3d191b), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const auto orange = juce::Colour (0xffffa040);
        title (g, R (24, 10, 300, 26), "STEP MODULATOR", juce::Colour (0xffe9c8b8), 16.0f, juce::Justification::centredLeft);
        title (g, R (24, 236, 300, 26), "FILTER", orange, 16.0f, juce::Justification::centredLeft);
        // The step pads' panel
        g.setColour (juce::Colour (0xff1c1a24));
        g.fillRect (20, 44, b.getWidth() - 40, 182);
        g.setFont (juce::FontOptions (13.0f));
        for (int k = 0; k < 8; ++k)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (juce::String (k + 1), R (40 + k * 92, 204, 80, 18), juce::Justification::centred, false);
        }
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (20, 232, b.getWidth() - 40, 1);
    }

    void paintVocalTransformer (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        textured (g, b, juce::Colour (0xff98aebf), juce::Colour (0xff6d8396), 0.08f);
        // Two recessed plates, as the original's moulded panel
        for (const auto x : { 30, 300 })
        {
            const auto plate = RF ((float) x, 26.0f, 240.0f, 300.0f);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (plate.translated (0.0f, 3.0f), 26.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8ea4b6), 0.0f, plate.getY(), juce::Colour (0xff768c9f), 0.0f, plate.getBottom(), false));
            g.fillRoundedRectangle (plate, 26.0f);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawRoundedRectangle (plate.reduced (1.0f), 26.0f, 1.0f);
        }
        g.setColour (juce::Colour (0xff3d4b58));
        g.fillRect (570, 26, 2, 300);
        g.fillRect (20, 345, b.getWidth() - 40, 2);
        title (g, R (24, 350, 300, 20), "EXTENDED", juce::Colour (0xff1d2833), 12.0f, juce::Justification::centredLeft);
    }

    void paintVocoder (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        textured (g, b, juce::Colour (0xff9db2c2), juce::Colour (0xff6f8597), 0.07f);
        // The filter bank display: green screen, a yellow line per band between the lowest and highest
        const RF screen (330.0f, 40.0f, 340.0f, 150.0f);
        g.setColour (juce::Colour (0xff1a3a2a));
        g.fillRoundedRectangle (screen.expanded (6.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2bd27a), 0.0f, screen.getY(), juce::Colour (0xff0e6a3a), 0.0f, screen.getBottom(), false));
        g.fillRoundedRectangle (screen, 6.0f);
        static const int counts[] { 8, 12, 16, 24, 32 };
        const auto bands = counts[juce::jlimit (0, 4, juce::roundToInt (valueOf (info, v, "bands", 2.0f)))];
        const auto low = valueOf (info, v, "low", 120.0f), high = valueOf (info, v, "high", 8000.0f);
        const auto xOf = [screen] (double f) { return screen.getX() + 20.0f + (float) (std::log (f / 50.0) / std::log (300.0)) * (screen.getWidth() - 40.0f); };
        g.setColour (juce::Colour (0xffffe14a));
        for (int k = 0; k < bands; ++k)
        {
            const auto f = low * std::pow (high / low, (k + 0.5f) / (float) bands);
            g.fillRect (xOf (f), screen.getY() + 30.0f, 1.5f, screen.getHeight() - 44.0f);
        }
        g.setColour (juce::Colour (0xffd8ffe8));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (juce::String ((int) low) + "Hz", RF (screen.getX() + 8.0f, screen.getY() + 6.0f, 100.0f, 18.0f), juce::Justification::left, false);
        g.drawText (juce::String ((int) high) + "Hz", RF (screen.getRight() - 108.0f, screen.getY() + 6.0f, 100.0f, 18.0f), juce::Justification::right, false);
        g.setColour (juce::Colour (0xff2b3a48));
        g.fillRect (700, 20, 2, b.getHeight() - 40);
        title (g, R (712, 14, 160, 20), "OUTPUT", juce::Colour (0xff1c2a36), 14.0f);
    }

    //==============================================================================
    // Multipressor (as Logic's): the band display on top, each band's settings in its own column below
    //==============================================================================
    const juce::Colour multiColours[] { juce::Colour (0xffff9a3c), juce::Colour (0xffe7d94a), juce::Colour (0xff3ddc6a), juce::Colour (0xff4f9bff) };
    const char* multiRows[] { "Comp Threshold", "Comp Ratio", "Exp Threshold", "Exp Ratio", "Exp Reduction", "Peak/RMS", "Attack", "Release", "Gain Make-up" };

    float meterAt (int index, float fallback);

    /** The Multipressor's screen: where it sits, and the dB scale of the bands' levels (+-24 dB over its height). */
    const RF multiScreen (30.0f, 24.0f, 1000.0f, 220.0f);

    void paintMultipressor (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        const bool glass = theme::glassSkin();
        if (glass)
        {
            // Liquid Glass: a glass card (black glass in dark mode, white in light), the table on recessed glass
            g.fillAll (theme::console);
            look::drawGlassPanel (g, b.toFloat().reduced (4.0f), 16.0f);
            look::drawGlassPanel (g, RF (16.0f, 330.0f, (float) b.getWidth() - 32.0f, 9.0f * 30.0f + 16.0f), 12.0f, true);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3e57), 0.0f, 0.0f, juce::Colour (0xff1c2a3d), 0.0f, (float) b.getBottom(), false));
            g.fillRect (b);
        }

        // As Logic's: each band a column between its crossovers, its level a line you drag up and down, and under the
        // line, live, how far the band is being turned down - with the spectrum of what comes out behind it all
        const auto a = multiScreen;
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (a.expanded (10.0f, 8.0f), 6.0f);
        const auto xOf = [a] (double f) { return a.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * a.getWidth(); };
        const auto yOf = [a] (float db) { return a.getCentreY() - juce::jlimit (-24.0f, 24.0f, db) / 24.0f * a.getHeight() * 0.5f; };
        g.setFont (juce::FontOptions (10.5f));
        for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (xOf (f), a.getY(), 1.0f, a.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.32f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) + 3.0f, a.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::left, false);
        }
        for (const auto db : { -18.0f, -12.0f, -6.0f, 6.0f, 12.0f, 18.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (a.getX(), yOf (db), a.getWidth(), 1.0f);
            g.setColour ((glass ? theme::text : juce::Colours::white).withAlpha (0.3f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), RF (a.getX() - 28.0f, yOf (db) - 6.0f, 24.0f, 12.0f), juce::Justification::right, false);
        }
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillRect (a.getX(), yOf (0.0f), a.getWidth(), 1.0f);
        drawSpectrum (g, a, xOf, juce::Colour (0xff7fb4e6));

        const float edges[] { 20.0f, valueOf (info, v, "x1", 150.0f), valueOf (info, v, "x2", 1000.0f), valueOf (info, v, "x3", 5000.0f), 20000.0f };
        bool anySolo = false;
        for (int k = 0; k < 4; ++k)
            anySolo = anySolo || valueOf (info, v, ("b" + juce::String (k + 1) + "Solo").toRawUTF8()) > 0.5f;
        const char* bandNames[] { "LOW", "LOW MID", "HIGH MID", "HIGH" };
        for (int k = 0; k < 4; ++k)
        {
            const auto id = "b" + juce::String (k + 1);
            const auto on = valueOf (info, v, (id + "On").toRawUTF8(), 1.0f) > 0.5f;
            const auto heard = on && (! anySolo || valueOf (info, v, (id + "Solo").toRawUTF8()) > 0.5f);
            const auto colour = multiColours[k];
            const auto gainDb = valueOf (info, v, (id + "Gain").toRawUTF8(), 0.0f);
            const auto change = on ? meterAt (k, 0.0f) : 0.0f;   // dB down now
            const auto x0 = xOf (edges[k]) + (k > 0 ? 1.5f : 0.0f), x1 = xOf (edges[k + 1]) - (k < 3 ? 1.5f : 0.0f);
            const auto level = yOf (gainDb);
            // The band's area, from 0 dB to its level
            g.setColour (colour.withAlpha (heard ? 0.13f : 0.04f));
            g.fillRect (RF (x0, std::min (level, yOf (0.0f)), x1 - x0, std::abs (level - yOf (0.0f)) + 1.0f));
            // The gain change, hanging from the level line
            if (std::abs (change) > 0.05f)
            {
                const auto to = yOf (gainDb - change);
                const auto bar = RF (x0 + 6.0f, std::min (level, to), x1 - x0 - 12.0f, std::abs (to - level));
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (heard ? 0.75f : 0.25f), 0.0f, level, colour.withAlpha (heard ? 0.35f : 0.1f), 0.0f, to, false));
                g.fillRect (bar);
            }
            // The level line, with a grip in its middle
            g.setColour (colour.withAlpha (heard ? 1.0f : 0.35f));
            g.fillRect (RF (x0 + 2.0f, level - 1.5f, x1 - x0 - 4.0f, 3.0f));
            const auto mid = xOf (std::sqrt ((double) edges[k] * edges[k + 1]));
            g.fillRoundedRectangle (RF (mid - 14.0f, level - 4.0f, 28.0f, 8.0f), 3.0f);
            // Its name and what it's doing now
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (bandNames[k], RF (x0 + 6.0f, a.getY() + 2.0f, x1 - x0 - 12.0f, 14.0f), juce::Justification::left, false);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (juce::Colours::white.withAlpha (on ? 0.85f : 0.4f));
            const auto text = ! on ? juce::String ("off") : std::abs (change) < 0.05f ? juce::String ("0.0 dB") : juce::String (-change, 1) + " dB";
            g.drawText (text, RF (x0 + 6.0f, a.getY() + 16.0f, x1 - x0 - 12.0f, 13.0f), juce::Justification::left, false);
        }
        // The crossovers: lines you drag across, the frequency above
        for (int k = 1; k < 4; ++k)
        {
            const auto x = xOf (edges[k]);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (x - 1.0f, a.getY() - 4.0f, 2.0f, a.getHeight() + 8.0f);
            g.fillEllipse (x - 5.0f, a.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            const auto f = edges[k];
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz",
                        RF (x - 40.0f, a.getBottom() + 0.0f, 80.0f, 12.0f), juce::Justification::centred, false);
        }

        // In and out meters at the right
        const auto meterTop = 26.0f, meterHeight = 214.0f;
        const auto yDb = [&] (float db) { return meterTop + meterHeight * (1.0f - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f)); };
        g.setFont (juce::FontOptions (10.0f));
        for (const auto db : { 6, 0, -6, -12, -24, -36, -48, -60 })
        {
            g.setColour ((glass ? theme::text : juce::Colours::white).withAlpha (0.35f));
            g.drawText (juce::String (db), RF ((float) b.getWidth() - 62.0f, yDb ((float) db) - 6.0f, 18.0f, 12.0f), juce::Justification::centred, false);
        }
        const auto bar = [&] (float x, float db)
        {
            const RF track (x, meterTop, 7.0f, meterHeight);
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRect (track);
            const auto top = yDb (db);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff4b3a), 0.0f, yDb (3.0f), juce::Colour (0xff59d36b), 0.0f, yDb (-18.0f), false));
            g.fillRect (track.withTop (top));
        };
        bar ((float) b.getWidth() - 104.0f, meterAt (4, -120.0f));
        bar ((float) b.getWidth() - 95.0f, meterAt (5, -120.0f));
        bar ((float) b.getWidth() - 40.0f, meterAt (6, -120.0f));
        bar ((float) b.getWidth() - 31.0f, meterAt (7, -120.0f));
        title (g, R (b.getWidth() - 112, 4, 30, 18), "IN", (glass ? theme::text : juce::Colours::white).withAlpha (0.6f), 11.0f);
        title (g, R (b.getWidth() - 50, 4, 36, 18), "OUT", (glass ? theme::text : juce::Colours::white).withAlpha (0.6f), 11.0f);

        // Band titles, row names
        const char* names[] { "LOW", "LOW MID", "HIGH MID", "HIGH" };
        for (int k = 0; k < 4; ++k)
            title (g, R (190 + k * 240, 268, 220, 22), names[k], glass && theme::glassLight() ? multiColours[k].withBrightness (juce::jmin (multiColours[k].getBrightness(), 0.62f)) : multiColours[k], 16.0f);
        g.setFont (juce::FontOptions (13.0f));
        const auto ink = glass ? theme::text : juce::Colours::white;
        for (int row = 0; row < 9; ++row)
        {
            g.setColour ((glass && theme::glassLight() ? juce::Colours::black : juce::Colours::white).withAlpha (row % 2 == 0 ? 0.035f : 0.0f));
            g.fillRect (20, 338 + row * 30, b.getWidth() - 40, 30);
            g.setColour (ink.withAlpha (0.7f));
            g.drawText (multiRows[row], R (24, 338 + row * 30, 160, 30), juce::Justification::centredLeft, false);
        }
        g.setColour (ink.withAlpha (0.1f));
        g.fillRect (20, b.getHeight() - 58, b.getWidth() - 40, 1);
        title (g, R (24, b.getHeight() - 46, 160, 30), "OUTPUT", glass ? theme::text : juce::Colour (0xffa9dcf2), 15.0f, juce::Justification::centredLeft);
        title (g, R (330, b.getHeight() - 48, 100, 30), "Lookahead", ink.withAlpha (0.7f), 13.0f, juce::Justification::centredRight);
        title (g, R (690, b.getHeight() - 48, 100, 30), "Out", ink.withAlpha (0.7f), 13.0f, juce::Justification::centredRight);
    }

    //==============================================================================
    // Formant Filterbank (as EVOC 20 Filterbank): blue-grey metal, two rows of band faders
    //==============================================================================
    void paintFormantBank (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        textured (g, b, juce::Colour (0xff9db2c2), juce::Colour (0xff6f8597), 0.07f);
        const auto ink = juce::Colour (0xff1c2a36);
        title (g, R (20, 8, 400, 26), "FORMANT FILTER", ink, 17.0f, juce::Justification::centredLeft);
        for (const auto y : { 100, 270 })
        {
            g.setColour (juce::Colour (0xff1a2530));
            g.fillRoundedRectangle (RF (60.0f, (float) y - 6.0f, 900.0f, 160.0f), 8.0f);
        }
        title (g, R (20, 100, 40, 150), "A", juce::Colour (0xff59b8ff), 26.0f);
        title (g, R (20, 270, 40, 150), "B", juce::Colour (0xff6ef2a2), 26.0f);
        g.setColour (juce::Colour (0xff2b3a48));
        g.fillRect (990, 50, 2, 390);
        g.fillRect (20, 450, b.getWidth() - 40, 2);
        title (g, R (20, 458, 300, 22), "MODULATION", ink, 15.0f, juce::Justification::centredLeft);
        title (g, R (b.getWidth() - 330, 458, 300, 22), "OUTPUT", ink, 15.0f, juce::Justification::centredRight);
    }

    //==============================================================================
    // Space Designer (as Logic's): the impulse response with its envelopes over it, a parameter bar, the globals
    //==============================================================================
    void paintSpaceDesigner (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f47), 0.0f, 0.0f, juce::Colour (0xff23262b), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const RF screen (20.0f, 56.0f, (float) b.getWidth() - 40.0f, 250.0f);
        g.setColour (juce::Colour (0xff0d1013));
        g.fillRoundedRectangle (screen, 6.0f);
        const auto length = std::max (0.1f, valueOf (info, v, "length", 2.5f));
        const auto pre = valueOf (info, v, "predelay", 10.0f) * 0.001f;
        const auto total = pre + length;
        const auto xOf = [screen, total] (float t) { return screen.getX() + 10.0f + (screen.getWidth() - 20.0f) * t / total; };
        const auto mid = screen.getCentreY(), half = screen.getHeight() * 0.42f;
        // Time grid in seconds
        g.setFont (juce::FontOptions (11.0f));
        for (float t = 0.0f; t <= total; t += total > 6.0f ? 1.0f : 0.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (xOf (t), screen.getY(), 1.0f, screen.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String (t, 1) + "s", RF (xOf (t) + 2.0f, screen.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::left, false);
        }
        // The impulse response's shape (the synthesized one, or a stand-in for a sampled one)
        const auto volOn = valueOf (info, v, "volOn") > 0.5f;
        const auto attack = valueOf (info, v, "attack") * 0.001f, decay = std::max (0.05f, valueOf (info, v, "decay", 2.0f));
        const auto init = valueOf (info, v, "initLevel", 100.0f) * 0.01f, end = valueOf (info, v, "endLevel") * 0.01f;
        const auto volumeAt = [&] (float t)
        {
            if (! volOn) return 1.0f;
            if (t < attack) return init + (1.0f - init) * t / std::max (attack, 1.0e-4f);
            const auto u = std::min (1.0f, (t - attack) / decay);
            return 1.0f + (end - 1.0f) * u;
        };
        juce::Random random (9);
        juce::Path wave;
        for (int x = 0; x < (int) screen.getWidth() - 20; ++x)
        {
            const auto t = total * (float) x / (screen.getWidth() - 20.0f);
            if (t < pre) continue;
            const auto e = std::exp (-6.9f * (t - pre) / length) * volumeAt (t - pre);
            const auto h = e * half * (0.4f + 0.6f * random.nextFloat());
            wave.addRectangle (screen.getX() + 10.0f + (float) x, mid - h, 1.0f, h * 2.0f);
        }
        g.setColour (juce::Colour (0xff6f8fb4).withAlpha (0.8f));
        g.fillPath (wave);
        // The volume envelope (orange) and the filter envelope (blue)
        if (volOn)
        {
            juce::Path env;
            for (int x = 0; x <= 200; ++x)
            {
                const auto t = length * (float) x / 200.0f;
                const auto y = mid - half * volumeAt (t);
                if (x == 0) env.startNewSubPath (xOf (pre + t), y); else env.lineTo (xOf (pre + t), y);
            }
            g.setColour (juce::Colour (0xffff9a3c));
            g.strokePath (env, juce::PathStrokeType (2.0f));
        }
        if (valueOf (info, v, "filtOn") > 0.5f)
        {
            const auto fi = valueOf (info, v, "initFreq", 16000.0f), fb = valueOf (info, v, "breakFreq", 8000.0f), fe = valueOf (info, v, "endFreq", 1500.0f);
            const auto fa = valueOf (info, v, "filtAttack") * 0.001f, fd = std::max (0.05f, valueOf (info, v, "filtDecay", 2.0f));
            const auto yOfF = [&] (float f) { return mid + half - (float) (std::log (f / 50.0f) / std::log (400.0f)) * half * 2.0f; };
            juce::Path env;
            env.startNewSubPath (xOf (pre), yOfF (fi));
            env.lineTo (xOf (pre + fa), yOfF (fb));
            env.lineTo (xOf (pre + fa + fd), yOfF (fe));
            env.lineTo (xOf (total), yOfF (fe));
            g.setColour (juce::Colour (0xff4fb7ff));
            g.strokePath (env, juce::PathStrokeType (2.0f));
        }
        // The parameter bar and the globals panel
        // The Output EQ page: its curve over the impulse response (frequency across, +-24 dB)
        if (juce::roundToInt (valueOf (info, v, "view")) == 3)
        {
            const RF plot (30.0f, 66.0f, 1040.0f, 230.0f);
            const auto rate = 48000.0;
            const auto on = [&] (const char* id) { return valueOf (info, v, "eqOn") > 0.5f && valueOf (info, v, id, 1.0f) > 0.5f; };
            Biquad bands[6];
            if (on ("eqHpOn")) bands[0].setHighPass (rate, valueOf (info, v, "eqHpFreq", 80.0f), 0.707); else bands[0].setIdentity();
            if (on ("eqLsOn")) bands[1].setLowShelf (rate, valueOf (info, v, "eqLsFreq", 200.0f), valueOf (info, v, "eqLsGain")); else bands[1].setIdentity();
            if (on ("eqP1On")) bands[2].setPeak (rate, valueOf (info, v, "eqP1Freq", 500.0f), valueOf (info, v, "eqP1Gain"), valueOf (info, v, "eqP1Q", 1.0f)); else bands[2].setIdentity();
            if (on ("eqP2On")) bands[3].setPeak (rate, valueOf (info, v, "eqP2Freq", 3000.0f), valueOf (info, v, "eqP2Gain"), valueOf (info, v, "eqP2Q", 1.0f)); else bands[3].setIdentity();
            if (on ("eqHsOn")) bands[4].setHighShelf (rate, valueOf (info, v, "eqHsFreq", 8000.0f), valueOf (info, v, "eqHsGain")); else bands[4].setIdentity();
            if (on ("eqLpOn")) bands[5].setLowPass (rate, valueOf (info, v, "eqLpFreq", 12000.0f), 0.707); else bands[5].setIdentity();
            g.setColour (juce::Colour (0xff0d1013).withAlpha (0.75f));
            g.fillRect (plot);
            juce::Path curve;
            for (int x = 0; x <= (int) plot.getWidth(); x += 2)
            {
                const auto f = 20.0 * std::pow (1000.0, x / (double) plot.getWidth());
                double db = 0.0;
                for (auto& q : bands) db += q.magnitudeDb (f, rate);
                const auto y = juce::jlimit (plot.getY(), plot.getBottom(), plot.getCentreY() - (float) (db / 24.0) * plot.getHeight() * 0.5f);
                if (x == 0) curve.startNewSubPath (plot.getX(), y); else curve.lineTo (plot.getX() + (float) x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
            g.setColour (juce::Colour (0xffe4eef8));
            g.strokePath (curve, juce::PathStrokeType (1.8f));
        }
        g.setColour (juce::Colour (0xff1b1e22));
        g.fillRect (20, 314, b.getWidth() - 40, 76);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (20, 398, b.getWidth() - 40, 1);
        // The bar's field names, for the page showing
        const auto page = juce::roundToInt (valueOf (info, v, "view"));
        const auto names = [&] (std::initializer_list<std::pair<int, const char*>> list)
        {
            for (const auto& [x, text] : list)
                title (g, R (x, 318, 110, 18), text, juce::Colours::white.withAlpha (0.55f), 11.5f);
        };
        if (page == 0) names ({ { 105, "Init Level" }, { 210, "Attack" }, { 315, "Decay" }, { 420, "End Level" } });
        if (page == 1) names ({ { 95, "Res" }, { 178, "Init Freq" }, { 263, "Attack" }, { 348, "Break Level" }, { 430, "Decay" }, { 503, "End Freq" } });
        if (page == 2) names ({ { 110, "Init Density" }, { 225, "Ramp Time" }, { 340, "End Density" }, { 455, "Refl. Shape" } });
        const auto sampledMode = valueOf (info, v, "mode", 1.0f) < 0.5f;
        title (g, R (690, 16, 200, 26), sampledMode ? "SAMPLED IR" : "SYNTHESIZED IR", juce::Colours::white.withAlpha (0.4f), 12.0f, juce::Justification::centredLeft);
    }

    //==============================================================================
    // Pedalboard: six stomp boxes on a dark board, each coloured by its kind
    //==============================================================================
    void paintPedalboard (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        textured (g, b, juce::Colour (0xff2a2b2e), juce::Colour (0xff151517), 0.06f);
        for (int s = 0; s < 6; ++s)
        {
            const auto n = juce::String (s + 1);
            const auto type = juce::roundToInt (valueOf (info, v, ("p" + n + "Type").toRawUTF8()));
            const auto on = valueOf (info, v, ("p" + n + "On").toRawUTF8()) > 0.5f;
            const RF box (20.0f + (float) s * 200.0f, 20.0f, 180.0f, 360.0f);
            const auto colour = juce::Colour::fromHSV ((float) ((type * 37) % 100) / 100.0f, 0.55f, 0.75f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (box.translated (0.0f, 4.0f), 12.0f);
            g.setGradientFill (juce::ColourGradient (colour.brighter (0.15f), 0.0f, box.getY(), colour.darker (0.35f), 0.0f, box.getBottom(), false));
            g.fillRoundedRectangle (box, 12.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (box, 12.0f, 1.5f);
            // The knob names for this pedal
            const auto names = pedalboardKnobNames (type);
            const RF knobs[] { { box.getX() + 10.0f, 70.0f, 80.0f, 16.0f }, { box.getX() + 90.0f, 70.0f, 80.0f, 16.0f }, { box.getX() + 50.0f, 165.0f, 80.0f, 16.0f } };
            for (int k = 0; k < 3; ++k)
                title (g, knobs[k].toNearestInt(), names[(size_t) k].toUpperCase(), juce::Colours::black.withAlpha (0.75f), 11.5f);
            // The lamp
            g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff4a1512));
            g.fillEllipse (box.getCentreX() - 6.0f, 262.0f, 12.0f, 12.0f);
            // The footswitch housing
            g.setColour (juce::Colour (0xff2b2c2f));
            g.fillRoundedRectangle (box.getX() + 40.0f, 290.0f, 100.0f, 70.0f, 8.0f);
        }
    }

    //==============================================================================
    // Meters: dark glass screens, green to amber to red
    //==============================================================================
    float meterAt (int index, float fallback = -100.0f)
    {
        int count = 0;
        const auto* m = paintingMeters (count);
        return m != nullptr && index < count ? m[index] : fallback;
    }

    void meterBar (juce::Graphics& g, RF bar, float db, float holdDb, bool vertical = true)
    {
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (bar, 3.0f);
        const auto t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        const auto filled = vertical ? bar.withTop (bar.getBottom() - bar.getHeight() * t) : bar.withWidth (bar.getWidth() * t);
        juce::ColourGradient grad (juce::Colour (0xff3ddc6a), vertical ? 0.0f : bar.getX(), vertical ? bar.getBottom() : 0.0f,
                                   juce::Colour (0xffff3b30), vertical ? 0.0f : bar.getRight(), vertical ? bar.getY() : 0.0f, false);
        grad.addColour (0.8, juce::Colour (0xffe7d94a));
        g.setGradientFill (grad);
        g.fillRect (filled);
        if (holdDb > -99.0f)
        {
            const auto h = juce::jlimit (0.0f, 1.0f, (holdDb + 60.0f) / 66.0f);
            g.setColour (juce::Colours::white);
            if (vertical) g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * h, bar.getWidth(), 2.0f);
            else g.fillRect (bar.getX() + bar.getWidth() * h, bar.getY(), 2.0f, bar.getHeight());
        }
    }

    void meterBackground (juce::Graphics& g, R b)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3138), 0.0f, 0.0f, juce::Colour (0xff1a1d22), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
    }

    juce::String lufsText (float v) { return v <= -69.9f ? juce::String ("--") : juce::String (v, 1); }

    void paintLevelMeter (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        meterBackground (g, b);
        const char* names[] { "L", "R" };
        for (int c = 0; c < 2; ++c)
        {
            const RF bar (60.0f + (float) c * 70.0f, 40.0f, 40.0f, 300.0f);
            meterBar (g, bar, meterAt (c), meterAt (4 + c));
            // (RMS as a line across)
            const auto rms = juce::jlimit (0.0f, 1.0f, (meterAt (2 + c) + 60.0f) / 66.0f);
            g.setColour (juce::Colour (0xff4fb7ff));
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * rms - 1.0f, bar.getWidth(), 3.0f);
            title (g, R ((int) bar.getX(), 344, 40, 18), names[c], juce::Colours::white.withAlpha (0.7f), 14.0f);
            title (g, R ((int) bar.getX() - 10, 14, 60, 20), juce::String (meterAt (4 + c), 1), juce::Colour (0xffe7d94a), 13.0f);
        }
        g.setFont (juce::FontOptions (11.0f));
        for (const auto db : { 0, -6, -12, -24, -40, -60 })
        {
            const auto y = 340.0f - 300.0f * ((float) db + 60.0f) / 66.0f;
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (juce::String (db), RF (10.0f, y - 7.0f, 40.0f, 14.0f), juce::Justification::right, false);
        }
    }

    void paintLoudnessMeter (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        meterBackground (g, b);
        const char* names[] { "M", "S", "I" };
        for (int k = 0; k < 3; ++k)
        {
            const RF bar (50.0f + (float) k * 80.0f, 60.0f, 46.0f, 300.0f);
            // (LUFS: -60 at the bottom, 0 at the top)
            meterBar (g, bar, meterAt (k) + 6.0f, -100.0f);
            title (g, R ((int) bar.getX() - 10, 364, 66, 20), names[k], juce::Colours::white.withAlpha (0.8f), 15.0f);
            title (g, R ((int) bar.getX() - 17, 28, 80, 24), lufsText (meterAt (k)), juce::Colour (0xff8fe39a), 16.0f);
        }
        title (g, R (300, 60, 200, 22), "LU RANGE", juce::Colours::white.withAlpha (0.6f), 13.0f);
        title (g, R (300, 84, 200, 34), juce::String (meterAt (3, 0.0f), 1) + " LU", juce::Colours::white, 24.0f);
        title (g, R (300, 150, 200, 22), "TRUE PEAK", juce::Colours::white.withAlpha (0.6f), 13.0f);
        const auto tp = meterAt (4);
        title (g, R (300, 174, 200, 34), (tp <= -99.0f ? juce::String ("--") : juce::String (tp, 1)) + " dBTP", tp > -1.0f ? juce::Colour (0xffff3b30) : juce::Colours::white, 24.0f);
        title (g, R (300, 240, 200, 22), "LUFS", juce::Colours::white.withAlpha (0.4f), 13.0f);
    }

    void paintCorrelation (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        meterBackground (g, b);
        const RF bar (40.0f, 60.0f, (float) b.getWidth() - 80.0f, 30.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (bar, 4.0f);
        const auto c = meterAt (0, 0.0f);
        const auto x = bar.getCentreX() + c * bar.getWidth() * 0.5f;
        g.setColour (c < 0.0f ? juce::Colour (0xffff3b30) : juce::Colour (0xff3ddc6a));
        g.fillRect (std::min (x, bar.getCentreX()), bar.getY() + 4.0f, std::abs (x - bar.getCentreX()), bar.getHeight() - 8.0f);
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1.5f, bar.getY(), 3.0f, bar.getHeight());
        for (const auto [v, t] : { std::pair { -1.0f, "-1" }, std::pair { 0.0f, "0" }, std::pair { 1.0f, "+1" } })
            title (g, R ((int) (bar.getCentreX() + v * bar.getWidth() * 0.5f) - 20, 96, 40, 18), t, juce::Colours::white.withAlpha (0.6f), 13.0f);
        title (g, R (0, 20, b.getWidth(), 26), "CORRELATION " + juce::String (c, 2), juce::Colours::white.withAlpha (0.8f), 15.0f);
    }

    void paintMultiMeter (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        meterBackground (g, b);
        const auto goniometer = valueOf (info, v, "view") > 0.5f;
        const RF screen (20.0f, 50.0f, (float) b.getWidth() - 140.0f, 300.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (screen, 6.0f);
        if (! goniometer)
        {
            // Equal octaves across (20 - 200 Hz as wide as 2 - 20 kHz); 0 to -60 dB up the side; the tilt about 1 kHz
            const auto slope = bands::tiltSlope (juce::roundToInt (valueOf (info, v, "tilt", 0.0f)));
            const auto plot = screen.reduced (34.0f, 10.0f).withTrimmedBottom (18.0f).withTrimmedRight (-24.0f);
            const auto xOf = [plot] (double hz) { return plot.getX() + (float) bands::position (hz) * plot.getWidth(); };
            const auto yOf = [plot] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };
            g.setFont (juce::FontOptions (10.0f));
            for (int db = 0; db >= -60; db -= 10)
            {
                g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.18f : 0.07f));
                g.fillRect (plot.getX(), yOf ((float) db), plot.getWidth(), 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText (juce::String (db), RF (screen.getX() + 2.0f, yOf ((float) db) - 7.0f, 28.0f, 14.0f), juce::Justification::centredRight, false);
            }
            for (const auto hz : { 20.0, 40.0, 80.0, 160.0, 315.0, 630.0, 1250.0, 2500.0, 5000.0, 10000.0, 20000.0 })
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRect (xOf (hz), plot.getY(), 1.0f, plot.getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, hz < 10000.0 && std::fmod (hz, 1000.0) > 0.0 ? 1 : 0) + "k" : juce::String ((int) hz),
                            RF (xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
            }
            for (int k = 0; k < 31; ++k)
            {
                const auto x0 = xOf (bands::edge (k, 31)) + 1.0f, x1 = xOf (bands::edge (k + 1, 31)) - 1.0f;
                const auto db = meterAt (k, -120.0f) + (float) bands::displayOffsetDb (bands::centre (k, 31), slope);
                const auto top = yOf (db);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd84a), 0.0f, plot.getY(), juce::Colour (0xff3ddc6a), 0.0f, plot.getBottom(), false));
                g.fillRect (RF::leftTopRightBottom (x0, top, x1, plot.getBottom()));
            }
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawText ("Tilt " + bands::tiltNames()[juce::roundToInt (valueOf (info, v, "tilt", 0.0f))], RF (plot.getRight() - 120.0f, plot.getY(), 116.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
        else
        {
            // The goniometer: side across, mid up; a mono signal is a vertical line
            const auto c = screen.getCentre();
            const auto r = screen.getHeight() * 0.45f;
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.drawLine (c.x - r, c.y, c.x + r, c.y);
            g.drawLine (c.x, c.y - r, c.x, c.y + r);
            g.setColour (juce::Colour (0xff3ddc6a).withAlpha (0.8f));
            for (int p = 0; p < 256; ++p)
            {
                const auto sx = meterAt (34 + p * 2, 0.0f), my = meterAt (34 + p * 2 + 1, 0.0f);
                g.fillRect (c.x + juce::jlimit (-1.0f, 1.0f, sx) * r - 1.0f, c.y - juce::jlimit (-1.0f, 1.0f, my) * r - 1.0f, 2.0f, 2.0f);
            }
        }
        // Peak levels and correlation on the right
        meterBar (g, RF ((float) b.getWidth() - 100.0f, 50.0f, 30.0f, 300.0f), meterAt (32), -100.0f);
        meterBar (g, RF ((float) b.getWidth() - 60.0f, 50.0f, 30.0f, 300.0f), meterAt (33), -100.0f);
        const auto corr = meterAt (31, 0.0f);
        title (g, R (20, 360, 300, 22), "Correlation " + juce::String (corr, 2), corr < 0.0f ? juce::Colour (0xffff3b30) : juce::Colours::white.withAlpha (0.7f), 14.0f, juce::Justification::centredLeft);
    }

    void paintTuner (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        meterBackground (g, b);
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto found = meterAt (3, 0.0f) > 0.5f;
        const auto note = juce::roundToInt (meterAt (1, 69.0f));
        const auto cents = meterAt (2, 0.0f);
        const auto inTune = std::abs (cents) < 3.0f;
        title (g, R (0, 30, b.getWidth(), 80), found ? juce::String (names[(note % 12 + 12) % 12]) + juce::String (note / 12 - 1) : juce::String ("--"),
               found && inTune ? juce::Colour (0xff3ddc6a) : juce::Colours::white, 60.0f);
        // The needle's arc: -50 to +50 cents
        const juce::Point<float> pivot ((float) b.getCentreX(), 330.0f);
        const auto radius = 180.0f;
        for (int c = -50; c <= 50; c += 10)
        {
            const auto a = (float) c / 50.0f * 0.9f;
            g.setColour (juce::Colours::white.withAlpha (c == 0 ? 0.9f : 0.4f));
            g.drawLine ({ pivot.getPointOnCircumference (radius, a), pivot.getPointOnCircumference (radius + (c == 0 ? 20.0f : 12.0f), a) }, c == 0 ? 2.5f : 1.5f);
        }
        if (found)
        {
            const auto a = juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * 0.9f;
            g.setColour (inTune ? juce::Colour (0xff3ddc6a) : juce::Colour (0xffff9a3c));
            g.drawLine ({ pivot, pivot.getPointOnCircumference (radius + 10.0f, a) }, 3.0f);
            title (g, R (0, 340, b.getWidth(), 24), juce::String (cents, 1) + " cents   " + juce::String (meterAt (0, 0.0f), 1) + " Hz", juce::Colours::white.withAlpha (0.7f), 15.0f);
        }
    }

    void paintBpmCounter (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        meterBackground (g, b);
        const auto bpm = meterAt (0, 0.0f);
        title (g, R (0, 30, b.getWidth(), 70), bpm > 1.0f ? juce::String (bpm, 1) : juce::String ("--"), juce::Colour (0xff8fe39a), 56.0f);
        title (g, R (0, 100, b.getWidth(), 22), "BPM", juce::Colours::white.withAlpha (0.6f), 15.0f);
    }

    void paintMasteringAssistant (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0.0f, 0.0f, juce::Colour (0xff15171b), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const auto purple = juce::Colour (0xffb07bff), amber = juce::Colour (0xffffb84d);

        // The display: the spectrum, the Auto EQ's curve (eight octave bands) and the custom EQ's
        const RF screen (20.0f, 60.0f, 700.0f, 240.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (screen, 6.0f);
        const auto inner = screen.reduced (6.0f, 4.0f);
        const auto amount = valueOf (info, v, "autoEq", 50.0f) * 0.01f;
        const auto xOf = [inner] (double f) { return inner.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * inner.getWidth(); };
        const auto yOf = [inner] (double db) { return inner.getCentreY() - (float) (juce::jlimit (-9.0, 9.0, db) / 9.0) * inner.getHeight() * 0.5f; };
        g.setFont (juce::FontOptions (10.5f));
        for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (xOf (f), inner.getY(), 1.0f, inner.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.32f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) + 3.0f, inner.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::left, false);
        }
        for (const auto db : { -6.0, -3.0, 3.0, 6.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (inner.getX(), yOf (db), inner.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), RF (inner.getX() + 2.0f, yOf (db) - 12.0f, 24.0f, 11.0f), juce::Justification::left, false);
        }
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (inner.getX(), yOf (0.0), inner.getWidth(), 1.0f);
        drawSpectrum (g, inner, xOf, juce::Colour (0xff7fb4e6));
        const auto curveOf = [&] (std::function<double (int)> gainOfBand)
        {
            Biquad bands[8];
            for (int k = 0; k < 8; ++k)
                bands[k].setPeak (48000.0, 62.5 * std::pow (2.0, k), gainOfBand (k), 1.0);
            juce::Path curve;
            for (int x = 0; x <= (int) inner.getWidth(); x += 2)
            {
                const auto f = 20.0 * std::pow (1000.0, x / (double) inner.getWidth());
                double db = 0.0;
                for (auto& q : bands) db += q.magnitudeDb (f, 48000.0);
                if (x == 0) curve.startNewSubPath (inner.getX(), yOf (db)); else curve.lineTo (inner.getX() + (float) x, yOf (db));
            }
            return curve;
        };
        if (valueOf (info, v, "customOn") > 0.5f)
        {
            const auto low = valueOf (info, v, "customLow"), mid = valueOf (info, v, "customMid"), high = valueOf (info, v, "customHigh");
            auto custom = curveOf ([=] (int k) { return (double) (k < 2 ? low * (k == 0 ? 1.0f : 0.5f) : k < 6 ? mid * ((k == 3 || k == 4) ? 1.0f : 0.5f)
                                                                                                         : high * (k == 7 ? 1.0f : 0.5f)); });
            juce::Path dashed;
            const float dashes[] { 6.0f, 4.0f };
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, custom, dashes, 2);
            g.setColour (amber);
            g.fillPath (dashed);
        }
        const auto autoCurve = curveOf ([amount] (int k) { return (double) (meterAt (5 + k, 0.0f) * amount); });
        g.setColour (purple.withAlpha (0.25f));
        g.strokePath (autoCurve, juce::PathStrokeType (5.0f));
        g.setColour (purple);
        g.strokePath (autoCurve, juce::PathStrokeType (2.2f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("AUTO EQ", RF (inner.getRight() - 150.0f, inner.getY() + 4.0f, 70.0f, 12.0f), juce::Justification::right, false);
        if (valueOf (info, v, "customOn") > 0.5f)
        {
            g.setColour (amber);
            g.drawText ("CUSTOM", RF (inner.getRight() - 74.0f, inner.getY() + 4.0f, 70.0f, 12.0f), juce::Justification::right, false);
        }

        // Analysis progress and the readings
        const auto progress = meterAt (4, 0.0f);
        title (g, R (20, 16, 400, 30), progress < 1.0f ? "Analyzing... " + juce::String (juce::roundToInt (progress * 100.0f)) + " %" : juce::String ("Analysis complete"),
               juce::Colours::white.withAlpha (0.75f), 15.0f, juce::Justification::centredLeft);
        const auto row = [&] (int y, const juce::String& name, const juce::String& text)
        {
            title (g, R (750, y, 200, 18), name, juce::Colours::white.withAlpha (0.55f), 12.0f, juce::Justification::centredLeft);
            title (g, R (750, y + 18, 200, 28), text, juce::Colours::white, 20.0f, juce::Justification::centredLeft);
        };
        row (60, "SHORT-TERM", lufsText (meterAt (0)) + " LUFS");
        row (115, "INTEGRATED", lufsText (meterAt (1)) + " LUFS");
        row (170, "TRUE PEAK", (meterAt (2) <= -99.0f ? juce::String ("--") : juce::String (meterAt (2), 1)) + " dBTP");
        row (225, "SOURCE / GAIN", lufsText (meterAt (3)) + " LUFS  " + (meterAt (13, 0.0f) >= 0.0f ? "+" : "") + juce::String (meterAt (13, 0.0f), 1) + " dB");

        // Level meters: in and out (left, right), and how hard the limiter works
        const auto meterTop = 78.0f, meterHeight = 222.0f;
        const auto yDb = [&] (float db) { return meterTop + meterHeight * (1.0f - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)); };
        g.setFont (juce::FontOptions (10.0f));
        for (const auto db : { 0, -6, -12, -18, -24, -36, -48, -60 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String (db), RF (1010.0f, yDb ((float) db) - 6.0f, 22.0f, 12.0f), juce::Justification::centred, false);
        }
        const auto bar = [&] (float x, float db)
        {
            const RF track (x, meterTop, 8.0f, meterHeight);
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRect (track);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff4b3a), 0.0f, yDb (0.0f), juce::Colour (0xff59d36b), 0.0f, yDb (-18.0f), false));
            g.fillRect (track.withTop (yDb (db)));
        };
        bar (978.0f, meterAt (14, -120.0f));
        bar (989.0f, meterAt (15, -120.0f));
        bar (1036.0f, meterAt (16, -120.0f));
        bar (1047.0f, meterAt (17, -120.0f));
        title (g, R (966, 58, 44, 16), "IN", juce::Colours::white.withAlpha (0.6f), 11.0f);
        title (g, R (1024, 58, 44, 16), "OUT", juce::Colours::white.withAlpha (0.6f), 11.0f);
        {
            // The limiter's reduction, down from the top (0 to 12 dB)
            const RF track (1080.0f, meterTop, 8.0f, meterHeight);
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRect (track);
            g.setColour (purple);
            g.fillRect (track.withHeight (meterHeight * juce::jlimit (0.0f, 1.0f, meterAt (18, 0.0f) / 12.0f)));
            title (g, R (1064, 58, 44, 16), "LIM", juce::Colours::white.withAlpha (0.6f), 11.0f);
            const auto limit = meterAt (18, 0.0f);
            title (g, R (1060, 302, 50, 16), limit < 0.05f ? juce::String ("0.0") : juce::String (-limit, 1), purple, 11.0f);
        }

        // Along the bottom: the loudness against the target, the dynamics left, the stereo correlation
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (20, 536, b.getWidth() - 40, 1);
        {
            title (g, R (20, 546, 110, 16), "LOUDNESS", juce::Colours::white.withAlpha (0.55f), 11.0f, juce::Justification::centredLeft);
            const RF track (20.0f, 566.0f, 520.0f, 14.0f);
            const auto xLufs = [track] (float lufs) { return track.getX() + track.getWidth() * juce::jlimit (0.0f, 1.0f, (lufs + 36.0f) / 32.0f); };
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRoundedRectangle (track, 3.0f);
            const auto shortTerm = meterAt (0, -70.0f), integrated = meterAt (1, -70.0f), target = valueOf (info, v, "loudness", -14.0f);
            g.setColour (purple.withAlpha (0.8f));
            g.fillRoundedRectangle (track.withRight (xLufs (shortTerm)), 3.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (xLufs (integrated) - 1.0f, track.getY() - 3.0f, 2.0f, track.getHeight() + 6.0f);
            g.setColour (amber);
            g.fillRect (xLufs (target) - 1.5f, track.getY() - 5.0f, 3.0f, track.getHeight() + 10.0f);
            g.setFont (juce::FontOptions (10.0f));
            for (const auto l : { -36, -30, -24, -18, -12, -6 })
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawText (juce::String (l), RF (xLufs ((float) l) - 12.0f, 582.0f, 24.0f, 12.0f), juce::Justification::centred, false);
            }
            title (g, R (130, 546, 420, 16), "short-term (bar)   integrated (white)   target " + juce::String (target, 1) + " LUFS (amber)",
                   juce::Colours::white.withAlpha (0.4f), 10.5f, juce::Justification::centredRight);
        }
        {
            // PLR: how far the peaks stand above the loudness (squashed below about 8)
            const auto truePeak = meterAt (2, -100.0f), integrated = meterAt (1, -70.0f);
            const auto plr = truePeak > -99.0f && integrated > -69.0f ? truePeak - integrated : 0.0f;
            title (g, R (570, 546, 140, 16), "DYNAMICS (PLR)", juce::Colours::white.withAlpha (0.55f), 11.0f, juce::Justification::centredLeft);
            title (g, R (570, 562, 140, 24), plr > 0.0f ? juce::String (plr, 1) + " dB" : juce::String ("--"),
                   plr > 0.0f && plr < 8.0f ? amber : juce::Colours::white, 17.0f, juce::Justification::centredLeft);
        }
        {
            title (g, R (740, 546, 200, 16), "CORRELATION", juce::Colours::white.withAlpha (0.55f), 11.0f, juce::Justification::centredLeft);
            const RF track (740.0f, 566.0f, 360.0f, 14.0f);
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRoundedRectangle (track, 3.0f);
            const auto c = juce::jlimit (-1.0f, 1.0f, meterAt (19, 1.0f));
            const auto mid = track.getCentreX(), x = mid + c * track.getWidth() * 0.5f;
            g.setColour (c < 0.0f ? juce::Colour (0xffff5a4a) : juce::Colour (0xff59d36b));
            g.fillRect (RF (std::min (mid, x), track.getY() + 2.0f, std::abs (x - mid), track.getHeight() - 4.0f));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillRect (mid - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("-1", RF (track.getX(), 582.0f, 20.0f, 12.0f), juce::Justification::left, false);
            g.drawText ("0", RF (mid - 10.0f, 582.0f, 20.0f, 12.0f), juce::Justification::centred, false);
            g.drawText ("+1", RF (track.getRight() - 20.0f, 582.0f, 20.0f, 12.0f), juce::Justification::right, false);
            title (g, R (940, 546, 160, 16), juce::String (c, 2), juce::Colours::white, 12.0f, juce::Justification::centredRight);
        }
    }

    //==============================================================================
    // Match EQ: the template, the current material and the filter that matches them, over the analyser
    //==============================================================================
    constexpr int matchBands = 31;
    double matchBandFrequency (int k) { return 1000.0 * std::pow (2.0, (k - 17) / 3.0); }

    void paintMatchEq (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff383b40), 0.0f, 0.0f, juce::Colour (0xff222428), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const RF screen (20.0f, 50.0f, (float) b.getWidth() - 40.0f, 290.0f);
        g.setColour (juce::Colour (0xff0f1114));
        g.fillRoundedRectangle (screen, 5.0f);
        const auto xOf = [screen] (double f) { return screen.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * screen.getWidth(); };
        const auto yOf = [screen] (double db) { return screen.getCentreY() - (float) (juce::jlimit (-24.0, 24.0, db) / 24.0) * screen.getHeight() * 0.5f; };
        g.setFont (juce::FontOptions (11.0f));
        for (const auto f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (xOf (f), screen.getY(), 1.0f, screen.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) + 3.0f, screen.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::left, false);
        }
        for (const auto db : { -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.2f : 0.06f));
            g.fillRect (screen.getX(), yOf (db), screen.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String ((int) db), RF (screen.getX() + 4.0f, yOf (db) - 13.0f, 30.0f, 12.0f), juce::Justification::left, false);
        }
        if (valueOf (info, v, "analyzer", 1.0f) > 0.5f)
            drawSpectrum (g, screen, xOf, juce::Colour (0xff6c8fb3));

        // The two spectra, each about its own average
        const auto spectrumCurve = [&] (const char* prefix, juce::Colour colour)
        {
            std::array<float, matchBands> d {};
            float mean = 0.0f;
            int valid = 0;
            for (int k = 0; k < matchBands; ++k)
            {
                d[(size_t) k] = valueOf (info, v, (juce::String (prefix) + juce::String (k + 1)).toRawUTF8(), -120.0f);
                if (d[(size_t) k] > -119.0f) { mean += d[(size_t) k]; ++valid; }
            }
            if (valid < 3)
                return false;
            mean /= (float) valid;
            juce::Path path;
            for (int k = 0; k < matchBands; ++k)
            {
                const auto p = juce::Point<float> (xOf (matchBandFrequency (k)), yOf (d[(size_t) k] - mean));
                if (k == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
            return true;
        };
        const auto hasTemplate = spectrumCurve ("t", juce::Colour (0xffffa23c));
        const auto hasCurrent = spectrumCurve ("c", juce::Colour (0xff53d37a));

        // The filter
        juce::Path filter;
        for (int x = 0; x <= (int) screen.getWidth(); x += 2)
        {
            const auto f = 20.0 * std::pow (1000.0, x / (double) screen.getWidth());
            const auto p = juce::Point<float> (screen.getX() + (float) x, yOf (info.response (v, f, 48000.0)));
            if (x == 0) filter.startNewSubPath (p); else filter.lineTo (p);
        }
        g.setColour (juce::Colour (0xfff2f2f2));
        g.strokePath (filter, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

        // Legend and status
        int count = 0;
        const auto* m = paintingMeters (count);
        const auto status = [&] (bool has, int secondsAt, const char* learnId)
        {
            if (valueOf (info, v, learnId) > 0.5f)
                return "Learning... " + juce::String (count > secondsAt ? m[secondsAt] : 0.0f, 1) + " s";
            return juce::String (has ? "Learned" : "Empty");
        };
        g.setFont (juce::FontOptions (13.0f));
        const auto legend = [&] (int x, juce::Colour c, const juce::String& text)
        {
            g.setColour (c);
            g.fillRoundedRectangle (RF ((float) x, 22.0f, 14.0f, 4.0f), 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText (text, R (x + 20, 14, 260, 20), juce::Justification::centredLeft, false);
        };
        legend (24, juce::Colour (0xffffa23c), "Template: " + status (hasTemplate, 62, "learnT"));
        legend (290, juce::Colour (0xff53d37a), "Current: " + status (hasCurrent, 63, "learnC"));
        legend (556, juce::Colour (0xfff2f2f2), hasTemplate && hasCurrent ? juce::String ("Filter: matching") : juce::String ("Filter: learn both to match"));

        // Section titles
        const auto ink = juce::Colours::white.withAlpha (0.55f);
        title (g, R (20, 356, 300, 20), "TEMPLATE", ink, 13.0f);
        title (g, R (340, 356, 200, 20), "CURRENT MATERIAL", ink, 13.0f);
        title (g, R (560, 356, 240, 20), "MATCH", ink, 13.0f);
        title (g, R (820, 356, 160, 20), "OUTPUT", ink, 13.0f);
        title (g, R (830, 474, 150, 16), "Gain", ink, 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (const auto x : { 330, 550, 810 })
            g.fillRect (x, 360, 1, 180);
    }

    //==============================================================================
    // Multi Effects (Phat FX, Step FX): processor panels, modulators, an XY pad
    //==============================================================================
    void multiPanel (juce::Graphics& g, R area, juce::Colour accent, bool on)
    {
        g.setColour (juce::Colour (0xff2a2c31));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        g.setColour (on ? accent.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (area.toFloat().removeFromTop (3.0f).reduced (6.0f, 0.0f), 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void smallLabel (juce::Graphics& g, R area, const juce::String& text, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (text, area, j, false);
    }

    void xyPad (juce::Graphics& g, R area, float x, float y, juce::Colour accent, bool active)
    {
        g.setColour (juce::Colour (0xff131417));
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (int k = 1; k < 4; ++k)
        {
            g.fillRect (area.getX() + area.getWidth() * k / 4, area.getY(), 1, area.getHeight());
            g.fillRect (area.getX(), area.getY() + area.getHeight() * k / 4, area.getWidth(), 1);
        }
        const juce::Point<float> puck (area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight());
        g.setColour (accent.withAlpha (active ? 0.25f : 0.1f));
        g.fillRect (puck.x, (float) area.getY(), 1.0f, (float) area.getHeight());
        g.fillRect ((float) area.getX(), puck.y, (float) area.getWidth(), 1.0f);
        g.setColour (active ? accent : juce::Colours::grey);
        g.fillEllipse (puck.x - 7.0f, puck.y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawEllipse (puck.x - 7.0f, puck.y - 7.0f, 14.0f, 14.0f, 1.2f);
    }

    const juce::Colour phatAccent (0xffffb03a), stepAccent (0xff47c6e8);

    void paintPhatFx (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.fillAll (juce::Colour (0xff1b1c1f));
        const auto on = [&] (const char* id) { return valueOf (info, v, id) > 0.5f; };
        const std::tuple<int, int, const char*> units[] { { 12, 190, "bpOn" }, { 210, 240, "fOn" }, { 458, 250, "dOn" }, { 716, 150, "mOn" }, { 874, 150, "bOn" }, { 1032, 156, "cOn" } };
        for (const auto& [x, w, id] : units)
            multiPanel (g, R (x, 12, w, 288), phatAccent, on (id));
        // The order strip
        smallLabel (g, R (12, 314, 60, 24), "ORDER");
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        for (int k = 0; k < 5; ++k)
        {
            const auto x = 92.0f + (float) (k + 1) * 182.0f - 26.0f;
            juce::Path arrow;
            arrow.addTriangle (x, 320.0f, x, 332.0f, x + 10.0f, 326.0f);
            g.fillPath (arrow);
        }
        const std::tuple<int, int, const char*> mods[] { { 12, 270, "l1On" }, { 290, 270, "l2On" }, { 568, 220, "eOn" } };
        for (const auto& [x, w, id] : mods)
        {
            multiPanel (g, R (x, 356, w, 352), phatAccent, on (id));
            smallLabel (g, R (x + 8, 394, 80, 14), "TARGET");
        }
        multiPanel (g, R (796, 356, 260, 352), phatAccent, on ("xOn") || on ("yOn"));
        smallLabel (g, R (806, 362, 150, 16), "XY PAD");
        xyPad (g, R (806, 386, 150, 150), valueOf (info, v, "xyX", 0.5f), valueOf (info, v, "xyY", 0.5f), phatAccent, on ("xOn") || on ("yOn"));
        multiPanel (g, R (1064, 356, 124, 352), phatAccent, true);
        smallLabel (g, R (1072, 362, 110, 16), "MASTER");
        title (g, R (12, 680, 1044, 24), "PHAT FX", juce::Colours::white.withAlpha (0.12f), 18.0f, juce::Justification::centredRight);
    }

    void paintStepFx (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.fillAll (juce::Colour (0xff17191c));
        const auto on = [&] (const char* id) { return valueOf (info, v, id) > 0.5f; };
        const std::tuple<int, int, const char*> units[] { { 12, 260, "fltOn" }, { 280, 150, "distOn" }, { 438, 300, "modOn" }, { 746, 278, "dlyOn" }, { 1032, 156, "revOn" } };
        for (const auto& [x, w, id] : units)
            multiPanel (g, R (x, 12, w, 228), stepAccent, on (id));

        // The selected step modulator: its steps as bars, the one playing lit
        const auto k = juce::jlimit (0, 2, juce::roundToInt (valueOf (info, v, "view")));
        const auto n = juce::String (k + 1);
        multiPanel (g, R (12, 248, 1176, 256), stepAccent, valueOf (info, v, ("s" + n + "On").toRawUTF8()) > 0.5f);
        const char* heads[] { "Target", "Depth", "Rate", "Swing", "Attack", "Hold", "Release", "Steps" };
        const int headX[] { 360, 540, 640, 730, 820, 910, 1000, 1090 };
        for (int h = 0; h < 8; ++h)
            smallLabel (g, R (headX[h], 252, 90, 14), heads[h]);
        const auto length = juce::roundToInt (valueOf (info, v, ("s" + n + "Length").toRawUTF8(), 15.0f)) + 1;
        int count = 0;
        const auto* m = paintingMeters (count);
        const auto playing = count > k ? juce::roundToInt (m[k]) : -1;
        for (int st = 0; st < stepFxMaxSteps; ++st)
        {
            const auto x = 30 + st * 35;
            const R slot (x, 300, 32, 170);
            g.setColour (st >= length ? juce::Colours::black.withAlpha (0.25f) : st == playing ? stepAccent.withAlpha (0.28f) : juce::Colour (0xff202227));
            g.fillRoundedRectangle (slot.toFloat(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (st == playing ? 0.9f : 0.35f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (st + 1), R (x, 286, 32, 12), juce::Justification::centred, false);
            if (st % 4 == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRect (x - 2, 286, 1, 210);
            }
        }
        multiPanel (g, R (12, 512, 560, 236), stepAccent, on ("xOn") || on ("yOn"));
        smallLabel (g, R (22, 518, 150, 16), "XY PAD");
        xyPad (g, R (22, 540, 196, 196), valueOf (info, v, "xyX", 0.5f), valueOf (info, v, "xyY", 0.5f), stepAccent, on ("xOn") || on ("yOn"));
        multiPanel (g, R (580, 512, 608, 236), stepAccent, true);
        smallLabel (g, R (590, 518, 150, 16), "MASTER");
        title (g, R (580, 716, 596, 24), "STEP FX", juce::Colours::white.withAlpha (0.12f), 18.0f, juce::Justification::centredRight);
    }

    const juce::Colour remixA (0xffff5a8a), remixB (0xff4fa8ff);

    void paintRemixFx (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.fillAll (juce::Colour (0xff141518));
        const auto on = [&] (const char* id) { return valueOf (info, v, id) > 0.5f; };
        const char* padNames[] { "Filter", "Repeater", "Wobble", "Reverb", "Orbit", "Delay" };
        const auto pad = [&] (R area, const char* x, const char* y, bool active, juce::Colour colour, int effect)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (active ? 0.35f : 0.14f), (float) area.getX(), (float) area.getY(),
                                                     juce::Colour (0xff141518), (float) area.getRight(), (float) area.getBottom(), false));
            g.fillRoundedRectangle (area.toFloat(), 10.0f);
            g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
            g.drawText (juce::String (padNames[juce::jlimit (0, 5, effect)]).toUpperCase(), area.reduced (20), juce::Justification::bottomLeft, false);
            const juce::Point<float> puck (area.getX() + valueOf (info, v, x, 0.5f) * area.getWidth(), area.getBottom() - valueOf (info, v, y, 0.5f) * area.getHeight());
            g.setColour (colour.withAlpha (active ? 0.9f : 0.4f));
            g.fillEllipse (puck.x - 16.0f, puck.y - 16.0f, 32.0f, 32.0f);
            g.setColour (juce::Colours::white.withAlpha (active ? 0.9f : 0.4f));
            g.drawEllipse (puck.x - 16.0f, puck.y - 16.0f, 32.0f, 32.0f, 2.0f);
        };
        pad (R (20, 62, 380, 380), "padAX", "padAY", on ("padAOn") || on ("padALock"), remixA, juce::roundToInt (valueOf (info, v, "padAEffect")));
        pad (R (700, 62, 380, 380), "padBX", "padBY", on ("padBOn") || on ("padBLock"), remixB, juce::roundToInt (valueOf (info, v, "padBEffect", 1.0f)));
        // The middle: the two sliders' tracks
        for (const auto& [x, id, name] : { std::tuple { 448, "gater", "GATER" }, std::tuple { 572, "downsampler", "DOWNSAMPLER" } })
        {
            g.setColour (juce::Colour (0xff22242a));
            g.fillRoundedRectangle (RF ((float) x, 116.0f, 80.0f, 300.0f), 8.0f);
            const auto level = valueOf (info, v, id) * 0.01f;
            g.setColour (juce::Colour (0xffffc04a).withAlpha (0.25f + 0.5f * level));
            g.fillRoundedRectangle (RF ((float) x, 116.0f + 300.0f * (1.0f - level), 80.0f, 300.0f * level), 8.0f);
            smallLabel (g, R (x - 20, 96, 120, 16), name, juce::Justification::centred);
        }
        multiPanel (g, R (20, 470, 1060, 96), juce::Colour (0xffffc04a), true);
        smallLabel (g, R (32, 476, 200, 14), "SETTINGS");
        smallLabel (g, R (40, 490, 280, 14), "Filter");
        smallLabel (g, R (370, 490, 300, 14), "Reverb Color");
        smallLabel (g, R (720, 490, 200, 14), "Orbit");
    }

    //==============================================================================
    // Beat Breaker: the input across the pattern, the slices over it, the selected one's settings
    //==============================================================================
    constexpr int bbGlobals = 13, bbPerSlice = 10, bbSlices = 32;
    const juce::Colour bbModeColours[] { juce::Colour (0xffff9a3c), juce::Colour (0xffff4fd8), juce::Colour (0xff3fd6ef), juce::Colour (0xffa77bff),
                                         juce::Colour (0xffffde4a), juce::Colour (0xff5fe07a) };

    void paintBeatBreaker (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.fillAll (juce::Colour (0xff17181b));
        const auto slice = [v] (int k, int p) { return v[bbGlobals + k * bbPerSlice + p]; };
        const auto mode = juce::jlimit (0, 5, juce::roundToInt (valueOf (info, v, "mode")));
        const auto colour = bbModeColours[mode];
        const auto count = juce::jlimit (1, bbSlices, juce::roundToInt (valueOf (info, v, "sliceCount", 7.0f)) + 1);
        const auto selected = juce::roundToInt (valueOf (info, v, "selected"));
        const double lengths[] { 1.0, 2.0, 4.0, 8.0, 16.0 };
        const auto sixteenths = lengths[juce::jlimit (0, 4, juce::roundToInt (valueOf (info, v, "length", 2.0f)))] * 4.0;
        const RF screen (20.0f, 60.0f, (float) b.getWidth() - 40.0f, 260.0f);
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (screen, 6.0f);
        const auto xOf = [&] (double sixteenth) { return screen.getX() + (float) (sixteenth / sixteenths) * screen.getWidth(); };
        // the grid: beats, sixteenths
        for (int k = 0; k <= (int) sixteenths; ++k)
        {
            g.setColour (juce::Colours::white.withAlpha (k % 4 == 0 ? 0.12f : 0.04f));
            g.fillRect (xOf (k), screen.getY(), 1.0f, screen.getHeight());
        }
        // the input, over the top
        int readings = 0;
        const auto* m = paintingMeters (readings);
        const RF wave (screen.getX(), screen.getY() + 8.0f, screen.getWidth(), 70.0f);
        if (readings >= 130)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            for (int k = 0; k < 128; ++k)
            {
                const auto h = std::min (1.0f, std::sqrt (m[2 + k])) * wave.getHeight() * 0.5f;
                g.fillRect (wave.getX() + wave.getWidth() * (float) k / 128.0f, wave.getCentreY() - h, wave.getWidth() / 128.0f - 1.0f, h * 2.0f);
            }
        }
        // the slices, in output order
        std::vector<int> order ((size_t) count);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int c) { return slice (a, 0) < slice (c, 0); });
        const RF lane (screen.getX(), screen.getY() + 90.0f, screen.getWidth(), screen.getHeight() - 104.0f);
        for (size_t i = 0; i < order.size(); ++i)
        {
            const auto k = order[i];
            const auto start = i == 0 ? 0.0 : (double) slice (k, 0);
            const auto end = i + 1 < order.size() ? (double) slice (order[i + 1], 0) : sixteenths;
            if (start >= sixteenths) continue;
            const RF block (xOf (start) + 1.0f, lane.getY(), xOf (std::min (end, sixteenths)) - xOf (start) - 2.0f, lane.getHeight());
            const auto isSelected = k == selected;
            const auto isPlaying = readings >= 2 && juce::roundToInt (m[1]) == k;
            g.setColour (colour.withAlpha (isPlaying ? 0.22f : 0.1f));
            g.fillRoundedRectangle (block, 3.0f);
            g.setColour (colour.withAlpha (isSelected ? 1.0f : 0.75f));
            switch (mode)
            {
                case 0:
                {
                    // where it plays from (the input beat), and its speed as a line's slope
                    const auto from = xOf (slice (k, 1));
                    g.drawLine (from, wave.getBottom(), block.getX(), block.getY(), 1.0f);
                    const auto speed = slice (k, 2) / 400.0f;
                    g.drawLine (block.getX(), block.getCentreY() + block.getHeight() * 0.4f * speed, block.getRight(), block.getCentreY() - block.getHeight() * 0.4f * speed, 2.0f);
                    break;
                }
                case 1:
                {
                    const auto n = juce::roundToInt (slice (k, 4)) + 1;
                    for (int r = 0; r < n; ++r)
                        g.fillRoundedRectangle (RF (block.getX() + block.getWidth() * (float) r / (float) n + 1.0f, block.getY() + 6.0f, block.getWidth() / (float) n - 2.0f, block.getHeight() - 12.0f), 2.0f);
                    break;
                }
                case 2: case 3:
                {
                    const auto t = mode == 2 ? std::log (slice (k, 5) / 20.0f) / std::log (1000.0f) : slice (k, 6) / 100.0f;
                    g.fillRoundedRectangle (block.withTop (block.getBottom() - block.getHeight() * juce::jlimit (0.02f, 1.0f, t)), 2.0f);
                    break;
                }
                case 4:
                {
                    const auto level = juce::jlimit (0.0f, 1.0f, (slice (k, 7) + 60.0f) / 66.0f);
                    const auto sl = slice (k, 8) / 100.0f;
                    const auto startLevel = level * (sl >= 0.0f ? 1.0f - sl : 1.0f), endLevel = level * (sl >= 0.0f ? 1.0f : 1.0f + sl);
                    juce::Path p;
                    p.startNewSubPath (block.getX(), block.getBottom());
                    p.lineTo (block.getX(), block.getBottom() - block.getHeight() * startLevel);
                    p.lineTo (block.getRight(), block.getBottom() - block.getHeight() * endLevel);
                    p.lineTo (block.getRight(), block.getBottom());
                    p.closeSubPath();
                    g.fillPath (p);
                    break;
                }
                default:
                {
                    const auto pn = slice (k, 9) / 100.0f;
                    const auto cx = block.getCentreX() + pn * block.getWidth() * 0.45f;
                    g.fillEllipse (cx - 6.0f, block.getCentreY() - 6.0f, 12.0f, 12.0f);
                    g.fillRect (block.getCentreX(), block.getCentreY() - 0.5f, cx - block.getCentreX(), 1.0f);
                    break;
                }
            }
            if (isSelected)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (block.expanded (1.0f), 3.0f, 1.4f);
            }
            g.setColour (juce::Colours::white.withAlpha (isSelected ? 0.95f : 0.5f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (k + 1), RF (block.getX(), lane.getBottom() + 1.0f, std::max (14.0f, block.getWidth()), 12.0f), juce::Justification::centredLeft, false);
        }
        if (readings >= 1)
        {
            const auto x = screen.getX() + m[0] * screen.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (x, screen.getY(), 1.5f, screen.getHeight());
        }
        // the selected slice's settings: their names
        const char* names[] { "Slice", "Output Beat", "Input Beat", "Speed", "Curve", "Repeat", "Cutoff", "Resonance", "Volume", "Slope", "Pan" };
        const int xs[] { 20, 120, 220, 320, 420, 520, 620, 720, 820, 920, 1000 };
        for (int k = 0; k < 11; ++k)
            smallLabel (g, R (xs[k], 334, 96, 14), names[k]);
        multiPanel (g, R (12, 398, b.getWidth() - 24, 190), colour, true);
        smallLabel (g, R (24, 406, 120, 14), "PATTERNS");
        smallLabel (g, R (24, 488, 120, 14), "GLOBAL");
    }

    /** Beat Breaker's built-in patterns and its randomize, as parameter values (set by the window). */
    void beatBreakerPattern (int pattern, const EffectInfo& fx, const float* v, const std::function<void (const juce::String&, float)>& set)
    {
        const auto put = [&] (int k, const char* what, float value)
        {
            const auto id = juce::String (what) + juce::String (k + 1);
            if (std::abs (valueOf (fx, v, id.toRawUTF8()) - value) > 1.0e-4f)
                set (id, value);
        };
        set ("sliceCount", 7.0f);
        set ("length", 2.0f);
        const int shuffle[] { 0, 6, 4, 2, 8, 14, 12, 10 };
        for (int k = 0; k < bbSlices; ++k)
        {
            const auto out = (float) (k * 2 % 64);
            auto in = out, speed = 100.0f, curve = 0.0f, repeat = 0.0f, cutoff = 20000.0f, reso = 0.0f, volume = 0.0f, slope = 0.0f, pan = 0.0f;
            if (k < 8)
                switch (pattern)
                {
                    case 1: if (k == 2 || k == 3) repeat = 3.0f; if (k == 7) { repeat = 7.0f; in = 12.0f; } break;
                    case 2: if (k >= 4) { speed = -100.0f; in = out + 2.0f; } break;
                    case 3: in = (float) k; speed = 50.0f; break;
                    case 4: in = (float) (k * 4 % 16); speed = 200.0f; break;
                    case 5: if (k >= 5) { in = 10.0f + (float) (k - 5) * 0.75f; speed = 100.0f - (float) (k - 4) * 25.0f; curve = -50.0f; slope = -40.0f; } break;
                    case 6: if (k % 2 == 1) volume = -60.0f; break;
                    case 7: in = (float) shuffle[k]; break;
                    case 8: cutoff = 300.0f * std::pow (20000.0f / 300.0f, (float) k / 7.0f); reso = 40.0f; break;
                    case 9: pan = k % 2 == 0 ? -80.0f : 80.0f; break;
                    default: break;
                }
            put (k, "out", out); put (k, "in", in); put (k, "speed", speed); put (k, "curve", curve); put (k, "repeat", repeat);
            put (k, "cutoff", cutoff); put (k, "reso", reso); put (k, "volume", volume); put (k, "slope", slope); put (k, "pan", pan);
        }
    }

    void beatBreakerRandomize (int mode, bool all, const EffectInfo& fx, const float* v, const std::function<void (const juce::String&, float)>& set)
    {
        juce::Random random;
        const auto count = juce::roundToInt (valueOf (fx, v, "sliceCount", 7.0f)) + 1;
        for (int k = 0; k < count; ++k)
        {
            const auto n = juce::String (k + 1);
            const auto on = [&] (int m) { return all || mode == m; };
            if (on (0))
            {
                const float speeds[] { 100.0f, 100.0f, 100.0f, -100.0f, 50.0f, 200.0f };
                set ("in" + n, (float) (random.nextInt (8) * 2));
                set ("speed" + n, speeds[random.nextInt (6)]);
            }
            if (on (1)) set ("repeat" + n, random.nextInt (3) == 0 ? (float) random.nextInt (4) : 0.0f);
            if (on (2)) set ("cutoff" + n, 400.0f * std::pow (50.0f, random.nextFloat()));
            if (on (3)) set ("reso" + n, random.nextFloat() * 70.0f);
            if (on (4)) { set ("volume" + n, -random.nextFloat() * 12.0f); set ("slope" + n, (random.nextFloat() * 2.0f - 1.0f) * 60.0f); }
            if (on (5)) set ("pan" + n, (random.nextFloat() * 2.0f - 1.0f) * 80.0f);
        }
    }

    //==============================================================================
    // Room Simulator: the reverb time with the room's range, in/out meters, the multiplier graph
    //==============================================================================
    void paintRoomSim (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        const auto modern = valueOf (info, v, "model", 1.0f) > 0.5f;
        const auto accent = modern ? juce::Colour (0xff6fb7ff) : juce::Colour (0xffffa94a);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2f38), 0.0f, 0.0f, juce::Colour (0xff181b21), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        int count = 0;
        const auto* m = paintingMeters (count);
        // The room's range of reverb times, as a ring around the knob
        if (count >= 4)
        {
            const auto& p = info.params[1];
            const auto angle = [&] (float t)
            {
                const auto unit = std::log (std::max (t, p.min) / p.min) / std::log (p.max / p.min);
                return juce::MathConstants<float>::pi * (1.2f + 1.6f * juce::jlimit (0.0f, 1.0f, unit)) - juce::MathConstants<float>::pi * 2.0f;
            };
            juce::Path ring;
            ring.addCentredArc (150.0f, 205.0f, 104.0f, 104.0f, 0.0f, angle (m[2]), angle (m[3]), true);
            g.setColour (accent.withAlpha (0.45f));
            g.strokePath (ring, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // the meters
            for (int k = 0; k < 2; ++k)
            {
                const RF bar (290.0f + k * 22.0f, 80.0f, 14.0f, 240.0f);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (bar, 3.0f);
                const auto level = juce::jlimit (0.0f, 1.0f, (m[k] + 60.0f) / 60.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3ddc6a), 0.0f, bar.getBottom(), juce::Colour (0xffffd84a), 0.0f, bar.getY(), false));
                g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * level), 3.0f);
                smallLabel (g, R ((int) bar.getX() - 6, 324, 26, 14), k == 0 ? "In" : "Out", juce::Justification::centred);
            }
        }
        // The multiplier graph: 0.1x to 10x up the side, frequency across
        const RF graph (340.0f, 70.0f, 640.0f, 250.0f);
        g.setColour (juce::Colour (0xff0e1014));
        g.fillRoundedRectangle (graph, 6.0f);
        const auto xOf = [&] (double f) { return graph.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * graph.getWidth(); };
        const auto yOf = [&] (double mult) { return graph.getBottom() - (float) ((std::log10 (juce::jlimit (0.1, 10.0, mult)) + 1.0) / 2.0) * graph.getHeight(); };
        g.setFont (juce::FontOptions (11.0f));
        for (const auto mult : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (mult == 1.0 ? 0.25f : 0.07f));
            g.fillRect (graph.getX(), yOf (mult), graph.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawText (juce::String (mult) + "x", RF (graph.getX() + 4.0f, yOf (mult) - 13.0f, 40.0f, 12.0f), juce::Justification::left, false);
            const auto seconds = mult * valueOf (info, v, "reverbTime", 2.0f);
            g.drawText (seconds >= 10.0 ? juce::String (seconds, 0) + " s" : juce::String (seconds, 2) + " s", RF (graph.getRight() - 64.0f, yOf (mult) - 13.0f, 60.0f, 12.0f), juce::Justification::right, false);
        }
        const auto lowX = modern ? valueOf (info, v, "lowXover", 300.0f) : 500.0f, highX = modern ? valueOf (info, v, "highXover", 3000.0f) : 1000.0f;
        const auto lowM = valueOf (info, v, "lowMult", 1.0f), highM = valueOf (info, v, "highMult", 1.0f);
        juce::Path curve;
        curve.startNewSubPath (graph.getX(), yOf (lowM));
        curve.lineTo (xOf (lowX), yOf (lowM));
        curve.lineTo (xOf (std::max (lowX * 1.6f, 1000.0f)), yOf (1.0));
        curve.lineTo (xOf (std::max (highX, 1000.0f)), yOf (1.0));
        curve.lineTo (xOf (std::max (highX, 1000.0f) * 1.6f), yOf (highM));
        curve.lineTo (graph.getRight(), yOf (highM));
        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f));
        for (const auto& [f, mult] : { std::pair { lowX, lowM }, std::pair { highX, highM } })
        {
            g.setColour (accent);
            g.fillEllipse (xOf (f) - 6.0f, yOf (mult) - 6.0f, 12.0f, 12.0f);
            g.setColour (juce::Colours::white);
            g.drawEllipse (xOf (f) - 6.0f, yOf (mult) - 6.0f, 12.0f, 12.0f, 1.2f);
        }
        smallLabel (g, R (348, 74, 300, 14), "REVERB TIME MULTIPLIER");
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (12, 334, b.getWidth() - 24, 1);
        title (g, R (20, 520, b.getWidth() - 40, 30), modern ? "ROOM SIMULATOR  -  MODERN" : "ROOM SIMULATOR  -  CLASSIC", juce::Colours::white.withAlpha (0.12f), 16.0f, juce::Justification::centredRight);
    }

    //==============================================================================
    // Delay Designer: the taps on a timeline, the selected tap's parameters below, the master section
    //==============================================================================
    void paintDelayDesigner (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), 0.0f, 0.0f, juce::Colour (0xff24262a), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        constexpr int globals = 11, perTap = 12;
        const auto tapValue = [v] (int t, int p) { return v[globals + t * perTap + p]; };
        const auto count = juce::jlimit (1, 26, juce::roundToInt (valueOf (info, v, "tapCount", 3.0f)) + 1);
        const auto selected = juce::roundToInt (valueOf (info, v, "tapSel"));
        const auto viewMode = juce::roundToInt (valueOf (info, v, "view", 4.0f));
        const auto fbTap = valueOf (info, v, "fbOn", 1.0f) > 0.5f ? juce::roundToInt (valueOf (info, v, "fbTap", 3.0f)) : -1;
        int readings = 0;
        const auto* times = paintingMeters (readings);
        const auto timeOf = [&] (int t) { return t < readings ? times[t] : tapValue (t, 0); };

        // The tap display
        const RF screen (20.0f, 52.0f, (float) b.getWidth() - 40.0f, 270.0f);
        g.setColour (juce::Colour (0xff111316));
        g.fillRoundedRectangle (screen, 5.0f);
        auto plot = screen.reduced (14.0f, 12.0f).withTrimmedBottom (26.0f);
        float longest = 500.0f;
        for (int t = 0; t < count; ++t)
            longest = std::max (longest, timeOf (t));
        longest *= 1.12f;
        const auto xOf = [&] (float ms) { return plot.getX() + ms / longest * plot.getWidth(); };
        // time ruler
        const auto step = longest > 6000.0f ? 1000.0f : longest > 2500.0f ? 500.0f : longest > 1000.0f ? 250.0f : 100.0f;
        g.setFont (juce::FontOptions (11.0f));
        for (float ms = 0.0f; ms <= longest; ms += step)
        {
            const auto x = xOf (ms);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawText (ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String ((int) ms) + " ms",
                        RF (x - 40.0f, screen.getBottom() - 20.0f, 80.0f, 14.0f), juce::Justification::centred, false);
        }
        const auto centreLine = viewMode == 2 || viewMode == 3;
        if (centreLine)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        }
        const juce::Colour viewColours[] { juce::Colour (0xffffb648), juce::Colour (0xffff7a59), juce::Colour (0xff5ad1ff), juce::Colour (0xffb98bff), juce::Colour (0xff7ee08c) };
        const auto ink = viewColours[juce::jlimit (0, 4, viewMode)];
        const auto barW = juce::jlimit (4.0f, 18.0f, plot.getWidth() / (float) (count * 2 + 2));
        const auto freqY = [&] (float f) { return plot.getBottom() - (float) (std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f)) * plot.getHeight(); };
        for (int t = 0; t < count; ++t)
        {
            const auto x = xOf (timeOf (t));
            const auto muted = tapValue (t, 11) > 0.5f;
            const auto colour = (muted ? juce::Colours::grey : ink).withAlpha (t == selected ? 1.0f : 0.6f);
            g.setColour (colour);
            RF bar;
            switch (viewMode)
            {
                case 0:
                {
                    const auto on = tapValue (t, 5) > 0.5f;
                    const auto top = freqY (on ? tapValue (t, 7) : 20000.0f), bottom = freqY (on ? tapValue (t, 6) : 20.0f);
                    bar = RF (x - barW * 0.5f, top, barW, std::max (2.0f, bottom - top));
                    if (! on) g.setColour (colour.withAlpha (0.25f));
                    break;
                }
                case 1:  { const auto h = tapValue (t, 9) * 0.01f * plot.getHeight(); bar = RF (x - barW * 0.5f, plot.getBottom() - h, barW, std::max (2.0f, h)); break; }
                case 2:
                case 3:
                {
                    const auto amount = viewMode == 2 ? (tapValue (t, 3) + tapValue (t, 4) * 0.01f) / 24.0f : tapValue (t, 2) / 100.0f;
                    const auto h = amount * plot.getHeight() * 0.5f;
                    bar = RF (x - barW * 0.5f, h > 0 ? plot.getCentreY() - h : plot.getCentreY(), barW, std::max (2.0f, std::abs (h)));
                    break;
                }
                default: { const auto h = (tapValue (t, 1) + 60.0f) / 60.0f * plot.getHeight(); bar = RF (x - barW * 0.5f, plot.getBottom() - h, barW, std::max (2.0f, h)); break; }
            }
            g.fillRoundedRectangle (bar, 2.0f);
            if (t == selected)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (bar.expanded (2.0f), 3.0f, 1.2f);
            }
            g.setColour (t == selected ? juce::Colours::white : juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f, t == selected ? juce::Font::bold : juce::Font::plain));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + t)), RF (x - 10.0f, plot.getBottom() + 2.0f, 20.0f, 14.0f), juce::Justification::centred, false);
            if (t == fbTap)
            {
                g.setColour (juce::Colour (0xffffd84a));
                g.drawText ("FB", RF (x - 14.0f, plot.getY() - 12.0f, 28.0f, 12.0f), juce::Justification::centred, false);
            }
        }
        const char* viewNames[] { "Cutoff", "Resonance", "Transposition", "Pan", "Level" };
        g.setColour (ink);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (viewNames[juce::jlimit (0, 4, viewMode)], screen.reduced (14.0f, 8.0f).removeFromTop (16.0f), juce::Justification::topRight, false);

        // The tap parameter bar
        g.setColour (juce::Colour (0xff2b2e33));
        g.fillRoundedRectangle (RF (12.0f, 334.0f, (float) b.getWidth() - 24.0f, 82.0f), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f));
        const std::pair<int, const char*> labels[] { { 20, "Tap" }, { 105, "Delay" }, { 230, "Transpose" }, { 310, "Fine" }, { 395, "Filter" },
                                                     { 465, "HP Cutoff" }, { 575, "LP Cutoff" }, { 685, "Slope" }, { 805, "Reso" },
                                                     { 895, "Pan" }, { 985, "Level" }, { 1085, "Flip" }, { 1140, "Mute" } };
        for (const auto& [x, text] : labels)
            g.drawText (text, R (x, 344, 110, 16), juce::Justification::centredLeft, false);

        // The master section
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (12, 428, b.getWidth() - 24, 1);
        title (g, R (150, 436, 330, 20), "SYNC", juce::Colours::white.withAlpha (0.55f), 13.0f);
        title (g, R (520, 436, 320, 20), "FEEDBACK", juce::Colours::white.withAlpha (0.55f), 13.0f);
        title (g, R (880, 436, 280, 20), "MIX", juce::Colours::white.withAlpha (0.55f), 13.0f);
    }


    //==============================================================================
    // Photographed faces: the hardware's own front panel, its moving parts cut out of the photo
    // (Tools/effect_skins) and turned or slid to the settings. Positions are the photo's pixels.
    //==============================================================================
    Sprite turning (const char* image, float cx, float cy, float r, float photographed, std::vector<float> angles)
    {
        Sprite s;
        s.image = image;
        s.rect = RF (cx - r, cy - r, r * 2.0f, r * 2.0f);
        s.pivot = { cx, cy };
        s.angle = photographed;
        s.angles = std::move (angles);
        return s;
    }

    R onFace (RF photoRect, float scale)
    {
        return photoRect.transformedBy (juce::AffineTransform::scale (scale)).getSmallestIntegerContainer();
    }

    Control photoControl (const char* id, Kind kind, Sprite sprite, float scale)
    {
        Control c;
        c.id = id;
        c.kind = kind;
        c.label = "-";
        c.bounds = onFace (sprite.rect, scale);
        c.sprite = std::move (sprite);
        return c;
    }

    /** An invisible button over a button in the photo (the face shows its state: a lamp, a lit cap). */
    Control photoButton (const char* id, RF rect, float scale, int choice = -1)
    {
        Sprite s;
        s.rect = rect;
        s.pivot = rect.getCentre();
        s.choice = choice;
        Control c;
        c.id = id;
        c.kind = Kind::hotspot;
        c.label = "-";
        c.bounds = onFace (rect, scale);
        c.sprite = s;
        return c;
    }

    void drawPart (juce::Graphics& g, const char* name, RF photoRect, float scale, float alpha = 1.0f)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (alpha);
        g.drawImage (skinImage (name), photoRect.transformedBy (juce::AffineTransform::scale (scale)));
        g.setOpacity (1.0f);
    }

    //==============================================================================
    // The Classic Compressors: drawn panels (Tools/compressor_skins/classic_faces.py) in five skins, their knobs,
    // selectors and switches from the knob art, the black compression meter with its needle, the power lamp
    //==============================================================================
    int paintingSkinValue = defaultClassicSkin;

    /** The skin's panel picture, by the skin's index (Auto: the theme's light or dark glass). Half & Half is put
        together once from the dark glass and the face's light (or coloured) glass, split on a diagonal. */
    juce::Image classicPanel (const classicfaces::Face& f, int skin)
    {
        if (skin == 0)
            skin = theme::glassSkin() && theme::glassLight() ? 3 : 2;
        const juce::String key (f.key);
        const auto named = [&key] (const char* skinKey) { return skinImage (("classic_" + key + "_" + skinKey + "_jpg").toRawUTF8()); };
        switch (skin)
        {
            case 2: return named ("gd");
            case 3: return named ("gs");
            case 4: return named ("mg");
            case 5: return named ("mb");
            default: break;
        }
        static std::map<juce::String, juce::Image> halves;
        auto& image = halves[key];
        if (image.isNull())
        {
            const auto dark = named ("gd"), light = f.tinted ? named ("gc") : named ("gs");
            if (dark.isNull() || light.isNull())
                return dark;
            image = juce::Image (juce::Image::RGB, dark.getWidth(), dark.getHeight(), false);
            juce::Graphics g (image);
            g.drawImageAt (light, 0, 0);
            const auto w = (float) f.width, h = (float) classicfaces::height;
            juce::Path right;
            right.startNewSubPath (f.seamTop, 0.0f); right.lineTo (w, 0.0f); right.lineTo (w, h); right.lineTo (f.seamBottom, h); right.closeSubPath();
            {
                juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (right);
                g.drawImageAt (dark, 0, 0);
            }
            g.setColour (juce::Colours::white.withAlpha (0.47f));
            g.drawLine (f.seamTop, 0.0f, f.seamBottom, h, 2.0f);
            // (the Colour section and the meter stay whole, the seam passing behind them)
            juce::Path whole;
            whole.addRoundedRectangle (f.meterX - 192.0f, 290.0f, 384.0f, 168.0f, 20.0f);
            const auto mx0 = f.meterX - classicfaces::meterWidth * 0.5f;
            whole.addRoundedRectangle (mx0 - 6.0f, classicfaces::meterTop - 6.0f, classicfaces::meterWidth + 16.0f, classicfaces::meterHeight + 20.0f, 18.0f);
            g.reduceClipRegion (whole);
            g.drawImageAt (dark, 0, 0);
        }
        return image;
    }

    void addClassicDesigns (std::map<juce::String, Design>& d)
    {
        using namespace classicfaces;
        for (const auto& f : faces())
        {
            const auto s = std::min (0.8f, 1300.0f / (float) f.width);
            Design c;
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::white; c.value = juce::Colours::white; c.accent = juce::Colour (0xfff2a43a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.photoScale = s;
            c.width = juce::roundToInt ((float) f.width * s);
            c.height = juce::roundToInt ((float) height * s);
            c.meters = true;
            c.sparks = true;
            c.skins = true;
            const auto mx0 = f.meterX - meterWidth * 0.5f;
            c.liveArea = RF (mx0, meterTop, meterWidth, meterHeight).transformedBy (juce::AffineTransform::scale (s)).getSmallestIntegerContainer().expanded (2);
            for (const auto& it : f.items)
            {
                if (it.part == Part::toggle)
                {
                    Sprite sp;
                    sp.image = "classic_switch_up_png";
                    sp.offImage = "classic_switch_down_png";
                    sp.rect = RF (it.x - toggleSize * 0.5f, it.y - toggleSize * 0.5f, toggleSize, toggleSize);
                    sp.pivot = { it.x, it.y };
                    c.controls.push_back (photoControl (it.id, Kind::hotspot, sp, s));
                    continue;
                }
                const auto size = it.part == Part::big ? bigSize : it.part == Part::selector ? selectorSize : smallSize;
                const auto* image = it.part == Part::small ? "classic_small_png" : "classic_big_png";
                auto sp = turning (image, it.x, it.y, size * 0.5f, it.part == Part::small ? smallAngle : bigAngle, it.angles);
                sp.hitRadius = size * 0.5f;
                c.controls.push_back (photoControl (it.id, Kind::knob, sp, s));
            }
            c.paint = [&f, s] (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float)
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.setOpacity (1.0f);
                g.drawImage (classicPanel (f, paintingSkinValue), b.toFloat());
                // The power lamp
                const auto on = valueOf (fx, v, "power", 1.0f) > 0.5f;
                drawPart (g, on ? "classic_led_on_png" : "classic_led_off_png", RF ((float) f.width - 160.0f - 28.0f, 290.0f, 56.0f, 56.0f), s);
                // The needle: 0..1 across the meter's 0..20 dB (or the output's VU), behind the window's edges
                const auto k = meterWidth / meterPhotoWidth;
                const auto x0 = f.meterX - meterWidth * 0.5f;
                const juce::Point<float> pivot (x0 + needlePivotX * k, meterTop + needlePivotY * k);
                const auto angle = juce::degreesToRadians (needleAt0 + needlePerDb * 20.0f * juce::jlimit (0.0f, 1.08f, meterAt (0, 0.0f)));
                const auto tip = pivot + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (needleReach * k);
                juce::Graphics::ScopedSaveState keep (g);
                g.addTransform (juce::AffineTransform::scale (s));
                g.reduceClipRegion (RF (x0 + needleWindow[0] * k, meterTop + needleWindow[1] * k,
                                        (needleWindow[2] - needleWindow[0]) * k, (needleWindow[3] - needleWindow[1]) * k).getSmallestIntegerContainer());
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawLine ({ pivot.translated (1.5f, 2.0f), tip.translated (1.5f, 2.0f) }, 2.2f);
                g.setColour (juce::Colour (0xfff5f5f0));
                g.drawLine ({ pivot, tip }, 2.0f);
            };
            d[f.type] = c;
        }
    }

    void addPhotoDesigns (std::map<juce::String, Design>& d)
    {
        const auto setCommon = [] (Design& c)
        {
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::white; c.value = juce::Colours::white; c.accent = juce::Colours::white; c.track = juce::Colours::black;
            c.valueAbove = false;
        };
        {
            // Graphic EQ: the ten-band 500-series graphic, its caps slide along their slots (-12 to +12 dB)
            constexpr float s = 0.85f;
            Design c;
            setCommon (c);
            c.photoScale = s;
            c.width = 248; c.height = 872;
            static const char* ids[] { "b16k", "b8k", "b4k", "b2k", "b1k", "b500", "b250", "b125", "b63", "b31" };
            static const char* caps[] { "graphic_cap0_png", "graphic_cap1_png", "graphic_cap2_png", "graphic_cap3_png", "graphic_cap4_png",
                                        "graphic_cap5_png", "graphic_cap6_png", "graphic_cap7_png", "graphic_cap8_png", "graphic_cap9_png" };
            // (each cap's picture: where it sits in the photo, and its centre across)
            const float cap[10][5] { { 132, 158, 52, 28, 158.0f }, { 132, 232, 50, 26, 157.0f }, { 131, 306, 49, 26, 155.5f }, { 130, 380, 52, 28, 156.0f },
                                     { 129, 454, 51, 29, 154.5f }, { 131, 528, 49, 28, 155.5f }, { 130, 602, 50, 29, 155.0f }, { 130, 675, 51, 30, 155.5f },
                                     { 129, 749, 50, 31, 154.0f }, { 128, 822, 51, 32, 153.5f } };
            const float slotTop[10] { 159, 233, 307, 381, 455, 529, 603, 676, 750, 823 };
            for (int k = 0; k < 10; ++k)
            {
                Sprite sp;
                sp.image = caps[k];
                sp.rect = RF (cap[k][0], cap[k][1], cap[k][2], cap[k][3]);
                sp.pivot = { cap[k][4], cap[k][1] + cap[k][3] * 0.5f };
                sp.slides = true;
                sp.from = 61.0f;   // (the -12 line)
                sp.to = 223.0f;    // (the +12 line)
                auto ctl = photoControl (ids[k], Kind::fader, sp, s);
                ctl.bounds = onFace (RF (34.0f, slotTop[k] - 3.0f, 222.0f, 36.0f), s);
                c.controls.push_back (ctl);
            }
            c.controls.push_back (photoButton ("eqIn", RF (86.0f, 886.0f, 42.0f, 46.0f), s));
            // The module stands in a dark rack as wide as the window (whose bar is wider than the module),
            // so it keeps its own shape instead of being stretched
            constexpr int module = 248;
            c.width = 572;
            c.photoOffset = { (c.width - module) / 2, 0 };
            for (auto& ctl : c.controls)
                ctl.bounds.translate (c.photoOffset.x, c.photoOffset.y);
            c.paint = [offset = c.photoOffset] (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float)
            {
                // (the rack: dark, with the shadow the module casts on it)
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff202226), 0.0f, 0.0f, juce::Colour (0xff121315), 0.0f, (float) b.getHeight(), false));
                g.fillRect (b);
                const auto face = R (offset.x, offset.y, module, b.getHeight() - offset.y);
                for (int k = 1; k <= 6; ++k)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.07f));
                    g.fillRect (face.expanded (k, 0).translated (0, 0));
                }
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.setOpacity (1.0f);   // (drawImage uses the current colour's alpha)
                g.drawImage (skinImage ("graphic_panel_jpg"), face.toFloat());
                if (valueOf (fx, v, "eqIn", 1.0f) > 0.5f)
                {
                    juce::Graphics::ScopedSaveState keep (g);
                    g.addTransform (juce::AffineTransform::translation ((float) offset.x, (float) offset.y));
                    drawPart (g, "graphic_lamp_png", RF (190.0f, 886.0f, 28.0f, 28.0f), s);
                }
            };
            d["graphicEq"] = c;
        }
        {
            // Tube EQ: the passive program EQ's blue panel. The big knobs' pointer lines turn (the knobs stay as
            // photographed, lit as they were); the frequency switches turn whole; OFF/ON switches the tubes, with the lamp
            const float s = 1400.0f / 2152.0f;
            Design c;
            setCommon (c);
            c.photoScale = s;
            c.width = 1400; c.height = 407;
            const std::vector<float> scale { -140, -114, -86, -56, -27, 3, 32, 59, 87, 111, 135 };   // (where 0 to 10 are printed)
            c.controls = {
                photoControl ("lowBoost", Kind::knob, turning ("tube_ptr_lowBoost_png", 572, 158, 100, 128.2f, scale), s),
                photoControl ("lowAtten", Kind::knob, turning ("tube_ptr_lowAtten_png", 866, 158, 100, -1.6f, scale), s),
                photoControl ("highBoost", Kind::knob, turning ("tube_ptr_highBoost_png", 1272, 162, 100, 33.6f, scale), s),
                photoControl ("highAtten", Kind::knob, turning ("tube_ptr_highAtten_png", 1567, 163, 100, 88.1f, scale), s),
                photoControl ("bandwidth", Kind::knob, turning ("tube_ptr_bandwidth_png", 1071, 473, 100, -0.3f, { -135.0f, 135.0f }), s),
                photoControl ("lowFreq", Kind::knob, turning ("tube_knob_lowFreq_png", 724, 458, 116, 9.7f, { -47.6f, -15.0f, 18.4f, 52.5f }), s),
                photoControl ("highFreq", Kind::knob, turning ("tube_knob_highFreq_png", 1416, 460, 116, 0.0f, { -96.0f, -66.7f, -35.3f, 0.0f, 32.8f, 66.2f, 96.4f }), s),
                photoControl ("attenFreq", Kind::knob, turning ("tube_knob_attenFreq_png", 1856, 160, 96, -1.7f, { -37.0f, -3.0f, 33.0f }), s),
            };
            auto tubes = turning ("tube_knob_tubes_png", 1884, 469, 52, 50.0f, {});
            tubes.onAngle = 50.0f; tubes.offAngle = -4.0f;
            c.controls.push_back (photoControl ("tubes", Kind::hotspot, tubes, s));
            auto in = turning ("tube_knob_eqIn_png", 441, 463, 52, 0.0f, {});
            in.onAngle = 0.0f; in.offAngle = 180.0f;   // (the bat thrown the other way)
            c.controls.push_back (photoControl ("eqIn", Kind::hotspot, in, s));
            c.paint = [s] (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float)
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (skinImage ("tube_panel_jpg"), b.toFloat());
                // (the pilot lamp, as photographed lit; dark with the tubes off)
                if (valueOf (fx, v, "tubes", 1.0f) < 0.5f)
                    drawPart (g, "tube_lampoff_png", RF (1743.0f, 269.0f, 92.0f, 92.0f), s);
            };
            d["tubeEq"] = c;
        }
        {
            // Console EQ: the British channel's 1U panel. Gain knobs and the frequency rings: their pointer lines turn;
            // the preamp gain and the high pass (finned knobs) turn whole. The right-hand buttons are Silk: Red and Blue
            constexpr float s = 1.0f;
            Design c;
            setCommon (c);
            c.photoScale = s;
            c.width = 1396; c.height = 194;
            c.meters = true;
            const std::vector<float> gain { 57.0f, 303.0f };   // (minus at upper right, round the bottom, plus at upper left)
            auto midGain = turning ("console_ptr_midGain_png", 418, 104, 43, 178.4f, gain);
            midGain.hitRadius = 40.0f;
            auto lowGain = turning ("console_ptr_lowGain_png", 573, 104, 43, 181.4f, gain);
            lowGain.hitRadius = 40.0f;
            c.controls = {
                photoControl ("drive", Kind::knob, turning ("console_knob_drive_png", 109, 101, 60, 180.0f, { 198.0f, 484.0f }), s),
                photoControl ("highGain", Kind::knob, turning ("console_ptr_highGain_png", 262, 101, 44, 181.8f, gain), s),
                photoControl ("midFreq", Kind::knob, turning ("console_ptr_midFreq_png", 418, 104, 60, 180.5f, { 116.0f, 150.0f, 180.0f, 214.0f, 244.0f, 273.0f }), s),
                photoControl ("midGain", Kind::knob, midGain, s),
                photoControl ("lowFreq", Kind::knob, turning ("console_ptr_lowFreq_png", 573, 104, 60, 180.3f, { 118.0f, 150.0f, 180.0f, 215.0f }), s),
                photoControl ("lowGain", Kind::knob, lowGain, s),
                photoControl ("hpf", Kind::knob, turning ("console_knob_hpf_png", 726, 102, 61, 178.0f, { 88.0f, 117.0f, 147.0f, 180.0f, 214.0f }), s),
                photoControl ("output", Kind::knob, turning ("console_knob_output_png", 1125, 104, 28, 180.0f, { 45.0f, 315.0f }), s),
                photoButton ("phase", RF (823, 29, 33, 64), s),
                photoButton ("eqIn", RF (823, 97, 33, 65), s),
                photoButton ("silk", RF (876, 32, 32, 63), s, 1),
                photoButton ("silk", RF (876, 99, 32, 65), s, 2),
            };
            c.paint = [] (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float)
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (skinImage ("console_panel_jpg"), b.toFloat());
                const auto led = [&g] (const char* colour, float cx, float cy) { drawPart (g, colour, RF (cx - 13.0f, cy - 13.0f, 26.0f, 26.0f), s); };
                // (a latched button sits a little deeper)
                if (valueOf (fx, v, "phase") > 0.5f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.16f));
                    g.fillRect (RF (823, 29, 33, 64));
                }
                if (valueOf (fx, v, "eqIn", 1.0f) > 0.5f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.12f));
                    g.fillRect (RF (823, 97, 33, 65));
                    led ("console_led_green_png", 1015, 99);
                }
                const auto silk = juce::roundToInt (valueOf (fx, v, "silk"));
                if (silk == 1) drawPart (g, "console_cap_red_png", RF (873, 29, 38, 69), s);
                if (silk == 2) drawPart (g, "console_cap_blue_png", RF (873, 96, 38, 71), s);
                // The output meter: dBu on the panel (0 dBFS = +24 dBu)
                const auto db = meterAt (0) + 24.0f;
                const struct { float at, x, y; const char* colour; } meter[] {
                    { -26.0f, 1039, 167, "console_led_green_png" }, { -10.0f, 1065, 167, "console_led_green_png" },
                    { 0.0f, 1092, 167, "console_led_yellow_png" }, { 5.0f, 1119, 166, "console_led_yellow_png" },
                    { 14.0f, 1145, 166, "console_led_yellow_png" }, { 18.0f, 1172, 166, "console_led_red_png" },
                    { 23.5f, 1199, 166, "console_led_red_png" } };
                for (const auto& m : meter)
                    if (db >= m.at)
                        led (m.colour, m.x, m.y);
            };
            d["consoleEq"] = c;
        }
    }

    //==============================================================================
    // The Compressor's circuits, each on the photographed front panel of the hardware it's modelled on (cut from the
    // photos by Tools/compressor_skins, the makers' names taken off): its knobs, keys, switches, lamps and meters.
    //==============================================================================
    namespace comp
    {
        constexpr int width = 1300, tabs = 46;
        enum Circuit { platinum, studioVca, studioFet, classicVca, vintageVca, vintageFet, vintageOpto };

        const photo::Part& part (const char* name)
        {
            static const photo::Part none {};
            for (const auto& p : photo::parts)
                if (std::strcmp (p.name, name) == 0)
                    return p;
            jassertfalse;
            return none;
        }
        const photo::Face& faceOf (const char* name)
        {
            for (const auto& f : photo::faces)
                if (std::strcmp (f.name, name) == 0)
                    return f;
            return photo::faces[0];
        }

        /** Where a face's photo sits in the window: its scale and top left. */
        struct Page
        {
            const char* face;
            float scale;
            juce::Point<float> origin;
            int height;
            juce::Point<float> photoOrigin {};   // (the panel's top left in the photo)
            juce::Point<float> at (float x, float y) const { return origin + juce::Point<float> (x, y) * scale; }
            /** A place given in the photo's own pixels (before its edges were trimmed). */
            RF photo (float x, float y, float w, float h) const { return rect (x - photoOrigin.x, y - photoOrigin.y, w, h); }
            RF rect (float x, float y, float w, float h) const { return RF (origin.x + x * scale, origin.y + y * scale, w * scale, h * scale); }
            std::string key (const char* p) const { return std::string (face) + "." + p; }
        };

        Page pageFor (int circuit)
        {
            static const char* names[] { "plat", "svca", "sfet", "cvca", "vvca", "vfet", "vopto" };
            const auto& f = faceOf (names[juce::jlimit (0, 6, circuit)]);
            Page p { f.name, 1.0f, {}, 0 };
            p.photoOrigin = { (float) f.originX, (float) f.originY };
            if (f.height > f.width)
            {
                // (the 500-series module: upright, in a rack as wide as the window)
                p.scale = 560.0f / (float) f.height;
                p.origin = { ((float) width - (float) f.width * p.scale) * 0.5f, (float) tabs };
                p.height = tabs + 560;
            }
            else
            {
                p.scale = (float) width / (float) f.width;
                p.origin = { 0.0f, (float) tabs };
                p.height = tabs + juce::roundToInt ((float) f.height * p.scale);
            }
            return p;
        }

        void image (juce::Graphics& g, const char* name, RF where, float alpha = 1.0f)
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.setOpacity (alpha);
            g.drawImage (skinImage (name), where);
            g.setOpacity (1.0f);
        }

        /** A lamp (or a lit key, a pushed key): drawn when on. */
        void lamp (juce::Graphics& g, const Page& pg, const char* name, bool on = true)
        {
            if (! on)
                return;
            const auto& p = part (pg.key (name).c_str());
            if (p.kind == photo::region)
                image (g, p.image, pg.rect ((float) p.x, (float) p.y, (float) p.w, (float) p.h));
            else
                image (g, p.image, pg.rect (p.cx - p.R, p.cy - p.R, p.R * 2.0f, p.R * 2.0f));
        }

        /** A bar meter lit from its foot up to the fraction (or from the top down, gain reduction). */
        void bar (juce::Graphics& g, const Page& pg, const char* name, float fraction, bool fromTop = false)
        {
            const auto& p = part (pg.key (name).c_str());
            const auto r = pg.rect ((float) p.x, (float) p.y, (float) p.w, (float) p.h);
            const auto f = juce::jlimit (0.0f, 1.0f, fraction);
            if (f <= 0.0f)
                return;
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion ((fromTop ? r.withHeight (r.getHeight() * f) : r.withTrimmedTop (r.getHeight() * (1.0f - f))).getSmallestIntegerContainer());
            image (g, p.image, r);
        }

        /** A lit arc round a knob, from its lowest setting's angle to the setting's. */
        void arc (juce::Graphics& g, const Page& pg, const char* name, float from, float to)
        {
            const auto& p = part (pg.key (name).c_str());
            const auto c = pg.at (p.cx, p.cy);
            const auto r = (p.R + 2.0f) * pg.scale;
            juce::Path wedge;
            wedge.addPieSegment (RF (c.x - r, c.y - r, r * 2.0f, r * 2.0f), juce::degreesToRadians (from), juce::degreesToRadians (to), 0.0f);
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (wedge);
            image (g, p.image, pg.rect (p.cx - p.R, p.cy - p.R, p.R * 2.0f, p.R * 2.0f));
        }

        /** A meter's needle at an angle (degrees clockwise from straight up), behind the meter's window. */
        void needle (juce::Graphics& g, const Page& pg, const char* name, float degrees, const char* imageFrom = nullptr)
        {
            const auto& p = part (pg.key (name).c_str());
            const auto img = skinImage (imageFrom != nullptr ? part (imageFrom).image : p.image);
            if (! img.isValid())
                return;
            juce::Graphics::ScopedSaveState keep (g);
            if (p.wx1 > p.wx0)
                g.reduceClipRegion (pg.rect ((float) p.wx0, (float) p.wy0, (float) (p.wx1 - p.wx0), (float) (p.wy1 - p.wy0)).getSmallestIntegerContainer());
            const auto c = pg.at (p.cx, p.cy);
            const auto s = p.R * 2.0f * pg.scale / (float) img.getWidth();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageTransformed (img, juce::AffineTransform::scale (s).translated (c.x - p.R * pg.scale, c.y - p.R * pg.scale)
                                                .rotated (juce::degreesToRadians (degrees), c.x, c.y));
        }

        /** A VU meter's angle for a reading: the scale's -20, 0 and +3 marks' angles, the usual VU spacing between. */
        float vuAngle (float vu, float at20, float at0, float at3)
        {
            static const std::pair<float, float> marks[] { { -20.0f, 0.0f }, { -10.0f, 0.25f }, { -7.0f, 0.39f }, { -5.0f, 0.52f },
                                                           { -3.0f, 0.69f }, { -2.0f, 0.78f }, { -1.0f, 0.88f }, { 0.0f, 1.0f } };
            if (vu >= 0.0f)
                return at0 + juce::jmin (vu, 3.6f) / 3.0f * (at3 - at0);
            vu = juce::jmax (vu, -22.0f);
            float f = 0.0f;
            for (size_t i = 1; i < std::size (marks); ++i)
                if (vu <= marks[i].first)
                {
                    const auto t = (vu - marks[i - 1].first) / (marks[i].first - marks[i - 1].first);
                    f = marks[i - 1].second + t * (marks[i].second - marks[i - 1].second);
                    break;
                }
            if (vu < -20.0f)
                f = (vu + 20.0f) * 0.02f;
            return at20 + f * (at0 - at20);
        }

        /** A needle's reading eased as a meter's needle swings (each needle its own), in its units. */
        float ballistics (const std::string& which, float target, float rise = 0.45f, float fall = 0.2f)
        {
            static std::map<std::string, float> shown;
            auto& v = shown.emplace (which, target).first->second;
            v += (target - v) * (target > v ? rise : fall);
            return v;
        }

        /** What the VU shows: gain reduction (0 dB at 0 VU, down from there), or the output against +4/+8, or rest. */
        float vuReading (int meterMode, float grDb, float outDb)
        {
            switch (meterMode)
            {
                case 0:  return -grDb;
                case 1:  return outDb + 18.0f;
                case 2:  return outDb + 14.0f;
                default: return -24.0f;
            }
        }

        /** The FET limiters' ratio keys: which one a ratio is (as the engine steps it). */
        float fetRatio (float r) { return r < 6.0f ? 4.0f : r < 10.0f ? 8.0f : r < 16.0f ? 12.0f : r < 25.0f ? 20.0f : 30.0f; }

        float meter (int index, float fallback = -100.0f)
        {
            int count = 0;
            const auto* m = paintingMeters (count);
            return m != nullptr && index < count ? m[index] : fallback;
        }

        // The controls: a knob, its pointer (or the whole knob) turned to the setting on its printed scale
        Control knob (const Page& pg, int pageNumber, const char* id, const char* partName, std::vector<std::pair<float, float>> map,
                      bool stepped = false, bool logMap = false, float photographed = std::numeric_limits<float>::quiet_NaN())
        {
            const auto& p = part (pg.key (partName).c_str());
            Sprite s;
            s.image = p.image;
            s.rect = RF (p.cx - p.R, p.cy - p.R, p.R * 2.0f, p.R * 2.0f);
            s.pivot = { p.cx, p.cy };
            s.angle = std::isnan (photographed) ? p.angle : photographed;
            s.map = std::move (map);
            s.stepped = stepped;
            s.logMap = logMap;
            s.scale = pg.scale;
            s.offset = pg.origin;
            // The knobs whose light would turn with them (their caps' shading, a bar's reflections) stay as photographed;
            // only their pointer - or the bar - is drawn, at the setting
            const juce::String face (pg.face), name (partName);
            const auto drawn = [&s, &p] (Sprite::Drawn kind, juce::Colour ink, float inner, float outer, float thickness)
            {
                s.drawn = kind; s.ink = ink; s.inner = inner; s.outer = outer; s.thickness = thickness;
                s.radius = p.R; s.image = nullptr;
            };
            if (face == "svca")
                drawn (Sprite::Drawn::bar, {}, 0.0f, 1.0f, 0.46f);
            else if (face == "cvca")
                drawn (Sprite::Drawn::line, juce::Colour (0xff141414), 0.06f, 0.9f, 0.1f);
            else if (face == "vvca")
                drawn (Sprite::Drawn::line, juce::Colour (0xfff4f4f2), 0.25f, 0.95f, 0.16f);
            else if (face == "sfet" && name != "input" && name != "output")
                drawn (Sprite::Drawn::dot, juce::Colour (0xff2e2e2e), 0.0f, 0.58f, 0.26f);
            else if (face == "vfet" && name != "input" && name != "output")
                drawn (Sprite::Drawn::dot, juce::Colour (0xff2a2a2a), 0.0f, 0.6f, 0.28f);
            Control c;
            c.id = id;
            c.kind = Kind::knob;
            c.label = "-";
            c.page = pageNumber;
            c.bounds = pg.rect (p.cx - p.R, p.cy - p.R, p.R * 2.0f, p.R * 2.0f).getSmallestIntegerContainer();
            c.sprite = s;
            return c;
        }
        /** ... with no picture of its own (its arc, or the face, shows it): just where it's dragged. */
        Control blindKnob (const Page& pg, int pageNumber, const char* id, float cx, float cy, float r, std::vector<std::pair<float, float>> map, bool logMap = false)
        {
            cx -= pg.photoOrigin.x;   // (its place in the photo's pixels)
            cy -= pg.photoOrigin.y;
            Sprite s;
            s.rect = RF (cx - r, cy - r, r * 2.0f, r * 2.0f);
            s.pivot = { cx, cy };
            s.map = std::move (map);
            s.logMap = logMap;
            s.scale = pg.scale;
            s.offset = pg.origin;
            Control c;
            c.id = id;
            c.kind = Kind::knob;
            c.label = "-";
            c.page = pageNumber;
            c.bounds = pg.rect (cx - r, cy - r, r * 2.0f, r * 2.0f).getSmallestIntegerContainer();
            c.sprite = s;
            return c;
        }
        /** An invisible key over the photo's (the face lights its lamp). */
        Control key (const Page& pg, int pageNumber, const char* id, float x, float y, float w, float h)
        {
            Sprite s;
            s.rect = RF (x, y, w, h);
            s.pivot = s.rect.getCentre();
            s.scale = pg.scale;
            s.offset = pg.origin;
            Control c;
            c.id = id;
            c.kind = Kind::hotspot;
            c.label = "-";
            c.page = pageNumber;
            c.bounds = pg.rect (x, y, w, h).getSmallestIntegerContainer();
            c.sprite = s;
            return c;
        }
        Control keyAt (const Page& pg, int pageNumber, const char* id, const char* partName, float grow = 1.0f)
        {
            const auto& p = part (pg.key (partName).c_str());
            if (p.kind == photo::region)
                return key (pg, pageNumber, id, (float) p.x, (float) p.y, (float) p.w, (float) p.h);
            const auto r = p.R * grow;
            return key (pg, pageNumber, id, p.cx - r, p.cy - r, r * 2.0f, r * 2.0f);
        }
        /** A toggle switch, photographed bat up and thrown down (up: on, unless upIsOn is false). */
        Control toggle (const Page& pg, int pageNumber, const char* id, const char* partName, bool upIsOn = true)
        {
            const auto& up = part (pg.key ((juce::String (partName) + "_up").toRawUTF8()).c_str());
            const auto& down = part (pg.key ((juce::String (partName) + "_down").toRawUTF8()).c_str());
            auto c = key (pg, pageNumber, id, up.cx - up.R, up.cy - up.R, up.R * 2.0f, up.R * 2.0f);
            c.sprite.image = upIsOn ? up.image : down.image;
            c.sprite.offImage = upIsOn ? down.image : up.image;
            c.sprite.pivot = { up.cx, up.cy };
            return c;
        }

        void panel (juce::Graphics& g, R b, const Page& pg)
        {
            // (the rack round the photo)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1c1f), 0.0f, (float) tabs, juce::Colour (0xff0d0e10), 0.0f, (float) b.getBottom(), false));
            g.fillRect (b.withTrimmedTop (tabs));
            const auto& f = faceOf (pg.face);
            image (g, f.panel, pg.rect (0.0f, 0.0f, (float) f.width, (float) f.height));
        }
    }

    void addCompressorFaces (std::map<juce::String, Design>& d)
    {
        using namespace comp;
        Design c;
        c.width = width;
        c.knobs = KnobStyle::black;
        c.label = juce::Colours::white; c.value = juce::Colours::white; c.accent = juce::Colour (0xffffb020); c.track = juce::Colours::black;
        c.valueAbove = false;
        c.meters = true;
        c.live = true;
        c.pageParam = "model";
        c.photoScale = 1.0f;
        for (int k = 0; k < 7; ++k)
            c.pageSizes.push_back ({ width, pageFor (k).height });
        c.height = c.pageSizes[0].y;
        // The circuits, along the top: a tap shows that one's face
        c.controls.push_back ({ "model", Kind::segmented, R (8, 7, width - 16, 32), "-" });

        const auto nan = std::numeric_limits<float>::quiet_NaN();
        auto& cs = c.controls;
        //------------------------------------------------------------------ Platinum Digital: the blue interface
        {
            const auto pg = pageFor (platinum);
            constexpr int n = platinum + 1;
            // gold knobs: threshold in steps (the ring's lit number), ratio
            cs.push_back (blindKnob (pg, n, "threshold", 340.0f, 106.7f, 34.0f, { { -150.0f, 0.0f }, { 150.0f, -60.0f } }));
            cs.push_back (blindKnob (pg, n, "ratio", 441.7f, 106.7f, 34.0f, { { -150.0f, 1.0f }, { 150.0f, 30.0f } }, true));
            cs.push_back (blindKnob (pg, n, "input", 676.7f, 106.7f, 38.0f, { { -135.0f, -30.0f }, { 135.0f, 30.0f } }));
            cs.push_back (blindKnob (pg, n, "output", 1193.7f, 103.0f, 37.0f, { { -135.0f, -30.0f }, { 135.0f, 30.0f } }));
            cs.push_back (blindKnob (pg, n, "makeup", 1290.6f, 95.6f, 24.0f, { { -135.0f, -30.0f }, { 135.0f, 30.0f } }));
            cs.push_back (blindKnob (pg, n, "mix", 1375.0f, 95.6f, 24.0f, { { -135.0f, 0.0f }, { 135.0f, 100.0f } }));
            cs.push_back (blindKnob (pg, n, "attack", 469.3f, 220.0f, 12.0f, { { -135.0f, 0.05f }, { 135.0f, 200.0f } }, true));
            cs.push_back (blindKnob (pg, n, "release", 469.3f, 267.3f, 12.0f, { { -135.0f, 5.0f }, { 135.0f, 3000.0f } }, true));
            cs.push_back (blindKnob (pg, n, "knee", 766.0f, 267.0f, 13.0f, { { -135.0f, 0.0f }, { 135.0f, 1.0f } }));
            cs.push_back (blindKnob (pg, n, "scHpf", 808.0f, 267.0f, 13.0f, { { -135.0f, 20.0f }, { 135.0f, 20000.0f } }, true));
            cs.push_back (blindKnob (pg, n, "scQ", 850.0f, 267.0f, 13.0f, { { -135.0f, 0.1f }, { 135.0f, 10.0f } }, true));
            cs.push_back (blindKnob (pg, n, "scGain", 892.0f, 267.0f, 13.0f, { { -135.0f, -24.0f }, { 135.0f, 24.0f } }));
            cs.push_back (blindKnob (pg, n, "limThr", 934.0f, 267.0f, 13.0f, { { -135.0f, -20.0f }, { 135.0f, 0.0f } }));
            const std::pair<const char*, const char*> toggles[] { { "peakRms", "kRms" }, { "detect", "kSum" }, { "scFilter", "kScFilter" },
                                                                  { "autoRelease", "kAutoRel" }, { "listen", "kListen" }, { "limiter", "kLimit" },
                                                                  { "meterHold", "kOhm" }, { "console", "kConsole" }, { "air", "kAir" } };
            for (const auto& [id, lampName] : toggles)
            {
                const auto& p = part (pg.key (lampName).c_str());
                cs.push_back (key (pg, n, id, (float) p.x, (float) p.y - 16.0f, (float) p.w, 26.0f));
            }
            {
                const auto& p = part (pg.key ("kDrive").c_str());
                auto k = key (pg, n, "distortion", (float) p.x, (float) p.y - 16.0f, (float) p.w, 26.0f);
                k.sprite.cycle = true;
                cs.push_back (k);
            }
            const char* modes[] { "kLP", "kBP", "kHP", "kEQ", "kHS" };
            for (int m = 0; m < 5; ++m)
            {
                const auto& p = part (pg.key (modes[m]).c_str());
                auto k = key (pg, n, "scMode", (float) p.x, (float) p.y - 16.0f, (float) p.w, 26.0f);
                k.sprite.choice = m; k.sprite.radio = true;
                cs.push_back (k);
            }
            const char* gains[] { "kOff", "k0dB", "k12dB" };
            for (int m = 0; m < 3; ++m)
            {
                const auto& p = part (pg.key (gains[m]).c_str());
                auto k = key (pg, n, "autoGain", (float) p.x, (float) p.y - 16.0f, (float) p.w, 26.0f);
                k.sprite.choice = m; k.sprite.radio = true;
                cs.push_back (k);
            }
            cs.push_back (keyAt (pg, n, "power", "power"));
        }
        //------------------------------------------------------------------ Studio VCA: the red dual compressor
        {
            const auto pg = pageFor (studioVca);
            constexpr int n = studioVca + 1;
            // (both channels' knobs set the stereo compressor: as the hardware's lower controls with Stereo on)
            const std::vector<std::pair<float, float>> ratio { { 40.0f, 1.5f }, { 80.0f, 2.0f }, { 115.0f, 3.0f }, { 160.0f, 5.0f }, { 205.0f, 7.0f }, { 250.0f, 10.0f } };
            const std::vector<std::pair<float, float>> thresh { { -150.0f, -48.0f }, { 150.0f, -12.0f } };
            const std::vector<std::pair<float, float>> gain { { -150.0f, 0.0f }, { 150.0f, 20.0f } };
            const std::vector<std::pair<float, float>> attack { { -135.0f, 0.3f }, { 135.0f, 100.0f } };
            const std::vector<std::pair<float, float>> release { { -135.0f, 100.0f }, { 135.0f, 3000.0f } };
            const std::vector<std::pair<float, float>> limit { { -150.0f, -18.0f }, { 150.0f, 0.0f } };
            const std::pair<const char*, float> bars[] { { "ratio1", 161.2f }, { "ratio2", 208.8f }, { "thresh1", -44.0f }, { "thresh2", -147.6f },
                                                         { "gain1", -107.2f }, { "gain2", -125.5f }, { "attack1", 14.3f }, { "attack2", -42.5f },
                                                         { "release1", -26.4f }, { "release2", 129.7f }, { "limThr1", 142.6f }, { "limThr2", 116.6f } };
            for (const auto& [name, photographed] : bars)
            {
                const juce::String nm (name);
                const auto id = nm.startsWith ("ratio") ? "ratio" : nm.startsWith ("thresh") ? "threshold" : nm.startsWith ("gain") ? "makeup"
                              : nm.startsWith ("attack") ? "attack" : nm.startsWith ("release") ? "release" : "limThr";
                const auto& map = nm.startsWith ("ratio") ? ratio : nm.startsWith ("thresh") ? thresh : nm.startsWith ("gain") ? gain
                                : nm.startsWith ("attack") ? attack : nm.startsWith ("release") ? release : limit;
                cs.push_back (knob (pg, n, id, name, map, false, nm.startsWith ("ratio") || nm.startsWith ("attack") || nm.startsWith ("release"), photographed));
            }
            for (const auto* k : { "in1", "in2" })   cs.push_back (keyAt (pg, n, "compIn", k, 1.3f));
            for (const auto* k : { "key1", "key2" }) cs.push_back (keyAt (pg, n, "extSc", k, 1.3f));
            for (const auto* k : { "auto1", "auto2" }) cs.push_back (keyAt (pg, n, "autoRelease", k, 1.3f));
            for (const auto* k : { "limIn1", "limIn2" }) cs.push_back (keyAt (pg, n, "limiter", k, 1.3f));
            cs.push_back (keyAt (pg, n, "link", "stereo", 1.3f));
            cs.push_back (keyAt (pg, n, "power", "power", 1.1f));
        }
        //------------------------------------------------------------------ Studio FET: the silver FET limiter
        {
            const auto pg = pageFor (studioFet);
            constexpr int n = studioFet + 1;
            const std::vector<std::pair<float, float>> io { { -150.0f, -30.0f }, { -115.0f, -24.0f }, { -73.0f, -12.0f }, { -36.0f, -6.0f },
                                                            { 3.0f, 0.0f }, { 42.0f, 6.0f }, { 77.0f, 12.0f }, { 114.0f, 18.0f }, { 150.0f, 24.0f } };
            cs.push_back (knob (pg, n, "input", "input", io));
            cs.push_back (knob (pg, n, "output", "output", io));
            // (SLOW at the lower left round to FAST at the lower right; past SLOW, at the bottom: OFF)
            auto attack = knob (pg, n, "attack", "attack", { { -135.0f, 200.0f }, { -45.0f, 60.0f }, { 45.0f, 10.0f }, { 135.0f, 0.05f } });
            attack.sprite.offBelow = -150.0f; attack.sprite.offParam = "compIn";   // (OFF: the compressor out, the amplifier in)
            cs.push_back (attack);
            cs.push_back (knob (pg, n, "release", "release", { { -135.0f, 3000.0f }, { -45.0f, 900.0f }, { 45.0f, 200.0f }, { 135.0f, 5.0f } }));
            cs.push_back (knob (pg, n, "mix", "mix", { { -135.0f, 100.0f }, { 135.0f, 0.0f } }));
            auto hpf = knob (pg, n, "scHpf", "scHpf", { { -135.0f, 30.0f }, { 135.0f, 300.0f } }, false, true);
            hpf.sprite.offBelow = -150.0f; hpf.sprite.offParam = "scFilter";
            cs.push_back (hpf);
            cs.push_back (toggle (pg, n, "impedance", "tg_zin", false));
            cs.push_back (toggle (pg, n, "power", "tg_bypass"));
            cs.push_back (toggle (pg, n, "link", "tg_link"));
            cs.push_back (toggle (pg, n, "extSc", "tg_extSc"));
            const std::pair<const char*, float> ratios[] { { "r20", 20.0f }, { "r12", 12.0f }, { "r8", 8.0f }, { "r4", 4.0f } };
            for (const auto& [name, value] : ratios)
            {
                auto k = keyAt (pg, n, "ratio", name);
                k.sprite.setValue = value; k.sprite.altValue = 30.0f;   // (shift-click: all the buttons in)
                cs.push_back (k);
            }
            const std::pair<const char*, int> meters[] { { "mGR", 0 }, { "m8", 2 }, { "m4", 1 }, { "mOff", 3 } };
            for (const auto& [name, mode] : meters)
            {
                auto k = keyAt (pg, n, "meterMode", name);
                k.sprite.choice = mode; k.sprite.radio = true;
                cs.push_back (k);
            }
        }
        //------------------------------------------------------------------ Classic VCA: the 1U bus compressor
        {
            const auto pg = pageFor (classicVca);
            constexpr int n = classicVca + 1;
            cs.push_back (knob (pg, n, "threshold", "threshold", { { -135.0f, -40.0f }, { 135.0f, 0.0f } }));
            cs.push_back (knob (pg, n, "attack", "attack", { { -84.0f, 0.1f }, { -47.0f, 0.3f }, { -18.0f, 1.0f }, { 15.0f, 3.0f }, { 44.0f, 10.0f }, { 82.0f, 30.0f } }, true));
            cs.push_back (knob (pg, n, "ratio", "ratio", { { -79.0f, 1.5f }, { -47.0f, 2.0f }, { -17.0f, 3.0f }, { 14.0f, 4.0f }, { 43.0f, 5.0f }, { 79.0f, 10.0f } }, true));
            auto rel = knob (pg, n, "release", "release", { { -84.0f, 100.0f }, { -46.0f, 200.0f }, { -15.0f, 400.0f }, { 15.0f, 800.0f }, { 45.0f, 1600.0f }, { 83.0f, 1600.0f } }, true);
            rel.sprite.autoParam = "autoRelease";
            cs.push_back (rel);
            cs.push_back (knob (pg, n, "makeup", "makeup", { { -135.0f, 0.0f }, { 135.0f, 20.0f } }));
            cs.push_back (knob (pg, n, "scHpfStep", "scHpf", { { -84.0f, 0.0f }, { -47.0f, 1.0f }, { -15.0f, 2.0f }, { 15.0f, 3.0f }, { 45.0f, 4.0f }, { 83.0f, 5.0f } }, true));
            cs.push_back (knob (pg, n, "vcaDrive", "vcaDrive", { { -135.0f, 0.0f }, { 135.0f, 1.0f } }));
            cs.push_back (keyAt (pg, n, "compIn", "kComp"));
            cs.push_back (keyAt (pg, n, "extSc", "kExt"));
            cs.push_back (keyAt (pg, n, "power", "power"));
        }
        //------------------------------------------------------------------ Vintage VCA: the 500-series bus compressor
        {
            const auto pg = pageFor (vintageVca);
            constexpr int n = vintageVca + 1;
            cs.push_back (knob (pg, n, "threshold", "threshold", { { -135.0f, -40.0f }, { 135.0f, 0.0f } }));
            cs.push_back (knob (pg, n, "makeup", "makeup", { { -135.0f, -5.0f }, { 135.0f, 15.0f } }));
            cs.push_back (knob (pg, n, "attack", "attack", { { -87.0f, 0.1f }, { -50.0f, 0.3f }, { -15.0f, 1.0f }, { 19.0f, 3.0f }, { 53.0f, 10.0f }, { 87.0f, 30.0f } }, true));
            auto rel = knob (pg, n, "release", "release", { { -88.0f, 100.0f }, { -53.0f, 200.0f }, { -22.0f, 400.0f }, { 18.0f, 800.0f }, { 53.0f, 1600.0f }, { 88.0f, 1600.0f } }, true);
            rel.sprite.autoParam = "autoRelease";
            cs.push_back (rel);
            cs.push_back (knob (pg, n, "ratio", "ratio", { { -86.0f, 1.5f }, { -52.0f, 2.0f }, { -27.0f, 3.0f }, { 15.0f, 4.0f }, { 48.0f, 5.0f }, { 85.0f, 10.0f } }, true));
            cs.push_back (knob (pg, n, "scHpfStep", "scHpf", { { -84.0f, 0.0f }, { -52.0f, 1.0f }, { -23.0f, 2.0f }, { 24.0f, 3.0f }, { 51.0f, 4.0f }, { 84.0f, 5.0f } }, true));
            cs.push_back (keyAt (pg, n, "compIn", "kIn"));
        }
        //------------------------------------------------------------------ Vintage FET: the black FET limiter
        {
            const auto pg = pageFor (vintageFet);
            constexpr int n = vintageFet + 1;
            const std::vector<std::pair<float, float>> io { { -155.0f, -30.0f }, { -118.0f, -24.0f }, { -73.0f, -12.0f }, { -34.0f, -6.0f },
                                                            { 5.0f, 0.0f }, { 43.0f, 6.0f }, { 76.0f, 12.0f }, { 115.0f, 18.0f }, { 153.0f, 24.0f } };
            cs.push_back (knob (pg, n, "input", "input", io));
            cs.push_back (knob (pg, n, "output", "output", io));
            auto attack = knob (pg, n, "attack", "attack", { { -135.0f, 200.0f }, { -45.0f, 60.0f }, { 45.0f, 10.0f }, { 135.0f, 0.05f } });
            attack.sprite.offBelow = -150.0f; attack.sprite.offParam = "compIn";
            cs.push_back (attack);
            cs.push_back (knob (pg, n, "release", "release", { { -135.0f, 3000.0f }, { -45.0f, 900.0f }, { 45.0f, 200.0f }, { 135.0f, 5.0f } }));
            const std::pair<const char*, float> ratios[] { { "r20", 20.0f }, { "r12", 12.0f }, { "r8", 8.0f }, { "r4", 4.0f } };
            for (const auto& [name, value] : ratios)
            {
                auto k = keyAt (pg, n, "ratio", name);
                k.sprite.setValue = value; k.sprite.altValue = 30.0f;
                cs.push_back (k);
            }
            const std::pair<const char*, int> meters[] { { "mGR", 0 }, { "m8", 2 }, { "m4", 1 }, { "mOff", 3 } };
            for (const auto& [name, mode] : meters)
            {
                auto k = keyAt (pg, n, "meterMode", name);
                k.sprite.choice = mode; k.sprite.radio = true;
                cs.push_back (k);
            }
        }
        //------------------------------------------------------------------ Vintage Opto: the grey levelling amplifier
        {
            const auto pg = pageFor (vintageOpto);
            constexpr int n = vintageOpto + 1;
            cs.push_back (knob (pg, n, "makeup", "gain", { { -142.0f, -30.0f }, { 3.0f, 0.0f }, { 135.0f, 30.0f } }));
            cs.push_back (knob (pg, n, "threshold", "peak", { { -142.0f, 0.0f }, { 135.0f, -50.0f } }));
            cs.push_back (knob (pg, n, "meterMode", "meterSel", { { -45.0f, 2.0f }, { 0.0f, 0.0f }, { 45.0f, 1.0f } }, true, false, 0.0f));
            cs.push_back (knob (pg, n, "emphasis", "r37", { { -135.0f, 0.0f }, { 135.0f, 1.0f } }, false, false, 0.0f));
            // (push buttons, lit when in)
            for (const auto& [id, name] : { std::pair { "power", "btPower" }, std::pair { "optoLimit", "btLimit" } })
            {
                const auto& lit = part (pg.key ((juce::String (name) + "_on").toRawUTF8()).c_str());
                const auto& dark = part (pg.key ((juce::String (name) + "_off").toRawUTF8()).c_str());
                auto b = key (pg, n, id, lit.cx - lit.R, lit.cy - lit.R, lit.R * 2.0f, lit.R * 2.0f);
                b.sprite.image = lit.image;
                b.sprite.offImage = dark.image;
                b.sprite.pivot = { lit.cx, lit.cy };
                cs.push_back (b);
            }
        }

        c.paint = [] (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float live)
        {
            const auto circuit = juce::jlimit (0, 6, juce::roundToInt (valueOf (fx, v, "model", 1.0f)));
            const auto pg = pageFor (circuit);
            // the tabs' strip
            g.setColour (juce::Colour (0xff141518));
            g.fillRect (b.withHeight (tabs));
            panel (g, b, pg);

            const auto on = [&] (const char* id, float fallback = 0.0f) { return valueOf (fx, v, id, fallback) > 0.5f; };
            const auto choice = [&] (const char* id) { return juce::roundToInt (valueOf (fx, v, id)); };
            const auto powered = on ("power", 1.0f);
            const auto gr = std::abs (meter (0, live));
            const auto outDb = std::max (meter (3), meter (4));
            const auto inL = meter (1), inR = meter (2), outL = meter (3), outR = meter (4);
            const auto limiting = meter (5, 0.0f);
            const auto mode = choice ("meterMode");
            const auto key = [&] (const char* n) { return std::string (pg.face) + n; };

            switch (circuit)
            {
                case platinum:
                {
                    if (! powered)
                        break;
                    lamp (g, pg, "power");
                    const std::pair<const char*, const char*> lit[] { { "peakRms", "kRms" }, { "detect", "kSum" }, { "scFilter", "kScFilter" },
                                                                      { "autoRelease", "kAutoRel" }, { "listen", "kListen" }, { "limiter", "kLimit" },
                                                                      { "meterHold", "kOhm" }, { "console", "kConsole" }, { "air", "kAir" } };
                    for (const auto& [id, l] : lit)
                        lamp (g, pg, l, on (id));
                    lamp (g, pg, "kDrive", choice ("distortion") > 0);
                    const char* modes[] { "kLP", "kBP", "kHP", "kEQ", "kHS" };
                    lamp (g, pg, modes[juce::jlimit (0, 4, choice ("scMode"))]);
                    const char* gains[] { "kOff", "k0dB", "k12dB" };
                    lamp (g, pg, gains[juce::jlimit (0, 2, choice ("autoGain"))]);
                    // the arcs and the stepped ring show the knobs' settings
                    const auto frac = [&] (const char* id, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, (valueOf (fx, v, id) - lo) / (hi - lo)); };
                    arc (g, pg, "arcInput", -135.0f, -135.0f + 270.0f * frac ("input", -30.0f, 30.0f));
                    arc (g, pg, "arcMonitor", -135.0f, -135.0f + 270.0f * frac ("output", -30.0f, 30.0f));
                    arc (g, pg, "arcHp1", -135.0f, -135.0f + 270.0f * frac ("makeup", -30.0f, 30.0f));
                    arc (g, pg, "arcHp2", -135.0f, -135.0f + 270.0f * frac ("mix", 0.0f, 100.0f));
                    // (the gold knobs' rings light from the lowest setting up to theirs, as Input and Output's)
                    arc (g, pg, "arcThreshold", -150.0f, -150.0f + 300.0f * frac ("threshold", 0.0f, -60.0f));
                    arc (g, pg, "arcRatio", -150.0f, -150.0f + 300.0f * std::log (juce::jlimit (1.0f, 30.0f, valueOf (fx, v, "ratio", 4.0f))) / std::log (30.0f));
                    // the meters: input, gain reduction (from the top), output
                    const auto barOf = [] (float db)
                    {
                        static const std::pair<float, float> marks[] { { -48.0f, 0.03f }, { -36.0f, 0.19f }, { -24.0f, 0.35f }, { -18.0f, 0.51f },
                                                                       { -12.0f, 0.67f }, { -6.0f, 0.83f }, { 0.0f, 0.99f } };
                        if (db <= -48.0f) return juce::jmax (0.0f, 0.03f + (db + 48.0f) * 0.004f);
                        for (size_t i = 1; i < std::size (marks); ++i)
                            if (db <= marks[i].first)
                                return marks[i - 1].second + (db - marks[i - 1].first) / (marks[i].first - marks[i - 1].first) * (marks[i].second - marks[i - 1].second);
                        return 1.0f;
                    };
                    const auto hold = on ("meterHold");
                    const auto held = [&] (const char* which, float db) { return hold ? ballistics (key (which), db, 1.0f, 0.004f) : ballistics (key (which), db, 1.0f, 0.12f); };
                    bar (g, pg, "barInL", barOf (held ("inL", inL)));
                    bar (g, pg, "barInR", barOf (held ("inR", inR)));
                    bar (g, pg, "barOutL", barOf (held ("outL", outL)));
                    bar (g, pg, "barOutR", barOf (held ("outR", outR)));
                    const auto grShown = ballistics (key ("gr"), gr, 0.6f, 0.15f);
                    bar (g, pg, "barGrL", grShown / 24.0f, true);
                    bar (g, pg, "barGrR", grShown / 24.0f, true);
                    break;
                }
                case studioVca:
                {
                    if (! powered)
                        break;
                    lamp (g, pg, "power");
                    for (const auto* l : { "in1", "in2" }) lamp (g, pg, l, on ("compIn", 1.0f));
                    for (const auto* l : { "key1", "key2" }) lamp (g, pg, l, on ("extSc", 1.0f));
                    for (const auto* l : { "auto1", "auto2" }) lamp (g, pg, l, on ("autoRelease"));
                    for (const auto* l : { "limIn1", "limIn2" }) lamp (g, pg, l, on ("limiter"));
                    lamp (g, pg, "stereo", on ("link", 1.0f));
                    lamp (g, pg, "gainChange", gr > 1.0f);
                    lamp (g, pg, "limit1", on ("limiter") && limiting > 0.3f);
                    lamp (g, pg, "limit2", on ("limiter") && limiting > 0.3f);
                    const auto vu = ballistics (key ("vu"), -gr, 0.35f, 0.18f);
                    // (their own needles were too fine to cut cleanly: the silver limiter's, dark and sharp)
                    needle (g, pg, "needle1", vuAngle (vu, -28.2f, 15.4f, 26.6f), "sfet.needle");
                    needle (g, pg, "needle2", vuAngle (vu, -30.5f, 15.0f, 31.9f), "sfet.needle");
                    break;
                }
                case studioFet:
                {
                    lamp (g, pg, "led", powered);
                    const auto ratio = valueOf (fx, v, "ratio", 4.0f);
                    const std::pair<const char*, float> ratios[] { { "r20", 20.0f }, { "r12", 12.0f }, { "r8", 8.0f }, { "r4", 4.0f } };
                    for (const auto& [l, value] : ratios)
                        lamp (g, pg, l, ratio >= 25.0f || std::abs (fetRatio (ratio) - value) < 0.5f);
                    const char* meters[] { "mGR", "m4", "m8", "mOff" };
                    lamp (g, pg, meters[juce::jlimit (0, 3, mode)]);
                    const auto vu = ballistics (key ("vu"), powered ? vuReading (mode, gr, outDb) : -24.0f, 0.35f, 0.18f);
                    needle (g, pg, "needle", vuAngle (vu, -50.1f, 13.9f, 46.0f));
                    if (! powered || mode == 3)
                    {
                        // (the meter's lamp out)
                        g.setColour (juce::Colours::black.withAlpha (0.45f));
                        g.fillRect (pg.photo (949.0f, 81.0f, 253.0f, 111.0f));
                    }
                    break;
                }
                case classicVca:
                {
                    if (! powered)
                        break;
                    lamp (g, pg, "power");
                    lamp (g, pg, "kComp", on ("compIn", 1.0f));
                    lamp (g, pg, "kExt", on ("extSc", 1.0f));
                    lamp (g, pg, "driveLed", valueOf (fx, v, "vcaDrive") > 0.02f);
                    const auto shown = ballistics (key ("gr"), on ("compIn", 1.0f) ? gr : 0.0f, 0.5f, 0.2f);
                    needle (g, pg, "needle", -36.6f + juce::jlimit (0.0f, 21.0f, shown) / 20.0f * (40.1f + 36.6f));
                    break;
                }
                case vintageVca:
                {
                    lamp (g, pg, "kIn", on ("compIn", 1.0f));
                    const auto shown = ballistics (key ("gr"), on ("compIn", 1.0f) ? gr : 0.0f, 0.5f, 0.2f);
                    needle (g, pg, "needle", -38.1f + juce::jlimit (0.0f, 21.0f, shown) / 20.0f * (41.4f + 38.1f));
                    break;
                }
                case vintageFet:
                {
                    const auto ratio = valueOf (fx, v, "ratio", 4.0f);
                    const std::pair<const char*, float> ratios[] { { "r20", 20.0f }, { "r12", 12.0f }, { "r8", 8.0f }, { "r4", 4.0f } };
                    for (const auto& [l, value] : ratios)
                        lamp (g, pg, l, ratio >= 25.0f || std::abs (fetRatio (ratio) - value) < 0.5f);
                    const char* meters[] { "mGR", "m4", "m8", "mOff" };
                    lamp (g, pg, meters[juce::jlimit (0, 3, mode)]);
                    const auto vu = ballistics (key ("vu"), powered ? vuReading (mode, gr, outDb) : -24.0f, 0.35f, 0.18f);
                    // (the photo's own needle was too faint to cut cleanly: the silver limiter's, the same kind of meter)
                    needle (g, pg, "needle", vuAngle (vu, -41.3f, 11.6f, 42.5f), "sfet.needle");
                    if (! powered || mode == 3)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.45f));
                        g.fillRect (pg.photo (1151.0f, 79.0f, 248.0f, 108.0f));
                    }
                    break;
                }
                case vintageOpto:
                {
                    const auto m = mode == 2 ? 2 : mode == 1 ? 1 : 0;
                    const auto reading = m == 0 ? -gr : outDb + (m == 1 ? 18.0f : 12.0f);
                    const auto vu = ballistics (key ("vu"), powered ? reading : -24.0f, 0.25f, 0.12f);
                    needle (g, pg, "needle", vuAngle (vu, -44.3f, 14.0f, 45.3f));
                    break;
                }
                default: break;
            }
            if (! powered && circuit != studioFet && circuit != vintageOpto)
            {
                // (switched off: the panel a touch darker, nothing lit)
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.fillRect (b.withTrimmedTop (tabs));
            }
        };
        d["studioComp"] = c;
    }

    //==========================================================================
    // The Studio Compressor in Liquid Glass: each circuit drawn in glass (its controls as on its hardware face), the
    // knobs LED-ringed, the keys glass pills; the Studio FET and the Opto on dark brushed metal; the FET's amber VU
    // (whiter on the Opto), the black compression meter on the bus compressors.
    namespace glasscomp
    {
        constexpr int faceTop = 46 + 8, faceHeight = 282, faceLeft = 0;

        struct Knob { const char* id; float cx, cy, d; std::vector<std::pair<float, float>> map; const char* label; juce::Colour ring;
                      bool log = false, stepped = false, fromCentre = false; const char* offParam = nullptr; const char* autoParam = nullptr; };
        struct Pill { const char* id; float cx, cy, w; const char* text; float setValue = -1.0f; int choice = -1; bool green = false;
                      bool invert = false; float h = 24.0f; };
        enum class Meter { none, fetVu, optoVu, blackGr, bars };
        struct Box { float x, w; const char* title = nullptr; };
        struct Face
        {
            std::vector<Box> boxes;
            std::vector<Knob> knobs;
            std::vector<Pill> pills;
            std::vector<std::pair<Meter, juce::Rectangle<float>>> meters;
            juce::Colour tint;
            bool metal = false;
            juce::Point<float> power { -1.0f, -1.0f };   // (a round power key, if the face has one)
            std::vector<std::tuple<float, float, const char*, bool>> captions;   // (UTF-8)   // x, y, text, dim
        };

        const juce::Colour amber (0xffff9f0a), cyan (0xff64d2ff), silver (0xffe6e8ec), gold (0xffffcc4d), red (0xffff453a),
                           yellow (0xffffd60a), teal (0xff5ac8fa), cream (0xfff5e6c8), green (0xff30d158), white (0xfff2f2ee);

        std::vector<std::pair<float, float>> lin (float a, float b) { return { { -135.0f, a }, { 135.0f, b } }; }
        std::vector<std::pair<float, float>> steps (std::initializer_list<float> values)
        {
            std::vector<std::pair<float, float>> m;
            const auto n = (int) values.size();
            int i = 0;
            for (auto v : values)
                m.push_back ({ -120.0f + 240.0f * (float) i++ / (float) std::max (1, n - 1), v });
            return m;
        }

        const std::vector<Face>& faces()
        {
            static const std::vector<Face> all = []
            {
                std::vector<Face> f (7);
                const std::vector<std::pair<float, float>> io { { -150.0f, -30.0f }, { -115.0f, -24.0f }, { -73.0f, -12.0f }, { -36.0f, -6.0f },
                                                                { 3.0f, 0.0f }, { 42.0f, 6.0f }, { 77.0f, 12.0f }, { 114.0f, 18.0f }, { 150.0f, 24.0f } };
                const std::vector<std::pair<float, float>> fetAttack { { -135.0f, 200.0f }, { -45.0f, 60.0f }, { 45.0f, 10.0f }, { 135.0f, 0.05f } };
                const std::vector<std::pair<float, float>> fetRelease { { -135.0f, 3000.0f }, { -45.0f, 900.0f }, { 45.0f, 200.0f }, { 135.0f, 5.0f } };
                const auto busAttack = steps ({ 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f });
                const auto busRelease = steps ({ 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 1600.0f });
                const auto busRatio = steps ({ 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 10.0f });
                const auto busHpf = steps ({ 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f });

                // Platinum Digital: the blue unit, every key and the side chain
                {
                    auto& p = f[0];
                    p.tint = juce::Colour (0xff0a84ff);
                    p.boxes = { { 14, 300 }, { 324, 72, "IN" }, { 406, 150 }, { 566, 320 }, { 896, 72, "OUT" }, { 978, 232 }, { 1218, 66 } };
                    p.knobs = { { "threshold", 80, 82, 72, lin (0.0f, -60.0f), "THRESHOLD", gold }, { "ratio", 190, 82, 72, lin (1.0f, 30.0f), "RATIO", gold, true },
                                { "attack", 270, 56, 32, lin (0.05f, 200.0f), "ATTACK", cyan, true }, { "release", 270, 132, 32, lin (5.0f, 3000.0f), "RELEASE", cyan, true },
                                { "input", 481, 92, 84, lin (-30.0f, 30.0f), "INPUT GAIN", silver, false, false, true },
                                { "knee", 680, 130, 30, lin (0.0f, 1.0f), "KNEE", cyan }, { "scHpf", 726, 130, 30, lin (20.0f, 20000.0f), "FREQ", cyan, true },
                                { "scQ", 772, 130, 30, lin (0.1f, 10.0f), "Q", cyan, true }, { "scGain", 818, 130, 30, lin (-24.0f, 24.0f), "GAIN", cyan, false, false, true },
                                { "limThr", 864, 130, 30, lin (-20.0f, 0.0f), "CEILING", cyan },
                                { "output", 1030, 92, 76, lin (-30.0f, 30.0f), "OUTPUT", silver, false, false, true },
                                { "makeup", 1112, 66, 46, lin (-30.0f, 30.0f), "MAKE UP", cyan, false, false, true }, { "mix", 1182, 66, 46, lin (0.0f, 100.0f), "MIX", cyan } };
                    const char* ids[] { "peakRms", "detect", "scFilter", "autoRelease", "listen", "limiter", "meterHold", "console" };
                    const char* texts[] { "RMS", "SUM", "FILTER", "AUTO", "LISTEN", "LIMIT", "HOLD", "CONSOLE" };
                    for (int i = 0; i < 8; ++i)
                        p.pills.push_back ({ ids[i], 48.0f + (float) (i % 4) * 66.0f, 214.0f + (float) (i / 4) * 30.0f, 58.0f, texts[i] });
                    p.pills.push_back ({ "air", 446, 200, 54, "AIR" });
                    p.pills.push_back ({ "distortion", 516, 200, 54, "DRIVE", -2.0f });
                    const char* modes[] { "LP", "BP", "HP", "EQ", "HS" };
                    for (int i = 0; i < 5; ++i)
                        p.pills.push_back ({ "scMode", 680.0f + (float) i * 46.0f, 62.0f, 40.0f, modes[i], -1.0f, i });
                    p.pills.push_back ({ "extSc", 770, 226, 86, "KEY 1 \xc2\xb7 2" });
                    const char* gains[] { "OFF", "0 dB", "-12 dB" };
                    for (int i = 0; i < 3; ++i)
                        p.pills.push_back ({ "autoGain", 1100.0f + (float) i * 48.0f, 200.0f, 44.0f, gains[i], -1.0f, i });
                    p.meters = { { Meter::bars, { 344, 44, 32, 170 } }, { Meter::bars, { 590, 44, 32, 110 } }, { Meter::bars, { 916, 44, 32, 170 } } };
                    p.captions = { { 770, 26, "SIDE CHAIN", false }, { 1146, 168, "AUTO GAIN", false }, { 611, 162, "GR", false } };
                    p.power = { 1251, 124 };
                }
                // Studio VCA: the red dual VCA, the FET's amber VUs
                {
                    auto& p = f[1];
                    p.tint = juce::Colour (0xffff453a);
                    p.boxes = { { 14, 660 }, { 690, 452 }, { 1156, 134, "LIMITER" } };
                    p.knobs = { { "threshold", 178, 120, 96, lin (-50.0f, 0.0f), "THRESHOLD", red }, { "ratio", 318, 120, 80, lin (1.5f, 10.0f), "RATIO", red, true },
                                { "attack", 440, 86, 54, lin (0.3f, 100.0f), "ATTACK", amber, true }, { "release", 440, 196, 54, lin (100.0f, 3000.0f), "RELEASE", amber, true },
                                { "makeup", 620, 120, 80, lin (0.0f, 20.0f), "GAIN", yellow }, { "limThr", 1223, 112, 62, lin (-18.0f, 0.0f), "THRESHOLD", red } };
                    p.pills = { { "compIn", 55, 83, 62, "IN", -1.0f, -1, true }, { "extSc", 55, 119, 62, "KEY" }, { "link", 55, 155, 62, "LINK" },
                                { "autoRelease", 528, 205, 56, "AUTO" }, { "limiter", 1195, 209, 50, "IN" } };
                    p.meters = { { Meter::fetVu, { 702, 46, 214, 108 } }, { Meter::fetVu, { 922, 46, 214, 108 } } };
                    p.captions = { { 809, 168, "OUTPUT", false }, { 1029, 168, "GAIN REDUCTION", false }, { 800, 230, "GAIN CHANGE", true }, { 1262, 230, "LIMIT", true } };
                }
                // Studio FET: the silver limiter on dark brushed metal
                {
                    auto& p = f[2];
                    p.metal = true;
                    p.boxes = { { 14, 110 }, { 134, 380 }, { 524, 130 }, { 664, 90, "RATIO" }, { 764, 300 }, { 1074, 90, "METER" }, { 1174, 110 } };
                    p.knobs = { { "input", 230, 110, 112, io, "INPUT", silver }, { "output", 410, 110, 112, io, "OUTPUT", silver },
                                { "attack", 589, 70, 52, fetAttack, "ATTACK", cyan, false, false, false, "compIn" },
                                { "release", 589, 172, 52, fetRelease, "RELEASE", cyan },
                                { "mix", 1229, 58, 42, lin (100.0f, 0.0f), "MIX", cyan }, { "scHpf", 1229, 150, 42, lin (30.0f, 300.0f), "SC HPF", cyan, true, false, false, "scFilter" } };
                    p.pills = { { "impedance", 69, 60, 60, "10k", -1.0f, -1, false, true }, { "impedance", 69, 90, 60, "600" },
                                { "power", 69, 162, 60, "IN", -1.0f, -1, true }, { "link", 1201, 238, 50, "LINK" }, { "extSc", 1257, 238, 50, "EXT" } };
                    const std::pair<const char*, float> ratios[] { { "20", 20.0f }, { "12", 12.0f }, { "8", 8.0f }, { "4", 4.0f }, { "ALL", 30.0f } };
                    for (int i = 0; i < 5; ++i)
                        p.pills.push_back ({ "ratio", 709.0f, 60.0f + (float) i * 36.0f, 58.0f, ratios[i].first, ratios[i].second });
                    const std::pair<const char*, int> modes[] { { "GR", 0 }, { "+8", 2 }, { "+4", 1 }, { "OFF", 3 } };
                    for (int i = 0; i < 4; ++i)
                        p.pills.push_back ({ "meterMode", 1119.0f, 60.0f + (float) i * 36.0f, 58.0f, modes[i].first, -1.0f, modes[i].second });
                    p.meters = { { Meter::fetVu, { 780, 40, 268, 136 } } };
                    p.captions = { { 69, 28, "Z-IN", false }, { 69, 130, "BYPASS", false }, { 914, 196, "GAIN REDUCTION", false }, { 589, 238, "left past slow: OFF", true } };
                }
                // Classic VCA: the bus compressor, the black compression meter
                {
                    auto& p = f[3];
                    p.tint = juce::Colours::white;
                    p.boxes = { { 14, 250 }, { 274, 560 }, { 844, 320 }, { 1174, 110 } };
                    p.knobs = { { "threshold", 80, 92, 78, lin (-40.0f, 0.0f), "THRESHOLD", white }, { "makeup", 190, 92, 78, lin (0.0f, 20.0f), "MAKE UP", white },
                                { "attack", 330, 92, 60, busAttack, "ATTACK", amber, false, true }, { "release", 442, 92, 60, busRelease, "RELEASE", amber, false, true, false, nullptr, "autoRelease" },
                                { "ratio", 554, 92, 60, busRatio, "RATIO", amber, false, true }, { "scHpfStep", 666, 92, 60, busHpf, "SC HPF", amber, false, true },
                                { "vcaDrive", 796, 92, 46, lin (0.0f, 1.0f), "VCA DRIVE", red } };
                    p.pills = { { "compIn", 100, 226, 60, "IN", -1.0f, -1, true }, { "extSc", 170, 226, 60, "EXT SC" } };
                    p.meters = { { Meter::blackGr, { 860, 30, 288, 160 } } };
                    p.captions = { { 135, 190, "COMP", false }, { 1004, 206, "GAIN REDUCTION", false }, { 330, 190, ".1 .3 1 3 10 30 ms", true },
                                   { 442, 206, ".1 .2 .4 .8 1.6 s AUTO", true }, { 554, 190, "1.5 2 3 4 5 10", true }, { 666, 206, "OFF 30\xe2\x80\x93" "185 Hz", true } };
                    p.power = { 1229, 104 };
                }
                // Vintage VCA: the 500-series bus compressor
                {
                    auto& p = f[4];
                    p.tint = teal;
                    p.boxes = { { 14, 170 }, { 194, 640 }, { 844, 440 } };
                    const std::pair<const char*, const char*> ids[] { { "threshold", "THRESHOLD" }, { "makeup", "MAKE UP" }, { "attack", "ATTACK" },
                                                                      { "release", "RELEASE" }, { "ratio", "RATIO" }, { "scHpfStep", "SC HPF" } };
                    const std::vector<std::pair<float, float>> maps[] { lin (-40.0f, 0.0f), lin (-5.0f, 15.0f), busAttack, busRelease, busRatio, busHpf };
                    for (int i = 0; i < 6; ++i)
                        p.knobs.push_back ({ ids[i].first, 260.0f + (float) i * 102.0f, 104.0f, 66.0f, maps[i], ids[i].second, teal, false, i >= 2,
                                             false, nullptr, i == 3 ? "autoRelease" : nullptr });
                    p.pills = { { "compIn", 99, 110, 70, "IN", -1.0f, -1, true } };
                    p.meters = { { Meter::blackGr, { 870, 30, 300, 166 } } };
                    p.captions = { { 99, 28, "VINTAGE VCA", false }, { 99, 44, "500 SERIES BUS COMPRESSOR", true }, { 1020, 212, "GAIN REDUCTION", false },
                                   { 512, 206, "stepped like the hardware: attack .1\xe2\x80\x93" "30 ms \xc2\xb7 release .1\xe2\x80\x93" "1.6 s + AUTO \xc2\xb7 ratio 1.5\xe2\x80\x93" "10 \xc2\xb7 SC HPF off\xe2\x80\x93" "185 Hz", true } };
                }
                // Vintage FET: the black FET limiter
                {
                    auto& p = f[5];
                    p.tint = amber;
                    p.boxes = { { 14, 380 }, { 404, 130 }, { 544, 90, "RATIO" }, { 644, 420 }, { 1074, 90, "METER" }, { 1174, 110 } };
                    const std::vector<std::pair<float, float>> vio { { -155.0f, -30.0f }, { -118.0f, -24.0f }, { -73.0f, -12.0f }, { -34.0f, -6.0f },
                                                                     { 5.0f, 0.0f }, { 43.0f, 6.0f }, { 76.0f, 12.0f }, { 115.0f, 18.0f }, { 153.0f, 24.0f } };
                    p.knobs = { { "input", 110, 110, 112, vio, "INPUT", amber }, { "output", 290, 110, 112, vio, "OUTPUT", amber },
                                { "attack", 469, 70, 52, fetAttack, "ATTACK", yellow, false, false, false, "compIn" },
                                { "release", 469, 172, 52, fetRelease, "RELEASE", yellow } };
                    const std::pair<const char*, float> ratios[] { { "20", 20.0f }, { "12", 12.0f }, { "8", 8.0f }, { "4", 4.0f }, { "ALL", 30.0f } };
                    for (int i = 0; i < 5; ++i)
                        p.pills.push_back ({ "ratio", 589.0f, 60.0f + (float) i * 36.0f, 58.0f, ratios[i].first, ratios[i].second });
                    const std::pair<const char*, int> modes[] { { "GR", 0 }, { "+8", 2 }, { "+4", 1 }, { "OFF", 3 } };
                    for (int i = 0; i < 4; ++i)
                        p.pills.push_back ({ "meterMode", 1119.0f, 60.0f + (float) i * 36.0f, 58.0f, modes[i].first, -1.0f, modes[i].second });
                    p.meters = { { Meter::fetVu, { 680, 34, 340, 170 } } };
                    p.captions = { { 850, 222, "VU \xc2\xb7 GAIN REDUCTION", false }, { 469, 238, "left past 1: OFF", true } };
                    p.power = { 1229, 104 };
                }
                // Vintage Opto: the grey levelling amplifier on dark brushed metal, its VU whiter
                {
                    auto& p = f[6];
                    p.metal = true;
                    p.boxes = { { 14, 286 }, { 310, 480 }, { 800, 300 }, { 1110, 176 } };
                    p.knobs = { { "threshold", 157, 116, 126, lin (0.0f, -50.0f), "PEAK REDUCTION", cream },
                                { "makeup", 960, 116, 126, lin (0.0f, 30.0f), "GAIN", cream }, { "emphasis", 1198, 198, 40, lin (0.0f, 1.0f), "HF EMPHASIS", cream } };
                    p.pills = { { "optoLimit", 1160, 58, 70, "COMP", -1.0f, -1, false, true }, { "optoLimit", 1236, 58, 70, "LIMIT" },
                                { "meterMode", 1150, 126, 48, "GR", -1.0f, 0 }, { "meterMode", 1200, 126, 44, "+4", -1.0f, 1 }, { "meterMode", 1248, 126, 44, "+10", -1.0f, 2 },
                                { "tubes", 834, 140, 70, "TUBES", -1.0f, -1, true } };
                    p.meters = { { Meter::optoVu, { 360, 26, 380, 193 } } };
                    p.captions = { { 1198, 26, "MODE", false }, { 1198, 96, "METER", false }, { 834, 56, "ON", true }, { 550, 238, "VU", false } };
                    p.power = { 834, 210 };
                }
                return f;
            }();
            return all;
        }

        juce::Point<float> at (float x, float y) { return { (float) faceLeft + 50.0f + x - 50.0f, (float) faceTop + y }; }
        RF rect (float x, float y, float w, float h) { return RF (w, h).withPosition (at (x, y)); }

        /** The FET's amber VU (cut from its face), or the Opto's whiter one. */
        juce::Image vuImage (bool whiter)
        {
            static juce::Image amberVu, whiteVu;
            if (amberVu.isNull())
            {
                const auto panel = skinImage ("sfet_panel_jpg");
                if (panel.isValid())
                    amberVu = panel.getClippedImage ({ 928, 60, 288, 146 }).createCopy();
            }
            if (whiter && whiteVu.isNull() && amberVu.isValid())
            {
                whiteVu = amberVu.createCopy();
                juce::Image::BitmapData px (whiteVu, juce::Image::BitmapData::readWrite);
                for (int y = 0; y < whiteVu.getHeight(); ++y)
                    for (int x = 0; x < whiteVu.getWidth(); ++x)
                    {
                        const auto c = px.getPixelColour (x, y);
                        px.setPixelColour (x, y, c.withSaturation (c.getSaturation() * 0.3f).withMultipliedBrightness (1.18f));
                    }
            }
            return whiter ? whiteVu : amberVu;
        }

        const ParamInfo* paramOf (const EffectInfo& fx, const char* id)
        {
            for (const auto& p : fx.params)
                if (p.id == id)
                    return &p;
            return nullptr;
        }

        juce::String valueText (const ParamInfo& p, float v)
        {
            if (p.isChoice())
                return p.text (v);
            auto suffix = p.suffix.trim();
            if (suffix == "ms" && std::abs (v) >= 1000.0f) { v /= 1000.0f; suffix = "s"; }
            if (suffix == "Hz" && std::abs (v) >= 1000.0f) { v /= 1000.0f; suffix = "kHz"; }
            const auto digits = std::abs (v) >= 100.0f ? 0 : (std::abs (v) >= 10.0f ? 1 : (std::abs (v) < 1.0f && suffix.isEmpty() ? 2 : 1));
            auto number = juce::String (v, digits);
            if (suffix == ":1") return number + ":1";
            if (suffix == "dB" && v > 0.05f) number = "+" + number;
            return suffix.isEmpty() ? number : number + " " + suffix;
        }

        void pill (juce::Graphics& g, RF r, const juce::String& text, bool on, bool greenOn)
        {
            const auto lit = greenOn ? green : (theme::glassLight() ? juce::Colour (0xff0a84ff) : juce::Colour (0xff0a84ff));
            look::drawKey (g, r, look::Key::dark, on, lit, false, false);
            g.setColour (on ? (greenOn ? juce::Colour (0xff062010) : juce::Colours::white) : theme::text);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        void paint (juce::Graphics& g, R b, const EffectInfo& fx, const float* v, float live)
        {
            using namespace comp;
            const auto circuit = juce::jlimit (0, 6, juce::roundToInt (valueOf (fx, v, "model", 1.0f)));
            const auto& f = faces()[(size_t) circuit];
            const auto light = theme::glassLight();
            g.fillAll (theme::console);
            g.setColour (theme::toolbarDark);
            g.fillRect (b.withHeight (46));

            // the face: glass tinted in the unit's colour, or dark brushed metal
            const auto face = RF (8.0f, (float) faceTop, (float) b.getWidth() - 16.0f, (float) faceHeight);
            look::drawGlassPanel (g, face, 16.0f);
            if (f.metal)
            {
                juce::Graphics::ScopedSaveState keep (g);
                juce::Path clip;
                clip.addRoundedRectangle (face.reduced (1.0f), 15.0f);
                g.reduceClipRegion (clip);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c41), 0.0f, face.getY(), juce::Colour (0xff232528), 0.0f, face.getBottom(), false));
                g.fillRect (face);
                for (float x = face.getX(); x < face.getRight(); x += 3.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.025f));
                    g.fillRect (x, face.getY(), 1.0f, face.getHeight());
                }
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawLine (face.getX() + 16.0f, face.getY() + 1.0f, face.getRight() - 16.0f, face.getY() + 1.0f, 1.0f);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (f.tint.withAlpha (light ? 0.10f : 0.14f), 0.0f, face.getY(),
                                                         f.tint.withAlpha (0.0f), 0.0f, face.getY() + face.getHeight() * 0.75f, false));
                g.fillRoundedRectangle (face.reduced (1.0f), 15.0f);
            }
            const auto ink = f.metal ? juce::Colour (0xfff2f4f8) : theme::text;
            const auto dim = f.metal ? juce::Colour (0xff9aa0a8) : theme::dimText;
            for (const auto& box : f.boxes)
            {
                const auto r = rect (box.x, 14.0f, box.w, (float) faceHeight - 28.0f);
                look::drawGlassPanel (g, r, 12.0f, true);
                if (box.title != nullptr)
                {
                    g.setColour (ink.withAlpha (0.75f));
                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    g.drawText (box.title, r.withHeight (22.0f).translated (0.0f, 6.0f), juce::Justification::centred, false);
                }
            }
            for (const auto& [x, y, text, isDim] : f.captions)
            {
                g.setColour (isDim ? dim.withAlpha (0.8f) : ink.withAlpha (0.75f));
                g.setFont (juce::FontOptions (isDim ? 9.5f : 10.5f, isDim ? juce::Font::plain : juce::Font::bold));
                g.drawText (juce::String::fromUTF8 (text), RF (360.0f, 14.0f).withCentre (at (x, y)), juce::Justification::centred, false);
            }

            // knobs: the LED ring and pointer at the setting, the name and value under them
            for (const auto& k : f.knobs)
            {
                Sprite s;
                s.map = k.map;
                s.logMap = k.log;
                s.stepped = k.stepped;
                auto angle = photoAngleOf (s, valueOf (fx, v, k.id, k.map.front().second));
                if (k.offParam != nullptr && valueOf (fx, v, k.offParam, 1.0f) < 0.5f)
                    angle = k.map.front().first - 15.0f;
                if (k.autoParam != nullptr && valueOf (fx, v, k.autoParam) > 0.5f)
                    angle = k.map.back().first;
                const auto a0 = k.map.front().first, a1 = k.map.back().first;
                const auto proportion = juce::jlimit (0.0f, 1.0f, (angle - a0) / (a1 - a0));
                const auto c = at (k.cx, k.cy);
                const auto area = RF (k.d, k.d).withCentre (c);
                look::drawGlassKnob (g, area.expanded (k.d * 0.18f), proportion, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f),
                                     k.ring, k.fromCentre, true);
                g.setColour (ink);
                g.setFont (juce::FontOptions (k.d >= 60.0f ? 11.5f : 10.0f, juce::Font::bold));
                g.drawText (k.label, RF (140.0f, 14.0f).withCentre ({ c.x, area.getBottom() + k.d * 0.18f + 9.0f }), juce::Justification::centred, false);
                if (const auto* p = paramOf (fx, k.id))
                {
                    g.setColour (dim);
                    g.setFont (juce::FontOptions (9.5f));
                    auto text = valueText (*p, valueOf (fx, v, k.id));
                    if (k.offParam != nullptr && valueOf (fx, v, k.offParam, 1.0f) < 0.5f) text = "OFF";
                    if (k.autoParam != nullptr && valueOf (fx, v, k.autoParam) > 0.5f) text = "AUTO";
                    g.drawText (text, RF (140.0f, 12.0f).withCentre ({ c.x, area.getBottom() + k.d * 0.18f + 22.0f }), juce::Justification::centred, false);
                }
            }
            // keys: lit at their setting
            for (const auto& p : f.pills)
            {
                const auto value = valueOf (fx, v, p.id);
                bool on;
                if (p.setValue == -2.0f)        on = value > 0.5f;                                        // (a key stepping through settings)
                else if (p.setValue >= 0.0f)    on = p.setValue >= 25.0f ? value >= 25.0f : (value < 25.0f && std::abs (fetRatio (value) - p.setValue) < 0.5f);
                else if (p.choice >= 0)         on = juce::roundToInt (value) == p.choice;
                else                            on = p.invert ? value < 0.5f : value > 0.5f;
                juce::String text (juce::CharPointer_UTF8 (p.text));
                if (p.setValue == -2.0f)
                    if (const auto* param = paramOf (fx, p.id)) text = param->text (value);
                pill (g, RF (p.w, p.h).withCentre (at (p.cx, p.cy)), value > 0.5f && p.setValue == -2.0f ? text : juce::String (juce::CharPointer_UTF8 (p.text)), on, p.green);
            }
            // the round power key
            const auto powered = valueOf (fx, v, "power", 1.0f) > 0.5f;
            if (f.power.x > 0.0f)
            {
                const auto c = at (f.power.x, f.power.y);
                const auto ball = RF (46.0f, 46.0f).withCentre (c);
                look::drawKey (g, ball, look::Key::dark, false, {}, false, false);
                const auto col = powered ? green : juce::Colour (0xff6b707a);
                juce::Path icon;
                icon.addCentredArc (c.x, c.y, 9.0f, 9.0f, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
                icon.startNewSubPath (c.x, c.y - 12.0f);
                icon.lineTo (c.x, c.y - 3.0f);
                if (powered)
                {
                    g.setColour (col.withAlpha (0.35f));
                    g.strokePath (icon, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (col);
                g.strokePath (icon, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (dim);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText ("POWER", RF (80.0f, 12.0f).withCentre ({ c.x, ball.getBottom() + 12.0f }), juce::Justification::centred, false);
            }

            // the meters
            const auto gr = std::abs (meter (0, live));
            const auto outDb = std::max (meter (3), meter (4));
            const auto mode = juce::roundToInt (valueOf (fx, v, "meterMode"));
            const auto key = [circuit] (const char* n) { return "glass" + std::to_string (circuit) + n; };
            int vuIndex = 0, barIndex = 0;
            for (const auto& [kind, r0] : f.meters)
            {
                const auto r = rect (r0.getX(), r0.getY(), r0.getWidth(), r0.getHeight());
                if (kind == Meter::fetVu || kind == Meter::optoVu)
                {
                    const auto img = vuImage (kind == Meter::optoVu);
                    juce::Graphics::ScopedSaveState keep (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (r, 10.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.5f));
                    g.fillRoundedRectangle (r.translated (0.0f, 4.0f), 10.0f);
                    g.reduceClipRegion (clip);
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g.setOpacity (1.0f);
                    if (img.isValid())
                        g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
                    // its needle (the Studio VCA's two: the output, then the gain reduction)
                    float reading;
                    if (circuit == 1)                 reading = vuIndex == 0 ? outDb + 18.0f : -gr;
                    else if (circuit == 6)            reading = mode == 0 ? -gr : outDb + (mode == 1 ? 18.0f : 12.0f);
                    else                              reading = powered ? vuReading (mode, gr, outDb) : -24.0f;
                    const auto shown = ballistics (key (vuIndex == 0 ? "vu0" : "vu1"), reading, 0.35f, 0.18f);
                    const auto sx = r.getWidth() / 288.0f, sy = r.getHeight() / 146.0f;
                    const juce::Point<float> pivot (r.getX() + 140.0f * sx, r.getY() + 164.0f * sy);
                    const auto a = juce::degreesToRadians (vuAngle (shown, -50.1f, 13.9f, 46.0f));
                    const auto tip = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (136.0f * sy);
                    g.setColour (juce::Colour (0xff1a1a1a));
                    g.drawLine ({ pivot, tip }, juce::jmax (1.2f, 1.6f * sy));
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(), false));
                    g.fillRect (r);
                    ++vuIndex;
                }
                else if (kind == Meter::blackGr)
                {
                    const auto shown = ballistics (key ("gr"), valueOf (fx, v, "compIn", 1.0f) > 0.5f ? gr : 0.0f, 0.5f, 0.2f);
                    g.setColour (juce::Colours::black.withAlpha (0.5f));
                    g.fillRoundedRectangle (r.translated (0.0f, 4.0f), 12.0f);
                    if (! looks::drawMeter (g, r, shown))
                    {
                        g.setColour (juce::Colour (0xff111214));
                        g.fillRoundedRectangle (r, 12.0f);
                    }
                }
                else
                {
                    // smooth bars: input (L, R), gain reduction from the top, output (L, R)
                    const auto inL = meter (1), inR = meter (2), outL = meter (3), outR = meter (4);
                    const auto frac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };
                    const bool reduction = barIndex == 1;
                    const float values[2] { reduction ? gr / 24.0f : frac (barIndex == 0 ? inL : outL), reduction ? gr / 24.0f : frac (barIndex == 0 ? inR : outR) };
                    for (int i = 0; i < 2; ++i)
                    {
                        const auto bar = RF (r.getX() + (float) i * 20.0f, r.getY(), 12.0f, r.getHeight());
                        g.setColour (juce::Colours::black.withAlpha (0.5f));
                        g.fillRoundedRectangle (bar, 6.0f);
                        const auto fr = juce::jlimit (0.0f, 1.0f, ballistics (key (("b" + std::to_string (barIndex) + std::to_string (i)).c_str()), values[i], 0.6f, 0.15f));
                        if (fr <= 0.0f)
                            continue;
                        const auto lit = reduction ? bar.withHeight (bar.getHeight() * fr) : bar.withTrimmedTop (bar.getHeight() * (1.0f - fr));
                        juce::ColourGradient grad = reduction ? juce::ColourGradient (yellow, 0.0f, bar.getY(), red, 0.0f, bar.getBottom(), false)
                                                              : juce::ColourGradient (green, 0.0f, bar.getBottom(), red, 0.0f, bar.getY(), false);
                        if (! reduction) grad.addColour (0.8, yellow);
                        g.setGradientFill (grad);
                        g.fillRoundedRectangle (lit, 6.0f);
                    }
                    ++barIndex;
                }
            }
            // lamps: the Studio VCA's gain change and limit, the Studio FET's on, the Classic VCA's drive, the Opto's pilot, the Vintage VCA's in
            const auto lampAt = [&g] (juce::Point<float> c, juce::Colour col, bool on)
            {
                if (on) { g.setColour (col.withAlpha (0.35f)); g.fillEllipse (RF (22.0f, 22.0f).withCentre (c)); }
                g.setColour (on ? col : col.withMultipliedBrightness (0.3f).withMultipliedSaturation (0.6f));
                g.fillEllipse (RF (13.0f, 13.0f).withCentre (c));
            };
            if (circuit == 1)
            {
                lampAt (at (740, 230), red, gr > 1.0f);
                lampAt (at (1240, 209), red, valueOf (fx, v, "limiter") > 0.5f && meter (5, 0.0f) > 0.3f);
            }
            if (circuit == 2) lampAt (at (69, 210), amber, powered);
            if (circuit == 3) lampAt (at (796, 156), red, valueOf (fx, v, "vcaDrive") > 0.02f);
            if (circuit == 4) lampAt (at (99, 160), green, valueOf (fx, v, "compIn", 1.0f) > 0.5f);
            if (circuit == 6) lampAt (at (834, 40), red, powered);
        }
    }

    void addGlassCompressorFaces (std::map<juce::String, Design>& d)
    {
        using namespace glasscomp;
        Design c;
        c.width = comp::width;
        c.height = faceTop + faceHeight + 8;
        c.knobs = KnobStyle::dark;
        c.label = theme::text; c.value = theme::text; c.accent = juce::Colour (0xffff9f0a); c.track = juce::Colours::black;
        c.meters = true;
        c.live = true;
        c.pageParam = "model";
        c.photoScale = 1.0f;
        c.controls.push_back ({ "model", Kind::segmented, R (8, 7, comp::width - 16, 32), "-" });
        const auto offset = juce::Point<float> (0.0f, 0.0f);
        for (int circuit = 0; circuit < 7; ++circuit)
        {
            const auto& f = faces()[(size_t) circuit];
            const auto n = circuit + 1;
            for (const auto& k : f.knobs)
            {
                const auto ctr = at (k.cx, k.cy);
                const auto r = k.d * 0.5f + 4.0f;
                Sprite s;
                s.rect = RF (ctr.x - r, ctr.y - r, r * 2.0f, r * 2.0f);
                s.pivot = ctr;
                s.map = k.map;
                s.logMap = k.log;
                s.stepped = k.stepped;
                s.scale = 1.0f;
                s.offset = offset;
                if (k.offParam != nullptr) { s.offBelow = k.map.front().first - 8.0f; s.offParam = k.offParam; }
                if (k.autoParam != nullptr) s.autoParam = k.autoParam;
                Control ctl;
                ctl.id = k.id;
                ctl.kind = Kind::knob;
                ctl.label = "-";
                ctl.page = n;
                ctl.bounds = s.rect.getSmallestIntegerContainer();
                ctl.sprite = s;
                c.controls.push_back (ctl);
            }
            for (const auto& p : f.pills)
            {
                const auto rr = RF (p.w, p.h).withCentre (at (p.cx, p.cy));
                Sprite s;
                s.rect = rr;
                s.pivot = rr.getCentre();
                s.scale = 1.0f;
                s.offset = offset;
                if (p.setValue >= 0.0f) { s.setValue = p.setValue; s.altValue = 30.0f; }
                if (p.setValue == -2.0f) s.cycle = true;
                if (p.choice >= 0) { s.choice = p.choice; s.radio = true; }
                if (p.invert) { s.choice = 0; s.radio = true; }
                if (! p.invert && p.choice < 0 && p.setValue == -1.0f
                    && std::any_of (f.pills.begin(), f.pills.end(), [&p] (const Pill& o) { return o.invert && juce::String (o.id) == p.id; }))
                    { s.choice = 1; s.radio = true; }
                Control ctl;
                ctl.id = p.id;
                ctl.kind = Kind::hotspot;
                ctl.label = "-";
                ctl.page = n;
                ctl.bounds = rr.getSmallestIntegerContainer();
                ctl.sprite = s;
                c.controls.push_back (ctl);
            }
            if (f.power.x > 0.0f)
            {
                const auto rr = RF (50.0f, 50.0f).withCentre (at (f.power.x, f.power.y));
                Sprite s;
                s.rect = rr; s.pivot = rr.getCentre(); s.scale = 1.0f; s.offset = offset;
                Control ctl;
                ctl.id = "power"; ctl.kind = Kind::hotspot; ctl.label = "-"; ctl.page = n;
                ctl.bounds = rr.getSmallestIntegerContainer();
                ctl.sprite = s;
                c.controls.push_back (ctl);
            }
        }
        c.paint = glasscomp::paint;
        c.glassFace = true;
        d["studioCompGlass"] = c;
    }

    void addHardwareDesigns (std::map<juce::String, Design>& d)
    {
        const auto knob = Kind::knob;
        {
            // Vintage console EQ: charcoal, blue frequency knobs, grey gains, red drive
            Design c;
            c.width = 1180; c.height = 380;
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::white.withAlpha (0.88f); c.value = juce::Colours::white; c.accent = juce::Colour (0xff3f86d9); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintConsoleEq;
            const juce::Colour blue (0xff2f6fc0), grey (0xff8a8e93), red (0xffb8301e);
            c.controls = {
                { "hpf", knob, R (160, 110, 170, 170), "LOW CUT", blue },
                { "lowGain", knob, R (380, 64, 120, 134), "LOW GAIN", grey },
                { "lowFreq", knob, R (355, 210, 170, 170), "LOW FREQ", blue },
                { "midGain", knob, R (575, 64, 120, 134), "MID GAIN", grey },
                { "midFreq", knob, R (550, 210, 170, 170), "MID FREQ", blue },
                { "highGain", knob, R (770, 120, 120, 134), "HIGH GAIN", grey },
                { "drive", knob, R (1010, 60, 130, 150), "DRIVE", red },
                { "output", knob, R (1015, 230, 120, 140), "VOLUME", grey },
            };
            d["consoleEq"] = c;
        }
        {
            // Vintage graphic EQ: ten faders over blue lines (1 kHz's cap red)
            Design c;
            c.width = 1300; c.height = 380;
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::white.withAlpha (0.9f); c.value = juce::Colours::white; c.accent = juce::Colour (0xff2f8ac0); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintGraphicEq;
            const char* ids[] { "b31", "b63", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k" };
            const char* names[] { "31", "63", "125", "250", "500", "1.0K", "2.0K", "4.0K", "8.0K", "16.0K" };
            for (int k = 0; k < 10; ++k)
                c.controls.push_back ({ ids[k], Kind::fader, R (200 + k * 86, 64, 50, 288), names[k], k == 5 ? juce::Colour (0xffc0201a) : juce::Colour (0xffc9ccd0) });
            c.controls.push_back ({ "output", knob, R (1085, 70, 130, 150), "VOLUME", juce::Colour (0xff2f7fd0) });
            d["graphicEq"] = c;
        }
        {
            // Vintage tube EQ: teal faceplate, black pointer knobs
            Design c;
            c.width = 1120; c.height = 420;
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::white.withAlpha (0.88f); c.value = juce::Colours::white; c.accent = juce::Colours::white; c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintTubeEq;
            c.controls = {
                { "lowBoost", knob, R (110, 40, 130, 150), "LOW BOOST" },
                { "lowAtten", knob, R (270, 40, 130, 150), "LOW ATTEN" },
                { "highBoost", knob, R (480, 40, 130, 150), "HIGH BOOST" },
                { "highAtten", knob, R (720, 40, 130, 150), "HIGH ATTEN" },
                { "lowFreq", knob, R (170, 220, 170, 170), "LOW FREQ" },
                { "bandwidth", knob, R (400, 220, 130, 160), "BANDWIDTH" },
                { "highFreq", knob, R (545, 220, 170, 170), "HIGH FREQ" },
                { "attenFreq", knob, R (720, 220, 170, 170), "ATTEN SEL" },
                { "output", knob, R (950, 90, 130, 160), "VOLUME" },
            };
            d["tubeEq"] = c;
        }
        {
            // Distortion: a wooden case, black panel, amber writing, black knurled knobs
            Design c;
            c.width = 860; c.height = 420;
            c.knobs = KnobStyle::black;
            c.label = juce::Colour (0xffc99a4a); c.value = juce::Colour (0xffe9c27a); c.accent = juce::Colour (0xffc99a4a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintDistortion;
            c.glassFace = true;
            c.controls = {
                { "drive", knob, R (90, 110, 160, 200), "DRIVE" },
                { "tone", knob, R (270, 110, 160, 200), "TONE" },
                { "output", knob, R (450, 110, 160, 200), "OUTPUT" },
                { "mix", knob, R (630, 110, 160, 200), "MIX" },
                { "levelComp", Kind::toggle, R (360, 322, 140, 30), "=LEVEL COMP" },
            };
            d["distortion"] = c;
            d["fuzz"] = c;
            d["fuzz"].controls = {
                { "fuzz", knob, R (60, 110, 150, 200), "FUZZ" }, { "bias", knob, R (215, 110, 150, 200), "BIAS" },
                { "tone", knob, R (370, 110, 150, 200), "TONE" }, { "gate", knob, R (525, 110, 150, 200), "GATE" },
                { "output", knob, R (680, 110, 150, 200), "OUTPUT" } };
        }
        {
            // Auto wah: orange wood, white knobs, a range fader
            Design c;
            c.width = 860; c.height = 470;
            c.knobs = KnobStyle::white;
            c.label = juce::Colour (0xff3a1e08); c.value = juce::Colour (0xff3a1e08); c.accent = juce::Colour (0xff3a1e08); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintAutoWah;
            c.glassFace = true;
            c.controls = {
                { "mode", Kind::segmented, R (420, 34, 230, 34), "-" },
                { "sensitivity", knob, R (60, 110, 150, 160), "SENSITIVITY" },
                { "resonance", knob, R (240, 110, 150, 160), "RELATIVE Q" },
                { "low", knob, R (420, 110, 150, 160), "RANGE LOW" },
                { "attack", knob, R (60, 290, 150, 160), "ATTACK" },
                { "release", knob, R (240, 290, 150, 160), "RELEASE" },
                { "high", knob, R (420, 290, 150, 160), "RANGE HIGH" },
                { "mix", Kind::fader, R (720, 110, 60, 330), "MIX" },
            };
            d["autoWah"] = c;
        }
        {
            // Guitar amp: a tweed combo, chrome strip, black knobs; model, cabinet and mic below
            Design c;
            c.width = 1100; c.height = 560;
            c.knobs = KnobStyle::black;
            c.label = juce::Colour (0xff2a2a2a); c.value = juce::Colour (0xffd8dadc); c.accent = juce::Colour (0xffd8dadc); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintGuitarAmp;
            c.glassFace = true;
            const auto grey = juce::Colour (0xffaab0b6);
            c.controls = {
                { "gain", knob, R (60, 74, 95, 120), "GAIN" },
                { "bass", knob, R (160, 74, 95, 120), "BASS" },
                { "mid", knob, R (250, 74, 95, 120), "MIDS" },
                { "treble", knob, R (340, 74, 95, 120), "TREBLE" },
                { "eqType", Kind::dropdown, R (250, 200, 130, 24), "-", juce::Colour (0xff2a2a2a) },
                { "revOn", Kind::toggle, R (500, 52, 70, 22), "=ON" },
                { "revType", Kind::dropdown, R (455, 80, 160, 24), "-", juce::Colour (0xff2a2a2a) },
                { "revLevel", knob, R (490, 108, 90, 100), "-" },
                { "fxOn", Kind::toggle, R (680, 52, 70, 22), "=ON" },
                { "fxMode", Kind::segmented, R (640, 80, 160, 24), "-" },
                { "fxDepth", knob, R (630, 108, 85, 95), "-" },
                { "fxSpeed", knob, R (720, 108, 85, 95), "-" },
                { "fxSync", Kind::segmented, R (650, 202, 140, 20), "-" },
                { "presence", knob, R (840, 74, 95, 120), "PRESENCE" },
                { "master", knob, R (940, 74, 95, 120), "MASTER" },
                { "model", Kind::dropdown, R (40, 486, 210, 30), "Amp", grey },
                { "cab", Kind::dropdown, R (265, 486, 220, 30), "Cabinet", grey },
                { "micType", Kind::dropdown, R (500, 486, 160, 30), "Mic", grey },
                { "mic", knob, R (680, 452, 80, 100), "Position", grey },
                { "micDistance", knob, R (770, 452, 80, 100), "Distance", grey },
                { "output", knob, R (870, 452, 80, 100), "Output", grey },
                { "mix", knob, R (960, 452, 80, 100), "Mix", grey },
            };
            d["guitarAmp"] = c;
        }
        {
            // Bass amp: a black head, brushed aluminium front, chrome knobs
            Design c;
            c.width = 1100; c.height = 560;
            c.knobs = KnobStyle::chrome;
            c.label = juce::Colour (0xff1c1c1c); c.value = juce::Colour (0xffd8dadc); c.accent = juce::Colour (0xffd8dadc); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintBassAmp;
            const auto grey = juce::Colour (0xffaab0b6);
            c.pageParam = "page";
            c.controls = {
                { "page", Kind::segmented, R (850, 30, 200, 26), "-", grey },
                // The amp page: the head's front panel
                { "channel", Kind::segmented, R (90, 140, 120, 26), "CHANNEL", {}, 1 },
                { "bright", Kind::toggle, R (230, 140, 80, 26), "=BRIGHT", {}, 1 },
                { "eqOn", Kind::toggle, R (330, 140, 70, 26), "=EQ", {}, 1 },
                { "lowSw", Kind::segmented, R (420, 140, 140, 26), "LOW", {}, 1 },
                { "midSw", Kind::segmented, R (580, 140, 120, 26), "MID FREQ", {}, 1 },
                { "highSw", Kind::segmented, R (720, 140, 150, 26), "HIGH", {}, 1 },
                { "gain", knob, R (110, 270, 110, 130), "GAIN", {}, 1 },
                { "bass", knob, R (240, 270, 110, 130), "BASS", {}, 1 },
                { "lowMid", knob, R (370, 270, 110, 130), "MIDS", {}, 1 },
                { "treble", knob, R (650, 270, 110, 130), "TREBLE", {}, 1 },
                { "master", knob, R (780, 270, 110, 130), "MASTER", {}, 1 },
                // The pedals page: compressor, extra EQ
                { "compOn", Kind::toggle, R (90, 140, 120, 26), "=COMPRESSOR", {}, 2 },
                { "compHard", Kind::segmented, R (90, 176, 120, 24), "-", {}, 2 },
                { "comp", knob, R (230, 130, 90, 110), "COMP", {}, 2 },
                { "compGain", knob, R (330, 130, 90, 110), "GAIN", {}, 2 },
                { "addEqOn", Kind::toggle, R (450, 140, 120, 26), "=EXTRA EQ", {}, 2 },
                { "addEqType", Kind::segmented, R (580, 140, 180, 26), "-", {}, 2 },
                { "addEqPost", Kind::segmented, R (770, 140, 110, 26), "-", {}, 2 },
                { "loGain", knob, R (110, 290, 80, 100), "LO GAIN", {}, 2 },
                { "loFreq", knob, R (195, 290, 80, 100), "LO kHz", {}, 2 },
                { "loQ", knob, R (280, 290, 80, 100), "LO Q", {}, 2 },
                { "hiGain", knob, R (380, 290, 80, 100), "HI GAIN", {}, 2 },
                { "hiFreq", knob, R (465, 290, 80, 100), "HI kHz", {}, 2 },
                { "hiQ", knob, R (550, 290, 80, 100), "HI Q", {}, 2 },
                // The D.I. box page
                { "diBoost", knob, R (110, 150, 110, 130), "BOOST", {}, 3 },
                { "diHfCut", Kind::toggle, R (260, 170, 100, 26), "=HF CUT", {}, 3 },
                { "diToneOn", Kind::toggle, R (380, 170, 100, 26), "=TONE", {}, 3 },
                { "diTone", Kind::segmented, R (500, 170, 300, 26), "TONE CURVE", {}, 3 },
                { "diLevel", knob, R (850, 150, 110, 130), "DI LEVEL", {}, 3 },
                // Always: the bottom bar
                { "model", Kind::dropdown, R (40, 486, 170, 30), "Amp", grey },
                { "cab", Kind::dropdown, R (225, 486, 210, 30), "Cabinet", grey },
                { "micType", Kind::dropdown, R (450, 486, 150, 30), "Mic", grey },
                { "mic", knob, R (610, 452, 75, 100), "Position", grey },
                { "micDistance", knob, R (690, 452, 75, 100), "Distance", grey },
                { "blend", knob, R (770, 452, 75, 100), "Blend", grey },
                { "mix", knob, R (850, 452, 75, 100), "Mix", grey },
                { "output", knob, R (930, 452, 75, 100), "Output", grey },
            };
            for (int b = 0; b < 7; ++b)
                c.controls.push_back ({ "g" + juce::String (b + 1), Kind::fader, R (650 + b * 50, 250, 34, 170), "-", juce::Colour (0xff6a6e73), 2 });
            d["bassAmp"] = c;
        }
        {
            // Rotary cabinet: wood, a dark control panel, the cabinet drawn on the right
            Design c;
            c.width = 1100; c.height = 560;
            c.knobs = KnobStyle::black;
            c.label = juce::Colour (0xffd8c9a6); c.value = juce::Colour (0xffd8c9a6); c.accent = juce::Colour (0xffd8c9a6); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintRotary;
            c.controls = {
                { "distance", knob, R (40, 64, 110, 130), "DISTANCE" },
                { "angle", knob, R (160, 64, 110, 130), "ANGLE" },
                { "balance", knob, R (280, 64, 110, 130), "HORN/DRUM" },
                { "width", knob, R (400, 64, 110, 130), "HORN WIDTH" },
                { "drumWidth", knob, R (520, 64, 110, 130), "DRUM WIDTH" },
                { "accel", knob, R (40, 244, 110, 130), "ACCELERATION" },
                { "fast", knob, R (160, 244, 110, 130), "MAX RATE" },
                { "doppler", knob, R (280, 244, 110, 130), "DOPPLER" },
                { "drive", knob, R (400, 244, 110, 130), "DRIVE" },
                { "motor", Kind::dropdown, R (525, 290, 120, 28), "MOTOR" },
                { "upperMic", Kind::dropdown, R (40, 425, 150, 28), "UPPER MIC" },
                { "lowerMic", Kind::dropdown, R (205, 425, 150, 28), "LOWER MIC" },
                { "micPos", Kind::segmented, R (370, 425, 150, 28), "MIC POSITION" },
                { "speed", Kind::segmented, R (535, 425, 240, 32), "ROTATION" },
                { "cabinet", Kind::dropdown, R (800, 425, 250, 28), "CABINET" },
            };
            d["rotary"] = c;
        }
        {
            // Saturator: purple, glowing waveforms round the Drive knob
            Design c;
            c.width = 1000; c.height = 600;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colour (0xffb9a8e0); c.value = juce::Colour (0xffffb04a); c.accent = juce::Colour (0xffffa03a); c.track = juce::Colour (0xff2a1f45);
            c.paint = paintSaturator;
            c.controls = {
                { "model", Kind::dropdown, R (40, 40, 220, 34), "Model" },
                { "style", Kind::segmented, R (290, 42, 220, 30), "Style" },
                { "drive", knob, R (420, 110, 160, 200), "Drive" },
                { "bypassOn", Kind::toggle, R (40, 330, 150, 28), "=BYPASS BELOW" },
                { "bypassFreq", knob, R (60, 368, 110, 110), "-" },
                { "input", knob, R (600, 318, 120, 120), "Level In" },
                { "mix", knob, R (730, 318, 120, 120), "Mix" },
                { "output", knob, R (860, 318, 120, 120), "Level Out" },
                // The cut filters along the bottom
                { "lcOn", Kind::toggle, R (40, 500, 110, 28), "=LOW CUT" },
                { "lcSlope", Kind::dropdown, R (40, 546, 110, 28), "-" },
                { "lcFreq", knob, R (160, 470, 100, 120), "Freq" },
                { "lcRes", knob, R (265, 470, 100, 120), "Resonance" },
                { "lcPost", Kind::segmented, R (370, 520, 110, 28), "-" },
                { "hcOn", Kind::toggle, R (520, 500, 110, 28), "=HIGH CUT" },
                { "hcSlope", Kind::dropdown, R (520, 546, 110, 28), "-" },
                { "hcFreq", knob, R (640, 470, 100, 120), "Freq" },
                { "hcRes", knob, R (745, 470, 100, 120), "Resonance" },
                { "hcPost", Kind::segmented, R (850, 520, 110, 28), "-" },
            };
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                if (id == "bypassFreq") return valueOf (fx, v, "bypassOn") > 0.5f;
                if (id.startsWith ("lc") && id != "lcOn") return valueOf (fx, v, "lcOn") > 0.5f;
                if (id.startsWith ("hc") && id != "hcOn") return valueOf (fx, v, "hcOn") > 0.5f;
                return true;
            };
            d["saturator"] = c;
        }
        {
            // Step filter: burgundy, eight blue step pads, orange-ringed knobs
            Design c;
            c.width = 790; c.height = 470;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colour (0xffe9c8b8); c.value = juce::Colour (0xffffb060); c.accent = juce::Colour (0xffffa040); c.track = juce::Colour (0xff2a1012);
            c.valueAbove = true;
            c.paint = paintStepFilter;
            for (int k = 0; k < 8; ++k)
                c.controls.push_back ({ "s" + juce::String (k + 1), Kind::fader, R (40 + k * 92, 50, 80, 150), "-", juce::Colour (0xff3d8fe0) });
            c.controls.push_back ({ "note", Kind::dropdown, R (40, 300, 110, 30), "Step" });
            c.controls.push_back ({ "low", knob, R (170, 250, 100, 160), "Low" });
            c.controls.push_back ({ "high", knob, R (290, 250, 100, 160), "High" });
            c.controls.push_back ({ "resonance", knob, R (410, 250, 100, 160), "Resonance" });
            c.controls.push_back ({ "glide", knob, R (530, 250, 100, 160), "Glide" });
            c.controls.push_back ({ "mix", knob, R (650, 250, 100, 160), "Mix" });
            d["stepFilter"] = c;
        }
        {
            // Vocal transformer: moulded blue-grey panel, chrome knobs, green readouts
            Design c;
            c.width = 860; c.height = 500;
            c.knobs = KnobStyle::chrome;
            c.label = juce::Colour (0xff1d2833); c.value = juce::Colour (0xffd9ff4a); c.accent = juce::Colour (0xffd9ff4a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.lcdValues = true;
            c.paint = paintVocalTransformer;
            c.controls = {
                { "formant", knob, R (70, 60, 160, 210), "Formant" },
                { "pitch", knob, R (340, 60, 160, 210), "Pitch" },
                { "mode", Kind::segmented, R (600, 70, 230, 34), "Mode" },
                { "robotNote", Kind::dropdown, R (620, 170, 190, 30), "Pitch Base" },
                { "mix", knob, R (650, 230, 130, 110), "Mix" },
                { "tracking", knob, R (40, 380, 110, 110), "Tracking" },
                { "glide", knob, R (170, 380, 110, 110), "Glide" },
                { "grain", Kind::dropdown, R (310, 420, 130, 28), "Grain Size" },
                { "formants", Kind::segmented, R (470, 420, 240, 28), "Formants" },
                { "detune", knob, R (730, 380, 110, 110), "Detune" },
            };
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                return (id != "tracking" && id != "robotNote") || valueOf (fx, v, "mode") > 0.5f;
            };
            d["vocalTransformer"] = c;
        }
        {
            // Vocoder: blue-grey metal, a green filter bank display, chrome knobs
            Design c;
            c.width = 880; c.height = 420;
            c.knobs = KnobStyle::chrome;
            c.label = juce::Colour (0xff1d2833); c.value = juce::Colour (0xff58ff9a); c.accent = juce::Colour (0xff58ff9a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.lcdValues = true;
            c.paint = paintVocoder;
            c.controls = {
                { "attack", knob, R (30, 30, 120, 130), "Attack" },
                { "release", knob, R (170, 30, 120, 130), "Release" },
                { "bands", Kind::segmented, R (340, 220, 330, 30), "Bands" },
                { "low", knob, R (330, 270, 110, 130), "Lowest" },
                { "formant", knob, R (445, 270, 110, 130), "Formant Shift" },
                { "high", knob, R (560, 270, 110, 130), "Highest" },
                { "carrier", Kind::dropdown, R (40, 220, 220, 30), "Carrier" },
                { "note", Kind::dropdown, R (40, 300, 140, 30), "Carrier Note" },
                { "unvoiced", knob, R (725, 40, 130, 140), "U/V Level" },
                { "mix", knob, R (725, 240, 130, 150), "Mix" },
            };
            d["vocoder"] = c;
        }
    }
    //==============================================================================
    std::map<juce::String, Design> makeDesigns()
    {
        std::map<juce::String, Design> d;

        {
            Design c;
            c.width = 1300; c.height = 570;
            c.knobs = KnobStyle::silver;
            c.label = juce::Colours::white; c.value = juce::Colours::white; c.accent = juce::Colour (0xff6ec6ff); c.track = juce::Colour (0xff1a2a42);
            c.valueAbove = false;
            c.live = true;
            c.paint = paintCompressor;
            c.controls = {
                { "model", Kind::segmented, R (60, 16, 1180, 46), "-" },
                { "input", Kind::knob, R (25, 322, 110, 110), "-" },
                { "threshold", Kind::knob, R (165, 322, 110, 110), "-" },
                { "ratio", Kind::knob, R (305, 322, 110, 110), "-" },
                { "makeup", Kind::knob, R (445, 322, 110, 110), "-" },
                { "autoGain", Kind::segmented, R (585, 345, 150, 34), "-" },
                { "knee", Kind::knob, R (175, 462, 90, 90), "-" },
                { "attack", Kind::knob, R (315, 462, 90, 90), "-" },
                { "release", Kind::knob, R (455, 462, 90, 90), "-" },
                { "autoRelease", Kind::toggle, R (585, 485, 110, 34), "-" },
                { "output", Kind::knob, R (695, 124, 110, 110), "-" },
                { "detect", Kind::segmented, R (850, 82, 200, 32), "-" },
                { "peakRms", Kind::segmented, R (850, 124, 200, 32), "-" },
                { "scFilter", Kind::toggle, R (850, 196, 95, 32), "-" },
                { "listen", Kind::toggle, R (955, 196, 95, 32), "-" },
                { "scMode", Kind::segmented, R (845, 252, 215, 32), "-" },
                { "scHpf", Kind::knob, R (845, 322, 95, 100), "-" },
                { "scQ", Kind::knob, R (960, 322, 95, 100), "-" },
                { "scGain", Kind::knob, R (902, 456, 95, 100), "-" },
                { "limiter", Kind::toggle, R (1090, 92, 90, 32), "-" },
                { "limThr", Kind::knob, R (1135, 160, 95, 100), "-" },
                { "distortion", Kind::knob, R (1080, 322, 100, 100), "-" },
                { "mix", Kind::knob, R (1190, 322, 95, 100), "-" },
            };
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                // (the controls each circuit has; the side chain filter's settings only with the filter on)
                const auto circuit = juce::roundToInt (valueOf (fx, v, "model", 1.0f));
                if (id == "knee")        return circuit <= 2;
                if (id == "attack")      return circuit != 3 && circuit != 6;
                if (id == "release")     return circuit != 3;
                if (id == "autoRelease") return circuit != 3 && circuit != 6;
                if (id == "peakRms")     return circuit == 0;
                if (id == "scMode" || id == "scHpf" || id == "scQ" || id == "scGain" || id == "listen")
                    return valueOf (fx, v, "scFilter") > 0.5f;
                if (id == "limThr")      return valueOf (fx, v, "limiter") > 0.5f;
                return true;
            };
            d["studioComp"] = c;
        }
        {
            Design c;
            c.width = 1100; c.height = 490;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.55f); c.value = juce::Colours::white; c.accent = juce::Colour (0xff4fa3ff); c.track = juce::Colour (0xff0d131c);
            c.glassFace = true;
            c.paint = [] (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float live)
            {
                paintChannelEq (g, b, info, v, live);
                paintChannelEqIcons (g, b.getWidth(), v);
                // The bar along the bottom
                if (theme::glassSkin())
                {
                    look::drawGlassPanel (g, RF (14.0f, (float) b.getHeight() - 58.0f, (float) b.getWidth() - 28.0f, 46.0f), 12.0f, true);
                    return;
                }
                g.setColour (juce::Colour (0xff0b1018));
                g.fillRect (0, b.getHeight() - 56, b.getWidth(), 56);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRect (0, b.getHeight() - 56, b.getWidth(), 1);
            };
            // The values under the screen, one column per band in its colour: frequency, gain (a cut's slope), Q
            const auto screenW = 1100 - 160;
            const auto w = screenW / 8;
            const auto column = [w] (int b) { return 40 + w * b; };
            const std::vector<std::vector<const char*>> bands {
                { "hpFreq", "hpSlope", "hpQ" }, { "lsFreq", "lsGain", "lsQ" }, { "p1Freq", "p1Gain", "p1Q" }, { "p2Freq", "p2Gain", "p2Q" },
                { "p3Freq", "p3Gain", "p3Q" }, { "p4Freq", "p4Gain", "p4Q" }, { "hsFreq", "hsGain", "hsQ" }, { "lpFreq", "lpSlope", "lpQ" } };
            for (int b = 0; b < 8; ++b)
            {
                for (size_t row = 0; row < bands[(size_t) b].size(); ++row)
                    c.controls.push_back ({ bands[(size_t) b][row], Kind::field, R (column (b), 322 + (int) row * 30, w, 28), "-", bandColours[b] });
                c.controls.push_back ({ "on" + juce::String (b + 1), Kind::hotspot, R (column (b) + w / 2 - 26, 6, 52, 36), "-" });
            }
            c.controls.push_back ({ "output", Kind::fader, R (1100 - 92, 70, 60, 240), "Gain", juce::Colours::white.withAlpha (0.7f) });
            c.controls.push_back ({ "analyzer", Kind::toggle, R (40, 446, 100, 30), "=ANALYZER" });
            c.controls.push_back ({ "analyzerPost", Kind::segmented, R (150, 446, 120, 30), "-" });
            c.controls.push_back ({ "qCouple", Kind::toggle, R (300, 446, 100, 30), "=Q-COUPLE" });
            c.controls.push_back ({ "hq", Kind::toggle, R (410, 446, 60, 30), "=HQ" });
            c.controls.push_back ({ "processing", Kind::dropdown, R (840, 446, 150, 30), "-" });
            // The curve's points: drag them
            c.graphs.push_back ({ R (40, 48, screenW, 262), { { "hpFreq", {}, "hpQ", "on1" }, { "lsFreq", "lsGain", "lsQ", "on2" }, { "p1Freq", "p1Gain", "p1Q", "on3" }, { "p2Freq", "p2Gain", "p2Q", "on4" },
                        { "p3Freq", "p3Gain", "p3Q", "on5" }, { "p4Freq", "p4Gain", "p4Q", "on6" }, { "hsFreq", "hsGain", "hsQ", "on7" }, { "lpFreq", {}, "lpQ", "on8" } } });
            c.analyzer = true;
            d["linearEq"] = c;
            // The Channel EQ's dynamic EQ: under each shelf and bell, DYN, its threshold and range; attack and release
            {
                auto& dyn = c;
                dyn.height = 600;
                for (auto& ctl : dyn.controls)
                    if (ctl.bounds.getY() == 446)
                        ctl.bounds.setY (556);
                for (int b = 1; b <= 6; ++b)
                {
                    const auto n = juce::String (b + 1);
                    dyn.controls.push_back ({ "dyn" + n, Kind::toggle, R (column (b) + 8, 418, w - 16, 26), "=DYN", bandColours[b] });
                    dyn.controls.push_back ({ "thr" + n, Kind::field, R (column (b), 448, w, 28), "-", bandColours[b] });
                    dyn.controls.push_back ({ "rng" + n, Kind::field, R (column (b), 478, w, 28), "-", bandColours[b] });
                }
                dyn.controls.push_back ({ "dynAttack", Kind::field, R (column (0), 448, w, 28), "-", juce::Colours::white.withAlpha (0.7f) });
                dyn.controls.push_back ({ "dynRelease", Kind::field, R (column (7), 448, w, 28), "-", juce::Colours::white.withAlpha (0.7f) });
            }
            d["channelEq"] = c;
            // (the linear phase one has no oversampling or one-side processing: always both, always exact)
            d["linearEq"].enabled = [] (const juce::String& id, const EffectInfo&, const float*) { return id != "hq" && id != "processing"; };
        }
        {
            Design c;
            c.width = 1000; c.height = 650;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colour (0xffc9d6e0); c.value = juce::Colour (0xff8fe39a); c.accent = juce::Colour (0xff8fe39a); c.track = juce::Colour (0xff1e2a38);
            c.paint = paintTapeDelay;
            c.controls = {
                { "sync", Kind::toggle, R (40, 104, 70, 34), "Tempo Sync" },
                { "time", Kind::knob, R (140, 82, 130, 190), "Delay Time" },
                { "note", Kind::dropdown, R (290, 104, 80, 30), "Note" },
                { "deviation", Kind::knob, R (285, 210, 90, 120), "Deviation" },
                { "smoothing", Kind::knob, R (410, 70, 110, 112), "Smoothing" },
                { "clip", Kind::knob, R (522, 70, 110, 112), "Clip Threshold" },
                { "spread", Kind::knob, R (634, 70, 110, 112), "Spread" },
                { "head", Kind::segmented, R (440, 212, 280, 30), "Tape Head Mode" },
                { "lowCut", Kind::knob, R (440, 270, 110, 118), "Low Cut" },
                { "highCut", Kind::knob, R (610, 270, 110, 118), "High Cut" },
                { "feedback", Kind::knob, R (810, 82, 130, 170), "Feedback" },
                { "freeze", Kind::toggle, R (830, 266, 90, 32), "=FREEZE" },
                { "lfoRate", Kind::knob, R (50, 470, 130, 150), "LFO Rate" },
                { "wow", Kind::knob, R (220, 470, 130, 150), "LFO Intensity" },
                { "flutterRate", Kind::knob, R (400, 470, 130, 150), "Flutter Rate" },
                { "flutter", Kind::knob, R (570, 470, 130, 150), "Flutter Intensity" },
                { "dry", Kind::fader, R (795, 390, 60, 240), "Dry" },
                { "mix", Kind::fader, R (895, 390, 60, 240), "Wet" },
            };
            d["tapeDelay"] = c;
        }
        {
            Design c;
            c.width = 1000; c.height = 720;
            c.knobs = KnobStyle::light;
            c.label = juce::Colour (0xff3b3b3b); c.value = juce::Colour (0xffff6a3d); c.accent = juce::Colour (0xffff6a3d); c.track = juce::Colour (0xffa9a9a9);
            c.paint = paintStudioReverb;
            c.glassFace = true;
            c.live = true;   // (the Liquid Glass particles move)
            c.pageParam = "view";
            const auto white = juce::Colours::white.withAlpha (0.6f), orange = juce::Colour (0xffff6a3d);
            c.controls = {
                { "space", Kind::dropdown, R (30, 14, 250, 36), "-" },
                { "view", Kind::segmented, R (820, 16, 150, 30), "-" },
                { "dry", Kind::fader, R (855, 450, 50, 240), "Dry" },
                { "mix", Kind::fader, R (925, 450, 50, 240), "Wet" },
                // Main: the damping EQ's values under the display, the main knobs below
                { "dLsFreq", Kind::field, R (60, 340, 200, 24), "-", orange, 1 },  { "dLsRatio", Kind::field, R (60, 366, 200, 24), "-", orange, 1 },
                { "dP1Freq", Kind::field, R (280, 340, 200, 24), "-", orange, 1 }, { "dP1Ratio", Kind::field, R (280, 366, 200, 24), "-", orange, 1 },
                { "dP1Q", Kind::field, R (280, 392, 200, 24), "-", orange, 1 },
                { "dP2Freq", Kind::field, R (500, 340, 200, 24), "-", orange, 1 }, { "dP2Ratio", Kind::field, R (500, 366, 200, 24), "-", orange, 1 },
                { "dP2Q", Kind::field, R (500, 392, 200, 24), "-", orange, 1 },
                { "dHsFreq", Kind::field, R (720, 340, 200, 24), "-", orange, 1 }, { "dHsRatio", Kind::field, R (720, 366, 200, 24), "-", orange, 1 },
                { "attack", Kind::knob, R (25, 460, 110, 130), "Attack", {}, 1 },
                { "size", Kind::knob, R (140, 460, 110, 130), "Size", {}, 1 },
                { "diffusion", Kind::knob, R (255, 460, 110, 130), "Density", {}, 1 },
                { "predelay", Kind::knob, R (380, 460, 110, 130), "Predelay", {}, 1 },
                { "predelaySync", Kind::toggle, R (390, 610, 90, 28), "=SYNC", {}, 1 },
                { "predelayNote", Kind::dropdown, R (385, 660, 100, 28), "-", {}, 1 },
                { "decay", Kind::knob, R (520, 450, 150, 180), "Decay", {}, 1 },
                { "decaySync", Kind::toggle, R (550, 640, 90, 28), "=SYNC", {}, 1 },
                { "decayNote", Kind::dropdown, R (545, 676, 100, 28), "-", {}, 1 },
                { "distance", Kind::knob, R (700, 460, 110, 130), "Distance", {}, 1 },
                { "freeze", Kind::toggle, R (710, 620, 90, 30), "=FREEZE", {}, 1 },
                // Details: the output EQ's bands under the display, the finer controls below
                { "eqOn", Kind::toggle, R (850, 316, 120, 24), "=OUTPUT EQ", {}, 2 },
                { "quality", Kind::segmented, R (30, 470, 240, 30), "Quality", {}, 2 },
                { "modSource", Kind::segmented, R (300, 470, 240, 30), "Mod Source", {}, 2 },
                { "monoMaker", Kind::toggle, R (580, 470, 130, 30), "=MONO MAKER", {}, 2 },
                { "modSpeed", Kind::knob, R (20, 550, 110, 130), "Mod Speed", {}, 2 },
                { "mod", Kind::knob, R (155, 550, 110, 130), "Mod Depth", {}, 2 },
                { "smoothing", Kind::knob, R (290, 550, 110, 130), "Smoothing", {}, 2 },
                { "earlyLate", Kind::knob, R (425, 550, 110, 130), "Early/Late Mix", {}, 2 },
                { "width", Kind::knob, R (560, 550, 110, 130), "Width", {}, 2 },
                { "monoFreq", Kind::knob, R (695, 550, 110, 130), "Mono Maker", {}, 2 },
            };
            // (the output EQ's six columns: on/off, frequency, gain, Q)
            const std::vector<std::vector<const char*>> eqBands {
                { "eqHpOn", "lowCut" }, { "eqLsOn", "eqLsFreq", "eqLsGain" }, { "eqP1On", "eqP1Freq", "eqP1Gain", "eqP1Q" },
                { "eqP2On", "eqP2Freq", "eqP2Gain", "eqP2Q" }, { "eqHsOn", "eqHsFreq", "eqHsGain" }, { "eqLpOn", "highCut" } };
            const char* eqNames[] { "=LOW CUT", "=LOW SHELF", "=LOW MID", "=HIGH MID", "=HIGH SHELF", "=HIGH CUT" };
            for (int b = 0; b < 6; ++b)
            {
                const auto x = 60 + b * 132;
                c.controls.push_back ({ eqBands[(size_t) b][0], Kind::toggle, R (x + 10, 316, 110, 22), eqNames[b], {}, 2 });
                for (size_t row = 1; row < eqBands[(size_t) b].size(); ++row)
                    c.controls.push_back ({ eqBands[(size_t) b][row], Kind::field, R (x, 342 + (int) (row - 1) * 24, 130, 22), "-", reverbEqColours[b], 2 });
            }
            c.graphs.push_back ({ R (60, 66, 880, 236), { { "dLsFreq", "dLsRatio", {}, {} }, { "dP1Freq", "dP1Ratio", "dP1Q", {} },
                                                          { "dP2Freq", "dP2Ratio", "dP2Q", {} }, { "dHsFreq", "dHsRatio", {}, {} } }, 1 });
            c.graphs.push_back ({ R (60, 66, 880, 236), { { "lowCut", {}, {}, "eqHpOn" }, { "eqLsFreq", "eqLsGain", {}, "eqLsOn" },
                                                          { "eqP1Freq", "eqP1Gain", "eqP1Q", "eqP1On" }, { "eqP2Freq", "eqP2Gain", "eqP2Q", "eqP2On" },
                                                          { "eqHsFreq", "eqHsGain", {}, "eqHsOn" }, { "highCut", {}, {}, "eqLpOn" } }, 2 });
            d["studioReverb"] = c;
        }
        {
            Design c;
            c.width = 940; c.height = 600;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xff4fb3e0); c.accent = juce::Colour (0xff4fb3e0); c.track = juce::Colour (0xff141a22);
            c.live = true;
            c.paint = paintPitchCorrection;
            c.glassFace = true;
            c.pageParam = "kbMode";
            c.controls = {
                { "key", Kind::dropdown, R (40, 52, 160, 30), "Root Note" },
                { "scale", Kind::dropdown, R (40, 132, 190, 30), "Scale/Chord" },
                { "bypassAll", Kind::toggle, R (40, 196, 160, 28), "=BYPASS ALL" },
                { "range", Kind::segmented, R (40, 268, 180, 28), "Pitch Range" },
                { "live", Kind::segmented, R (760, 332, 150, 26), "-" },
                { "kbMode", Kind::segmented, R (290, 332, 400, 26), "-" },
                { "response", Kind::knob, R (40, 420, 120, 160), "Response" },
                { "tolerance", Kind::knob, R (210, 420, 120, 160), "Tolerance" },
                { "amount", Kind::knob, R (380, 420, 120, 160), "Amount" },
                { "reference", Kind::knob, R (620, 420, 120, 160), "Reference" },
                { "detune", Kind::knob, R (770, 420, 120, 160), "Detune" },
            };
            // The keyboard's keys: click to leave a note out of the scale (Edit Scale), or to bypass it (Bypass Notes)
            {
                const RF keys (250.0f, 120.0f, 480.0f, 200.0f);
                const auto whiteW = keys.getWidth() / 7.0f;
                const int whites[] { 0, 2, 4, 5, 7, 9, 11 }, blacks[] { 1, 3, 6, 8, 10 };
                const float blackAt[] { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };
                for (int page = 1; page <= 2; ++page)
                {
                    const juce::String prefix (page == 1 ? "n" : "bp");
                    for (int w = 0; w < 7; ++w)
                        c.controls.push_back ({ prefix + juce::String (whites[w] + 1), Kind::hotspot,
                                                RF (keys.getX() + whiteW * (float) w, keys.getY(), whiteW - 3.0f, keys.getHeight()).toNearestInt(), "-", {}, page });
                    for (int k = 0; k < 5; ++k)
                        c.controls.push_back ({ prefix + juce::String (blacks[k] + 1), Kind::hotspot,
                                                RF (keys.getX() + whiteW * blackAt[k] - whiteW * 0.33f - 1.5f, keys.getY(), whiteW * 0.66f, keys.getHeight() * 0.58f).toNearestInt(),
                                                "-", {}, page });
                }
            }
            d["pitchCorrection"] = c;
        }
        {
            // Multipressor: display on top, a column per band
            Design c;
            c.width = 1160; c.height = 680;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.7f); c.value = juce::Colour (0xff8fe39a); c.accent = juce::Colour (0xff8fe39a); c.track = juce::Colour (0xff17263a);
            c.live = true;
            c.meters = true;     // (each band's gain change, the levels in and out)
            c.analyzer = true;   // (the spectrum behind the bands)
            c.paint = paintMultipressor;
            c.glassFace = true;
            const char* fields[] { "Thr", "Ratio", "ExpThr", "ExpRatio", "ExpRed", "Rms", "Attack", "Release", "Gain" };
            for (int b = 0; b < 4; ++b)
            {
                const auto id = "b" + juce::String (b + 1);
                const auto x = 190 + b * 240;
                c.controls.push_back ({ id + "On", Kind::toggle, R (x + 20, 298, 85, 28), "=ON" });
                c.controls.push_back ({ id + "Solo", Kind::toggle, R (x + 115, 298, 85, 28), "=SOLO" });
                for (int row = 0; row < 9; ++row)
                    c.controls.push_back ({ id + fields[row], Kind::field, R (x, 339 + row * 30, 220, 28), "-", multiColours[b] });
            }
            c.controls.push_back ({ "autoGain", Kind::toggle, R (190, 632, 120, 30), "=AUTO GAIN" });
            c.controls.push_back ({ "lookahead", Kind::field, R (430, 632, 200, 30), "-", juce::Colours::white });
            c.controls.push_back ({ "output", Kind::field, R (790, 632, 200, 30), "-", juce::Colours::white });
            // Drag the crossovers across, each band's level up and down (as Logic's)
            c.graphs.push_back ({ multiScreen.toNearestInt(), { { "x1", {}, {}, {} }, { "x2", {}, {}, {} }, { "x3", {}, {}, {} },
                                                               { {}, "b1Gain", {}, "b1On", {}, "x1" }, { {}, "b2Gain", {}, "b2On", "x1", "x2" },
                                                               { {}, "b3Gain", {}, "b3On", "x2", "x3" }, { {}, "b4Gain", {}, "b4On", "x3", {} } }, 0 });
            d["multiband"] = c;
        }
        {
            // Formant Filterbank: bank A and bank B, twenty faders each
            Design c;
            c.width = 1240; c.height = 620;
            c.knobs = KnobStyle::chrome;
            c.label = juce::Colour (0xff1d2833); c.value = juce::Colour (0xff58ff9a); c.accent = juce::Colour (0xff58ff9a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.lcdValues = true;
            c.paint = paintFormantBank;
            for (int k = 0; k < 20; ++k)
            {
                c.controls.push_back ({ "a" + juce::String (k + 1), Kind::fader, R (70 + k * 44, 100, 34, 148), "-", juce::Colour (0xff59b8ff) });
                c.controls.push_back ({ "b" + juce::String (k + 1), Kind::fader, R (70 + k * 44, 270, 34, 148), "-", juce::Colour (0xff6ef2a2) });
            }
            c.controls.push_back ({ "low", Kind::field, R (60, 52, 140, 26), "-", juce::Colour (0xff1c2a36) });
            c.controls.push_back ({ "high", Kind::field, R (820, 52, 140, 26), "-", juce::Colour (0xff1c2a36) });
            c.controls.push_back ({ "bands", Kind::dropdown, R (430, 52, 120, 28), "Bands" });
            c.controls.push_back ({ "lowest", Kind::segmented, R (220, 52, 190, 28), "Lowest" });
            c.controls.push_back ({ "highest", Kind::segmented, R (580, 52, 190, 28), "Highest" });
            c.controls.push_back ({ "fade", Kind::fader, R (1010, 100, 50, 320), "Fade A/B" });
            c.controls.push_back ({ "shift", Kind::knob, R (1090, 70, 130, 130), "Formant Shift" });
            c.controls.push_back ({ "resonance", Kind::knob, R (1090, 240, 130, 130), "Resonance" });
            c.controls.push_back ({ "slope", Kind::segmented, R (1090, 410, 130, 28), "Slope" });
            c.controls.push_back ({ "shiftLfo", Kind::knob, R (20, 500, 100, 110), "Shift LFO" });
            c.controls.push_back ({ "shiftRate", Kind::knob, R (130, 500, 100, 110), "Rate" });
            c.controls.push_back ({ "shiftWave", Kind::dropdown, R (240, 540, 130, 28), "Wave" });
            c.controls.push_back ({ "fadeLfo", Kind::knob, R (400, 500, 100, 110), "Fade LFO" });
            c.controls.push_back ({ "fadeRate", Kind::knob, R (510, 500, 100, 110), "Rate" });
            c.controls.push_back ({ "fadeWave", Kind::dropdown, R (620, 540, 130, 28), "Wave" });
            c.controls.push_back ({ "overdrive", Kind::toggle, R (800, 540, 120, 28), "=OVERDRIVE" });
            c.controls.push_back ({ "width", Kind::knob, R (950, 500, 100, 110), "Stereo Width" });
            c.controls.push_back ({ "level", Kind::knob, R (1080, 500, 100, 110), "Level" });
            d["formantBank"] = c;
        }
        {
            // Space Designer
            Design c;
            c.width = 1100; c.height = 660;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.65f); c.value = juce::Colour (0xffff9a3c); c.accent = juce::Colour (0xffff9a3c); c.track = juce::Colour (0xff15181c);
            c.paint = paintSpaceDesigner;
            c.pageParam = "view";
            const auto orange = juce::Colour (0xffff9a3c), blue = juce::Colour (0xff4fb7ff), grey = juce::Colour (0xffc9ced6);
            c.controls = {
                { "mode", Kind::segmented, R (20, 14, 300, 30), "-" },
                { "irFile", Kind::fileButton, R (340, 14, 330, 30), "-" },
                { "view", Kind::segmented, R (600, 320, 480, 26), "-" },
                // Volume envelope bar
                { "volOn", Kind::toggle, R (30, 322, 70, 24), "=ON", {}, 1 },
                { "initLevel", Kind::field, R (110, 340, 100, 30), "-", orange, 1 },
                { "attack", Kind::field, R (215, 340, 100, 30), "-", orange, 1 },
                { "decay", Kind::field, R (320, 340, 100, 30), "-", orange, 1 },
                { "endLevel", Kind::field, R (425, 340, 100, 30), "-", orange, 1 },
                { "exp", Kind::segmented, R (530, 344, 60, 24), "-", {}, 1 },
                // Filter envelope bar
                { "filtOn", Kind::toggle, R (30, 322, 70, 24), "=ON", {}, 2 },
                { "filtType", Kind::dropdown, R (30, 350, 80, 24), "-", {}, 2 },
                { "filtRes", Kind::field, R (115, 340, 70, 30), "-", blue, 2 },
                { "initFreq", Kind::field, R (190, 340, 85, 30), "-", blue, 2 },
                { "filtAttack", Kind::field, R (280, 340, 75, 30), "-", blue, 2 },
                { "breakFreq", Kind::field, R (360, 340, 85, 30), "-", blue, 2 },
                { "filtDecay", Kind::field, R (450, 340, 70, 30), "-", blue, 2 },
                { "endFreq", Kind::field, R (520, 340, 75, 30), "-", blue, 2 },
                // Density envelope bar
                { "densOn", Kind::toggle, R (30, 322, 70, 24), "=ON", {}, 3 },
                { "initDensity", Kind::field, R (110, 340, 110, 30), "-", grey, 3 },
                { "ramp", Kind::field, R (225, 340, 110, 30), "-", grey, 3 },
                { "endDensity", Kind::field, R (340, 340, 110, 30), "-", grey, 3 },
                { "reflShape", Kind::field, R (455, 340, 110, 30), "-", grey, 3 },
                // The globals
                { "quality", Kind::segmented, R (30, 430, 260, 28), "Quality" },
                { "input", Kind::segmented, R (310, 430, 240, 28), "Input" },
                { "reverse", Kind::toggle, R (570, 430, 100, 28), "=REVERSE" },
                { "size", Kind::knob, R (20, 490, 100, 150), "Size" },
                { "length", Kind::knob, R (125, 490, 100, 150), "Length" },
                { "predelay", Kind::knob, R (230, 490, 100, 150), "Pre-Delay" },
                { "early", Kind::knob, R (335, 490, 100, 150), "Early" },
                { "width", Kind::knob, R (440, 490, 100, 150), "Spread" },
                { "lowCut", Kind::knob, R (545, 490, 100, 150), "Low Cut" },
                { "xover", Kind::knob, R (650, 490, 100, 150), "X-Over" },
                { "direct", Kind::fader, R (900, 420, 60, 220), "Direct" },
                { "mix", Kind::fader, R (990, 420, 60, 220), "Reverb" },
                { "eqOn", Kind::toggle, R (690, 430, 100, 28), "=OUTPUT EQ" },
            };
            // The output EQ's bands in the bar on its page
            const std::vector<std::vector<const char*>> eqBands {
                { "eqHpOn", "eqHpFreq" }, { "eqLsOn", "eqLsFreq", "eqLsGain" }, { "eqP1On", "eqP1Freq", "eqP1Gain", "eqP1Q" },
                { "eqP2On", "eqP2Freq", "eqP2Gain", "eqP2Q" }, { "eqHsOn", "eqHsFreq", "eqHsGain" }, { "eqLpOn", "eqLpFreq" } };
            for (int b = 0; b < 6; ++b)
            {
                const auto x = 30 + b * 95;
                c.controls.push_back ({ eqBands[(size_t) b][0], Kind::toggle, R (x, 318, 40, 20), "=" + juce::String (b + 1), {}, 4 });
                for (size_t row = 1; row < eqBands[(size_t) b].size(); ++row)
                    c.controls.push_back ({ eqBands[(size_t) b][row], Kind::field, R (x - 10, 338 + (int) (row - 1) * 18, 90, 18), "-", reverbEqColours[b], 4 });
            }
            c.graphs.push_back ({ R (30, 66, 1040, 230), { { "eqHpFreq", {}, {}, "eqHpOn" }, { "eqLsFreq", "eqLsGain", {}, "eqLsOn" },
                                                           { "eqP1Freq", "eqP1Gain", "eqP1Q", "eqP1On" }, { "eqP2Freq", "eqP2Gain", "eqP2Q", "eqP2On" },
                                                           { "eqHsFreq", "eqHsGain", {}, "eqHsOn" }, { "eqLpFreq", {}, {}, "eqLpOn" } }, 4 });
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                const auto sampled = valueOf (fx, v, "mode", 1.0f) < 0.5f;
                if (id == "irFile") return sampled;
                if (id == "early" || id == "densOn" || id == "initDensity" || id == "ramp" || id == "endDensity" || id == "reflShape") return ! sampled;
                return true;
            };
            d["convSpace"] = c;
        }
        {
            // Pedalboard
            const auto knob = Kind::knob;
            Design c;
            c.width = 1220; c.height = 500;
            c.knobs = KnobStyle::black;
            c.label = juce::Colours::black.withAlpha (0.75f); c.value = juce::Colours::white; c.accent = juce::Colours::white; c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintPedalboard;
            for (int s = 0; s < 6; ++s)
            {
                const auto n = juce::String (s + 1);
                const auto x = 20 + s * 200;
                c.controls.push_back ({ "p" + n + "Type", Kind::dropdown, R (x + 10, 30, 160, 28), "-", juce::Colours::white });
                c.controls.push_back ({ "p" + n + "K1", knob, R (x + 15, 86, 70, 70), "-" });
                c.controls.push_back ({ "p" + n + "K2", knob, R (x + 95, 86, 70, 70), "-" });
                c.controls.push_back ({ "p" + n + "K3", knob, R (x + 55, 182, 70, 70), "-" });
                c.controls.push_back ({ "p" + n + "On", Kind::toggle, R (x + 55, 300, 70, 50), "=ON/OFF" });
            }
            c.controls.push_back ({ "mix", knob, R (470, 392, 90, 100), "MIX", juce::Colour (0xffc9ccd0) });
            c.controls.push_back ({ "output", knob, R (660, 392, 90, 100), "OUTPUT", juce::Colour (0xffc9ccd0) });
            d["pedalboard"] = c;
        }
        {
            // The meters: their screens, a control or two
            const auto meter = [&d] (const char* type, int w, int h, std::function<void (juce::Graphics&, R, const EffectInfo&, const float*, float)> paint,
                                     std::vector<Control> controls)
            {
                Design c;
                c.width = w; c.height = h;
                c.knobs = KnobStyle::dark;
                c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xff8fe39a); c.accent = juce::Colour (0xff8fe39a); c.track = juce::Colour (0xff17263a);
                c.meters = true;
                c.paint = std::move (paint);
                c.controls = std::move (controls);
                d[type] = c;
            };
            meter ("levelMeter", 220, 400, paintLevelMeter, { { "reset", Kind::toggle, R (60, 366, 110, 26), "=RESET HOLD" } });
            meter ("loudnessMeter", 520, 420, paintLoudnessMeter, { { "running", Kind::segmented, R (300, 300, 200, 28), "-" },
                                                                   { "reset", Kind::toggle, R (300, 350, 120, 28), "=RESET" } });
            meter ("correlationMeter", 460, 140, paintCorrelation, {});
            meter ("multiMeter", 820, 400, paintMultiMeter, { { "view", Kind::segmented, R (20, 12, 260, 28), "-" },
                                                             { "peakHold", Kind::toggle, R (300, 12, 120, 28), "=PEAK HOLD" },
                                                             { "tilt", Kind::dropdown, R (440, 12, 140, 28), "-", juce::Colours::white } });
            meter ("tuner", 520, 400, paintTuner, { { "reference", Kind::field, R (160, 368, 200, 26), "-", juce::Colours::white } });
            meter ("bpmCounter", 320, 150, paintBpmCounter, {});
            Design m;
            m.width = 1120; m.height = 610;
            m.knobs = KnobStyle::dark;
            m.label = juce::Colours::white.withAlpha (0.6f); m.value = juce::Colour (0xffb07bff); m.accent = juce::Colour (0xffb07bff); m.track = juce::Colour (0xff17263a);
            m.meters = true;
            m.analyzer = true;
            m.paint = paintMasteringAssistant;
            m.controls = {
                { "character", Kind::segmented, R (440, 18, 480, 30), "-" },
                { "autoEq", Kind::knob, R (30, 330, 120, 170), "Auto EQ" },
                { "loudness", Kind::knob, R (170, 330, 120, 170), "Loudness" },
                { "width", Kind::knob, R (310, 330, 120, 170), "Width" },
                { "excite", Kind::toggle, R (460, 360, 110, 28), "=EXCITE" },
                { "loudComp", Kind::toggle, R (460, 400, 110, 28), "=LOUDNESS COMP" },
                { "customOn", Kind::toggle, R (600, 360, 110, 28), "=CUSTOM EQ" },
                { "customLow", Kind::knob, R (590, 400, 80, 124), "Low" },
                { "customMid", Kind::knob, R (680, 400, 80, 124), "Mid" },
                { "customHigh", Kind::knob, R (770, 400, 80, 124), "High" },
                { "reanalyze", Kind::toggle, R (750, 290, 180, 28), "=REANALYZE" },
                { "bypass", Kind::toggle, R (460, 440, 110, 28), "=BYPASS" },
            };
            d["masteringAssistant"] = m;
        }
        {
            // Phat FX
            const auto knob = Kind::knob, drop = Kind::dropdown, toggle = Kind::toggle;
            const auto ink = juce::Colour (0xffe9e9ea);
            Design c;
            c.width = 1200; c.height = 720;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = phatAccent; c.accent = phatAccent; c.track = juce::Colour (0xff121316);
            c.paint = paintPhatFx;
            c.controls = {
                // Bandpass
                { "bpOn", toggle, R (20, 20, 174, 24), "=BANDPASS" }, { "bpType", drop, R (20, 50, 174, 24), "-", ink },
                { "bpLow", knob, R (16, 84, 60, 100), "Low" }, { "bpHigh", knob, R (76, 84, 60, 100), "High" }, { "bpReject", knob, R (136, 84, 60, 100), "Reject" },
                { "bpLowRes", knob, R (36, 190, 60, 100), "Low Res" }, { "bpHighRes", knob, R (110, 190, 60, 100), "High Res" },
                // Filter
                { "fOn", toggle, R (218, 20, 224, 24), "=FILTER" }, { "fType", drop, R (218, 50, 224, 24), "-", ink },
                { "fCutoff", knob, R (226, 84, 100, 100), "Cutoff" }, { "fRes", knob, R (334, 84, 100, 100), "Res" },
                { "fDrive", knob, R (226, 190, 100, 100), "Drive" }, { "fMix", knob, R (334, 190, 100, 100), "Mix" },
                // Distortion
                { "dOn", toggle, R (466, 20, 234, 24), "=DISTORTION" },
                { "dType1", drop, R (464, 50, 78, 24), "-", ink }, { "dType2", drop, R (544, 50, 78, 24), "-", ink }, { "dType3", drop, R (624, 50, 78, 24), "-", ink },
                { "dAmt1", knob, R (466, 84, 74, 100), "Amount 1" }, { "dAmt2", knob, R (546, 84, 74, 100), "Amount 2" }, { "dAmt3", knob, R (626, 84, 74, 100), "Amount 3" },
                // Mod FX
                { "mOn", toggle, R (724, 20, 134, 24), "=MOD FX" }, { "mType", drop, R (724, 50, 134, 24), "-", ink },
                { "mRate", knob, R (746, 84, 90, 100), "Rate" }, { "mMix", knob, R (746, 190, 90, 100), "Mix" },
                // Bass Enhancer
                { "bOn", toggle, R (882, 20, 134, 24), "=BASS ENHANCER" }, { "bType", drop, R (882, 50, 134, 24), "-", ink },
                { "bAmount", knob, R (904, 84, 90, 100), "Amount" }, { "bTune", knob, R (904, 190, 90, 100), "Tune" },
                // Compressor
                { "cOn", toggle, R (1040, 20, 140, 24), "=COMPRESSOR" }, { "cType", drop, R (1040, 50, 140, 24), "-", ink },
                { "cAmount", knob, R (1065, 84, 90, 100), "Amount" }, { "cRelease", knob, R (1065, 190, 90, 100), "Release" },
                // Envelope follower
                { "eOn", toggle, R (576, 364, 204, 24), "=ENVELOPE FOLLOWER" }, { "eTarget", drop, R (576, 410, 204, 24), "-", ink },
                { "eAttack", knob, R (580, 446, 96, 110), "Attack" }, { "eRelease", knob, R (680, 446, 96, 110), "Release" },
                { "eDepth", knob, R (630, 570, 96, 110), "Depth" },
                // XY pad
                { "xOn", toggle, R (964, 386, 84, 22), "=X" }, { "xT1", drop, R (964, 412, 84, 22), "-", ink }, { "xD1", Kind::field, R (964, 436, 84, 20), "-", ink },
                { "xT2", drop, R (964, 460, 84, 22), "-", ink }, { "xD2", Kind::field, R (964, 484, 84, 20), "-", ink },
                { "yOn", toggle, R (806, 548, 60, 22), "=Y" }, { "yT1", drop, R (872, 548, 90, 22), "-", ink }, { "yD1", Kind::field, R (966, 548, 82, 22), "-", ink },
                { "yT2", drop, R (872, 576, 90, 22), "-", ink }, { "yD2", Kind::field, R (966, 576, 82, 22), "-", ink },
                // Master
                { "limiter", drop, R (1072, 384, 108, 24), "-", ink },
                { "input", knob, R (1081, 414, 90, 96), "Input" }, { "mix", knob, R (1081, 512, 90, 96), "Mix" }, { "output", knob, R (1081, 608, 90, 96), "Output" },
            };
            for (int s = 0; s < 6; ++s)
                c.controls.push_back ({ "slot" + juce::String (s + 1), drop, R (92 + s * 182, 314, 150, 24), "-", ink });
            for (int k = 0; k < 2; ++k)
            {
                const auto n = juce::String (k + 1);
                const auto x = 12 + k * 278;
                c.controls.push_back ({ "l" + n + "On", toggle, R (x + 8, 364, 90, 24), "=LFO " + n });
                c.controls.push_back ({ "l" + n + "Wave", drop, R (x + 104, 364, 158, 24), "-", ink });
                c.controls.push_back ({ "l" + n + "Target", drop, R (x + 8, 410, 254, 24), "-", ink });
                c.controls.push_back ({ "l" + n + "Rate", knob, R (x + 8, 446, 84, 110), "Rate" });
                c.controls.push_back ({ "l" + n + "Depth", knob, R (x + 96, 446, 84, 110), "Depth" });
                c.controls.push_back ({ "l" + n + "Sync", toggle, R (x + 186, 470, 76, 24), "=SYNC" });
                c.controls.push_back ({ "l" + n + "Div", drop, R (x + 186, 500, 76, 24), "-", ink });
            }
            c.graphs.push_back ({ R (806, 386, 150, 150), { { "xyX", "xyY", {}, {} } }, 0, true });
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                const auto on = [&] (const char* which) { return valueOf (fx, v, which) > 0.5f; };
                if (id.endsWith ("On") || id.startsWith ("slot") || id == "xyX" || id == "xyY") return true;
                if (id.startsWith ("bp")) return on ("bpOn");
                if (id == "fType" || id == "fCutoff" || id == "fRes" || id == "fDrive" || id == "fMix") return on ("fOn");
                if (id.startsWith ("dType") || id.startsWith ("dAmt")) return on ("dOn");
                if (id == "mType" || id == "mRate" || id == "mMix") return on ("mOn");
                if (id == "bType" || id == "bAmount" || id == "bTune") return on ("bOn");
                if (id == "cType" || id == "cAmount" || id == "cRelease") return on ("cOn");
                for (const char* lfo : { "l1", "l2" })
                    if (id.startsWith (lfo))
                    {
                        const auto n = juce::String (lfo);
                        if (! on ((n + "On").toRawUTF8())) return false;
                        const auto synced = on ((n + "Sync").toRawUTF8());
                        if (id.endsWith ("Rate")) return ! synced;
                        if (id.endsWith ("Div")) return synced;
                        return true;
                    }
                if (id.startsWith ("e")) return on ("eOn");
                if (id.startsWith ("x")) return on ("xOn");
                if (id.startsWith ("y")) return on ("yOn");
                return true;
            };
            d["phatFx"] = c;
        }
        {
            // Step FX
            const auto knob = Kind::knob, drop = Kind::dropdown, toggle = Kind::toggle, field = Kind::field;
            const auto ink = juce::Colour (0xffe9e9ea);
            Design c;
            c.width = 1200; c.height = 760;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = stepAccent; c.accent = stepAccent; c.track = juce::Colour (0xff121316);
            c.paint = paintStepFx;
            c.meters = true;
            c.pageParam = "view";
            c.controls = {
                // Filter
                { "fltOn", toggle, R (20, 20, 120, 24), "=FILTER" }, { "fltType", drop, R (146, 20, 118, 24), "-", ink },
                { "fltCutoff", knob, R (20, 60, 78, 100), "Cutoff" }, { "fltRes", knob, R (102, 60, 78, 100), "Res" }, { "fltMix", knob, R (184, 60, 78, 100), "Mix" },
                // Distortion
                { "distOn", toggle, R (288, 20, 134, 24), "=DISTORTION" },
                { "distExciter", knob, R (290, 60, 64, 100), "Exciter" }, { "distDirt", knob, R (358, 60, 64, 100), "Dirt" },
                // Mod FX
                { "modOn", toggle, R (446, 20, 284, 24), "=MOD FX" },
                { "modDelay", knob, R (446, 56, 70, 96), "Delay" }, { "modRate", knob, R (518, 56, 70, 96), "Rate" }, { "modDepth", knob, R (590, 56, 70, 96), "Depth" },
                { "modFeedback", knob, R (482, 140, 70, 96), "Feedback" }, { "modMix", knob, R (590, 140, 70, 96), "Mix" },
                // Delay
                { "dlyOn", toggle, R (754, 20, 120, 24), "=DELAY" }, { "dlyFilter", drop, R (880, 20, 136, 24), "-", ink },
                { "dlyFeedback", knob, R (754, 56, 70, 96), "Feedback" }, { "dlyCutoff", knob, R (826, 56, 70, 96), "Cutoff" }, { "dlyMix", knob, R (898, 56, 70, 96), "Mix" },
                { "dlySync", toggle, R (970, 60, 46, 22), "=SYNC" },
                { "dlyTimeL", knob, R (790, 140, 70, 96), "Time L" }, { "dlyTimeR", knob, R (868, 140, 70, 96), "Time R" },
                { "dlyDivL", drop, R (950, 160, 66, 22), "-", ink }, { "dlyDivR", drop, R (950, 190, 66, 22), "-", ink },
                // Reverb
                { "revOn", toggle, R (1040, 20, 140, 24), "=REVERB" }, { "revType", drop, R (1040, 50, 140, 24), "-", ink },
                { "revTime", knob, R (1040, 84, 70, 100), "Time" }, { "revMix", knob, R (1110, 84, 70, 100), "Mix" },
                // Which step modulator shows
                { "view", Kind::segmented, R (20, 266, 236, 24), "-" },
                // XY pad
                { "xOn", toggle, R (236, 540, 60, 22), "=X" }, { "xT1", drop, R (302, 540, 150, 22), "-", ink }, { "xD1", field, R (458, 540, 100, 22), "-", ink },
                { "xT2", drop, R (302, 568, 150, 22), "-", ink }, { "xD2", field, R (458, 568, 100, 22), "-", ink },
                { "yOn", toggle, R (236, 620, 60, 22), "=Y" }, { "yT1", drop, R (302, 620, 150, 22), "-", ink }, { "yD1", field, R (458, 620, 100, 22), "-", ink },
                { "yT2", drop, R (302, 648, 150, 22), "-", ink }, { "yD2", field, R (458, 648, 100, 22), "-", ink },
                // Master
                { "input", knob, R (620, 560, 110, 140), "Input" }, { "mix", knob, R (760, 560, 110, 140), "Mix" }, { "output", knob, R (900, 560, 110, 140), "Output" },
            };
            for (int k = 0; k < 3; ++k)
            {
                const auto n = juce::String (k + 1);
                const auto page = k + 1;
                const auto id = [&] (const char* what) { return "s" + n + what; };
                c.controls.push_back ({ id ("On"), toggle, R (264, 266, 86, 24), "=ON", {}, page });
                c.controls.push_back ({ id ("Target"), drop, R (360, 266, 170, 24), "-", ink, page });
                c.controls.push_back ({ id ("Depth"), field, R (540, 266, 90, 24), "-", ink, page });
                c.controls.push_back ({ id ("Rate"), drop, R (640, 266, 80, 24), "-", ink, page });
                c.controls.push_back ({ id ("Swing"), field, R (730, 266, 80, 24), "-", ink, page });
                c.controls.push_back ({ id ("Attack"), field, R (820, 266, 80, 24), "-", ink, page });
                c.controls.push_back ({ id ("Hold"), field, R (910, 266, 80, 24), "-", ink, page });
                c.controls.push_back ({ id ("Release"), field, R (1000, 266, 80, 24), "-", ink, page });
                c.controls.push_back ({ id ("Length"), drop, R (1090, 266, 90, 24), "-", ink, page });
                for (int st = 0; st < stepFxMaxSteps; ++st)
                {
                    const auto x = 30 + st * 35;
                    c.controls.push_back ({ "s" + n + "L" + juce::String (st + 1), Kind::fader, R (x, 302, 32, 166), "-", {}, page });
                    c.controls.push_back ({ "s" + n + "S" + juce::String (st + 1), drop, R (x, 474, 32, 22), "-", ink, page });
                }
            }
            c.graphs.push_back ({ R (22, 540, 196, 196), { { "xyX", "xyY", {}, {} } }, 0, true });
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                const auto on = [&] (const char* which) { return valueOf (fx, v, which) > 0.5f; };
                if (id.endsWith ("On") || id == "view" || id == "xyX" || id == "xyY") return true;
                const std::pair<const char*, const char*> units[] { { "flt", "fltOn" }, { "dist", "distOn" }, { "mod", "modOn" }, { "dly", "dlyOn" }, { "rev", "revOn" } };
                for (const auto& [prefix, onId] : units)
                    if (id.startsWith (prefix))
                    {
                        if (! on (onId)) return false;
                        if (id == "dlyTimeL" || id == "dlyTimeR") return ! on ("dlySync");
                        if (id == "dlyDivL" || id == "dlyDivR") return on ("dlySync");
                        return true;
                    }
                if (id.startsWith ("x")) return on ("xOn");
                if (id.startsWith ("y")) return on ("yOn");
                for (const char* k : { "s1", "s2", "s3" })
                    if (id.startsWith (k))
                    {
                        if (! on ((juce::String (k) + "On").toRawUTF8())) return false;
                        const auto rest = id.substring (2);
                        if ((rest.startsWith ("L") || rest.startsWith ("S")) && rest.substring (1).containsOnly ("0123456789"))
                            return rest.substring (1).getIntValue() <= juce::roundToInt (valueOf (fx, v, (juce::String (k) + "Length").toRawUTF8(), 15.0f)) + 1;
                        return true;
                    }
                return true;
            };
            d["stepFx"] = c;
        }
        {
            // Remix FX
            const auto drop = Kind::dropdown, toggle = Kind::toggle;
            const auto ink = juce::Colour (0xffe9e9ea);
            Design c;
            c.width = 1100; c.height = 580;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xffffc04a); c.accent = juce::Colour (0xffffc04a); c.track = juce::Colour (0xff121316);
            c.paint = paintRemixFx;
            c.controls = {
                { "padAEffect", drop, R (20, 20, 180, 28), "-", remixA }, { "padAOn", toggle, R (210, 20, 90, 28), "=ACTIVE" }, { "padALock", toggle, R (310, 20, 90, 28), "=LOCK" },
                { "padBEffect", drop, R (700, 20, 180, 28), "-", remixB }, { "padBOn", toggle, R (890, 20, 90, 28), "=ACTIVE" }, { "padBLock", toggle, R (990, 20, 90, 28), "=LOCK" },
                { "reverse", toggle, R (420, 20, 80, 28), "=REVERSE" }, { "scratch", toggle, R (510, 20, 80, 28), "=SCRATCH" }, { "tapeStop", toggle, R (600, 20, 80, 28), "=TAPE STOP" },
                { "reverseDiv", drop, R (420, 54, 80, 24), "-", ink }, { "scratchDiv", drop, R (510, 54, 80, 24), "-", ink }, { "tapeStopDiv", drop, R (600, 54, 80, 24), "-", ink },
                { "gater", Kind::fader, R (448, 116, 80, 300), "-" }, { "downsampler", Kind::fader, R (572, 116, 80, 300), "-" },
                { "gaterRate", drop, R (440, 424, 96, 24), "-", ink }, { "downType", drop, R (558, 424, 110, 24), "-", ink },
                { "filterType", Kind::segmented, R (40, 506, 280, 28), "-" },
                { "reverbColor", Kind::segmented, R (370, 506, 300, 28), "-" },
                { "orbitType", Kind::segmented, R (720, 506, 200, 28), "-" },
            };
            c.graphs.push_back ({ R (20, 62, 380, 380), { { "padAX", "padAY", {}, {} } }, 0, true });
            c.graphs.push_back ({ R (700, 62, 380, 380), { { "padBX", "padBY", {}, {} } }, 0, true });
            d["remixFx"] = c;
        }
        {
            // Beat Breaker
            const auto drop = Kind::dropdown, toggle = Kind::toggle, field = Kind::field;
            const auto ink = juce::Colour (0xffe9e9ea);
            Design c;
            c.width = 1100; c.height = 600;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xffff9a3c); c.accent = juce::Colour (0xffff9a3c); c.track = juce::Colour (0xff121316);
            c.paint = paintBeatBreaker;
            c.meters = true;
            c.pageParam = "selected";
            c.controls = {
                { "mode", Kind::segmented, R (20, 16, 540, 28), "-" },
                { "randomize", toggle, R (580, 16, 110, 28), "=RANDOMIZE" },
                { "randomAll", toggle, R (700, 16, 110, 28), "=RANDOM ALL" },
                { "length", drop, R (840, 16, 110, 28), "-", ink },
                { "selected", drop, R (20, 352, 90, 28), "-", ink },
                { "pattern", Kind::segmented, R (24, 424, 940, 30), "-" },
                { "patternOff", toggle, R (976, 424, 100, 30), "=OFF" },
                { "sliceCount", drop, R (24, 516, 120, 28), "-", ink },
                { "bypassOn", toggle, R (170, 516, 130, 28), "=BYPASS BELOW" },
                { "bypassFreq", field, R (306, 516, 100, 28), "-", ink },
                { "declick", Kind::knob, R (700, 488, 110, 100), "De-click" },
                { "mix", Kind::knob, R (860, 488, 110, 100), "Mix" },
            };
            for (int k = 0; k < bbSlices; ++k)
            {
                const auto n = juce::String (k + 1);
                const auto page = k + 1;
                const std::pair<const char*, int> fields[] { { "out", 120 }, { "in", 220 }, { "speed", 320 }, { "curve", 420 }, { "repeat", 520 },
                                                             { "cutoff", 620 }, { "reso", 720 }, { "volume", 820 }, { "slope", 920 }, { "pan", 1000 } };
                for (const auto& [what, x] : fields)
                    c.controls.push_back ({ what + n, juce::String (what) == "repeat" ? drop : field, R (x, 352, 90, 28), "-", ink, page });
            }
            // Patterns and randomize: written into the slices' parameters
            c.learn = [] (const float*, int, const EffectInfo& fx, const float* v, const std::function<void (const juce::String&, float)>& set)
            {
                const auto pattern = juce::roundToInt (valueOf (fx, v, "pattern"));
                if (pattern != juce::roundToInt (valueOf (fx, v, "patternLoaded")))
                {
                    set ("patternLoaded", (float) pattern);
                    beatBreakerPattern (pattern, fx, v, set);
                }
                const auto mode = juce::roundToInt (valueOf (fx, v, "mode"));
                if (valueOf (fx, v, "randomize") > 0.5f) { beatBreakerRandomize (mode, false, fx, v, set); set ("randomize", 0.0f); }
                if (valueOf (fx, v, "randomAll") > 0.5f) { beatBreakerRandomize (mode, true, fx, v, set); set ("randomAll", 0.0f); }
            };
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                if (id == "bypassFreq") return valueOf (fx, v, "bypassOn") > 0.5f;
                return true;
            };
            d["beatBreaker"] = c;
        }
        {
            // Room Simulator
            const auto knob = Kind::knob, drop = Kind::dropdown, toggle = Kind::toggle;
            const auto ink = juce::Colour (0xffe9e9ea);
            Design c;
            c.width = 1000; c.height = 560;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xff9fd0ff); c.accent = juce::Colour (0xff6fb7ff); c.track = juce::Colour (0xff11141a);
            c.paint = paintRoomSim;
            c.meters = true;
            c.pageParam = "panel";
            c.controls = {
                { "model", Kind::segmented, R (20, 16, 220, 28), "-" },
                { "freeze", toggle, R (560, 16, 100, 28), "=FREEZE" },
                { "add", toggle, R (668, 16, 80, 28), "=ADD" },
                { "clear", toggle, R (756, 16, 80, 28), "=CLEAR" },
                { "mode", drop, R (860, 16, 120, 28), "-", ink },
                { "enhance", toggle, R (250, 16, 110, 28), "=ENHANCE" },
                { "reverbTime", knob, R (60, 70, 180, 250), "Reverb Time" },
                { "panel", Kind::segmented, R (20, 346, 400, 28), "-" },
                { "lowMult", Kind::field, R (360, 288, 80, 24), "-", ink }, { "lowXover", Kind::field, R (446, 288, 90, 24), "-", ink },
                { "highMult", Kind::field, R (790, 288, 80, 24), "-", ink }, { "highXover", Kind::field, R (876, 288, 96, 24), "-", ink },
            };
            const char* pages[3][6] { { "roomSize", "density", "reverbDelay", "dryLevel", "refLevel", "reverbLevel" },
                                      { "refDelay", "refSpread", "highCut", "bassGain", "bassXover", "correlation" },
                                      { "drySource", "refSource", "corrL", "corrR", "subsonic", nullptr } };
            for (int pg = 0; pg < 3; ++pg)
                for (int k = 0; k < 6; ++k)
                    if (const auto* id = pages[pg][k])
                    {
                        const auto x = 20 + k * 162;
                        if (pg < 2)
                            c.controls.push_back ({ id, knob, R (x, 384, 140, 140), {}, {}, pg + 1 });
                        else if (juce::String (id) == "subsonic")
                            c.controls.push_back ({ id, toggle, R (x + 10, 430, 120, 28), "=SUBSONIC", {}, pg + 1 });
                        else
                            c.controls.push_back ({ id, drop, R (x, 430, 150, 28), "-", ink, pg + 1 });
                    }
            c.graphs.push_back ({ R (340, 70, 640, 250), { { "lowXover", "lowMult", {}, {} }, { "highXover", "highMult", {}, {} } } });
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                const auto modern = valueOf (fx, v, "model", 1.0f) > 0.5f;
                const auto frozen = valueOf (fx, v, "freeze") > 0.5f;
                if (id == "add") return frozen;
                if (id == "enhance") return ! modern;
                if (id == "reverbTime" || id == "lowMult" || id == "highMult") return modern || valueOf (fx, v, "enhance") < 0.5f;
                for (const char* only : { "mode", "density", "lowXover", "highXover", "refSpread", "highCut", "bassGain", "bassXover", "correlation", "corrL", "corrR" })
                    if (id == only) return modern;
                return true;
            };
            d["roomSim"] = c;
        }
        {
            // Match EQ
            Design c;
            c.width = 1000; c.height = 560;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xffffc46b); c.accent = juce::Colour (0xffffa23c); c.track = juce::Colour (0xff17191c);
            c.meters = true;
            c.analyzer = true;
            c.paint = paintMatchEq;
            c.controls = {
                { "learnT", Kind::toggle, R (40, 392, 120, 30), "=LEARN" },
                { "clearT", Kind::toggle, R (180, 392, 120, 30), "=CLEAR" },
                { "irFile", Kind::fileButton, R (40, 436, 260, 30), "-" },
                { "learnC", Kind::toggle, R (370, 392, 140, 30), "=LEARN" },
                { "clearC", Kind::toggle, R (370, 436, 140, 30), "=CLEAR" },
                { "apply", Kind::knob, R (570, 384, 110, 150), "Apply" },
                { "smoothing", Kind::knob, R (690, 384, 110, 150), "Smoothing" },
                { "phase", Kind::segmented, R (830, 392, 150, 30), "-" },
                { "analyzer", Kind::toggle, R (830, 436, 150, 30), "=ANALYZER" },
                { "output", Kind::field, R (830, 490, 150, 30), "-", juce::Colours::white },
            };
            c.graphs.push_back ({ R (20, 50, 960, 290), { { "u1Freq", "u1Gain", "u1Q", "u1On" }, { "u2Freq", "u2Gain", "u2Q", "u2On" },
                                                          { "u3Freq", "u3Gain", "u3Q", "u3On" }, { "u4Freq", "u4Gain", "u4Q", "u4On" } } });
            // What it learns (and a reference file's spectrum) goes into its parameters, so it is kept with the project
            c.learn = [] (const float* m, int count, const EffectInfo& fx, const float* v, const std::function<void (const juce::String&, float)>& set)
            {
                if (count < 96)
                    return;
                static int tick = 0;
                ++tick;
                const auto band = [] (const char* prefix, int k) { return juce::String (prefix) + juce::String (k + 1); };
                const auto write = [&] (const char* prefix, const float* from, bool force)
                {
                    for (int k = 0; k < matchBands; ++k)
                    {
                        const auto id = band (prefix, k);
                        const auto now = juce::jlimit (-120.0f, 20.0f, std::round (from[k] * 100.0f) / 100.0f);
                        if (force || std::abs (valueOf (fx, v, id.toRawUTF8(), -120.0f) - now) > 0.05f)
                            set (id, now);
                    }
                };
                std::array<float, matchBands> cleared;
                cleared.fill (-120.0f);
                for (const auto& [flag, prefix] : { std::pair { "clearT", "t" }, std::pair { "clearC", "c" } })
                    if (valueOf (fx, v, flag) > 0.5f)
                    {
                        write (prefix, cleared.data(), true);
                        set (flag, 0.0f);
                        if (juce::String (prefix) == "t") set ("learnT", 0.0f); else set ("learnC", 0.0f);
                    }
                const auto generation = m[64];
                if (generation > 0.5f && std::abs (generation - valueOf (fx, v, "templateGen")) > 0.5f)
                {
                    write ("t", m + 65, false);
                    set ("templateGen", generation);
                }
                if (tick % 8 != 0)
                    return;
                if (valueOf (fx, v, "learnT") > 0.5f && m[62] > 0.25f) write ("t", m, false);
                if (valueOf (fx, v, "learnC") > 0.5f && m[63] > 0.25f) write ("c", m + 31, false);
            };
            d["matchEq"] = c;
        }
        {
            // Delay Designer
            Design c;
            c.width = 1210; c.height = 600;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xff7ee08c); c.accent = juce::Colour (0xff7ee08c); c.track = juce::Colour (0xff17191c);
            c.meters = true;
            c.paint = paintDelayDesigner;
            c.pageParam = "tapSel";
            const auto ink = juce::Colour (0xffe8eaec);
            c.controls = {
                { "view", Kind::segmented, R (20, 14, 460, 28), "-" },
                { "tapSel", Kind::dropdown, R (20, 368, 70, 30), "-", ink },
                { "tapCount", Kind::knob, R (30, 450, 100, 140), "Taps" },
                { "sync", Kind::toggle, R (170, 486, 80, 30), "=SYNC" },
                { "grid", Kind::dropdown, R (265, 486, 90, 30), "-", ink },
                { "swing", Kind::knob, R (370, 450, 100, 140), "Swing" },
                { "fbOn", Kind::toggle, R (540, 486, 90, 30), "=ON" },
                { "fbTap", Kind::dropdown, R (645, 486, 70, 30), "-", ink },
                { "feedback", Kind::knob, R (730, 450, 100, 140), "Level" },
                { "dry", Kind::knob, R (900, 450, 100, 140), "Dry" },
                { "mix", Kind::knob, R (1040, 450, 100, 140), "Wet" },
            };
            for (int t = 0; t < 26; ++t)
            {
                const auto n = juce::String (t + 1);
                const auto page = t + 1;
                c.controls.push_back ({ "time" + n, Kind::field, R (105, 368, 110, 30), "-", ink, page });
                c.controls.push_back ({ "transp" + n, Kind::field, R (230, 368, 70, 30), "-", ink, page });
                c.controls.push_back ({ "cents" + n, Kind::field, R (310, 368, 70, 30), "-", ink, page });
                c.controls.push_back ({ "filter" + n, Kind::toggle, R (395, 368, 56, 30), "=ON", {}, page });
                c.controls.push_back ({ "hp" + n, Kind::field, R (465, 368, 100, 30), "-", ink, page });
                c.controls.push_back ({ "lp" + n, Kind::field, R (575, 368, 100, 30), "-", ink, page });
                c.controls.push_back ({ "slope" + n, Kind::segmented, R (685, 368, 110, 30), "-", {}, page });
                c.controls.push_back ({ "reso" + n, Kind::field, R (805, 368, 80, 30), "-", ink, page });
                c.controls.push_back ({ "pan" + n, Kind::field, R (895, 368, 80, 30), "-", ink, page });
                c.controls.push_back ({ "level" + n, Kind::field, R (985, 368, 90, 30), "-", ink, page });
                c.controls.push_back ({ "flip" + n, Kind::toggle, R (1085, 368, 50, 30), "=FLIP", {}, page });
                c.controls.push_back ({ "mute" + n, Kind::toggle, R (1140, 368, 52, 30), "=MUTE", {}, page });
            }
            c.enabled = [] (const juce::String& id, const EffectInfo& fx, const float* v)
            {
                if (id == "grid" || id == "swing") return valueOf (fx, v, "sync") > 0.5f;
                if (id == "fbTap" || id == "feedback") return valueOf (fx, v, "fbOn", 1.0f) > 0.5f;
                for (const char* prefix : { "hp", "lp", "slope", "reso" })
                    if (id.startsWith (prefix) && id.substring ((int) std::strlen (prefix)).containsOnly ("0123456789"))
                        return valueOf (fx, v, ("filter" + id.substring ((int) std::strlen (prefix))).toRawUTF8()) > 0.5f;
                return true;
            };
            d["delayDesigner"] = c;
        }
        addHardwareDesigns (d);
        addPhotoDesigns (d);
        addClassicDesigns (d);
        addCompressorFaces (d);
        addGlassCompressorFaces (d);

        // The vintage EQs: under their faces, a screen with the curve they make and the live spectrum behind it
        // (not under the narrow graphic EQ)
        for (const char* type : { "consoleEq", "tubeEq" })
        {
            auto& c = d[type];
            const auto faceHeight = c.height;
            constexpr int strip = 190;
            c.height += strip;
            c.analyzer = true;
            c.paint = [old = c.paint, faceHeight] (juce::Graphics& g, R bounds, const EffectInfo& fx, const float* v, float live)
            {
                {
                    juce::Graphics::ScopedSaveState keep (g);
                    g.reduceClipRegion (bounds.withHeight (faceHeight));
                    old (g, bounds.withHeight (faceHeight), fx, v, live);
                }
                const auto panel = R (0, faceHeight, bounds.getWidth(), bounds.getHeight() - faceHeight);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2128), 0.0f, (float) panel.getY(), juce::Colour (0xff121418), 0.0f, (float) panel.getBottom(), false));
                g.fillRect (panel);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRect (panel.withHeight (2));
                const auto screen = panel.reduced (20, 16).toFloat();
                g.setColour (juce::Colour (0xff0b0d10));
                g.fillRoundedRectangle (screen, 5.0f);
                const auto inner = screen.reduced (8.0f, 6.0f);
                const auto xOf = [inner] (double f) { return inner.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * inner.getWidth(); };
                const auto yOf = [inner] (double db) { return inner.getCentreY() - (float) juce::jlimit (-1.0, 1.0, db / 24.0) * inner.getHeight() * 0.5f; };
                g.setFont (juce::FontOptions (10.5f));
                for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
                {
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.fillRect (xOf (f), inner.getY(), 1.0f, inner.getHeight());
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) + 3.0f, inner.getBottom() - 13.0f, 34.0f, 12.0f), juce::Justification::left, false);
                }
                for (const auto db : { -12.0, 12.0 })
                {
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.fillRect (inner.getX(), yOf (db), inner.getWidth(), 1.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), RF (inner.getX() + 2.0f, yOf (db) - 12.0f, 30.0f, 12.0f), juce::Justification::left, false);
                }
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (inner.getX(), yOf (0.0), inner.getWidth(), 1.0f);
                drawSpectrum (g, inner, xOf, juce::Colour (0xff7fb4e6));
                if (fx.response != nullptr)
                {
                    juce::Path curve;
                    for (int x = 0; x <= (int) inner.getWidth(); x += 2)
                    {
                        const auto f = 20.0 * std::pow (1000.0, x / (double) inner.getWidth());
                        const auto y = yOf (fx.response (v, f, 48000.0));
                        if (x == 0) curve.startNewSubPath (inner.getX(), y); else curve.lineTo (inner.getX() + (float) x, y);
                    }
                    g.setColour (juce::Colour (0xffffc35a).withAlpha (0.25f));
                    g.strokePath (curve, juce::PathStrokeType (5.0f));
                    g.setColour (juce::Colour (0xffffc35a));
                    g.strokePath (curve, juce::PathStrokeType (2.0f));
                }
            };
        }
        return d;
    }
}

namespace
{
    const Spectrum* currentSpectrum = nullptr;
    const float* currentMeters = nullptr;
    int currentMeterCount = 0;
}
const float* paintingMeters (int& count) noexcept           { count = currentMeterCount; return currentMeters; }
void setPaintingMeters (const float* values, int count) noexcept { currentMeters = values; currentMeterCount = count; }
const Spectrum* paintingSpectrum() noexcept           { return currentSpectrum; }
void setPaintingSpectrum (const Spectrum* s) noexcept { currentSpectrum = s; }

void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> screen, const std::function<float (double)>& xOf, juce::Colour colour)
{
    const auto* sp = currentSpectrum;
    if (sp == nullptr || ! sp->valid)
        return;
    const auto yOf = [screen] (float db) { return screen.getY() + juce::jlimit (0.0f, 1.0f, -db / 84.0f) * screen.getHeight(); };
    juce::Path line, fill;
    for (int k = 0; k < Spectrum::points; ++k)
    {
        const auto x = xOf (Spectrum::frequencyOf (k)), y = yOf (sp->db[(size_t) k]);
        if (k == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, screen.getBottom()); }
        else line.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (xOf (Spectrum::frequencyOf (Spectrum::points - 1)), screen.getBottom());
    fill.closeSubPath();
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (screen.toNearestInt());
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.32f), 0.0f, screen.getY(), colour.withAlpha (0.06f), 0.0f, screen.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.75f));
    g.strokePath (line, juce::PathStrokeType (1.2f));
}

juce::Image skinImage (const char* name)
{
    int size = 0;
    if (const auto* data = BinaryData::getNamedResource (name, size))
        return juce::ImageCache::getFromMemory (data, size);
    jassertfalse;   // (not in Resources/effects?)
    return {};
}

bool writesInTheme (const Design& d)
{
    // (in light mode a face still in its own dark colours keeps its light writing: dark writing would vanish on it)
    return theme::glassSkin() && d.photoScale <= 0.0f
        && (d.glassFace || ! theme::glassLight() || d.label.getPerceivedBrightness() < 0.55f);
}
juce::Colour labelColourOf (const Design& d) { return writesInTheme (d) ? theme::text : d.label; }
juce::Colour valueColourOf (const Design& d) { return writesInTheme (d) ? theme::accent : d.value; }

const juce::StringArray& classicSkinNames()
{
    static const juce::StringArray names { "Auto", "Half & Half", "Glass Dark", "Glass Silver", "Metal Grey", "Metal Black" };
    return names;
}

void setPaintingSkin (int skin) noexcept { paintingSkinValue = juce::jlimit (0, 5, skin); }

const Design* designFor (const juce::String& type)
{
    // Hand-made faces where they exist, the standard Logic look for every other stock effect
    static const auto designs = []
    {
        auto all = makeDesigns();
        for (const auto& info : builtInEffects())
            if (all.find (info.type) == all.end())
                all[info.type] = makeModern (info);
        return all;
    }();
    // (Liquid Glass: the Studio Compressor's glass faces)
    const auto it = designs.find (type == "studioComp" && theme::glassSkin() ? juce::String ("studioCompGlass") : type);
    return it != designs.end() ? &it->second : nullptr;
}

//==============================================================================
DesignLook::DesignLook (const Design& d) : design (d)
{
    setColour (juce::Label::textColourId, labelColourOf (design));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff20252c));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, design.accent.withAlpha (0.6f));
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee101418));
    setColour (juce::BubbleComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
}

juce::Slider::SliderLayout DesignLook::getSliderLayout (juce::Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);
    if (s.getProperties()["field"])
    {
        // A value field: the number fills it, and dragging anywhere on it changes it
        layout.textBoxBounds = s.getLocalBounds();
        layout.sliderBounds = s.getLocalBounds();
    }
    return layout;
}

juce::Label* DesignLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    const bool field = s.getProperties()["field"];
    l->setFont (juce::FontOptions (field ? 16.0f : 17.0f));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    const auto colour = s.getProperties().contains ("colour") ? juce::Colour::fromString (s.getProperties()["colour"].toString()) : valueColourOf (design);
    l->setColour (juce::Label::textColourId, colour);
    l->setColour (juce::TextEditor::textColourId, juce::Colours::white);
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101418));
    if (field)
        l->setInterceptsMouseClicks (false, false);   // (drags go to the field)
    if (design.lcdValues && ! field)
    {
        // A small dark window with a green readout
        l->setFont (juce::FontOptions (15.0f, juce::Font::bold));
        l->setColour (juce::Label::backgroundColourId, juce::Colour (0xff0e1210));
        l->setColour (juce::Label::outlineColourId, juce::Colours::black);
        l->setColour (juce::Label::textColourId, valueColourOf (design));
    }
    return l;
}

namespace
{
    /** A sprite's angle at a position: its settings' own angles, or smoothly between evenly spaced ones. */
    float angleAt (const std::vector<float>& angles, float pos, int settings)
    {
        const auto n = (int) angles.size();
        if (n == 1)
            return angles[0];
        if (settings > 1 && n == settings)
            return angles[(size_t) juce::jlimit (0, n - 1, juce::roundToInt (pos * (float) (n - 1)))];
        const auto x = juce::jlimit (0.0f, 1.0f, pos) * (float) (n - 1);
        const auto i = std::min ((int) x, n - 2);
        return angles[(size_t) i] + (x - (float) i) * (angles[(size_t) i + 1] - angles[(size_t) i]);
    }

    int settingsOf (const juce::Slider& s)
    {
        return s.textFromValueFunction != nullptr && s.getInterval() >= 1.0 ? juce::roundToInt (s.getMaximum() - s.getMinimum()) + 1 : 0;
    }
}

float photoValueAt (const Sprite& sp, float angle)
{
    const auto& m = sp.map;
    if (m.empty())
        return 0.0f;
    if (sp.stepped)
    {
        size_t best = 0;
        for (size_t i = 1; i < m.size(); ++i)
            if (std::abs (m[i].first - angle) < std::abs (m[best].first - angle))
                best = i;
        return m[best].second;
    }
    if (angle <= m.front().first) return m.front().second;
    for (size_t i = 1; i < m.size(); ++i)
        if (angle <= m[i].first)
        {
            const auto [a0, v0] = m[i - 1];
            const auto [a1, v1] = m[i];
            const auto t = (angle - a0) / std::max (1.0e-4f, a1 - a0);
            return sp.logMap && v0 > 0.0f && v1 > 0.0f ? v0 * std::pow (v1 / v0, t) : v0 + t * (v1 - v0);
        }
    return m.back().second;
}

float photoAngleOf (const Sprite& sp, float value)
{
    const auto& m = sp.map;
    if (m.empty())
        return 0.0f;
    if (sp.stepped)
    {
        size_t best = 0;
        for (size_t i = 1; i < m.size(); ++i)
            if (std::abs (m[i].second - value) < std::abs (m[best].second - value))
                best = i;
        return m[best].first;
    }
    for (size_t i = 1; i < m.size(); ++i)
    {
        const auto [a0, v0] = m[i - 1];
        const auto [a1, v1] = m[i];
        if ((v0 <= v1 && value >= v0 && value <= v1) || (v0 > v1 && value <= v0 && value >= v1))
        {
            const auto t = sp.logMap && v0 > 0.0f && v1 > 0.0f && value > 0.0f ? std::log (value / v0) / std::log (v1 / v0)
                                                                               : (value - v0) / (std::abs (v1 - v0) < 1.0e-9f ? 1.0f : v1 - v0);
            return a0 + (float) t * (a1 - a0);
        }
    }
    const bool rising = m.back().second >= m.front().second;
    return (rising ? value < m.front().second : value > m.front().second) ? m.front().first : m.back().first;
}

void DesignLook::drawSprite (juce::Graphics& g, juce::Component& c, const Sprite& sp, float pos, bool on, int settings)
{
    if (sp.drawn != Sprite::Drawn::none)
    {
        const auto absolute = c.getProperties().contains ("photoAngle") ? (float) c.getProperties()["photoAngle"]
                            : sp.angles.empty() ? 0.0f : angleAt (sp.angles, pos, settings);
        const auto scale = sp.scale > 0.0f ? sp.scale : design.photoScale;
        const auto offset = sp.scale > 0.0f ? sp.offset : design.photoOffset.toFloat();
        const auto centre = sp.pivot * scale + offset - c.getPosition().toFloat();
        const auto r = sp.radius * scale;
        const auto a = juce::degreesToRadians (absolute);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        juce::Graphics::ScopedSaveState keep (g);
        if (sp.drawn == Sprite::Drawn::line)
        {
            const auto p0 = centre + dir * (r * sp.inner), p1 = centre + dir * (r * sp.outer);
            g.setColour (sp.ink);
            g.drawLine ({ p0, p1 }, std::max (1.2f, r * sp.thickness));
        }
        else if (sp.drawn == Sprite::Drawn::dot)
        {
            const auto p = centre + dir * (r * sp.outer);
            const auto d = std::max (2.5f, r * sp.thickness);
            g.setColour (sp.ink);
            g.drawEllipse (RF (d, d).withCentre (p), std::max (1.0f, d * 0.28f));
        }
        else
        {
            // A bar knob: a machined bar across the knob, the light always from above (it doesn't turn with it)
            const auto len = r * 1.92f, wid = r * sp.thickness;
            juce::Path bar;
            bar.addRoundedRectangle (-len * 0.5f, -wid * 0.5f, len, wid, wid * 0.18f);
            const auto t = juce::AffineTransform::rotation (a + juce::MathConstants<float>::halfPi).translated (centre);
            // shadow
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (bar, t.translated (r * 0.05f, r * 0.12f));
            // the bar: lighter on its upper long face, darker on the lower, as lit from above
            const juce::Point<float> across (-dir.y, dir.x);   // (the bar's width direction... its long axis is `dir`)
            const auto upFace = juce::Point<float> (dir.y, -dir.x).y < 0.0f ? juce::Point<float> (dir.y, -dir.x) : juce::Point<float> (-dir.y, dir.x);
            juce::ignoreUnused (across);
            const auto top = centre + upFace * (wid * 0.5f), bottom = centre - upFace * (wid * 0.5f);
            juce::ColourGradient grad (juce::Colour (0xfff4f5f6), top.x, top.y, juce::Colour (0xff8e9196), bottom.x, bottom.y, false);
            grad.addColour (0.45, juce::Colour (0xffd8dadd));
            grad.addColour (0.55, juce::Colour (0xffb9bcc0));
            g.setGradientFill (grad);
            g.fillPath (bar, t);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.strokePath (bar, juce::PathStrokeType (std::max (0.8f, r * 0.03f)), t);
            // the end faces: a little darker
            for (const auto sgn : { -1.0f, 1.0f })
            {
                const auto e = centre + dir * (sgn * len * 0.5f);
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.fillEllipse (RF (wid * 0.5f, wid * 0.5f).withCentre (e - dir * (sgn * wid * 0.25f)));
            }
        }
        return;
    }
    if (sp.image == nullptr)
        return;
    const auto image = skinImage (sp.offImage != nullptr && ! on ? sp.offImage : sp.image);
    if (! image.isValid())
        return;
    float degrees = 0.0f, dx = 0.0f;
    if (sp.offImage != nullptr)
        degrees = 0.0f;
    else if (c.getProperties().contains ("photoAngle"))
        degrees = (float) c.getProperties()["photoAngle"] - sp.angle;
    else if (sp.slides)
        dx = sp.from + juce::jlimit (0.0f, 1.0f, pos) * (sp.to - sp.from) - sp.pivot.x;
    else if (sp.angles.empty())
        degrees = (on ? sp.onAngle : sp.offAngle) - sp.angle;
    else
        degrees = angleAt (sp.angles, pos, settings) - sp.angle;
    const auto origin = c.getPosition().toFloat();
    const auto scale = sp.scale > 0.0f ? sp.scale : design.photoScale;
    const auto offset = sp.scale > 0.0f ? sp.offset : design.photoOffset.toFloat();
    const auto t = juce::AffineTransform::scale (sp.rect.getWidth() / (float) image.getWidth(), sp.rect.getHeight() / (float) image.getHeight())
                       .translated (sp.rect.getX() + dx, sp.rect.getY())
                       .rotated (juce::degreesToRadians (degrees), sp.pivot.x + dx, sp.pivot.y)
                       .scaled (scale)
                       .translated (offset.x - origin.x, offset.y - origin.y);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (image, t);
}

void DesignLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    if (s.getProperties()["field"])
        return;   // (a value field: just its number)
    if (s.getProperties().contains ("sprite"))
    {
        drawSprite (g, s, design.controls[(size_t) (int) s.getProperties()["sprite"]].sprite, pos, false, settingsOf (s));
        return;
    }
    const auto size = (float) std::min (w, h);
    const auto bounds = RF (size, size).withCentre (R (x, y, w, h).toFloat().getCentre());
    const auto centre = bounds.getCentre();
    const auto angle = start + pos * (end - start);

    if (theme::glassSkin() && design.photoScale <= 0.0f)
    {
        // Liquid Glass: the LED-ring knob, its ring in the control's own colour (a band's), else the accent
        auto ring = s.getProperties().contains ("cap") ? juce::Colour::fromString (s.getProperties()["cap"].toString()) : juce::Colour();
        if (ring.isTransparent() || ring.getSaturation() < 0.3f)
            ring = s.findColour (juce::Slider::rotarySliderFillColourId);
        if (ring.isTransparent() || ring.getSaturation() < 0.3f)
            ring = theme::accent;
        const bool fromCentre = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        look::drawGlassKnob (g, bounds.reduced (size * 0.06f), pos, start, end, ring, fromCentre, s.isEnabled());
        return;
    }

    const auto capColour = s.getProperties().contains ("cap") ? juce::Colour::fromString (s.getProperties()["cap"].toString()) : juce::Colour();
    if (design.knobs == KnobStyle::black || design.knobs == KnobStyle::white || design.knobs == KnobStyle::chrome)
    {
        // Hardware knobs: ticks round them, the settings' names for a switch
        const bool choices = s.textFromValueFunction != nullptr && s.getInterval() >= 1.0;
        // (a switch keeps room round it for its settings' names)
        const auto radius = choices && ! design.lcdValues ? std::min (size * 0.5f - 24.0f, (float) w * 0.5f - 46.0f)
                                                          : size * 0.5f - (design.lcdValues ? 8.0f : 16.0f);
        const auto steps = choices ? juce::roundToInt (s.getMaximum() - s.getMinimum()) : 10;
        const auto tickColour = design.knobs == KnobStyle::white ? juce::Colour (0xff3a2210) : design.label;
        g.setColour (tickColour.withAlpha (0.85f));
        for (int t = 0; t <= steps; ++t)
        {
            const auto a = start + (end - start) * (float) t / (float) std::max (1, steps);
            g.drawLine ({ centre.getPointOnCircumference (radius + 3.0f, a), centre.getPointOnCircumference (radius + 7.0f, a) }, 1.4f);
        }
        if (choices && ! design.lcdValues)
        {
            g.setFont (juce::FontOptions (11.5f));
            for (int t = 0; t <= steps; ++t)
            {
                const auto a = start + (end - start) * (float) t / (float) std::max (1, steps);
                const auto at = centre.getPointOnCircumference (radius + 17.0f, a);
                g.drawText (s.getTextFromValue (s.getMinimum() + t), RF (at.x - 26.0f, at.y - 7.0f, 52.0f, 14.0f), juce::Justification::centred, false);
            }
        }
        const auto knob = RF (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (knob.translated (1.5f, 3.5f));
        if (design.knobs == KnobStyle::white)
        {
            // A glossy white knob with a dark dot (as on old pedals)
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x - radius * 0.4f, knob.getY(), juce::Colour (0xffcfd2d4), centre.x + radius * 0.3f, knob.getBottom(), true));
            g.fillEllipse (knob);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawEllipse (knob, 1.0f);
            g.setColour (juce::Colour (0xff4a4f55));
            const auto dot = centre.getPointOnCircumference (radius * 0.68f, angle);
            g.fillEllipse (dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f);
            return;
        }
        if (design.knobs == KnobStyle::chrome)
        {
            juce::ColourGradient chrome (juce::Colour (0xfff7f8f9), knob.getX(), knob.getY(), juce::Colour (0xff6b7177), knob.getRight(), knob.getBottom(), false);
            chrome.addColour (0.5, juce::Colour (0xffb9bec3));
            g.setGradientFill (chrome);
            g.fillEllipse (knob);
            const auto inner = knob.reduced (radius * 0.28f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9aa0a6), inner.getX(), inner.getY(), juce::Colour (0xfff0f1f2), inner.getRight(), inner.getBottom(), false));
            g.fillEllipse (inner);
            g.setColour (juce::Colour (0xff222222));
            g.drawLine ({ centre.getPointOnCircumference (radius * 0.75f, angle), centre.getPointOnCircumference (radius * 0.98f, angle) }, 3.0f);
            return;
        }
        // Black, knurled round the edge; a coloured cap if it has one (the console's blue and red)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3d40), centre.x, knob.getY(), juce::Colour (0xff0d0e10), centre.x, knob.getBottom(), false));
        g.fillEllipse (knob);
        for (int k = 0; k < 48; ++k)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) k / 48.0f;
            g.setColour (juce::Colours::white.withAlpha (k % 2 == 0 ? 0.07f : 0.0f));
            g.drawLine ({ centre.getPointOnCircumference (radius * 0.86f, a), centre.getPointOnCircumference (radius * 0.99f, a) }, 1.2f);
        }
        if (! capColour.isTransparent())
        {
            const auto cap = knob.reduced (radius * 0.2f);
            g.setGradientFill (juce::ColourGradient (capColour.brighter (0.5f), cap.getX(), cap.getY(), capColour.darker (0.5f), cap.getRight(), cap.getBottom(), false));
            g.fillEllipse (cap);
            const auto centreCap = knob.reduced (radius * 0.55f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8dadc), centreCap.getX(), centreCap.getY(), juce::Colour (0xff7b7f84), centreCap.getRight(), centreCap.getBottom(), false));
            g.fillEllipse (centreCap);
        }
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.3f, angle), centre.getPointOnCircumference (radius * 0.95f, angle) }, 2.6f);
        return;
    }

    if (design.knobs == KnobStyle::silver)
    {
        // A machined silver knob with its scale printed round it
        const auto radius = size * 0.5f - 16.0f;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (int t = 0; t <= 10; ++t)
        {
            const auto a = start + (end - start) * (float) t / 10.0f;
            g.drawLine ({ centre.getPointOnCircumference (radius + 5.0f, a), centre.getPointOnCircumference (radius + 10.0f, a) }, 1.4f);
        }
        g.setFont (juce::FontOptions (11.0f));
        const auto compact = [&s] (double value)
        {
            if (s.textFromValueFunction != nullptr && s.getInterval() >= 1.0)
                return s.getTextFromValue (value);   // (a switch: its setting's name)
            const auto a = std::abs (value);
            return a >= 1000.0 ? juce::String (value / 1000.0, a >= 10000.0 ? 0 : 1) + "k"
                 : juce::String (value, a >= 10.0 || a == 0.0 ? 0 : 1);
        };
        const bool choices = s.textFromValueFunction != nullptr && s.getInterval() >= 1.0;
        const auto steps = choices ? juce::roundToInt (s.getMaximum() - s.getMinimum()) : 2;
        for (int k = 0; k <= steps; ++k)
        {
            const auto t = (float) k / (float) std::max (1, steps);
            const auto a = start + (end - start) * t;
            const auto at = centre.getPointOnCircumference (radius + 21.0f, a);
            g.drawText (compact (choices ? s.getMinimum() + k : s.proportionOfLengthToValue (t)), RF (at.x - 24.0f, at.y - 7.0f, 48.0f, 14.0f),
                        juce::Justification::centred, false);
        }
        const auto knob = RF (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knob.translated (2.0f, 4.0f).expanded (1.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6f7378), knob.getX(), knob.getY(), juce::Colour (0xff2a2d31), knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        const auto cap = knob.reduced (radius * 0.12f);
        juce::ColourGradient metal (juce::Colour (0xffe4e6e8), cap.getCentreX() - cap.getWidth() * 0.25f, cap.getY(),
                                    juce::Colour (0xff6d7176), cap.getCentreX() + cap.getWidth() * 0.25f, cap.getBottom(), false);
        metal.addColour (0.45, juce::Colour (0xffaeb2b6));
        g.setGradientFill (metal);
        g.fillEllipse (cap);
        // (brushed rings on the cap)
        for (float r = cap.getWidth() * 0.12f; r < cap.getWidth() * 0.5f; r += 2.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawEllipse (RF (r * 2.0f, r * 2.0f).withCentre (centre), 0.7f);
        }
        g.setColour (juce::Colour (0xff1b1c1e));
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.35f, angle), centre.getPointOnCircumference (radius * 0.88f, angle) }, 2.5f);
        return;
    }

    const auto radius = size * 0.5f - 6.0f;
    const auto light = design.knobs == KnobStyle::light;
    // The track and the value arc
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (design.track);
    g.strokePath (track, juce::PathStrokeType (light ? 4.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const auto from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (design.accent);
    g.strokePath (value, juce::PathStrokeType (light ? 6.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    // The body
    const auto body = RF ((radius - 6.0f) * 2.0f, (radius - 6.0f) * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (light ? 0.12f : 0.4f));
    g.fillEllipse (body.translated (0.0f, 2.0f));
    if (light)
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(), juce::Colour (0xffe9e9e9), centre.x, body.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4656), centre.x, body.getY(), juce::Colour (0xff27313d), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (light ? juce::Colours::black.withAlpha (0.1f) : juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (body, 1.2f);
    g.setColour (design.accent);
    g.drawLine ({ centre, centre.getPointOnCircumference (radius - 7.0f, angle) }, 2.0f);

    if (design.showScale && ! (s.textFromValueFunction != nullptr && s.getInterval() >= 1.0))
    {
        // The lowest and highest values, under the knob's two ends
        const auto compact = [] (double value)
        {
            const auto a = std::abs (value);
            return a >= 1000.0 ? juce::String (value / 1000.0, a >= 10000.0 ? 0 : 1) + "k" : juce::String (value, a >= 10.0 || a == 0.0 ? 0 : 2);
        };
        g.setColour (design.label.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f));
        const auto lo = centre.getPointOnCircumference (radius + 4.0f, start), hi = centre.getPointOnCircumference (radius + 4.0f, end);
        g.drawText (compact (s.getMinimum()), RF (lo.x - 26.0f, lo.y + 4.0f, 40.0f, 14.0f), juce::Justification::centred, false);
        g.drawText (compact (s.getMaximum()), RF (hi.x - 14.0f, hi.y + 4.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
}

void DesignLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (s.getProperties().contains ("sprite"))
    {
        drawSprite (g, s, design.controls[(size_t) (int) s.getProperties()["sprite"]].sprite,
                    (float) s.valueToProportionOfLength (s.getValue()), false, settingsOf (s));
        return;
    }
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    const auto cx = (float) x + (float) w * 0.5f;
    if (theme::glassSkin())
    {
        // Liquid Glass: a recessed glass slot, lit below the cap in the accent, and the Dark VU cap
        const auto slot = RF (cx - 4.0f, (float) y - 2.0f, 8.0f, (float) h + 4.0f);
        look::drawGlassPanel (g, slot, 4.0f, true);
        const auto lit = slot.withTop (juce::jlimit (slot.getY(), slot.getBottom(), pos)).reduced (1.5f, 1.0f);
        g.setColour (theme::accent.withAlpha (s.isEnabled() ? 0.85f : 0.3f));
        g.fillRoundedRectangle (lit, 2.5f);
        const auto capHeight = juce::jlimit (26.0f, 46.0f, (float) h * 0.2f);
        const auto cap = RF (cx - 20.0f, pos - capHeight * 0.5f, 40.0f, capHeight);
        if (! looks::drawFaderCap (g, cap, true, s.isEnabled() ? 1.0f : 0.5f))
        {
            look::drawGlassPanel (g, cap.withSizeKeepingCentre (30.0f, capHeight * 0.6f), 5.0f);
        }
        return;
    }
    if (design.knobs == KnobStyle::black || design.knobs == KnobStyle::chrome)
    {
        // A hardware fader: a dark rounded slot and a rounded cap (its own colour, or grey)
        const auto slot = RF (cx - 9.0f, (float) y - 6.0f, 18.0f, (float) h + 12.0f);
        g.setColour (juce::Colour (0xff0b0c0d));
        g.fillRoundedRectangle (slot, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (slot, 9.0f, 1.0f);
        const auto colour = s.getProperties().contains ("cap") ? juce::Colour::fromString (s.getProperties()["cap"].toString()) : juce::Colour (0xffc9ccd0);
        const auto cap = RF (22.0f, 34.0f).withCentre ({ cx, pos });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.3f), cap.getX(), cap.getY(), colour.darker (0.35f), cap.getRight(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (cap.getX() + 3.0f, pos - 0.5f, cap.getWidth() - 6.0f, 1.0f);
        return;
    }
    // A fader: a thin slot, the level in the accent colour, a small flat cap
    g.setColour (design.knobs == KnobStyle::light ? juce::Colour (0xff8e8e8e) : juce::Colours::black.withAlpha (0.6f));
    g.fillRect (cx - 1.5f, (float) y, 3.0f, (float) h);
    g.setColour (design.accent);
    g.fillRect (cx - 2.5f, pos, 5.0f, (float) (y + h) - pos);
    const auto cap = RF (28.0f, 10.0f).withCentre ({ cx, pos });
    g.setColour (design.knobs == KnobStyle::light ? juce::Colour (0xfff4f4f4) : juce::Colour (0xff5a6878));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (cap, 2.0f, 1.0f);
    g.setColour (design.accent);
    g.fillRect (cap.getX() + 4.0f, cx * 0.0f + pos - 0.5f, cap.getWidth() - 8.0f, 1.0f);
}

juce::Colour DesignLook::hardwareLamp() const
{
    // (amber on wooden and tweed faces, else the face's accent if bright enough to read as lit)
    return design.knobs == KnobStyle::white || design.accent.getPerceivedBrightness() < 0.45f ? juce::Colour (0xffffb020) : design.accent;
}

void DesignLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    if (b.getProperties().contains ("sprite"))
    {
        // (a photographed switch: its part of the photo, thrown; a button: the face shows it)
        drawSprite (g, b, design.controls[(size_t) (int) b.getProperties()["sprite"]].sprite, 0.0f, b.getToggleState(), 0);
        return;
    }
    if (b.getProperties()["hotspot"])
    {
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.06f));
            g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 4.0f);
        }
        return;
    }
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = b.getToggleState();
    if (theme::glassSkin() && design.photoScale <= 0.0f || theme::glassSkin() && design.pageParam == "model")
    {
        // Liquid Glass: a glass pill, lit in the accent when chosen
        look::drawKey (g, r, look::Key::dark, on, theme::accent, over, down);
        return;
    }
    if (design.knobs == KnobStyle::silver)
    {
        // Light hardware keys, the chosen one sunk and lit
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 3.0f);
        g.setGradientFill (on ? juce::ColourGradient (juce::Colour (0xff35577d), 0.0f, r.getY(), juce::Colour (0xff27466a), 0.0f, r.getBottom(), false)
                              : juce::ColourGradient (juce::Colour (0xffe6e8ea), 0.0f, r.getY(), juce::Colour (0xffb7bbc0), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return;
    }
    if (design.knobs == KnobStyle::black || design.knobs == KnobStyle::white || design.knobs == KnobStyle::chrome)
    {
        // Hardware push buttons: dark caps, a lamp lit over the chosen one
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (on || down ? 0xff2b2d31 : 0xff45484d), 0.0f, r.getY(), juce::Colour (0xff1c1d20), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        if (over && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (on)
        {
            const auto lamp = hardwareLamp();
            g.setColour (lamp.withAlpha (0.3f));
            g.fillRoundedRectangle (r.getX() + 6.0f, r.getY() + 2.0f, r.getWidth() - 12.0f, 5.0f, 2.0f);
            g.setColour (lamp);
            g.fillRoundedRectangle (r.getX() + 8.0f, r.getY() + 3.0f, r.getWidth() - 16.0f, 3.0f, 1.5f);
        }
        return;
    }
    const auto light = design.knobs == KnobStyle::light;
    juce::Colour fill = on ? design.accent.withAlpha (light ? 0.9f : 0.85f) : (light ? juce::Colour (0xfff2f2f2) : juce::Colour (0xff222b36));
    if (over && ! on) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (light ? 0.12f : 0.35f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void DesignLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    if (b.getProperties()["hotspot"] || b.getProperties().contains ("sprite"))
        return;
    const auto on = b.getToggleState();
    juce::Colour colour;
    if (theme::glassSkin() && (design.photoScale <= 0.0f || design.pageParam == "model"))
        colour = on ? (theme::accent.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff1a1205) : juce::Colours::white) : theme::text;
    else if (design.knobs == KnobStyle::silver)
        colour = on ? juce::Colour (0xff7fd0ff) : juce::Colour (0xff23262a);
    else if (design.knobs == KnobStyle::light)
        colour = on ? juce::Colours::white : juce::Colour (0xff8a8a8a);
    else if (design.knobs == KnobStyle::black || design.knobs == KnobStyle::white || design.knobs == KnobStyle::chrome)
        colour = on ? hardwareLamp() : juce::Colour (0xffc4c7cb);
    else
        colour = on ? juce::Colour (0xff0c1a24) : design.label;
    g.setColour (colour);
    g.setFont (juce::FontOptions (b.getHeight() > 40 ? 14.5f : 14.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2, 0.8f);
}

void DesignLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // Just the value and a small up/down mark (as Logic's menus)
    juce::ignoreUnused (h);
    if (design.lcdValues)
    {
        // (a small dark readout, as the knobs' values)
        g.setColour (juce::Colour (0xff0c0d0e));
        g.fillRoundedRectangle (box.getLocalBounds().toFloat().reduced (0.5f), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (box.getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
    if (w < 44)
    {
        // (a narrow one, as a step's cell: a small box, the value alone)
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (box.getLocalBounds().toFloat().reduced (1.0f), 3.0f);
        return;
    }
    g.setColour (box.getProperties().contains ("ink") ? juce::Colour::fromString (box.getProperties()["ink"].toString()) : valueColourOf (design));
    const auto cx = (float) w - 10.0f, cy = (float) box.getHeight() * 0.5f;
    juce::Path p;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f); p.lineTo (cx, cy - 6.0f); p.lineTo (cx + 4.0f, cy - 2.0f);
    p.startNewSubPath (cx - 4.0f, cy + 2.0f); p.lineTo (cx, cy + 6.0f); p.lineTo (cx + 4.0f, cy + 2.0f);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void DesignLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto inset = design.lcdValues ? 6 : 0;
    const auto narrow = box.getWidth() < 44;
    label.setBounds (narrow ? 0 : inset, 0, narrow ? box.getWidth() : box.getWidth() - 20 - inset, box.getHeight());
    label.setFont (narrow ? juce::Font (juce::FontOptions (11.0f)) : getComboBoxFont (box));
    label.setBorderSize (narrow ? juce::BorderSize<int>() : label.getBorderSize());
    // (a menu with its own ink, for one sitting on a light panel)
    label.setColour (juce::Label::textColourId, box.getProperties().contains ("ink") ? juce::Colour::fromString (box.getProperties()["ink"].toString()) : valueColourOf (design));
    label.setJustificationType (box.getProperties()["centred"] || narrow ? juce::Justification::centred : juce::Justification::centredLeft);
}

juce::Font DesignLook::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions (box.getHeight() >= 38 ? 24.0f : 17.0f));
}

} // namespace riff::effectdesign
