#pragma once
#include <QSize>
#include <QStringList>

namespace Recording {
enum class Format { Mp4, Gif };
int frameRate(Format format);
int maximumSeconds(Format format);
QSize encodedSize(QSize size);
QStringList encoderArguments(QSize size, Format format, const QString& codec,
    const QStringList& audioSources, const QString& output);
QStringList gifArguments(const QString& input, const QString& output);
}
