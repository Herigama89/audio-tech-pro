#include "Looks.h"
#include "Theme.h"
#include "Skin.h"
#include <BinaryData.h>
#include <map>

namespace riff::looks
{

namespace
{
    #define RIFF_LOOK_IMAGE(fn, name) \
        juce::Image fn() { static const juce::Image image = juce::ImageCache::getFromMemory (BinaryData::name, BinaryData::name##Size); return image; }

    RIFF_LOOK_IMAGE (capBlack,      look_cap_black_png)
    RIFF_LOOK_IMAGE (capBlackRed,   look_cap_black_red_png)
    RIFF_LOOK_IMAGE (capSilver,     look_cap_silver_png)
    RIFF_LOOK_IMAGE (knobBlack,     look_knob_black_png)
    RIFF_LOOK_IMAGE (knobBlackGold, look_knob_blackyellow_png)
    RIFF_LOOK_IMAGE (knobYellow,    look_knob_yellow_png)
    RIFF_LOOK_IMAGE (knobRed,       look_knob_red_png)
    RIFF_LOOK_IMAGE (buttonWhite,   look_btn_square_white_png)
    RIFF_LOOK_IMAGE (buttonGrey,    look_btn_square_grey_png)
    RIFF_LOOK_IMAGE (buttonRound,   look_btn_round_png)
    RIFF_LOOK_IMAGE (meterBlack,    look_vu_black_png)
    RIFF_LOOK_IMAGE (meterCream,    look_vu_cream_png)
    RIFF_LOOK_IMAGE (meterAmber,    look_vu_amber_png)   // the Dark VU theme's pieces
    RIFF_LOOK_IMAGE (capDark,       look_cap_dark_png)
    RIFF_LOOK_IMAGE (knobDark,      look_knob_dark_png)
    RIFF_LOOK_IMAGE (buttonDark,    look_btn_dark_png)
    #undef RIFF_LOOK_IMAGE

    juce::Image capFor (int style)
    {
        switch (style)
        {
            case 4: return skin::whiteCapBlack();
            case 5: return capBlack();
            case 6: return capBlackRed();
            case 7: return capSilver();
            case 8: return capDark();
            default: return {};
        }
    }

    juce::Image knobFor (int style)
    {
        switch (style)
        {
            case 1: return knobBlack();
            case 2: return knobBlackGold();
            case 3: return knobYellow();
            case 4: return knobRed();
            case 7: return knobDark();
            default: return {};
        }
    }

    /** A key's photo, lit in a colour or unlit (made once per colour: cached). Its bright parts take the colour; the bezel stays. */
    juce::Image keyIn (int style, bool on, juce::Colour colour)
    {
        static std::map<juce::String, juce::Image> cache;
        const auto source = style == 1 ? buttonWhite() : style == 2 ? buttonGrey() : style == 4 ? buttonDark() : buttonRound();
        const auto key = juce::String (style) + (on ? colour.toString() : juce::String ("off"));
        if (auto it = cache.find (key); it != cache.end())
            return it->second;

        auto image = source.convertedToFormat (juce::Image::ARGB);
        image = image.createCopy();
        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const auto p = data.getPixelColour (x, y);
                    const auto l = p.getPerceivedBrightness();
                    juce::Colour c;
                    if (style == 4)
                    {
                        // The dark key: lit, its face fills with the colour (a little shaded, as the picture's FX key)
                        c = on && p.getAlpha() > 200 && l > 0.07f
                                ? p.interpolatedWith (colour.withMultipliedBrightness (0.62f + 0.9f * l), 0.82f)
                                : p;
                    }
                    else if (style == 2)
                    {
                        // A grey cap: unlit as photographed; lit, its face takes the colour.
                        c = on ? p.interpolatedWith (colour.withBrightness (juce::jmin (1.0f, colour.getBrightness() * (0.55f + 0.6f * l))),
                                                     juce::jlimit (0.0f, 0.85f, (l - 0.25f) * 2.0f))
                               : p;
                    }
                    else if (on)
                    {
                        // Backlit: the light in the key is the colour, brightest where the photo is.
                        const auto lit = juce::Colour::fromHSV (colour.getHue(), colour.getSaturation() * (1.0f - 0.2f * l * l),
                                                                juce::jmin (1.0f, 0.35f + l * 0.85f), 1.0f);
                        c = p.interpolatedWith (lit, juce::jlimit (0.0f, 1.0f, (l - 0.15f) * 1.6f));
                    }
                    else
                    {
                        // Unlit: the same key, its light off (dimmer, greyer).
                        const auto grey = juce::Colour::fromHSV (p.getHue(), p.getSaturation() * 0.25f, p.getBrightness() * 0.42f, 1.0f);
                        c = grey;
                    }
                    data.setPixelColour (x, y, c.withAlpha (p.getAlpha()));
                }
        }
        return cache[key] = image;
    }

    /** A meter photo and where its needle turns: pivot (in the photo's pixels), length, the window it shows in,
        and the needle's angle (radians, 0 = up) at each marked reduction. */
    struct MeterFace
    {
        juce::Image image;
        juce::Point<float> pivot;
        float length;
        juce::Rectangle<float> window;
        std::vector<std::pair<float, float>> marks;   // (dB of reduction, angle)
        juce::Colour needle;
    };

    const MeterFace* meterFor (int style)
    {
        auto deg = [] (float d) { return juce::degreesToRadians (d); };
        static const MeterFace black { meterBlack(), { 266.5f, 325.5f }, 250.0f, { 38.0f, 43.0f, 480.0f, 212.0f },
                                       { { 0.0f, deg (-37.2f) }, { 4.0f, deg (-21.7f) }, { 8.0f, deg (-6.5f) },
                                         { 12.0f, deg (8.8f) }, { 16.0f, deg (24.3f) }, { 20.0f, deg (39.1f) } },
                                       juce::Colour (0xffeceeee) };
        // A VU's scale read as reduction: 0 at the "0" mark, 20 at "20" (its other marks between).
        static const MeterFace cream { meterCream(), { 200.0f, 322.6f }, 212.0f, { 12.0f, 12.0f, 398.0f, 216.0f },
                                       { { 0.0f, deg (16.7f) }, { 1.0f, deg (10.9f) }, { 3.0f, deg (0.0f) }, { 5.0f, deg (-9.96f) },
                                         { 7.0f, deg (-18.85f) }, { 10.0f, deg (-24.3f) }, { 20.0f, deg (-32.6f) } },
                                       juce::Colour (0xff1b1a18) };
        // The Dark VU theme's amber meter, read as reduction: 0 at its "0" (where the red starts), 20 at the far left.
        static const MeterFace amber { meterAmber(), { 82.3f, 103.0f }, 88.0f, { 3.0f, 3.0f, 161.0f, 73.0f },
                                       { { 0.0f, deg (15.5f) }, { 1.0f, deg (8.0f) }, { 2.0f, deg (1.5f) }, { 3.0f, deg (-5.5f) },
                                         { 5.0f, deg (-15.5f) }, { 7.0f, deg (-23.0f) }, { 10.0f, deg (-32.0f) }, { 20.0f, deg (-45.5f) } },
                                       juce::Colour (0xffeadfc4) };
        return style == 1 ? &black : style == 2 ? &cream : style == 3 ? &amber : nullptr;
    }

    /** The same amber meter as a VU (its marks: -20, -10, -7, -5, -3, -2, -1, 0, +1, +2, +3) */
    const MeterFace& levelVu()
    {
        auto deg = [] (float d) { return juce::degreesToRadians (d); };
        static const MeterFace vu { meterAmber(), { 82.3f, 103.0f }, 88.0f, { 3.0f, 3.0f, 161.0f, 73.0f },
                                    { { -20.0f, deg (-45.5f) }, { -10.0f, deg (-32.0f) }, { -7.0f, deg (-23.0f) }, { -5.0f, deg (-15.5f) },
                                      { -3.0f, deg (-5.5f) }, { -2.0f, deg (1.5f) }, { -1.0f, deg (8.0f) }, { 0.0f, deg (15.5f) },
                                      { 1.0f, deg (25.0f) }, { 2.0f, deg (34.5f) }, { 3.0f, deg (46.5f) } },
                                    juce::Colour (0xffeadfc4) };
        return vu;
    }

    float meterAngle (const MeterFace& m, float db)
    {
        db = juce::jlimit (m.marks.front().first, m.marks.back().first, db);
        for (size_t i = 1; i < m.marks.size(); ++i)
            if (db <= m.marks[i].first)
            {
                const auto [d0, a0] = m.marks[i - 1];
                const auto [d1, a1] = m.marks[i];
                return a0 + (a1 - a0) * (db - d0) / juce::jmax (1.0e-3f, d1 - d0);
            }
        return m.marks.back().second;
    }
}

juce::StringArray faderNames()
{
    return { "Skin's own", "Classic", "Studio White (photo cap)", "Xenon (glass cap)", "REAPER theme's",
             "Black (photo)", "Black long-throw", "Black, red line", "Silver console", "Dark VU (picture)" };
}

juce::StringArray knobNames()
{
    return { "Skin's own", "Classic", "Black, white line", "Black, yellow pointer", "Yellow cap", "Red",
             "Studio White (LED ring)", "Xenon (glass)", "Dark VU (picture)" };
}

juce::StringArray buttonNames()
{
    return { "Skin's own", "Classic", "Square backlit", "Square grey", "Round lamp", "Dark square (picture)" };
}

juce::StringArray meterNames()
{
    return { "Skin's own", "Classic", "Black compression meter", "Cream VU", "Amber VU (picture)" };
}

float capWidthForHeight (float height)
{
    const auto cap = capFor (theme::faderStyle());
    return cap.isValid() ? height * (float) cap.getWidth() / (float) juce::jmax (1, cap.getHeight()) : height * 0.55f;
}

bool drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, float opacity)
{
    const auto image = capFor (theme::faderStyle());
    if (! image.isValid())
        return false;
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (opacity);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto aspect = (float) image.getWidth() / (float) image.getHeight();
    if (vertical)
    {
        const auto h = cap.getHeight();
        g.drawImage (image, juce::Rectangle<float> (h * aspect, h).withCentre (cap.getCentre()));
    }
    else
    {
        // Lying on its side (the track headers' faders run across): its width across the slot.
        const auto s = cap.getHeight() / (float) image.getWidth();
        g.drawImageTransformed (image, juce::AffineTransform::translation (-(float) image.getWidth() * 0.5f, -(float) image.getHeight() * 0.5f)
                                           .scaled (s)
                                           .rotated (juce::MathConstants<float>::halfPi)
                                           .translated (cap.getCentreX(), cap.getCentreY()));
    }
    return true;
}

bool drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, float startAngle, float endAngle,
               bool fromCentre, bool enabled)
{
    const auto style = theme::knobStyle();
    if (style == 5)
    {
        skin::drawPhotoKnob (g, area, proportion, startAngle, endAngle, fromCentre, enabled);
        return true;
    }
    if (style == 6)
    {
        skin::drawXenonKnob (g, area, proportion, startAngle, endAngle, enabled);
        return true;
    }
    const auto image = knobFor (style);
    if (! image.isValid())
        return false;

    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto alpha = enabled ? 1.0f : 0.45f;
    proportion = juce::jlimit (0.0f, 1.0f, proportion);
    const auto to = startAngle + proportion * (endAngle - startAngle);

    // Its scale: dots round it, the ones up to the value lit (from the centre for pan and gains).
    const auto dotRadius = size * 0.47f;
    const auto from = startAngle + (fromCentre ? 0.5f : 0.0f) * (endAngle - startAngle);
    const int dots = size > 40.0f ? 11 : 7;
    for (int i = 0; i < dots; ++i)
    {
        const auto a = startAngle + (endAngle - startAngle) * (float) i / (float) (dots - 1);
        const bool lit = (a >= juce::jmin (from, to) - 0.01f && a <= juce::jmax (from, to) + 0.01f);
        const auto p = juce::Point<float> (centre.x + dotRadius * std::sin (a), centre.y - dotRadius * std::cos (a));
        const auto r = juce::jmax (0.9f, size * 0.025f);
        g.setColour ((lit ? theme::accent : juce::Colour (0xffb9bcc0)).withAlpha ((lit ? 0.95f : 0.55f) * alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p));
    }

    // The knob, turned (the photo's pointer is up), with a soft shadow under it.
    const auto knobSize = size * 0.78f;
    const auto knob = juce::Rectangle<float> (knobSize, knobSize).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillEllipse (knob.translated (knobSize * 0.03f, knobSize * 0.06f));
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto scale = knobSize / (float) image.getWidth();
    g.drawImageTransformed (image, juce::AffineTransform::translation (-(float) image.getWidth() * 0.5f, -(float) image.getHeight() * 0.5f)
                                       .scaled (scale)
                                       .rotated (to)
                                       .translated (centre.x, centre.y));
    return true;
}

bool drawButton (juce::Graphics& g, juce::Rectangle<float> area, bool on, juce::Colour onColour, bool over, bool down)
{
    const auto style = theme::buttonStyle();
    if (style < 1 || style > 4 || area.isEmpty())
        return false;

    const auto image = keyIn (style, on, onColour);
    juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    auto target = area;
    if (style == 3)
        target = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()), juce::jmin (area.getWidth(), area.getHeight()));
    if (down)
        target = target.reduced (0.6f);
    if (on)
    {
        // The light spills round it a little.
        g.setColour (onColour.withAlpha (0.25f));
        if (style == 3)
            g.fillEllipse (target.expanded (2.0f));
        else
            g.fillRoundedRectangle (target.expanded (1.5f), 3.0f);
    }
    g.setOpacity (1.0f);   // (an image is drawn at the current colour's opacity)
    g.drawImage (image, target);
    if (over && ! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        if (style == 3)
            g.fillEllipse (target.reduced (target.getWidth() * 0.15f));
        else
            g.fillRect (target.reduced (target.getWidth() * 0.15f, target.getHeight() * 0.15f));
    }
    return true;
}

juce::Colour buttonTextColour (bool on, juce::Colour onColour)
{
    const auto style = theme::buttonStyle();
    if (style == 2)
        return on ? (onColour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff111214) : juce::Colours::white) : juce::Colour (0xff1c1d20);
    if (on)
        return juce::Colour (0xff111214);    // dark on the lit key
    return juce::Colour (0xffd9dcdf);
}

static bool drawFace (juce::Graphics& g, juce::Rectangle<float> area, const MeterFace& face, float value, bool keepProportion);

bool drawMeter (juce::Graphics& g, juce::Rectangle<float> area, float reductionDb)
{
    const auto* m = meterFor (theme::meterStyle());
    if (m == nullptr || ! m->image.isValid())
        return false;
    return drawFace (g, area, *m, reductionDb, true);
}

bool drawLevelVu (juce::Graphics& g, juce::Rectangle<float> area, float vu, bool stretch)
{
    return drawFace (g, area, levelVu(), vu, ! stretch);
}

static bool drawFace (juce::Graphics& g, juce::Rectangle<float> area, const MeterFace& face, float value, bool keepProportion)
{
    const auto* m = &face;
    const auto reductionDb = value;
    if (! m->image.isValid())
        return false;

    // The photo, kept in proportion, centred in the area (or filling it: a strip's VU is as wide as the strip).
    const auto iw = (float) m->image.getWidth(), ih = (float) m->image.getHeight();
    const auto sx = keepProportion ? juce::jmin (area.getWidth() / iw, area.getHeight() / ih) : area.getWidth() / iw;
    const auto sy = keepProportion ? sx : area.getHeight() / ih;
    const auto scale = juce::jmin (sx, sy);
    const auto placed = keepProportion ? juce::Rectangle<float> (iw * sx, ih * sy).withCentre (area.getCentre()) : area;
    const auto toArea = [placed, sx, sy] (juce::Point<float> p) { return placed.getPosition() + juce::Point<float> (p.x * sx, p.y * sy); };

    juce::Graphics::ScopedSaveState state (g);
    // The photo, scaled once for this size (a scaled photo on every needle move, on every strip, was heavy)
    if (placed.getWidth() >= 1.0f && placed.getHeight() >= 1.0f)
    {
        const auto at = placed.getSmallestIntegerContainer();
        const auto image = m->image;
        skin::drawCached (g, "meterFace" + juce::String ((juce::int64) image.getPixelData().get()), at,
                          [image, size = at.withZeroOrigin().toFloat()] (juce::Graphics& ig)
                          {
                              ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                              ig.drawImage (image, size, juce::RectanglePlacement::stretchToFit);
                          });
    }

    // The needle, in the window only.
    const auto window = juce::Rectangle<float> (toArea (m->window.getPosition()), toArea (m->window.getBottomRight()));
    g.reduceClipRegion (window.toNearestInt());
    const auto pivot = toArea (m->pivot);
    const auto a = meterAngle (*m, reductionDb);
    const auto tip = toArea ({ m->pivot.x + m->length * std::sin (a), m->pivot.y - m->length * std::cos (a) });
    const auto width = juce::jmax (1.0f, 2.2f * scale * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawLine (juce::Line<float> (pivot.translated (3.0f * scale * 2.0f, 2.0f * scale * 2.0f), tip.translated (3.0f * scale * 2.0f, 2.0f * scale * 2.0f)), width);
    g.setColour (m->needle);
    g.drawLine (juce::Line<float> (pivot, tip), width);
    return true;
}

} // namespace riff::looks
