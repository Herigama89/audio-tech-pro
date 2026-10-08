#pragma once

#include <JuceHeader.h>
#include <map>

namespace riff
{

/** One AudioThumbnail per audio file, shared by every region that uses it. */
class WaveformCache final : private juce::ChangeListener
{
public:
    explicit WaveformCache (juce::AudioFormatManager& formats) : formatManager (formats) {}

    ~WaveformCache() override
    {
        for (auto& [path, thumb] : thumbnails)
            thumb->removeChangeListener (this);
    }

    juce::AudioThumbnail& get (const juce::File& file)
    {
        const auto path = file.getFullPathName();

        if (auto it = thumbnails.find (path); it != thumbnails.end())
            return *it->second;

        auto thumb = std::make_unique<juce::AudioThumbnail> (256, formatManager, cache);
        thumb->setSource (new juce::FileInputSource (file));
        thumb->addChangeListener (this);
        return *(thumbnails[path] = std::move (thumb));
    }

    /** Called (on the message thread) as thumbnails finish loading in the background. */
    std::function<void()> onChange;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (onChange)
            onChange();
    }

    juce::AudioFormatManager& formatManager;
    juce::AudioThumbnailCache cache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
};

} // namespace riff
