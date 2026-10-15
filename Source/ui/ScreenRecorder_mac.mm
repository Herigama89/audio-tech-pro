// Screen recording on macOS: ScreenCaptureKit for the picture, the DAW's own
// mix (via ScreenAudioTap) for the sound, AVAssetWriter for the MP4 file.
// Compiled with ARC (see CMakeLists.txt).

#define DONT_SET_USING_JUCE_NAMESPACE 1
#include "ScreenRecorder.h"

#if JUCE_MAC

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <unistd.h>
#include <vector>

#if ! __has_feature(objc_arc)
 #error "ScreenRecorder_mac.mm must be compiled with -fobjc-arc"
#endif

//==============================================================================
API_AVAILABLE(macos(12.3))
@interface ATPRecordingSession : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithTap:(riff::ScreenAudioTap*)tap sampleRate:(double)rate;
- (void)startWithFilter:(SCContentFilter*)filter
          configuration:(SCStreamConfiguration*)configuration
                   file:(NSURL*)file
                  width:(int)width
                 height:(int)height
              frameRate:(int)fps
             completion:(void (^)(NSString* error))done;
- (void)stopWithCompletion:(void (^)(NSString* error))done;
- (NSString*)takeFailure;
@end

@implementation ATPRecordingSession
{
    riff::ScreenAudioTap* tap;
    double sampleRate;
    int32_t rateScale;
    dispatch_queue_t queue;
    dispatch_source_t audioTimer;
    SCStream* stream;
    AVAssetWriter* writer;
    AVAssetWriterInput* videoInput;
    AVAssetWriterInput* audioInput;
    CMAudioFormatDescriptionRef audioFormat;
    CMTime audioStart, sessionStart;
    int64_t audioSamples;
    BOOL sessionStarted;
    std::vector<float> left, right, interleaved;
    NSString* failure;
}

- (instancetype)initWithTap:(riff::ScreenAudioTap*)theTap sampleRate:(double)rate
{
    if ((self = [super init]))
    {
        tap = theTap;
        sampleRate = rate > 0.0 ? rate : 48000.0;
        rateScale = (int32_t) std::lround (sampleRate);
        queue = dispatch_queue_create ("com.audiotechpro.screenrecorder", DISPATCH_QUEUE_SERIAL);
        audioFormat = NULL;
        audioSamples = 0;
        sessionStarted = NO;
        left.resize (2048);
        right.resize (2048);
        interleaved.resize (4096);
        failure = nil;
    }
    return self;
}

- (void)dealloc
{
    if (audioFormat != NULL)
        CFRelease (audioFormat);
}

- (void)startWithFilter:(SCContentFilter*)filter
          configuration:(SCStreamConfiguration*)configuration
                   file:(NSURL*)file
                  width:(int)width
                 height:(int)height
              frameRate:(int)fps
             completion:(void (^)(NSString* error))done
{
    dispatch_async (queue, ^{
        [[NSFileManager defaultManager] removeItemAtURL: file error: nil];

        NSError* error = nil;
        self->writer = [[AVAssetWriter alloc] initWithURL: file fileType: AVFileTypeMPEG4 error: &error];
        if (self->writer == nil)
        {
            done (error.localizedDescription ?: @"Couldn't create the video file.");
            return;
        }

        const int bitrate = riff::ScreenRecorder::bitrateFor ({ width, height }, fps);
        NSDictionary* compression = @{ AVVideoAverageBitRateKey: @(bitrate),
                                       AVVideoExpectedSourceFrameRateKey: @(fps),
                                       AVVideoMaxKeyFrameIntervalKey: @(fps * 2),
                                       AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel };
        NSDictionary* videoSettings = @{ AVVideoCodecKey: AVVideoCodecTypeH264,
                                         AVVideoWidthKey: @(width),
                                         AVVideoHeightKey: @(height),
                                         AVVideoCompressionPropertiesKey: compression };
        self->videoInput = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo outputSettings: videoSettings];
        self->videoInput.expectsMediaDataInRealTime = YES;

        AudioChannelLayout layout;
        memset (&layout, 0, sizeof (layout));
        layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
        const double outputRate = self->sampleRate <= 48000.0 ? self->sampleRate : 48000.0;
        NSDictionary* audioSettings = @{ AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                                         AVSampleRateKey: @(outputRate),
                                         AVNumberOfChannelsKey: @2,
                                         AVEncoderBitRateKey: @256000,
                                         AVChannelLayoutKey: [NSData dataWithBytes: &layout length: sizeof (layout)] };
        self->audioInput = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeAudio outputSettings: audioSettings];
        self->audioInput.expectsMediaDataInRealTime = YES;

        if (! [self->writer canAddInput: self->videoInput] || ! [self->writer canAddInput: self->audioInput])
        {
            done (@"The video file can't take this format.");
            return;
        }
        [self->writer addInput: self->videoInput];
        [self->writer addInput: self->audioInput];

        if (! [self->writer startWriting])
        {
            done (self->writer.error.localizedDescription ?: @"Couldn't start writing the video file.");
            return;
        }

        AudioStreamBasicDescription asbd;
        memset (&asbd, 0, sizeof (asbd));
        asbd.mSampleRate = self->sampleRate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mBytesPerPacket = 8;
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = 8;
        asbd.mChannelsPerFrame = 2;
        asbd.mBitsPerChannel = 32;
        CMAudioFormatDescriptionCreate (kCFAllocatorDefault, &asbd, 0, NULL, 0, NULL, NULL, &self->audioFormat);

        self->stream = [[SCStream alloc] initWithFilter: filter configuration: configuration delegate: self];
        NSError* addError = nil;
        if (! [self->stream addStreamOutput: self type: SCStreamOutputTypeScreen sampleHandlerQueue: self->queue error: &addError])
        {
            [self->writer cancelWriting];
            done (addError.localizedDescription ?: @"Couldn't start the screen capture.");
            return;
        }

        // The sound starts now: throw away what the tap gathered before.
        while (self->tap->getNumReady() > 0)
            if (self->tap->pull (self->left.data(), self->right.data(), (int) self->left.size()) <= 0)
                break;
        self->audioStart = CMClockGetTime (CMClockGetHostTimeClock());
        self->audioSamples = 0;

        self->audioTimer = dispatch_source_create (DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self->queue);
        dispatch_source_set_timer (self->audioTimer, dispatch_time (DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
                                   20 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC);
        __weak ATPRecordingSession* weakSelf = self;
        dispatch_source_set_event_handler (self->audioTimer, ^{ [weakSelf pumpAudio]; });
        dispatch_resume (self->audioTimer);

        [self->stream startCaptureWithCompletionHandler: ^(NSError* startError)
        {
            if (startError != nil)
            {
                dispatch_async (self->queue, ^{
                    if (self->audioTimer != nil)
                    {
                        dispatch_source_cancel (self->audioTimer);
                        self->audioTimer = nil;
                    }
                    [self->writer cancelWriting];
                });
                done (startError.localizedDescription ?: @"Couldn't start the screen capture.");
                return;
            }
            done (nil);
        }];
    });
}

/** On the queue: moves the sound gathered so far into the file. */
- (void)pumpAudio
{
    if (audioInput == nil || audioFormat == NULL)
        return;

    const int chunk = (int) left.size();

    for (;;)
    {
        const auto ready = tap->getNumReady();
        if (ready <= 0)
            break;

        const auto n = tap->pull (left.data(), right.data(), juce::jmin (ready, chunk));
        if (n <= 0)
            break;

        auto pts = CMTimeAdd (audioStart, CMTimeMake (audioSamples, rateScale));
        audioSamples += n;

        if (! sessionStarted)
            continue;   // nothing to line it up with yet

        int skip = 0;
        if (CMTimeCompare (pts, sessionStart) < 0)
        {
            const auto behind = CMTimeGetSeconds (CMTimeSubtract (sessionStart, pts));
            skip = (int) std::ceil (behind * sampleRate);
            if (skip >= n)
                continue;
            pts = CMTimeAdd (pts, CMTimeMake (skip, rateScale));
        }

        [self appendAudioFrom: skip count: n - skip at: pts];
    }
}

- (void)appendAudioFrom:(int)start count:(int)n at:(CMTime)pts
{
    if (n <= 0 || ! audioInput.readyForMoreMediaData || writer.status != AVAssetWriterStatusWriting)
        return;

    for (int i = 0; i < n; ++i)
    {
        interleaved[(size_t) (2 * i)] = left[(size_t) (start + i)];
        interleaved[(size_t) (2 * i + 1)] = right[(size_t) (start + i)];
    }

    const size_t bytes = (size_t) n * 2 * sizeof (float);
    CMBlockBufferRef block = NULL;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock (kCFAllocatorDefault, NULL, bytes, kCFAllocatorDefault, NULL,
                                                          0, bytes, kCMBlockBufferAssureMemoryNowFlag, &block);
    if (status != noErr || block == NULL)
        return;

    CMBlockBufferReplaceDataBytes (interleaved.data(), block, 0, bytes);

    CMSampleBufferRef sample = NULL;
    status = CMAudioSampleBufferCreateReadyWithPacketDescriptions (kCFAllocatorDefault, block, audioFormat,
                                                                   (CMItemCount) n, pts, NULL, &sample);
    CFRelease (block);

    if (status == noErr && sample != NULL)
    {
        [audioInput appendSampleBuffer: sample];
        CFRelease (sample);
    }
}

// SCStreamOutput: a new picture (on the queue).
- (void)stream:(SCStream*)theStream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen || ! CMSampleBufferIsValid (sampleBuffer))
        return;

    // Only complete frames carry a picture (idle ones just say nothing changed).
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray (sampleBuffer, false);
    if (attachments == NULL || CFArrayGetCount (attachments) == 0)
        return;

    NSDictionary* info = (__bridge NSDictionary*) CFArrayGetValueAtIndex (attachments, 0);
    NSNumber* status = info[SCStreamFrameInfoStatus];
    if (status == nil || (SCFrameStatus) status.integerValue != SCFrameStatusComplete)
        return;

    if (writer.status != AVAssetWriterStatusWriting)
        return;

    const auto pts = CMSampleBufferGetPresentationTimeStamp (sampleBuffer);
    if (! sessionStarted)
    {
        [writer startSessionAtSourceTime: pts];
        sessionStart = pts;
        sessionStarted = YES;
    }

    if (videoInput.readyForMoreMediaData)
        [videoInput appendSampleBuffer: sampleBuffer];
}

// SCStreamDelegate: the capture stopped by itself (display gone, permission taken away...).
- (void)stream:(SCStream*)theStream didStopWithError:(NSError*)error
{
    @synchronized (self)
    {
        failure = error.localizedDescription ?: @"The screen capture stopped.";
    }
}

- (NSString*)takeFailure
{
    @synchronized (self)
    {
        NSString* f = failure;
        failure = nil;

        if (f == nil && writer != nil && writer.status == AVAssetWriterStatusFailed)
            f = writer.error.localizedDescription ?: @"Writing the video failed (is the disk full?).";
        return f;
    }
}

- (void)stopWithCompletion:(void (^)(NSString* error))done
{
    void (^finish)(void) = ^{
        dispatch_async (self->queue, ^{
            if (self->audioTimer != nil)
            {
                dispatch_source_cancel (self->audioTimer);
                self->audioTimer = nil;
            }

            [self pumpAudio];

            if (! self->sessionStarted || self->writer.status != AVAssetWriterStatusWriting)
            {
                NSString* why = self->writer.status == AVAssetWriterStatusFailed
                                    ? (self->writer.error.localizedDescription ?: @"Writing the video failed.")
                                    : @"No picture was captured, so there's no video. Check that Audio Tech Pro may record the screen.";
                [self->writer cancelWriting];
                done (why);
                return;
            }

            [self->videoInput markAsFinished];
            [self->audioInput markAsFinished];
            [self->writer finishWritingWithCompletionHandler: ^{
                if (self->writer.status == AVAssetWriterStatusCompleted)
                    done (nil);
                else
                    done (self->writer.error.localizedDescription ?: @"The video couldn't be finished.");
            }];
        });
    };

    if (stream != nil)
        [stream stopCaptureWithCompletionHandler: ^(NSError*) { finish(); }];
    else
        finish();
}

@end

//==============================================================================
// macOS 14+: the system's content picker. What you pick there may be recorded without the
// Screen Recording permission, so recording works even when macOS won't confirm that switch.
API_AVAILABLE(macos(14.0))
@interface ATPPickerObserver : NSObject <SCContentSharingPickerObserver>
@property (copy) void (^onFilter)(SCContentFilter* filter);
@property (copy) void (^onFail)(NSString* reason);
@end

@implementation ATPPickerObserver
- (void)finish:(SCContentSharingPicker*)picker
{
    [picker removeObserver: self];
    picker.active = NO;
}

- (void)contentSharingPicker:(SCContentSharingPicker*)picker didUpdateWithFilter:(SCContentFilter*)filter forStream:(SCStream*)stream
{
    [self finish: picker];
    auto done = self.onFilter;
    self.onFilter = nil;
    self.onFail = nil;
    if (done != nil)
        done (filter);
}

- (void)contentSharingPicker:(SCContentSharingPicker*)picker didCancelForStream:(SCStream*)stream
{
    [self finish: picker];
    auto fail = self.onFail;
    self.onFilter = nil;
    self.onFail = nil;
    if (fail != nil)
        fail (@"Recording cancelled.");
}

- (void)contentSharingPickerStartDidFailWithError:(NSError*)error
{
    [self finish: SCContentSharingPicker.sharedPicker];
    auto fail = self.onFail;
    self.onFilter = nil;
    self.onFail = nil;
    if (fail != nil)
        fail (error.localizedDescription ?: @"The screen picker couldn't open.");
}
@end

//==============================================================================
namespace riff
{

namespace
{
    juce::String toJuce (NSString* s) { return s != nil ? juce::String (juce::CharPointer_UTF8 ([s UTF8String])) : juce::String(); }

    const char* permissionText = "Audio Tech Pro needs permission to record the screen. Allow it in System Settings > "
                                 "Privacy & Security > Screen & System Audio Recording, then quit and reopen Audio Tech Pro.\n\n"
                                 "If it's already switched on: select Audio Tech Pro in that list, remove it with the minus button, "
                                 "add it again with the plus button (or just switch it on when asked), then reopen the app. "
                                 "(macOS ties the permission to the copy of the app it was given to; from this version on, "
                                 "updates keep it.)";

    class MacScreenRecorder final : public ScreenRecorder
    {
    public:
        ~MacScreenRecorder() override
        {
            alive->store (false);
            if (@available (macOS 12.3, *))
                if (session != nil)
                    [(ATPRecordingSession*) session stopWithCompletion: ^(NSString*) {}];
        }

        bool isAvailable() const override
        {
            if (@available (macOS 12.3, *))
                return true;
            return false;
        }

        juce::String unavailableReason() const override
        {
            return isAvailable() ? juce::String() : juce::String ("Screen recording needs macOS 12.3 or later.");
        }

        bool isRecording() const override { return session != nil && ! stopping; }
        juce::Point<int> getVideoSize() const override { return videoSize; }

        juce::String takeFailure() override
        {
            if (@available (macOS 12.3, *))
                if (session != nil && ! stopping)
                    return toJuce ([(ATPRecordingSession*) session takeFailure]);
            return {};
        }

        void start (const Options& options, ScreenAudioTap& tap, std::function<void (const juce::String&)> started) override
        {
            if (@available (macOS 12.3, *))
            {
                if (session != nil || starting)
                {
                    started ("Already recording.");
                    return;
                }

                // macOS's own check can say "no" even with the switch on (a rebuilt or moved app, until it's
                // reopened, or macOS 15's monthly confirmation). Then, on macOS 14 and later, the system's
                // picker is used instead: what you pick there is recorded with no permission needed at all.
                // Never asks for the permission itself (that's the prompt that kept coming back).
                const bool permitted = CGPreflightScreenCaptureAccess();
                bool usePicker = false;
                if (@available (macOS 14.0, *))
                    usePicker = ! permitted;
                if (! permitted && ! usePicker)
                {
                    static bool asked = false;
                    if (! asked)
                    {
                        asked = true;
                        CGRequestScreenCaptureAccess();   // (macOS 12.3 to 13: the only way)
                    }
                }

                // Which display: the one showing our window.
                CGDirectDisplayID displayID = CGMainDisplayID();
                if (options.window != nullptr)
                    if (auto* peer = options.window->getPeer())
                    {
                        NSView* view = (__bridge NSView*) peer->getNativeHandle();
                        NSNumber* number = view.window.screen.deviceDescription[@"NSScreenNumber"];
                        if (number != nil)
                            displayID = (CGDirectDisplayID) number.unsignedIntValue;
                    }

                starting = true;
                currentFile = options.file;
                const auto opts = options;
                auto flag = alive;
                auto* tapPointer = &tap;

                auto report = [this, flag, started] (ATPRecordingSession* newSession, juce::Point<int> size, const juce::String& error)
                {
                    juce::MessageManager::callAsync ([this, flag, started, newSession, size, error]
                    {
                        if (! flag->load())
                            return;
                        starting = false;
                        if (error.isEmpty())
                        {
                            session = newSession;
                            stopping = false;
                            videoSize = size;
                        }
                        started (error);
                    });
                };

                // Records what a filter shows (a display, or this app's windows), its size in pixels.
                void (^begin)(SCContentFilter*, size_t, size_t) = ^(SCContentFilter* filter, size_t pixelWidth, size_t pixelHeight)
                {
                    const auto size = ScreenRecorder::fitSize ((int) pixelWidth, (int) pixelHeight, opts.maxHeight);
                    const int fps = juce::jlimit (1, 120, opts.frameRate);

                    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
                    config.width = (size_t) size.x;
                    config.height = (size_t) size.y;
                    config.minimumFrameInterval = CMTimeMake (1, fps);
                    config.pixelFormat = kCVPixelFormatType_32BGRA;
                    config.showsCursor = YES;
                    config.queueDepth = 8;

                    ATPRecordingSession* newSession = [[ATPRecordingSession alloc] initWithTap: tapPointer sampleRate: opts.sampleRate];
                    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: opts.file.getFullPathName().toRawUTF8()]];

                    [newSession startWithFilter: filter configuration: config file: url width: size.x height: size.y frameRate: fps
                                     completion: ^(NSString* startError)
                    {
                        report (startError == nil ? newSession : nil, size, toJuce (startError));
                    }];
                };

                // The system's picker: choose the screen, a window or an app, and recording starts.
                void (^presentPicker)(void) = ^
                {
                    if (@available (macOS 14.0, *))
                    {
                        ATPPickerObserver* observer = [[ATPPickerObserver alloc] init];
                        pickerObserver = observer;
                        observer.onFilter = ^(SCContentFilter* filter)
                        {
                            if (@available (macOS 14.0, *))
                            {
                                const CGRect rect = filter.contentRect;
                                const double scale = filter.pointPixelScale > 0 ? (double) filter.pointPixelScale : 2.0;
                                begin (filter, (size_t) std::lround (rect.size.width * scale), (size_t) std::lround (rect.size.height * scale));
                            }
                        };
                        observer.onFail = ^(NSString* reason)
                        {
                            report (nil, {}, toJuce (reason));
                        };

                        SCContentSharingPicker* picker = SCContentSharingPicker.sharedPicker;
                        SCContentSharingPickerConfiguration* pickerConfig = [[SCContentSharingPickerConfiguration alloc] init];
                        pickerConfig.allowedPickerModes = SCContentSharingPickerModeSingleDisplay
                                                        | SCContentSharingPickerModeSingleWindow
                                                        | SCContentSharingPickerModeSingleApplication;
                        picker.defaultConfiguration = pickerConfig;
                        [picker addObserver: observer];
                        picker.active = YES;
                        [picker present];
                        return;
                    }
                    report (nil, {}, juce::String (permissionText));
                };

                if (usePicker)
                {
                    presentPicker();
                    return;
                }

                [SCShareableContent getShareableContentExcludingDesktopWindows: NO
                                                           onScreenWindowsOnly: YES
                                                             completionHandler: ^(SCShareableContent* content, NSError* error)
                {
                    if (content == nil)
                    {
                        // Refused: on macOS 14 and later the picker still works, without the permission.
                        if (@available (macOS 14.0, *))
                        {
                            dispatch_async (dispatch_get_main_queue(), presentPicker);
                            return;
                        }
                        report (nil, {}, error != nil && error.code == SCStreamErrorUserDeclined
                                            ? juce::String (permissionText)
                                            : toJuce (error.localizedDescription ?: @"Couldn't see the screen.") + "\n\n" + permissionText);
                        return;
                    }

                    SCDisplay* display = content.displays.firstObject;
                    for (SCDisplay* d in content.displays)
                        if (d.displayID == displayID)
                            display = d;

                    if (display == nil)
                    {
                        report (nil, {}, "No display was found to record.");
                        return;
                    }

                    SCContentFilter* filter = nil;
                    if (opts.wholeScreen)
                    {
                        filter = [[SCContentFilter alloc] initWithDisplay: display excludingWindows: @[]];
                    }
                    else
                    {
                        const pid_t me = getpid();
                        SCRunningApplication* app = nil;
                        for (SCRunningApplication* a in content.applications)
                            if (a.processID == me)
                                app = a;

                        if (app == nil)
                        {
                            report (nil, {}, "Couldn't find Audio Tech Pro's windows on this display. Try recording the whole screen.");
                            return;
                        }
                        filter = [[SCContentFilter alloc] initWithDisplay: display includingApplications: @[ app ] exceptingWindows: @[]];
                    }

                    // The display's size in pixels (Retina screens have more pixels than points).
                    size_t pixelWidth = (size_t) display.width, pixelHeight = (size_t) display.height;
                    if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode (display.displayID))
                    {
                        pixelWidth = CGDisplayModeGetPixelWidth (mode);
                        pixelHeight = CGDisplayModeGetPixelHeight (mode);
                        CGDisplayModeRelease (mode);
                    }
                    begin (filter, pixelWidth, pixelHeight);
                }];
                return;
            }

            started (unavailableReason());
        }

        void stop (std::function<void (const juce::File&, const juce::String&)> finished) override
        {
            if (@available (macOS 12.3, *))
            {
                if (session == nil || stopping)
                {
                    if (finished)
                        finished ({}, "Not recording.");
                    return;
                }

                stopping = true;
                auto flag = alive;
                ATPRecordingSession* s = (ATPRecordingSession*) session;
                const auto file = currentFile;

                [s stopWithCompletion: ^(NSString* error)
                {
                    const auto message = toJuce (error);
                    juce::MessageManager::callAsync ([this, flag, finished, file, message]
                    {
                        if (! flag->load())
                            return;
                        session = nil;
                        stopping = false;
                        if (finished)
                            finished (file, message);
                    });
                }];
                return;
            }

            if (finished)
                finished ({}, unavailableReason());
        }

    private:
        id session = nil;
        id pickerObserver = nil;   // (kept alive while the system's picker is up)
        bool starting = false, stopping = false;
        juce::Point<int> videoSize;
        juce::File currentFile;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    };
}

std::unique_ptr<ScreenRecorder> ScreenRecorder::create()
{
    return std::make_unique<MacScreenRecorder>();
}

void ScreenRecorder::openPermissionSettings()
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

} // namespace riff

#endif
