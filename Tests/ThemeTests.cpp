#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "../Source/ui/ThemePack.h"
#include "../Source/ui/Theme.h"

using namespace riff;

namespace
{
    const juce::Colour pink (255, 0, 255);

    void savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    /** A small theme as REAPER's are laid out: a .ReaperTheme file and a folder of pictures. */
    juce::File makeTheme (const juce::File& parent)
    {
        auto folder = parent.getChildFile ("Test Theme");
        folder.deleteRecursively();
        folder.createDirectory();
        folder.getChildFile ("Test Theme.ReaperTheme")
              .replaceWithText ("[color theme]\ncol_arrangebg=255\ncol_main_text=16711680\n[REAPER]\nui_img=pics\n");
        auto pics = folder.getChildFile ("pics");
        pics.createDirectory();

        // A button: three states of 10 x 8 side by side, in a 1-pixel frame. Pink marks: 2 px fixed at the
        // left, 2 at the right, 1 at the top (the rest stretches).
        juce::Image play (juce::Image::ARGB, 31, 9, true);
        for (int y = 1; y < 9; ++y)
            for (int x = 1; x < 31; ++x)
                play.setPixelAt (x, y, juce::Colours::grey);
        for (int x : { 1, 2, 9, 10 })
            play.setPixelAt (x, 0, pink);
        play.setPixelAt (0, 1, pink);
        savePng (play, pics.getChildFile ("transport_play.png"));

        // A clear picture (REAPER themes leave some so: they write the text themselves) and a placeholder.
        savePng (juce::Image (juce::Image::ARGB, 60, 20, true), pics.getChildFile ("track_mute_off.png"));
        juce::Image on (juce::Image::ARGB, 60, 20, true);
        on.clear (on.getBounds(), juce::Colours::red);
        savePng (on, pics.getChildFile ("track_mute_on.png"));
        juce::Image off (juce::Image::ARGB, 60, 20, true);
        off.clear (off.getBounds(), juce::Colours::darkgrey);
        savePng (off, pics.getChildFile ("gen_mute_off.png"));
        savePng (on, pics.getChildFile ("gen_mute_on.png"));
        savePng (juce::Image (juce::Image::ARGB, 4, 4, true), pics.getChildFile ("transport_stop.png"));
        return folder;
    }
}

TEST_CASE ("A REAPER theme's colours and pictures are read as REAPER lays them out", "[theme]")
{
    juce::TemporaryFile temp;
    temp.getFile().createDirectory();
    const auto folder = makeTheme (temp.getFile());

    themepack::useForTesting (folder);
    REQUIRE (themepack::active());

    // Colours are Windows COLORREFs: 0x00BBGGRR
    REQUIRE (themepack::colour ("col_arrangebg") == std::optional<juce::Colour> (juce::Colour (255, 0, 0)));
    REQUIRE (themepack::colour ("col_main_text") == std::optional<juce::Colour> (juce::Colour (0, 0, 255)));
    REQUIRE (! themepack::colour ("col_nothing").has_value());

    const auto& play = themepack::piece ({ "transport_play" });
    REQUIRE (play.isValid());
    CHECK (play.states == 3);
    CHECK (play.image.getWidth() == 30);     // the frame's gone
    CHECK (play.image.getHeight() == 8);
    CHECK (play.stateBounds (1) == juce::Rectangle<int> (10, 0, 10, 8));
    CHECK (play.stretchX == juce::Range<int> (2, 8));
    CHECK (play.stretchY == juce::Range<int> (1, 8));

    // Placeholders and clear pictures count as "not provided"
    CHECK (! themepack::piece ({ "transport_stop" }).isValid());
    CHECK (! themepack::piece ({ "track_mute_off" }).isValid());
    CHECK (themepack::piece ({ "track_mute_off", "gen_mute_off" }).isValid());

    // A button takes on and off from the same family (here the general one: the track one has no off).
    juce::Image canvas (juce::Image::ARGB, 60, 20, true);
    {
        juce::Graphics g (canvas);
        REQUIRE (themepack::drawButton (g, "mute", false, false, false, canvas.getBounds().toFloat()));
    }
    CHECK (canvas.getPixelAt (30, 10).getRed() < 150);   // dark grey (off), not red
    {
        juce::Graphics g (canvas);
        REQUIRE (themepack::drawButton (g, "mute", true, false, false, canvas.getBounds().toFloat()));
    }
    CHECK (canvas.getPixelAt (30, 10).getRed() > 200);

    // Nothing for what it doesn't have
    {
        juce::Graphics g (canvas);
        CHECK (! themepack::drawButton (g, "recarm", false, false, false, canvas.getBounds().toFloat()));
    }

    themepack::useForTesting ({});
    CHECK (! themepack::active());
}

TEST_CASE ("A REAPER theme is copied in and listed", "[theme]")
{
    juce::TemporaryFile temp;
    temp.getFile().createDirectory();
    const auto source = makeTheme (temp.getFile());

    juce::String error;
    const auto installedFolder = themepack::install (source, error);
    REQUIRE (error.isEmpty());
    REQUIRE (installedFolder.isDirectory());
    CHECK (installedFolder != source);
    CHECK (themepack::installed().contains (installedFolder));

    themepack::useForTesting (installedFolder);
    CHECK (themepack::piece ({ "transport_play" }).isValid());
    themepack::useForTesting ({});
    installedFolder.deleteRecursively();

    // Something that isn't a theme
    const auto notATheme = temp.getFile().getChildFile ("notes.txt");
    notATheme.replaceWithText ("hello");
    CHECK (themepack::install (notATheme, error) == juce::File());
    CHECK (error.isNotEmpty());
}

TEST_CASE ("The fader choice and the REAPER theme are kept with the theme settings", "[theme]")
{
    theme::Settings s;
    s.skin = theme::Skin::reaper;
    s.themePack = "/Users/me/Library/Application Support/Audio Tech Pro/Themes/My Theme, v2";
    s.faderStyle = 2;
    const auto back = theme::Settings::fromString (s.toString());
    CHECK (back.skin == theme::Skin::reaper);
    CHECK (back.themePack == s.themePack);
    CHECK (back.faderStyle == 2);

    // Older settings (without them): the skin's own faders
    const auto plain = theme::Settings::fromString (theme::Settings().toString());
    CHECK (plain.faderStyle == -1);
    CHECK (plain.themePack.isEmpty());
}

TEST_CASE ("Edit window colours are kept with the theme and used", "[theme]")
{
    theme::Settings s;
    s.editBack = 0xff102030;
    s.gridBarColour = 0xff405060;
    s.gridBeatColour = 0xff203040;
    s.rollBack = 0xff303030;
    const auto back = theme::Settings::fromString (s.toString());
    CHECK (back.editBack == s.editBack);
    CHECK (back.gridBarColour == s.gridBarColour);
    CHECK (back.gridBeatColour == s.gridBeatColour);
    CHECK (back.rollBack == s.rollBack);

    theme::set (s);
    CHECK (theme::laneA == juce::Colour (0xff102030));
    CHECK (theme::gridBar == juce::Colour (0xff405060));
    CHECK (theme::rollWhite == juce::Colour (0xff303030));
    theme::set ({});
}

TEST_CASE ("Faders, knobs, keys and the meter are chosen apart from the skin and kept", "[theme]")
{
    theme::Settings s;
    s.skin = theme::Skin::xenon;
    s.faderStyle = 5;
    s.knobStyle = 2;
    s.buttonStyle = 3;
    s.meterStyle = 1;
    const auto back = theme::Settings::fromString (s.toString());
    CHECK (back.faderStyle == 5);
    CHECK (back.knobStyle == 2);
    CHECK (back.buttonStyle == 3);
    CHECK (back.meterStyle == 1);

    theme::set (back);
    CHECK (theme::faderStyle() == 5);
    CHECK (theme::knobStyle() == 2);
    CHECK (theme::meterStyle() == 1);

    // Older settings: the skin's own
    const auto plain = theme::Settings::fromString (theme::Settings().toString());
    CHECK (plain.knobStyle == -1);
    CHECK (plain.buttonStyle == -1);
    CHECK (plain.meterStyle == -1);
    theme::set ({});
}

TEST_CASE ("The editor can have a theme of its own", "[theme]")
{
    theme::Settings editor;
    editor.skin = theme::Skin::white;
    theme::Settings s;
    s.skin = theme::Skin::xenon;
    s.editorTheme = editor.toString();

    const auto back = theme::Settings::fromString (s.toString());
    REQUIRE (back.editorTheme.isNotEmpty());
    CHECK (theme::Settings::fromString (back.editorTheme).skin == theme::Skin::white);

    // The tracks area takes the editor's colours, the mixer the main skin's
    theme::set (editor);
    const auto whiteLanes = theme::laneA;
    theme::set (theme::Settings { theme::Skin::xenon });
    const auto xenonLanes = theme::laneA, xenonStrip = theme::stripFace;
    REQUIRE (whiteLanes != xenonLanes);

    theme::set (back);
    CHECK (theme::laneA == whiteLanes);
    CHECK (theme::stripFace == xenonStrip);
    CHECK (theme::xenonSkin());
    CHECK_FALSE (theme::glassRegions());   // (the editor's: Studio White's regions are filled)
    theme::set ({});
}
