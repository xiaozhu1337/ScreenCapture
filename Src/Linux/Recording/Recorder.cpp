#include "Recorder.h"
#include <QDir>
#include <QStandardPaths>

Recorder::Recorder(QObject* parent) : QObject(parent)
{
    process.setProcessChannelMode(QProcess::SeparateChannels);
    frames.setTimerType(Qt::PreciseTimer);
    connect(&frames, &QTimer::timeout, this, &Recorder::writeFrame);
    connect(&process, &QProcess::bytesWritten, this, [this] {
        if (current == State::Finishing && job == Job::Encode) writeFrame();
    });
    watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, this, [this] { fail(tr("录制处理超时，请检查 FFmpeg 和音频服务。")); });
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        stderrText.append(process.readAllStandardError());
        if (stderrText.size() > 16384) stderrText = stderrText.right(16384);
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] { stdoutText.append(process.readAllStandardOutput()); });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (current == State::Cancelled || current == State::Failed) return;
        if (error == QProcess::FailedToStart) fail(tr("无法启动 %1：%2").arg(process.program(), process.errorString()));
    });
    connect(&process, &QProcess::finished, this, &Recorder::complete);
    connect(&process, &QProcess::started, this, [this] {
        if (job != Job::Encode || current != State::Preparing) return;
        watchdog.stop();
        current = State::Recording;
        elapsed.start();
        framesWritten = 0;
        writeFrame();
        if (current == State::Recording) {
            frames.start(qRound(1000.0 / Recording::frameRate(format)));
            emit recordingStarted();
        }
    });
}
Recorder::~Recorder() { cancel(); }

std::unique_ptr<QTemporaryFile> Recorder::temporary(const QString& suffix)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/recordings";
    if (!QDir().mkpath(directory)) return {};
    auto file = std::make_unique<QTemporaryFile>(directory + "/capture-XXXXXX." + suffix);
    if (!file->open()) return {};
    file->close();
    return file;
}
QString Recorder::filePath() const { return format == Recording::Format::Gif && gif ? gif->fileName() : movie ? movie->fileName() : QString(); }

void Recorder::retainFile()
{
    // Clipboard managers retain file URLs after taking ownership; those files must survive exit.
    if (current != State::Ready) return;
    if (format == Recording::Format::Gif && gif) gif->setAutoRemove(false);
    else if (movie) movie->setAutoRemove(false);
}

void Recorder::start(Recording::Format requested, QImage firstFrame, bool systemAudio, bool microphone)
{
    if (current == State::Preparing || current == State::Recording || current == State::Finishing) return;
    if (process.state() != QProcess::NotRunning) return;
    format = requested;
    size = Recording::encodedSize(firstFrame.size());
    if (size.width() < 2 || size.height() < 2) { fail(tr("录制区域太小。")); return; }
    latest = std::move(firstFrame);
    speaker = systemAudio && format == Recording::Format::Mp4;
    mic = microphone && format == Recording::Format::Mp4;
    sources.clear();
    elapsed.invalidate();
    framesWritten = finishFrames = 0;
    pendingError.clear();
    movie = temporary("mp4");
    gif.reset();
    if (!movie) { fail(tr("无法创建录制临时文件，请检查缓存目录和空间。")); return; }
    current = State::Preparing;
    codec = format == Recording::Format::Mp4 ? "libx265" : "libx264";
    probe();
}
void Recorder::probe()
{
    job = Job::Probe;
    stderrText.clear(); stdoutText.clear();
    watchdog.start(15000);
    QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "lavfi", "-i",
        QString("color=size=%1x%2:rate=1").arg(size.width()).arg(size.height()),
        "-frames:v", "1", "-c:v", codec, "-preset", "ultrafast", "-pix_fmt", "yuv420p"};
    if (codec == "libx265") args << "-x265-params" << "log-level=error:pools=2";
    args << "-f" << "null" << "-";
    process.start("ffmpeg", args);
}
void Recorder::resolveAudio()
{
    stderrText.clear(); stdoutText.clear();
    if (speaker) {
        speaker = false; job = Job::Sink;
        watchdog.start(10000); process.start("pactl", {"get-default-sink"});
    } else if (mic) {
        mic = false; job = Job::Microphone;
        watchdog.start(10000); process.start("pactl", {"get-default-source"});
    } else encode();
}
void Recorder::encode()
{
    job = Job::Encode;
    stderrText.clear(); stdoutText.clear();
    watchdog.start(15000);
    process.start("ffmpeg", Recording::encoderArguments(size, format, codec, sources, movie->fileName()));
}
void Recorder::updateFrame(QImage frame)
{
    if (current != State::Preparing && current != State::Recording) return;
    if (Recording::encodedSize(frame.size()) != size) {
        fail(tr("采集屏幕尺寸已改变，录制无法继续。")); return;
    }
    latest = std::move(frame);
}
void Recorder::writeFrame()
{
    if (current != State::Recording && !(current == State::Finishing && job == Job::Encode)) return;
    if (current == State::Recording && elapsed.elapsed() >= qint64(Recording::maximumSeconds(format)) * 1000) {
        emit timeLimitReached(); stop(); return;
    }
    const int due = current == State::Finishing ? finishFrames
        : int(elapsed.elapsed() * Recording::frameRate(format) / 1000) + 1;
    const qint64 bytes = qint64(size.width()) * size.height() * 4;
    if (framesWritten < due && process.bytesToWrite() + bytes <= bytes * 2) {
        QImage image = latest.copy(QRect(QPoint(), size)).convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { fail(tr("无法准备录制画面。")); return; }
        // Keep CFR timestamps even after delayed timers; drain debt without growing the pipe queue.
        while (framesWritten < due && process.bytesToWrite() + bytes <= bytes * 2) {
            if (process.write(reinterpret_cast<const char*>(image.constBits()), bytes) != bytes) {
                fail(tr("无法向 FFmpeg 写入录制画面。")); return;
            }
            ++framesWritten;
        }
    }
    if (current == State::Finishing && framesWritten >= finishFrames) process.closeWriteChannel();
}
void Recorder::stop()
{
    if (current == State::Preparing) { cancel(); return; }
    if (current != State::Recording) return;
    frames.stop();
    finishFrames = qMax(framesWritten, int(qMin(elapsed.elapsed(),
        qint64(Recording::maximumSeconds(format)) * 1000) * Recording::frameRate(format) / 1000) + 1);
    current = State::Finishing;
    watchdog.start(120000);
    writeFrame();
    emit finalizing();
}
void Recorder::complete(int code, QProcess::ExitStatus exitStatus)
{
    watchdog.stop();
    if (current == State::Cancelled || current == State::Failed) return;
    stderrText.append(process.readAllStandardError());
    stdoutText.append(process.readAllStandardOutput());
    const bool success = code == 0 && exitStatus == QProcess::NormalExit;
    if (job == Job::Probe) {
        if (!success && codec == "libx265") { codec = "libx264"; probe(); return; }
        if (!success) { fail(tr("没有可用的 H.264 / HEVC 编码器。")); return; }
        resolveAudio();
    } else if (job == Job::Sink || job == Job::Microphone) {
        const QString source = QString::fromUtf8(stdoutText).trimmed();
        if (!success || source.isEmpty() || source.contains('\n')) { fail(tr("无法查询系统声或麦克风的音频源。")); return; }
        sources << (job == Job::Sink ? source + ".monitor" : source);
        resolveAudio();
    } else if (job == Job::Encode) {
        frames.stop();
        if (!success || !framesWritten) { fail(tr("FFmpeg 录制失败。")); return; }
        if (!pendingError.isEmpty()) {
            fail(pendingError + tr(" 已保存录制文件：%1").arg(movie->fileName()));
            return;
        }
        if (format == Recording::Format::Gif) {
            current = State::Finishing;
            emit finalizing();
            gif = temporary("gif");
            if (!gif) { fail(tr("无法创建 GIF 临时文件。")); return; }
            job = Job::Gif;
            stderrText.clear(); stdoutText.clear();
            watchdog.start(20 * 60 * 1000);
            process.start("ffmpeg", Recording::gifArguments(movie->fileName(), gif->fileName()));
        } else {
            current = State::Ready;
            emit ready(filePath());
        }
    } else if (job == Job::Gif) {
        if (!success) { fail(tr("GIF 转换失败，MP4 中间文件仍可在窗口关闭前恢复：%1").arg(movie->fileName())); return; }
        current = State::Ready;
        emit ready(filePath());
    }
}
void Recorder::fail(QString message)
{
    if (current == State::Failed) return;
    if (current == State::Recording && framesWritten > 0 && process.state() == QProcess::Running) {
        pendingError = std::move(message);
        stop();
        return;
    }
    current = State::Failed;
    frames.stop(); watchdog.stop();
    if (process.state() != QProcess::NotRunning) process.kill();
    const QString detail = QString::fromUtf8(stderrText.right(4096));
    qCritical("%s\n%s", qPrintable(message), qPrintable(detail));
    emit failed(message);
}
void Recorder::cancel()
{
    current = State::Cancelled;
    frames.stop(); watchdog.stop();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
}
