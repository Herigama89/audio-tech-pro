#include <JuceHeader.h>

// Proof of concept: can we paint components offscreen without a display?
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Slider s (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    s.setSize (100, 100);
    s.setValue (5.0);
    auto img = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
    juce::File out ("/tmp/claude-0/-home-claude/ca4db44e-0c79-57ef-b3ad-cb0a2da93e21/scratchpad/poc.png");
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (img, stream);
    std::cout << "wrote " << img.getWidth() << "x" << img.getHeight() << std::endl;
    return 0;
}
