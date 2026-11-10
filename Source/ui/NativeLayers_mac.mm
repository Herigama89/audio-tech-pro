// Core Animation layers over JUCE windows (see NativeLayers.h). Manual retain/release (not compiled with ARC).
// (JUCE first: its OpenGL header must come before the system's; without its namespace opened, so the system's
// own Point and Rect don't clash with JUCE's)
#define DONT_SET_USING_JUCE_NAMESPACE 1
#include "NativeLayers.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cstring>
#include <map>
#include <memory>

/** The see-through view on top of a window that holds the layers: clicks go straight through it. */
@interface RiffLayerOverlayView : NSView
@end

@implementation RiffLayerOverlayView
- (NSView*) hitTest: (NSPoint) point { (void) point; return nil; }
- (BOOL) acceptsFirstResponder { return NO; }
@end

namespace riff::layers
{
namespace
{
    bool enabledFlag = true;

    struct Window
    {
        juce::Component::SafePointer<juce::Component> top;
        NSView* host = nil;                       // (not retained: the window's own view)
        RiffLayerOverlayView* overlay = nil;      // retained
        CALayer* root = nil;                      // retained
    };

    struct Layer
    {
        CALayer* layer = nil;           // retained: the group's clip, a solid child, or a graded child's clip
        CALayer* content = nil;         // retained (picture children: the whole picture, inside the clip)
        CGImageRef cgImage = nullptr;   // (its picture, made from `image`)
        juce::Image image;
        Id group = 0;                   // (children: their group)
        Window* window = nullptr;
        juce::Component::SafePointer<juce::Component> owner;   // (groups)

        void free()
        {
            if (content != nil) [content release];
            if (cgImage != nullptr) CGImageRelease (cgImage);
            [layer removeFromSuperlayer];
            [layer release];
        }
    };

    std::map<juce::Component*, std::unique_ptr<Window>> windows;
    std::map<Id, Layer> layers;
    Id nextId = 1;

    struct NoAnimation
    {
        NoAnimation()  { [CATransaction begin]; [CATransaction setDisableActions: YES]; }
        ~NoAnimation() { [CATransaction commit]; }
    };

    CGColorSpaceRef srgb()
    {
        static CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        return space;
    }

    CGColorRef cgColour (juce::Colour c)
    {
        const CGFloat parts[] { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha() };
        return CGColorCreate (srgb(), parts);
    }

    /** A JUCE picture as a Core Graphics one (copied: the layer keeps it). */
    CGImageRef cgImageOf (const juce::Image& source)
    {
        const auto image = source.convertedToFormat (juce::Image::ARGB);
        const juce::Image::BitmapData bits (image, juce::Image::BitmapData::readOnly);
        const auto w = (size_t) image.getWidth(), h = (size_t) image.getHeight(), stride = w * 4;
        auto* data = CFDataCreateMutable (nullptr, (CFIndex) (stride * h));
        CFDataSetLength (data, (CFIndex) (stride * h));
        auto* out = CFDataGetMutableBytePtr (data);
        for (size_t y = 0; y < h; ++y)
            std::memcpy (out + y * stride, bits.getLinePointer ((int) y), stride);
        auto* provider = CGDataProviderCreateWithCFData (data);
        CFRelease (data);
        // (JUCE's pixels: premultiplied BGRA in memory)
        auto* cg = CGImageCreate (w, h, 8, 32, stride, srgb(), (CGBitmapInfo) ((uint32_t) kCGBitmapByteOrder32Little | (uint32_t) kCGImageAlphaPremultipliedFirst),
                                  provider, nullptr, false, kCGRenderingIntentDefault);
        CGDataProviderRelease (provider);
        return cg;
    }

    /** Groups whose component has gone, or isn't on screen any more (its panel closed): hidden. */
    struct Watch final : juce::Timer
    {
        void timerCallback() override;
    };
    Watch& watch() { static auto* w = new Watch(); return *w; }   // (never deleted: no timer torn down after the app's message loop)

    Window* windowFor (juce::Component& owner)
    {
        auto* top = owner.getTopLevelComponent();
        auto* peer = top != nullptr ? top->getPeer() : nullptr;
        if (peer == nullptr)
            return nullptr;
        auto* view = (NSView*) peer->getNativeHandle();
        if (view == nil)
            return nullptr;

        auto& w = windows[top];
        if (w == nullptr)
        {
            w = std::make_unique<Window>();
            w->root = [[CALayer layer] retain];
        }
        w->top = top;
        if (w->host != view || w->overlay == nil)
        {
            // A new window view (or the first): the overlay put on top of it, the layers moved with it
            if (w->overlay != nil)
            {
                [w->overlay removeFromSuperview];
                [w->overlay release];
            }
            w->overlay = [[RiffLayerOverlayView alloc] initWithFrame: [view bounds]];
            [w->overlay setAutoresizingMask: NSViewWidthSizable | NSViewHeightSizable];
            [w->overlay setLayer: w->root];
            [w->overlay setWantsLayer: YES];
            [view addSubview: w->overlay positioned: NSWindowAbove relativeTo: nil];
            w->host = view;
            // (the way up: below)
        }
        else if ([[view subviews] lastObject] != w->overlay)
        {
            // (something else was added on top since - the graphics card's drawing view: back above it)
            [w->overlay retain];
            [w->overlay removeFromSuperview];
            [view addSubview: w->overlay positioned: NSWindowAbove relativeTo: nil];
            [w->overlay release];
        }
        // Top-left origin inside it, as the components. Kept so every frame: left as it was set up, the layers came
        // out upside down (the meters at the top of the window, the playhead in the mixer)
        if (! w->root.geometryFlipped)
            w->root.geometryFlipped = YES;
        if (! NSEqualRects ([w->overlay frame], [view bounds]))
            [w->overlay setFrame: [view bounds]];
        return w.get();
    }

    /** Component coordinates to the window view's points (they differ when the app's UI is scaled). */
    CGRect toView (const Window& w, juce::Rectangle<float> r)
    {
        auto scale = 1.0f;
        if (auto* top = w.top.getComponent(); top != nullptr && top->getWidth() > 0 && w.host != nil)
            scale = (float) [w.host bounds].size.width / (float) top->getWidth();
        return CGRectMake (r.getX() * scale, r.getY() * scale, r.getWidth() * scale, r.getHeight() * scale);
    }
}

bool available() { return enabledFlag; }
void setEnabled (bool on) { enabledFlag = on; }

Id createGroup (juce::Component& owner)
{
    if (! enabledFlag)
        return 0;
    auto* w = windowFor (owner);
    if (w == nullptr)
        return 0;
    const NoAnimation quiet;
    Layer l;
    l.layer = [[CALayer layer] retain];
    l.layer.masksToBounds = YES;
    l.layer.hidden = YES;
    l.window = w;
    l.owner = &owner;
    [w->root addSublayer: l.layer];
    const auto id = nextId++;
    layers[id] = l;
    if (! watch().isTimerRunning())
        watch().startTimerHz (30);
    return id;
}

void Watch::timerCallback()
{
    bool any = false;
    const NoAnimation quiet;
    for (auto& [id, l] : layers)
    {
        if (l.group != 0)
            continue;
        any = true;
        auto* o = l.owner.getComponent();
        if (! l.layer.hidden && (o == nullptr || ! o->isShowing() || o->getTopLevelComponent() != l.window->top.getComponent()))
            l.layer.hidden = YES;
    }
    if (! any)
        stopTimer();
}

Id addChild (Id group)
{
    auto g = layers.find (group);
    if (g == layers.end())
        return 0;
    const NoAnimation quiet;
    Layer l;
    l.layer = [[CALayer layer] retain];
    l.group = group;
    l.window = g->second.window;
    [g->second.layer addSublayer: l.layer];
    const auto id = nextId++;
    layers[id] = l;
    return id;
}

void destroy (Id id)
{
    auto it = layers.find (id);
    if (it == layers.end())
        return;
    const NoAnimation quiet;
    // (a group's children go with it)
    if (it->second.group == 0)
        for (auto c = layers.begin(); c != layers.end();)
        {
            if (c->second.group == id)
            {
                c->second.free();
                c = layers.erase (c);
            }
            else
                ++c;
        }
    it = layers.find (id);
    it->second.free();
    layers.erase (it);
}

void placeGroup (Id group, juce::Rectangle<float> clip, bool visible)
{
    auto g = layers.find (group);
    if (g == layers.end())
        return;
    auto& l = g->second;
    if (auto* top = l.window->top.getComponent())
        windowFor (*top);   // (keeps the overlay on top and sized)
    const NoAnimation quiet;
    const bool show = visible && ! clip.isEmpty();
    if (l.layer.hidden != ! show)
        l.layer.hidden = ! show;
    if (show)
    {
        const auto frame = toView (*l.window, clip);
        if (! CGRectEqualToRect (frame, l.layer.frame))
            l.layer.frame = frame;
    }
}

namespace
{
    /** A child's frame inside its group (the group's frame is where it clips, in the window). */
    CGRect inGroup (const Layer& child, juce::Rectangle<float> area)
    {
        const auto g = layers.find (child.group);
        auto frame = toView (*child.window, area);
        if (g != layers.end())
        {
            const auto origin = g->second.layer.frame.origin;
            frame.origin.x -= origin.x;
            frame.origin.y -= origin.y;
        }
        return frame;
    }
}

void setChild (Id child, juce::Rectangle<float> area, juce::Colour colour)
{
    auto c = layers.find (child);
    if (c == layers.end())
        return;
    const NoAnimation quiet;
    auto& l = c->second;
    l.layer.frame = inGroup (l, area);
    l.layer.hidden = area.isEmpty();
    if (l.content != nil)
        l.content.hidden = YES;   // (it showed a picture before)
    auto cg = cgColour (colour);
    l.layer.backgroundColor = cg;
    CGColorRelease (cg);
}

void setPictureChild (Id child, juce::Rectangle<float> fullArea, juce::Rectangle<float> area, const juce::Image& picture)
{
    auto c = layers.find (child);
    if (c == layers.end())
        return;
    const NoAnimation quiet;
    auto& l = c->second;
    if (l.content == nil)
    {
        l.content = [[CALayer layer] retain];
        l.layer.masksToBounds = YES;
        [l.layer addSublayer: l.content];
    }
    l.content.hidden = NO;
    if (l.layer.backgroundColor != nullptr)
        l.layer.backgroundColor = nullptr;
    if (! (l.image == picture))
    {
        l.image = picture;
        if (l.cgImage != nullptr)
            CGImageRelease (l.cgImage);
        l.cgImage = picture.isValid() ? cgImageOf (picture) : nullptr;
        l.content.contents = (id) l.cgImage;
    }
    l.layer.hidden = area.isEmpty();
    if (area.isEmpty())
        return;
    const auto lit = inGroup (l, area);
    l.layer.frame = lit;
    // the whole picture, placed so the part in view is the right part of it
    auto full = inGroup (l, fullArea);
    full.origin.x -= lit.origin.x;
    full.origin.y -= lit.origin.y;
    l.content.frame = full;
}
}
