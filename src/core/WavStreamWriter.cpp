#include "WavStreamWriter.h"
#include <QtEndian>
#include <QDataStream>
#include <algorithm>

WavStreamWriter::WavStreamWriter()
{
}

WavStreamWriter::~WavStreamWriter()
{
    if (file_.isOpen())
        close();
}

bool WavStreamWriter::open(const QString &path, int sampleRate, int channels)
{
    if (file_.isOpen())
        close();

    file_.setFileName(path);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    sampleRate_ = sampleRate;
    channels_ = channels;
    framesWritten_ = 0;

    return writeHeader();
}

bool WavStreamWriter::writeHeader()
{
    // Standard PCM/float WAV header (16-byte fmt chunk).
    // Format tag 3 = IEEE float, 32 bits per sample.
    file_.write("RIFF", 4);
    riffSizePos_ = file_.pos();
    writeLittleEndian32(0); // file size - 8, filled on close

    file_.write("WAVE", 4);
    file_.write("fmt ", 4);
    writeLittleEndian32(16); // fmt chunk size
    writeLittleEndian16(3);  // WAVE_FORMAT_IEEE_FLOAT
    writeLittleEndian16(static_cast<quint16>(channels_));
    writeLittleEndian32(static_cast<quint32>(sampleRate_));
    writeLittleEndian32(static_cast<quint32>(sampleRate_ * channels_ * sizeof(float)));
    writeLittleEndian16(static_cast<quint16>(channels_ * sizeof(float)));
    writeLittleEndian16(32); // bits per sample

    file_.write("data", 4);
    dataSizePos_ = file_.pos();
    writeLittleEndian32(0); // data chunk size, filled on close
    return true;
}

void WavStreamWriter::writeLittleEndian16(quint16 v)
{
    char buf[2];
    qToLittleEndian(v, reinterpret_cast<uchar *>(buf));
    file_.write(buf, 2);
}

void WavStreamWriter::writeLittleEndian32(quint32 v)
{
    char buf[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(buf));
    file_.write(buf, 4);
}

void WavStreamWriter::write(const float *data, size_t frames)
{
    if (!file_.isOpen())
        return;

    qint64 bytesToWrite = static_cast<qint64>(frames * channels_ * sizeof(float));
    file_.write(reinterpret_cast<const char *>(data), bytesToWrite);
    framesWritten_ += static_cast<qint64>(frames);
}

bool WavStreamWriter::close()
{
    if (!file_.isOpen())
        return false;

    qint64 dataBytes = framesWritten_ * channels_ * sizeof(float);
    qint64 riffBytes = dataBytes + 36;

    file_.seek(dataSizePos_);
    writeLittleEndian32(static_cast<quint32>(dataBytes));

    file_.seek(riffSizePos_);
    writeLittleEndian32(static_cast<quint32>(riffBytes));

    file_.close();
    return true;
}

bool WavStreamWriter::isOpen() const
{
    return file_.isOpen();
}

qint64 WavStreamWriter::framesWritten() const
{
    return framesWritten_;
}
