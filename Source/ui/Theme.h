#pragma once

#include <JuceHeader.h>

namespace riff::theme
{
    inline const juce::Colour background { 0xff1c1d21 };
    inline const juce::Colour toolbar    { 0xff222429 };
    inline const juce::Colour panel      { 0xff26282e };
    inline const juce::Colour laneA      { 0xff202227 };
    inline const juce::Colour laneB      { 0xff24262c };
    inline const juce::Colour outline    { 0xff3a3d45 };
    inline const juce::Colour gridBar    { 0xff3a3d45 };
    inline const juce::Colour gridBeat   { 0xff2c2f36 };
    inline const juce::Colour text       { 0xffe6e7ea };
    inline const juce::Colour dimText    { 0xff8d919b };
    inline const juce::Colour accent     { 0xff4aa3ff };
    inline const juce::Colour selection  { 0xff2f3a4d };
    inline const juce::Colour mute       { 0xff4aa3ff };
    inline const juce::Colour solo       { 0xffffc043 };
    inline const juce::Colour danger     { 0xffff5a52 };
    inline const juce::Colour meterGreen { 0xff3ddc84 };
    inline const juce::Colour meterAmber { 0xffffc043 };

    void apply (juce::LookAndFeel& lf);
}
