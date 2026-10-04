#include "TrackWidget.h"
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <algorithm>

TrackWidget::TrackWidget(AudioEngine *engine, AudioTrack *track, QWidget *parent)
    : QFrame(parent), engine_(engine), track_(track)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setMinimumWidth(180);
    setMaximumWidth(220);

    selectedDevice_ = {-1, paNoDevice, 0, 0.0};

    buildUi();
    buildDeviceMenu();
    updateInfo();
}

void TrackWidget::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);

    nameEdit_ = new QLineEdit(this);
    if (track_->name().isEmpty())
        track_->setName(QStringLiteral("TRACK_1"));
    nameEdit_->setText(track_->name());
    mainLayout->addWidget(nameEdit_);

    mainLayout->addWidget(new QLabel(tr("Input"), this));

    deviceButton_ = new QPushButton(tr("Select input..."), this);
    mainLayout->addWidget(deviceButton_);

    QHBoxLayout *channelLayout = new QHBoxLayout();
    channelLayout->addWidget(new QLabel(tr("1st channel"), this));
    channelSpin_ = new QSpinBox(this);
    channelSpin_->setMinimum(1);
    channelSpin_->setMaximum(128);
    channelSpin_->setValue(1);
    channelLayout->addWidget(channelSpin_);
    mainLayout->addLayout(channelLayout);

    stereoCheck_ = new QCheckBox(tr("Stereo"), this);
    stereoCheck_->setChecked(true);
    mainLayout->addWidget(stereoCheck_);

    sampleRateLabel_ = new QLabel(tr("Sample rate: -"), this);
    sampleRateLabel_->setWordWrap(true);
    mainLayout->addWidget(sampleRateLabel_);

    meter_ = new MeterWidget(this);
    meter_->setMinimumSize(60, 180);
    meter_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(meter_, 1);

    armedCheck_ = new QCheckBox(tr("Armed"), this);
    armedCheck_->setChecked(track_->isArmed());
    mainLayout->addWidget(armedCheck_);

    removeButton_ = new QPushButton(tr("Remove"), this);
    mainLayout->addWidget(removeButton_);

    connect(nameEdit_, &QLineEdit::textChanged, this, &TrackWidget::onNameChanged);
    connect(channelSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TrackWidget::onOffsetChanged);
    connect(stereoCheck_, &QCheckBox::stateChanged, this, &TrackWidget::onStereoChanged);
    connect(armedCheck_, &QCheckBox::stateChanged, this, &TrackWidget::onArmedChanged);
    connect(removeButton_, &QPushButton::clicked, this, &TrackWidget::onRemoveClicked);
}

AudioTrack *TrackWidget::track() const { return track_; }

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
                                                                  dev.defaultSampleRate}));
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
                deviceButton_->setText(QStringLiteral("%1\n%2")
                                           .arg(topAction->text())
                                           .arg(action->text().replace(QStringLiteral(" ("),
                                                                       QStringLiteral("\n("))));
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

    // Update button text: API name on first line, device name on second.
    QMenu *apiMenu = qobject_cast<QMenu *>(action->parent());
    if (apiMenu) {
        deviceButton_->setText(QStringLiteral("%1\n%2")
                                   .arg(apiMenu->title())
                                   .arg(action->text().replace(QStringLiteral(" ("),
                                                               QStringLiteral("\n("))));
    }

    updateInfo();
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
        sampleRateLabel_->setText(tr("Sample rate: %1 Hz").arg(static_cast<int>(sr)));
    else
        sampleRateLabel_->setText(tr("Sample rate: -"));

    // Keep the model in sync with the checkbox.
    track_->setArmed(armedCheck_->isChecked());
}

void TrackWidget::onNameChanged(const QString &text)
{
    track_->setName(text.trimmed());
}

void TrackWidget::onOffsetChanged(int value)
{
    track_->setChannelOffset(value - 1);
    updateInfo();
}

void TrackWidget::onStereoChanged(int state)
{
    bool stereo = state == Qt::Checked;
    track_->setChannelCount(stereo ? 2 : 1);
    meter_->setStereo(stereo);
    updateInfo();
}

void TrackWidget::onArmedChanged(int state)
{
    track_->setArmed(state == Qt::Checked);
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
}
