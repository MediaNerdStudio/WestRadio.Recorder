#include "FfmpegTask.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>

FfmpegTask::ProgressCallback FfmpegTask::progressCallback_;

QString FfmpegTask::ffmpegPath()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/ffmpeg.exe")
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) {
            cached = QDir::toNativeSeparators(p);
            return cached;
        }
    }

    QString path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (path.isEmpty()) {
        const QString fallback = QStringLiteral("C:\\ffmpeg\\bin\\ffmpeg.exe");
        if (QFileInfo::exists(fallback))
            path = fallback;
    }
    if (!path.isEmpty())
        cached = path;
    return cached;
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
                             int channels)
{
    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-i") << wavPath
         << QStringLiteral("-ar") << QStringLiteral("48000")
         << QStringLiteral("-ac") << QString::number(channels)
         << QStringLiteral("-b:a") << QStringLiteral("320k")
         << mp3Path;
    return runFfmpeg(args, QStringLiteral("MP3 encode"));
}
