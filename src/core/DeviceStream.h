#pragma once

#include <portaudio.h>
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

private:
    static int paCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData);

    PaStream *stream_ = nullptr;
    PaDeviceIndex deviceIndex_ = paNoDevice;
    int totalChannels_ = 0;
    double sampleRate_ = 0.0;
    std::vector<Subscriber> subscribers_;
};
