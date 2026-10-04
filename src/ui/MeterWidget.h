#pragma once

#include <QWidget>

class MeterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeterWidget(QWidget *parent = nullptr);

    void setLevels(float leftDb, float rightDb);
    void setStereo(bool stereo);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawBar(QPainter &painter, QRect rect, float db);
    float dbToFraction(float db) const;

    float leftDb_ = -60.0f;
    float rightDb_ = -60.0f;
    bool stereo_ = true;
};
