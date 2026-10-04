#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class FfmpegTask
{
public:
    static QString ffmpegPath();
    static bool available();

    static bool encodeToMp3(const QString &wavPath, const QString &mp3Path,
                            int sampleRate, int channels);

    static bool combineWav(const QString &outputPath, const QStringList &wavPaths,
                           int totalChannels);

    using ProgressCallback = std::function<void(const QString &message)>;
    static void setProgressCallback(ProgressCallback cb);

private:
    static bool runFfmpeg(const QStringList &args, const QString &description);
    static ProgressCallback progressCallback_;
};
