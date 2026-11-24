#pragma once

#include <JuceHeader.h>
#include "GlassDialogLook.h"

namespace riff
{
/**
    The New Project window (Pro Tools' Dashboard): shown when the app opens (unless switched off) and by File > New.
    Create: start from an empty project, a ready-made start (StarterTemplates) or one of your templates, and set the
    project's name, folder, sample rate, bit depth, file type, audio interface, tempo, key, time signature and
    count-in. Recent: your recent projects with their rate, bit depth, tracks, tempo and key.
*/
class ProjectDashboard final : public juce::Component
{
public:
    struct Settings
    {
        juce::String starter = "empty";      // a starter's id, or empty when `templateFile` is set
        juce::File templateFile;
        juce::String name = "Untitled";
        juce::File folder;                   // where the project's folder is made
        double sampleRate = 48000.0;
        int bitDepth = 32;                   // 16, 24, 32 (float), 64 (float)
        juce::String fileType = "wav";       // "wav", "aiff", "flac"
        juce::String device;                 // the audio interface (empty: keep the one in use)
        double tempo = 120.0;
        int keyRoot = 0;                     // 0 = C
        bool keyMinor = false;
        int timeSig = 4;                     // beats a bar (in quarter notes)
        int countInBars = 0;                 // 0 off, 1 or 2
    };

    struct Callbacks
    {
        std::function<void (const Settings&)> create;
        std::function<void (const juce::File&)> openRecent;
        std::function<void()> openOther, cancel;
        std::function<void (bool)> setShowAtStartup;
    };

    ProjectDashboard (Settings defaults, juce::StringArray devices, juce::Array<juce::File> recent,
                      juce::File templatesFolder, bool showAtStartup, Callbacks callbacks);
    ~ProjectDashboard() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shows the Recent tab (true) or Create (false). */
    void showRecent (bool recent);

    static juce::StringArray keyNames();   // "C Major", "C Minor", "C# Major", ...

private:
    class Card;
    class RecentRow;
    class Segmented;

    void choose (Card* card);
    void layoutCards();
    void chooseFolder();
    void tap();
    void createNow();
    void updateFolderButton();
    void updateFileTypeLimits();

    GlassDialogLook look;   // (first: it outlives the controls)
    Settings settings;
    Callbacks callbacks;
    juce::File templatesFolder;
    bool recentTab = false;

    juce::TextButton createTab { "Create" }, recentTabButton { "Recent" };
    juce::OwnedArray<juce::TextButton> sectionButtons;
    juce::StringArray sections;
    juce::Viewport cardView, recentView;
    struct PaintedHolder final : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };
    PaintedHolder cardHolder;
    juce::Component recentHolder;
    juce::OwnedArray<Card> cards;
    juce::OwnedArray<RecentRow> recentRows;
    std::vector<std::pair<juce::String, int>> sectionTops;
    Card* chosen = nullptr;

    juce::TextEditor nameBox, tempoBox;
    juce::TextButton folderButton, tapButton { "TAP" };
    juce::ComboBox rateBox, bitsBox, deviceBox, keyBox, sigBox, countInBox;
    std::unique_ptr<Segmented> fileType;
    juce::ToggleButton showAtStartupToggle { "Show this window when the app opens" };
    juce::TextButton openButton { "Open Project..." }, cancelButton { "Cancel" }, createButton { "Create" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<double> taps;
    juce::Label note;
    std::vector<std::pair<juce::String, juce::Component*>> labels;   // (drawn left of their controls)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectDashboard)
};
}
