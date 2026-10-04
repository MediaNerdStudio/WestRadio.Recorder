#include "AudioEngine.h"
#include "AudioTrack.h"
#include "DeviceStream.h"
#include <QDebug>
#include <QDateTime>
#include <QSet>

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
    shuttingDown_ = true;
    stopRecording();
    teardownStreams();
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

bool AudioEngine::isRecording() const { return state_ == State::Recording; }
bool AudioEngine::isMonitoring() const { return state_ == State::Monitoring; }

QString AudioEngine::lastError() const { return lastError_; }

bool AudioEngine::fail(const QString &message)
{
    lastError_ = message;
    qWarning().noquote() << "AudioEngine:" << message;
    emit recordingFailed(message);
    return false;
}

// Groups armed tracks by device index, validating channel ranges.
QMap<PaDeviceIndex, QVector<AudioTrack *>> AudioEngine::buildGroups(QStringList *errors) const
{
    QMap<PaDeviceIndex, QVector<AudioTrack *>> groups;
    for (AudioTrack *track : tracks_) {
        if (!track || !track->isArmed() || track->deviceIndex() == paNoDevice)
            continue;
        const PaDeviceInfo *info = Pa_GetDeviceInfo(track->deviceIndex());
        if (!info)
            continue;
        if (track->channelOffset() + track->channelCount() > info->maxInputChannels) {
            if (errors) {
                errors->append(tr("Track '%1': channels %2-%3 exceed device '%4' input count (%5)")
                                   .arg(track->name())
                                   .arg(track->channelOffset() + 1)
                                   .arg(track->channelOffset() + track->channelCount())
                                   .arg(QString::fromLocal8Bit(info->name))
                                   .arg(info->maxInputChannels));
            }
            continue;
        }
        groups[track->deviceIndex()].append(track);
    }
    return groups;
}

// Opens (does not start) one PortAudio stream per device group. In strict mode
// the first failure returns false with *error set; in lenient mode failing
// groups are skipped and reported via monitoringError instead.
bool AudioEngine::openStreams(const QMap<PaDeviceIndex, QVector<AudioTrack *>> &groups,
                              bool lenient, QString *error)
{
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        PaDeviceIndex devIdx = it.key();
        QVector<AudioTrack *> tracks = it.value();

        std::vector<DeviceStream::Subscriber> subs;
        QStringList trackNames;
        for (AudioTrack *track : tracks) {
            subs.push_back({track, track->channelOffset(), track->channelCount()});
            trackNames.append(track->name());
        }

        auto *ds = new DeviceStream();
        if (!ds->open(devIdx, subs)) {
            QString msg = tr("Could not open audio stream for tracks [%1]:\n%2")
                              .arg(trackNames.join(QStringLiteral(", ")))
                              .arg(ds->lastError());
            delete ds;
            if (lenient) {
                qWarning().noquote() << "AudioEngine monitoring:" << msg;
                emit monitoringError(msg);
                continue;
            }
            if (error)
                *error = msg;
            return false;
        }

        deviceStreams_.insert(devIdx, ds);
    }
    return true;
}

bool AudioEngine::startRecording(const QString &outputDir)
{
    if (!initialized_)
        return fail(tr("PortAudio is not initialized."));

    // Stop any running recording (emits recordingStopped) or monitoring
    // (silent teardown) before opening the record streams. Don't let
    // stopRecording auto-resume monitoring — we immediately re-open.
    suppressMonitorResume_ = true;
    stopRecording();
    suppressMonitorResume_ = false;
    teardownStreams();
    state_ = State::Idle;
    lastError_.clear();

    QDateTime startTime = QDateTime::currentDateTime();

    QStringList rangeErrors;
    QMap<PaDeviceIndex, QVector<AudioTrack *>> groups = buildGroups(&rangeErrors);

    if (groups.isEmpty()) {
        QString msg = tr("No armed tracks with valid devices");
        if (!rangeErrors.isEmpty())
            msg += QLatin1Char('\n') + rangeErrors.join(QLatin1Char('\n'));
        return fail(msg);
    }

    // PortAudio can only have one ASIO stream (and therefore one ASIO device)
    // open at a time.
    QSet<PaDeviceIndex> asioDevices;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(it.key());
        const PaHostApiInfo *apiInfo = info ? Pa_GetHostApiInfo(info->hostApi) : nullptr;
        if (apiInfo && apiInfo->type == paASIO)
            asioDevices.insert(it.key());
    }
    if (asioDevices.size() > 1)
        return fail(tr("PortAudio can only open one ASIO device at a time; "
                       "put all ASIO tracks on the same device."));

    // Open one PortAudio stream per device first so each track's writer is
    // created with the sample rate the stream actually settled on.
    QString openError;
    if (!openStreams(groups, false, &openError)) {
        cleanupOnStartFailure();
        return fail(openError);
    }

    // Start each track's writer with the negotiated sample rate before audio
    // starts arriving.
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        DeviceStream *ds = deviceStreams_.value(it.key());
        for (AudioTrack *track : it.value()) {
            if (ds)
                track->setSampleRate(ds->sampleRate());
            if (!track->start(startTime, outputDir)) {
                QString msg = tr("Failed to start track writer for '%1'").arg(track->name());
                cleanupOnStartFailure();
                return fail(msg);
            }
        }
    }

    // Writers are ready; start the streams.
    for (auto it = deviceStreams_.begin(); it != deviceStreams_.end(); ++it) {
        DeviceStream *ds = it.value();
        if (!ds->start()) {
            QString msg = tr("Could not start audio stream:\n%1").arg(ds->lastError());
            cleanupOnStartFailure();
            return fail(msg);
        }
    }

    state_ = State::Recording;
    emit recordingStarted();
    return true;
}

bool AudioEngine::startMonitoring()
{
    if (!initialized_ || state_ == State::Recording)
        return false;
    if (state_ == State::Monitoring)
        return true;

    QStringList rangeErrors;
    QMap<PaDeviceIndex, QVector<AudioTrack *>> groups = buildGroups(&rangeErrors);
    for (const QString &e : rangeErrors)
        emit monitoringError(e);

    if (groups.isEmpty()) {
        state_ = State::Idle;
        return true;
    }

    // PortAudio can only have one ASIO stream open at a time: monitor only the
    // first ASIO device and warn about the rest.
    PaDeviceIndex firstAsio = paNoDevice;
    QStringList skippedAsio;
    for (auto it = groups.begin(); it != groups.end();) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(it.key());
        const PaHostApiInfo *apiInfo = info ? Pa_GetHostApiInfo(info->hostApi) : nullptr;
        if (apiInfo && apiInfo->type == paASIO) {
            if (firstAsio == paNoDevice) {
                firstAsio = it.key();
                ++it;
            } else {
                for (AudioTrack *t : it.value())
                    skippedAsio.append(t->name());
                it = groups.erase(it);
            }
        } else {
            ++it;
        }
    }
    if (!skippedAsio.isEmpty())
        emit monitoringError(tr("PortAudio can only open one ASIO device at a time; "
                                "not monitoring tracks [%1].")
                                 .arg(skippedAsio.join(QStringLiteral(", "))));

    // Lenient: keep monitoring the devices that do open.
    openStreams(groups, true, nullptr);

    for (auto it = deviceStreams_.begin(); it != deviceStreams_.end();) {
        DeviceStream *ds = it.value();
        if (!ds->start()) {
            emit monitoringError(tr("Could not start monitoring stream:\n%1").arg(ds->lastError()));
            ds->close();
            delete ds;
            it = deviceStreams_.erase(it);
        } else {
            ++it;
        }
    }

    state_ = deviceStreams_.isEmpty() ? State::Idle : State::Monitoring;
    return true;
}

void AudioEngine::stopMonitoring()
{
    if (state_ != State::Monitoring)
        return;
    teardownStreams();
    state_ = State::Idle;
}

void AudioEngine::refreshMonitoring()
{
    if (state_ == State::Recording)
        return;
    teardownStreams();
    state_ = State::Idle;
    startMonitoring();
}

void AudioEngine::teardownStreams()
{
    for (auto it = deviceStreams_.begin(); it != deviceStreams_.end(); ++it) {
        it.value()->stop();
        it.value()->close();
        delete it.value();
    }
    deviceStreams_.clear();
}

void AudioEngine::cleanupOnStartFailure()
{
    teardownStreams();

    for (AudioTrack *track : tracks_)
        if (track)
            track->stop();
}

bool AudioEngine::stopRecording()
{
    bool wasRecording = (state_ == State::Recording);
    for (AudioTrack *track : tracks_)
        if (track && track->isRecording())
            wasRecording = true;

    // Stop capture first so the ring buffers stop receiving data.
    teardownStreams();

    for (AudioTrack *track : tracks_)
        if (track)
            track->stop();

    state_ = State::Idle;

    if (wasRecording) {
        emit recordingStopped();
        // Keep the input meters alive after a real recording stops.
        if (!shuttingDown_ && !suppressMonitorResume_)
            startMonitoring();
    }
    return true;
}
