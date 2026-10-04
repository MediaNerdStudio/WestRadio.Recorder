#include "RecorderConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool RecorderConfig::save(const QString &path, const RecorderConfig &config, QString *error)
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("outputDir")] = config.outputDir;
    root[QStringLiteral("format")] = config.mp3 ? QStringLiteral("mp3") : QStringLiteral("wav");
    root[QStringLiteral("combined")] = config.combined;

    QJsonArray tracks;
    for (const TrackConfig &t : config.tracks) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = t.name;
        obj[QStringLiteral("api")] = t.api;
        obj[QStringLiteral("device")] = t.device;
        obj[QStringLiteral("firstChannel")] = t.firstChannel;
        obj[QStringLiteral("stereo")] = t.stereo;
        obj[QStringLiteral("armed")] = t.armed;
        tracks.append(obj);
    }
    root[QStringLiteral("tracks")] = tracks;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool RecorderConfig::load(const QString &path, RecorderConfig *config, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                         ? parseError.errorString()
                         : QStringLiteral("not a JSON object");
        return false;
    }

    QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        if (error)
            *error = QStringLiteral("unsupported config version");
        return false;
    }

    config->outputDir = root.value(QStringLiteral("outputDir")).toString();
    config->mp3 = root.value(QStringLiteral("format")).toString() == QStringLiteral("mp3");
    config->combined = root.value(QStringLiteral("combined")).toBool(false);

    config->tracks.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("tracks")).toArray()) {
        QJsonObject obj = v.toObject();
        TrackConfig t;
        t.name = obj.value(QStringLiteral("name")).toString();
        t.api = obj.value(QStringLiteral("api")).toString();
        t.device = obj.value(QStringLiteral("device")).toString();
        t.firstChannel = obj.value(QStringLiteral("firstChannel")).toInt(1);
        t.stereo = obj.value(QStringLiteral("stereo")).toBool(true);
        t.armed = obj.value(QStringLiteral("armed")).toBool(true);
        config->tracks.append(t);
    }
    return true;
}
