#include "AudioRingBuffer.h"
#include <algorithm>
#include <cstring>

AudioRingBuffer::AudioRingBuffer(size_t capacityFrames, int channels)
    : channels_(channels)
{
    size_t samples = nextPowerOfTwo(capacityFrames * channels);
    data_.resize(samples);
    mask_ = data_.size() - 1;
}

size_t AudioRingBuffer::nextPowerOfTwo(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

size_t AudioRingBuffer::available() const
{
    return (write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire)) & mask_;
}

size_t AudioRingBuffer::write(const float *src, size_t frames)
{
    size_t samples = frames * channels_;
    size_t freeSlots = data_.size() - available() - 1;
    if (samples > freeSlots)
        samples = freeSlots;

    size_t w = write_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < samples; ++i)
        data_[(w + i) & mask_] = src[i];

    write_.store((w + samples) & mask_, std::memory_order_release);
    return samples / channels_;
}

size_t AudioRingBuffer::read(float *dst, size_t maxFrames)
{
    size_t maxSamples = maxFrames * channels_;
    size_t avail = available();
    size_t samples = std::min(avail, maxSamples);

    size_t r = read_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < samples; ++i)
        dst[i] = data_[(r + i) & mask_];

    read_.store((r + samples) & mask_, std::memory_order_release);
    return samples / channels_;
}
