#include "TrackWidget.h"
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QFontMetrics>
#include <algorithm>

TrackWidget::TrackWidget(AudioEngine *engine, AudioTrack *track, QWidget *parent)
    : QFrame(parent), engine_(engine), track_(track)
{
    setFixedWidth(96);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    selectedDevice_ = {-1, paNoDevice, 0, 0.0, QString(), QString()};

    buildUi();
    buildDeviceMenu();
    updateInfo();
}

void TrackWidget::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 4, 6, 6);

    channelNumberLabel_ = new QLabel(this);
    channelNumberLabel_->setAlignment(Qt::AlignCenter);
    channelNumberLabel_->setProperty("dim", true);
    QFont numFont = channelNumberLabel_->font();
    numFont.setPointSize(8);
    channelNumberLabel_->setFont(numFont);
    mainLayout->addWidget(channelNumberLabel_);

    deviceButton_ = new QPushButton(tr("No Input"), this);
    deviceButton_->setFlat(false);
    mainLayout->addWidget(deviceButton_);

    QHBoxLayout *channelLayout = new QHBoxLayout();
    channelLayout->setSpacing(3);
    channelSpin_ = new QSpinBox(this);
    channelSpin_->setMinimum(1);
    channelSpin_->setMaximum(128);
    channelSpin_->setValue(1);
    channelSpin_->setToolTip(tr("First channel"));
    channelLayout->addWidget(channelSpin_);
    stereoCheck_ = new QCheckBox(tr("ST"), this);
    stereoCheck_->setChecked(true);
    stereoCheck_->setToolTip(tr("Stereo"));
    channelLayout->addWidget(stereoCheck_);
    mainLayout->addLayout(channelLayout);

    sampleRateLabel_ = new QLabel(tr("-"), this);
    sampleRateLabel_->setAlignment(Qt::AlignCenter);
    sampleRateLabel_->setProperty("dim", true);
    QFont srFont = sampleRateLabel_->font();
    srFont.setPointSize(7);
    sampleRateLabel_->setFont(srFont);
    mainLayout->addWidget(sampleRateLabel_);

    meter_ = new MeterWidget(this);
    meter_->setMinimumSize(40, 140);
    meter_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(meter_, 1);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setAlignment(Qt::AlignCenter);
    nameEdit_->setMaxLength(16);
    QFont nameFont = nameEdit_->font();
    nameFont.setPointSize(8);
    nameFont.setBold(true);
    nameEdit_->setFont(nameFont);
    if (track_->name().isEmpty())
        track_->setName(QStringLiteral("TRACK_1"));
    nameEdit_->setText(track_->name());
    mainLayout->addWidget(nameEdit_);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(3);
    armButton_ = new QPushButton(tr("ARM"), this);
    armButton_->setCheckable(true);
    armButton_->setChecked(track_->isArmed());
    armButton_->setStyleSheet(QStringLiteral(
        "QPushButton:checked { background-color: #e0322d; border-color: #e0322d; "
        "color: #ffffff; font-weight: bold; }"));
    bottomLayout->addWidget(armButton_, 1);
    removeButton_ = new QPushButton(QStringLiteral("\u00d7"), this);
    removeButton_->setFixedWidth(24);
    removeButton_->setToolTip(tr("Remove source"));
    bottomLayout->addWidget(removeButton_);
    mainLayout->addLayout(bottomLayout);

    connect(nameEdit_, &QLineEdit::textChanged, this, &TrackWidget::onNameChanged);
    connect(channelSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TrackWidget::onOffsetChanged);
    connect(stereoCheck_, &QCheckBox::stateChanged, this, &TrackWidget::onStereoChanged);
    connect(armButton_, &QPushButton::toggled, this, &TrackWidget::onArmedChanged);
    connect(removeButton_, &QPushButton::clicked, this, &TrackWidget::onRemoveClicked);
}

AudioTrack *TrackWidget::track() const { return track_; }

void TrackWidget::setChannelNumber(int number)
{
    channelNumberLabel_->setText(QString::number(number));
}

void TrackWidget::setArmed(bool armed)
{
    armButton_->setChecked(armed);
}

void TrackWidget::updateDeviceButton()
{
    if (selectedDevice_.deviceIndex == paNoDevice) {
        deviceButton_->setText(tr("No Input"));
        deviceButton_->setToolTip(QString());
        return;
    }
    QFontMetrics fm(deviceButton_->font());
    deviceButton_->setText(fm.elidedText(selectedDevice_.deviceName, Qt::ElideMiddle, 76));
    deviceButton_->setToolTip(QStringLiteral("%1 \u2014 %2 (%3 ch @ %4 Hz)")
                                  .arg(selectedDevice_.apiName)
                                  .arg(selectedDevice_.deviceName)
                                  .arg(selectedDevice_.maxInputChannels)
                                  .arg(static_cast<int>(selectedDevice_.sampleRate)));
}

void TrackWidget::buildDeviceMenu()
{
    deviceMenu_ = new QMenu(this);

    int firstApi = -1;
    PaDeviceIndex firstDevice = paNoDevice;

    for (const auto &api : engine_->audioApis()) {
        QMenu *apiMenu = deviceMenu_->addMenu(api.name);
        auto devices = engine_->audioDevices(api.apiIndex);
        bool hasDevice = false;
        for (const auto &dev : devices) {
            if (dev.maxInputChannels <= 0)
                continue;
            hasDevice = true;
            QString text = QStringLiteral("%1 (%2 ch @ %3 Hz)")
                               .arg(dev.name)
                               .arg(dev.maxInputChannels)
                               .arg(static_cast<int>(dev.defaultSampleRate));
            QAction *action = apiMenu->addAction(text);
            action->setCheckable(true);
            action->setData(QVariant::fromValue(DeviceActionData{api.apiIndex,
                                                                  dev.deviceIndex,
                                                                  dev.maxInputChannels,
                                                                  dev.defaultSampleRate,
                                                                  api.name,
                                                                  dev.name}));
            connect(action, &QAction::triggered, this, &TrackWidget::onDeviceActionTriggered);

            if (firstDevice == paNoDevice) {
                firstApi = api.apiIndex;
                firstDevice = dev.deviceIndex;
            }
        }
        if (!hasDevice)
            apiMenu->addAction(tr("No inputs"))->setEnabled(false);
    }

    deviceButton_->setMenu(deviceMenu_);

    // Default to the first available device if none was previously set.
    if (track_->deviceIndex() == paNoDevice && firstDevice != paNoDevice)
        selectDevice(firstApi, firstDevice);
    else if (track_->deviceIndex() != paNoDevice)
        selectDevice(track_->apiIndex(), track_->deviceIndex());
}

void TrackWidget::selectDevice(int apiIndex, PaDeviceIndex deviceIndex)
{
    for (QAction *topAction : deviceMenu_->actions()) {
        QMenu *apiMenu = topAction->menu();
        if (!apiMenu)
            continue;
        for (QAction *action : apiMenu->actions()) {
            if (!action->data().isValid())
                continue;
            DeviceActionData d = action->data().value<DeviceActionData>();
            if (d.deviceIndex == deviceIndex && d.apiIndex == apiIndex) {
                action->setChecked(true);
                selectedDevice_ = d;
                track_->setApiIndex(d.apiIndex);
                track_->setDeviceIndex(d.deviceIndex);
                updateDeviceButton();
                updateInfo();
                return;
            }
        }
    }
}

void TrackWidget::onDeviceActionTriggered()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action || !action->data().isValid())
        return;

    // Uncheck all other device actions in this widget so only one is active.
    for (QAction *topAction : deviceMenu_->actions()) {
        QMenu *apiMenu = topAction->menu();
        if (!apiMenu)
            continue;
        for (QAction *a : apiMenu->actions()) {
            if (a != action && a->isChecked())
                a->setChecked(false);
        }
    }

    DeviceActionData d = action->data().value<DeviceActionData>();
    selectedDevice_ = d;
    track_->setApiIndex(d.apiIndex);
    track_->setDeviceIndex(d.deviceIndex);
    updateDeviceButton();
    updateInfo();
    emit configChanged();
}

void TrackWidget::updateInfo()
{
    int maxCh = selectedDevice_.maxInputChannels;
    int first = channelSpin_->value();
    bool stereo = stereoCheck_->isChecked();

    int count = stereo ? 2 : 1;
    track_->setChannelCount(count);
    meter_->setStereo(stereo);

    // Spin shows 1-based first channel.
    int maxFirst = std::max(1, maxCh - (stereo ? 1 : 0));
    channelSpin_->setMaximum(maxFirst);
    if (first > maxFirst)
        channelSpin_->setValue(1);

    track_->setChannelOffset(channelSpin_->value() - 1);

    double sr = selectedDevice_.sampleRate;
    if (sr > 0)
        sampleRateLabel_->setText(tr("%1 kHz").arg(sr / 1000.0, 0, 'f', 1));
    else
        sampleRateLabel_->setText(tr("-"));

    // Keep the model in sync with the arm button.
    track_->setArmed(armButton_->isChecked());
}

void TrackWidget::onNameChanged(const QString &text)
{
    track_->setName(text.trimmed());
}

void TrackWidget::onOffsetChanged(int value)
{
    track_->setChannelOffset(value - 1);
    updateInfo();
    emit configChanged();
}

void TrackWidget::onStereoChanged(int state)
{
    bool stereo = state == Qt::Checked;
    track_->setChannelCount(stereo ? 2 : 1);
    meter_->setStereo(stereo);
    updateInfo();
    emit configChanged();
}

void TrackWidget::onArmedChanged(bool checked)
{
    track_->setArmed(checked);
    emit configChanged();
}

void TrackWidget::onRemoveClicked()
{
    emit removeRequested(this);
}

void TrackWidget::refreshMeter()
{
    float left = track_->readLeftPeak();
    float right = track_->readRightPeak();

    auto db = [](float linear) {
        if (linear <= 0.0000001f)
            return -60.0f;
        return 20.0f * std::log10(linear);
    };

    meter_->setLevels(db(left), db(right));

    // While recording, show the rate the stream actually negotiated.
    if (track_->isRecording()) {
        double sr = track_->sampleRate();
        QString text = (std::fmod(sr, 1000.0) == 0.0)
            ? tr("%1 kHz").arg(static_cast<int>(sr / 1000.0))
            : tr("%1 kHz").arg(sr / 1000.0, 0, 'f', 1);
        if (sampleRateLabel_->text() != text)
            sampleRateLabel_->setText(text);
    }
}
