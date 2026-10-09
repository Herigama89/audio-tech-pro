#include "Inspector.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

Inspector::Inspector (MixerPanel& m)
    : mixer (m)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

Inspector::~Inspector()
{
    clear();
}

void Inspector::clear()
{
    for (auto* s : { trackStrip.get(), masterStrip.get() })
        if (s != nullptr)
            mixer.removeExtraStrip (s);
    trackStrip.reset();
    masterStrip.reset();
}

void Inspector::setTrack (juce::ValueTree newProject, juce::ValueTree newTrack)
{
    if (newProject == project && newTrack == track && masterStrip != nullptr)
        return;

    clear();
    project = newProject;
    track = newTrack;

    if (project.isValid())
    {
        if (track.isValid())
        {
            trackStrip = std::make_unique<ChannelStrip> (mixer, project, track);
            holder.addAndMakeVisible (*trackStrip);
            mixer.addExtraStrip (trackStrip.get());
        }
        masterStrip = std::make_unique<ChannelStrip> (mixer, project, juce::ValueTree());
        holder.addAndMakeVisible (*masterStrip);
        mixer.addExtraStrip (masterStrip.get());
    }

    resized();
    repaint();
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2a2d));

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (theme::toolbarDark);
    g.fillRect (header);
    g.setColour (juce::Colours::black);
    g.fillRect (header.removeFromBottom (1));

    // Each strip's name above it.
    auto names = getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (4);
    const auto left = names.removeFromLeft (ChannelStrip::stripWidth).reduced (2, 0);
    names.removeFromLeft (4);
    const auto right = names.removeFromLeft (ChannelStrip::stripWidth).reduced (2, 0);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (theme::cream);
    g.drawFittedText (track.isValid() ? track[ids::name].toString() : juce::String ("No track"), left, juce::Justification::centred, 1, 0.7f);
    g.setColour (theme::dimText);
    g.drawFittedText ("Stereo Out", right, juce::Justification::centred, 1, 0.7f);

    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void Inspector::resized()
{
    auto area = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedRight (1);
    viewport.setBounds (area);

    const auto height = juce::jmax (area.getHeight(), ChannelStrip::minimumHeight);
    holder.setSize (area.getWidth() - (height > area.getHeight() ? viewport.getScrollBarThickness() : 0), height);

    auto strips = holder.getLocalBounds().reduced (4, 0);
    if (trackStrip != nullptr)
        trackStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
    strips.removeFromLeft (4);
    if (masterStrip != nullptr)
        masterStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
}

} // namespace riff
