#include "DeviceStream.h"
#include "AudioTrack.h"
#include <QDebug>

DeviceStream::DeviceStream()
{
}

DeviceStream::~DeviceStream()
{
    close();
}

bool DeviceStream::open(PaDeviceIndex deviceIndex, const std::vector<Subscriber> &subscribers)
{
    close();

    const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex);
    if (!info)
        return false;

    deviceIndex_ = deviceIndex;
    subscribers_ = subscribers;
    totalChannels_ = info->maxInputChannels;
    sampleRate_ = info->defaultSampleRate;

    if (totalChannels_ <= 0)
        return false;

    for (const auto &sub : subscribers_) {
        sub.track->setSampleRate(sampleRate_);
    }

    PaStreamParameters inputParams{};
    inputParams.device = deviceIndex_;
    inputParams.channelCount = totalChannels_;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_,
                                &inputParams,
                                nullptr,
                                sampleRate_,
                                paFramesPerBufferUnspecified,
                                paClipOff,
                                paCallback,
                                this);
    if (err != paNoError) {
        qWarning() << "Pa_OpenStream failed:" << Pa_GetErrorText(err);
        stream_ = nullptr;
        return false;
    }

    return true;
}

bool DeviceStream::start()
{
    if (!stream_)
        return false;
    PaError err = Pa_StartStream(stream_);
    return err == paNoError;
}

bool DeviceStream::stop()
{
    if (!stream_)
        return true;
    PaError err = Pa_StopStream(stream_);
    return err == paNoError;
}

bool DeviceStream::close()
{
    stop();
    if (stream_) {
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    return true;
}

PaDeviceIndex DeviceStream::deviceIndex() const { return deviceIndex_; }
double DeviceStream::sampleRate() const { return sampleRate_; }

int DeviceStream::paCallback(const void *inputBuffer, void *,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *,
                             PaStreamCallbackFlags,
                             void *userData)
{
    DeviceStream *self = static_cast<DeviceStream *>(userData);
    if (!inputBuffer)
        return paContinue;

    const float *input = static_cast<const float *>(inputBuffer);
    for (const auto &sub : self->subscribers_) {
        sub.track->pushSamples(input, self->totalChannels_, sub.offset, sub.count, framesPerBuffer);
    }
    return paContinue;
}
