#include "MeterWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kBarWidth = 14;
constexpr int kBarGap = 2;
constexpr int kScaleWidth = 26;
constexpr qint64 kPeakHoldMs = 1500;
constexpr float kPeakDecayDbPerSec = 20.0f;

const QColor kGreen(0x2f, 0xd1, 0x4a);
const QColor kYellow(0xf2, 0xd1, 0x3a);
const QColor kRed(0xf0, 0x43, 0x3a);
const QColor kTrackBg(0x0f, 0x11, 0x14);
const QColor kDimText(0x8a, 0x90, 0x99);
}

MeterWidget::MeterWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(kBarWidth + kScaleWidth + 4, 120);
    peakTimer_[0].start();
    peakTimer_[1].start();
}

void MeterWidget::setLevels(float leftDb, float rightDb)
{
    updateChannel(0, leftDb);
    updateChannel(1, rightDb);
    update();
}

void MeterWidget::updateChannel(int ch, float db)
{
    db = std::clamp(db, kMinDb, 0.0f);
    db_[ch] = db;
    if (db >= -0.1f)
        clipped_[ch] = true;

    if (db >= peakDb_[ch]) {
        peakDb_[ch] = db;
        peakTimer_[ch].restart();
    } else if (peakTimer_[ch].elapsed() > kPeakHoldMs) {
        float decay = kPeakDecayDbPerSec * (peakTimer_[ch].elapsed() - kPeakHoldMs) / 1000.0f;
        peakDb_[ch] = std::max(db, peakDb_[ch] - decay);
        if (peakDb_[ch] <= db)
            peakTimer_[ch].restart();
    }
}

void MeterWidget::setStereo(bool stereo)
{
    stereo_ = stereo;
    update();
}

float MeterWidget::dbToFraction(float db) const
{
    return std::clamp((db - kMinDb) / (0.0f - kMinDb), 0.0f, 1.0f);
}

int MeterWidget::dbToY(float db, int height) const
{
    return height - static_cast<int>(height * dbToFraction(db));
}

void MeterWidget::drawBar(QPainter &painter, QRect rect, float db, float peakDb, bool clipped)
{
    painter.fillRect(rect, kTrackBg);

    const int bottom = rect.bottom() + 1;
    const int top = rect.top();

    auto zoneRect = [&](float loDb, float hiDb, const QColor &color) {
        float level = std::clamp(db, loDb, hiDb);
        if (level <= loDb)
            return;
        int yTop = bottom - static_cast<int>((bottom - top) * dbToFraction(level));
        int yBot = bottom - static_cast<int>((bottom - top) * dbToFraction(loDb));
        painter.fillRect(QRect(rect.left(), yTop, rect.width(), yBot - yTop), color);
    };

    // Segmented zones: green below -18 dB, yellow -18..-6, red above -6.
    zoneRect(kMinDb, -18.0f, kGreen);
    zoneRect(-18.0f, -6.0f, kYellow);
    zoneRect(-6.0f, 0.0f, kRed);

    // Peak-hold marker.
    if (peakDb > kMinDb) {
        int y = bottom - static_cast<int>((bottom - top) * dbToFraction(peakDb));
        painter.fillRect(QRect(rect.left(), y - 1, rect.width(), 2), QColor(0xcf, 0xd3, 0xd9));
    }

    // Clip indicator.
    if (clipped) {
        painter.fillRect(QRect(rect.left(), top, rect.width(), 8), kRed);
        painter.setPen(QColor(0x15, 0x17, 0x1c));
        QFont f = painter.font();
        f.setPixelSize(7);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(QRect(rect.left(), top, rect.width(), 8),
                         Qt::AlignCenter, QStringLiteral("!"));
    }
}

void MeterWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int h = height();
    const int channels = stereo_ ? 2 : 1;
    const int barsWidth = channels * kBarWidth + (channels - 1) * kBarGap;
    int x = std::max(0, (width() - kScaleWidth - barsWidth) / 2);

    for (int ch = 0; ch < channels; ++ch) {
        QRect barRect(x, 0, kBarWidth, h);
        drawBar(painter, barRect, db_[ch], peakDb_[ch], clipped_[ch]);
        x += kBarWidth + kBarGap;
    }

    // dB scale on the right.
    const float ticks[] = {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -40.0f, -50.0f};
    const int scaleX = width() - kScaleWidth;
    painter.setPen(kDimText);
    QFont f = painter.font();
    f.setPixelSize(9);
    painter.setFont(f);
    for (float t : ticks) {
        int y = h - static_cast<int>(h * dbToFraction(t));
        painter.drawLine(scaleX, y, scaleX + 4, y);
        painter.drawText(QRect(scaleX + 5, y - 6, kScaleWidth - 5, 12),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(static_cast<int>(t)));
    }
}

void MeterWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    clipped_[0] = false;
    clipped_[1] = false;
    update();
}
