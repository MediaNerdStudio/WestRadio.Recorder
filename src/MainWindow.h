#pragma once

#include <QMainWindow>
#include <QVector>
#include <QDateTime>
#include <memory>
#include "core/RecorderConfig.h"

class AudioEngine;
class AudioTrack;
class TrackWidget;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QRadioButton;
class QLabel;
class QScrollArea;
class QHBoxLayout;
class QVBoxLayout;
class QTimer;
class QAction;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &configPath = QString());
    ~MainWindow();

private slots:
    void addTrack();
    void removeTrackWidget(TrackWidget *widget);
    void startRecording();
    void stopRecording();
    void updateMeters();
    void updateElapsed();
    void onBrowseOutput();
    void onRecordingStarted();
    void onRecordingStartFailed(const QString &message);
    void onRecordingStopped();
    void onPostProcessMessage(const QString &message);
    void onPostProcessFinished();
    void newConfig();
    void openConfig();
    void saveConfig();
    void saveConfigAs();
    void markDirty();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void postProcess();
    void setUiEnabled(bool enabled);
    QString safeFileName(const QString &name) const;
    void renumberTracks();
    void updateTrackSummary();
    void appendTrackWidget(AudioTrack *track);
    void removeAllTracks();
    RecorderConfig currentConfig() const;
    void applyConfig(const RecorderConfig &config, QStringList *warnings);
    bool saveConfigTo(const QString &path);
    void updateTitle();
    void updateStartupAction();
    void updateOutputPathAction();
    void setStatus(const QString &text);

    AudioEngine *engine_;
    QVector<AudioTrack *> tracks_;
    QVector<TrackWidget *> trackWidgets_;

    QString outputDir_;
    QString statusText_;
    QAction *wavAction_;
    QAction *mp3Action_;
    QAction *outputPathAction_;
    QMenu *optionsMenu_;
    QPushButton *addTrackButton_;
    QPushButton *recordButton_;
    QPushButton *stopButton_;
    QLabel *statusLabel_;
    QLabel *elapsedLabel_;
    QLabel *trackSummaryLabel_;

    QAction *newAction_;
    QAction *openAction_;
    QAction *startupAction_;

    QScrollArea *scrollArea_;
    QWidget *tracksContainer_;
    QHBoxLayout *tracksLayout_;

    QTimer *meterTimer_;
    QTimer *elapsedTimer_;
    QTimer *monitorRefreshTimer_;
    QThread *postProcessThread_ = nullptr;

    QString currentConfigPath_;
    bool dirty_ = false;
    QDateTime recordStart_;
    bool recording_ = false;
    bool stopping_ = false;
    bool postProcessing_ = false;
};
