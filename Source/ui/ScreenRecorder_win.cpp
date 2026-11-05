// Screen recording on Windows: the picture copied from the screen (GDI, with the mouse pointer drawn in),
// the DAW's own mix (via ScreenAudioTap) for the sound, and Media Foundation's Sink Writer for the MP4
// (H.264 video, AAC audio; the hardware encoder when the graphics card has one).

#include "ScreenRecorder.h"

#if JUCE_WINDOWS

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <vector>

#if defined (_MSC_VER)
 #pragma comment (lib, "mfplat.lib")
 #pragma comment (lib, "mfreadwrite.lib")
 #pragma comment (lib, "mfuuid.lib")
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "gdi32.lib")
 #pragma comment (lib, "user32.lib")
#endif

namespace riff
{
namespace
{
    template <typename T>
    struct ComPtr
    {
        T* p = nullptr;
        ComPtr() = default;
        ComPtr (const ComPtr&) = delete;
        ComPtr& operator= (const ComPtr&) = delete;
        ~ComPtr() { reset(); }
        void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
        T** operator&() { reset(); return &p; }
        T* operator->() const { return p; }
        T* get() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    juce::String hr (const juce::String& what, HRESULT code)
    {
        return juce::String (what) + " (error 0x" + juce::String::toHexString ((juce::int64) (juce::uint32) code) + ")";
    }

    /** One recording: its own thread copies the screen, takes the sound and writes the file. */
    class WindowsRecorder final : public ScreenRecorder
    {
    public:
        ~WindowsRecorder() override
        {
            stopFlag.store (true);
            userStopped.store (true);
            if (worker.joinable())
                worker.join();
        }

        bool isAvailable() const override { return true; }
        bool isRecording() const override { return recording.load(); }
        juce::Point<int> getVideoSize() const override { return { outW, outH }; }

        juce::String takeFailure() override
        {
            const juce::ScopedLock sl (failureLock);
            return std::exchange (failure, juce::String());
        }

        void start (const Options& options, ScreenAudioTap& theTap, std::function<void (const juce::String&)> started) override
        {
            if (recording.load())
            {
                if (started) started ("A recording is already running.");
                return;
            }
            if (worker.joinable())
                worker.join();   // (the last one, finished)

            // What to record: the display showing the app, or the app's window
            HWND window = nullptr;
            if (options.window != nullptr)
                if (auto* peer = options.window->getPeer())
                    window = GetAncestor ((HWND) peer->getNativeHandle(), GA_ROOT);

            RECT area {};
            if (options.wholeScreen || window == nullptr)
            {
                MONITORINFO info {};
                info.cbSize = sizeof (info);
                GetMonitorInfo (MonitorFromWindow (window, MONITOR_DEFAULTTOPRIMARY), &info);
                area = info.rcMonitor;
                followWindow = nullptr;
            }
            else
            {
                GetWindowRect (window, &area);
                followWindow = window;
            }
            capW = juce::jmax (2, (int) (area.right - area.left));
            capH = juce::jmax (2, (int) (area.bottom - area.top));
            capX = area.left;
            capY = area.top;

            const auto size = fitSize (capW, capH, options.maxHeight);
            outW = size.x;
            outH = size.y;
            fps = juce::jlimit (10, 60, options.frameRate);
            rate = options.sampleRate > 0.0 ? options.sampleRate : 48000.0;
            file = options.file.withFileExtension (".mp4");
            tap = &theTap;
            stopFlag.store (false);
            userStopped.store (false);
            finishError.clear();
            {
                const juce::ScopedLock sl (finishLock);
                onFinished = nullptr;
            }
            {
                const juce::ScopedLock sl (failureLock);
                failure.clear();
            }

            worker = std::thread ([this, started]
            {
                const auto error = open();
                if (error.isNotEmpty())
                {
                    close (false);
                    juce::MessageManager::callAsync ([started, error] { if (started) started (error); });
                    return;
                }
                recording.store (true);
                juce::MessageManager::callAsync ([started] { if (started) started ({}); });
                run();
                close (true);

                // Stopped by a failure: it counts as recording until the app asks it to stop (and hears why)
                while (! userStopped.load())
                    Sleep (20);
                std::function<void (const juce::File&, const juce::String&)> done;
                {
                    const juce::ScopedLock sl (finishLock);
                    done = std::move (onFinished);
                }
                const auto finishedError = finishError;
                const auto result = file;
                recording.store (false);
                if (done)
                    juce::MessageManager::callAsync ([done, finishedError, result]
                                                     { done (finishedError.isEmpty() ? result : juce::File(), finishedError); });
            });
        }

        void stop (std::function<void (const juce::File&, const juce::String&)> finished) override
        {
            // (the file is finished on the recording's thread; then the message thread hears)
            {
                const juce::ScopedLock sl (finishLock);
                onFinished = std::move (finished);
            }
            stopFlag.store (true);
            userStopped.store (true);
        }

    private:
        // --- The file ------------------------------------------------------------------------------------
        juce::String open()
        {
            if (FAILED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)))
                return "Windows couldn't start the video encoder (COM).";
            comStarted = true;
            if (const auto r = MFStartup (MF_VERSION, MFSTARTUP_FULL); FAILED (r))
                return hr ("Windows' Media Foundation isn't available (on Windows N editions, install the Media Feature Pack)", r);
            mfStarted = true;

            file.deleteFile();
            ComPtr<IMFAttributes> attributes;
            MFCreateAttributes (&attributes, 4);
            attributes->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);   // (the graphics card's encoder, if there's one)
            attributes->SetGUID (MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
            const auto path = file.getFullPathName();
            if (const auto r = MFCreateSinkWriterFromURL (path.toWideCharPointer(), nullptr, attributes.get(), &writer); FAILED (r))
                return hr ("Couldn't create the video file " + path, r);

            // Video: H.264, from top-down 32-bit RGB frames
            {
                ComPtr<IMFMediaType> type;
                MFCreateMediaType (&type);
                type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
                type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
                type->SetUINT32 (MF_MT_AVG_BITRATE, (UINT32) bitrateFor ({ outW, outH }, fps));
                type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                MFSetAttributeSize (type.get(), MF_MT_FRAME_SIZE, (UINT32) outW, (UINT32) outH);
                MFSetAttributeRatio (type.get(), MF_MT_FRAME_RATE, (UINT32) fps, 1);
                MFSetAttributeRatio (type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                if (const auto r = writer->AddStream (type.get(), &videoStream); FAILED (r))
                    return hr ("This computer can't encode H.264 video at " + juce::String (outW) + " x " + juce::String (outH)
                               + " (try a smaller size)", r);
            }
            {
                ComPtr<IMFMediaType> type;
                MFCreateMediaType (&type);
                type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
                type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                type->SetUINT32 (MF_MT_DEFAULT_STRIDE, (UINT32) (outW * 4));   // (positive: top-down rows)
                MFSetAttributeSize (type.get(), MF_MT_FRAME_SIZE, (UINT32) outW, (UINT32) outH);
                MFSetAttributeRatio (type.get(), MF_MT_FRAME_RATE, (UINT32) fps, 1);
                MFSetAttributeRatio (type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                if (const auto r = writer->SetInputMediaType (videoStream, type.get(), nullptr); FAILED (r))
                    return hr ("The video encoder doesn't take the screen's pictures", r);
            }

            // Audio: AAC (44.1 or 48 kHz; other rates are converted to 48 kHz here)
            audioRate = (std::abs (rate - 44100.0) < 1.0) ? 44100 : 48000;
            {
                ComPtr<IMFMediaType> type;
                MFCreateMediaType (&type);
                type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
                type->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                type->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) audioRate);
                type->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
                type->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);   // 192 kbps
                if (const auto r = writer->AddStream (type.get(), &audioStream); FAILED (r))
                    return hr ("This computer can't encode AAC audio", r);
            }
            {
                ComPtr<IMFMediaType> type;
                MFCreateMediaType (&type);
                type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
                type->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                type->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) audioRate);
                type->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
                type->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
                type->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (audioRate * 4));
                if (const auto r = writer->SetInputMediaType (audioStream, type.get(), nullptr); FAILED (r))
                    return hr ("The audio encoder doesn't take the mix", r);
            }

            if (const auto r = writer->BeginWriting(); FAILED (r))
                return hr ("Couldn't start writing the video", r);

            // The screen copy: a top-down 32-bit picture the size of the video
            screenDC = GetDC (nullptr);
            memDC = CreateCompatibleDC (screenDC);
            BITMAPINFO bmi {};
            bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = outW;
            bmi.bmiHeader.biHeight = -outH;   // (negative: top-down)
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            dib = CreateDIBSection (memDC, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (dib == nullptr || pixels == nullptr)
                return "Not enough memory to copy the screen.";
            oldBitmap = SelectObject (memDC, dib);
            SetStretchBltMode (memDC, HALFTONE);
            SetBrushOrgEx (memDC, 0, 0, nullptr);
            return {};
        }

        void close (bool finalise)
        {
            if (writer)
            {
                if (finalise)
                    if (const auto r = writer->Finalize(); FAILED (r))
                        finishError = hr ("The video couldn't be finished", r);
                writer.reset();
            }
            if (memDC != nullptr)
            {
                if (oldBitmap != nullptr) SelectObject (memDC, oldBitmap);
                DeleteDC (memDC);
                memDC = nullptr;
            }
            if (dib != nullptr) { DeleteObject (dib); dib = nullptr; pixels = nullptr; }
            if (screenDC != nullptr) { ReleaseDC (nullptr, screenDC); screenDC = nullptr; }
            if (mfStarted) { MFShutdown(); mfStarted = false; }
            if (comStarted) { CoUninitialize(); comStarted = false; }
        }

        void fail (const juce::String& why)
        {
            const juce::ScopedLock sl (failureLock);
            if (failure.isEmpty())
                failure = why;
            stopFlag.store (true);
        }

        // --- Recording ------------------------------------------------------------------------------------
        void run()
        {
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            const auto frameTime = (LONGLONG) (10000000LL / fps);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::int64 frame = 0;

            while (! stopFlag.load())
            {
                // The next frame, on time (a slow frame is skipped rather than everything going late)
                const auto now = juce::Time::getMillisecondCounterHiRes() - start;
                const auto due = (double) frame * 1000.0 / fps;
                if (now < due)
                {
                    writeAudio();
                    const auto wait = due - (juce::Time::getMillisecondCounterHiRes() - start);
                    if (wait > 1.5)
                        Sleep ((DWORD) (wait - 1.0));
                    continue;
                }
                if (now - due > 3.0 * 1000.0 / fps)
                    frame = (juce::int64) (now * fps / 1000.0);

                capture();
                if (! writeVideo (frame * frameTime, frameTime))
                    break;
                ++frame;
                writeAudio();
            }
            writeAudio();
        }

        void capture()
        {
            // The window may have moved: follow it (its size stays as it was when recording started)
            if (followWindow != nullptr && IsWindow (followWindow))
            {
                RECT r {};
                if (GetWindowRect (followWindow, &r) && ! IsIconic (followWindow))
                {
                    capX = r.left;
                    capY = r.top;
                }
            }

            if (outW == capW && outH == capH)
                BitBlt (memDC, 0, 0, outW, outH, screenDC, capX, capY, SRCCOPY | CAPTUREBLT);
            else
                StretchBlt (memDC, 0, 0, outW, outH, screenDC, capX, capY, capW, capH, SRCCOPY | CAPTUREBLT);

            // The mouse pointer (the screen copy doesn't have it)
            CURSORINFO cursor {};
            cursor.cbSize = sizeof (cursor);
            if (GetCursorInfo (&cursor) && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr)
            {
                ICONINFO icon {};
                if (GetIconInfo (cursor.hCursor, &icon))
                {
                    const auto sx = (double) outW / capW, sy = (double) outH / capH;
                    BITMAP bm {};
                    int w = 32, h = 32;
                    if (icon.hbmColor != nullptr && GetObject (icon.hbmColor, sizeof (bm), &bm)) { w = bm.bmWidth; h = bm.bmHeight; }
                    else if (icon.hbmMask != nullptr && GetObject (icon.hbmMask, sizeof (bm), &bm)) { w = bm.bmWidth; h = bm.bmHeight / 2; }
                    const auto x = (int) std::lround ((cursor.ptScreenPos.x - capX - (LONG) icon.xHotspot) * sx);
                    const auto y = (int) std::lround ((cursor.ptScreenPos.y - capY - (LONG) icon.yHotspot) * sy);
                    DrawIconEx (memDC, x, y, cursor.hCursor, juce::jmax (1, (int) std::lround (w * sx)), juce::jmax (1, (int) std::lround (h * sy)),
                                0, nullptr, DI_NORMAL);
                    if (icon.hbmColor != nullptr) DeleteObject (icon.hbmColor);
                    if (icon.hbmMask != nullptr) DeleteObject (icon.hbmMask);
                }
            }
            GdiFlush();
        }

        bool writeVideo (LONGLONG time, LONGLONG duration)
        {
            const auto bytes = (DWORD) (outW * outH * 4);
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED (MFCreateMemoryBuffer (bytes, &buffer)))
            {
                fail ("Out of memory while recording the screen.");
                return false;
            }
            BYTE* data = nullptr;
            if (SUCCEEDED (buffer->Lock (&data, nullptr, nullptr)))
            {
                std::memcpy (data, pixels, bytes);
                buffer->Unlock();
            }
            buffer->SetCurrentLength (bytes);
            ComPtr<IMFSample> sample;
            MFCreateSample (&sample);
            sample->AddBuffer (buffer.get());
            sample->SetSampleTime (time);
            sample->SetSampleDuration (duration);
            if (const auto r = writer->WriteSample (videoStream, sample.get()); FAILED (r))
            {
                fail (hr ("Writing the video failed (is the disk full?)", r));
                return false;
            }
            return true;
        }

        void writeAudio()
        {
            if (tap == nullptr)
                return;
            constexpr int block = 4096;
            left.resize (block);
            right.resize (block);
            for (;;)
            {
                const auto got = tap->pull (left.data(), right.data(), block);
                if (got <= 0)
                    return;

                // To the encoder's rate when the session runs at another, then 16-bit interleaved
                pcm.clear();
                if (std::abs (rate - (double) audioRate) < 1.0)
                {
                    pcm.reserve ((size_t) got * 2);
                    for (int i = 0; i < got; ++i)
                    {
                        pcm.push_back ((int16_t) juce::jlimit (-32768, 32767, (int) std::lround (left[(size_t) i] * 32767.0f)));
                        pcm.push_back ((int16_t) juce::jlimit (-32768, 32767, (int) std::lround (right[(size_t) i] * 32767.0f)));
                    }
                }
                else
                {
                    const auto step = rate / audioRate;
                    for (; resamplePos < (double) got; resamplePos += step)
                    {
                        const auto i = (int) resamplePos;
                        const auto f = (float) (resamplePos - i);
                        const auto l0 = i == 0 ? lastL : left[(size_t) i - 1], r0 = i == 0 ? lastR : right[(size_t) i - 1];
                        const auto l = l0 + (left[(size_t) i] - l0) * f, r = r0 + (right[(size_t) i] - r0) * f;
                        pcm.push_back ((int16_t) juce::jlimit (-32768, 32767, (int) std::lround (l * 32767.0f)));
                        pcm.push_back ((int16_t) juce::jlimit (-32768, 32767, (int) std::lround (r * 32767.0f)));
                    }
                    resamplePos -= (double) got;
                    lastL = left[(size_t) got - 1];
                    lastR = right[(size_t) got - 1];
                }

                const auto frames = (juce::int64) pcm.size() / 2;
                if (frames == 0)
                    continue;
                const auto bytes = (DWORD) (pcm.size() * sizeof (int16_t));
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED (MFCreateMemoryBuffer (bytes, &buffer)))
                    return;
                BYTE* data = nullptr;
                if (SUCCEEDED (buffer->Lock (&data, nullptr, nullptr)))
                {
                    std::memcpy (data, pcm.data(), bytes);
                    buffer->Unlock();
                }
                buffer->SetCurrentLength (bytes);
                ComPtr<IMFSample> sample;
                MFCreateSample (&sample);
                sample->AddBuffer (buffer.get());
                sample->SetSampleTime ((LONGLONG) (audioFrames * 10000000LL / audioRate));
                sample->SetSampleDuration ((LONGLONG) (frames * 10000000LL / audioRate));
                audioFrames += frames;
                if (const auto r = writer->WriteSample (audioStream, sample.get()); FAILED (r))
                {
                    fail (hr ("Writing the sound failed", r));
                    return;
                }
            }
        }

        std::thread worker;
        std::atomic<bool> stopFlag { false }, recording { false }, userStopped { false };
        juce::CriticalSection failureLock, finishLock;
        std::function<void (const juce::File&, const juce::String&)> onFinished;
        juce::String failure, finishError;

        ScreenAudioTap* tap = nullptr;
        juce::File file;
        HWND followWindow = nullptr;
        int capX = 0, capY = 0, capW = 0, capH = 0, outW = 0, outH = 0, fps = 30, audioRate = 48000;
        double rate = 48000.0;

        bool comStarted = false, mfStarted = false;
        ComPtr<IMFSinkWriter> writer;
        DWORD videoStream = 0, audioStream = 0;
        juce::int64 audioFrames = 0;
        double resamplePos = 0.0;
        float lastL = 0.0f, lastR = 0.0f;
        std::vector<float> left, right;
        std::vector<int16_t> pcm;

        HDC screenDC = nullptr, memDC = nullptr;
        HBITMAP dib = nullptr;
        HGDIOBJ oldBitmap = nullptr;
        void* pixels = nullptr;
    };
}

std::unique_ptr<ScreenRecorder> ScreenRecorder::create() { return std::make_unique<WindowsRecorder>(); }
void ScreenRecorder::openPermissionSettings() {}

} // namespace riff

#endif
