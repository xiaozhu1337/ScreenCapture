#include "EncoderOptions.h"

int Recording::frameRate(Format format) { return format == Format::Gif ? 16 : 30; }
int Recording::maximumSeconds(Format format) { return format == Format::Gif ? 6 * 60 : 120 * 60; }
QSize Recording::encodedSize(QSize size) { return {size.width() & ~1, size.height() & ~1}; }
QStringList Recording::encoderArguments(QSize size, Format format, const QString& codec,
    const QStringList& audioSources, const QString& output)
{
    QStringList args{"-hide_banner", "-loglevel", "warning", "-nostdin", "-y",
        "-thread_queue_size", "2", "-f", "rawvideo", "-pixel_format", "bgra",
        "-video_size", QString("%1x%2").arg(size.width()).arg(size.height()),
        "-framerate", QString::number(frameRate(format)), "-i", "pipe:0"};
    if (format == Format::Mp4) {
        for (const auto& source : audioSources)
            args << "-thread_queue_size" << "512" << "-f" << "pulse" << "-i" << source;
    }
    args << "-map" << "0:v:0";
    if (format == Format::Mp4 && !audioSources.isEmpty()) {
        QString filter;
        for (int i = 1; i <= audioSources.size(); ++i) filter += QString("[%1:a]").arg(i);
        filter += QString("amix=inputs=%1:duration=longest:normalize=0,aresample=async=1:first_pts=0[a]").arg(audioSources.size());
        args << "-filter_complex" << filter << "-map" << "[a]" << "-c:a" << "aac" << "-b:a" << "192k";
    }
    args << "-c:v" << codec << "-preset" << "ultrafast" << "-crf" << "23" << "-pix_fmt" << "yuv420p"
         << "-fps_mode" << "cfr" << "-r" << QString::number(frameRate(format));
    if (codec == "libx265") args << "-x265-params" << "log-level=error:pools=2" << "-tag:v" << "hvc1";
    if (format == Format::Mp4 && !audioSources.isEmpty()) args << "-shortest";
    args << "-t" << QString::number(maximumSeconds(format)) << "-movflags" << "+faststart" << output;
    return args;
}
QStringList Recording::gifArguments(const QString& input, const QString& output)
{
    // Per-frame palettes bound memory independently of a six-minute recording's length.
    return {"-hide_banner", "-loglevel", "warning", "-nostdin", "-y", "-i", input,
        "-filter_complex", "fps=16,split[a][b];[a]palettegen=stats_mode=single[p];[b][p]paletteuse=new=1:dither=sierra2_4a",
        "-an", "-loop", "0", output};
}
