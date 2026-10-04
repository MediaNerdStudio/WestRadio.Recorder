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

void DeviceStream::appendError(const QString &line)
{
    if (!lastError_.isEmpty())
        lastError_ += QLatin1Char('\n');
    lastError_ += line;
    qWarning().noquote() << line;
}

bool DeviceStream::open(PaDeviceIndex deviceIndex, const std::vector<Subscriber> &subscribers)
{
    close();
    lastError_.clear();

    const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex);
    if (!info) {
        appendError(QStringLiteral("Device %1: no device info").arg(deviceIndex));
        return false;
    }

    deviceIndex_ = deviceIndex;
    subscribers_ = subscribers;
    baseOffset_ = 0;
    isAsio_ = false;

    const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(info->hostApi);
    if (apiInfo && apiInfo->type == paASIO)
        isAsio_ = true;

    const QString devName = QString::fromLocal8Bit(info->name);

    if (isAsio_) {
        // ASIO can't share a device with other APIs and opening all channels of
        // large devices (e.g. 512-channel virtual matrices) often fails. Select
        // only the channel range our subscribers actually need.
        int minOffset = subscribers_.empty() ? 0 : subscribers_.front().offset;
        int maxEnd = minOffset;
        for (const auto &sub : subscribers_) {
            minOffset = std::min(minOffset, sub.offset);
            maxEnd = std::max(maxEnd, sub.offset + sub.count);
        }
        baseOffset_ = minOffset;
        totalChannels_ = maxEnd - minOffset;

        channelSelectors_.resize(totalChannels_);
        for (int i = 0; i < totalChannels_; ++i)
            channelSelectors_[i] = minOffset + i;

        asioInfo_ = {};
        asioInfo_.size = sizeof(PaAsioStreamInfo);
        asioInfo_.hostApiType = paASIO;
        asioInfo_.version = 1;
        asioInfo_.flags = paAsioUseChannelSelectors;
        asioInfo_.channelSelectors = channelSelectors_.data();
    } else {
        totalChannels_ = info->maxInputChannels;
    }

    if (totalChannels_ <= 0) {
        appendError(QStringLiteral("%1: no input channels to open").arg(devName));
        return false;
    }

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

    appendError(QStringLiteral("%1: failed to open after trying %2 sample rates")
                    .arg(devName)
                    .arg(sampleRates.size()));
    return false;
}

bool DeviceStream::tryOpen(const PaDeviceInfo *info, double sampleRate, bool useOutput)
{
    const QString devName = QString::fromLocal8Bit(info->name);

    PaStreamParameters inputParams{};
    inputParams.device = deviceIndex_;
    inputParams.channelCount = totalChannels_;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = isAsio_ ? &asioInfo_ : nullptr;

    PaStreamParameters outputParams{};
    PaStreamParameters *outputPtr = nullptr;
    int outputChannels = 0;
    if (useOutput && info->maxOutputChannels > 0) {
        outputChannels = info->maxOutputChannels >= 2 ? 2 : info->maxOutputChannels;
        outputParams.device = deviceIndex_;
        outputParams.channelCount = outputChannels;
        outputParams.sampleFormat = paFloat32;
        outputParams.suggestedLatency = info->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = nullptr;
        outputPtr = &outputParams;
    }

    PaError err = Pa_IsFormatSupported(&inputParams, outputPtr, sampleRate);
    if (err != paNoError) {
        QString line = QStringLiteral("%1: %2 Hz %3 not supported: %4")
                           .arg(devName)
                           .arg(static_cast<int>(sampleRate))
                           .arg(useOutput ? QStringLiteral("duplex") : QStringLiteral("input-only"))
                           .arg(QString::fromLocal8Bit(Pa_GetErrorText(err)));
        if (err == paUnanticipatedHostError) {
            const PaHostErrorInfo *hostErr = Pa_GetLastHostErrorInfo();
            if (hostErr && hostErr->errorText)
                line += QStringLiteral(" [%1]").arg(QString::fromLocal8Bit(hostErr->errorText));
        }
        appendError(line);
        return false;
    }

    PaStream *candidate = nullptr;
    err = Pa_OpenStream(&candidate,
                        &inputParams,
                        outputPtr,
                        sampleRate,
                        paFramesPerBufferUnspecified,
                        paClipOff,
                        paCallback,
                        this);
    if (err != paNoError) {
        QString line = QStringLiteral("%1: Pa_OpenStream failed (%2 Hz, %3): %4")
                           .arg(devName)
                           .arg(static_cast<int>(sampleRate))
                           .arg(useOutput ? QStringLiteral("duplex") : QStringLiteral("input-only"))
                           .arg(QString::fromLocal8Bit(Pa_GetErrorText(err)));
        if (err == paUnanticipatedHostError) {
            const PaHostErrorInfo *hostErr = Pa_GetLastHostErrorInfo();
            if (hostErr && hostErr->errorText)
                line += QStringLiteral(" [%1]").arg(QString::fromLocal8Bit(hostErr->errorText));
        }
        appendError(line);
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
    if (err != paNoError) {
        appendError(QStringLiteral("Device %1: Pa_StartStream failed: %2")
                        .arg(deviceIndex_)
                        .arg(QString::fromLocal8Bit(Pa_GetErrorText(err))));
        return false;
    }
    return true;
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
QString DeviceStream::lastError() const { return lastError_; }

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
        sub.track->pushSamples(input, self->totalChannels_, sub.offset - self->baseOffset_,
                               sub.count, framesPerBuffer);
    }
    return paContinue;
}
