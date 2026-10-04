#include "AudioEngine.h"
#include "AudioTrack.h"
#include "DeviceStream.h"
#include <QDebug>
#include <QDateTime>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    PaError err = Pa_Initialize();
    if (err == paNoError)
        initialized_ = true;
    else
        qWarning() << "PortAudio init failed:" << Pa_GetErrorText(err);
}

AudioEngine::~AudioEngine()
{
    stopRecording();
    Pa_Terminate();
}

bool AudioEngine::isInitialized() const { return initialized_; }

QVector<AudioApiInfo> AudioEngine::audioApis() const
{
    QVector<AudioApiInfo> apis;
    if (!initialized_)
        return apis;

    int count = Pa_GetHostApiCount();
    for (int i = 0; i < count; ++i) {
        const PaHostApiInfo *info = Pa_GetHostApiInfo(i);
        if (info)
            apis.append({i, QString::fromLocal8Bit(info->name)});
    }
    return apis;
}

QVector<AudioDeviceInfo> AudioEngine::audioDevices(int apiIndex) const
{
    QVector<AudioDeviceInfo> devices;
    if (!initialized_)
        return devices;

    const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(apiIndex);
    if (!apiInfo)
        return devices;

    for (int i = 0; i < apiInfo->deviceCount; ++i) {
        PaDeviceIndex idx = Pa_HostApiDeviceIndexToDeviceIndex(apiIndex, i);
        const PaDeviceInfo *info = Pa_GetDeviceInfo(idx);
        if (!info || info->maxInputChannels <= 0)
            continue;

        devices.append({idx,
                        apiIndex,
                        QString::fromLocal8Bit(apiInfo->name),
                        QString::fromLocal8Bit(info->name),
                        info->maxInputChannels,
                        info->defaultSampleRate});
    }
    return devices;
}

void AudioEngine::addTrack(AudioTrack *track)
{
    if (track)
        tracks_.append(track);
}

void AudioEngine::removeTrack(AudioTrack *track)
{
    tracks_.removeAll(track);
}

void AudioEngine::clearTracks()
{
    tracks_.clear();
}

const QVector<AudioTrack *> &AudioEngine::tracks() const { return tracks_; }

bool AudioEngine::isRecording() const
{
    for (auto *ds : deviceStreams_)
        if (ds)
            return true;
    return false;
}

bool AudioEngine::startRecording(const QString &outputDir)
{
    if (!initialized_)
        return false;

    stopRecording();

    QDateTime startTime = QDateTime::currentDateTime();

    // Set the sample rate for each armed track from its device and group by device index.
    QMap<PaDeviceIndex, QVector<AudioTrack *>> groups;
    for (AudioTrack *track : tracks_) {
        if (!track || !track->isArmed() || track->deviceIndex() == paNoDevice)
            continue;
        const PaDeviceInfo *info = Pa_GetDeviceInfo(track->deviceIndex());
        if (!info)
            continue;
        if (track->channelOffset() + track->channelCount() > info->maxInputChannels)
            continue;
        track->setSampleRate(info->defaultSampleRate);
        groups[track->deviceIndex()].append(track);
    }

    if (groups.isEmpty()) {
        qWarning() << "No armed tracks with valid devices";
        return false;
    }

    // Start each track's writer first so buffers are ready before audio arrives.
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        for (AudioTrack *track : it.value()) {
            if (!track->start(startTime, outputDir)) {
                qWarning() << "Failed to start track writer for" << track->name();
                stopRecording();
                return false;
            }
        }
    }

    // Open and start one PortAudio stream per device.
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        PaDeviceIndex devIdx = it.key();
        QVector<AudioTrack *> tracks = it.value();

        std::vector<DeviceStream::Subscriber> subs;
        for (AudioTrack *track : tracks) {
            subs.push_back({track, track->channelOffset(), track->channelCount()});
        }

        auto *ds = new DeviceStream();
        if (!ds->open(devIdx, subs)) {
            delete ds;
            cleanupOnStartFailure();
            return false;
        }

        if (!ds->start()) {
            ds->close();
            delete ds;
            cleanupOnStartFailure();
            return false;
        }

        deviceStreams_.insert(devIdx, ds);
    }

    emit recordingStarted();
    return true;
}

void AudioEngine::cleanupOnStartFailure()
{
    for (auto it = deviceStreams_.begin(); it != deviceStreams_.end(); ++it) {
        it.value()->stop();
        it.value()->close();
        delete it.value();
    }
    deviceStreams_.clear();

    for (AudioTrack *track : tracks_)
        if (track)
            track->stop();
}

bool AudioEngine::stopRecording()
{
    // Stop capture first so the ring buffers stop receiving data.
    for (auto it = deviceStreams_.begin(); it != deviceStreams_.end(); ++it) {
        it.value()->stop();
        it.value()->close();
        delete it.value();
    }
    deviceStreams_.clear();

    for (AudioTrack *track : tracks_)
        if (track)
            track->stop();

    emit recordingStopped();
    return true;
}
