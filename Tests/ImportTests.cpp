#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "import/ProjectImport.h"
#include "import/PropertyList.h"
#include "model/Project.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    struct TempFolder
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("riff-import", "");
        TempFolder() { dir.createDirectory(); }
        ~TempFolder() { dir.deleteRecursively(); }
    };

    void writeWav (const juce::File& file, double seconds, double rate = 44100.0, juce::int64 timeReference = -1)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::AudioBuffer<float> buffer (2, (int) (seconds * rate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.25f * std::sin ((float) i * 0.05f)), buffer.setSample (1, i, buffer.getSample (0, i));
        juce::StringPairArray metadata;
        if (timeReference >= 0)
            metadata = juce::WavAudioFormat::createBWAVMetadata ("", "", "", juce::Time::getCurrentTime(), timeReference, "");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), rate, 2, 16, metadata, 0));
        REQUIRE (writer != nullptr);
        out.release();
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    void writeZip (const juce::File& file, const std::vector<std::pair<juce::String, juce::MemoryBlock>>& entries)
    {
        juce::ZipFile::Builder builder;
        for (const auto& [name, data] : entries)
            builder.addEntry (new juce::MemoryInputStream (data, true), 0, name, juce::Time::getCurrentTime());
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (builder.writeToStream (out, nullptr));
    }

    juce::MemoryBlock textBlock (const juce::String& s) { return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8()); }

    juce::MemoryBlock vstPreset (const juce::MemoryBlock& component, const juce::MemoryBlock& controller)
    {
        juce::MemoryOutputStream out;
        out.write ("VST3", 4);
        out.writeInt (1);
        out.write ("0123456789ABCDEF0123456789ABCDEF", 32);
        const auto dataStart = (juce::int64) 48;
        const auto listAt = dataStart + (juce::int64) component.getSize() + (juce::int64) controller.getSize();
        out.writeInt64 (listAt);
        out.write (component.getData(), component.getSize());
        out.write (controller.getData(), controller.getSize());
        out.write ("List", 4);
        out.writeInt (2);
        out.write ("Comp", 4); out.writeInt64 (dataStart); out.writeInt64 ((juce::int64) component.getSize());
        out.write ("Cont", 4); out.writeInt64 (dataStart + (juce::int64) component.getSize()); out.writeInt64 ((juce::int64) controller.getSize());
        return out.getMemoryBlock();
    }

    /** Pretend installed plugins. */
    void fillKnownPlugins (juce::KnownPluginList& list)
    {
        const auto add = [&list] (const char* name, const char* format, bool instrument, const char* maker)
        {
            juce::PluginDescription d;
            d.name = name;
            d.pluginFormatName = format;
            d.isInstrument = instrument;
            d.manufacturerName = maker;
            d.fileOrIdentifier = juce::String ("/plugins/") + name + "." + format;
            d.uniqueId = d.deprecatedUid = (int) d.fileOrIdentifier.hashCode();
            list.addType (d);
        };
        add ("Test Synth", "VST3", true, "Acme");
        add ("Test Delay", "VST3", false, "Acme");
        add ("Test Delay", "AudioUnit", false, "Acme");
    }

    projectimport::PluginMatcher matcher (const juce::KnownPluginList& list)
    {
        return [&list] (const interchange::Plugin& p) { return projectimport::matchPlugin (list, p); };
    }

    juce::ValueTree trackNamed (const juce::ValueTree& project, const juce::String& name)
    {
        for (const auto& t : project)
            if (t.hasType (ids::TRACK) && t[ids::name].toString() == name)
                return t;
        return {};
    }

    juce::Array<juce::ValueTree> regionsOf (const juce::ValueTree& track)
    {
        juce::Array<juce::ValueTree> out;
        for (const auto& r : track)
            if (r.hasType (ids::REGION))
                out.add (r);
        return out;
    }

    juce::Array<juce::ValueTree> notesOf (const juce::ValueTree& region)
    {
        juce::Array<juce::ValueTree> out;
        for (const auto& n : region)
            if (n.hasType (ids::NOTE))
                out.add (n);
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return (double) a[ids::start] < (double) b[ids::start]; });
        return out;
    }

    juce::String decodedState (const juce::String& base64)
    {
        juce::MemoryBlock block;
        block.fromBase64Encoding (base64);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()))
            return xml->toString();
        return block.toString();
    }
}

//==============================================================================
TEST_CASE ("Import: the formats we open", "[import]")
{
    for (const auto* name : { "a.als", "b.flp", "c.song", "d.dawproject", "e.ptx", "f.ptf", "g.cpr", "h.mid" })
        REQUIRE (projectimport::formatName (juce::File::getCurrentWorkingDirectory().getChildFile (name)).isNotEmpty());
    REQUIRE (projectimport::formatName (juce::File::getCurrentWorkingDirectory().getChildFile ("x.wav")).isEmpty());
    REQUIRE (projectimport::wildcard().contains ("*.als"));
}

TEST_CASE ("Import: VST3 preset and state helpers", "[import]")
{
    const juce::MemoryBlock comp ("abc", 3), cont ("de", 2);
    juce::String id;
    juce::MemoryBlock c1, c2;
    REQUIRE (interchange::readVstPreset (vstPreset (comp, cont), id, c1, c2));
    REQUIRE (id == "0123456789ABCDEF0123456789ABCDEF");
    REQUIRE (c1 == comp);
    REQUIRE (c2 == cont);

    const auto state = interchange::juceVst3State (comp, cont);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    REQUIRE (xml->hasTagName ("VST3PluginState"));
    juce::MemoryBlock back;
    back.fromBase64Encoding (xml->getChildElementAllSubText ("IComponent", {}));
    REQUIRE (back == comp);

    REQUIRE (interchange::fromHex ("0A 0b\n0C") == juce::MemoryBlock ("\x0a\x0b\x0c", 3));
}

TEST_CASE ("Import: binary property lists (Logic)", "[import]")
{
    const auto value = interchange::plist::parse (juce::File (TEST_DATA_DIR).getChildFile ("logic-metadata-binary.plist"));
    REQUIRE ((double) value.getProperty ("BeatsPerMinute", 0) == 96.0);
    REQUIRE ((int) value.getProperty ("SongSignatureNumerator", 0) == 3);
    REQUIRE (value.getProperty ("AudioFiles", {}).size() == 2);
    REQUIRE (value.getProperty ("AudioFiles", {})[0].toString() == "Audio Files/Vox#01.wav");
}

//==============================================================================
TEST_CASE ("Import: an Ableton Live set", "[import]")
{
    TempFolder temp;
    writeWav (temp.dir.getChildFile ("Samples/Imported/loop.wav"), 4.0);

    const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="11.0_433" Creator="Ableton Live 11.3">
<LiveSet>
<Tracks>
  <GroupTrack Id="20"><Name><EffectiveName Value="Band"/><UserName Value=""/></Name><Color Value="3"/><TrackGroupId Value="-1"/>
    <DeviceChain><Mixer><Volume><Manual Value="1"/></Volume><Pan><Manual Value="0"/></Pan><Speaker><Manual Value="true"/></Speaker></Mixer></DeviceChain></GroupTrack>
  <MidiTrack Id="21"><Name><EffectiveName Value="Keys"/><UserName Value=""/></Name><Color Value="10"/><TrackGroupId Value="20"/>
    <DeviceChain>
      <Mixer><Volume><Manual Value="0.5"/></Volume><Pan><Manual Value="-0.5"/></Pan><Speaker><Manual Value="true"/></Speaker>
        <Sends><TrackSendHolder Id="0"><Send><Manual Value="0.5"/></Send></TrackSendHolder></Sends></Mixer>
      <MainSequencer>
        <ClipTimeable><ArrangerAutomation><Events>
          <MidiClip Id="0" Time="4"><CurrentStart Value="4"/><CurrentEnd Value="8"/>
            <Loop><LoopStart Value="0"/><LoopEnd Value="4"/><StartRelative Value="0"/><LoopOn Value="false"/></Loop>
            <Name Value="Chords"/><Disabled Value="false"/>
            <Notes><KeyTracks><KeyTrack Id="0"><Notes>
              <MidiNoteEvent Time="0" Duration="1" Velocity="100" IsEnabled="true"/>
              <MidiNoteEvent Time="2" Duration="0.5" Velocity="80" IsEnabled="true"/>
            </Notes><MidiKey Value="60"/></KeyTrack></KeyTracks></Notes>
          </MidiClip>
        </Events></ArrangerAutomation></ClipTimeable>
        <ClipSlotList><ClipSlot Id="0"><ClipSlot><Value>
          <MidiClip Id="1" Time="0"><CurrentStart Value="0"/><CurrentEnd Value="2"/>
            <Loop><LoopStart Value="0"/><LoopEnd Value="2"/><StartRelative Value="0"/><LoopOn Value="true"/></Loop><Name Value="Riff"/>
            <Notes><KeyTracks><KeyTrack Id="0"><Notes><MidiNoteEvent Time="0" Duration="1" Velocity="90" IsEnabled="true"/></Notes><MidiKey Value="62"/></KeyTrack></KeyTracks></Notes>
          </MidiClip></Value></ClipSlot></ClipSlot></ClipSlotList>
      </MainSequencer>
      <DeviceChain><Devices>
        <PluginDevice Id="0"><On><Manual Value="true"/></On><PluginDesc><Vst3PluginInfo Id="0"><Name Value="Test Synth"/><DeviceType Value="1"/>
          <Uid><Fields.0 Value="1"/><Fields.1 Value="2"/><Fields.2 Value="3"/><Fields.3 Value="-1"/></Uid>
          <Preset><Vst3Preset Id="0"><ProcessorState>0A0B0C</ProcessorState><ControllerState>0D</ControllerState></Vst3Preset></Preset></Vst3PluginInfo></PluginDesc></PluginDevice>
        <Eq8 Id="1"><UserName Value=""/></Eq8>
        <PluginDevice Id="2"><On><Manual Value="false"/></On><PluginDesc><AuPluginInfo Id="0"><Name Value="Test Delay"/><Manufacturer Value="Acme"/><ComponentType Value="1635083896"/>
          <Preset><AuPreset><Buffer>3C706C6973743E</Buffer></AuPreset></Preset></AuPluginInfo></PluginDesc></PluginDevice>
      </Devices></DeviceChain>
    </DeviceChain></MidiTrack>
  <AudioTrack Id="22"><Name><EffectiveName Value="Loop"/><UserName Value=""/></Name><Color Value="20"/><TrackGroupId Value="-1"/>
    <DeviceChain>
      <Mixer><Volume><Manual Value="1"/></Volume><Pan><Manual Value="0"/></Pan><Speaker><Manual Value="false"/></Speaker></Mixer>
      <MainSequencer><Sample><ArrangerAutomation><Events>
        <AudioClip Id="0" Time="0"><CurrentStart Value="0"/><CurrentEnd Value="8"/>
          <Loop><LoopStart Value="0.5"/><LoopEnd Value="8.5"/><StartRelative Value="0"/><LoopOn Value="false"/></Loop>
          <Name Value=""/><Disabled Value="false"/>
          <SampleRef><FileRef><RelativePath Value="Samples/Imported/loop.wav"/><Path Value="/nowhere/loop.wav"/></FileRef></SampleRef>
          <IsWarped Value="true"/><WarpMarkers><WarpMarker Id="0" SecTime="0" BeatTime="0"/><WarpMarker Id="1" SecTime="2" BeatTime="4"/></WarpMarkers>
          <SampleVolume Value="0.5"/>
        </AudioClip>
      </Events></ArrangerAutomation></Sample></MainSequencer>
      <DeviceChain><Devices/></DeviceChain>
    </DeviceChain></AudioTrack>
  <ReturnTrack Id="23"><Name><EffectiveName Value="Reverb"/><UserName Value=""/></Name><Color Value="5"/><TrackGroupId Value="-1"/>
    <DeviceChain><Mixer><Volume><Manual Value="1"/></Volume><Pan><Manual Value="0"/></Pan><Speaker><Manual Value="true"/></Speaker></Mixer></DeviceChain></ReturnTrack>
</Tracks>
<MasterTrack><DeviceChain><Mixer>
  <Tempo><Manual Value="100"/><AutomationTarget Id="8"/></Tempo><TimeSignature><Manual Value="201"/><AutomationTarget Id="9"/></TimeSignature><Volume><Manual Value="1"/></Volume>
  </Mixer></DeviceChain>
  <AutomationEnvelopes><Envelopes><AutomationEnvelope Id="0"><EnvelopeTarget><PointeeId Value="8"/></EnvelopeTarget><Automation><Events>
    <FloatEvent Id="1" Time="-63072000" Value="100"/><FloatEvent Id="2" Time="16" Value="140"/>
  </Events></Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes>
</MasterTrack>
<Locators><Locators><Locator Id="0"><Time Value="8"/><Name Value="Verse"/></Locator></Locators></Locators>
</LiveSet></Ableton>)";

    const auto file = temp.dir.getChildFile ("Song.als");
    {
        juce::FileOutputStream raw (file);
        juce::GZIPCompressorOutputStream gz (raw, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gz.writeText (xml, false, false, nullptr);
    }

    juce::KnownPluginList known;
    fillKnownPlugins (known);
    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, matcher (known), notes, error);
    INFO (error);
    REQUIRE (project.isValid());

    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (100.0, 1.0e-9));
    REQUIRE (model::numTracks (project) == 4);
    REQUIRE (project.getChild (0)[ids::name].toString() == "Band");

    const auto band = trackNamed (project, "Band"), keys = trackNamed (project, "Keys"), loop = trackNamed (project, "Loop"), reverb = trackNamed (project, "Reverb");
    REQUIRE (model::isBusTrack (band));
    REQUIRE (model::isBusTrack (reverb));
    REQUIRE (keys[ids::stackParent] == band[ids::id]);
    REQUIRE (keys[ids::output] == band[ids::id]);
    REQUIRE_THAT ((double) keys[ids::volume], WithinAbs (0.5, 1.0e-9));
    REQUIRE_THAT ((double) keys[ids::pan], WithinAbs (-0.5, 1.0e-9));
    REQUIRE ((bool) loop[ids::mute]);

    // The MIDI clip at beat 4 (2.4 s at 100 BPM), its notes in seconds from its start
    const auto keyRegions = regionsOf (keys);
    REQUIRE (keyRegions.size() == 1);
    REQUIRE_THAT ((double) keyRegions[0][ids::start], WithinAbs (2.4, 1.0e-6));
    REQUIRE_THAT ((double) keyRegions[0][ids::length], WithinAbs (2.4, 1.0e-6));
    const auto keyNotes = notesOf (keyRegions[0]);
    REQUIRE (keyNotes.size() == 2);
    REQUIRE_THAT ((double) keyNotes[0][ids::length], WithinAbs (0.6, 1.0e-6));
    REQUIRE_THAT ((double) keyNotes[1][ids::start], WithinAbs (1.2, 1.0e-6));
    REQUIRE ((int) keyNotes[1][ids::velocity] == 80);

    // The session clip went into Live Loops
    REQUIRE (model::liveCell (project, keys[ids::id].toString(), 0).isValid());

    // The instrument plugin with its settings; the AU delay, bypassed, with its preset
    REQUIRE (keys[ids::instrument].toString() == "plugin");
    REQUIRE (keys[ids::pluginDescription].toString().contains ("Test Synth"));
    REQUIRE (decodedState (keys[ids::pluginState].toString()).contains ("IComponent"));
    const auto inserts = model::insertsOf (model::findChain (project, keys[ids::id].toString()));
    REQUIRE (inserts.size() == 1);
    REQUIRE (inserts[0][ids::pluginDescription].toString().contains ("AudioUnit"));
    REQUIRE ((bool) inserts[0][ids::bypass]);
    REQUIRE (decodedState (inserts[0][ids::pluginState].toString()) == "<plist>");
    REQUIRE (notes.joinIntoString ("\n").contains ("Eq8"));

    // The send to the return
    const auto sends = model::sendsOf (model::findChain (project, keys[ids::id].toString()));
    REQUIRE (sends.size() == 1);
    REQUIRE (sends[0][ids::target] == reverb[ids::id]);

    // The warped clip: its own tempo (120) follows the song's (Flex), starting half a beat in
    const auto loopRegions = regionsOf (loop);
    REQUIRE (loopRegions.size() == 1);
    REQUIRE (juce::File (loopRegions[0][ids::file].toString()).existsAsFile());
    REQUIRE_THAT ((double) loopRegions[0][ids::length], WithinAbs (4.8, 1.0e-6));
    REQUIRE_THAT ((double) loopRegions[0][ids::offset], WithinAbs (0.25, 1.0e-6));
    REQUIRE_THAT ((double) loopRegions[0][ids::sourceBpm], WithinAbs (120.0, 1.0e-6));
    REQUIRE ((bool) loopRegions[0][ids::flex]);
    REQUIRE_THAT ((double) loopRegions[0][ids::gain], WithinAbs (0.5, 1.0e-9));

    // Tempo change at beat 16, the locator at beat 8
    const auto tempos = model::sortedTempoChanges (project);
    REQUIRE (tempos.size() == 1);
    REQUIRE_THAT ((double) tempos[0][ids::bpm], WithinAbs (140.0, 1.0e-9));
    const auto markers = model::sortedMarkers (project);
    REQUIRE (markers.size() == 1);
    REQUIRE_THAT ((double) markers[0][ids::time], WithinAbs (4.8, 1.0e-6));
}

//==============================================================================
TEST_CASE ("Import: a DAWproject", "[import]")
{
    TempFolder temp;
    const auto wavFile = temp.dir.getChildFile ("kick-src.wav");
    writeWav (wavFile, 1.0);
    juce::MemoryBlock wav;
    wavFile.loadFileAsData (wav);

    const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
 <Application name="Bitwig Studio" version="5.0"/>
 <Transport><Tempo unit="bpm" value="90" id="t"/><TimeSignature numerator="3" denominator="4" id="s"/></Transport>
 <Structure>
  <Track contentType="audio" id="tr1" name="Drums" color="#ff0000">
   <Channel role="regular" id="ch1" destination="m" audioChannels="2" solo="false">
     <Devices><Vst3Plugin deviceID="ABCDEF" deviceName="Test Delay" deviceRole="audioFX" id="d1"><Enabled value="false"/><State path="plugins/delay.vstpreset"/></Vst3Plugin>
              <Equalizer deviceName="EQ+" id="d2"/></Devices>
     <Mute value="false"/><Pan unit="normalized" value="0.75"/><Volume unit="linear" value="0.5"/>
     <Sends><Send destination="ch2" type="post"><Volume unit="linear" value="0.25"/></Send></Sends>
   </Channel></Track>
  <Track contentType="notes" id="tr2" name="Bass"><Channel role="regular" id="ch3" destination="m">
     <Devices><Vst3Plugin deviceName="Test Synth" deviceRole="instrument" id="d3"/></Devices><Volume unit="decibel" value="-6"/></Channel></Track>
  <Track contentType="audio" id="tr3" name="FX"><Channel role="effect" id="ch2" destination="m"><Volume unit="linear" value="1"/></Channel></Track>
  <Channel role="master" id="m"><Volume unit="linear" value="0.9"/></Channel>
 </Structure>
 <Arrangement id="a">
  <Lanes timeUnit="beats" id="l">
   <Lanes track="tr1"><Clips><Clip time="2" duration="4" playStart="0">
     <Warps contentTimeUnit="seconds" timeUnit="beats"><Audio channels="2" duration="1" sampleRate="44100"><File path="audio/kick.wav"/></Audio>
       <Warp time="0" contentTime="0"/><Warp time="2" contentTime="1"/></Warps></Clip></Clips></Lanes>
   <Lanes track="tr2"><Clips><Clip time="0" duration="8" playStart="0" loopStart="0" loopEnd="4">
     <Notes><Note time="0" duration="1" key="40" vel="0.5" channel="0"/></Notes></Clip></Clips></Lanes>
   <Markers><Marker time="4" name="B"/></Markers>
  </Lanes>
  <TempoAutomation><RealPoint time="0" value="90"/><RealPoint time="12" value="120" interpolation="hold"/></TempoAutomation>
 </Arrangement>
</Project>)";

    const auto file = temp.dir.getChildFile ("Mix.dawproject");
    writeZip (file, { { "project.xml", textBlock (xml) }, { "audio/kick.wav", wav },
                      { "plugins/delay.vstpreset", vstPreset (juce::MemoryBlock ("comp", 4), juce::MemoryBlock ("ctl", 3)) } });

    juce::KnownPluginList known;
    fillKnownPlugins (known);
    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, matcher (known), notes, error);
    INFO (error);
    REQUIRE (project.isValid());
    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (90.0, 1.0e-9));
    REQUIRE ((int) project[ids::timeSigNumerator] == 3);

    const auto drums = trackNamed (project, "Drums"), bass = trackNamed (project, "Bass"), fx = trackNamed (project, "FX");
    REQUIRE (drums.isValid());
    REQUIRE (model::isInstrumentTrack (bass));
    REQUIRE (model::isBusTrack (fx));
    REQUIRE (juce::Colour::fromString (drums[ids::colour].toString()) == juce::Colour (0xffff0000));
    REQUIRE_THAT ((double) drums[ids::pan], WithinAbs (0.5, 1.0e-9));
    REQUIRE_THAT ((double) drums[ids::volume], WithinAbs (0.5, 1.0e-9));
    REQUIRE_THAT ((double) bass[ids::volume], WithinAbs (interchange::dbToGain (-6.0), 1.0e-9));

    const auto inserts = model::insertsOf (model::findChain (project, drums[ids::id].toString()));
    REQUIRE (inserts.size() == 1);
    REQUIRE ((bool) inserts[0][ids::bypass]);
    REQUIRE (inserts[0][ids::pluginDescription].toString().contains ("VST3"));
    REQUIRE (decodedState (inserts[0][ids::pluginState].toString()).contains ("IEditController"));
    REQUIRE (notes.joinIntoString ("\n").contains ("EQ+"));

    const auto sends = model::sendsOf (model::findChain (project, drums[ids::id].toString()));
    REQUIRE (sends.size() == 1);
    REQUIRE (sends[0][ids::target] == fx[ids::id]);
    REQUIRE_THAT ((double) sends[0][ids::level], WithinAbs (0.25, 1.0e-9));

    // Audio unpacked next to it; the clip at beat 2 (90 BPM), its own tempo 120
    const auto drumRegions = regionsOf (drums);
    REQUIRE (drumRegions.size() == 1);
    const juce::File unpacked (drumRegions[0][ids::file].toString());
    REQUIRE (unpacked.existsAsFile());
    REQUIRE (unpacked.getParentDirectory().getFileName() == "Mix Audio");
    REQUIRE_THAT ((double) drumRegions[0][ids::start], WithinAbs (2.0 * 60.0 / 90.0, 1.0e-6));
    REQUIRE_THAT ((double) drumRegions[0][ids::sourceBpm], WithinAbs (120.0, 1.0e-6));

    // The looped note clip: one 4-beat pass, looping to beat 8
    const auto bassRegions = regionsOf (bass);
    REQUIRE (bassRegions.size() == 1);
    REQUIRE_THAT ((double) bassRegions[0][ids::length], WithinAbs (4.0 * 60.0 / 90.0, 1.0e-6));
    REQUIRE ((bool) bassRegions[0][ids::loop]);
    REQUIRE_THAT ((double) bassRegions[0][ids::loopEnd], WithinAbs (8.0 * 60.0 / 90.0, 1.0e-6));
    REQUIRE ((int) notesOf (bassRegions[0])[0][ids::velocity] == 64);
    REQUIRE (bass[ids::pluginDescription].toString().contains ("Test Synth"));

    REQUIRE (model::sortedTempoChanges (project).size() == 1);
    REQUIRE (model::sortedMarkers (project).size() == 1);
}

//==============================================================================
namespace
{
    struct FlpWriter
    {
        juce::MemoryOutputStream events;

        void byteEvent (int id, int v)  { events.writeByte ((char) id); events.writeByte ((char) v); }
        void wordEvent (int id, int v)  { events.writeByte ((char) id); events.writeShort ((short) v); }
        void dwordEvent (int id, juce::uint32 v) { events.writeByte ((char) id); events.writeInt ((int) v); }
        void dataEvent (int id, const juce::MemoryBlock& data)
        {
            events.writeByte ((char) id);
            auto n = data.getSize();
            do { auto b = (juce::uint8) (n & 0x7f); n >>= 7; if (n > 0) b |= 0x80; events.writeByte ((char) b); } while (n > 0);
            events.write (data.getData(), data.getSize());
        }
        void textEvent (int id, const juce::String& s)
        {
            juce::MemoryOutputStream utf16;
            for (auto c : s) utf16.writeShort ((short) c);
            utf16.writeShort (0);
            dataEvent (id, utf16.getMemoryBlock());
        }
        static juce::MemoryBlock wrapper (const juce::String& name, const juce::String& path)
        {
            juce::MemoryOutputStream w;
            w.writeInt (10);
            for (auto [id, text] : { std::pair<int, juce::String> { 54, name }, { 55, path } })
            {
                w.writeInt (id);
                w.writeInt64 ((juce::int64) text.getNumBytesAsUTF8());
                w.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
            }
            return w.getMemoryBlock();
        }

        juce::MemoryBlock file (int ppq)
        {
            juce::MemoryOutputStream out;
            out.write ("FLhd", 4); out.writeInt (6); out.writeShort (0); out.writeShort (2); out.writeShort ((short) ppq);
            out.write ("FLdt", 4); out.writeInt ((int) events.getDataSize());
            out.write (events.getData(), events.getDataSize());
            return out.getMemoryBlock();
        }
    };
}

TEST_CASE ("Import: an FL Studio project", "[import]")
{
    TempFolder temp;
    writeWav (temp.dir.getChildFile ("kick.wav"), 0.5);

    FlpWriter w;
    w.dataEvent (199, textBlock ("20.8.4.2576"));
    w.dwordEvent (156, 128000);
    w.textEvent (194, "My Beat");
    // Channel 0: a VST3 synth; channel 1: a sampler with kick.wav
    w.wordEvent (64, 0); w.byteEvent (21, 2); w.byteEvent (22, 1);
    w.textEvent (201, "Fruity Wrapper"); w.textEvent (203, "Lead"); w.dataEvent (213, FlpWriter::wrapper ("Test Synth", "C:/VST3/Test Synth.vst3"));
    w.wordEvent (64, 1); w.byteEvent (21, 0); w.textEvent (203, "Kick"); w.textEvent (196, "C:/Samples/kick.wav");
    // Pattern 1: a note for each channel
    w.wordEvent (65, 1); w.textEvent (193, "Hook");
    {
        juce::MemoryOutputStream notes;
        const auto note = [&notes] (juce::uint32 pos, int channel, juce::uint32 len, int key, int vel)
        {
            notes.writeInt ((int) pos); notes.writeShort (0); notes.writeShort ((short) channel); notes.writeInt ((int) len);
            notes.writeShort ((short) key); notes.writeShort (0);
            for (int i = 0; i < 8; ++i) notes.writeByte ((char) (i == 5 ? vel : 0));   // fine pitch, ?, release, midi ch, pan, velocity, x, y
        };
        note (0, 0, 96, 60, 100);
        note (96, 1, 48, 60, 110);
        w.dataEvent (224, notes.getMemoryBlock());
    }
    // The arrangement: the pattern at 0 (4 beats), the kick audio clip at beat 4 on playlist track 2
    w.wordEvent (99, 0);
    {
        juce::MemoryOutputStream items;
        const auto item = [&items] (juce::uint32 pos, int index, juce::uint32 len, int track)
        {
            items.writeInt ((int) pos); items.writeShort ((short) 20480); items.writeShort ((short) index); items.writeInt ((int) len);
            items.writeShort ((short) (499 - track)); items.writeShort (0); items.writeShort (0); items.writeShort (0);
            items.writeInt (0); items.writeInt (-1); items.writeInt (-1);
        };
        item (0, 20481, 384, 0);
        item (384, 1, 96, 1);
        w.dataEvent (233, items.getMemoryBlock());
    }
    w.textEvent (239, "Beat");
    w.textEvent (239, "Kick audio");
    w.dwordEvent (148, 192); w.textEvent (205, "Drop");
    // Mixer: the master insert (0) has a delay in its first slot
    w.textEvent (201, "Fruity Wrapper"); w.textEvent (203, "Test Delay"); w.dataEvent (213, FlpWriter::wrapper ("Test Delay", "C:/VST3/Test Delay.vst3"));
    w.wordEvent (98, 0);
    w.dwordEvent (147, 0);

    const auto file = temp.dir.getChildFile ("beat.flp");
    const auto data = w.file (96);
    file.replaceWithData (data.getData(), data.getSize());

    juce::KnownPluginList known;
    fillKnownPlugins (known);
    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, matcher (known), notes, error);
    INFO (error);
    REQUIRE (project.isValid());
    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (128.0, 1.0e-9));
    REQUIRE (project[ids::name].toString() == "My Beat");

    const auto lead = trackNamed (project, "Lead"), kick = trackNamed (project, "Kick"), audio = trackNamed (project, "Kick audio");
    REQUIRE (lead[ids::instrument].toString() == "plugin");
    REQUIRE (lead[ids::pluginDescription].toString().contains ("Test Synth"));
    REQUIRE (kick[ids::instrument].toString() == "sampler");
    REQUIRE (juce::File (kick[ids::samplerFile].toString()).existsAsFile());

    const auto spb = 60.0 / 128.0;
    const auto leadRegions = regionsOf (lead);
    REQUIRE (leadRegions.size() == 1);
    REQUIRE_THAT ((double) leadRegions[0][ids::length], WithinAbs (4.0 * spb, 1.0e-6));
    REQUIRE (leadRegions[0][ids::name].toString() == "Hook");
    REQUIRE (notesOf (leadRegions[0]).size() == 1);
    const auto kickNotes = notesOf (regionsOf (kick)[0]);
    REQUIRE_THAT ((double) kickNotes[0][ids::start], WithinAbs (spb, 1.0e-6));
    REQUIRE ((int) kickNotes[0][ids::velocity] == 110);

    const auto audioRegions = regionsOf (audio);
    REQUIRE (audioRegions.size() == 1);
    REQUIRE_THAT ((double) audioRegions[0][ids::start], WithinAbs (4.0 * spb, 1.0e-6));
    REQUIRE (juce::File (audioRegions[0][ids::file].toString()).existsAsFile());

    REQUIRE (model::insertsOf (model::findChain (project, model::masterOwner)).size() == 1);
    const auto markers = model::sortedMarkers (project);
    REQUIRE (markers.size() == 1);
    REQUIRE (markers[0][ids::name].toString() == "Drop");
}

//==============================================================================
TEST_CASE ("Import: a MIDI file", "[import]")
{
    TempFolder temp;
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    juce::MidiMessageSequence conductor, piano;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000 * 4 / 3), 0);   // 90 BPM
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (3, 4), 0);
    piano.addEvent (juce::MidiMessage::textMetaEvent (3, "Piano"), 0);
    piano.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 480);
    piano.addEvent (juce::MidiMessage::noteOff (1, 64), 960);
    piano.updateMatchedPairs();
    midi.addTrack (conductor);
    midi.addTrack (piano);
    const auto file = temp.dir.getChildFile ("song.mid");
    {
        juce::FileOutputStream out (file);
        midi.writeTo (out);
    }

    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, {}, notes, error);
    REQUIRE (project.isValid());
    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (90.0, 1.0e-3));
    REQUIRE ((int) project[ids::timeSigNumerator] == 3);
    const auto t = trackNamed (project, "Piano");
    REQUIRE (model::isInstrumentTrack (t));
    const auto n = notesOf (regionsOf (t)[0]);
    REQUIRE (n.size() == 1);
    REQUIRE_THAT ((double) n[0][ids::start], WithinAbs (60.0 / 90.0, 1.0e-3));
    REQUIRE ((int) n[0][ids::pitch] == 64);
}

//==============================================================================
TEST_CASE ("Import: a Studio One song", "[import]")
{
    TempFolder temp;
    writeWav (temp.dir.getChildFile ("Media/vox.wav"), 4.0);
    const juce::String song = R"(<?xml version="1.0" encoding="UTF-8"?>
<Song xmlns:x="http://www.presonus.com/x">
 <TempoMap x:id="TempoMap"><TempoMapSegment start="0" tempo="0.5"/></TempoMap>
 <TimeSignatureMap><TimeSignatureMapSegment start="0" numerator="4" denominator="4"/></TimeSignatureMap>
 <List x:id="Tracks">
  <MediaTrack name="Vox" mediaType="Audio" trackID="{1}"><List x:id="Events"><AudioEvent name="Vox take" start="1.5" length="2" offset="0.5" clipID="{c1}"/></List></MediaTrack>
  <MediaTrack name="Keys" mediaType="Music" trackID="{2}"><MusicPartEvent name="Part" start="0" length="2"><List x:id="Notes"><NoteEvent start="0.5" length="0.5" pitch="64" velocity="0.5"/></List></MusicPartEvent></MediaTrack>
 </List>
 <MarkerTrack><MarkerEvent name="Chorus" start="3"/></MarkerTrack>
</Song>)";
    const juce::String pool = R"(<MediaPool><AudioClip mediaID="{c1}"><Url x:id="path" url="file:///nowhere/vox.wav"/></AudioClip></MediaPool>)";
    const auto file = temp.dir.getChildFile ("Take.song");
    writeZip (file, { { "Song/song.xml", textBlock (song) }, { "Song/mediapool.xml", textBlock (pool) } });

    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, {}, notes, error);
    INFO (error);
    REQUIRE (project.isValid());
    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (120.0, 1.0e-9));
    const auto vox = regionsOf (trackNamed (project, "Vox"));
    REQUIRE (vox.size() == 1);
    REQUIRE_THAT ((double) vox[0][ids::start], WithinAbs (1.5, 1.0e-9));
    REQUIRE_THAT ((double) vox[0][ids::offset], WithinAbs (0.5, 1.0e-9));
    REQUIRE (juce::File (vox[0][ids::file].toString()).existsAsFile());
    const auto keys = regionsOf (trackNamed (project, "Keys"));
    REQUIRE (notesOf (keys[0]).size() == 1);
    REQUIRE_THAT ((double) notesOf (keys[0])[0][ids::start], WithinAbs (0.5, 1.0e-9));
    REQUIRE (model::sortedMarkers (project).size() == 1);
}

//==============================================================================
TEST_CASE ("Import: a Logic project's audio and settings", "[import]")
{
    TempFolder temp;
    const auto package = temp.dir.getChildFile ("Song.logicx");
    const auto alt = package.getChildFile ("Alternatives/000");
    alt.createDirectory();
    juce::File (TEST_DATA_DIR).getChildFile ("logic-metadata-binary.plist").copyFileTo (alt.getChildFile ("MetaData.plist"));
    writeWav (package.getChildFile ("Media/Audio Files/Vox#01.wav"), 1.0, 48000.0, (juce::int64) (3602 * 48000));   // stamped 2 s after bar 1
    writeWav (package.getChildFile ("Media/Audio Files/Gtr.wav"), 1.0, 48000.0);

    REQUIRE (projectimport::canImport (package));
    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (package, {}, notes, error);
    INFO (error);
    REQUIRE (project.isValid());
    REQUIRE_THAT ((double) project[ids::tempo], WithinAbs (96.0, 1.0e-9));
    REQUIRE ((int) project[ids::timeSigNumerator] == 3);
    REQUIRE (model::numTracks (project) == 2);
    const auto vox = regionsOf (trackNamed (project, "Vox#01"));
    REQUIRE_THAT ((double) vox[0][ids::start], WithinAbs (2.0, 1.0e-6));
    REQUIRE_THAT ((double) regionsOf (trackNamed (project, "Gtr"))[0][ids::start], WithinAbs (0.0, 1.0e-9));
}

TEST_CASE ("Import: a Cubase project's audio files", "[import]")
{
    TempFolder temp;
    writeWav (temp.dir.getChildFile ("Audio/Kick_01.wav"), 0.5);
    juce::MemoryOutputStream cpr;
    cpr.write ("RIFF\0\0\0\0NUNDROOT", 16);
    cpr.write ("\x01\x02PAudioClip\0", 13);
    cpr.write ("Kick_01.wav\0", 12);
    cpr.write ("\x05\x06Missing.wav\0", 14);
    const auto file = temp.dir.getChildFile ("Song.cpr");
    file.replaceWithData (cpr.getData(), cpr.getDataSize());

    juce::StringArray notes;
    juce::String error;
    const auto project = projectimport::open (file, {}, notes, error);
    INFO (error);
    REQUIRE (project.isValid());
    REQUIRE (model::numTracks (project) == 1);
    REQUIRE (trackNamed (project, "Kick_01").isValid());
}

//==============================================================================
TEST_CASE ("Import: Pro Tools sessions", "[import]")
{
    int opened = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File (TEST_DATA_DIR).getChildFile ("protools"), false, "*.pt*"))
    {
        interchange::Project p;
        juce::String error;
        const bool ok = projectimport::read (entry.getFile(), p, error);
        INFO (entry.getFile().getFileName() << ": " << error);
        if (! ok)
            continue;
        ++opened;
        REQUIRE (! p.tracks.empty());
        for (const auto& t : p.tracks)
            for (const auto& r : t.regions)
            {
                REQUIRE (r.start.value >= 0.0);
                REQUIRE (r.length.value > 0.0);
            }
        const auto project = projectimport::build (p, {});
        REQUIRE (model::numTracks (project) == (int) p.tracks.size());
    }
    REQUIRE (opened >= 4);
}

TEST_CASE ("Import: a Pro Tools stereo track becomes one stereo file", "[import]")
{
    TempFolder temp;
    const auto session = temp.dir.getChildFile ("TestPTX.ptx");
    juce::File (TEST_DATA_DIR).getChildFile ("protools/TestPTX.ptx").copyFileTo (session);

    // Without its files: two tracks, panned left and right
    {
        interchange::Project p;
        juce::String error;
        REQUIRE (projectimport::read (session, p, error));
        const auto left = std::find_if (p.tracks.begin(), p.tracks.end(), [] (auto& t) { return t.name == "stereoTone L"; });
        REQUIRE (left != p.tracks.end());
        REQUIRE (left->pan == -1.0);
    }

    writeWav (temp.dir.getChildFile ("Audio Files/stereoTone.L.wav"), 10.0, 48000.0);
    writeWav (temp.dir.getChildFile ("Audio Files/stereoTone.R.wav"), 10.0, 48000.0);
    writeWav (temp.dir.getChildFile ("Audio Files/monoTone.1.wav"), 10.0, 48000.0);
    interchange::Project p;
    juce::String error;
    REQUIRE (projectimport::read (session, p, error));
    const auto stereo = std::find_if (p.tracks.begin(), p.tracks.end(), [] (auto& t) { return t.name == "stereoTone"; });
    REQUIRE (stereo != p.tracks.end());
    REQUIRE (stereo->regions.size() == 1);
    REQUIRE (stereo->regions[0].file.existsAsFile());
    REQUIRE (stereo->regions[0].file.getParentDirectory().getFileName() == "TestPTX Stereo Files");
    REQUIRE (std::count_if (p.tracks.begin(), p.tracks.end(), [] (auto& t) { return t.name.startsWith ("stereoTone"); }) == 1);
    const auto mono = std::find_if (p.tracks.begin(), p.tracks.end(), [] (auto& t) { return t.name == "monoTone"; });
    REQUIRE (mono->regions[0].file.existsAsFile());
}

TEST_CASE ("Import: Pro Tools session summary", "[.ptinfo]")
{
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File (TEST_DATA_DIR).getChildFile ("protools"), false, "*.pt*"))
    {
        interchange::Project p;
        juce::String error;
        const bool ok = projectimport::read (entry.getFile(), p, error);
        std::cout << entry.getFile().getFileName() << (ok ? " OK" : (" FAILED " + error).toStdString()) << "\n";
        for (const auto& t : p.tracks)
        {
            std::cout << "  " << t.name << (t.kind == interchange::Track::Kind::instrument ? " (MIDI)" : "") << ": ";
            for (const auto& r : t.regions)
                std::cout << "[" << r.name << " @" << r.start.value << " len " << r.length.value << " off " << r.offsetSeconds << " notes " << r.notes.size() << "] ";
            std::cout << "\n";
        }
    }
}
