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

    elapsedTimer_ = new QTimer(this);
    elapsedTimer_->setInterval(1000);
    connect(elapsedTimer_, &QTimer::timeout, this, &MainWindow::updateElapsed);

    connect(engine_, &AudioEngine::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(engine_, &AudioEngine::recordingStopped, this, &MainWindow::onRecordingStopped);
    connect(engine_, &AudioEngine::recordingFailed,
            this, &MainWindow::onRecordingStartFailed, Qt::QueuedConnection);

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
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top toolbar
    auto *toolbar = new QFrame(this);
    toolbar->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #22262d; border: none; "
        "border-bottom: 1px solid #2e333b; }"));
    auto *topBar = new QHBoxLayout(toolbar);
    topBar->setContentsMargins(10, 6, 10, 6);
    topBar->setSpacing(10);

    auto *titleLabel = new QLabel(tr("WestRadio Recorder"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(11);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    topBar->addWidget(titleLabel);

    topBar->addSpacing(16);

    recordButton_ = new QPushButton(tr("\u25cf REC"), this);
    recordButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #e0322d; border: 1px solid #a02220; "
        "border-radius: 14px; color: #ffffff; font-weight: bold; padding: 4px 18px; }"
        "QPushButton:hover { background-color: #f0433a; }"
        "QPushButton:disabled { background-color: #5a2725; color: #8a9099; }"));
    recordButton_->setMinimumHeight(28);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::startRecording);
    topBar->addWidget(recordButton_);

    stopButton_ = new QPushButton(tr("\u25a0 Stop"), this);
    stopButton_->setEnabled(false);
    stopButton_->setMinimumHeight(28);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopRecording);
    topBar->addWidget(stopButton_);

    elapsedLabel_ = new QLabel(QStringLiteral("00:00:00"), this);
    QFont monoFont(QStringLiteral("Consolas"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(14);
    elapsedLabel_->setFont(monoFont);
    topBar->addWidget(elapsedLabel_);

    topBar->addStretch();

    armAllButton_ = new QPushButton(tr("Arm all"), this);
    armAllButton_->setFlat(true);
    connect(armAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto *w : trackWidgets_)
            w->setArmed(true);
        updateTrackSummary();
    });
    topBar->addWidget(armAllButton_);

    disarmAllButton_ = new QPushButton(tr("Disarm all"), this);
    disarmAllButton_->setFlat(true);
    connect(disarmAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto *w : trackWidgets_)
            w->setArmed(false);
        updateTrackSummary();
    });
    topBar->addWidget(disarmAllButton_);

    topBar->addSpacing(16);

    outputEdit_ = new QLineEdit(this);
    QString defaultOutput = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    outputEdit_->setText(defaultOutput);
    outputEdit_->setMaximumWidth(260);
    topBar->addWidget(outputEdit_);
    auto *browseButton = new QPushButton(tr("Browse"), this);
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    topBar->addWidget(browseButton);

    wavRadio_ = new QRadioButton(tr("WAV"), this);
    mp3Radio_ = new QRadioButton(tr("MP3"), this);
    wavRadio_->setChecked(true);
    topBar->addWidget(wavRadio_);
    topBar->addWidget(mp3Radio_);
    combinedCheck_ = new QCheckBox(tr("Combined WAV"), this);
    topBar->addWidget(combinedCheck_);

    mainLayout->addWidget(toolbar);

    // Track strips
    trackWidgets_.clear();
    tracks_.clear();

    tracksContainer_ = new QWidget(this);
    tracksContainer_->setStyleSheet(QStringLiteral("background-color: #15171c;"));
    tracksLayout_ = new QHBoxLayout(tracksContainer_);
    tracksLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    tracksLayout_->setSpacing(6);
    tracksLayout_->setContentsMargins(6, 6, 6, 6);

    addTrackButton_ = new QPushButton(tr("+"), this);
    addTrackButton_->setFixedWidth(48);
    addTrackButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    addTrackButton_->setToolTip(tr("Add source"));
    addTrackButton_->setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px dashed #3a4049; background-color: transparent; "
        "color: #8a9099; font-size: 20px; }"
        "QPushButton:hover { border-color: #3d8bff; color: #3d8bff; }"));
    connect(addTrackButton_, &QPushButton::clicked, this, &MainWindow::addTrack);
    tracksLayout_->addWidget(addTrackButton_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidget(tracksContainer_);
    mainLayout->addWidget(scrollArea_, 1);

    // Bottom status bar
    auto *statusBar = new QFrame(this);
    statusBar->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #22262d; border: none; "
        "border-top: 1px solid #2e333b; }"));
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(10, 4, 10, 4);
    statusLabel_ = new QLabel(tr("Ready"), this);
    statusLabel_->setProperty("dim", true);
    statusLayout->addWidget(statusLabel_, 1);
    trackSummaryLabel_ = new QLabel(this);
    trackSummaryLabel_->setProperty("dim", true);
    statusLayout->addWidget(trackSummaryLabel_);
    mainLayout->addWidget(statusBar);

    setCentralWidget(central);
    setWindowTitle(tr("WestRadio Recorder"));
    setMinimumSize(900, 600);
    resize(1400, 800);
}

void MainWindow::renumberTracks()
{
    for (int i = 0; i < trackWidgets_.size(); ++i)
        trackWidgets_[i]->setChannelNumber(i + 1);
}

void MainWindow::updateTrackSummary()
{
    int armed = 0;
    for (auto *w : trackWidgets_)
        if (w->track()->isArmed())
            ++armed;
    trackSummaryLabel_->setText(tr("%1 tracks \u00b7 %2 armed")
                                    .arg(trackWidgets_.size())
                                    .arg(armed));
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
    tracksLayout_->insertWidget(tracksLayout_->indexOf(addTrackButton_), widget);

    renumberTracks();
    updateTrackSummary();
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

    renumberTracks();
    updateTrackSummary();
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
    // Failure is reported via AudioEngine::recordingFailed.
    QThread *startThread = QThread::create([this]() {
        engine_->startRecording(outputEdit_->text());
    });
    connect(startThread, &QThread::finished, startThread, &QObject::deleteLater);
    startThread->start();
}

void MainWindow::onRecordingStartFailed(const QString &message)
{
    if (!recording_)
        recordButton_->setText(tr("\u25cf REC"));
    setUiEnabled(true);
    statusLabel_->setText(message.section(QLatin1Char('\n'), 0, 0));
    QMessageBox box(QMessageBox::Critical, tr("Recording failed"),
                    tr("Could not start recording."), QMessageBox::Ok, this);
    box.setDetailedText(message);
    box.exec();
}

void MainWindow::onRecordingStarted()
{
    recording_ = true;
    setUiEnabled(false);
    statusLabel_->setText(tr("Recording..."));
    recordButton_->setEnabled(false);
    recordButton_->setText(tr("Recording..."));
    stopButton_->setEnabled(true);
    elapsedLabel_->setText(QStringLiteral("00:00:00"));
    elapsedTimer_->start();
}

void MainWindow::updateElapsed()
{
    qint64 secs = recordStart_.secsTo(QDateTime::currentDateTime());
    int h = static_cast<int>(secs / 3600);
    int m = static_cast<int>((secs % 3600) / 60);
    int s = static_cast<int>(secs % 60);
    elapsedLabel_->setText(QStringLiteral("%1:%2:%3")
                               .arg(h, 2, 10, QLatin1Char('0'))
                               .arg(m, 2, 10, QLatin1Char('0'))
                               .arg(s, 2, 10, QLatin1Char('0')));
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
    elapsedTimer_->stop();
    elapsedLabel_->setText(QStringLiteral("00:00:00"));
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
    recordButton_->setText(tr("\u25cf REC"));

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
