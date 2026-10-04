#include "DeviceStream.h"
#include "AudioTrack.h"
#include <QDebug>
#include <algorithm>
#include <vector>

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

    if (totalChannels_ <= 0)
        return false;

    // ASIO devices are picky about sample rate and often need a full-duplex
    // stream even when only recording. Try a few common configurations.
    std::vector<double> sampleRates;
    if (info->defaultSampleRate > 0)
        sampleRates.push_back(info->defaultSampleRate);
    for (double sr : {48000.0, 44100.0, 96000.0, 88200.0, 192000.0}) {
        if (std::find(sampleRates.begin(), sampleRates.end(), sr) == sampleRates.end())
            sampleRates.push_back(sr);
    }

    for (double sr : sampleRates) {
        // Try input-only first (works for MME/WASAPI/DirectSound).
        if (tryOpen(info, sr, false))
            return true;
        // Try full-duplex (often required for ASIO).
        if (info->maxOutputChannels > 0 && tryOpen(info, sr, true))
            return true;
    }

    qWarning() << "DeviceStream: failed to open device" << deviceIndex
               << "after trying" << sampleRates.size() << "sample rates.";
    return false;
}

bool DeviceStream::tryOpen(const PaDeviceInfo *info, double sampleRate, bool useOutput)
{
    PaStreamParameters inputParams{};
    inputParams.device = deviceIndex_;
    inputParams.channelCount = totalChannels_;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters outputParams{};
    PaStreamParameters *outputPtr = nullptr;
    int outputChannels = 0;
    if (useOutput && info->maxOutputChannels > 0) {
        outputParams.device = deviceIndex_;
        outputParams.channelCount = info->maxOutputChannels;
        outputParams.sampleFormat = paFloat32;
        outputParams.suggestedLatency = info->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = nullptr;
        outputPtr = &outputParams;
        outputChannels = info->maxOutputChannels;
    }

    PaStream *candidate = nullptr;
    PaError err = Pa_OpenStream(&candidate,
                                &inputParams,
                                outputPtr,
                                sampleRate,
                                paFramesPerBufferUnspecified,
                                paClipOff,
                                paCallback,
                                this);
    if (err != paNoError) {
        qWarning() << "Pa_OpenStream failed (sr=" << sampleRate
                   << " duplex=" << useOutput << "):" << Pa_GetErrorText(err);
        return false;
    }

    stream_ = candidate;
    sampleRate_ = sampleRate;
    outputChannelCount_ = outputChannels;
    for (const auto &sub : subscribers_)
        sub.track->setSampleRate(sampleRate_);

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
    outputChannelCount_ = 0;
    return true;
}

PaDeviceIndex DeviceStream::deviceIndex() const { return deviceIndex_; }
double DeviceStream::sampleRate() const { return sampleRate_; }

int DeviceStream::paCallback(const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *,
                             PaStreamCallbackFlags,
                             void *userData)
{
    DeviceStream *self = static_cast<DeviceStream *>(userData);

    // Keep any full-duplex ASIO output silent; we only record.
    if (outputBuffer && self->outputChannelCount_ > 0) {
        float *output = static_cast<float *>(outputBuffer);
        std::fill(output, output + framesPerBuffer * self->outputChannelCount_, 0.0f);
    }

    if (!inputBuffer)
        return paContinue;

    const float *input = static_cast<const float *>(inputBuffer);
    for (const auto &sub : self->subscribers_) {
        sub.track->pushSamples(input, self->totalChannels_, sub.offset, sub.count, framesPerBuffer);
    }
    return paContinue;
}
