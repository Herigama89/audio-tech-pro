#pragma once

#include <JuceHeader.h>

namespace riff::icons
{

/** Track icons, drawn as simple line pictures (Logic shows one in each track header). */
inline const juce::StringArray& names()
{
    static const juce::StringArray list { "None", "Microphone", "Guitar", "Bass", "Drums", "Keys", "Synth", "Strings", "Speaker", "Waveform" };
    return list;
}

inline void draw (juce::Graphics& g, int index, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour);
    const auto c = r.getCentre();
    const auto w = r.getWidth(), h = r.getHeight();
    const juce::PathStrokeType stroke (1.4f);
    juce::Path p;

    switch (index)
    {
        case 1:   // microphone
            p.addRoundedRectangle (c.x - w * 0.16f, r.getY(), w * 0.32f, h * 0.55f, w * 0.16f);
            p.startNewSubPath (c.x - w * 0.3f, r.getY() + h * 0.4f);
            p.quadraticTo (c.x, r.getY() + h * 0.85f, c.x + w * 0.3f, r.getY() + h * 0.4f);
            p.startNewSubPath (c.x, r.getY() + h * 0.72f);
            p.lineTo (c.x, r.getBottom());
            g.strokePath (p, stroke);
            return;
        case 2:   // guitar: body and neck
        case 3:   // bass: a longer neck
        {
            const auto neck = index == 3 ? 0.62f : 0.5f;
            p.addEllipse (r.getX() + w * 0.05f, r.getBottom() - h * 0.45f, w * 0.45f, h * 0.45f);
            p.startNewSubPath (r.getX() + w * 0.4f, r.getBottom() - h * 0.35f);
            p.lineTo (r.getX() + w * (0.4f + neck * 0.9f), r.getBottom() - h * (0.35f + neck));
            g.strokePath (p, stroke);
            return;
        }
        case 4:   // drum
            p.addEllipse (r.getX() + w * 0.1f, r.getY() + h * 0.25f, w * 0.8f, h * 0.25f);
            p.startNewSubPath (r.getX() + w * 0.1f, r.getY() + h * 0.37f);
            p.lineTo (r.getX() + w * 0.1f, r.getY() + h * 0.8f);
            p.quadraticTo (c.x, r.getBottom() + h * 0.05f, r.getRight() - w * 0.1f, r.getY() + h * 0.8f);
            p.lineTo (r.getRight() - w * 0.1f, r.getY() + h * 0.37f);
            g.strokePath (p, stroke);
            return;
        case 5:   // keys
            g.drawRect (r.reduced (w * 0.05f, h * 0.2f), 1.4f);
            for (int i = 1; i < 4; ++i)
                g.fillRect (r.getX() + w * (0.05f + 0.225f * (float) i), r.getY() + h * 0.2f, 1.2f, h * 0.6f);
            return;
        case 6:   // synth: a knob and a slider
            p.addEllipse (r.getX() + w * 0.08f, r.getY() + h * 0.25f, w * 0.4f, w * 0.4f);
            p.startNewSubPath (r.getRight() - w * 0.25f, r.getY() + h * 0.1f);
            p.lineTo (r.getRight() - w * 0.25f, r.getBottom() - h * 0.1f);
            g.strokePath (p, stroke);
            g.fillRect (r.getRight() - w * 0.36f, c.y - 2.0f, w * 0.22f, 4.0f);
            return;
        case 7:   // strings: a violin outline
            p.addEllipse (c.x - w * 0.22f, r.getY() + h * 0.45f, w * 0.44f, h * 0.5f);
            p.addEllipse (c.x - w * 0.16f, r.getY() + h * 0.15f, w * 0.32f, h * 0.36f);
            p.startNewSubPath (c.x, r.getY());
            p.lineTo (c.x, r.getY() + h * 0.2f);
            g.strokePath (p, stroke);
            return;
        case 8:   // speaker
            p.startNewSubPath (r.getX() + w * 0.1f, c.y - h * 0.15f);
            p.lineTo (r.getX() + w * 0.3f, c.y - h * 0.15f);
            p.lineTo (r.getX() + w * 0.55f, r.getY() + h * 0.1f);
            p.lineTo (r.getX() + w * 0.55f, r.getBottom() - h * 0.1f);
            p.lineTo (r.getX() + w * 0.3f, c.y + h * 0.15f);
            p.lineTo (r.getX() + w * 0.1f, c.y + h * 0.15f);
            p.closeSubPath();
            p.startNewSubPath (r.getX() + w * 0.7f, c.y - h * 0.25f);
            p.quadraticTo (r.getRight(), c.y, r.getX() + w * 0.7f, c.y + h * 0.25f);
            g.strokePath (p, stroke);
            return;
        case 9:   // waveform
            p.startNewSubPath (r.getX(), c.y);
            for (int i = 1; i <= 12; ++i)
                p.lineTo (r.getX() + w * (float) i / 12.0f, c.y + (i % 2 == 0 ? -1.0f : 1.0f) * h * 0.4f * (float) std::sin (i * 0.3f + 0.5f));
            g.strokePath (p, stroke);
            return;
        default:
            return;
    }
}

} // namespace riff::icons
