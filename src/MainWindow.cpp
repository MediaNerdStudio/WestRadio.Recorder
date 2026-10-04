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
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QFileInfo>

namespace {
const QString kConfigFilter = QStringLiteral("WestRadio Recorder config (*.wrrec.json)");
const QString kConfigSuffix = QStringLiteral(".wrrec.json");
}

MainWindow::MainWindow(QWidget *parent, const QString &configPath)
    : QMainWindow(parent), engine_(new AudioEngine(this))
{
    buildUi();

    meterTimer_ = new QTimer(this);
    connect(meterTimer_, &QTimer::timeout, this, &MainWindow::updateMeters);
    meterTimer_->start(50);

    elapsedTimer_ = new QTimer(this);
    elapsedTimer_->setInterval(1000);
    connect(elapsedTimer_, &QTimer::timeout, this, &MainWindow::updateElapsed);

    monitorRefreshTimer_ = new QTimer(this);
    monitorRefreshTimer_->setSingleShot(true);
    monitorRefreshTimer_->setInterval(300);
    connect(monitorRefreshTimer_, &QTimer::timeout, this, [this]() {
        engine_->refreshMonitoring();
    });

    connect(engine_, &AudioEngine::monitoringError, this, [this](const QString &msg) {
        setStatus(msg.section(QLatin1Char('\n'), 0, 0));
    }, Qt::QueuedConnection);

    connect(engine_, &AudioEngine::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(engine_, &AudioEngine::recordingStopped, this, &MainWindow::onRecordingStopped);
    connect(engine_, &AudioEngine::recordingFailed,
            this, &MainWindow::onRecordingStartFailed, Qt::QueuedConnection);

    FfmpegTask::setProgressCallback([this](const QString &msg) {
        QMetaObject::invokeMethod(this, [this, msg]() { onPostProcessMessage(msg); },
                                  Qt::QueuedConnection);
    });

    // Persisted window geometry (independent of any config file); a loaded
    // config may override it via applyConfig.
    QByteArray savedGeometry = QSettings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"))
                                   .value(QStringLiteral("windowGeometry")).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);

    // Startup config: --config argument wins, then the stored QSettings path.
    QString startupPath = configPath;
    if (startupPath.isEmpty())
        startupPath = QSettings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"))
                          .value(QStringLiteral("startupConfig")).toString();

    if (!startupPath.isEmpty() && QFile::exists(startupPath)) {
        RecorderConfig cfg;
        QString error;
        if (RecorderConfig::load(startupPath, &cfg, &error)) {
            QStringList warnings;
            applyConfig(cfg, &warnings);
            currentConfigPath_ = startupPath;
            if (!warnings.isEmpty()) {
                setStatus(warnings.first());
                QMessageBox::warning(this, tr("Config loaded with warnings"),
                                     warnings.join(QLatin1Char('\n')));
            }
        } else {
            setStatus(tr("Failed to load startup config: %1").arg(error));
            addTrack();
        }
    } else {
        if (!startupPath.isEmpty())
            setStatus(tr("Startup config not found: %1").arg(startupPath));
        addTrack();
    }

    dirty_ = false;
    updateTitle();
    updateStartupAction();
}

MainWindow::~MainWindow()
{
    FfmpegTask::setProgressCallback(nullptr);
    engine_->stopRecording();
    if (postProcessThread_ && postProcessThread_->isRunning())
        postProcessThread_->wait(15000);
    for (AudioTrack *t : tracks_)
        delete t;
}

void MainWindow::buildUi()
{
    // File menu
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    newAction_ = fileMenu->addAction(tr("&New"), this, &MainWindow::newConfig);
    newAction_->setShortcut(QKeySequence::New);
    openAction_ = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openConfig);
    openAction_->setShortcut(QKeySequence::Open);
    QAction *saveAction = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveConfig);
    saveAction->setShortcut(QKeySequence::Save);
    QAction *saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveConfigAs);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    startupAction_ = fileMenu->addAction(tr("Use this config at startup"));
    startupAction_->setCheckable(true);
    connect(startupAction_, &QAction::toggled, this, [this](bool checked) {
        if (checked && currentConfigPath_.isEmpty())
            saveConfigAs();
        QSettings settings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"));
        if (checked && !currentConfigPath_.isEmpty())
            settings.setValue(QStringLiteral("startupConfig"), currentConfigPath_);
        else
            settings.remove(QStringLiteral("startupConfig"));
        updateStartupAction();
    });
    QAction *clearStartupAction = fileMenu->addAction(tr("Clear startup config"), this, [this]() {
        QSettings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"))
            .remove(QStringLiteral("startupConfig"));
        updateStartupAction();
    });
    Q_UNUSED(clearStartupAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    // Options menu: format, output folder, arm/disarm.
    outputDir_ = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);

    optionsMenu_ = menuBar()->addMenu(tr("&Options"));
    QMenu *formatMenu = optionsMenu_->addMenu(tr("Format"));
    auto *formatGroup = new QActionGroup(this);
    wavAction_ = formatMenu->addAction(tr("WAV"));
    wavAction_->setCheckable(true);
    wavAction_->setChecked(true);
    formatGroup->addAction(wavAction_);
    mp3Action_ = formatMenu->addAction(tr("MP3 (320 kbps)"));
    mp3Action_->setCheckable(true);
    formatGroup->addAction(mp3Action_);
    connect(wavAction_, &QAction::toggled, this, &MainWindow::markDirty);
    connect(mp3Action_, &QAction::toggled, this, &MainWindow::markDirty);

    optionsMenu_->addAction(tr("Output folder\u2026"), this, &MainWindow::onBrowseOutput);
    outputPathAction_ = optionsMenu_->addAction(QString());
    outputPathAction_->setEnabled(false);
    updateOutputPathAction();

    optionsMenu_->addSeparator();
    optionsMenu_->addAction(tr("Arm all"), this, [this]() {
        for (auto *w : trackWidgets_)
            w->setArmed(true);
        updateTrackSummary();
    });
    optionsMenu_->addAction(tr("Disarm all"), this, [this]() {
        for (auto *w : trackWidgets_)
            w->setArmed(false);
        updateTrackSummary();
    });

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

    recordButton_ = new QPushButton(tr("\u25cf REC"), this);
    recordButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #e0322d; border: 1px solid #a02220; "
        "border-radius: 14px; color: #ffffff; font-weight: bold; padding: 4px 18px; }"
        "QPushButton:hover { background-color: #f0433a; }"
        "QPushButton:disabled { background-color: #5a2725; color: #8a9099; }"));
    recordButton_->setMinimumHeight(28);
    recordButton_->setMinimumWidth(0);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::startRecording);
    topBar->addWidget(recordButton_);

    stopButton_ = new QPushButton(tr("\u25a0 Stop"), this);
    stopButton_->setEnabled(false);
    stopButton_->setMinimumHeight(28);
    stopButton_->setMinimumWidth(0);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopRecording);
    topBar->addWidget(stopButton_);

    elapsedLabel_ = new QLabel(QStringLiteral("00:00:00"), this);
    QFont monoFont(QStringLiteral("Consolas"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(14);
    elapsedLabel_->setFont(monoFont);
    elapsedLabel_->setFixedWidth(
        QFontMetrics(monoFont).horizontalAdvance(QStringLiteral("00:00:00")) + 12);
    topBar->addWidget(elapsedLabel_);

    topBar->addStretch();

    mainLayout->addWidget(toolbar);

    // Track strips
    trackWidgets_.clear();
    tracks_.clear();

    tracksContainer_ = new QWidget(this);
    tracksContainer_->setMinimumWidth(0);
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
    scrollArea_->setMinimumWidth(0);
    scrollArea_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
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
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusText_ = statusLabel_->text();
    statusLayout->addWidget(statusLabel_, 1);
    trackSummaryLabel_ = new QLabel(this);
    trackSummaryLabel_->setProperty("dim", true);
    trackSummaryLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLayout->addWidget(trackSummaryLabel_);
    mainLayout->addWidget(statusBar);

    setCentralWidget(central);
    setMinimumSize(340, 420);
    resize(1400, 800);
}

void MainWindow::setStatus(const QString &text)
{
    statusText_ = text;
    statusLabel_->setToolTip(text);
    statusLabel_->setText(QFontMetrics(statusLabel_->font())
                              .elidedText(text, Qt::ElideMiddle,
                                          std::max(0, statusLabel_->width() - 4)));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (statusLabel_)
        setStatus(statusText_);
}

void MainWindow::updateTitle()
{
    QString name = currentConfigPath_.isEmpty()
        ? tr("Untitled")
        : QFileInfo(currentConfigPath_).fileName();
    setWindowTitle(tr("WestRadio Recorder \u2014 %1%2")
                       .arg(name)
                       .arg(dirty_ ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateStartupAction()
{
    QString stored = QSettings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"))
                         .value(QStringLiteral("startupConfig")).toString();
    startupAction_->setChecked(!currentConfigPath_.isEmpty()
                               && stored == currentConfigPath_);
}

void MainWindow::markDirty()
{
    dirty_ = true;
    updateTitle();
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

void MainWindow::appendTrackWidget(AudioTrack *track)
{
    auto *widget = new TrackWidget(engine_, track, tracksContainer_);
    connect(widget, &TrackWidget::removeRequested, this, &MainWindow::removeTrackWidget);
    connect(widget, &TrackWidget::configChanged, this, [this]() {
        monitorRefreshTimer_->start();
        markDirty();
    });
    connect(widget, &TrackWidget::nameChanged, this, &MainWindow::markDirty);

    tracks_.append(track);
    trackWidgets_.append(widget);
    engine_->addTrack(track);
    tracksLayout_->insertWidget(tracksLayout_->indexOf(addTrackButton_), widget);

    renumberTracks();
    updateTrackSummary();
    monitorRefreshTimer_->start();
}

void MainWindow::addTrack()
{
    auto *track = new AudioTrack();
    track->setName(QStringLiteral("TRACK_%1").arg(tracks_.size() + 1));
    track->setArmed(true);

    // Smart default: continue on the same device right after the last track.
    if (!tracks_.isEmpty()) {
        AudioTrack *last = tracks_.last();
        track->setApiIndex(last->apiIndex());
        track->setDeviceIndex(last->deviceIndex());
        track->setChannelCount(last->channelCount());
        int offset = last->channelOffset() + last->channelCount();
        const PaDeviceInfo *info = Pa_GetDeviceInfo(last->deviceIndex());
        int maxCh = info ? info->maxInputChannels : 0;
        if (offset + track->channelCount() > maxCh)
            offset = 0;
        track->setChannelOffset(offset);
    }

    appendTrackWidget(track);
    markDirty();
}

void MainWindow::removeAllTracks()
{
    while (!trackWidgets_.isEmpty()) {
        TrackWidget *widget = trackWidgets_.takeFirst();
        tracksLayout_->removeWidget(widget);
        AudioTrack *track = widget->track();
        tracks_.removeAll(track);
        engine_->removeTrack(track);
        widget->deleteLater();
        delete track;
    }
    renumberTracks();
    updateTrackSummary();
    monitorRefreshTimer_->start();
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
    monitorRefreshTimer_->start();
    markDirty();
}

RecorderConfig MainWindow::currentConfig() const
{
    RecorderConfig cfg;
    cfg.outputDir = outputDir_;
    cfg.mp3 = mp3Action_->isChecked();
    cfg.windowGeometry = saveGeometry();

    for (AudioTrack *t : tracks_) {
        TrackConfig tc;
        tc.name = t->name();
        tc.firstChannel = t->channelOffset() + 1;
        tc.stereo = t->channelCount() == 2;
        tc.armed = t->isArmed();
        const PaDeviceInfo *info = Pa_GetDeviceInfo(t->deviceIndex());
        if (info) {
            tc.device = QString::fromLocal8Bit(info->name);
            const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(info->hostApi);
            if (apiInfo)
                tc.api = QString::fromLocal8Bit(apiInfo->name);
        }
        cfg.tracks.append(tc);
    }
    return cfg;
}

void MainWindow::applyConfig(const RecorderConfig &config, QStringList *warnings)
{
    removeAllTracks();

    if (!config.outputDir.isEmpty()) {
        outputDir_ = config.outputDir;
        updateOutputPathAction();
    }
    wavAction_->setChecked(!config.mp3);
    mp3Action_->setChecked(config.mp3);

    if (!config.windowGeometry.isEmpty())
        restoreGeometry(config.windowGeometry);

    for (const TrackConfig &tc : config.tracks) {
        auto *track = new AudioTrack();
        track->setName(tc.name.isEmpty()
                           ? QStringLiteral("TRACK_%1").arg(tracks_.size() + 1)
                           : tc.name);
        track->setChannelCount(tc.stereo ? 2 : 1);
        track->setChannelOffset(std::max(0, tc.firstChannel - 1));
        track->setArmed(tc.armed);
        track->setFormat(mp3Action_->isChecked() ? AudioTrack::MP3 : AudioTrack::WAV);

        // Devices are stored by API + device name; PortAudio indices are not stable.
        bool found = false;
        for (const auto &api : engine_->audioApis()) {
            if (api.name != tc.api)
                continue;
            for (const auto &dev : engine_->audioDevices(api.apiIndex)) {
                if (dev.name == tc.device) {
                    track->setApiIndex(api.apiIndex);
                    track->setDeviceIndex(dev.deviceIndex);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found && warnings)
            warnings->append(tr("Track '%1': device '%2' (%3) not found")
                                 .arg(track->name(), tc.device, tc.api));

        appendTrackWidget(track);
    }

    if (tracks_.isEmpty())
        addTrack();
}

bool MainWindow::saveConfigTo(const QString &path)
{
    QString error;
    if (!RecorderConfig::save(path, currentConfig(), &error)) {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    currentConfigPath_ = path;
    dirty_ = false;
    updateTitle();
    updateStartupAction();
    setStatus(tr("Saved config: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::newConfig()
{
    if (recording_) {
        QMessageBox::warning(this, tr("Recording"), tr("Stop recording before creating a new config."));
        return;
    }
    removeAllTracks();
    outputDir_ = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    updateOutputPathAction();
    wavAction_->setChecked(true);
    currentConfigPath_.clear();
    addTrack();
    dirty_ = false;
    updateTitle();
    updateStartupAction();
    setStatus(tr("Ready"));
}

void MainWindow::openConfig()
{
    if (recording_) {
        QMessageBox::warning(this, tr("Recording"), tr("Stop recording before opening a config."));
        return;
    }
    QString path = QFileDialog::getOpenFileName(this, tr("Open config"), QString(), kConfigFilter);
    if (path.isEmpty())
        return;

    RecorderConfig cfg;
    QString error;
    if (!RecorderConfig::load(path, &cfg, &error)) {
        QMessageBox::critical(this, tr("Open failed"), error);
        return;
    }

    QStringList warnings;
    applyConfig(cfg, &warnings);
    currentConfigPath_ = path;
    dirty_ = false;
    updateTitle();
    updateStartupAction();
    if (!warnings.isEmpty()) {
        setStatus(warnings.first());
        QMessageBox::warning(this, tr("Config loaded with warnings"),
                             warnings.join(QLatin1Char('\n')));
    } else {
        setStatus(tr("Loaded config: %1").arg(QDir::toNativeSeparators(path)));
    }
}

void MainWindow::saveConfig()
{
    if (currentConfigPath_.isEmpty())
        saveConfigAs();
    else
        saveConfigTo(currentConfigPath_);
}

void MainWindow::saveConfigAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save config"),
                                                currentConfigPath_, kConfigFilter);
    if (path.isEmpty())
        return;
    if (!path.endsWith(kConfigSuffix, Qt::CaseInsensitive))
        path += kConfigSuffix;
    saveConfigTo(path);
}

void MainWindow::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select output folder"), outputDir_);
    if (!dir.isEmpty()) {
        outputDir_ = dir;
        updateOutputPathAction();
        markDirty();
    }
}

void MainWindow::updateOutputPathAction()
{
    QString shown = QDir::toNativeSeparators(outputDir_);
    outputPathAction_->setText(QFontMetrics(font()).elidedText(shown, Qt::ElideMiddle, 400));
    outputPathAction_->setToolTip(shown);
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

    AudioTrack::Format fmt = wavAction_->isChecked() ? AudioTrack::WAV : AudioTrack::MP3;
    for (AudioTrack *t : tracks_)
        t->setFormat(fmt);

    monitorRefreshTimer_->stop();

    // ASIO drivers are loaded through COM and must be driven from the thread that
    // initialised PortAudio (the GUI thread), so start synchronously here.
    // Failure is reported via AudioEngine::recordingFailed.
    setStatus(tr("Starting..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    engine_->startRecording(outputDir_);
    QApplication::restoreOverrideCursor();
}

void MainWindow::onRecordingStartFailed(const QString &message)
{
    if (!recording_)
        recordButton_->setText(tr("\u25cf REC"));
    setUiEnabled(true);
    setStatus(message.section(QLatin1Char('\n'), 0, 0));
    QMessageBox box(QMessageBox::Critical, tr("Recording failed"),
                    tr("Could not start recording."), QMessageBox::Ok, this);
    box.setDetailedText(message);
    box.exec();
}

void MainWindow::onRecordingStarted()
{
    recording_ = true;
    setUiEnabled(false);
    setStatus(tr("Recording..."));
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
    setStatus(tr("Stopping..."));
    stopButton_->setEnabled(false);

    // Same thread rule as start: ASIO must be stopped from the GUI thread.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    engine_->stopRecording();
    QApplication::restoreOverrideCursor();
}

void MainWindow::onRecordingStopped()
{
    if (!recording_)
        return;

    recording_ = false;
    stopping_ = false;
    elapsedTimer_->stop();
    elapsedLabel_->setText(QStringLiteral("00:00:00"));
    recordButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    setStatus(tr("Encoding / finishing..."));

    postProcess();
}

void MainWindow::setUiEnabled(bool enabled)
{
    addTrackButton_->setEnabled(enabled);
    optionsMenu_->setEnabled(enabled);
    newAction_->setEnabled(enabled);
    openAction_->setEnabled(enabled);
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
    setStatus(message);
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
        setStatus(tr("Saved: %1").arg(created.join(QStringLiteral("; "))));
    else
        setStatus(tr("Ready"));
}

void MainWindow::postProcess()
{
    if (postProcessing_)
        return;
    postProcessing_ = true;

    // Read UI state on the main thread before handing work to the background thread.
    const bool mp3Selected = mp3Action_->isChecked();

    struct TrackInfo {
        AudioTrack *track;
        QString wav;
        int channels;
        int sampleRate;
    };
    QVector<TrackInfo> armedTracks;
    for (AudioTrack *t : tracks_) {
        if (!t->isArmed() || t->currentWavPath().isEmpty())
            continue;
        armedTracks.append({t, t->currentWavPath(), t->channelCount(), static_cast<int>(t->sampleRate())});
    }

    // Nothing was actually captured (e.g. spurious recordingStopped) — skip.
    if (armedTracks.isEmpty()) {
        postProcessing_ = false;
        setUiEnabled(true);
        recordButton_->setText(tr("\u25cf REC"));
        setStatus(tr("Ready"));
        return;
    }

    // Run FFmpeg work on a background thread so the UI stays responsive.
    QThread *worker = QThread::create([=]() {
        for (const auto &info : armedTracks) {
            if (info.wav.isEmpty())
                continue;

            if (mp3Selected) {
                QString mp3 = info.wav;
                mp3.replace(mp3.size() - 4, 4, QStringLiteral(".mp3"));
                if (FfmpegTask::encodeToMp3(info.wav, mp3, info.channels)) {
                    info.track->setFinalPath(mp3);
                } else {
                    info.track->setFinalPath(info.wav);
                }
            } else {
                info.track->setFinalPath(info.wav);
            }
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

        QMetaObject::invokeMethod(this, [this]() { onPostProcessFinished(); },
                                  Qt::QueuedConnection);
    });

    postProcessThread_ = worker;
    connect(worker, &QThread::finished, this, [this, worker]() {
        if (postProcessThread_ == worker)
            postProcessThread_ = nullptr;
    }, Qt::QueuedConnection);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopRecording();
    engine_->stopMonitoring();

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

    if (postProcessThread_ && postProcessThread_->isRunning())
        postProcessThread_->wait(15000);

    QSettings(QStringLiteral("WestRadio"), QStringLiteral("Recorder"))
        .setValue(QStringLiteral("windowGeometry"), saveGeometry());

    event->accept();
}
