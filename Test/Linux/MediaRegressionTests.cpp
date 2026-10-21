// Standalone QtTest: Qt core/gui/dbus/testlib, libpipewire-0.3, all five Recording cpp files.
// Link with --wrap=pw_stream_dequeue_buffer and --wrap=pw_stream_queue_buffer.
// Run under dbus-run-session with QT_QPA_PLATFORM=offscreen; no portal authorization is requested.
#include <QtTest>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVirtualObject>
#include <QDBusObjectPath>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QThread>
#include <QScopeGuard>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>
#include <signal.h>

#define private public
#include "Recorder.h"
#include "ScrollStitcher.h"
#include "PortalStream.h"
#undef private

namespace {
pw_buffer* pendingBuffer{};
int returnedBuffers{};

QImage document(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    quint32 value = 1234567;
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            value = value * 1664525 + 1013904223;
            const int gray = value >> 24;
            row[x] = qRgb(gray, gray, gray);
        }
    }
    return image;
}

QJsonObject videoInfo(const QString& path)
{
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-count_frames",
        "-show_entries", "stream=nb_read_frames,duration", "-of", "json", path});
    if (!probe.waitForFinished(5000) || probe.exitCode() != 0) return {};
    const auto streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object()
        .value("streams").toArray();
    return streams.isEmpty() ? QJsonObject{} : streams.at(0).toObject();
}

class PortalMock : public QDBusVirtualObject
{
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "media-mock");
    QDBusMessage pending;
    std::atomic_int calls{0};
    QString introspect(const QString&) const override {
        return "<interface name='org.freedesktop.portal.RemoteDesktop'>"
            "<method name='NotifyPointerAxisDiscrete'><arg type='o' direction='in'/>"
            "<arg type='a{sv}' direction='in'/><arg type='u' direction='in'/>"
            "<arg type='i' direction='in'/></method></interface>";
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.member() == "NotifyPointerAxisDiscrete") {
            message.setDelayedReply(true);
            pending = message;
            ++calls;
        } else bus.send(message.createReply());
        return true;
    }
    void replyError() {
        bus.send(pending.createErrorReply(
            "org.freedesktop.DBus.Error.Failed", "Simulated scroll failure"));
    }
};
}

extern "C" pw_buffer* __wrap_pw_stream_dequeue_buffer(pw_stream*)
{
    auto* buffer = pendingBuffer;
    pendingBuffer = nullptr;
    return buffer;
}
extern "C" int __wrap_pw_stream_queue_buffer(pw_stream*, pw_buffer*)
{
    ++returnedBuffers;
    return 0;
}

class MediaRegressionTests : public QObject
{
    Q_OBJECT
private slots:
    void encoderOptions() {
        const auto args = Recording::encoderArguments({3840, 2160}, Recording::Format::Mp4,
            "libx264", {"speaker.monitor", "microphone"}, "unused.mp4");
        QVERIFY(!args.contains("-use_wallclock_as_timestamps"));
        QCOMPARE(args.at(args.indexOf("-thread_queue_size") + 1), "2");
        QCOMPARE(args.count("512"), 2);
        QCOMPARE(args.at(args.indexOf("-video_size") + 1), "3840x2160");
    }
    void burstInputKeepsTimeline() {
        QTemporaryFile output(QDir::currentPath() + "/__tmp/burst-XXXXXX.mp4");
        QVERIFY(output.open());
        output.close();
        QProcess encoder;
        encoder.start("ffmpeg", Recording::encoderArguments({64, 64}, Recording::Format::Mp4,
            "libx264", {}, output.fileName()));
        QVERIFY(encoder.waitForStarted(3000));
        const QByteArray pixels(64 * 64 * 4 * 30, char(127));
        QCOMPARE(encoder.write(pixels), pixels.size());
        encoder.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(encoder.state(), QProcess::NotRunning, 5000);
        QCOMPARE(encoder.exitCode(), 0);
        const auto info = videoInfo(output.fileName());
        QCOMPARE(info.value("nb_read_frames").toString().toInt(), 30);
        QVERIFY(qAbs(info.value("duration").toString().toDouble() - 1.0) < 0.04);
    }
    void recorderDelayAndBackpressure_data() {
        QTest::addColumn<bool>("resize");
        QTest::newRow("normal-stop") << false;
        QTest::newRow("resize-finalizes-before-error") << true;
    }
    void recorderDelayAndBackpressure() {
        QFETCH(bool, resize);
        Recorder recorder;
        QSignalSpy failed(&recorder, &Recorder::failed), ready(&recorder, &Recorder::ready);
        // Bypass codec/audio probing so this test exercises the recording pipe directly.
        recorder.format = Recording::Format::Mp4;
        recorder.size = {128, 128};
        recorder.latest = document(128, 128);
        recorder.codec = "libx264";
        recorder.movie = recorder.temporary("mp4");
        QVERIFY(recorder.movie);
        recorder.current = Recorder::State::Preparing;
        recorder.encode();
        QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), Recorder::State::Recording, 5000);
        recorder.frames.stop();
        QTest::qWait(30);
        QVERIFY(::kill(pid_t(recorder.process.processId()), SIGSTOP) == 0);
        QTest::qSleep(250);
        recorder.writeFrame();
        const qint64 frameBytes = 128 * 128 * 4;
        QVERIFY(recorder.process.bytesToWrite() <= frameBytes * 2);
        QVERIFY(recorder.framesWritten < recorder.elapsed.elapsed() * 30 / 1000 + 1);
        const int written = recorder.framesWritten;
        recorder.writeFrame();
        QCOMPARE(recorder.framesWritten, written);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(recorder.state(), Recorder::State::Recording);
        QVERIFY(::kill(pid_t(recorder.process.processId()), SIGCONT) == 0);
        if (resize) recorder.updateFrame(document(130, 128));
        else recorder.stop();
        const int expected = recorder.finishFrames;
        QCOMPARE(recorder.state(), Recorder::State::Finishing);
        QCOMPARE(failed.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(recorder.process.state(), QProcess::NotRunning, 5000);
        QCOMPARE(recorder.process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(recorder.process.exitCode(), 0);
        QCOMPARE(recorder.framesWritten, expected);
        const auto info = videoInfo(recorder.filePath());
        QCOMPARE(info.value("nb_read_frames").toString().toInt(), expected);
        QVERIFY(qAbs(info.value("duration").toString().toDouble() - expected / 30.0) < 0.04);
        QCOMPARE(failed.size(), resize ? 1 : 0);
        QCOMPARE(ready.size(), resize ? 0 : 1);
        if (resize) QVERIFY(failed.at(0).at(0).toString().contains(recorder.filePath()));
    }
    void scrollRejectsUnrelatedFrames_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("narrow") << 64;
        QTest::newRow("wide") << 640;
    }
    void scrollRejectsUnrelatedFrames() {
        QFETCH(int, width);
        QImage initial(width, 200, QImage::Format_RGB32);
        for (int y = 0; y < 200; ++y)
            std::fill_n(reinterpret_cast<QRgb*>(initial.scanLine(y)), width, qRgb(y, y, y));
        QImage white(initial.size(), initial.format());
        white.fill(Qt::white);
        ScrollStitcher stitcher(initial);
        QCOMPARE(stitcher.append(white), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(stitcher.image(), initial);
        QCOMPARE(stitcher.previous, initial);
    }
    void bottomAnimationDoesNotLockSearch() {
        const auto whole = document(64, 224);
        const auto first = whole.copy(0, 0, 64, 200);
        auto animation = first;
        animation.setPixel(0, 199, animation.pixel(0, 199) ^ 0xffffff);
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(animation), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(stitcher.changeStart, 199);
        QCOMPARE(stitcher.append(whole.copy(0, 24, 64, 200)), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.changeStart, 0);
        QCOMPARE(stitcher.image(), whole);
    }
    void sparseSearchStillValidatesAllColumns() {
        const auto whole = document(640, 224);
        const auto first = whole.copy(0, 0, 640, 200);
        auto unrelated = document(640, 200);
        unrelated.fill(Qt::white);
        // Only the sampled columns match. Full-width validation must reject them.
        const auto shifted = whole.copy(0, 24, 640, 200);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 640; x += 20) unrelated.setPixel(x, y, shifted.pixel(x, y));
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(unrelated), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(stitcher.image(), first);
    }
    void smallShiftAndStaticHeader() {
        const auto whole = document(96, 202);
        const auto first = whole.copy(0, 0, 96, 200);
        auto shifted = whole.copy(0, 2, 96, 200);
        for (int y = 0; y < 12; ++y)
            std::memcpy(shifted.scanLine(y), first.constScanLine(y), 96 * 4);
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(shifted), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.image(), whole);
    }
    void scrollSearch4k() {
        const auto whole = document(3840, 2256);
        ScrollStitcher stitcher(whole.copy(0, 0, 3840, 2160));
        const auto frame = whole.copy(0, 96, 3840, 2160);
        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(stitcher.append(frame), ScrollStitcher::Result::Appended);
        qInfo("4K append: %lld ms", elapsed.elapsed());
        QCOMPARE(stitcher.image(), whole);
    }
    void pipeWireDisconnectAndGeneration() {
        PipeWireFrames frames;
        QSignalSpy failed(&frames, &PipeWireFrames::failed);
        PipeWireFrames::stateChanged(&frames, PW_STREAM_STATE_UNCONNECTED, PW_STREAM_STATE_CONNECTING, nullptr);
        QCoreApplication::sendPostedEvents(&frames, QEvent::MetaCall);
        QCOMPARE(failed.size(), 0);
        for (auto state : {PW_STREAM_STATE_PAUSED, PW_STREAM_STATE_STREAMING}) {
            PipeWireFrames::stateChanged(&frames, PW_STREAM_STATE_CONNECTING, state, nullptr);
            PipeWireFrames::stateChanged(&frames, state, PW_STREAM_STATE_UNCONNECTED, nullptr);
            QCoreApplication::sendPostedEvents(&frames, QEvent::MetaCall);
        }
        QCOMPARE(failed.size(), 2);
        PipeWireFrames::stateChanged(&frames, PW_STREAM_STATE_CONNECTING, PW_STREAM_STATE_ERROR, "test");
        frames.stop();
        QCoreApplication::sendPostedEvents(&frames, QEvent::MetaCall);
        QCOMPARE(failed.size(), 2);
    }
    void pipeWireChunk_data() {
        QTest::addColumn<uint>("size");
        QTest::addColumn<uint>("flags");
        QTest::addColumn<uint>("offset");
        QTest::addColumn<bool>("valid");
        QTest::newRow("empty") << 0u << 0u << 4u << false;
        QTest::newRow("short") << 19u << 0u << 4u << false;
        QTest::newRow("valid-padded") << 20u << 0u << 4u << true;
        QTest::newRow("flag-empty") << 20u << uint(SPA_CHUNK_FLAG_EMPTY) << 4u << false;
        QTest::newRow("corrupted") << 20u << uint(SPA_CHUNK_FLAG_CORRUPTED) << 4u << false;
        QTest::newRow("allocation-overflow") << 20u << 0u << 50u << false;
        QTest::newRow("oversized-chunk") << 64u << 0u << 4u << false;
    }
    void pipeWireChunk() {
        QFETCH(uint, size); QFETCH(uint, flags); QFETCH(uint, offset); QFETCH(bool, valid);
        PipeWireFrames frames;
        frames.info.size = {2, 2};
        frames.info.format = SPA_VIDEO_FORMAT_BGRA;
        QSignalSpy ready(&frames, &PipeWireFrames::frameReady);
        QByteArray storage(64, char(123));
        spa_chunk chunk{};
        chunk.offset = offset; chunk.size = size; chunk.stride = 12; chunk.flags = flags;
        spa_data data{};
        data.data = storage.data(); data.maxsize = storage.size(); data.chunk = &chunk;
        spa_buffer buffer{};
        buffer.n_datas = 1; buffer.datas = &data;
        pw_buffer pwBuffer{};
        pwBuffer.buffer = &buffer;
        pendingBuffer = &pwBuffer;
        returnedBuffers = 0;
        PipeWireFrames::process(&frames);
        storage.fill(0);
        QCoreApplication::sendPostedEvents(&frames, QEvent::MetaCall);
        QCOMPARE(returnedBuffers, 1);
        QCOMPARE(ready.size(), valid ? 1 : 0);
        if (valid) QCOMPARE(qRed(qvariant_cast<QImage>(ready.at(0).at(0)).pixel(0, 0)), 123);
    }
    void staleScrollError() {
        PortalMock mock;
        QThread worker;
        mock.moveToThread(&worker);
        worker.start();
        const auto cleanup = qScopeGuard([&] {
            QMetaObject::invokeMethod(&mock, [&] {
                mock.bus.unregisterObject("/org/freedesktop/portal/desktop");
                mock.bus.unregisterService("org.freedesktop.portal.Desktop");
                mock.moveToThread(QCoreApplication::instance()->thread());
            }, Qt::BlockingQueuedConnection);
            worker.quit();
            worker.wait(1000);
        });
        bool registered = false;
        QMetaObject::invokeMethod(&mock, [&] {
            registered = mock.bus.registerService("org.freedesktop.portal.Desktop")
                && mock.bus.registerVirtualObject("/org/freedesktop/portal/desktop", &mock);
        }, Qt::BlockingQueuedConnection);
        QVERIFY2(registered, "Use dbus-run-session");
        PortalStream stream;
        QSignalSpy failed(&stream, &PortalStream::failed);
        stream.pointer = stream.delivered = true;
        stream.step = PortalStream::Step::Open;
        stream.session = QDBusObjectPath("/test/old");
        stream.scroll();
        QTRY_COMPARE_WITH_TIMEOUT(mock.calls.load(), 1, 3000);
        stream.stop();
        stream.step = PortalStream::Step::Open;
        stream.session = QDBusObjectPath("/test/new");
        QMetaObject::invokeMethod(&mock, &PortalMock::replyError, Qt::BlockingQueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(stream.findChildren<QDBusPendingCallWatcher*>().isEmpty(), 3000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(stream.session.path(), "/test/new");
        QCOMPARE(stream.step, PortalStream::Step::Open);
        stream.scroll();
        QTRY_COMPARE_WITH_TIMEOUT(mock.calls.load(), 2, 3000);
        QMetaObject::invokeMethod(&mock, &PortalMock::replyError, Qt::BlockingQueuedConnection);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
        QCOMPARE(stream.step, PortalStream::Step::Idle);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QDir().mkpath("__tmp");
    qputenv("XDG_CACHE_HOME", QDir::current().absoluteFilePath("__tmp/cache").toUtf8());
    MediaRegressionTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "MediaRegressionTests.moc"
