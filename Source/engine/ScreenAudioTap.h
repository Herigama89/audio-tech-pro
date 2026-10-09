#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace riff
{

/**
    The sound for a screen recording: the master output, plus (optionally) a
    mic input mixed in for the video only, never to the speakers.

    The audio thread pushes every block; the recorder pulls from another
    thread. Lock-free: if the recorder falls behind, the newest audio is
    dropped and counted.
*/
class ScreenAudioTap
{
public:
    explicit ScreenAudioTap (int capacitySamples = 48000 * 4)
        : fifo (capacitySamples), buffer (2, capacitySamples)
    {
    }

    /** Which input to mix in: its first channel (-1: none), mono or a stereo pair, and a gain. Set before recording. */
    void setMic (int firstChannel, bool stereo, float gain) noexcept
    {
        micChannel.store (firstChannel);
        micStereo.store (stereo);
        micGain.store (gain);
    }

    /** Empties it and starts the count of dropped samples again (call before the engine starts pushing). */
    void reset() noexcept
    {
        fifo.reset();
        dropped.store (0);
    }

    /** Audio thread: the final mix and the device inputs for the same samples. */
    void push (const float* left, const float* right, int n,
               const float* const* inputs, int numInputs, int inputOffset) noexcept
    {
        const auto mic = micChannel.load();
        const auto stereo = micStereo.load();
        const auto gain = micGain.load();

        const float* micL = nullptr;
        const float* micR = nullptr;
        if (mic >= 0 && inputs != nullptr && mic < numInputs && inputs[mic] != nullptr)
        {
            micL = inputs[mic] + inputOffset;
            micR = (stereo && mic + 1 < numInputs && inputs[mic + 1] != nullptr) ? inputs[mic + 1] + inputOffset : micL;
        }

        const auto scope = fifo.write (n);
        if (scope.blockSize1 + scope.blockSize2 < n)
            dropped.fetch_add (n - scope.blockSize1 - scope.blockSize2);

        auto copy = [&] (int destStart, int count, int sourceStart)
        {
            if (count <= 0)
                return;
            auto* l = buffer.getWritePointer (0, destStart);
            auto* r = buffer.getWritePointer (1, destStart);
            juce::FloatVectorOperations::copy (l, left + sourceStart, count);
            juce::FloatVectorOperations::copy (r, right + sourceStart, count);
            if (micL != nullptr)
            {
                juce::FloatVectorOperations::addWithMultiply (l, micL + sourceStart, gain, count);
                juce::FloatVectorOperations::addWithMultiply (r, micR + sourceStart, gain, count);
            }
        };

        copy (scope.startIndex1, scope.blockSize1, 0);
        copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    /** Recorder thread: takes up to `maxSamples`, returns how many. */
    int pull (float* left, float* right, int maxSamples) noexcept
    {
        const auto scope = fifo.read (juce::jmin (maxSamples, fifo.getNumReady()));
        auto copy = [&] (int sourceStart, int count, int destStart)
        {
            if (count <= 0)
                return;
            juce::FloatVectorOperations::copy (left + destStart, buffer.getReadPointer (0, sourceStart), count);
            juce::FloatVectorOperations::copy (right + destStart, buffer.getReadPointer (1, sourceStart), count);
        };
        copy (scope.startIndex1, scope.blockSize1, 0);
        copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    int getNumDropped() const noexcept { return dropped.load(); }

private:
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buffer;
    std::atomic<int> micChannel { -1 };
    std::atomic<bool> micStereo { false };
    std::atomic<float> micGain { 1.0f };
    std::atomic<int> dropped { 0 };
};

} // namespace riff
