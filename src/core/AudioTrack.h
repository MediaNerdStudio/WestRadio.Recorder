#pragma once

#include <QString>
#include <QDateTime>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <portaudio.h>

class AudioRingBuffer;
class WavStreamWriter;

class AudioTrack
{
public:
    enum Format { WAV, MP3 };

    AudioTrack();
    ~AudioTrack();

    QString name() const;
    void setName(const QString &name);

    int apiIndex() const;
    void setApiIndex(int apiIndex);

    PaDeviceIndex deviceIndex() const;
    void setDeviceIndex(PaDeviceIndex index);

    int channelOffset() const;
    void setChannelOffset(int offset);

    int channelCount() const;
    void setChannelCount(int count);

    Format format() const;
    void setFormat(Format fmt);

    bool isArmed() const;
    void setArmed(bool armed);

    double sampleRate() const;
    void setSampleRate(double rate);

    QString currentWavPath() const;
    QString finalPath() const;
    void setFinalPath(const QString &path);

    bool isRecording() const;

    bool start(const QDateTime &recordStart, const QString &outputDir);
    void stop();

    // Called from PortAudio callback thread.
    void pushSamples(const float *interleaved, int stride, int offset, int count, unsigned long frames);

    // Called from UI thread.
    float readLeftPeak();
    float readRightPeak();

private:
    void writerThread();

    QString name_;
    int apiIndex_ = -1;
    PaDeviceIndex deviceIndex_ = paNoDevice;
    int channelOffset_ = 0;
    int channelCount_ = 1;
    Format format_ = WAV;
    bool armed_ = true;
    double sampleRate_ = 48000.0;

    std::atomic<bool> running_{false};
    std::unique_ptr<AudioRingBuffer> ringBuffer_;
    std::unique_ptr<WavStreamWriter> writer_;
    std::thread writerThread_;

    std::atomic<float> peakLeft_{0.0f};
    std::atomic<float> peakRight_{0.0f};

    QString currentWavPath_;
    QString finalPath_;
    mutable std::mutex finalPathMutex_;
};
