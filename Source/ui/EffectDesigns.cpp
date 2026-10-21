#include "EffectDesigns.h"
#include "../engine/StockDsp.h"
#include "EffectFace.h"

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
    void paintCompressor (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float gr)
    {
        brushed (g, bounds, juce::Colour (0xff35537c), juce::Colour (0xff1f3352), 0.035f);

        // The meter: a light face in a dark bezel, the needle showing the gain reduction
        const RF bezel (110.0f, 78.0f, 540.0f, 206.0f);
        g.setColour (juce::Colour (0xff101722));
        g.fillRect (bezel);
        g.setColour (juce::Colours::black);
        g.drawRect (bezel, 2.0f);
        auto face = bezel.reduced (16.0f, 12.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd7dad8), face.getCentreX(), face.getY(), juce::Colour (0xffb5b9b8), face.getCentreX(), face.getBottom(), false));
        g.fillRect (face);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRect (face, 1.0f);
        // The scale: an arc across the face, its pivot well below it (as a real VU)
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + 250.0f);
        const auto arcRadius = 340.0f;
        const auto angleFor = [] (float db) { return juce::jmap (std::sqrt (juce::jlimit (0.0f, 50.0f, -db) / 50.0f), 0.0f, 1.0f, 0.6f, -0.6f); };
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (face.toNearestInt());
            juce::Path arc;
            arc.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, angleFor (-50.0f), angleFor (0.0f), true);
            g.setColour (juce::Colour (0xff222222));
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
            // The needle: from below the face, up past the scale
            const auto needle = angleFor (-gr);
            g.setColour (juce::Colour (0xff111111));
            g.drawLine ({ pivot.getPointOnCircumference (arcRadius - 150.0f, needle), pivot.getPointOnCircumference (arcRadius + 26.0f, needle) }, 2.6f);
        }
        g.setColour (juce::Colour (0xff3a3d40));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("GAIN REDUCTION", face.removeFromBottom (26.0f), juce::Justification::centred, false);

        // Labels, engraved white
        const auto label = [&] (int x, int y, int w, const char* text) { title (g, R (x, y, w, 20), text, juce::Colours::white.withAlpha (0.92f), 15.5f); };
        label (155, 300, 130, "THRESHOLD");
        label (295, 300, 130, "RATIO");
        label (435, 300, 130, "MAKE UP");
        label (568, 300, 100, "AUTO GAIN");
        label (160, 440, 110, "KNEE");
        label (300, 440, 110, "ATTACK");
        label (440, 440, 110, "RELEASE");
        label (690, 92, 110, "LIMITER");
        label (800, 92, 110, "KEY FILTER");
        label (690, 252, 110, "DISTORTION");
        label (800, 252, 110, "OUTPUT GAIN");
        label (745, 412, 110, "MIX");
        // The limiter's lamp
        const auto limiterOn = valueOf (info, v, "limiter") > 0.5f;
        g.setColour (limiterOn ? juce::Colour (0xffff3b30) : juce::Colour (0xff5a1c1a));
        g.fillEllipse (787.0f, 96.0f, 11.0f, 11.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (787.0f, 96.0f, 11.0f, 11.0f, 1.0f);
        // A line between the main and side controls
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (675, 78, 1, 450);
    }

    //==============================================================================
    // Channel EQ (as Logic's): navy screen, a colour per band, values to drag
    //==============================================================================
    const juce::Colour bandColours[] { juce::Colour (0xffa5adb8), juce::Colour (0xffff9a3c), juce::Colour (0xffe7d94a), juce::Colour (0xff3ddc6a),
                                       juce::Colour (0xff38d6c4), juce::Colour (0xff6fa8ff), juce::Colour (0xffc28bff), juce::Colour (0xffd0d4da) };

    /** A Channel EQ band on its own (the others flat), for its own coloured curve. */
    double bandResponse (const EffectInfo& info, const float* v, int band, double f, double rate)
    {
        std::array<float, BuiltInEffect::maxParams> only {};
        std::copy (v, v + info.params.size(), only.begin());
        only[0] = 10.0f; only[2] = 0.0f; only[4] = 0.0f; only[7] = 0.0f; only[10] = 0.0f; only[13] = 0.0f; only[14] = 20000.0f; only[15] = 0.0f;
        switch (band)
        {
            case 0: only[0] = v[0]; break;
            case 1: only[2] = v[2]; break;
            case 2: only[4] = v[4]; break;
            case 3: only[7] = v[7]; break;
            case 4: only[10] = v[10]; break;
            case 5: only[13] = v[13]; break;
            case 6: only[14] = v[14]; break;
            default: break;
        }
        return info.response (only.data(), f, rate);
    }

    void paintChannelEq (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2433), 0.0f, 0.0f, juce::Colour (0xff0f1620), 0.0f, (float) bounds.getBottom(), false));
        g.fillRect (bounds);
        const RF screen (40.0f, 48.0f, (float) bounds.getWidth() - 80.0f, 262.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2b3e), 0.0f, screen.getY(), juce::Colour (0xff15202f), 0.0f, screen.getBottom(), false));
        g.fillRect (screen);

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

        // Each band's own curve in its colour, then the whole curve over them
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (screen.toNearestInt());
        for (int band = 0; band < 7; ++band)
        {
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
        const struct { int band, freq, gain; } points[] { { 1, 1, 2 }, { 2, 3, 4 }, { 3, 6, 7 }, { 4, 9, 10 }, { 5, 12, 13 } };
        for (const auto& p : points)
        {
            const auto x = xOf (v[p.freq]), y = yOf (v[p.gain]);
            g.setColour (bandColours[p.band]);
            g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.0f);
        }
    }

    void paintChannelEqIcons (juce::Graphics& g, int width)
    {
        // The band icons above the screen: the shape of each band's filter
        const auto columns = 8;
        const auto step = (float) (width - 80) / (float) columns;
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
            g.setColour (b == 7 ? bandColours[6] : bandColours[b == 6 ? 5 : b == 5 ? 4 : b]);
            if (b == 7) g.setColour (juce::Colour (0xffa5adb8));
            g.strokePath (p, juce::PathStrokeType (1.6f));
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
    void paintStudioReverb (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float)
    {
        const R screen (0, 0, bounds.getWidth(), 400);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26282c), 0.0f, 0.0f, juce::Colour (0xff17181b), 0.0f, (float) screen.getBottom(), false));
        g.fillRect (screen);
        const RF plot (60.0f, 80.0f, (float) bounds.getWidth() - 120.0f, 270.0f);
        g.setFont (juce::FontOptions (11.0f));
        for (int k = 1; k <= 10; ++k)
        {
            const auto y = plot.getBottom() - plot.getHeight() * (float) k / 10.0f;
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawText (juce::String (k * 10) + "%", RF (plot.getX() - 46.0f, y - 7.0f, 40.0f, 14.0f), juce::Justification::right, false);
        }
        const auto xOf = [plot] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        for (const auto f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (xOf (f), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), RF (xOf (f) - 18.0f, plot.getBottom() + 4.0f, 36.0f, 14.0f),
                        juce::Justification::centred, false);
        }

        // The cloud: how long each frequency rings, as coloured points (bass red, highs violet)
        const auto decay = juce::jlimit (0.1f, 20.0f, valueOf (info, v, "decay", 2.0f));
        const auto damping = valueOf (info, v, "damping", 40.0f) * 0.01f;
        const auto lowCut = valueOf (info, v, "lowCut", 100.0f), highCut = valueOf (info, v, "highCut", 9000.0f);
        const auto size = valueOf (info, v, "size", 50.0f) * 0.01f;
        juce::Random random (42);
        const auto height = [&] (double f)
        {
            auto h = 0.35f + 0.45f * std::min (1.0f, std::log (1.0f + decay) / std::log (21.0f)) + 0.15f * size;
            h *= 1.0f - damping * 0.7f * (float) juce::jlimit (0.0, 1.0, std::log (f / 1000.0) / std::log (20.0));
            if (f < lowCut) h *= (float) std::pow (f / lowCut, 1.5);
            if (f > highCut) h *= (float) std::pow (highCut / f, 1.5);
            return juce::jlimit (0.02f, 0.95f, h);
        };
        for (int i = 0; i < 7000; ++i)
        {
            const auto t = random.nextFloat();
            const auto f = 20.0 * std::pow (1000.0, (double) t);
            const auto top = height (f);
            const auto y = std::pow (random.nextFloat(), 0.8f) * top;
            const auto colour = juce::Colour::fromHSV (juce::jmap (t, 0.0f, 0.82f), 0.85f, 1.0f, 0.75f * (1.0f - y / std::max (0.05f, top)) + 0.2f);
            g.setColour (colour);
            g.fillEllipse (plot.getX() + t * plot.getWidth(), plot.getBottom() - y * plot.getHeight(), 2.0f, 2.0f);
        }
        // The damping curve in orange
        juce::Path curve;
        for (int x = 0; x <= (int) plot.getWidth(); x += 3)
        {
            const auto f = 20.0 * std::pow (1000.0, x / (double) plot.getWidth());
            const auto y = plot.getBottom() - height (f) * plot.getHeight() - 8.0f;
            if (x == 0) curve.startNewSubPath (plot.getX(), y); else curve.lineTo (plot.getX() + (float) x, y);
        }
        g.setColour (juce::Colour (0xffff6a3d));
        g.strokePath (curve, juce::PathStrokeType (2.5f));

        // The light panel below
        const R panel (0, 400, bounds.getWidth(), bounds.getHeight() - 400);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe1e1e1), 0.0f, (float) panel.getY(), juce::Colour (0xffcfcfcf), 0.0f, (float) panel.getBottom(), false));
        g.fillRect (panel);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        for (const auto x : { 470, 690, 860 })
            g.fillRect (x, panel.getY() + 22, 1, panel.getHeight() - 44);
        // The space's name box
        g.setColour (juce::Colour (0xff3b2722));
        g.fillRoundedRectangle (RF ((float) bounds.getCentreX() - 115.0f, 14.0f, 230.0f, 44.0f), 4.0f);
    }

    //==============================================================================
    // Pitch Correction (as Logic's): the scale on a keyboard, a correction meter, Settings and Tuning
    //==============================================================================
    void paintPitchCorrection (juce::Graphics& g, R bounds, const EffectInfo& info, const float* v, float cents)
    {
        g.setColour (juce::Colour (0xff191d25));
        g.fillRect (bounds.withHeight (360));
        g.setColour (juce::Colour (0xff222b38));
        g.fillRect (bounds.withTrimmedTop (360));
        const auto blue = juce::Colour (0xff4fb3e0);

        // The keyboard: the notes of the scale lit
        static const int scales[][12] {
            { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 },
            { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 } };
        const auto key = juce::roundToInt (valueOf (info, v, "key")), scale = juce::jlimit (0, 5, juce::roundToInt (valueOf (info, v, "scale", 1.0f)));
        const auto inScale = [&] (int pc) { return scales[scale][((pc - key) % 12 + 12) % 12] == 1; };
        const RF keys (250.0f, 120.0f, 480.0f, 200.0f);
        const auto whiteW = keys.getWidth() / 7.0f;
        static const int whites[] { 0, 2, 4, 5, 7, 9, 11 }, blacks[] { 1, 3, 6, 8, 10 };
        static const float blackAt[] { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        g.setFont (juce::FontOptions (14.0f));
        for (int w = 0; w < 7; ++w)
        {
            auto r = RF (keys.getX() + whiteW * (float) w, keys.getY(), whiteW - 3.0f, keys.getHeight());
            g.setColour (inScale (whites[w]) ? blue : juce::Colour (0xff2c3644));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (inScale (whites[w]) ? juce::Colour (0xff0e2433) : juce::Colours::white.withAlpha (0.5f));
            g.drawText (names[whites[w]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
        }
        for (int b = 0; b < 5; ++b)
        {
            auto r = RF (keys.getX() + whiteW * blackAt[b] - whiteW * 0.33f - 1.5f, keys.getY(), whiteW * 0.66f, keys.getHeight() * 0.58f);
            g.setColour (juce::Colour (0xff111419));
            g.fillRoundedRectangle (r.expanded (2.0f, 0.0f), 5.0f);
            g.setColour (inScale (blacks[b]) ? blue : juce::Colour (0xff222a35));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (inScale (blacks[b]) ? juce::Colour (0xff0e2433) : juce::Colours::white.withAlpha (0.5f));
            g.drawText (names[blacks[b]], r.removeFromBottom (26.0f), juce::Justification::centred, false);
        }

        // The correction meter: how far the voice is being moved now
        const RF meter (848.0f, 90.0f, 8.0f, 230.0f);
        g.setColour (juce::Colour (0xff2c3644));
        g.fillRoundedRectangle (meter, 3.0f);
        const auto yOf = [meter] (float c) { return meter.getCentreY() - juce::jlimit (-100.0f, 100.0f, c) / 100.0f * meter.getHeight() * 0.5f; };
        g.setColour (blue);
        const auto y0 = yOf (0.0f), y1 = yOf (cents);
        g.fillRect (meter.getX(), std::min (y0, y1), meter.getWidth(), std::max (2.0f, std::abs (y1 - y0)));
        g.setFont (juce::FontOptions (13.0f));
        for (const auto c : { 100, 50, 0, -50, -100 })
        {
            g.setColour (juce::Colours::white.withAlpha (c == 0 ? 0.9f : 0.55f));
            g.drawText ((c > 0 ? "+ " : c < 0 ? "- " : "") + juce::String (std::abs (c)), RF (790.0f, yOf ((float) c) - 8.0f, 48.0f, 16.0f), juce::Justification::right, false);
        }
        title (g, R (780, 30, 140, 20), "Correction", juce::Colours::white.withAlpha (0.6f), 15.0f);
        title (g, R (780, 52, 140, 22), juce::String (juce::roundToInt (cents)) + " Cent", blue, 17.0f);

        title (g, R (40, 380, 300, 30), "Settings", juce::Colours::white.withAlpha (0.9f), 21.0f, juce::Justification::centredLeft);
        title (g, R (610, 380, 300, 30), "Tuning", juce::Colours::white.withAlpha (0.9f), 21.0f, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRect (580, 390, 1, 190);
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
        d.knobs = KnobStyle::dark;
        d.label = juce::Colour (0xffb9c8d6);
        d.value = modernGreen;
        d.accent = modernGreen;
        d.track = juce::Colour (0xff17263a);
        d.showScale = true;

        const bool display = effectface::hasDisplay (info);
        const bool meter = info.category == "Dynamics";
        d.live = meter;
        const auto sections = effectface::sectionsFor (info);

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
        int x = left + gap, y = 10, rowH = 0, index = 0, widest = 0;
        for (const auto& section : sections)
        {
            // A section's cells, wrapping inside it when there are many
            std::vector<int> widths;
            for (int k = 0; k < section.count && index + k < (int) info.params.size(); ++k)
                widths.push_back (widthOf (index + k));
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
            for (size_t k = 0; k < widths.size(); ++k, ++index)
            {
                if (cx + widths[k] > x + w - 10 + 1)
                {
                    cx = x + 10;
                    cy += cellH;
                }
                const auto& p = info.params[(size_t) index];
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
            g.setGradientFill (juce::ColourGradient (modernBlueTop, 0.0f, 0.0f, modernBlueBottom, 0.0f, (float) bounds.getBottom(), false));
            g.fillRect (bounds);
            for (size_t b = 0; b < boxes.size(); ++b)
            {
                const auto& box = boxes[b];
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
                g.setColour (juce::Colour (0xff141c27));
                g.fillRect (panel);
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
                g.setColour (juce::Colour (0xff0f1722));
                g.fillRect (displayArea.expanded (10, 14).withRight (bounds.getRight()));
                effectface::drawDisplay (g, displayArea.toFloat(), fx, v, modernGreen);
            }
        };
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

    void paintDistortion (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        wood (g, b, juce::Colour (0xff7a3a1c));
        const auto panel = b.reduced (34, 46);
        textured (g, panel, juce::Colour (0xff2a2a2a), juce::Colour (0xff141414), 0.12f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRect (panel, 3);
    }

    void paintAutoWah (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
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

    void paintGuitarAmp (juce::Graphics& g, R b, const EffectInfo&, const float*, float)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2c), 0.0f, 0.0f, juce::Colour (0xff0c0c0d), 0.0f, (float) b.getBottom(), false));
        g.fillRect (b);
        const RF amp (30.0f, 24.0f, (float) b.getWidth() - 60.0f, 420.0f);
        tweed (g, amp);
        // The chrome control strip
        const auto strip = RF (amp.getX() + 26.0f, amp.getY() + 22.0f, amp.getWidth() - 52.0f, 150.0f);
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
        // A black head with vents, a brushed aluminium front
        const RF head (40.0f, 20.0f, (float) b.getWidth() - 80.0f, 420.0f);
        g.setColour (juce::Colour (0xff161718));
        g.fillRoundedRectangle (head, 10.0f);
        for (int k = 0; k < 2; ++k)
            for (float x = head.getX() + 120.0f + (float) k * (head.getWidth() * 0.5f - 60.0f); x < head.getX() + 120.0f + (float) k * (head.getWidth() * 0.5f - 60.0f) + head.getWidth() * 0.32f; x += 9.0f)
            {
                g.setColour (juce::Colours::black);
                g.fillRect (x, head.getY() + 18.0f, 4.0f, 60.0f);
            }
        const auto front = RF (head.getX() + 20.0f, head.getY() + 100.0f, head.getWidth() - 40.0f, head.getHeight() - 120.0f);
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
        // The badge
        const auto badge = RF (front.getCentreX() - 120.0f, front.getY() + 30.0f, 240.0f, 100.0f);
        g.setColour (juce::Colour (0xff101010));
        g.fillEllipse (badge);
        g.setColour (juce::Colour (0xff8a8f94));
        g.drawEllipse (badge, 3.0f);
        title (g, badge.toNearestInt(), "BASS", juce::Colour (0xffd8dadc), 38.0f);
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
        title (g, R (40, 34, 480, 22), "-  MIC PROCESSING  -", juce::Colour (0xffd8c9a6), 13.0f);
        title (g, R (40, 214, 480, 22), "-  MOTOR  -", juce::Colour (0xffd8c9a6), 13.0f);
        // The cabinet on the right
        const RF cab ((float) b.getWidth() - 360.0f, 60.0f, 300.0f, 380.0f);
        g.setColour (juce::Colour (0xff121214));
        g.fillRect (cab.expanded (10.0f, 20.0f));
        wood (g, cab.reduced (40.0f, 30.0f).toNearestInt(), juce::Colour (0xff8a4a22));
        const auto box = cab.reduced (40.0f, 30.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int k = 0; k < 5; ++k)
            g.fillRoundedRectangle (box.getX() + 30.0f, box.getY() + 40.0f + (float) k * 22.0f, box.getWidth() - 60.0f, 8.0f, 3.0f);
        g.drawRect (box, 2.0f);
        const auto speed = juce::roundToInt (valueOf (info, v, "speed"));
        title (g, R ((int) cab.getX(), (int) cab.getBottom() - 6, (int) cab.getWidth(), 22), speed == 1 ? "FAST" : speed == 2 ? "BRAKE" : "SLOW", juce::Colour (0xffd8c9a6), 14.0f);
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
            c.controls = {
                { "drive", knob, R (90, 110, 160, 200), "DRIVE" },
                { "tone", knob, R (270, 110, 160, 200), "TONE" },
                { "output", knob, R (450, 110, 160, 200), "OUTPUT" },
                { "mix", knob, R (630, 110, 160, 200), "MIX" },
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
            const char* ids[] { "gain", "bass", "mid", "treble", "presence", "master" };
            const char* names[] { "GAIN", "BASS", "MIDDLE", "TREBLE", "PRESENCE", "MASTER" };
            for (int k = 0; k < 6; ++k)
                c.controls.push_back ({ ids[k], knob, R (110 + k * 150, 52, 120, 134), names[k] });
            c.controls.push_back ({ "model", Kind::dropdown, R (90, 486, 200, 30), "Amp", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "cab", Kind::dropdown, R (330, 486, 200, 30), "Cabinet", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "mic", knob, R (580, 452, 90, 100), "Mic", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "output", knob, R (720, 452, 90, 100), "Output", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "mix", knob, R (860, 452, 90, 100), "Mix", juce::Colour (0xffaab0b6) });
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
            const char* ids[] { "gain", "bass", "lowMid", "highMid", "treble", "master" };
            const char* names[] { "GAIN", "BASS", "LO MID", "HI MID", "TREBLE", "MASTER" };
            for (int k = 0; k < 6; ++k)
                c.controls.push_back ({ ids[k], knob, R ((k < 3 ? 110 : 650) + (k % 3) * 120, 270, 110, 130), names[k] });
            c.controls.push_back ({ "cab", Kind::dropdown, R (60, 486, 170, 30), "Cabinet", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "mic", knob, R (260, 452, 90, 100), "Mic", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "blend", knob, R (380, 452, 90, 100), "DI Blend", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "diLevel", knob, R (500, 452, 90, 100), "DI Level", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "mix", knob, R (620, 452, 90, 100), "Mix", juce::Colour (0xffaab0b6) });
            c.controls.push_back ({ "output", knob, R (740, 452, 90, 100), "Output", juce::Colour (0xffaab0b6) });
            d["bassAmp"] = c;
        }
        {
            // Rotary cabinet: wood, a dark control panel, the cabinet drawn on the right
            Design c;
            c.width = 940; c.height = 520;
            c.knobs = KnobStyle::black;
            c.label = juce::Colour (0xffd8c9a6); c.value = juce::Colour (0xffd8c9a6); c.accent = juce::Colour (0xffd8c9a6); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.paint = paintRotary;
            c.controls = {
                { "doppler", knob, R (50, 64, 120, 140), "DOPPLER" },
                { "balance", knob, R (200, 64, 120, 140), "HORN/DRUM" },
                { "width", knob, R (350, 64, 120, 140), "MIC WIDTH" },
                { "accel", knob, R (50, 244, 120, 140), "ACCELERATION" },
                { "fast", knob, R (200, 244, 120, 140), "MAX RATE" },
                { "drive", knob, R (350, 244, 120, 140), "DRIVE" },
                { "speed", Kind::segmented, R (90, 404, 330, 36), "-" },
            };
            d["rotary"] = c;
        }
        {
            // Saturator: purple, glowing waveforms round the Drive knob
            Design c;
            c.width = 1000; c.height = 420;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colour (0xffb9a8e0); c.value = juce::Colour (0xffffb04a); c.accent = juce::Colour (0xffffa03a); c.track = juce::Colour (0xff2a1f45);
            c.paint = paintSaturator;
            c.controls = {
                { "model", Kind::dropdown, R (40, 40, 220, 34), "Model" },
                { "drive", knob, R (420, 120, 160, 200), "Drive" },
                { "tone", knob, R (600, 290, 120, 120), "-" },
                { "mix", knob, R (730, 290, 120, 120), "-" },
                { "output", knob, R (860, 290, 120, 120), "-" },
            };
            for (auto& ctl : c.controls)
                if (ctl.label == "-") ctl.label = {};
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
            c.width = 860; c.height = 350;
            c.knobs = KnobStyle::chrome;
            c.label = juce::Colour (0xff1d2833); c.value = juce::Colour (0xffd9ff4a); c.accent = juce::Colour (0xffd9ff4a); c.track = juce::Colours::black;
            c.valueAbove = false;
            c.lcdValues = true;
            c.paint = paintVocalTransformer;
            c.controls = {
                { "formant", knob, R (70, 60, 160, 210), "Formant" },
                { "pitch", knob, R (340, 60, 160, 210), "Pitch" },
                { "mode", Kind::segmented, R (600, 70, 230, 34), "Mode" },
                { "robotNote", Kind::dropdown, R (620, 170, 190, 30), "Robot Note" },
                { "mix", knob, R (650, 230, 130, 110), "Mix" },
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
            c.width = 940; c.height = 560;
            c.knobs = KnobStyle::silver;
            c.label = juce::Colours::white; c.value = juce::Colours::white; c.accent = juce::Colour (0xff6ec6ff); c.track = juce::Colour (0xff1a2a42);
            c.valueAbove = false;
            c.live = true;
            c.paint = paintCompressor;
            c.controls = {
                { "model", Kind::segmented, R (110, 16, 700, 46), "-" },
                { "threshold", Kind::knob, R (165, 322, 110, 110), "-" },
                { "ratio", Kind::knob, R (305, 322, 110, 110), "-" },
                { "makeup", Kind::knob, R (445, 322, 110, 110), "-" },
                { "autoGain", Kind::toggle, R (585, 345, 66, 34), "-" },
                { "knee", Kind::knob, R (175, 462, 90, 90), "-" },
                { "attack", Kind::knob, R (315, 462, 90, 90), "-" },
                { "release", Kind::knob, R (455, 462, 90, 90), "-" },
                { "limiter", Kind::toggle, R (712, 118, 66, 34), "-" },
                { "scHpf", Kind::knob, R (810, 114, 90, 84), "-" },
                { "distortion", Kind::knob, R (690, 274, 110, 96), "-" },
                { "output", Kind::knob, R (810, 274, 90, 90), "-" },
                { "mix", Kind::knob, R (750, 434, 100, 100), "-" },
            };
            d["studioComp"] = c;
        }
        {
            Design c;
            c.width = 1000; c.height = 440;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.55f); c.value = juce::Colours::white; c.accent = juce::Colour (0xff4fa3ff); c.track = juce::Colour (0xff0d131c);
            c.paint = [] (juce::Graphics& g, R b, const EffectInfo& info, const float* v, float live)
            {
                paintChannelEq (g, b, info, v, live);
                paintChannelEqIcons (g, b.getWidth());
            };
            // The values under the screen, one column per band in its colour
            const auto column = [] (int b) { return 40 + (int) ((1000 - 80) / 8.0f * (float) b); };
            const auto w = (1000 - 80) / 8;
            const std::vector<std::vector<const char*>> bands {
                { "hpFreq" }, { "lsFreq", "lsGain" }, { "p1Freq", "p1Gain", "p1Q" }, { "p2Freq", "p2Gain", "p2Q" },
                { "p3Freq", "p3Gain", "p3Q" }, { "hsFreq", "hsGain" }, { "lpFreq" }, { "output" } };
            for (int b = 0; b < 8; ++b)
                for (size_t row = 0; row < bands[(size_t) b].size(); ++row)
                    c.controls.push_back ({ bands[(size_t) b][row], Kind::field, R (column (b), 326 + (int) row * 30, w, 28), "-",
                                            b == 7 ? juce::Colour (0xffd0d4da) : bandColours[b] });
            d["channelEq"] = c;
            d["linearEq"] = c;
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
                { "drive", Kind::knob, R (420, 82, 120, 150), "Saturation" },
                { "spread", Kind::knob, R (600, 82, 120, 150), "Spread" },
                { "lowCut", Kind::knob, R (420, 240, 120, 140), "Low Cut" },
                { "highCut", Kind::knob, R (600, 240, 120, 140), "High Cut" },
                { "feedback", Kind::knob, R (810, 82, 130, 170), "Feedback" },
                { "wow", Kind::knob, R (150, 460, 130, 160), "Wow" },
                { "flutter", Kind::knob, R (420, 460, 130, 160), "Flutter" },
                { "mix", Kind::fader, R (850, 370, 60, 260), "Mix" },
            };
            d["tapeDelay"] = c;
        }
        {
            Design c;
            c.width = 1000; c.height = 680;
            c.knobs = KnobStyle::light;
            c.label = juce::Colour (0xff3b3b3b); c.value = juce::Colour (0xffff6a3d); c.accent = juce::Colour (0xffff6a3d); c.track = juce::Colour (0xffa9a9a9);
            c.paint = paintStudioReverb;
            c.controls = {
                { "space", Kind::dropdown, R (385, 16, 230, 40), "-" },
                { "freeze", Kind::toggle, R (880, 40, 90, 30), "Freeze", juce::Colours::white.withAlpha (0.55f) },
                { "size", Kind::knob, R (30, 420, 120, 140), "Size" },
                { "diffusion", Kind::knob, R (170, 420, 120, 140), "Density" },
                { "damping", Kind::knob, R (310, 420, 120, 140), "Damping" },
                { "predelay", Kind::knob, R (40, 570, 100, 100), "Predelay" },
                { "lowCut", Kind::knob, R (180, 570, 100, 100), "Low Cut" },
                { "highCut", Kind::knob, R (320, 570, 100, 100), "High Cut" },
                { "decay", Kind::knob, R (505, 420, 150, 190), "Decay" },
                { "mod", Kind::knob, R (715, 420, 120, 140), "Modulation" },
                { "width", Kind::knob, R (715, 560, 120, 110), "Width" },
                { "mix", Kind::fader, R (895, 420, 70, 240), "Mix" },
            };
            d["studioReverb"] = c;
        }
        {
            Design c;
            c.width = 940; c.height = 600;
            c.knobs = KnobStyle::dark;
            c.label = juce::Colours::white.withAlpha (0.6f); c.value = juce::Colour (0xff4fb3e0); c.accent = juce::Colour (0xff4fb3e0); c.track = juce::Colour (0xff141a22);
            c.live = true;
            c.paint = paintPitchCorrection;
            c.controls = {
                { "key", Kind::dropdown, R (40, 52, 160, 30), "Root Note" },
                { "scale", Kind::dropdown, R (40, 132, 190, 30), "Scale" },
                { "response", Kind::knob, R (220, 420, 120, 160), "Response" },
                { "amount", Kind::knob, R (390, 420, 120, 160), "Amount" },
                { "detune", Kind::knob, R (760, 420, 120, 160), "Detune" },
            };
            d["pitchCorrection"] = c;
        }
        addHardwareDesigns (d);
        return d;
    }
}

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
    const auto it = designs.find (type);
    return it != designs.end() ? &it->second : nullptr;
}

//==============================================================================
DesignLook::DesignLook (const Design& d) : design (d)
{
    setColour (juce::Label::textColourId, design.label);
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
    const auto colour = s.getProperties().contains ("colour") ? juce::Colour::fromString (s.getProperties()["colour"].toString()) : design.value;
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
        l->setColour (juce::Label::textColourId, design.value);
    }
    return l;
}

void DesignLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    if (s.getProperties()["field"])
        return;   // (a value field: just its number)
    const auto size = (float) std::min (w, h);
    const auto bounds = RF (size, size).withCentre (R (x, y, w, h).toFloat().getCentre());
    const auto centre = bounds.getCentre();
    const auto angle = start + pos * (end - start);

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
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    const auto cx = (float) x + (float) w * 0.5f;
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
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = b.getToggleState();
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
    const auto on = b.getToggleState();
    juce::Colour colour;
    if (design.knobs == KnobStyle::silver)
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
    g.setColour (design.value);
    const auto cx = (float) w - 10.0f, cy = (float) box.getHeight() * 0.5f;
    juce::Path p;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f); p.lineTo (cx, cy - 6.0f); p.lineTo (cx + 4.0f, cy - 2.0f);
    p.startNewSubPath (cx - 4.0f, cy + 2.0f); p.lineTo (cx, cy + 6.0f); p.lineTo (cx + 4.0f, cy + 2.0f);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void DesignLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto inset = design.lcdValues ? 6 : 0;
    label.setBounds (inset, 0, box.getWidth() - 20 - inset, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, design.value);
    label.setJustificationType (box.getProperties()["centred"] ? juce::Justification::centred : juce::Justification::centredLeft);
}

juce::Font DesignLook::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions (box.getHeight() >= 38 ? 24.0f : 17.0f));
}

} // namespace riff::effectdesign
