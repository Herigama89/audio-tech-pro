#pragma once

#include <JuceHeader.h>

/** Small helpers for reading other programs' XML. */
namespace riff::interchange::xml
{
    /** The child at a path of tag names ("DeviceChain/Mixer/Volume"), or null. */
    inline const juce::XmlElement* at (const juce::XmlElement* e, juce::StringRef path)
    {
        for (const auto& part : juce::StringArray::fromTokens (path, "/", {}))
        {
            if (e == nullptr)
                return nullptr;
            e = e->getChildByName (part);
        }
        return e;
    }

    /** The "Value" attribute at a path (Ableton keeps everything as <Name Value="..."/>). */
    inline juce::String value (const juce::XmlElement* e, juce::StringRef path, const juce::String& fallback = {})
    {
        if (const auto* found = at (e, path))
            return found->getStringAttribute ("Value", fallback);
        return fallback;
    }

    inline double number (const juce::XmlElement* e, juce::StringRef path, double fallback = 0.0)
    {
        const auto v = value (e, path);
        return v.isNotEmpty() ? v.getDoubleValue() : fallback;
    }

    inline bool flag (const juce::XmlElement* e, juce::StringRef path, bool fallback = false)
    {
        const auto v = value (e, path);
        return v.isNotEmpty() ? v.equalsIgnoreCase ("true") || v == "1" : fallback;
    }

    /** The first element with this tag at or under `e` (depth first). */
    inline const juce::XmlElement* find (const juce::XmlElement* e, juce::StringRef tag)
    {
        if (e == nullptr)
            return nullptr;
        if (e->hasTagName (tag))
            return e;
        for (auto* c : e->getChildIterator())
            if (const auto* found = find (c, tag))
                return found;
        return nullptr;
    }

    /** Every element with this tag at or under `e` (not looking inside the ones found). */
    inline void findAll (const juce::XmlElement* e, juce::StringRef tag, juce::Array<const juce::XmlElement*>& out)
    {
        if (e == nullptr)
            return;
        if (e->hasTagName (tag))
        {
            out.add (e);
            return;
        }
        for (auto* c : e->getChildIterator())
            findAll (c, tag, out);
    }

    /** An attribute as a number, accepting any of several names. */
    inline double attr (const juce::XmlElement* e, std::initializer_list<const char*> names, double fallback = 0.0)
    {
        if (e != nullptr)
            for (const auto* n : names)
                if (e->hasAttribute (n))
                    return e->getDoubleAttribute (n, fallback);
        return fallback;
    }

    /** A file inside a zip, as text (or empty). */
    inline juce::String zipText (juce::ZipFile& zip, const juce::String& name)
    {
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (const auto* entry = zip.getEntry (i); entry != nullptr && entry->filename.equalsIgnoreCase (name))
                if (std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (i) }; in != nullptr)
                    return in->readEntireStreamAsString();
        return {};
    }

    inline juce::MemoryBlock zipData (juce::ZipFile& zip, const juce::String& name)
    {
        juce::MemoryBlock block;
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (const auto* entry = zip.getEntry (i); entry != nullptr && entry->filename.equalsIgnoreCase (name))
                if (std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (i) }; in != nullptr)
                    in->readIntoMemoryBlock (block);
        return block;
    }
}
