#include "CapturePortal.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QDir>
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
    QString introspect(const QString&) const override {
        return "<interface name='org.freedesktop.portal.Screenshot'>"
               "<method name='Screenshot'><arg type='s' direction='in'/>"
               "<arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method></interface>";
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.member() != "Screenshot") return false;
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
    QTemporaryDir directory("__tmp/portal-test-XXXXXX");
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
    return 0;
}
