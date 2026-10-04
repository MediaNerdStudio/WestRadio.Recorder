#include "TrackWidget.h"
#include "core/AudioEngine.h"
#include "core/AudioTrack.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QDateTime>
#include <algorithm>

TrackWidget::TrackWidget(AudioEngine *engine, AudioTrack *track, QWidget *parent)
    : QFrame(parent), engine_(engine), track_(track)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(8);

    nameEdit_ = new QLineEdit(this);
    if (track_->name().isEmpty())
        track_->setName(QStringLiteral("TRACK_1"));
    nameEdit_->setText(track_->name());
    nameEdit_->setMaximumWidth(160);
    mainLayout->addWidget(new QLabel(tr("Name"), this));
    mainLayout->addWidget(nameEdit_);

    apiCombo_ = new QComboBox(this);
    mainLayout->addWidget(new QLabel(tr("API"), this));
    mainLayout->addWidget(apiCombo_);

    deviceCombo_ = new QComboBox(this);
    deviceCombo_->setMinimumWidth(220);
    mainLayout->addWidget(new QLabel(tr("Device"), this));
    mainLayout->addWidget(deviceCombo_);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(tr("Mono"), 1);
    modeCombo_->addItem(tr("Stereo"), 2);
    modeCombo_->setCurrentIndex(track_->channelCount() == 2 ? 1 : 0);
    mainLayout->addWidget(new QLabel(tr("Mode"), this));
    mainLayout->addWidget(modeCombo_);

    offsetSpin_ = new QSpinBox(this);
    offsetSpin_->setMinimum(0);
    offsetSpin_->setMaximum(128);
    offsetSpin_->setValue(0);
    mainLayout->addWidget(new QLabel(tr("Channel offset"), this));
    mainLayout->addWidget(offsetSpin_);

    infoLabel_ = new QLabel(this);
    mainLayout->addWidget(infoLabel_);

    meter_ = new MeterWidget(this);
    meter_->setMinimumSize(40, 120);
    meter_->setStereo(track_->channelCount() == 2);
    mainLayout->addWidget(meter_);

    armedCheck_ = new QCheckBox(tr("Armed"), this);
    armedCheck_->setChecked(true);
    mainLayout->addWidget(armedCheck_);

    removeButton_ = new QPushButton(tr("Remove"), this);
    mainLayout->addWidget(removeButton_);

    connect(nameEdit_, &QLineEdit::textChanged, this, &TrackWidget::onNameChanged);
    connect(apiCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TrackWidget::onApiChanged);
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TrackWidget::onDeviceChanged);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TrackWidget::onModeChanged);
    connect(offsetSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TrackWidget::onOffsetChanged);
    connect(armedCheck_, &QCheckBox::stateChanged, this, &TrackWidget::onArmedChanged);
    connect(removeButton_, &QPushButton::clicked, this, &TrackWidget::onRemoveClicked);

    populateApis();
    onApiChanged(0);
    onNameChanged(nameEdit_->text());
}

AudioTrack *TrackWidget::track() const { return track_; }

void TrackWidget::populateApis()
{
    updating_ = true;
    apiCombo_->clear();
    auto apis = engine_->audioApis();
    int currentApi = track_->apiIndex();
    int selected = 0;
    for (int i = 0; i < apis.size(); ++i) {
        apiCombo_->addItem(apis[i].name, apis[i].apiIndex);
        if (apis[i].apiIndex == currentApi)
            selected = i;
    }
    apiCombo_->setCurrentIndex(selected);
    updating_ = false;
}

void TrackWidget::populateDevices()
{
    updating_ = true;
    deviceCombo_->clear();

    int apiIndex = apiCombo_->currentData().toInt();
    if (apiIndex < 0)
        apiIndex = 0;

    auto devices = engine_->audioDevices(apiIndex);
    int currentDevice = static_cast<int>(track_->deviceIndex());
    int selected = -1;
    int firstValid = -1;
    for (int i = 0; i < devices.size(); ++i) {
        int maxCh = devices[i].maxInputChannels;
        QString text = QStringLiteral("%1 [%2ch @ %3Hz]")
                           .arg(devices[i].name)
                           .arg(maxCh)
                           .arg(static_cast<int>(devices[i].defaultSampleRate));
        deviceCombo_->addItem(text, static_cast<int>(devices[i].deviceIndex));
        if (static_cast<int>(devices[i].deviceIndex) == currentDevice)
            selected = i;
        if (firstValid < 0 && maxCh > 0)
            firstValid = i;
    }

    if (selected >= 0) {
        deviceCombo_->setCurrentIndex(selected);
    } else if (firstValid >= 0) {
        deviceCombo_->setCurrentIndex(firstValid);
    }

    updating_ = false;
    onDeviceChanged(deviceCombo_->currentIndex());
}

void TrackWidget::onApiChanged(int)
{
    if (updating_)
        return;
    int apiIndex = apiCombo_->currentData().toInt();
    track_->setApiIndex(apiIndex);
    populateDevices();
}

void TrackWidget::onDeviceChanged(int)
{
    if (updating_)
        return;
    int deviceIndex = deviceCombo_->currentData().toInt();
    track_->setDeviceIndex(static_cast<PaDeviceIndex>(deviceIndex));

    int maxCh = 0;
    auto devices = engine_->audioDevices(track_->apiIndex());
    for (const auto &dev : devices) {
        if (dev.deviceIndex == deviceIndex) {
            maxCh = dev.maxInputChannels;
            break;
        }
    }

    offsetSpin_->setMaximum(std::max(0, maxCh - 1));
    if (track_->channelOffset() > offsetSpin_->maximum())
        offsetSpin_->setValue(0);
    updateInfoLabel();
}

void TrackWidget::onModeChanged(int)
{
    int count = modeCombo_->currentData().toInt();
    track_->setChannelCount(count);
    meter_->setStereo(count == 2);
    updateInfoLabel();
}

void TrackWidget::onNameChanged(const QString &text)
{
    track_->setName(text.trimmed());
}

void TrackWidget::onOffsetChanged(int value)
{
    track_->setChannelOffset(value);
    updateInfoLabel();
}

void TrackWidget::onArmedChanged(int state)
{
    track_->setArmed(state == Qt::Checked);
}

void TrackWidget::onRemoveClicked()
{
    emit removeRequested(this);
}

void TrackWidget::updateInfoLabel()
{
    int mode = modeCombo_->currentData().toInt();
    int offset = offsetSpin_->value();
    if (mode == 2) {
        infoLabel_->setText(tr("Rec L=%1 R=%2").arg(offset + 1).arg(offset + 2));
    } else {
        infoLabel_->setText(tr("Rec ch %1").arg(offset + 1));
    }
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
