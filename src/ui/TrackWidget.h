#pragma once

#include <QFrame>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include "MeterWidget.h"

class AudioEngine;
class AudioTrack;

class TrackWidget : public QFrame
{
    Q_OBJECT
public:
    TrackWidget(AudioEngine *engine, AudioTrack *track, QWidget *parent = nullptr);

    AudioTrack *track() const;
    void refreshMeter();

signals:
    void removeRequested(TrackWidget *widget);

private slots:
    void onApiChanged(int index);
    void onDeviceChanged(int index);
    void onModeChanged(int index);
    void onNameChanged(const QString &text);
    void onOffsetChanged(int value);
    void onArmedChanged(int state);
    void onRemoveClicked();

private:
    void populateApis();
    void populateDevices();
    void updateInfoLabel();

    AudioEngine *engine_;
    AudioTrack *track_;

    QLineEdit *nameEdit_;
    QComboBox *apiCombo_;
    QComboBox *deviceCombo_;
    QComboBox *modeCombo_;
    QSpinBox *offsetSpin_;
    QCheckBox *armedCheck_;
    QPushButton *removeButton_;
    QLabel *infoLabel_;
    MeterWidget *meter_;

    bool updating_ = false;
};
