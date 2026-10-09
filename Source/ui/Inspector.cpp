#include "Inspector.h"
#include "Theme.h"
#include "../model/Project.h"
#include "RoutingMenu.h"
#include "../engine/AutomationWriter.h"

namespace riff
{

Inspector::Inspector (MixerPanel& m)
    : mixer (m)
{
    for (auto* box : { &regionBox, &trackBox })
    {
        box->onLayoutChanged = [this] { resized(); };
        holder.addChildComponent (box);
    }

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

    buildTrackBox();
    resized();
    repaint();
}

void Inspector::buildTrackBox()
{
    trackBox.setVisible (track.isValid());
    if (! track.isValid())
        return;

    auto& um = mixer.getUndoManager();
    auto t = track;
    auto p = project;
    trackBox.reset ("Track: " + track[ids::name].toString(), track);

    if (! model::isVcaTrack (track))
        trackBox.addMenu ("Automation", [t] { const auto m = AutomationWriter::modeOf (t); return m.substring (0, 1).toUpperCase() + m.substring (1); },
                          [t] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            for (auto m : { "off", "read", "touch", "latch", "write" })
                menu.addItem (juce::String (m).substring (0, 1).toUpperCase() + juce::String (m).substring (1), true,
                              AutomationWriter::modeOf (t) == m, [t, value = juce::String (m)]() mutable { t.setProperty (ids::automationMode, value, nullptr); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });

    trackBox.addMenu ("Routing", [t, p]
    {
        juce::String text = "Main";
        if (const auto out = model::findById (p, t[ids::output].toString()); out.isValid())
            text = out[ids::name].toString();
        if (const int g = t.getProperty (ids::group, 0); g > 0)
            text << ", G" << g;
        return text;
    }, [t, p, &um] (juce::Component& target)
    {
        juce::PopupMenu menu;
        routing::addRoutingItems (menu, p, t, um);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    });

    if (model::isInstrumentTrack (track))
    {
        trackBox.addNumber ("Transpose", ids::transpose, -48.0, 48.0, 1.0, 0.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); });
        trackBox.addNumber ("Velocity", ids::velocityOffset, -64.0, 64.0, 1.0, 0.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); });
    }

    if (! model::isBusTrack (track) && ! model::isVcaTrack (track))
        trackBox.addSwitch ("Freeze", ids::frozen, false, [t] (bool) { if (routing::toggleFreeze) routing::toggleFreeze (t); });

    trackBox.resized();
}

void Inspector::setRegion (juce::ValueTree newRegion)
{
    if (newRegion == region)
        return;
    region = newRegion;
    regionBox.setVisible (region.isValid());

    if (region.isValid())
    {
        auto& um = mixer.getUndoManager();
        auto r = region;
        regionBox.reset ("Region: " + region[ids::name].toString(), region);
        regionBox.addSwitch ("Mute", ids::mute, false);
        regionBox.addSwitch ("Loop", ids::loop, false, [r, &um] (bool on) mutable
        {
            um.beginNewTransaction ("Loop region");
            model::setLoop (r, on, &um);
        });

        if (model::isMidiRegion (region))
        {
            regionBox.addNumber ("Transpose", ids::transpose, -48.0, 48.0, 1.0, 0.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); });
            regionBox.addNumber ("Velocity", ids::velocityOffset, -64.0, 64.0, 1.0, 0.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); });
        }
        else
        {
            regionBox.addNumber ("Gain", ids::gain, -30.0, 30.0, 0.1, 0.0,
                                 [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; },
                                 [] (double db) { return juce::Decibels::decibelsToGain (db, -96.0); },
                                 [] (double gain) { return juce::Decibels::gainToDecibels (gain, -30.0); });
            const auto ms = [] (double v) { return v <= 0.0 ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " ms"; };
            regionBox.addNumber ("Fade In", ids::fadeIn, 0.0, 10000.0, 1.0, 0.0, ms,
                                 [] (double v) { return v / 1000.0; }, [] (double v) { return v * 1000.0; });
            regionBox.addNumber ("Fade Out", ids::fadeOut, 0.0, 10000.0, 1.0, 0.0, ms,
                                 [] (double v) { return v / 1000.0; }, [] (double v) { return v * 1000.0; });
        }
        regionBox.resized();
    }
    resized();
}

int Inspector::boxesHeight() const
{
    int h = 0;
    for (const auto* box : { &regionBox, &trackBox })
        if (box->isVisible())
            h += box->preferredHeight() + 4;
    return h;
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
    g.setColour (theme::text);
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

    const auto boxes = boxesHeight();
    const auto height = juce::jmax (area.getHeight(), ChannelStrip::minimumHeight + boxes);
    holder.setSize (area.getWidth() - (height > area.getHeight() ? viewport.getScrollBarThickness() : 0), height);

    // The Region and Track boxes, then the strips.
    auto all = holder.getLocalBounds();
    for (auto* box : { &regionBox, &trackBox })
        if (box->isVisible())
        {
            box->setBounds (all.removeFromTop (box->preferredHeight()).reduced (2, 0));
            all.removeFromTop (4);
        }

    auto strips = all.reduced (4, 0);
    if (trackStrip != nullptr)
        trackStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
    strips.removeFromLeft (4);
    if (masterStrip != nullptr)
        masterStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
}

} // namespace riff
