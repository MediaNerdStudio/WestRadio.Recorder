#include "AudioTrack.h"
#include "AudioRingBuffer.h"
#include "WavStreamWriter.h"
#include <QDir>
#include <algorithm>
#include <cmath>
#include <vector>

AudioTrack::AudioTrack()
{
}

AudioTrack::~AudioTrack()
{
    stop();
}

QString AudioTrack::name() const { return name_; }
void AudioTrack::setName(const QString &name) { name_ = name; }

int AudioTrack::apiIndex() const { return apiIndex_; }
void AudioTrack::setApiIndex(int apiIndex) { apiIndex_ = apiIndex; }

PaDeviceIndex AudioTrack::deviceIndex() const { return deviceIndex_; }
void AudioTrack::setDeviceIndex(PaDeviceIndex index) { deviceIndex_ = index; }

int AudioTrack::channelOffset() const { return channelOffset_; }
void AudioTrack::setChannelOffset(int offset) { channelOffset_ = offset; }

int AudioTrack::channelCount() const { return channelCount_; }
void AudioTrack::setChannelCount(int count) { channelCount_ = std::clamp(count, 1, 2); }

AudioTrack::Format AudioTrack::format() const { return format_; }
void AudioTrack::setFormat(Format fmt) { format_ = fmt; }

bool AudioTrack::isArmed() const { return armed_; }
void AudioTrack::setArmed(bool armed) { armed_ = armed; }

double AudioTrack::sampleRate() const { return sampleRate_; }
void AudioTrack::setSampleRate(double rate) { sampleRate_ = rate; }

QString AudioTrack::currentWavPath() const { return currentWavPath_; }

QString AudioTrack::finalPath() const
{
    std::lock_guard<std::mutex> lock(finalPathMutex_);
    return finalPath_;
}

bool AudioTrack::isRecording() const { return running_.load(std::memory_order_acquire); }

bool AudioTrack::start(const QDateTime &recordStart, const QString &outputDir)
{
    stop();

    QString baseName = name_.isEmpty() ? QStringLiteral("TRACK") : name_;
    QString datePart = recordStart.toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    QString fileBase = QStringLiteral("%1_%2").arg(baseName, datePart);
    QDir dir(outputDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    currentWavPath_ = dir.filePath(fileBase + QStringLiteral(".wav"));
    finalPath_.clear();

    writer_ = std::make_unique<WavStreamWriter>();
    if (!writer_->open(currentWavPath_, static_cast<int>(sampleRate_), channelCount_))
        return false;

    size_t capacityFrames = static_cast<size_t>(std::max(sampleRate_, 44100.0) * channelCount_ * 5);
    ringBuffer_ = std::make_unique<AudioRingBuffer>(capacityFrames, channelCount_);

    running_.store(true, std::memory_order_release);
    writerThread_ = std::thread(&AudioTrack::writerThread, this);
    return true;
}

void AudioTrack::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    running_.store(false, std::memory_order_release);
    if (writerThread_.joinable())
        writerThread_.join();

    if (writer_)
        writer_->close();

    if (format_ == WAV) {
        std::lock_guard<std::mutex> lock(finalPathMutex_);
        finalPath_ = currentWavPath_;
    }
}

void AudioTrack::pushSamples(const float *interleaved, int stride, int offset, int count, unsigned long frames)
{
    if (!running_.load(std::memory_order_acquire) || !ringBuffer_)
        return;

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(frames) * count);

    float leftPeak = 0.0f;
    float rightPeak = 0.0f;

    for (unsigned long f = 0; f < frames; ++f) {
        for (int c = 0; c < count; ++c) {
            float s = interleaved[f * stride + offset + c];
            samples.push_back(s);
            float a = std::abs(s);
            if (c == 0)
                leftPeak = std::max(leftPeak, a);
            if (count == 1)
                rightPeak = leftPeak;
            else if (c == 1)
                rightPeak = std::max(rightPeak, a);
        }
    }

    float oldLeft = peakLeft_.load(std::memory_order_relaxed);
    while (leftPeak > oldLeft && !peakLeft_.compare_exchange_weak(oldLeft, leftPeak, std::memory_order_relaxed));

    float oldRight = peakRight_.load(std::memory_order_relaxed);
    while (rightPeak > oldRight && !peakRight_.compare_exchange_weak(oldRight, rightPeak, std::memory_order_relaxed));

    ringBuffer_->write(samples.data(), frames);
}

void AudioTrack::setFinalPath(const QString &path)
{
    std::lock_guard<std::mutex> lock(finalPathMutex_);
    finalPath_ = path;
}

void AudioTrack::writerThread()
{
    constexpr size_t chunkFrames = 4096;
    std::vector<float> buffer(chunkFrames * channelCount_);

    while (running_.load(std::memory_order_acquire) || ringBuffer_->available() > 0) {
        size_t got = ringBuffer_->read(buffer.data(), chunkFrames);
        if (got > 0) {
            writer_->write(buffer.data(), got);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

float AudioTrack::readLeftPeak()
{
    float v = peakLeft_.exchange(0.0f, std::memory_order_acquire);
    return v;
}

float AudioTrack::readRightPeak()
{
    float v = peakRight_.exchange(0.0f, std::memory_order_acquire);
    return v;
}
