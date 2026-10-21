#include "CapturePortal.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

static void check(bool value, const char* message)
{
    if (!value) qFatal("%s", message);
}

class Portal : public QDBusVirtualObject
{
public:
    uint code{0};
    QString uri;
    bool fail{false};
    int requests{0};
    QString introspect(const QString&) const override {
        return "<interface name='org.freedesktop.portal.Screenshot'>"
               "<method name='Screenshot'><arg type='s' direction='in'/>"
               "<arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method></interface>";
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.member() != "Screenshot") return false;
        ++requests;
        if (fail) { bus.send(message.createErrorReply("org.freedesktop.DBus.Error.Failed", "Simulated failure")); return true; }
        const auto options = qdbus_cast<QVariantMap>(message.arguments().at(1));
        QString sender = message.service().mid(1);
        sender.replace('.', '_');
        const QString path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + options.value("handle_token").toString();
        bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(path))));
        QTimer::singleShot(0, this, [=, this] {
            auto response = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
            response.setArguments({code, QVariantMap{{"uri", uri}}});
            bus.send(response);
        });
        return true;
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    auto bus = QDBusConnection::sessionBus();
    Portal mock;
    check(bus.registerService("org.freedesktop.portal.Desktop"), "Run this test under dbus-run-session");
    check(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &mock), "Cannot register mock portal");
    QDir().mkpath("__tmp");
    QTemporaryDir directory(QDir::current().absoluteFilePath("__tmp/portal-test-XXXXXX"));
    check(directory.isValid(), "Cannot create portal test directory");
    const QString path = directory.filePath("screenshot.png");
    QImage sample(13, 17, QImage::Format_RGB32);
    sample.fill(Qt::blue);
    check(sample.save(path), "Cannot write portal test image");
    mock.uri = QUrl::fromLocalFile(path).toString();
    CapturePortal portal;
    QString outcome;
    QObject::connect(&portal, &CapturePortal::captured, &app, [&](const QImage& image) {
        check(image.size() == sample.size() && image.pixelColor(0, 0) == QColor(Qt::blue), "Portal must deliver original image pixels");
        outcome = "success";
    });
    QObject::connect(&portal, &CapturePortal::cancelled, &app, [&] { outcome = "cancelled"; });
    QObject::connect(&portal, &CapturePortal::failed, &app, [&] { outcome = "error"; });
    auto request = [&](const QString& expected) {
        outcome.clear();
        portal.capture();
        QElapsedTimer timer;
        timer.start();
        while (outcome.isEmpty() && timer.elapsed() < 3000) QTest::qWait(10);
        check(outcome == expected, "Unexpected portal result");
    };
    request("success");
    mock.code = 1;
    request("cancelled");
    mock.code = 2;
    request("error");
    mock.code = 0;
    mock.uri = "https://invalid.example/screenshot.png";
    request("error");
    mock.uri = QUrl::fromLocalFile(directory.filePath("missing.png")).toString();
    request("error");
    mock.fail = true;
    request("error");
    qInfo("Portal success, cancellation, denial, URI, image, and D-Bus error checks passed");

    mock.fail = false;
    mock.uri = QUrl::fromLocalFile(path).toString();
    const QString configHome = directory.filePath("config-home");
    const QString configDirectory = configHome + "/xland/ScreenCapture";
    check(QDir().mkpath(configDirectory), "Cannot create isolated app configuration");
    QFile config(configDirectory + "/config.yaml");
    check(config.open(QIODevice::WriteOnly), "Cannot create startup preference fixture");
    config.write("capture_on_start: false\ncapture_shortcut: ''\nocr_shortcut: ''\n");
    config.close();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_CONFIG_HOME", configHome);
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    const QString executable = QDir::current().absoluteFilePath("build/linux/x86_64/release/ScreenCapture");
    check(QFileInfo::exists(executable), "Build ScreenCapture before portal integration tests");
    auto waitFor = [&](auto predicate) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!predicate() && elapsed.elapsed() < 4000) QTest::qWait(10);
        return predicate();
    };
    QProcess output;
    output.setProcessEnvironment(environment);
    const QString exported = directory.filePath("output.png");
    output.start(executable, {"--output", exported});
    check(waitFor([&] { return output.state() == QProcess::NotRunning; }), "One-shot output must not hang when startup capture is disabled");
    check(output.exitCode() == 0 && QImage(exported) == sample, "One-shot output must capture original image despite startup preference");
    QProcess resident;
    resident.setProcessEnvironment(environment);
    resident.start(executable, {"--enter=settings"});
    check(waitFor([&] { return bus.interface()->isServiceRegistered("io.github.xland.ScreenCapture").value(); }), "Resident app must register a single-instance service");
    const int before = mock.requests;
    QProcess second;
    second.setProcessEnvironment(environment);
    second.start(executable, {});
    check(waitFor([&] { return second.state() == QProcess::NotRunning; }) && second.exitCode() == 0,
        "Repeated launch must forward and exit");
    QTest::qWait(400);
    check(mock.requests == before, "Repeated launch must honor disabled startup capture");
    second.start(executable, {"--enter=cap"});
    check(waitFor([&] { return second.state() == QProcess::NotRunning && mock.requests == before + 1; }),
        "Explicit capture must reach the resident instance through D-Bus");
    resident.terminate();
    check(waitFor([&] { return resident.state() == QProcess::NotRunning; }), "Cannot stop resident fixture");
    qInfo("One-shot output, startup preference and single-instance forwarding checks passed");
    return 0;
}
