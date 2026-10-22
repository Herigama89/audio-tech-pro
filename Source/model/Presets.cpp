#include "Presets.h"
#include "Identifiers.h"
#include "Project.h"

namespace riff::presets
{
namespace
{
    juce::File& testRoot()
    {
        static juce::File folder;
        return folder;
    }

    const juce::String extension (".atppreset");
    const juce::String defaultName ("__default__");

    /** The insert's own properties a preset doesn't carry: what it is, where it is, whether it's on. */
    bool isIdentity (const juce::Identifier& p)
    {
        static const juce::Array<juce::Identifier> skip { ids::id, ids::kind, ids::effect, ids::bypass, ids::slot, ids::sidechain, ids::print,
                                                          ids::pluginDescription, ids::pluginState, ids::araArchive, ids::name, "presetName" };
        return skip.contains (p);
    }

    juce::File fileFor (const juce::ValueTree& insert, const juce::String& name)
    {
        return root().getChildFile (keyOf (insert)).getChildFile (juce::File::createLegalFileName (name) + extension);
    }

    bool isPlugin (const juce::ValueTree& insert) { return insert[ids::kind].toString() == model::pluginKind; }

    bool write (const juce::ValueTree& insert, const juce::File& file, const juce::String& name, const juce::String& pluginState)
    {
        juce::XmlElement xml ("ATPPRESET");
        xml.setAttribute ("name", name);
        xml.setAttribute ("plugin", keyOf (insert));
        if (isPlugin (insert))
            xml.setAttribute ("state", pluginState.isNotEmpty() ? pluginState : insert[ids::pluginState].toString());
        else
        {
            auto* params = xml.createNewChildElement ("PARAMS");
            for (int i = 0; i < insert.getNumProperties(); ++i)
            {
                const auto p = insert.getPropertyName (i);
                if (! isIdentity (p))
                    params->setAttribute (p, insert[p].toString());
            }
        }
        if (! file.getParentDirectory().createDirectory())
            return false;
        return xml.writeTo (file);
    }

    bool read (juce::ValueTree insert, const juce::File& file, juce::UndoManager* um, juce::String* pluginState, const juce::String& shownName)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("ATPPRESET"))
            return false;
        if (isPlugin (insert))
        {
            const auto state = xml->getStringAttribute ("state");
            insert.setProperty (ids::pluginState, state, um);
            if (pluginState != nullptr)
                *pluginState = state;
        }
        else if (const auto* params = xml->getChildByName ("PARAMS"))
            for (int i = 0; i < params->getNumAttributes(); ++i)
            {
                const auto name = params->getAttributeName (i);
                const auto text = params->getAttributeValue (i);
                // (numbers as numbers, the rest as text: a file path, say)
                const juce::var value = text.containsOnly ("0123456789.-eE") && text.containsAnyOf ("0123456789") ? juce::var (text.getDoubleValue())
                                                                                                               : juce::var (text);
                insert.setProperty (juce::Identifier (name), value, um);
            }
        insert.setProperty ("presetName", shownName, um);
        return true;
    }
}

juce::File root()
{
    if (testRoot() != juce::File())
        return testRoot();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Audio Tech Pro").getChildFile ("Presets");
}

void setRootForTests (const juce::File& folder) { testRoot() = folder; }

juce::String keyOf (const juce::ValueTree& insert)
{
    if (! isPlugin (insert))
        return juce::File::createLegalFileName ("Built-in " + insert[ids::effect].toString());
    juce::PluginDescription d;
    if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
        return juce::File::createLegalFileName (d.manufacturerName + " - " + d.name + " (" + d.pluginFormatName + ")");
    return "Unknown plug-in";
}

juce::String displayNameOf (const juce::ValueTree& insert)
{
    if (! isPlugin (insert))
        return insert[ids::effect].toString();
    juce::PluginDescription d;
    if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
        return d.name;
    return "Plug-in";
}

juce::StringArray list (const juce::ValueTree& insert)
{
    juce::StringArray names;
    for (const auto& f : root().getChildFile (keyOf (insert)).findChildFiles (juce::File::findFiles, false, "*" + extension))
        if (f.getFileNameWithoutExtension() != defaultName)
            names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool save (const juce::ValueTree& insert, const juce::String& name, const juce::String& pluginState)
{
    if (name.trim().isEmpty() || name == defaultName)
        return false;
    return write (insert, fileFor (insert, name.trim()), name.trim(), pluginState);
}

bool load (juce::ValueTree insert, const juce::String& name, juce::UndoManager* um, juce::String* pluginState)
{
    return read (insert, fileFor (insert, name), um, pluginState, name);
}

bool remove (const juce::ValueTree& insert, const juce::String& name)
{
    return fileFor (insert, name).deleteFile();
}

bool saveDefault (const juce::ValueTree& insert, const juce::String& pluginState)
{
    return write (insert, fileFor (insert, defaultName), "Default", pluginState);
}

bool hasDefault (const juce::ValueTree& insert) { return fileFor (insert, defaultName).existsAsFile(); }
bool clearDefault (const juce::ValueTree& insert) { return fileFor (insert, defaultName).deleteFile(); }

bool applyDefault (juce::ValueTree insert, juce::UndoManager* um, juce::String* pluginState)
{
    return hasDefault (insert) && read (insert, fileFor (insert, defaultName), um, pluginState, "Default");
}

void resetToFactory (juce::ValueTree insert, juce::UndoManager* um)
{
    // (a built-in's knobs are its properties; without them it takes each knob's default)
    juce::Array<juce::Identifier> knobs;
    for (int i = 0; i < insert.getNumProperties(); ++i)
        if (! isIdentity (insert.getPropertyName (i)) && insert.getPropertyName (i) != juce::Identifier ("irFile"))
            knobs.add (insert.getPropertyName (i));
    for (const auto& k : knobs)
        insert.removeProperty (k, um);
    insert.setProperty ("presetName", "Factory Default", um);
}

} // namespace riff::presets
