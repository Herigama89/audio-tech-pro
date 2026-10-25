#include "ThemePack.h"
#include "Theme.h"

namespace riff::themepack
{
namespace
{
    struct Current
    {
        juce::File root, images;
        juce::String name;
        std::map<juce::String, juce::Colour> colours;
        std::map<juce::String, Piece> pieces;
    };

    Current& current()
    {
        static Current c;
        return c;
    }

    const Piece& none()
    {
        static Piece empty;
        return empty;
    }

    juce::File themesFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Audio Tech Pro").getChildFile ("Themes");
    }

    bool ignored (const juce::File& f) { return f.getFullPathName().contains ("__MACOSX"); }

    juce::File themeFileIn (const juce::File& root)
    {
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "*.ReaperTheme", juce::File::findFiles))
            if (! ignored (e.getFile()))
                return e.getFile();
        return {};
    }

    /** The folder holding the pictures: the theme file's ui_img, else the one with rtconfig.txt, else the one with most pictures. */
    juce::File imagesIn (const juce::File& root, const juce::File& themeFile, const juce::String& uiImg)
    {
        if (uiImg.isNotEmpty())
            for (const auto& base : { themeFile.getParentDirectory(), root })
                if (const auto f = base.getChildFile (uiImg); f.isDirectory())
                    return f;
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "rtconfig.txt", juce::File::findFiles))
            if (! ignored (e.getFile()))
                return e.getFile().getParentDirectory();
        juce::File best = root;
        int most = 0;
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "*", juce::File::findDirectories))
        {
            if (ignored (e.getFile()))
                continue;
            const auto n = e.getFile().getNumberOfChildFiles (juce::File::findFiles, "*.png");
            if (n > most)
            {
                most = n;
                best = e.getFile();
            }
        }
        return best;
    }

    /** REAPER keeps colours as Windows COLORREFs (0x00BBGGRR), sometimes with flags in the top byte. */
    juce::Colour fromColorRef (juce::int64 v)
    {
        const auto c = (juce::uint32) (v & 0xffffff);
        return juce::Colour ((juce::uint8) (c & 0xff), (juce::uint8) ((c >> 8) & 0xff), (juce::uint8) ((c >> 16) & 0xff));
    }

    /** REAPER's marks: pink (255, 0, 255) and yellow (255, 255, 0) pixels in a picture's frame. */
    bool isMarker (juce::Colour c)
    {
        return c.getAlpha() == 255 && c.getRed() == 255
            && ((c.getGreen() == 0 && c.getBlue() == 255) || (c.getGreen() == 255 && c.getBlue() == 0));
    }

    bool hasStates (const juce::String& name)
    {
        for (const auto* prefix : { "transport_", "track_", "gen_mute", "gen_solo", "gen_play", "gen_stop", "gen_pause", "gen_repeat",
                                    "mcp_mute", "mcp_solo", "mcp_recarm", "mcp_monitor", "mcp_env", "mcp_fx_", "tcp_fx_" })
            if (name.startsWith (prefix))
                return ! name.contains ("_bg") && ! name.contains ("stack") && ! name.contains ("thumb");
        return false;
    }

    Piece loadPiece (const juce::String& name)
    {
        Piece p;
        const auto& c = current();
        juce::File file;
        for (const auto* sub : { "200", "" })
        {
            const auto f = (juce::String (sub).isEmpty() ? c.images : c.images.getChildFile (sub)).getChildFile (name + ".png");
            if (f.existsAsFile())
            {
                file = f;
                p.scale = juce::String (sub).isEmpty() ? 1.0f : 2.0f;
                break;
            }
        }
        if (! file.existsAsFile())
            return p;
        auto image = juce::ImageFileFormat::loadFrom (file);
        if (! image.isValid() || (image.getWidth() <= 8 && image.getHeight() <= 8))
            return p;   // (a placeholder: the theme draws it some other way)
        image = image.convertedToFormat (juce::Image::ARGB);
        {
            // A picture left fully clear means the same (the theme writes the button's text itself).
            bool anything = false;
            const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < image.getHeight() && ! anything; ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    if (pixels.getPixelColour (x, y).getAlpha() > 8) { anything = true; break; }
            if (! anything)
                return p;
        }

        // REAPER's frame: marks along the top and left (and sometimes the right and bottom) say what stretches.
        const auto w = image.getWidth(), h = image.getHeight();
        bool top = false, left = false, right = false, bottom = false;
        for (int x = 0; x < w; ++x) { top = top || isMarker (image.getPixelAt (x, 0)); bottom = bottom || isMarker (image.getPixelAt (x, h - 1)); }
        for (int y = 0; y < h; ++y) { left = left || isMarker (image.getPixelAt (0, y)); right = right || isMarker (image.getPixelAt (w - 1, y)); }
        const bool framed = top || left;

        p.states = hasStates (name) ? 3 : 1;
        p.statesVertical = name.startsWith ("mcp_fxlist") || name.startsWith ("tcp_fxlist");
        if (p.statesVertical)
            p.states = 3;

        if (framed)
        {
            // Margins: marked runs from the start and from the end of the first row / column (within the first state).
            const auto stateW = (w - 1 - (right ? 1 : 0)) / (p.statesVertical ? 1 : p.states);
            const auto stateH = (h - 1 - (bottom ? 1 : 0)) / (p.statesVertical ? p.states : 1);
            int l = 0, r = 0, t = 0, b = 0;
            while (l < stateW && isMarker (image.getPixelAt (1 + l, 0))) ++l;
            while (r < stateW - l && isMarker (image.getPixelAt (stateW - r, 0))) ++r;
            while (t < stateH && isMarker (image.getPixelAt (0, 1 + t))) ++t;
            while (b < stateH - t && isMarker (image.getPixelAt (0, stateH - b))) ++b;
            if (l > 0 || r > 0)
                p.stretchX = { l, juce::jmax (l, stateW - r) };
            if (t > 0 || b > 0)
                p.stretchY = { t, juce::jmax (t, stateH - b) };
            image = image.getClippedImage ({ 1, 1, w - 1 - (right ? 1 : 0), h - 1 - (bottom ? 1 : 0) });
            image = image.createCopy();
        }
        p.image = image;
        return p;
    }
}

juce::Rectangle<int> Piece::stateBounds (int state) const
{
    state = juce::jlimit (0, states - 1, state);
    if (statesVertical)
    {
        const auto hEach = image.getHeight() / states;
        return { 0, state * hEach, image.getWidth(), hEach };
    }
    const auto wEach = image.getWidth() / states;
    return { state * wEach, 0, wEach, image.getHeight() };
}

juce::Array<juce::File> installed()
{
    juce::Array<juce::File> list;
    for (const auto& e : juce::RangedDirectoryIterator (themesFolder(), false, "*", juce::File::findDirectories))
        list.add (e.getFile());
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return list;
}

juce::File install (const juce::File& source, juce::String& error)
{
    auto name = source.getFileNameWithoutExtension().replace ("_unpacked", "").trim();
    if (name.isEmpty())
        name = "Theme";
    auto target = themesFolder().getChildFile (juce::File::createLegalFileName (name));
    target.deleteRecursively();
    target.createDirectory();

    if (source.isDirectory())
    {
        if (! source.copyDirectoryTo (target))
            error = "The theme folder couldn't be copied.";
    }
    else if (source.hasFileExtension ("ReaperThemeZip;zip"))
    {
        juce::ZipFile zip (source);
        if (zip.getNumEntries() == 0 || zip.uncompressTo (target).failed())
            error = "The theme file couldn't be unpacked.";
    }
    else if (source.hasFileExtension ("ReaperTheme"))
    {
        // Colours only (its pictures, if any, sit in a folder beside it).
        source.copyFileTo (target.getChildFile (source.getFileName()));
        juce::StringArray lines;
        lines.addLines (source.loadFileAsString());
        for (const auto& line : lines)
            if (line.startsWith ("ui_img="))
                if (const auto pictures = source.getSiblingFile (line.fromFirstOccurrenceOf ("=", false, false).trim()); pictures.isDirectory())
                    pictures.copyDirectoryTo (target.getChildFile (pictures.getFileName()));
    }
    else
        error = "Choose a REAPER theme (.ReaperThemeZip or .ReaperTheme).";

    if (error.isEmpty() && ! themeFileIn (target).existsAsFile() && target.getNumberOfChildFiles (juce::File::findFiles, "*.png") == 0)
        error = "That doesn't look like a REAPER theme (no .ReaperTheme or pictures inside).";
    if (error.isNotEmpty())
    {
        target.deleteRecursively();
        return {};
    }
    target.setLastModificationTime (juce::Time::getCurrentTime());
    return target;
}

void useForTesting (const juce::File& themeFolder) { use (themeFolder); }

void use (const juce::File& themeFolder)
{
    auto& c = current();
    if (c.root == themeFolder && (themeFolder == juce::File() || ! c.colours.empty() || c.images != juce::File()))
        return;
    theme::touch();
    c = {};
    if (! themeFolder.isDirectory())
        return;

    c.root = themeFolder;
    c.name = themeFolder.getFileName();
    const auto themeFile = themeFileIn (themeFolder);
    juce::String uiImg;
    if (themeFile.existsAsFile())
    {
        juce::StringArray lines;
        lines.addLines (themeFile.loadFileAsString());
        bool inColours = false;
        for (const auto& raw : lines)
        {
            const auto line = raw.trim();
            if (line.startsWith ("["))
            {
                inColours = line.equalsIgnoreCase ("[color theme]");
                continue;
            }
            const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();
            if (key == "ui_img")
                uiImg = value;
            else if (inColours && key.isNotEmpty() && value.containsOnly ("-0123456789"))
                c.colours[key] = fromColorRef (value.getLargeIntValue());
        }
    }
    c.images = imagesIn (themeFolder, themeFile, uiImg);
}

bool active()          { return current().root != juce::File(); }
juce::String name()    { return current().name; }
juce::File folder()    { return current().root; }

std::optional<juce::Colour> colour (const juce::String& key)
{
    const auto& colours = current().colours;
    if (auto it = colours.find (key); it != colours.end())
        return it->second;
    return {};
}

const Piece& piece (std::initializer_list<const char*> names)
{
    auto& c = current();
    if (! active())
        return none();
    for (const auto* n : names)
    {
        auto it = c.pieces.find (n);
        if (it == c.pieces.end())
            it = c.pieces.emplace (n, loadPiece (n)).first;
        if (it->second.isValid())
            return it->second;
    }
    return none();
}

void draw (juce::Graphics& g, const Piece& p, int state, juce::Rectangle<float> area, bool stretch, float opacity)
{
    if (! p.isValid() || area.isEmpty())
        return;
    const auto src = p.stateBounds (state);
    juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (opacity);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    if (! stretch)
    {
        // In proportion, centred
        const auto aspect = (float) src.getWidth() / (float) juce::jmax (1, src.getHeight());
        auto dest = area;
        if (dest.getWidth() / dest.getHeight() > aspect)
            dest = dest.withSizeKeepingCentre (dest.getHeight() * aspect, dest.getHeight());
        else
            dest = dest.withSizeKeepingCentre (dest.getWidth(), dest.getWidth() / aspect);
        g.drawImage (p.image, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                     src.getX(), src.getY(), src.getWidth(), src.getHeight());
        return;
    }

    // Stretched: the margins keep their size (at the picture's scale), the middle stretches (nine pieces).
    const auto cuts = [] (juce::Range<int> stretchRange, int length, float destStart, float destLength, float scale)
    {
        std::vector<std::pair<juce::Range<int>, juce::Range<float>>> parts;
        if (stretchRange.isEmpty())
        {
            parts.push_back ({ { 0, length }, { destStart, destStart + destLength } });
            return parts;
        }
        const auto a = (float) stretchRange.getStart() / scale, b = (float) (length - stretchRange.getEnd()) / scale;
        const auto shrink = juce::jmin (1.0f, destLength / juce::jmax (1.0f, a + b));
        const auto la = a * shrink, lb = b * shrink;
        parts.push_back ({ { 0, stretchRange.getStart() }, { destStart, destStart + la } });
        parts.push_back ({ stretchRange, { destStart + la, destStart + destLength - lb } });
        parts.push_back ({ { stretchRange.getEnd(), length }, { destStart + destLength - lb, destStart + destLength } });
        return parts;
    };
    for (const auto& [sx, dx] : cuts (p.stretchX, src.getWidth(), area.getX(), area.getWidth(), p.scale))
        for (const auto& [sy, dy] : cuts (p.stretchY, src.getHeight(), area.getY(), area.getHeight(), p.scale))
            if (! sx.isEmpty() && ! sy.isEmpty() && dx.getLength() > 0.0f && dy.getLength() > 0.0f)
                g.drawImage (p.image, (int) std::floor (dx.getStart()), (int) std::floor (dy.getStart()),
                             (int) std::ceil (dx.getEnd()) - (int) std::floor (dx.getStart()), (int) std::ceil (dy.getEnd()) - (int) std::floor (dy.getStart()),
                             src.getX() + sx.getStart(), src.getY() + sy.getStart(), sx.getLength(), sy.getLength());
}

bool drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, bool pan, bool enabled)
{
    const auto& stack = pan ? piece ({ "mcp_pan_knob_stack", "tcp_pan_knob_stack", "tcp_vol_knob_stack" })
                            : piece ({ "tcp_vol_knob_stack", "mcp_send_knob_stack", "tcp_send_knob_stack", "tcp_pan_knob_stack" });
    const auto& background = piece ({ "gen_knob_bg_small", "transport_knob_bg_small" });
    if (! stack.isValid() && ! background.isValid())
        return false;
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);
    if (background.isValid())
        draw (g, background, 0, box, false, enabled ? 1.0f : 0.5f);
    if (stack.isValid())
    {
        const auto side = stack.image.getWidth();
        const auto frames = juce::jmax (1, stack.image.getHeight() / juce::jmax (1, side));
        const auto index = juce::jlimit (0, frames - 1, juce::roundToInt (juce::jlimit (0.0f, 1.0f, proportion) * (float) (frames - 1)));
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (enabled ? 1.0f : 0.5f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (stack.image, box.getX(), box.getY(), box.getWidth(), box.getHeight(), 0, index * side, side, side);
    }
    return true;
}

bool drawButton (juce::Graphics& g, const juce::String& kind, bool on, bool over, bool down, juce::Rectangle<float> area)
{
    // The first family (mixer, track, general) that has both states, so on and off match.
    const Piece* chosen = nullptr;
    for (const char* prefix : { "mcp_", "track_", "gen_" })
    {
        const auto offName = (prefix + kind + "_off").toStdString(), onName = (prefix + kind + "_on").toStdString();
        const auto& off = piece ({ offName.c_str() });
        const auto& onPiece = piece ({ onName.c_str() });
        if (off.isValid() && onPiece.isValid())
        {
            chosen = on ? &onPiece : &off;
            break;
        }
    }
    if (chosen == nullptr)
    {
        const auto suffix = on ? "_on" : "_off";
        const auto a = ("mcp_" + kind + suffix).toStdString(), b = ("track_" + kind + suffix).toStdString(), c = ("gen_" + kind + suffix).toStdString();
        chosen = &piece ({ a.c_str(), b.c_str(), c.c_str() });
    }
    const auto& p = *chosen;
    if (! p.isValid())
        return false;
    draw (g, p, down ? 2 : over ? 1 : 0, area, false);
    return true;
}

} // namespace riff::themepack
