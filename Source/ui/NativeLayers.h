#pragma once

#include <JuceHeader.h>
#include <vector>

/**
    Logic's way of moving things on screen: the parts that move every frame - the meters' bars, the playhead line -
    are their own Core Animation layers above the window, which the graphics card moves and composites on its own.
    Nothing under them is redrawn when they move (JUCE draws a window as one picture: a meter rising meant drawing
    that part of the picture again, the strip and fader under it, and sending it to the screen).

    macOS only; elsewhere (and when switched off) available() is false and everything here does nothing, so the
    callers keep drawing those parts themselves.

    A "group" is a clipping rectangle in a component's window (the part of a meter that's in view); its children
    are solid or graded rectangles inside it. Positions are in the window's top-level component's coordinates.
*/
namespace riff::layers
{
    using Id = int;

    /** Layers can be used here (macOS) and are switched on (View > Smooth Meters & Playhead). */
    bool available();
    void setEnabled (bool on);

    /** A clipping group in `owner`'s window (0 when layers aren't available). */
    Id createGroup (juce::Component& owner);
    /** A rectangle in a group: solid, or (with colours) graded from bottom to top. */
    Id addChild (Id group);
    void destroy (Id id);   // a group with its children, or one child

    /** Where the group clips (top-level coordinates), and whether it shows at all. */
    void placeGroup (Id group, juce::Rectangle<float> clip, bool visible);
    /** A child's rectangle (top-level coordinates) and its colour. */
    void setChild (Id child, juce::Rectangle<float> area, juce::Colour colour);
    /** A child showing part of a picture: the picture spread over `fullArea` (top-level coordinates), only `area`
        of it in view (the lit part of a meter, cut from the whole lit column). */
    void setPictureChild (Id child, juce::Rectangle<float> fullArea, juce::Rectangle<float> area, const juce::Image& picture);

    /** The part of `c` that's on screen (inside every parent), in its top-level component's coordinates. */
    juce::Rectangle<float> visibleAreaOf (const juce::Component& c, juce::Rectangle<float> local);
    /** Something drawn after `c` in its window overlaps it (a panel, a dialog, a strip over it): layers would show
        through that, so the caller draws for itself while it's covered. */
    bool coveredByOthers (const juce::Component& c);

    /** A component's layers: a clipping group with `children` rectangles, in the component's own coordinates.
        update() each frame (it follows the component when it scrolls, moves to another window, or is covered);
        while it returns false, the component draws those parts itself. Unchanged rectangles cost nothing. */
    class Overlay
    {
    public:
        Overlay (juce::Component& owner, int children);
        ~Overlay();

        bool update (juce::Rectangle<float> clipLocal);
        void solid (int index, juce::Rectangle<float> local, juce::Colour colour);
        void picture (int index, juce::Rectangle<float> fullLocal, juce::Rectangle<float> areaLocal, const juce::Image& image);
        void hide();

    private:
        juce::Component& owner;
        juce::Component* top = nullptr;
        Id group = 0;
        std::vector<Id> kids;
        struct Last { juce::Rectangle<float> full, area; juce::Colour colour; juce::Image image; bool set = false; };
        std::vector<Last> last;
        juce::Rectangle<float> lastClip;
        bool shown = false;
        void release();
        juce::Rectangle<float> toTop (juce::Rectangle<float> r) const { return top->getLocalArea (&owner, r); }
        JUCE_DECLARE_NON_COPYABLE (Overlay)
    };

    /** A playhead as a layer: the white line over `clipLocal` at `x`, with the ruler's triangle on top. */
    class PlayheadLine
    {
    public:
        explicit PlayheadLine (juce::Component& owner) : overlay (owner, 2) {}
        /** False while it can't be a layer (the owner draws its playhead then). */
        bool place (juce::Rectangle<float> clipLocal, float x, bool withHead);
        void hide() { overlay.hide(); }

    private:
        Overlay overlay;
        juce::Image head;
    };
}
