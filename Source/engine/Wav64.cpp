#include "Wav64.h"

namespace riff
{
namespace
{
    constexpr juce::uint32 tag (const char* s) noexcept
    {
        return (juce::uint32) (juce::uint8) s[0] | ((juce::uint32) (juce::uint8) s[1] << 8)
             | ((juce::uint32) (juce::uint8) s[2] << 16) | ((juce::uint32) (juce::uint8) s[3] << 24);
    }

    class Reader final : public juce::AudioFormatReader
    {
    public:
        explicit Reader (juce::InputStream* in) : AudioFormatReader (in, "WAV 64-bit float")
        {
            if ((juce::uint32) input->readInt() != tag ("RIFF"))
                return;
            input->readInt();
            if ((juce::uint32) input->readInt() != tag ("WAVE"))
                return;
            bool gotFormat = false;
            while (! input->isExhausted())
            {
                const auto id = (juce::uint32) input->readInt();
                const auto size = (juce::int64) (juce::uint32) input->readInt();
                const auto next = input->getPosition() + size + (size & 1);
                if (id == tag ("fmt "))
                {
                    auto format = (int) (juce::uint16) input->readShort();
                    numChannels = (unsigned int) (juce::uint16) input->readShort();
                    sampleRate = (double) (juce::uint32) input->readInt();
                    input->readInt();
                    input->readShort();
                    bitsPerSample = (unsigned int) (juce::uint16) input->readShort();
                    if (format == 0xfffe && size >= 40)
                    {
                        input->readShort();   // (cbSize)
                        input->readShort();   // (valid bits)
                        input->readInt();     // (channel mask)
                        format = (int) (juce::uint16) input->readShort();   // (the sub-format GUID's first two bytes)
                    }
                    gotFormat = format == 3 && bitsPerSample == 64;
                }
                else if (id == tag ("data") && gotFormat)
                {
                    dataStart = input->getPosition();
                    lengthInSamples = numChannels > 0 ? size / (juce::int64) (8 * numChannels) : 0;
                    usesFloatingPointData = true;
                    ok = sampleRate > 0 && numChannels > 0;
                    return;
                }
                if (! input->setPosition (next))
                    return;
            }
        }

        bool ok = false;

        bool readSamples (int* const* dest, int numDestChannels, int startOffsetInDestBuffer,
                          juce::int64 startSampleInFile, int numSamples) override
        {
            clearSamplesBeyondAvailableLength (dest, numDestChannels, startOffsetInDestBuffer, startSampleInFile, numSamples, lengthInSamples);
            if (numSamples <= 0)
                return true;
            input->setPosition (dataStart + startSampleInFile * 8 * (juce::int64) numChannels);
            std::vector<double> frame ((size_t) numChannels * 512);
            while (numSamples > 0)
            {
                const auto n = std::min (numSamples, 512);
                const auto want = (int) (n * 8 * (int) numChannels);
                const auto got = input->read (frame.data(), want);
                if (got < want)
                    std::fill ((char*) frame.data() + std::max (0, got), (char*) frame.data() + want, 0);
                for (int c = 0; c < numDestChannels; ++c)
                    if (auto* d = reinterpret_cast<float*> (dest[c]))
                    {
                        const auto src = std::min (c, (int) numChannels - 1);
                        for (int i = 0; i < n; ++i)
                            {
                            juce::uint64 bits;
                            std::memcpy (&bits, &frame[(size_t) (i * (int) numChannels + src)], 8);
                            bits = juce::ByteOrder::swapIfBigEndian (bits);
                            double v;
                            std::memcpy (&v, &bits, 8);
                            d[startOffsetInDestBuffer + i] = (float) v;
                        }
                    }
                startOffsetInDestBuffer += n;
                numSamples -= n;
            }
            return true;
        }

    private:
        juce::int64 dataStart = 0;
    };

    class Writer final : public juce::AudioFormatWriter
    {
    public:
        Writer (juce::OutputStream* out, double rate, int channels)
            : AudioFormatWriter (out, "WAV 64-bit float", rate, (unsigned int) channels, 64)
        {
            usesFloatingPointData = true;
            headerStart = output->getPosition();
            writeHeader();
        }

        ~Writer() override
        {
            if ((dataBytes & 1) != 0)
                output->writeByte (0);
            output->setPosition (headerStart);
            writeHeader();
            output->flush();
        }

        bool write (const int** data, int numSamples) override
        {
            std::vector<double> frame ((size_t) numChannels * (size_t) std::max (0, numSamples));
            for (int i = 0; i < numSamples; ++i)
                for (unsigned int c = 0; c < numChannels; ++c)
                {
                    const auto* src = reinterpret_cast<const float*> (data[c]);
                    const double v = src != nullptr ? (double) src[i] : 0.0;
                    juce::uint64 bits;
                    std::memcpy (&bits, &v, 8);
                    bits = juce::ByteOrder::swapIfBigEndian (bits);
                    std::memcpy (&frame[(size_t) i * numChannels + c], &bits, 8);
                }
            const auto bytes = frame.size() * 8;
            if (! output->write (frame.data(), bytes))
                return false;
            dataBytes += (juce::uint64) bytes;
            return true;
        }

    private:
        void writeHeader()
        {
            const auto frames = (juce::uint32) (dataBytes / (8 * std::max (1u, numChannels)));
            const auto data = (juce::uint32) std::min<juce::uint64> (dataBytes, 0xffffffe0u);
            output->writeInt ((int) tag ("RIFF"));
            output->writeInt ((int) (4 + (8 + 18) + (8 + 4) + 8 + data + (data & 1)));
            output->writeInt ((int) tag ("WAVE"));
            output->writeInt ((int) tag ("fmt "));
            output->writeInt (18);
            output->writeShort (3);
            output->writeShort ((short) numChannels);
            output->writeInt ((int) sampleRate);
            output->writeInt ((int) (sampleRate * 8 * numChannels));
            output->writeShort ((short) (8 * numChannels));
            output->writeShort (64);
            output->writeShort (0);
            output->writeInt ((int) tag ("fact"));
            output->writeInt (4);
            output->writeInt ((int) frames);
            output->writeInt ((int) tag ("data"));
            output->writeInt ((int) data);
        }

        juce::int64 headerStart = 0;
        juce::uint64 dataBytes = 0;
    };
}

juce::AudioFormatReader* Wav64AudioFormat::createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails)
{
    auto reader = std::make_unique<Reader> (source);
    if (reader->ok)
        return reader.release();
    if (! deleteStreamIfOpeningFails)
        reader->input = nullptr;
    return nullptr;
}

std::unique_ptr<juce::AudioFormatWriter> Wav64AudioFormat::createWriterFor (std::unique_ptr<juce::OutputStream>& stream,
                                                                            const juce::AudioFormatWriterOptions& options)
{
    if (stream == nullptr || options.getBitsPerSample() != 64 || options.getNumChannels() < 1)
        return nullptr;
    return std::make_unique<Writer> (stream.release(), options.getSampleRate(), options.getNumChannels());
}

void registerAudioFormats (juce::AudioFormatManager& manager)
{
    manager.registerBasicFormats();
    manager.registerFormat (new Wav64AudioFormat(), false);
}

juce::String extensionForFileType (const juce::String& fileType)
{
    if (fileType == "aiff") return ".aif";
    if (fileType == "flac") return ".flac";
    return ".wav";
}

std::unique_ptr<juce::AudioFormatWriter> createAudioWriter (const juce::String& fileType, int bits, double sampleRate,
                                                            int numChannels, std::unique_ptr<juce::OutputStream>& stream)
{
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (numChannels);
    if (fileType == "flac")
    {
        juce::FlacAudioFormat flac;
        return flac.createWriterFor (stream, options.withBitsPerSample (bits <= 16 ? 16 : 24));
    }
    if (fileType == "aiff")
    {
        juce::AiffAudioFormat aiff;
        return aiff.createWriterFor (stream, options.withBitsPerSample (bits <= 16 ? 16 : bits == 24 ? 24 : 32));
    }
    if (bits >= 64)
    {
        Wav64AudioFormat wav64;
        return wav64.createWriterFor (stream, options.withBitsPerSample (64));
    }
    juce::WavAudioFormat wav;
    auto o = options.withBitsPerSample (bits <= 16 ? 16 : bits == 24 ? 24 : 32);
    if (bits == 32)
        o = o.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    return wav.createWriterFor (stream, o);
}
}
