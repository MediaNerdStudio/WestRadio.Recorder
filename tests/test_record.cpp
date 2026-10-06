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
#include "core/RecorderConfig.h"
#include "core/FfmpegTask.h"
#include "core/WavStreamWriter.h"
#include <cmath>

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

// "config" mode: round-trip a RecorderConfig through save/load.
static int runConfigTest()
{
    AudioEngine engine;
    if (!engine.isInitialized()) {
        std::cerr << "PortAudio not initialized" << std::endl;
        return 1;
    }

    QString apiName, devName;
    for (const auto &api : engine.audioApis()) {
        if (!api.name.contains(QStringLiteral("MME"), Qt::CaseInsensitive))
            continue;
        auto devices = engine.audioDevices(api.apiIndex);
        if (!devices.isEmpty()) {
            apiName = api.name;
            devName = devices.first().name;
            break;
        }
    }
    if (devName.isEmpty()) {
        std::cout << "No MME input device; skipping config test." << std::endl;
        return 0;
    }

    RecorderConfig cfg;
    cfg.outputDir = QStringLiteral("D:/tmp/out");
    cfg.mp3 = true;
    cfg.windowGeometry = QByteArrayLiteral("\x01\x02\x03dummy-geometry\xff");
    for (int i = 0; i < 2; ++i) {
        TrackConfig t;
        t.name = QStringLiteral("TRACK_%1").arg(i + 1);
        t.api = apiName;
        t.device = devName;
        t.firstChannel = 1 + i * 2;
        t.stereo = (i == 0);
        t.armed = (i != 1);
        cfg.tracks.append(t);
    }

    QString path = QDir::temp().filePath(QStringLiteral("wr_cfg.wrrec.json"));
    QString error;
    if (!RecorderConfig::save(path, cfg, &error)) {
        std::cerr << "save failed: " << error.toStdString() << std::endl;
        return 2;
    }
    std::cout << "Saved: " << path.toStdString() << std::endl;

    RecorderConfig loaded;
    if (!RecorderConfig::load(path, &loaded, &error)) {
        std::cerr << "load failed: " << error.toStdString() << std::endl;
        QFile::remove(path);
        return 3;
    }

    bool ok = loaded.outputDir == cfg.outputDir && loaded.mp3 == cfg.mp3 &&
              loaded.windowGeometry == cfg.windowGeometry &&
              loaded.tracks.size() == cfg.tracks.size();
    for (int i = 0; ok && i < cfg.tracks.size(); ++i) {
        const TrackConfig &a = cfg.tracks[i], &b = loaded.tracks[i];
        ok = a.name == b.name && a.api == b.api && a.device == b.device &&
             a.firstChannel == b.firstChannel && a.stereo == b.stereo && a.armed == b.armed;
    }

    QFile::remove(path);
    std::cout << (ok ? "CONFIG TEST PASS" : "CONFIG TEST FAIL") << std::endl;
    return ok ? 0 : 4;
}

// "ffmpeg" mode: resolve ffmpegPath() and run a real sine WAV -> MP3 encode.
static int runFfmpegTest()
{
    QString ffmpeg = FfmpegTask::ffmpegPath();
    std::cout << "ffmpegPath: " << (ffmpeg.isEmpty() ? "<not found>" : ffmpeg.toStdString())
              << std::endl;
    if (ffmpeg.isEmpty())
        return 2;

    QString wav = QDir::current().filePath(QStringLiteral("TEST_FFMPEG.wav"));
    QString mp3 = QDir::current().filePath(QStringLiteral("TEST_FFMPEG.mp3"));

    {
        WavStreamWriter writer;
        if (!writer.open(wav, 48000, 1)) {
            std::cerr << "could not open test wav" << std::endl;
            return 3;
        }
        std::vector<float> buf(480);
        for (int i = 0; i < 48000; i += 480) {
            for (int j = 0; j < 480; ++j)
                buf[j] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * (i + j) / 48000.0f);
            writer.write(buf.data(), 480);
        }
        writer.close();
    }

    bool ok = FfmpegTask::encodeToMp3(wav, mp3, 1)
              && QFile::exists(mp3) && QFileInfo(mp3).size() > 0;
    std::cout << (ok ? "FFMPEG TEST PASS" : "FFMPEG TEST FAIL") << std::endl;
    QFile::remove(wav);
    QFile::remove(mp3);
    return ok ? 0 : 4;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("monitor"))
        return runMonitorTest();
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("config"))
        return runConfigTest();
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("ffmpeg"))
        return runFfmpegTest();

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
