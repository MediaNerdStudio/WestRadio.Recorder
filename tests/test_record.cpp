#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <iostream>
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"

class RecorderTest : public QObject
{
    Q_OBJECT
public:
    RecorderTest(QObject *parent = nullptr) : QObject(parent), engine_(new AudioEngine(this)) {}

    void run(int milliseconds, const QString &apiFilter = QString(),
             const QString &deviceFilter = QString(), int channelOffset = 0)
    {
        if (!engine_->isInitialized()) {
            std::cerr << "PortAudio not initialized" << std::endl;
            qApp->exit(1);
            return;
        }

        auto apis = engine_->audioApis();
        std::cout << "Available APIs:" << std::endl;
        for (const auto &api : apis)
            std::cout << "  " << api.apiIndex << " " << api.name.toStdString() << std::endl;

        int selectedApi = -1;
        PaDeviceIndex selectedDevice = paNoDevice;
        for (const auto &api : apis) {
            if (!apiFilter.isEmpty() && !api.name.contains(apiFilter, Qt::CaseInsensitive))
                continue;
            auto devices = engine_->audioDevices(api.apiIndex);
            for (const auto &dev : devices) {
                std::cout << "  Device: " << dev.name.toStdString()
                          << " api: " << dev.apiName.toStdString()
                          << " channels: " << dev.maxInputChannels
                          << " sampleRate: " << dev.defaultSampleRate << std::endl;
                if (selectedDevice == paNoDevice && dev.maxInputChannels > 0 &&
                    (deviceFilter.isEmpty() || dev.name.contains(deviceFilter, Qt::CaseInsensitive))) {
                    selectedApi = api.apiIndex;
                    selectedDevice = dev.deviceIndex;
                }
            }
        }

        if (selectedDevice == paNoDevice) {
            std::cout << "No input devices found" << (apiFilter.isEmpty() ? "" : " for API ")
                      << apiFilter.toStdString()
                      << (deviceFilter.isEmpty() ? "" : " matching ")
                      << deviceFilter.toStdString() << "; skipping live test." << std::endl;
            qApp->exit(0);
            return;
        }

        track_ = new AudioTrack();
        track_->setName(QStringLiteral("TEST"));
        track_->setApiIndex(selectedApi);
        track_->setDeviceIndex(selectedDevice);
        track_->setChannelCount(1);
        track_->setChannelOffset(channelOffset);
        track_->setArmed(true);
        track_->setFormat(AudioTrack::WAV);

        engine_->addTrack(track_);

        QString outputDir = QDir::currentPath();
        if (!engine_->startRecording(outputDir)) {
            std::cerr << "Failed to start recording:\n"
                      << engine_->lastError().toStdString() << std::endl;
            qApp->exit(2);
            return;
        }

        std::cout << "Recording for " << milliseconds << " ms..." << std::endl;
        QTimer::singleShot(milliseconds, this, &RecorderTest::stop);
    }

private slots:
    void stop()
    {
        engine_->stopRecording();

        QString path = track_->finalPath();
        std::cout << "Output file: " << path.toStdString() << std::endl;
        if (QFile::exists(path)) {
            QFileInfo info(path);
            std::cout << "File size: " << info.size() << " bytes" << std::endl;
            qApp->exit(0);
        } else {
            std::cerr << "Output file does not exist!" << std::endl;
            qApp->exit(3);
        }
    }

private:
    AudioEngine *engine_;
    AudioTrack *track_ = nullptr;
};

#include "test_record.moc"

static int countTestWavs()
{
    return QDir::current().entryList({QStringLiteral("TEST_*.wav")}, QDir::Files).size();
}

// "monitor" mode: verify armed tracks get live input levels without recording.
static int runMonitorTest()
{
    AudioEngine engine;
    if (!engine.isInitialized()) {
        std::cerr << "PortAudio not initialized" << std::endl;
        return 1;
    }

    PaDeviceIndex device = paNoDevice;
    for (const auto &api : engine.audioApis()) {
        if (!api.name.contains(QStringLiteral("MME"), Qt::CaseInsensitive))
            continue;
        for (const auto &dev : engine.audioDevices(api.apiIndex)) {
            if (dev.maxInputChannels > 0) {
                device = dev.deviceIndex;
                std::cout << "Monitor device: " << dev.name.toStdString() << std::endl;
                break;
            }
        }
        if (device != paNoDevice)
            break;
    }
    if (device == paNoDevice) {
        std::cout << "No MME input device; skipping monitor test." << std::endl;
        return 0;
    }

    AudioTrack *track = new AudioTrack();
    track->setName(QStringLiteral("TEST"));
    track->setDeviceIndex(device);
    track->setChannelCount(1);
    track->setChannelOffset(0);
    track->setArmed(true);
    engine.addTrack(track);

    if (!engine.startMonitoring()) {
        std::cerr << "startMonitoring failed" << std::endl;
        return 2;
    }
    std::cout << "isMonitoring after start: " << engine.isMonitoring() << std::endl;

    float maxPeak = 0.0f;
    for (int i = 0; i < 20; ++i) {
        QThread::msleep(100);
        maxPeak = std::max(maxPeak, track->readLeftPeak());
    }
    std::cout << "Max peak during monitoring: " << maxPeak << std::endl;

    int wavsBefore = countTestWavs();

    if (!engine.startRecording(QDir::currentPath())) {
        std::cerr << "startRecording failed:\n" << engine.lastError().toStdString() << std::endl;
        return 3;
    }
    std::cout << "Recording for 1000 ms..." << std::endl;
    QThread::msleep(1000);
    engine.stopRecording();

    std::cout << "isMonitoring after stop: " << engine.isMonitoring() << std::endl;
    std::cout << "TEST wavs before record: " << wavsBefore
              << ", after record: " << countTestWavs() << std::endl;

    bool ok = engine.isMonitoring() && wavsBefore == 0 && countTestWavs() == 1;
    std::cout << (ok ? "MONITOR TEST PASS" : "MONITOR TEST FAIL") << std::endl;
    engine.stopMonitoring();
    delete track;
    return ok ? 0 : 4;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("monitor"))
        return runMonitorTest();

    int duration = 3000;
    QString apiFilter;
    QString deviceFilter;
    int channelOffset = 0;
    if (argc > 1)
        duration = QString::fromLocal8Bit(argv[1]).toInt();
    if (argc > 2)
        apiFilter = QString::fromLocal8Bit(argv[2]);
    if (argc > 3)
        deviceFilter = QString::fromLocal8Bit(argv[3]);
    if (argc > 4)
        channelOffset = QString::fromLocal8Bit(argv[4]).toInt();

    RecorderTest test;
    QTimer::singleShot(0, &test, [&test, duration, apiFilter, deviceFilter, channelOffset]() {
        test.run(duration, apiFilter, deviceFilter, channelOffset);
    });
    return app.exec();
}
