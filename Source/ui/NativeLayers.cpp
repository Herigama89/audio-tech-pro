// Where Core Animation isn't there (Windows, Linux): layers are never available, so callers draw for themselves.
#include "NativeLayers.h"

namespace riff::layers
{
#if ! JUCE_MAC
bool available() { return false; }
void setEnabled (bool) {}
Id createGroup (juce::Component&) { return 0; }
Id addChild (Id) { return 0; }
void destroy (Id) {}
void placeGroup (Id, juce::Rectangle<float>, bool) {}
void setChild (Id, juce::Rectangle<float>, juce::Colour) {}
void setPictureChild (Id, juce::Rectangle<float>, juce::Rectangle<float>, const juce::Image&) {}
#endif

juce::Rectangle<float> visibleAreaOf (const juce::Component& c, juce::Rectangle<float> local)
{
    auto* top = c.getTopLevelComponent();
    auto area = top->getLocalArea (&c, local);
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        area = area.getIntersection (top->getLocalArea (p, p->getLocalBounds().toFloat()));
    return area;
}

bool coveredByOthers (const juce::Component& c)
{
    auto* top = c.getTopLevelComponent();
    // (a pixel's overlap at the edges doesn't count: neighbours placed at scaled, rounded positions touch)
    const auto mine = top->getLocalArea (&c, c.getLocalBounds().toFloat()).reduced (1.5f);
    const juce::Component* child = &c;
    for (auto* p = c.getParentComponent(); p != nullptr; child = p, p = p->getParentComponent())
    {
        const auto index = p->getIndexOfChildComponent (child);
        const auto n = p->getNumChildComponents();
        for (int i = index + 1; i < n; ++i)
        {
            auto* other = p->getChildComponent (i);
            if (other->isVisible() && ! other->getBounds().isEmpty()
                && top->getLocalArea (p, other->getBoundsInParent()).toFloat().intersects (mine))
                return true;
        }
    }
    return false;
}

Overlay::Overlay (juce::Component& o, int children) : owner (o), last ((size_t) children) {}
Overlay::~Overlay() { release(); }

void Overlay::release()
{
    if (group != 0)
        destroy (group);
    group = 0;
    kids.clear();
    top = nullptr;
    shown = false;
}

bool Overlay::update (juce::Rectangle<float> clipLocal)
{
    if (! available())
    {
        release();
        return false;
    }
    if (! owner.isShowing() || coveredByOthers (owner))
    {
        hide();
        return false;
    }
    auto* nowTop = owner.getTopLevelComponent();
    if (nowTop != top || group == 0)
    {
        // (first time, or the component is in another window now)
        release();
        top = nowTop;
        group = createGroup (owner);
        if (group == 0)
            return false;
        for (size_t i = 0; i < last.size(); ++i)
            kids.push_back (addChild (group));
        for (auto& l : last)
            l.set = false;
    }
    const auto clip = visibleAreaOf (owner, clipLocal);
    if (! shown || clip != lastClip)
    {
        // (the children are placed inside the group: moved with it)
        for (auto& l : last)
            l.set = false;
        lastClip = clip;
    }
    placeGroup (group, clip, true);
    shown = true;
    return true;
}

void Overlay::solid (int index, juce::Rectangle<float> local, juce::Colour colour)
{
    if (group == 0 || ! juce::isPositiveAndBelow (index, (int) kids.size()))
        return;
    auto& l = last[(size_t) index];
    if (l.set && l.area == local && l.colour == colour && ! l.image.isValid())
        return;
    l = { {}, local, colour, {}, true };
    setChild (kids[(size_t) index], local.isEmpty() ? juce::Rectangle<float>() : toTop (local), colour);
}

void Overlay::picture (int index, juce::Rectangle<float> fullLocal, juce::Rectangle<float> areaLocal, const juce::Image& image)
{
    if (group == 0 || ! juce::isPositiveAndBelow (index, (int) kids.size()))
        return;
    auto& l = last[(size_t) index];
    if (l.set && l.full == fullLocal && l.area == areaLocal && l.image == image)
        return;
    l = { fullLocal, areaLocal, {}, image, true };
    setPictureChild (kids[(size_t) index], toTop (fullLocal), areaLocal.isEmpty() ? juce::Rectangle<float>() : toTop (areaLocal), image);
}

void Overlay::hide()
{
    if (group != 0 && shown)
        placeGroup (group, {}, false);
    shown = false;
}

bool PlayheadLine::place (juce::Rectangle<float> clipLocal, float x, bool withHead)
{
    if (! overlay.update (clipLocal))
        return false;
    overlay.solid (0, juce::Rectangle<float> (x - 0.5f, clipLocal.getY(), 1.5f, clipLocal.getHeight()), juce::Colours::white);
    if (withHead)
    {
        if (! head.isValid())
        {
            // (drawn at 4x: sharp at any screen scale)
            head = juce::Image (juce::Image::ARGB, 48, 36, true, juce::SoftwareImageType());
            juce::Graphics g (head);
            juce::Path p;
            p.addTriangle (0.0f, 0.0f, 48.0f, 0.0f, 24.0f, 36.0f);
            g.setColour (juce::Colours::white);
            g.fillPath (p);
        }
        const juce::Rectangle<float> area (x - 6.0f, clipLocal.getY(), 12.0f, 9.0f);
        overlay.picture (1, area, area, head);
    }
    else
        overlay.picture (1, {}, {}, head);
    return true;
}
}
