#pragma once

#include <QString>
#include <QFile>
#include <cstddef>

class WavStreamWriter
{
public:
    WavStreamWriter();
    ~WavStreamWriter();

    bool open(const QString &path, int sampleRate, int channels);
    void write(const float *data, size_t frames);
    bool close();

    bool isOpen() const;
    qint64 framesWritten() const;

private:
    bool writeHeader();
    void writeLittleEndian16(quint16 v);
    void writeLittleEndian32(quint32 v);

    QFile file_;
    int sampleRate_ = 0;
    int channels_ = 0;
    qint64 riffSizePos_ = 0;
    qint64 dataSizePos_ = 0;
    qint64 framesWritten_ = 0;
};
