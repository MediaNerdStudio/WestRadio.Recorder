#pragma once

#include <QString>
#include <QVector>

struct TrackConfig {
    QString name;
    QString api;
    QString device;
    int firstChannel = 1;
    bool stereo = true;
    bool armed = true;
};

struct RecorderConfig {
    QString outputDir;
    bool mp3 = false;
    bool combined = false;
    QVector<TrackConfig> tracks;

    static bool save(const QString &path, const RecorderConfig &config, QString *error);
    static bool load(const QString &path, RecorderConfig *config, QString *error);
};
