#include "EffectDesigns.h"
#include "../engine/StockDsp.h"

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
        return d;
    }
}

const Design* designFor (const juce::String& type)
{
    static const auto designs = makeDesigns();
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
}

void DesignLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    // A fader: a thin slot, the level in the accent colour, a small flat cap
    const auto cx = (float) x + (float) w * 0.5f;
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
    g.setColour (design.value);
    const auto cx = (float) w - 10.0f, cy = (float) box.getHeight() * 0.5f;
    juce::Path p;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f); p.lineTo (cx, cy - 6.0f); p.lineTo (cx + 4.0f, cy - 2.0f);
    p.startNewSubPath (cx - 4.0f, cy + 2.0f); p.lineTo (cx, cy + 6.0f); p.lineTo (cx + 4.0f, cy + 2.0f);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void DesignLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (0, 0, box.getWidth() - 20, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, design.value);
    label.setJustificationType (box.getProperties()["centred"] ? juce::Justification::centred : juce::Justification::centredLeft);
}

juce::Font DesignLook::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions (box.getHeight() >= 38 ? 24.0f : 17.0f));
}

} // namespace riff::effectdesign
