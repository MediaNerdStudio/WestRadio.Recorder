#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <iostream>
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"

class RecorderTest : public QObject
{
    Q_OBJECT
public:
    RecorderTest(QObject *parent = nullptr) : QObject(parent), engine_(new AudioEngine(this)) {}

    void run(int milliseconds)
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
            auto devices = engine_->audioDevices(api.apiIndex);
            for (const auto &dev : devices) {
                std::cout << "  Device: " << dev.name.toStdString()
                          << " api: " << dev.apiName.toStdString()
                          << " channels: " << dev.maxInputChannels
                          << " sampleRate: " << dev.defaultSampleRate << std::endl;
                if (selectedDevice == paNoDevice && dev.maxInputChannels > 0) {
                    selectedApi = api.apiIndex;
                    selectedDevice = dev.deviceIndex;
                }
            }
        }

        if (selectedDevice == paNoDevice) {
            std::cout << "No input devices found; skipping live test." << std::endl;
            qApp->exit(0);
            return;
        }

        track_ = new AudioTrack();
        track_->setName(QStringLiteral("TEST"));
        track_->setApiIndex(selectedApi);
        track_->setDeviceIndex(selectedDevice);
        track_->setChannelCount(1);
        track_->setChannelOffset(0);
        track_->setArmed(true);
        track_->setFormat(AudioTrack::WAV);

        engine_->addTrack(track_);

        QString outputDir = QDir::currentPath();
        if (!engine_->startRecording(outputDir)) {
            std::cerr << "Failed to start recording" << std::endl;
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int duration = 3000;
    if (argc > 1)
        duration = QString::fromLocal8Bit(argv[1]).toInt();

    RecorderTest test;
    QTimer::singleShot(0, &test, [&test, duration]() { test.run(duration); });
    return app.exec();
}
