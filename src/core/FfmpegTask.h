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

    // channelCounts[i] is the channel count of wavPaths[i] (1 or 2). Output
    // channels are laid out strictly in input order.
    static bool combineWav(const QString &outputPath, const QStringList &wavPaths,
                           const QList<int> &channelCounts);

    using ProgressCallback = std::function<void(const QString &message)>;
    static void setProgressCallback(ProgressCallback cb);

private:
    static bool runFfmpeg(const QStringList &args, const QString &description);
    static ProgressCallback progressCallback_;
};
