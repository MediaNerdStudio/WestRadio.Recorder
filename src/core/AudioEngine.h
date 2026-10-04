#pragma once

#include <QObject>
#include <QVector>
#include <QString>
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

signals:
    void recordingStarted();
    void recordingStopped();

private:
    void cleanupOnStartFailure();

    bool initialized_ = false;
    QVector<AudioTrack *> tracks_;
    QMap<PaDeviceIndex, DeviceStream *> deviceStreams_;
};
