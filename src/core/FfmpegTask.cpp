#include "FfmpegTask.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>

FfmpegTask::ProgressCallback FfmpegTask::progressCallback_;

QString FfmpegTask::ffmpegPath()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!path.isEmpty())
        return path;

    const QString fallback = QStringLiteral("C:\\ffmpeg\\bin\\ffmpeg.exe");
    if (QFileInfo::exists(fallback))
        return fallback;

    return QString();
}

bool FfmpegTask::available()
{
    return !ffmpegPath().isEmpty();
}

void FfmpegTask::setProgressCallback(ProgressCallback cb)
{
    progressCallback_ = cb;
}

bool FfmpegTask::runFfmpeg(const QStringList &args, const QString &description)
{
    QString exe = ffmpegPath();
    if (exe.isEmpty()) {
        qWarning() << "FFmpeg not found";
        return false;
    }

    if (progressCallback_)
        progressCallback_(QStringLiteral("Running: %1 %2").arg(exe, args.join(' ')));

    int exitCode = QProcess::execute(exe, args);
    if (exitCode != 0) {
        qWarning() << "FFmpeg failed for" << description << "exit code:" << exitCode;
        return false;
    }
    return true;
}

bool FfmpegTask::encodeToMp3(const QString &wavPath, const QString &mp3Path,
                             int sampleRate, int channels)
{
    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-i") << wavPath
         << QStringLiteral("-ar") << QString::number(sampleRate)
         << QStringLiteral("-ac") << QString::number(channels)
         << QStringLiteral("-b:a") << QStringLiteral("192k")
         << mp3Path;
    return runFfmpeg(args, QStringLiteral("MP3 encode"));
}

bool FfmpegTask::combineWav(const QString &outputPath, const QStringList &wavPaths,
                            const QList<int> &channelCounts)
{
    if (wavPaths.isEmpty() || channelCounts.size() != wavPaths.size())
        return false;

    QStringList args;
    args << QStringLiteral("-y");
    for (const QString &path : wavPaths)
        args << QStringLiteral("-i") << path;

    if (wavPaths.size() == 1) {
        args << QStringLiteral("-acodec") << QStringLiteral("pcm_f32le")
             << QStringLiteral("-f") << QStringLiteral("wav")
             << outputPath;
        return runFfmpeg(args, QStringLiteral("combined WAV"));
    }

    // amerge re-sorts channels by speaker position (FL, FR, FC, ...) when the
    // inputs have different layouts, which scrambles mono/stereo track order.
    // Splitting every stereo input into mono streams first makes all inputs
    // identical (mono), so amerge keeps strict input order.
    QString filter;
    QString mergeInputs;
    int monoCount = 0;
    for (int i = 0; i < wavPaths.size(); ++i) {
        if (channelCounts[i] >= 2) {
            filter += QStringLiteral("[%1:a]channelsplit=channel_layout=stereo[s%1l][s%1r];").arg(i);
            mergeInputs += QStringLiteral("[s%1l][s%1r]").arg(i);
            monoCount += 2;
        } else {
            mergeInputs += QStringLiteral("[%1:a]").arg(i);
            monoCount += 1;
        }
    }
    filter += mergeInputs + QStringLiteral("amerge=inputs=%1[out]").arg(monoCount);

    args << QStringLiteral("-filter_complex") << filter
         << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-acodec") << QStringLiteral("pcm_f32le")
         << QStringLiteral("-f") << QStringLiteral("w64")
         << outputPath;
    return runFfmpeg(args, QStringLiteral("combined WAV"));
}
