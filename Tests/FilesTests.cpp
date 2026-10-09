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
    CHECK (model::files::autosaveFileFor (project, "Song", "abc").getFullPathName() == "/Users/me/Music/Song/Autosave/Song.atp");
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
