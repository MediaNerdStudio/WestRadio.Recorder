#pragma once

#include <vector>
#include <atomic>
#include <cstddef>

class AudioRingBuffer
{
public:
    explicit AudioRingBuffer(size_t capacityFrames, int channels);

    size_t write(const float *src, size_t frames);
    size_t read(float *dst, size_t maxFrames);
    size_t available() const;

private:
    std::vector<float> data_;
    size_t mask_;
    int channels_;
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};

    static size_t nextPowerOfTwo(size_t n);
};
