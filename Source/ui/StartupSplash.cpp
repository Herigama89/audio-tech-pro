#include "StartupSplash.h"

namespace riff
{

StartupSplash::StartupSplash (juce::Image l, juce::AudioPluginFormatManager& f, juce::KnownPluginList& k,
                              juce::File crash, std::function<void()> finished)
    : logo (std::move (l)), formats (f), list (k), crashFile (std::move (crash)), onFinished (std::move (finished))
{
    const auto width = 720;
    const auto logoHeight = logo.isValid() ? width * logo.getHeight() / juce::jmax (1, logo.getWidth()) : 300;
    setSize (width, logoHeight + 46);
    setOpaque (true);
    started = juce::Time::getMillisecondCounterHiRes();
    found = list.getNumTypes();
    startTimer (15);
}

void StartupSplash::timerCallback()
{
    if (done)
        return;

    // One format's folders listed per tick (so the logo shows straight away)...
    if (! listed)
    {
        if (formatIndex < formats.getNumFormats())
        {
            auto* format = formats.getFormat (formatIndex++);
            if (format != nullptr && format->canScanForPlugins())
            {
                status = "Looking for " + format->getName() + " plugins...";
                scanners.push_back (std::make_unique<juce::PluginDirectoryScanner> (list, *format, format->getDefaultLocationsToSearch(),
                                                                                   true, crashFile, false));
            }
            repaint();
            return;
        }
        listed = true;
    }

    // ...then one plugin per tick: new or changed ones are opened and added; known ones are skipped quickly.
    while (! skip && current < scanners.size())
    {
        juce::String name;
        auto& scanner = *scanners[current];
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool more = scanner.scanNextFile (true, name);
        progress = scanner.getProgress();
        if (name.isNotEmpty())
            status = "Scanning: " + name;
        if (! more)
            ++current;
        if (juce::Time::getMillisecondCounterHiRes() - t0 > 20.0)
            break;   // that one took a while: show it
    }
    repaint();

    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    const bool scanned = skip || current >= scanners.size();
    if (scanned)
    {
        const auto added = list.getNumTypes() - found;
        status = added > 0 ? juce::String (added) + " new plugin" + (added == 1 ? "" : "s") + " found. Opening..."
                           : juce::String ("Plugins up to date. Opening...");
        progress = 1.0f;
    }
    if (scanned && (elapsed >= minimumSeconds || skip))
    {
        done = true;
        stopTimer();
        auto finished = onFinished;   // (this may be deleted by it)
        if (finished)
            finished();
    }
}

void StartupSplash::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111214));
    const auto logoArea = getLocalBounds().withTrimmedBottom (46);
    if (logo.isValid())
        g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::centred);

    auto bottom = getLocalBounds().removeFromBottom (46).reduced (18, 10);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    const auto bar = bottom.removeFromBottom (4).toFloat();
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (juce::Colour (0xff3fa7d6));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (status, bottom.withTrimmedBottom (3), juce::Justification::centredLeft, true);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawText ("click to skip", bottom.withTrimmedBottom (3), juce::Justification::centredRight, false);
}

} // namespace riff
