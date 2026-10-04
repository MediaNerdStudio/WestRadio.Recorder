#include "MeterWidget.h"
#include <QPainter>
#include <cmath>

MeterWidget::MeterWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(24, 120);
}

void MeterWidget::setLevels(float leftDb, float rightDb)
{
    leftDb_ = leftDb;
    rightDb_ = rightDb;
    update();
}

void MeterWidget::setStereo(bool stereo)
{
    stereo_ = stereo;
    update();
}

float MeterWidget::dbToFraction(float db) const
{
    if (db <= -60.0f)
        return 0.0f;
    if (db >= 0.0f)
        return 1.0f;
    return (db + 60.0f) / 60.0f;
}

void MeterWidget::drawBar(QPainter &painter, QRect rect, float db)
{
    float frac = dbToFraction(db);
    int fillHeight = static_cast<int>(rect.height() * frac);
    QRect fillRect(rect.left(), rect.bottom() - fillHeight, rect.width(), fillHeight);

    QColor color;
    if (db > -6.0f)
        color = QColor(220, 50, 50);
    else if (db > -18.0f)
        color = QColor(220, 220, 50);
    else
        color = QColor(50, 200, 50);

    painter.fillRect(rect, QColor(30, 30, 30));
    painter.fillRect(fillRect, color);

    // Tick marks at -60, -30, -18, -6, 0 dB.
    painter.setPen(QPen(QColor(120, 120, 120), 1));
    const float ticks[] = {-60.0f, -30.0f, -18.0f, -6.0f, 0.0f};
    for (float t : ticks) {
        int y = rect.bottom() - static_cast<int>(rect.height() * dbToFraction(t));
        painter.drawLine(rect.left(), y, rect.right(), y);
    }
}

void MeterWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int gap = 4;
    int w = width();
    int h = height();

    if (stereo_) {
        int barW = (w - gap) / 2;
        drawBar(painter, QRect(0, 0, barW, h), leftDb_);
        drawBar(painter, QRect(barW + gap, 0, w - barW - gap, h), rightDb_);
    } else {
        drawBar(painter, QRect(0, 0, w, h), leftDb_);
    }
}
