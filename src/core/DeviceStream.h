#pragma once

#include <portaudio.h>
#include <pa_asio.h>
#include <QString>
#include <vector>

class AudioTrack;

class DeviceStream
{
public:
    struct Subscriber {
        AudioTrack *track;
        int offset;
        int count;
    };

    DeviceStream();
    ~DeviceStream();

    bool open(PaDeviceIndex deviceIndex, const std::vector<Subscriber> &subscribers);
    bool start();
    bool stop();
    bool close();

    PaDeviceIndex deviceIndex() const;
    double sampleRate() const;
    QString lastError() const;

private:
    bool tryOpen(const PaDeviceInfo *info, double sampleRate, bool useOutput);
    void appendError(const QString &line);

    static int paCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData);

    PaStream *stream_ = nullptr;
    PaDeviceIndex deviceIndex_ = paNoDevice;
    int totalChannels_ = 0;
    int baseOffset_ = 0;
    int outputChannelCount_ = 0;
    double sampleRate_ = 0.0;
    bool isAsio_ = false;
    PaAsioStreamInfo asioInfo_{};
    std::vector<int> channelSelectors_;
    QString lastError_;
    std::vector<Subscriber> subscribers_;
};
