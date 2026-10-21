#include "EncoderOptions.h"
#include "PortalStream.h"
#include "Recorder.h"
#include "ScrollStitcher.h"
#include "LongCapture.h"
#include "VideoCapture.h"
#include "Linux/Settings/Settings.h"
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QPointer>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

namespace {
QImage document(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    quint32 random = 1789;
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            random = random * 1664525U + 1013904223U;
            line[x] = qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255);
        }
    }
    return image;
}
class RejectedPortal : public QDBusVirtualObject
{
public:
    uint code{1};
    bool respond{true};
    QString createdSession;
    QStringList closed;
    QString introspect(const QString&) const override {
        return "<interface name='org.freedesktop.portal.ScreenCast'>"
            "<method name='CreateSession'><arg type='a{sv}' direction='in'/>"
            "<arg type='o' direction='out'/></method></interface>"
            "<interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>";
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.member() == "Close") { closed << message.path(); bus.send(message.createReply()); return true; }
        if (message.member() != "CreateSession") return false;
        const auto options = qdbus_cast<QVariantMap>(message.arguments().first());
        QString sender = message.service().mid(1);
        sender.replace('.', '_');
        createdSession = "/org/freedesktop/portal/desktop/session/" + sender + "/" + options.value("session_handle_token").toString();
        const QString path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + options.value("handle_token").toString();
        bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(path))));
        if (!respond) return true;
        QTimer::singleShot(0, this, [this, bus, path] {
            auto response = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
            response.setArguments({code, QVariantMap{}});
            bus.send(response);
        });
        return true;
    }
};
}

class RecordingTests : public QObject
{
    Q_OBJECT
private slots:
    void stitching() {
        const auto full = document(48, 600);
        ScrollStitcher stitcher(full.copy(0, 0, 48, 240));
        QCOMPARE(stitcher.append(full.copy(0, 35, 48, 240)), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.image(), full.copy(0, 0, 48, 275));
        QCOMPARE(stitcher.append(full.copy(0, 79, 48, 240)), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.image(), full.copy(0, 0, 48, 319));
        for (int i = 0; i < 8; ++i)
            QCOMPARE(stitcher.append(full.copy(0, 79, 48, 240)), ScrollStitcher::Result::RetryScroll);
        QCOMPARE(stitcher.append(full.copy(0, 79, 48, 240)), ScrollStitcher::Result::Bottom);
        QCOMPARE(stitcher.append(QImage(1, 1, QImage::Format_RGB32)), ScrollStitcher::Result::Invalid);
    }
    void fixedHeader() {
        auto first = document(32, 300);
        auto next = first;
        const auto body = document(32, 500);
        QPainter before(&first); before.drawImage(0, 20, body.copy(0, 0, 32, 280)); before.end();
        QPainter after(&next); after.drawImage(0, 20, body.copy(0, 30, 32, 280)); after.end();
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(next), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.image().height(), 330);
        QCOMPARE(stitcher.image().copy(0, 0, 32, 20), first.copy(0, 0, 32, 20));
        QCOMPARE(stitcher.image().copy(0, 20, 32, 310), body.copy(0, 0, 32, 310));
    }
    void bottomStripAndSettling() {
        QImage first(32, 200, QImage::Format_RGB32); first.fill(Qt::white);
        const QImage body = document(32, 140);
        QPainter a(&first); a.drawImage(0, 120, body.copy(0, 0, 32, 80)); a.end();
        QImage next(32, 200, QImage::Format_RGB32); next.fill(Qt::white);
        QPainter b(&next); b.drawImage(0, 120, body.copy(0, 20, 32, 80)); b.end();
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(next), ScrollStitcher::Result::Appended);
        QCOMPARE(stitcher.image().height(), 220);
        QImage animation(32, 200, QImage::Format_RGB32); animation.fill(Qt::black);
        ScrollStitcher unstable;
        QImage blank(32, 200, QImage::Format_RGB32); blank.fill(Qt::white);
        unstable = ScrollStitcher(blank);
        QCOMPARE(unstable.append(animation), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(unstable.append(animation), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(unstable.append(animation), ScrollStitcher::Result::RetryScroll);
    }
    void unrelatedFrame() {
        QImage first(32, 200, QImage::Format_RGB32); first.fill(Qt::black);
        QPainter painter(&first); painter.fillRect(0, 100, 32, 100, Qt::gray); painter.end();
        QImage next(32, 200, QImage::Format_RGB32); next.fill(Qt::white);
        ScrollStitcher stitcher(first);
        QCOMPARE(stitcher.append(next), ScrollStitcher::Result::RetryFrame);
        QCOMPARE(stitcher.image(), first);
    }
    void pipeWireBuffers() {
        QImage image = document(16, 16);
        spa_chunk chunk{}; chunk.size = uint(image.sizeInBytes()); chunk.stride = image.bytesPerLine();
        spa_data pixels{}; pixels.data = image.bits(); pixels.maxsize = chunk.size; pixels.chunk = &chunk;
        spa_buffer buffer{}; buffer.n_datas = 1; buffer.datas = &pixels;
        spa_video_info_raw info{}; info.size = SPA_RECTANGLE(16, 16); info.format = SPA_VIDEO_FORMAT_BGRx;
        QCOMPARE(PipeWireFrames::copyFrame(buffer, info), image);
        --chunk.size; QVERIFY(PipeWireFrames::copyFrame(buffer, info).isNull());
        chunk.size = pixels.maxsize; chunk.flags = SPA_CHUNK_FLAG_EMPTY;
        QVERIFY(PipeWireFrames::copyFrame(buffer, info).isNull());
        chunk.flags = SPA_CHUNK_FLAG_CORRUPTED;
        QVERIFY(PipeWireFrames::copyFrame(buffer, info).isNull());
        chunk.flags = 0; chunk.offset = 1;
        QVERIFY(PipeWireFrames::copyFrame(buffer, info).isNull());
    }
    void heightLimit() {
        const auto full = document(4, 36120);
        ScrollStitcher stitcher(full.copy(0, 0, 4, 36000));
        QCOMPARE(stitcher.append(full.copy(0, 20, 4, 36000)), ScrollStitcher::Result::HeightLimit);
        QCOMPARE(stitcher.image().height(), 36020);
    }
    void regionAndOptions() {
        QCOMPARE(PortalStream::pixelRegion(QRect(-1800, 100, 200, 300), QRect(-1920, 0, 1920, 1080), QSize(3840, 2160)), QRect(240, 200, 400, 600));
        QVERIFY(PortalStream::pixelRegion(QRect(0, 0, 10, 10), QRect(50, 0, 100, 100), QSize(200, 200)).isEmpty());
        QCOMPARE(Recording::encodedSize({101, 99}), QSize(100, 98));
        QCOMPARE(Recording::frameRate(Recording::Format::Mp4), 30);
        QCOMPARE(Recording::frameRate(Recording::Format::Gif), 16);
        QCOMPARE(Recording::maximumSeconds(Recording::Format::Gif), 360);
        QCOMPARE(Recording::maximumSeconds(Recording::Format::Mp4), 7200);
        const auto mp4 = Recording::encoderArguments({100, 98}, Recording::Format::Mp4, "libx265", {"sink.monitor", "mic"}, "out.mp4");
        QVERIFY(mp4.contains("sink.monitor") && mp4.contains("mic"));
        QVERIFY(mp4.contains("[1:a][2:a]amix=inputs=2:duration=longest:normalize=0,aresample=async=1:first_pts=0[a]"));
        QVERIFY(mp4.contains("-shortest") && mp4.contains("hvc1"));
        const auto gif = Recording::encoderArguments({100, 98}, Recording::Format::Gif, "libx264", {"mic"}, "out.mp4");
        QVERIFY(!gif.contains("pulse") && !gif.contains("mic"));
        QVERIFY(Recording::gifArguments("in.mp4", "out.gif").contains("0"));
    }
    void portalRejection() {
        auto bus = QDBusConnection::sessionBus();
        RejectedPortal mock;
        QVERIFY2(bus.registerService("org.freedesktop.portal.Desktop"), "Run under dbus-run-session");
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &mock, QDBusConnection::SubPath));
        PortalStream stream;
        QSignalSpy cancelled(&stream, &PortalStream::cancelled), failed(&stream, &PortalStream::failed);
        stream.start(QRect(1, 1, 16, 16), false, true);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 3000);
        mock.code = 2;
        stream.start(QRect(1, 1, 16, 16), false, true);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        stream.stop(); stream.stop();
        mock.respond = false; mock.createdSession.clear(); mock.closed.clear();
        stream.start(QRect(1, 1, 16, 16), false, true);
        QTRY_VERIFY_WITH_TIMEOUT(!mock.createdSession.isEmpty(), 3000);
        stream.stop();
        QTRY_VERIFY_WITH_TIMEOUT(mock.closed.contains(mock.createdSession), 3000);
        bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void encode_data() {
        QTest::addColumn<bool>("gif");
        QTest::addColumn<bool>("speaker");
        QTest::addColumn<bool>("mic");
        QTest::newRow("mp4") << false << false << false;
        QTest::newRow("gif") << true << false << false;
        QTest::newRow("system-audio") << false << true << false;
        QTest::newRow("microphone") << false << false << true;
        QTest::newRow("mixed-audio") << false << true << true;
    }
    void encode() {
        QFETCH(bool, gif);
        QFETCH(bool, speaker);
        QFETCH(bool, mic);
        Recorder recorder;
        const QImage frame = document(64, 64);
        QSignalSpy started(&recorder, &Recorder::recordingStarted), ready(&recorder, &Recorder::ready), failed(&recorder, &Recorder::failed);
        recorder.start(gif ? Recording::Format::Gif : Recording::Format::Mp4, frame, speaker, mic);
        QCOMPARE(recorder.state(), Recorder::State::Preparing);
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 15000);
        QTest::qWait(650);
        recorder.updateFrame(frame);
        recorder.stop();
        QCOMPARE(recorder.state(), Recorder::State::Finishing);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(recorder.state(), Recorder::State::Ready);
        const QString output = ready.first().first().toString();
        QVERIFY(QFileInfo(output).size() > 0);
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name,width,height,avg_frame_rate", "-of", "default=nw=1", output});
        QTRY_VERIFY_WITH_TIMEOUT(probe.state() == QProcess::NotRunning, 5000);
        QCOMPARE(probe.exitCode(), 0);
        const QByteArray info = probe.readAllStandardOutput();
        QVERIFY(info.contains("width=64") && info.contains("height=64"));
        QVERIFY(gif ? info.contains("codec_name=gif") : (info.contains("codec_name=hevc") || info.contains("codec_name=h264")));
        if (speaker || mic) {
            probe.start("ffprobe", {"-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name", "-of", "default=nw=1", output});
            QTRY_VERIFY_WITH_TIMEOUT(probe.state() == QProcess::NotRunning, 5000);
            QCOMPARE(probe.exitCode(), 0);
            QVERIFY(probe.readAllStandardOutput().contains("codec_name=aac"));
        }
        recorder.cancel();
        QCOMPARE(recorder.state(), Recorder::State::Cancelled);
    }
    void windowControls() {
        QTemporaryDir directory(QDir::currentPath() + "/__tmp/recording-settings-XXXXXX");
        QVERIFY(directory.isValid());
        Settings settings(directory.path());
        auto* video = new VideoCapture({10, 10, 64, 64}, settings);
        QPointer<VideoCapture> videoPointer = video;
        auto* speaker = video->findChild<QToolButton*>("recordSpeaker");
        auto* microphone = video->findChild<QToolButton*>("recordMicrophone");
        QVERIFY(speaker && microphone);
        QVERIFY(speaker->isChecked());
        QVERIFY(!microphone->isChecked());
        QVERIFY(!video->findChild<QToolButton*>("save")->isEnabled());
        QVERIFY(!video->findChild<QToolButton*>("clipboard")->isEnabled());
        QCOMPARE(video->size(), QSize(216, 34));
        QCOMPARE(video->findChild<QStackedWidget*>()->currentIndex(), 0);
        video->findChild<QToolButton*>("formatGif")->click();
        QVERIFY(!speaker->isEnabled() && !microphone->isEnabled());
        QVERIFY(!speaker->isChecked() && !microphone->isChecked());
        video->close();
        QTRY_VERIFY(videoPointer.isNull());
        const auto initial = document(64, 64);
        auto* longCapture = new LongCapture(initial, {10, 10, 64, 64}, settings);
        QPointer<LongCapture> longPointer = longCapture;
        QSignalSpy pinned(longCapture, &LongCapture::pinRequested), finished(longCapture, &LongCapture::finished);
        longCapture->findChild<QToolButton*>("pin")->click();
        QCOMPARE(pinned.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(pinned.first().first()), initial);
        QCOMPARE(finished.count(), 1);
        QTRY_VERIFY(longPointer.isNull());
    }
    void retainedFile() {
        QString path;
        {
            Recorder recorder;
            QSignalSpy started(&recorder, &Recorder::recordingStarted), ready(&recorder, &Recorder::ready);
            recorder.start(Recording::Format::Mp4, document(16, 16), false, false);
            QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 15000);
            QTest::qWait(200);
            recorder.stop();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
            path = recorder.filePath();
            recorder.retainFile();
        }
        QVERIFY(QFileInfo(path).size() > 0);
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "stream=codec_name", "-of", "default=nw=1", path});
        QTRY_VERIFY_WITH_TIMEOUT(probe.state() == QProcess::NotRunning, 5000);
        QCOMPARE(probe.exitCode(), 0);
        const auto codec = probe.readAllStandardOutput();
        QVERIFY(codec.contains("codec_name=hevc") || codec.contains("codec_name=h264"));
        QVERIFY(QFile::remove(path));
        {
            Recorder recorder;
            recorder.start(Recording::Format::Mp4, document(64, 64), false, false);
            path = recorder.filePath();
            QVERIFY(QFileInfo::exists(path));
            recorder.cancel();
        }
        QVERIFY(!QFileInfo::exists(path));
    }
    void cancellation() {
        Recorder recorder;
        QSignalSpy ready(&recorder, &Recorder::ready);
        recorder.start(Recording::Format::Mp4, document(64, 64), false, false);
        recorder.cancel();
        QCOMPARE(recorder.state(), Recorder::State::Cancelled);
        QTest::qWait(50);
        QCOMPARE(ready.count(), 0);
    }
};
QTEST_MAIN(RecordingTests)
#include "RecordingTests.moc"
