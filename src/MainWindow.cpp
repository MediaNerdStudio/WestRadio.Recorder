#include "MainWindow.h"
#include "ui/TrackWidget.h"
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"
#include "core/FfmpegTask.h"
#include <QApplication>
#include <QDir>
#include <QWidget>
#include <portaudio.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <QRegularExpression>
#include <QSettings>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), engine_(new AudioEngine(this))
{
    buildUi();

    meterTimer_ = new QTimer(this);
    connect(meterTimer_, &QTimer::timeout, this, &MainWindow::updateMeters);
    meterTimer_->start(50);

    connect(engine_, &AudioEngine::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(engine_, &AudioEngine::recordingStopped, this, &MainWindow::onRecordingStopped);

    FfmpegTask::setProgressCallback([this](const QString &msg) {
        QMetaObject::invokeMethod(this, "onPostProcessMessage", Q_ARG(QString, msg));
    });

    addTrack();
}

MainWindow::~MainWindow()
{
    engine_->stopRecording();
    for (AudioTrack *t : tracks_)
        delete t;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    // Output directory row
    auto *outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel(tr("Output folder"), this));
    outputEdit_ = new QLineEdit(this);
    QString defaultOutput = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    outputEdit_->setText(defaultOutput);
    outputLayout->addWidget(outputEdit_);
    auto *browseButton = new QPushButton(tr("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    outputLayout->addWidget(browseButton);
    mainLayout->addLayout(outputLayout);

    // Format row
    auto *formatLayout = new QHBoxLayout();
    formatLayout->addWidget(new QLabel(tr("Recording format"), this));
    wavRadio_ = new QRadioButton(tr("WAV"), this);
    mp3Radio_ = new QRadioButton(tr("MP3"), this);
    wavRadio_->setChecked(true);
    formatLayout->addWidget(wavRadio_);
    formatLayout->addWidget(mp3Radio_);
    combinedCheck_ = new QCheckBox(tr("Also create one combined multi-channel WAV"), this);
    formatLayout->addWidget(combinedCheck_);
    formatLayout->addStretch();
    mainLayout->addLayout(formatLayout);

    // Track list
    trackWidgets_.clear();
    tracks_.clear();

    tracksContainer_ = new QWidget(this);
    tracksLayout_ = new QHBoxLayout(tracksContainer_);
    tracksLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    tracksLayout_->setSpacing(8);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidget(tracksContainer_);
    mainLayout->addWidget(scrollArea_, 1);

    // Controls
    auto *controls = new QHBoxLayout();
    addTrackButton_ = new QPushButton(tr("+ Add source"), this);
    connect(addTrackButton_, &QPushButton::clicked, this, &MainWindow::addTrack);
    controls->addWidget(addTrackButton_);

    armAllButton_ = new QPushButton(tr("Arm all"), this);
    connect(armAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto *w : trackWidgets_) {
            w->track()->setArmed(true);
            // UI will be refreshed by updateMeters / could add a setter
        }
    });
    controls->addWidget(armAllButton_);

    disarmAllButton_ = new QPushButton(tr("Disarm all"), this);
    connect(disarmAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto *w : trackWidgets_)
            w->track()->setArmed(false);
    });
    controls->addWidget(disarmAllButton_);

    controls->addStretch();

    recordButton_ = new QPushButton(tr("Record"), this);
    recordButton_->setStyleSheet(QStringLiteral("QPushButton { background-color: #c00000; color: white; font-weight: bold; }"));
    recordButton_->setMinimumWidth(100);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::startRecording);
    controls->addWidget(recordButton_);

    stopButton_ = new QPushButton(tr("Stop"), this);
    stopButton_->setEnabled(false);
    stopButton_->setMinimumWidth(100);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopRecording);
    controls->addWidget(stopButton_);

    mainLayout->addLayout(controls);

    statusLabel_ = new QLabel(tr("Ready"), this);
    mainLayout->addWidget(statusLabel_);

    setCentralWidget(central);
    setWindowTitle(tr("WestRadio Recorder"));
    resize(1200, 700);
}

void MainWindow::addTrack()
{
    auto *track = new AudioTrack();
    track->setName(QStringLiteral("TRACK_%1").arg(tracks_.size() + 1));
    track->setArmed(true);

    auto *widget = new TrackWidget(engine_, track, tracksContainer_);
    connect(widget, &TrackWidget::removeRequested, this, &MainWindow::removeTrackWidget);

    tracks_.append(track);
    trackWidgets_.append(widget);
    tracksLayout_->addWidget(widget);
}

void MainWindow::removeTrackWidget(TrackWidget *widget)
{
    if (recording_) {
        QMessageBox::warning(this, tr("Cannot remove"), tr("Stop recording before removing sources."));
        return;
    }

    int idx = trackWidgets_.indexOf(widget);
    if (idx >= 0) {
        tracksLayout_->removeWidget(widget);
        AudioTrack *track = widget->track();
        trackWidgets_.removeAt(idx);
        tracks_.removeAll(track);
        engine_->removeTrack(track);
        widget->deleteLater();
        delete track;
    }
}

void MainWindow::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select output folder"), outputEdit_->text());
    if (!dir.isEmpty())
        outputEdit_->setText(dir);
}

QString MainWindow::safeFileName(const QString &name) const
{
    return name.trimmed().replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
}

void MainWindow::startRecording()
{
    if (recording_)
        return;

    if (!engine_->isInitialized()) {
        QMessageBox::critical(this, tr("Audio engine error"), tr("PortAudio could not be initialized."));
        return;
    }

    if (trackWidgets_.isEmpty()) {
        QMessageBox::warning(this, tr("No tracks"), tr("Add at least one source before recording."));
        return;
    }

    bool anyArmed = false;
    for (TrackWidget *w : trackWidgets_) {
        AudioTrack *t = w->track();
        if (!t->isArmed())
            continue;
        anyArmed = true;
        if (t->deviceIndex() == paNoDevice) {
            QMessageBox::warning(this, tr("Invalid device"),
                                 tr("Track '%1' has no audio device selected.").arg(t->name()));
            return;
        }
        t->setName(safeFileName(t->name()));
        if (t->name().isEmpty()) {
            QMessageBox::warning(this, tr("Invalid name"), tr("Track name cannot be empty."));
            return;
        }
    }

    if (!anyArmed) {
        QMessageBox::warning(this, tr("No armed tracks"), tr("Arm at least one track to record."));
        return;
    }

    recordStart_ = QDateTime::currentDateTime();
    recordButton_->setEnabled(false);
    recordButton_->setText(tr("Starting..."));

    AudioTrack::Format fmt = wavRadio_->isChecked() ? AudioTrack::WAV : AudioTrack::MP3;
    engine_->clearTracks();
    for (AudioTrack *t : tracks_) {
        t->setFormat(fmt);
        engine_->addTrack(t);
    }

    // Opening ASIO drivers can block the UI, so start recording on a background thread.
    QThread *startThread = QThread::create([this]() {
        if (!engine_->startRecording(outputEdit_->text()))
            QMetaObject::invokeMethod(this, "onRecordingStartFailed", Qt::QueuedConnection);
    });
    connect(startThread, &QThread::finished, startThread, &QObject::deleteLater);
    startThread->start();
}

void MainWindow::onRecordingStartFailed()
{
    if (!recording_)
        recordButton_->setText(tr("Record"));
    setUiEnabled(true);
    QMessageBox::critical(this, tr("Recording failed"), tr("Could not start one or more audio streams."));
}

void MainWindow::onRecordingStarted()
{
    recording_ = true;
    setUiEnabled(false);
    statusLabel_->setText(tr("Recording..."));
    recordButton_->setEnabled(false);
    recordButton_->setText(tr("Recording..."));
    stopButton_->setEnabled(true);
}

void MainWindow::stopRecording()
{
    if (!recording_ || stopping_)
        return;

    stopping_ = true;
    statusLabel_->setText(tr("Stopping..."));
    stopButton_->setEnabled(false);

    // Stopping some ASIO drivers can block for a moment, so run it off the UI thread.
    QThread *stopThread = QThread::create([this]() {
        engine_->stopRecording();
    });
    connect(stopThread, &QThread::finished, stopThread, &QObject::deleteLater);
    stopThread->start();
}

void MainWindow::onRecordingStopped()
{
    recording_ = false;
    stopping_ = false;
    recordButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    statusLabel_->setText(tr("Encoding / finishing..."));

    postProcess();
}

void MainWindow::setUiEnabled(bool enabled)
{
    addTrackButton_->setEnabled(enabled);
    outputEdit_->setEnabled(enabled);
    wavRadio_->setEnabled(enabled);
    mp3Radio_->setEnabled(enabled);
    combinedCheck_->setEnabled(enabled);
    armAllButton_->setEnabled(enabled);
    disarmAllButton_->setEnabled(enabled);
    recordButton_->setEnabled(enabled);
    stopButton_->setEnabled(false);
    for (TrackWidget *w : trackWidgets_)
        w->setEnabled(enabled);
}

void MainWindow::updateMeters()
{
    for (TrackWidget *w : trackWidgets_)
        w->refreshMeter();
}

void MainWindow::onPostProcessMessage(const QString &message)
{
    statusLabel_->setText(message);
}

void MainWindow::onPostProcessFinished()
{
    postProcessing_ = false;
    setUiEnabled(true);
    recordButton_->setText(tr("Record"));

    QStringList created;
    for (AudioTrack *t : tracks_) {
        QString p = t->finalPath();
        if (!p.isEmpty())
            created.append(QDir::toNativeSeparators(p));
    }

    if (!created.isEmpty())
        statusLabel_->setText(tr("Saved: %1").arg(created.join(QStringLiteral("; "))));
    else
        statusLabel_->setText(tr("Ready"));
}

void MainWindow::postProcess()
{
    if (postProcessing_)
        return;
    postProcessing_ = true;

    // Read UI state on the main thread before handing work to the background thread.
    const bool needCombined = combinedCheck_->isChecked();
    const bool mp3Selected = mp3Radio_->isChecked();
    const QString outputDir = outputEdit_->text();
    const QDateTime recordStart = recordStart_;

    struct TrackInfo {
        AudioTrack *track;
        QString wav;
        int channels;
        int sampleRate;
    };
    QVector<TrackInfo> armedTracks;
    for (AudioTrack *t : tracks_) {
        if (!t->isArmed())
            continue;
        armedTracks.append({t, t->currentWavPath(), t->channelCount(), static_cast<int>(t->sampleRate())});
    }

    // Run FFmpeg work on a background thread so the UI stays responsive.
    QThread *worker = QThread::create([=]() {
        QStringList wavPaths;
        int totalChannels = 0;

        for (const auto &info : armedTracks) {
            if (info.wav.isEmpty())
                continue;
            wavPaths.append(info.wav);
            totalChannels += info.channels;

            if (mp3Selected) {
                QString mp3 = info.wav;
                mp3.replace(mp3.size() - 4, 4, QStringLiteral(".mp3"));
                if (FfmpegTask::encodeToMp3(info.wav, mp3, info.sampleRate, info.channels)) {
                    info.track->setFinalPath(mp3);
                } else {
                    info.track->setFinalPath(info.wav);
                }
            } else {
                info.track->setFinalPath(info.wav);
            }
        }

        if (needCombined && wavPaths.size() >= 1) {
            QString base = QStringLiteral("COMBINED_%1").arg(recordStart.toString(QStringLiteral("yyyy-MM-dd_HHmmss")));
            QString combinedPath = QDir(outputDir).filePath(base + QStringLiteral(".wav"));
            FfmpegTask::combineWav(combinedPath, wavPaths, totalChannels);
        }

        // Clean up temporary WAVs when MP3 was requested.
        if (mp3Selected) {
            for (const auto &info : armedTracks) {
                if (info.track->format() == AudioTrack::MP3) {
                    QString wav = info.wav;
                    QString final = info.track->finalPath();
                    if (QFile::exists(wav) && final != wav)
                        QFile::remove(wav);
                }
            }
        }

        QMetaObject::invokeMethod(this, "onPostProcessFinished");
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopRecording();

    // Give the stop thread a moment to finish if a recording is active.
    if (recording_ || stopping_ || postProcessing_) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(engine_, &AudioEngine::recordingStopped, &loop, &QEventLoop::quit);
        timer.start(3000);
        loop.exec();
    }

    event->accept();
}
