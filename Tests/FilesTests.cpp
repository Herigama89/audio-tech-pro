#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "model/Project.h"
#include "model/ProjectFiles.h"

using namespace riff;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atp-files-" + juce::Uuid().toString());
        TempDir()  { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };
}

TEST_CASE ("Autosave places", "[files]")
{
    const auto project = juce::File ("/Users/me/Music/Song/Song.atp");
    CHECK (model::files::autosaveFileFor (project, "Song", "abc") == project.getParentDirectory().getChildFile ("Autosave").getChildFile ("Song.atp"));
    const auto untitled = model::files::autosaveFileFor ({}, "Untitled", "abc");
    CHECK (untitled.getParentDirectory() == model::files::untitledAutosaveFolder());
    CHECK (untitled.getFileName() == "Untitled abc.atp");
}

TEST_CASE ("Backups are made on save and only the newest are kept", "[files]")
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("Song.atp");
    CHECK_FALSE (model::files::makeBackup (file).existsAsFile());   // nothing saved yet

    for (int i = 0; i < 5; ++i)
    {
        file.replaceWithText ("version " + juce::String (i));
        file.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::minutes (10 - i));
        const auto backup = model::files::makeBackup (file, 3);
        REQUIRE (backup.existsAsFile());
        CHECK (backup.loadFileAsString() == "version " + juce::String (i));
    }

    const auto backups = model::files::backupsOf (file);
    CHECK (backups.size() == 3);
    CHECK (backups.getFirst().loadFileAsString() == "version 4");
}

TEST_CASE ("A newer autosave is found; an older one isn't", "[files]")
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("Song.atp");
    file.replaceWithText ("saved");
    CHECK_FALSE (model::files::newerAutosave (file).existsAsFile());

    auto autosave = model::files::autosaveFileFor (file, {}, {});
    autosave.getParentDirectory().createDirectory();
    autosave.replaceWithText ("autosaved");
    autosave.setLastModificationTime (file.getLastModificationTime() + juce::RelativeTime::minutes (2));
    CHECK (model::files::newerAutosave (file) == autosave);

    autosave.setLastModificationTime (file.getLastModificationTime() - juce::RelativeTime::minutes (2));
    CHECK_FALSE (model::files::newerAutosave (file).existsAsFile());
}

TEST_CASE ("Consolidate copies outside audio into the project folder", "[files]")
{
    TempDir temp;
    const auto outside = temp.dir.getChildFile ("elsewhere");
    outside.createDirectory();
    const auto a = outside.getChildFile ("kick.wav");
    a.replaceWithText ("audio");
    const auto projectFile = temp.dir.getChildFile ("Song").getChildFile ("Song.atp");
    projectFile.getParentDirectory().createDirectory();
    const auto inside = projectFile.getParentDirectory().getChildFile ("vox.wav");
    inside.replaceWithText ("audio2");

    juce::UndoManager um;
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    auto r1 = model::addRegion (track, a, 0.0, 1.0, nullptr);
    auto r2 = model::addRegion (track, a, 2.0, 1.0, nullptr);        // the same file twice: copied once
    auto r3 = model::addRegion (track, inside, 4.0, 1.0, nullptr);   // already inside
    model::addRegion (track, outside.getChildFile ("gone.wav"), 6.0, 1.0, nullptr);

    CHECK (model::files::audioFilesOf (project).size() == 3);

    juce::String error;
    um.beginNewTransaction();
    CHECK (model::files::consolidate (project, projectFile, &um, error) == 1);
    CHECK (error.contains ("gone.wav"));
    const auto copied = projectFile.getParentDirectory().getChildFile ("Audio Files").getChildFile ("kick.wav");
    CHECK (copied.existsAsFile());
    CHECK (r1[ids::file].toString() == copied.getFullPathName());
    CHECK (r2[ids::file].toString() == copied.getFullPathName());
    CHECK (r3[ids::file].toString() == inside.getFullPathName());

    um.undo();
    CHECK (r1[ids::file].toString() == a.getFullPathName());
}

TEST_CASE ("Missing audio is found by name: case, accents, extension, a stereo pair's halves", "[files]")
{
    TempDir temp;
    const auto audio = temp.dir.getChildFile ("Session").getChildFile ("Audio Files");
    audio.createDirectory();
    for (const auto* name : { "Kick_01.wav", "VOZ Principal.aif", "Guitar.L.wav", "Guitar.R.wav", "Bajo-Final.wav" })
        audio.getChildFile (name).replaceWithText ("x");

    model::files::MediaIndex index;
    index.add (temp.dir);
    CHECK (index.find ("kick_01.WAV").getFileName() == "Kick_01.wav");
    CHECK (index.find ("/Volumes/Old Drive/Session/Audio Files/Voz Principal.wav").getFileName() == "VOZ Principal.aif");   // another extension
    CHECK (index.find ("Guitar.wav").getFileName().startsWith ("Guitar."));                                                // a stereo pair
    CHECK (index.find (juce::String::fromUTF8 ("Bajo Final.wav")).getFileName() == "Bajo-Final.wav");                        // punctuation
    CHECK_FALSE (index.find ("Nothing.wav").existsAsFile());

    // A project pointing at where they were: pointed at where they are
    auto project = model::createProject();
    auto track = model::addTrack (project, "Drums", nullptr);
    model::addRegion (track, juce::File ("/Users/someone/Old/Kick_01.wav"), 0.0, 1.0, nullptr);
    model::addRegion (track, juce::File ("/Users/someone/Old/Snare.wav"), 1.0, 1.0, nullptr);
    CHECK (model::files::missingFilesOf (project).size() == 2);
    CHECK (model::files::relinkMissing (project, index, nullptr) == 1);
    CHECK (model::files::missingFilesOf (project).size() == 1);
    CHECK (track.getChild (0)[ids::file].toString() == audio.getChildFile ("Kick_01.wav").getFullPathName());
}

TEST_CASE ("Save a Copy: the project and every audio file it uses, in one folder that opens anywhere", "[files]")
{
    TempDir temp;
    const auto elsewhere = temp.dir.getChildFile ("Samples");
    elsewhere.createDirectory();
    const auto a = elsewhere.getChildFile ("a.wav"), b = temp.dir.getChildFile ("Desktop").getChildFile ("a.wav");
    a.replaceWithText ("first");
    b.getParentDirectory().createDirectory();
    b.replaceWithText ("second, same name");

    auto project = model::createProject ("Song");
    auto track = model::addTrack (project, "T", nullptr);
    model::addRegion (track, a, 0.0, 1.0, nullptr);
    model::addRegion (track, b, 1.0, 1.0, nullptr);
    model::addRegion (track, temp.dir.getChildFile ("gone.wav"), 2.0, 1.0, nullptr);

    juce::String error;
    const auto copy = model::files::saveCopyWithMedia (project, temp.dir.getChildFile ("Out"), "Song Copy", {}, error);
    REQUIRE (copy.existsAsFile());
    CHECK (error.isEmpty());
    const auto folder = copy.getParentDirectory();
    CHECK (folder.getFileName() == "Song Copy");
    CHECK (folder.getChildFile ("Audio Files").getNumberOfChildFiles (juce::File::findFiles) == 2);   // both, though named alike
    CHECK (folder.getChildFile ("Missing files.txt").loadFileAsString().contains ("gone.wav"));

    // Moved somewhere else entirely (another computer), it still finds its audio
    const auto moved = temp.dir.getChildFile ("Friend").getChildFile ("Song Copy");
    moved.getParentDirectory().createDirectory();
    REQUIRE (folder.moveFileTo (moved));
    auto opened = model::load (moved.getChildFile (copy.getFileName()), error);
    REQUIRE (opened.isValid());
    const auto first = juce::File (opened.getChild (0).getChild (0)[ids::file].toString());
    const auto second = juce::File (opened.getChild (0).getChild (1)[ids::file].toString());
    CHECK (first.loadFileAsString() == "first");
    CHECK (second.loadFileAsString() == "second, same name");
    CHECK (first.isAChildOf (moved));

    // and zipped to send
    const auto zip = temp.dir.getChildFile ("Song Copy.zip");
    CHECK (model::files::zipFolder (moved, zip, {}, error));
    juce::ZipFile z (zip);
    CHECK (z.getNumEntries() >= 3);
}

TEST_CASE ("Timed backups hold the project as it is now", "[files]")
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("Song.atp");
    auto project = model::createProject ("Song");
    juce::String error;
    REQUIRE (model::save (project, file, error));
    model::addTrack (project, "New since saving", nullptr);
    const auto backup = model::files::backupNow (project, file, 5);
    REQUIRE (backup.existsAsFile());
    auto restored = model::load (backup, error);
    REQUIRE (restored.isValid());
    CHECK (model::numTracks (restored) == 1);
    CHECK (model::files::backupsOf (file).contains (backup));
}
