#pragma once

#include <QMainWindow>
#include <QVector>
#include <QDateTime>
#include <memory>

class AudioEngine;
class AudioTrack;
class TrackWidget;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QRadioButton;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void addTrack();
    void removeTrackWidget(TrackWidget *widget);
    void startRecording();
    void stopRecording();
    void updateMeters();
    void onBrowseOutput();
    void onRecordingStarted();
    void onRecordingStopped();
    void onPostProcessMessage(const QString &message);
    void onPostProcessFinished();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void postProcess();
    void setUiEnabled(bool enabled);
    QString safeFileName(const QString &name) const;

    AudioEngine *engine_;
    QVector<AudioTrack *> tracks_;
    QVector<TrackWidget *> trackWidgets_;

    QLineEdit *outputEdit_;
    QRadioButton *wavRadio_;
    QRadioButton *mp3Radio_;
    QCheckBox *combinedCheck_;
    QPushButton *addTrackButton_;
    QPushButton *recordButton_;
    QPushButton *stopButton_;
    QPushButton *armAllButton_;
    QPushButton *disarmAllButton_;
    QLabel *statusLabel_;

    QScrollArea *scrollArea_;
    QWidget *tracksContainer_;
    QVBoxLayout *tracksLayout_;

    QTimer *meterTimer_;
    QDateTime recordStart_;
    bool recording_ = false;
    bool postProcessing_ = false;
};
