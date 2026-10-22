#include "StemSeparator.h"
#include "Resampler.h"

#define ORT_API_MANUAL_INIT
#include "onnxruntime_cxx_api.h"

namespace riff::stems
{
namespace
{
    constexpr int order = 12;   // 4096

    const std::vector<float>& hann()
    {
        // periodic (torch.hann_window's default): N + 1 points, the last dropped
        static const std::vector<float> w = []
        {
            std::vector<float> v (nfft);
            for (int n = 0; n < nfft; ++n)
                v[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) nfft));
            return v;
        }();
        return w;
    }

    /** Reflect padding as torch's (the edge sample not repeated). */
    float reflected (const float* x, int length, int i)
    {
        if (length <= 1)
            return length == 1 ? x[0] : 0.0f;
        const auto period = 2 * (length - 1);
        i = ((i % period) + period) % period;
        return x[i < length ? i : period - i];
    }
}

Spectrogram stft (const float* x, int length)
{
    Spectrogram s;
    s.frames = length / hop + 1;
    s.bins = bins;
    s.data.assign ((size_t) s.frames * (size_t) bins, {});
    juce::dsp::FFT fft (order);
    std::vector<float> buffer ((size_t) nfft * 2);
    const auto& w = hann();
    const auto scale = 1.0f / std::sqrt ((float) nfft);   // (normalized=True)
    for (int f = 0; f < s.frames; ++f)
    {
        const auto start = f * hop - nfft / 2;   // (center=True: the signal padded by half a window, reflected)
        for (int n = 0; n < nfft; ++n)
        {
            const auto i = start + n;
            buffer[(size_t) n] = (i >= 0 && i < length ? x[i] : reflected (x, length, i)) * w[(size_t) n];
        }
        std::fill (buffer.begin() + nfft, buffer.end(), 0.0f);
        fft.performRealOnlyForwardTransform (buffer.data(), true);
        for (int b = 0; b < bins; ++b)
            s.at (f, b) = std::complex<float> (buffer[(size_t) (2 * b)], buffer[(size_t) (2 * b + 1)]) * scale;
    }
    return s;
}

std::vector<float> istft (const Spectrogram& z, int length)
{
    // Overlap-add of the inverse frames, windowed, over the sum of the squared windows (torch.istft)
    const auto padded = nfft + hop * (z.frames - 1);
    std::vector<float> out ((size_t) padded, 0.0f), envelope ((size_t) padded, 0.0f), buffer ((size_t) nfft * 2);
    juce::dsp::FFT fft (order);
    const auto& w = hann();
    const auto scale = std::sqrt ((float) nfft);
    for (int f = 0; f < z.frames; ++f)
    {
        for (int b = 0; b < bins; ++b)
        {
            const auto c = z.at (f, b) * scale;
            buffer[(size_t) (2 * b)] = c.real();
            buffer[(size_t) (2 * b + 1)] = c.imag();
        }
        for (int b = bins; b < nfft; ++b)
        {
            // (the mirrored half, as a real signal's)
            const auto c = std::conj (z.at (f, nfft - b)) * scale;
            buffer[(size_t) (2 * b)] = c.real();
            buffer[(size_t) (2 * b + 1)] = c.imag();
        }
        fft.performRealOnlyInverseTransform (buffer.data());   // (scaled by 1/N)
        const auto start = f * hop;
        for (int n = 0; n < nfft; ++n)
        {
            out[(size_t) (start + n)] += buffer[(size_t) n] * w[(size_t) n];
            envelope[(size_t) (start + n)] += w[(size_t) n] * w[(size_t) n];
        }
    }
    std::vector<float> y ((size_t) length, 0.0f);
    for (int i = 0; i < length; ++i)
    {
        const auto at = (size_t) (i + nfft / 2);   // (center: the half window taken back off)
        if (at < out.size() && envelope[at] > 1.0e-11f)
            y[(size_t) i] = out[at] / envelope[at];
    }
    return y;
}

juce::StringArray sourceNames (int count)
{
    if (count == 6)
        return { "Drums", "Bass", "Other", "Vocals", "Guitar", "Piano" };
    return { "Drums", "Bass", "Other", "Vocals" };
}

//==============================================================================
struct Separator::Session
{
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames, outputNames;
    int sourceCount = 0;
    int specOutput = 0, waveOutput = 1, waveInput = 0, specInput = 1;
};

namespace
{
    /** The runtime library, loaded once for the life of the app (its API table is global). */
    bool loadRuntime (const juce::File& library, juce::String& error)
    {
        static juce::DynamicLibrary lib;
        static bool loaded = false;
        if (loaded)
            return true;
        if (! lib.open (library.getFullPathName()))
        {
            error = "Couldn't load the separation engine (" + library.getFileName() + ").";
            return false;
        }
        using GetBase = const OrtApiBase* (*)();
        auto getBase = reinterpret_cast<GetBase> (lib.getFunction ("OrtGetApiBase"));
        const auto* api = getBase != nullptr ? getBase()->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            error = "The separation engine is a version this app can't use.";
            return false;
        }
        Ort::InitApi (api);
        loaded = true;
        return true;
    }
}

Separator::Separator() = default;
Separator::~Separator() = default;

bool Separator::isLoaded() const noexcept { return session != nullptr && session->session != nullptr; }
int Separator::sources() const noexcept { return session != nullptr ? session->sourceCount : 0; }

bool Separator::load (const juce::File& runtimeLibrary, const juce::File& model, juce::String& error)
{
    if (! loadRuntime (runtimeLibrary, error))
        return false;
    try
    {
        auto s = std::make_unique<Session>();
        s->env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "AudioTechPro");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (std::max (1, juce::SystemStats::getNumCpus() - 1));
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
       #if JUCE_WINDOWS
        s->session = std::make_unique<Ort::Session> (*s->env, model.getFullPathName().toWideCharPointer(), options);
       #else
        s->session = std::make_unique<Ort::Session> (*s->env, model.getFullPathName().toRawUTF8(), options);
       #endif
        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < s->session->GetInputCount(); ++i)
        {
            s->inputNames.push_back (s->session->GetInputNameAllocated (i, allocator).get());
            const auto rank = s->session->GetInputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape().size();
            (rank == 3 ? s->waveInput : s->specInput) = (int) i;
        }
        for (size_t i = 0; i < s->session->GetOutputCount(); ++i)
        {
            s->outputNames.push_back (s->session->GetOutputNameAllocated (i, allocator).get());
            const auto shape = s->session->GetOutputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() == 5) { s->specOutput = (int) i; s->sourceCount = (int) shape[1]; }
            else s->waveOutput = (int) i;
        }
        if (s->inputNames.size() != 2 || s->outputNames.size() != 2 || s->sourceCount < 1)
        {
            error = "The separation model isn't one this app knows.";
            return false;
        }
        session = std::move (s);
        return true;
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("The separation model couldn't be opened: ") + e.what();
        return false;
    }
}

/** One segment through the network: the padded mix (2 x paddedLength) in, each source's segment (S x 2 x segmentSamples) out. */
bool Separator::runSegment (const std::vector<float>& paddedMix, int paddedLength, int pad, std::vector<float>& out, juce::String& error)
{
    const auto le = (int) std::ceil ((double) segmentSamples / hop);   // 336 frames
    const auto freq = nfft / 2;                                          // 2048 bins (the last dropped)
    const auto S = session->sourceCount;

    // The spectrogram, complex as channels: [1, 4, 2048, le]; the frames 2..2+le of the padded segment's
    std::vector<float> specIn ((size_t) 4 * freq * le);
    std::vector<Spectrogram> spectra;
    for (int c = 0; c < 2; ++c)
    {
        spectra.push_back (stft (paddedMix.data() + (size_t) c * (size_t) paddedLength, paddedLength));
        const auto& z = spectra.back();
        jassert (z.frames == le + 4);
        for (int b = 0; b < freq; ++b)
            for (int t = 0; t < le; ++t)
            {
                const auto v = z.at (t + 2, b);
                specIn[((size_t) (2 * c) * freq + (size_t) b) * le + (size_t) t] = v.real();
                specIn[((size_t) (2 * c + 1) * freq + (size_t) b) * le + (size_t) t] = v.imag();
            }
    }
    // The waveform: [1, 2, segmentSamples], from the padded segment's middle
    std::vector<float> waveIn ((size_t) 2 * segmentSamples);
    for (int c = 0; c < 2; ++c)
        std::copy_n (paddedMix.data() + (size_t) c * (size_t) paddedLength + (size_t) pad, segmentSamples, waveIn.data() + (size_t) c * segmentSamples);

    try
    {
        auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> specShape { 1, 4, freq, le };
        const std::array<int64_t, 3> waveShape { 1, 2, segmentSamples };
        std::array<Ort::Value, 2> inputs { Ort::Value (nullptr), Ort::Value (nullptr) };
        inputs[(size_t) session->specInput] = Ort::Value::CreateTensor<float> (memory, specIn.data(), specIn.size(), specShape.data(), specShape.size());
        inputs[(size_t) session->waveInput] = Ort::Value::CreateTensor<float> (memory, waveIn.data(), waveIn.size(), waveShape.data(), waveShape.size());
        std::vector<const char*> inNames, outNames;
        for (auto& n : session->inputNames) inNames.push_back (n.c_str());
        for (auto& n : session->outputNames) outNames.push_back (n.c_str());
        auto results = session->session->Run (Ort::RunOptions { nullptr }, inNames.data(), inputs.data(), inputs.size(), outNames.data(), outNames.size());
        const auto* specOut = results[(size_t) session->specOutput].GetTensorData<float>();   // [1, S, 4, 2048, le]
        const auto* waveOut = results[(size_t) session->waveOutput].GetTensorData<float>();   // [1, S, 2, segmentSamples]

        out.assign ((size_t) S * 2 * segmentSamples, 0.0f);
        for (int s = 0; s < S; ++s)
            for (int c = 0; c < 2; ++c)
            {
                // the frequency branch back to sound (padded as the input was), plus the time branch
                Spectrogram z;
                z.frames = le + 4;
                z.bins = bins;
                z.data.assign ((size_t) z.frames * bins, {});
                for (int b = 0; b < freq; ++b)
                    for (int t = 0; t < le; ++t)
                    {
                        const auto base = ((size_t) s * 4 + (size_t) (2 * c)) * freq;
                        const auto re = specOut[(base + (size_t) b) * le + (size_t) t];
                        const auto im = specOut[(base + freq + (size_t) b) * le + (size_t) t];
                        z.at (t + 2, b) = { re, im };
                    }
                const auto y = istft (z, paddedLength);
                auto* dest = out.data() + ((size_t) s * 2 + (size_t) c) * segmentSamples;
                const auto* wave = waveOut + ((size_t) s * 2 + (size_t) c) * segmentSamples;
                for (int k = 0; k < segmentSamples; ++k)
                    dest[k] = y[(size_t) (pad + k)] + wave[k];
            }
        return true;
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("Separation failed: ") + e.what();
        return false;
    }
}

std::vector<juce::AudioBuffer<float>> Separator::separate (const juce::AudioBuffer<float>& mix, const std::function<bool (float)>& progress,
                                                           juce::String& error)
{
    if (! isLoaded())
    {
        error = "The separation model isn't loaded.";
        return {};
    }
    const auto mixLength = mix.getNumSamples();
    const auto S = session->sourceCount;
    const auto right = mix.getNumChannels() > 1 ? 1 : 0;
    // Half a second of silence on both sides (Demucs's shift padding): the segments' edges, where the
    // cropped STFT frames can't rebuild the sound, never fall on the music
    const auto margin = sampleRate / 2;
    const auto length = mixLength + 2 * margin;
    const auto sample = [&mix, mixLength, margin] (int c, int i) { i -= margin; return i >= 0 && i < mixLength ? mix.getSample (c, i) : 0.0f; };

    // Normalized by the mono mix's mean and spread (as Demucs does), then the segments
    double mean = 0.0;
    for (int i = 0; i < mixLength; ++i)
        mean += 0.5 * ((double) mix.getSample (0, i) + mix.getSample (right, i));
    mean /= std::max (1, mixLength);
    double variance = 0.0;
    for (int i = 0; i < mixLength; ++i)
        variance += juce::square (0.5 * ((double) mix.getSample (0, i) + mix.getSample (right, i)) - mean);
    const auto spread = (float) std::max (1.0e-8, std::sqrt (variance / std::max (1, mixLength - 1)));

    const auto le = (int) std::ceil ((double) segmentSamples / hop);
    const auto pad = hop / 2 * 3;                          // 1536
    const auto padEnd = pad + le * hop - segmentSamples;
    const auto paddedLength = segmentSamples + pad + padEnd;
    const auto stride = (int) ((1.0 - 0.25) * segmentSamples);

    // The triangular weights the overlapping segments are blended with
    std::vector<float> weight ((size_t) segmentSamples);
    const auto half = segmentSamples / 2;
    for (int i = 0; i < half; ++i)
        weight[(size_t) i] = weight[(size_t) (segmentSamples - 1 - i)] = (float) (i + 1) / (float) half;
    if (segmentSamples % 2 == 1)
        weight[(size_t) half] = 1.0f;

    std::vector<float> outAll ((size_t) S * 2 * (size_t) length, 0.0f), weights ((size_t) length, 0.0f);
    std::vector<float> padded ((size_t) 2 * paddedLength), segmentOut;
    const auto chunks = std::max (1, (int) std::ceil ((double) length / stride));
    int done = 0;
    for (int offset = 0; offset < length; offset += stride)
    {
        const auto chunkLength = std::min (segmentSamples, length - offset);
        const auto symmetric = (segmentSamples - chunkLength) / 2;   // (a short last chunk sits in the middle of its segment)
        std::fill (padded.begin(), padded.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            auto* row = padded.data() + (size_t) c * paddedLength;
            const auto channel = c == 0 ? 0 : right;
            for (int k = 0; k < chunkLength; ++k)
            {
                const auto i = offset + k;
                // (the silent margins are silence after the normalizing too: zeros, as Demucs pads)
                row[pad + symmetric + k] = i >= margin && i < margin + mixLength ? (sample (channel, i) - (float) mean) / spread : 0.0f;
            }
            // reflect around the segment's edges, for the STFT's edges
            for (int i = 0; i < pad; ++i)
                row[pad - 1 - i] = row[pad + i];
            for (int i = 0; i < padEnd; ++i)
                row[pad + segmentSamples + i] = row[pad + segmentSamples - 1 - i];
        }
        if (! runSegment (padded, paddedLength, pad, segmentOut, error))
            return {};
        for (int s = 0; s < S; ++s)
            for (int c = 0; c < 2; ++c)
            {
                auto* dest = outAll.data() + ((size_t) s * 2 + (size_t) c) * (size_t) length + (size_t) offset;
                const auto* src = segmentOut.data() + ((size_t) s * 2 + (size_t) c) * segmentSamples + (size_t) symmetric;
                for (int k = 0; k < chunkLength; ++k)
                    dest[k] += weight[(size_t) k] * src[k];
            }
        for (int k = 0; k < chunkLength; ++k)
            weights[(size_t) (offset + k)] += weight[(size_t) k];
        if (progress != nullptr && ! progress ((float) ++done / (float) chunks))
            return {};
    }

    std::vector<juce::AudioBuffer<float>> result;
    for (int s = 0; s < S; ++s)
    {
        juce::AudioBuffer<float> b (2, mixLength);
        for (int c = 0; c < 2; ++c)
        {
            const auto* src = outAll.data() + ((size_t) s * 2 + (size_t) c) * (size_t) length;
            auto* dest = b.getWritePointer (c);
            for (int k = 0; k < mixLength; ++k)
                dest[k] = src[k + margin] / std::max (1.0e-8f, weights[(size_t) (k + margin)]) * spread + (float) mean;
        }
        result.push_back (std::move (b));
    }
    return result;
}

// --- Files and downloads -------------------------------------------------------------------------------

namespace
{
    juce::File& folderOverride() { static juce::File f; return f; }
}

juce::File Files::folder()
{
    if (folderOverride() != juce::File())
        return folderOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Audio Tech Pro").getChildFile ("Stem Separation");
}

void Files::setFolderForTests (const juce::File& f) { folderOverride() = f; }

juce::String Files::runtimeName()
{
   #if JUCE_MAC
    return "libonnxruntime.dylib";
   #elif JUCE_WINDOWS
    return "onnxruntime.dll";
   #else
    return "libonnxruntime.so";
   #endif
}

juce::String Files::modelName (bool sixStems) { return sixStems ? "htdemucs_6s.onnx" : "htdemucs.onnx"; }

juce::String Files::baseUrl() { return "https://github.com/Herigama89/audio-tech-pro/releases/download/stem-models-v1/"; }

juce::String downloadFile (const juce::URL& url, const juce::File& dest, const std::function<bool (float)>& progress)
{
    int status = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (20000)
                                             .withNumRedirectsToFollow (8)
                                             .withStatusCode (&status));
    if (stream == nullptr)
        return "Couldn't reach " + url.getDomain() + ". Check the internet connection.";
    if (status != 0 && (status < 200 || status >= 300))
        return "The download failed (HTTP " + juce::String (status) + ").";

    if (! dest.getParentDirectory().createDirectory())
        return "Couldn't create " + dest.getParentDirectory().getFullPathName();
    const auto temp = dest.getSiblingFile (dest.getFileName() + ".part");
    temp.deleteFile();
    juce::String error;
    {
        // closed before anything is deleted or moved (Windows can't delete an open file)
        juce::FileOutputStream out (temp);
        if (! out.openedOk())
            return "Couldn't write " + temp.getFullPathName();
        const auto total = stream->getTotalLength();
        juce::HeapBlock<char> chunk (1 << 16);
        juce::int64 done = 0;
        for (;;)
        {
            const auto n = stream->read (chunk, 1 << 16);
            if (n < 0) { error = "The download was cut off."; break; }
            if (n == 0)
                break;
            if (! out.write (chunk, (size_t) n)) { error = "Couldn't write " + temp.getFullPathName() + " (is the disk full?)"; break; }
            done += n;
            if (progress && ! progress (total > 0 ? (float) ((double) done / (double) total) : 0.0f)) { error = "cancelled"; break; }
            if (stream->isExhausted())
                break;
        }
        out.flush();
        if (error.isEmpty() && total > 0 && done != total)
            error = "The download was cut off.";
        if (error.isEmpty() && done < 1024)
            error = "The download was empty.";
    }
    if (error.isNotEmpty())
    {
        temp.deleteFile();
        return error;
    }
    dest.deleteFile();
    if (! temp.moveFileTo (dest))
        return "Couldn't move the download into " + dest.getFullPathName();
    return {};
}

juce::String download (bool sixStems, const std::function<bool (float, const juce::String&)>& progress)
{
    struct Item { juce::File file; juce::String what; };
    std::vector<Item> items;
    if (! Files::runtime().existsAsFile())
        items.push_back ({ Files::runtime(), "ONNX Runtime" });
    if (! Files::model (sixStems).existsAsFile())
        items.push_back ({ Files::model (sixStems), sixStems ? "the 6-stem model" : "the 4-stem model" });

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto error = downloadFile (juce::URL (Files::baseUrl() + item.file.getFileName()), item.file, [&] (float p)
        {
            return ! progress || progress (((float) i + p) / (float) items.size(), "Downloading " + item.what + "...");
        });
        if (error.isNotEmpty())
            return error;
    }
    if (! items.empty())
    {
        // ONNX Runtime's licence, kept next to it (best effort)
        const auto licence = Files::folder().getChildFile ("onnxruntime-LICENSE.txt");
        if (! licence.existsAsFile())
            downloadFile (juce::URL (Files::baseUrl() + licence.getFileName()), licence, {});
    }
    return {};
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& buffer, double fromRate, double toRate)
{
    const auto inLength = buffer.getNumSamples();
    const auto channels = buffer.getNumChannels();
    if (inLength == 0 || channels == 0)
        return juce::AudioBuffer<float> (2, 0);

    if (std::abs (fromRate - toRate) < 0.5)
    {
        juce::AudioBuffer<float> out (2, inLength);
        out.copyFrom (0, 0, buffer, 0, 0, inLength);
        out.copyFrom (1, 0, buffer, juce::jmin (1, channels - 1), 0, inLength);
        return out;
    }

    const auto ratio = fromRate / toRate;
    const auto outLength = (int) std::floor ((double) inLength / ratio);
    juce::AudioBuffer<float> out (2, outLength);
    out.clear();
    resampler::initialise();
    resampler::addResampled (buffer, 0.0, ratio, 1.0f, out.getWritePointer (0), out.getWritePointer (1), outLength);
    return out;
}

} // namespace riff::stems
