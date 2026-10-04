#pragma once

#include <QFrame>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMetaType>
#include <portaudio.h>
#include "MeterWidget.h"

class AudioEngine;
class AudioTrack;

class TrackWidget : public QFrame
{
    Q_OBJECT
public:
    explicit TrackWidget(AudioEngine *engine, AudioTrack *track, QWidget *parent = nullptr);

    AudioTrack *track() const;
    void refreshMeter();
    void setChannelNumber(int number);
    void setArmed(bool armed);

    struct DeviceActionData {
        int apiIndex;
        PaDeviceIndex deviceIndex;
        int maxInputChannels;
        double sampleRate;
        QString apiName;
        QString deviceName;
    };

signals:
    void removeRequested(TrackWidget *widget);

private slots:
    void onDeviceActionTriggered();
    void onNameChanged(const QString &text);
    void onOffsetChanged(int value);
    void onStereoChanged(int state);
    void onArmedChanged(bool checked);
    void onRemoveClicked();

private:
    void buildUi();
    void buildDeviceMenu();
    void selectDevice(int apiIndex, PaDeviceIndex deviceIndex);
    void updateInfo();
    void updateDeviceButton();

    AudioEngine *engine_;
    AudioTrack *track_;

    QLabel *channelNumberLabel_;
    QLineEdit *nameEdit_;
    QPushButton *deviceButton_;
    QMenu *deviceMenu_;
    QSpinBox *channelSpin_;
    QCheckBox *stereoCheck_;
    QLabel *sampleRateLabel_;
    QPushButton *armButton_;
    QPushButton *removeButton_;
    MeterWidget *meter_;

    DeviceActionData selectedDevice_;
};

Q_DECLARE_METATYPE(TrackWidget::DeviceActionData)
