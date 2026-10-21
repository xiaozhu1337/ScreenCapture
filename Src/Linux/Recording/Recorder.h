#pragma once
#include "EncoderOptions.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

class Recorder : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Preparing, Recording, Finishing, Ready, Failed, Cancelled };
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;
    void start(Recording::Format format, QImage firstFrame, bool systemAudio, bool microphone);
    void updateFrame(QImage frame);
    void stop();
    void cancel();
    State state() const { return current; }
    qint64 elapsedMilliseconds() const { return elapsed.isValid() ? elapsed.elapsed() : 0; }
    QString filePath() const;
    void retainFile();
signals:
    void recordingStarted();
    void finalizing();
    void ready(QString file);
    void failed(QString message);
    void timeLimitReached();
private:
    enum class Job { None, Probe, Sink, Microphone, Encode, Gif };
    void probe();
    void resolveAudio();
    void encode();
    void complete(int code, QProcess::ExitStatus exitStatus);
    void writeFrame();
    void fail(QString message);
    std::unique_ptr<QTemporaryFile> temporary(const QString& suffix);
    QProcess process;
    QTimer frames, watchdog;
    QElapsedTimer elapsed;
    State current{State::Idle};
    Job job{Job::None};
    Recording::Format format{Recording::Format::Mp4};
    bool speaker{false}, mic{false};
    int framesWritten{0}, finishFrames{0};
    QSize size;
    QImage latest;
    QString codec, pendingError;
    QStringList sources;
    QByteArray stderrText, stdoutText;
    std::unique_ptr<QTemporaryFile> movie, gif;
};
