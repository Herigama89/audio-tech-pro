#pragma once

#include <JuceHeader.h>

/** Apple property lists (XML or binary "bplist00") as juce::var: dictionaries are DynamicObjects. */
namespace riff::interchange::plist
{
    namespace detail
    {
        struct Binary
        {
            const juce::uint8* d = nullptr;
            size_t size = 0;
            int offsetSize = 0, refSize = 0;
            juce::uint64 numObjects = 0, offsetTable = 0;
            int depth = 0;

            juce::uint64 readInt (size_t at, int bytes) const
            {
                juce::uint64 v = 0;
                for (int i = 0; i < bytes && at + (size_t) i < size; ++i)
                    v = (v << 8) | d[at + (size_t) i];
                return v;
            }

            size_t offsetOf (juce::uint64 ref) const
            {
                return ref < numObjects ? (size_t) readInt ((size_t) (offsetTable + ref * (juce::uint64) offsetSize), offsetSize) : size;
            }

            /** A length nibble of 15: the real length follows as an int object. */
            juce::uint64 lengthAt (size_t& at, int nibble) const
            {
                if (nibble != 15)
                    return (juce::uint64) nibble;
                if (at >= size)
                    return 0;
                const auto marker = d[at++];
                const auto bytes = 1 << (marker & 0x0f);
                const auto v = readInt (at, bytes);
                at += (size_t) bytes;
                return v;
            }

            juce::var object (juce::uint64 ref)
            {
                auto at = offsetOf (ref);
                if (at >= size || ++depth > 64)
                    return {};
                juce::ScopeGuard leave { [this] { --depth; } };
                const auto marker = d[at++];
                const auto type = marker >> 4, nibble = marker & 0x0f;
                switch (type)
                {
                    case 0x0: return nibble == 9 ? juce::var (true) : nibble == 8 ? juce::var (false) : juce::var();
                    case 0x1: return juce::var ((juce::int64) readInt (at, 1 << nibble));
                    case 0x2:
                    {
                        const auto bits = readInt (at, 1 << nibble);
                        if (nibble == 2) { juce::uint32 b = (juce::uint32) bits; float f; std::memcpy (&f, &b, 4); return juce::var ((double) f); }
                        double v; std::memcpy (&v, &bits, 8); return juce::var (v);
                    }
                    case 0x3: return {};
                    case 0x4:
                    {
                        const auto n = lengthAt (at, nibble);
                        if (at + n > size) return {};
                        return juce::var (juce::MemoryBlock (d + at, (size_t) n));
                    }
                    case 0x5:
                    {
                        const auto n = lengthAt (at, nibble);
                        if (at + n > size) return {};
                        return juce::var (juce::String::fromUTF8 (reinterpret_cast<const char*> (d + at), (int) n));
                    }
                    case 0x6:
                    {
                        const auto n = lengthAt (at, nibble);
                        if (at + n * 2 > size) return {};
                        juce::String s;
                        for (juce::uint64 i = 0; i < n; ++i)
                            s += juce::String::charToString ((juce::juce_wchar) readInt (at + (size_t) i * 2, 2));
                        return juce::var (s);
                    }
                    case 0xA:
                    {
                        const auto n = lengthAt (at, nibble);
                        juce::Array<juce::var> items;
                        for (juce::uint64 i = 0; i < n && at + (i + 1) * (juce::uint64) refSize <= size; ++i)
                            items.add (object (readInt (at + (size_t) (i * (juce::uint64) refSize), refSize)));
                        return juce::var (items);
                    }
                    case 0xD:
                    {
                        const auto n = lengthAt (at, nibble);
                        auto* dict = new juce::DynamicObject();
                        juce::var result (dict);
                        for (juce::uint64 i = 0; i < n && at + (n + i + 1) * (juce::uint64) refSize <= size; ++i)
                        {
                            const auto key = object (readInt (at + (size_t) (i * (juce::uint64) refSize), refSize)).toString();
                            dict->setProperty (key, object (readInt (at + (size_t) ((n + i) * (juce::uint64) refSize), refSize)));
                        }
                        return result;
                    }
                    default: return {};
                }
            }
        };

        inline juce::var fromXml (const juce::XmlElement* e)
        {
            if (e == nullptr)
                return {};
            const auto tag = e->getTagName();
            if (tag == "dict")
            {
                auto* dict = new juce::DynamicObject();
                juce::var result (dict);
                juce::String key;
                for (auto* c : e->getChildIterator())
                {
                    if (c->hasTagName ("key"))
                        key = c->getAllSubText();
                    else
                        dict->setProperty (key, fromXml (c));
                }
                return result;
            }
            if (tag == "array")
            {
                juce::Array<juce::var> items;
                for (auto* c : e->getChildIterator())
                    items.add (fromXml (c));
                return juce::var (items);
            }
            if (tag == "string") return e->getAllSubText();
            if (tag == "integer") return juce::var (e->getAllSubText().getLargeIntValue());
            if (tag == "real") return juce::var (e->getAllSubText().getDoubleValue());
            if (tag == "true") return juce::var (true);
            if (tag == "false") return juce::var (false);
            if (tag == "data")
            {
                juce::MemoryOutputStream decoded;
                juce::Base64::convertFromBase64 (decoded, e->getAllSubText().removeCharacters (" \t\r\n"));
                return juce::var (decoded.getMemoryBlock());
            }
            return {};
        }
    }

    inline juce::var parse (const juce::MemoryBlock& data)
    {
        const auto* bytes = static_cast<const juce::uint8*> (data.getData());
        const auto size = data.getSize();
        if (size >= 40 && std::memcmp (bytes, "bplist00", 8) == 0)
        {
            detail::Binary b;
            b.d = bytes;
            b.size = size;
            const auto* trailer = bytes + size - 32;
            b.offsetSize = trailer[6];
            b.refSize = trailer[7];
            b.numObjects = b.readInt (size - 24, 8);
            const auto top = b.readInt (size - 16, 8);
            b.offsetTable = b.readInt (size - 8, 8);
            if (b.offsetSize < 1 || b.offsetSize > 8 || b.refSize < 1 || b.refSize > 8 || b.offsetTable >= size)
                return {};
            return b.object (top);
        }
        if (auto xml = juce::parseXML (data.toString()))
            if (xml->hasTagName ("plist"))
                return detail::fromXml (xml->getFirstChildElement());
        return {};
    }

    inline juce::var parse (const juce::File& file)
    {
        juce::MemoryBlock data;
        return file.loadFileAsData (data) ? parse (data) : juce::var();
    }
}
