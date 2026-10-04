#pragma once

#include <QWidget>
#include <QElapsedTimer>

class MeterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeterWidget(QWidget *parent = nullptr);

    void setLevels(float leftDb, float rightDb);
    void setStereo(bool stereo);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void drawBar(QPainter &painter, QRect rect, float db, float peakDb, bool clipped);
    float dbToFraction(float db) const;
    int dbToY(float db, int height) const;
    void updateChannel(int ch, float db);

    static constexpr float kMinDb = -60.0f;

    float db_[2] = {kMinDb, kMinDb};
    float peakDb_[2] = {kMinDb, kMinDb};
    QElapsedTimer peakTimer_[2];
    bool clipped_[2] = {false, false};
    bool stereo_ = true;
};
