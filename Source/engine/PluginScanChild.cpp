#include "PluginScanChild.h"
#include "ClapFormat.h"

namespace riff::pluginscan
{

bool isScanChild (const juce::StringArray& commandLine)
{
    return commandLine.size() >= 4 && commandLine[0] == argument;
}

int runScanChild (const juce::StringArray& commandLine)
{
    if (! isScanChild (commandLine))
        return 2;
    const auto formatName = commandLine[1], identifier = commandLine[2];
    const juce::File output (commandLine[3]);

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    formats.addFormat (std::make_unique<ClapPluginFormat>());

    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == formatName)
            format = f;
    if (format == nullptr)
        return 3;

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, identifier);   // (if the plugin crashes here, only this process goes)

    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    // Written beside, then moved into place: the parent only ever sees a whole result
    const auto temp = output.getSiblingFile (output.getFileName() + ".part");
    if (! xml.writeTo (temp) || ! temp.moveFileTo (output))
        return 4;
    return 0;
}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    const auto output = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("AudioTechProScan", ".xml", false);
    const auto app = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    juce::ChildProcess child;
    if (! child.start (juce::StringArray { app.getFullPathName(), argument, format.getName(), fileOrIdentifier, output.getFullPathName() }, 0))
    {
        // (no second process possible: checked here, as before)
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    // Waits for it, a little at a time (so a scan can be cancelled)
    for (int waited = 0; child.isRunning(); waited += 100)
    {
        if (shouldExit() || waited >= timeoutMs)
        {
            child.kill();
            output.deleteFile();
            return shouldExit();   // hung: not scanned again (false); cancelled: tried again next time
        }
        child.waitForProcessToFinish (100);
    }

    const auto xml = juce::XmlDocument::parse (output);
    output.deleteFile();
    output.getSiblingFile (output.getFileName() + ".part").deleteFile();
    if (xml == nullptr || ! xml->hasTagName ("PLUGINS"))
        return false;   // it crashed: put on the "don't scan" list

    for (auto* e : xml->getChildIterator())
    {
        auto d = std::make_unique<juce::PluginDescription>();
        if (d->loadFromXml (*e))
            result.add (d.release());
    }
    return true;
}

} // namespace riff::pluginscan
