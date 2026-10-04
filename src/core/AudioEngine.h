#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <portaudio.h>
#include <memory>

class AudioTrack;
class DeviceStream;

struct AudioApiInfo {
    int apiIndex;
    QString name;
};

struct AudioDeviceInfo {
    PaDeviceIndex deviceIndex;
    int apiIndex;
    QString apiName;
    QString name;
    int maxInputChannels;
    double defaultSampleRate;
};

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    bool isInitialized() const;

    QVector<AudioApiInfo> audioApis() const;
    QVector<AudioDeviceInfo> audioDevices(int apiIndex) const;

    void addTrack(AudioTrack *track);
    void removeTrack(AudioTrack *track);
    void clearTracks();
    const QVector<AudioTrack *> &tracks() const;

    bool isRecording() const;
    bool startRecording(const QString &outputDir);
    bool stopRecording();
    QString lastError() const;

    bool startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const;
    void refreshMonitoring();

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingFailed(const QString &message);
    void monitoringError(const QString &message);

private:
    enum class State { Idle, Monitoring, Recording };

    void teardownStreams();
    void cleanupOnStartFailure();
    bool fail(const QString &message);
    QMap<PaDeviceIndex, QVector<AudioTrack *>> buildGroups(QStringList *errors) const;
    bool openStreams(const QMap<PaDeviceIndex, QVector<AudioTrack *>> &groups,
                     bool lenient, QString *error);

    bool initialized_ = false;
    bool shuttingDown_ = false;
    bool suppressMonitorResume_ = false;
    QString lastError_;
    State state_ = State::Idle;
    QVector<AudioTrack *> tracks_;
    QMap<PaDeviceIndex, DeviceStream *> deviceStreams_;
};
