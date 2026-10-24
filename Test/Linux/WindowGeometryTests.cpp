#include "CaptureOverlay.h"
#include "Settings.h"
#include "WindowGeometry.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVirtualObject>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

static void check(bool value, const char* message)
{
    if (!value) qFatal("%s", message);
}

class KWin : public QDBusVirtualObject
{
public:
    QJSEngine engine;
    QString source, response;
    bool fail{false}, silent{false};
    int loads{0};
    QString introspect(const QString& path) const override {
        if (path == "/Scripting")
            return "<interface name='org.kde.kwin.Scripting'>"
                   "<method name='loadScript'><arg type='s' direction='in'/><arg type='s' direction='in'/>"
                   "<arg type='i' direction='out'/></method>"
                   "<method name='unloadScript'><arg type='s' direction='in'/><arg type='b' direction='out'/></method></interface>";
        return "<interface name='org.kde.kwin.Script'><method name='run'/></interface>";
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.member() == "loadScript") {
            ++loads;
            if (fail) {
                bus.send(message.createErrorReply("org.freedesktop.DBus.Error.Failed", "Simulated KWin failure"));
                return true;
            }
            QFile script(message.arguments()[0].toString());
            check(script.open(QIODevice::ReadOnly), "KWin must receive a readable script");
            source = QString::fromUtf8(script.readAll());
            bus.send(message.createReply(7));
        } else if (message.member() == "unloadScript") {
            bus.send(message.createReply(true));
        } else if (message.member() == "run") {
            const auto evaluated = engine.evaluate(source);
            check(!evaluated.isError(), qPrintable(evaluated.toString()));
            bus.send(message.createReply());
            if (!silent) {
                auto callback = QDBusMessage::createMethodCall(message.service(), "/WindowGeometry",
                    "io.github.xland.ScreenCapture.Geometry", "setWindows");
                callback.setArguments({response.isNull() ? engine.globalObject().property("result").toString() : response});
                bus.asyncCall(callback);
            }
        } else return false;
        return true;
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    WindowGeometry geometry;
    QSignalSpy ready(&geometry, &WindowGeometry::ready);
    auto request = [&] {
        const int before = ready.size();
        geometry.request();
        geometry.request();
        check(QTest::qWaitFor([&] { return ready.size() > before; }, 2500), "Window discovery must finish");
        check(ready.size() == before + 1, "Window discovery must finish only once");
        return ready.last()[0].value<QList<QRectF>>();
    };
    if (app.arguments().contains("--probe")) {
        const auto windows = request();
        check(!windows.isEmpty(), "The native KWin probe must find visible windows");
        qInfo() << "Native KWin windows:" << windows;
        QDir().mkpath("__tmp");
        QTemporaryDir directory(QDir::current().absoluteFilePath("__tmp/geometry-native-XXXXXX"));
        check(directory.isValid(), "Cannot create native geometry test directory");
        Settings settings(directory.path());
        QRect desktopBounds;
        for (auto* screen : QGuiApplication::screens()) desktopBounds = desktopBounds.united(screen->geometry());
        QImage image(desktopBounds.size() * 2, QImage::Format_RGB32);
        image.fill(Qt::white);
        auto* overlay = new CaptureOverlay(image, settings, "cap", false, windows);
        overlay->showCapture();
        check(QTest::qWaitForWindowExposed(overlay), "Native selection overlay must be exposed");
        const auto clipped = windows.first().intersected(desktopBounds);
        check(clipped.width() > 6 && clipped.height() > 6, "The topmost native window must intersect the desktop");
        const QPointF local = clipped.center() - overlay->property("captureOrigin").toPoint();
        QEnterEvent event(local, local, clipped.center());
        QCoreApplication::sendEvent(overlay, &event);
        const QRectF pixels((clipped.topLeft() - desktopBounds.topLeft()) * 2, clipped.size() * 2);
        check(overlay->selectedPixels() == pixels.toAlignedRect(), "Native hover must snap to the frontmost KWin window");
        QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, local.toPoint());
        check(overlay->selectedPixels() == pixels.toAlignedRect(), "A native click must confirm the snapped window");
        delete overlay;
        qInfo("Native KWin discovery, hover and click confirmation checks passed");
        return 0;
    }
    auto bus = QDBusConnection::sessionBus();
    KWin mock;
    check(bus.registerService("org.kde.KWin"), "Run this test under dbus-run-session");
    check(bus.registerVirtualObject("/Scripting", &mock, QDBusConnection::SubPath), "Cannot register mock KWin");
    const auto fixture = mock.engine.evaluate(R"JS(
var desktopOne = {}, otherDesktop = {};
var workspace = {currentDesktop: desktopOne, currentActivity: "active", stackingOrder: [],
    currentDesktopForScreen: function(output) { return output === "other" ? otherDesktop : desktopOne; }};
function candidate(type, x) {
    var win = {desktops: [desktopOne], activities: [], frameGeometry: {x: x, y: 30.25, width: 80.5, height: 60.75}};
    win[type] = true;
    workspace.stackingOrder.push(win);
    return win;
}
function callDBus(service, path, iface, method, json) { result = json; }
candidate("normalWindow", -120.5);
candidate("dialog", 1.25);
candidate("dock", 2.25);
candidate("toolbar", 3.25);
candidate("utility", 4.25);
candidate("menu", 5.25);
candidate("dropdownMenu", 6.25);
candidate("popupMenu", 7.25);
candidate("popupWindow", 7.5);
candidate("appletPopup", 7.75);
candidate("comboBox", 8);
candidate("splash", 8.125);
candidate("normalWindow", 8.25).desktops = [];
candidate("normalWindow", 8.5).activities = ["active"];
var secondScreen = candidate("normalWindow", 9.25);
secondScreen.output = "other";
secondScreen.desktops = [otherDesktop];
candidate("desktopWindow", 90);
candidate("tooltip", 91).popupWindow = true;
candidate("notification", 92).popupWindow = true;
candidate("normalWindow", 93).minimized = true;
candidate("normalWindow", 94).deleted = true;
candidate("normalWindow", 95).hidden = true;
candidate("normalWindow", 96).desktops = [otherDesktop];
candidate("normalWindow", 97).activities = ["other"];
candidate("normalWindow", 98).frameGeometry.width = 6;
)JS");
    check(!fixture.isError(), "Invalid KWin fixture");
    const auto windows = request();
    check(windows.size() == 15 && mock.loads == 1, qPrintable(QString("Expected 15 visible windows from one script, got %1 windows and %2 loads")
        .arg(windows.size()).arg(mock.loads)));
    check(windows.first() == QRectF(9.25, 30.25, 80.5, 60.75) &&
          windows.last() == QRectF(-120.5, 30.25, 80.5, 60.75), "Discovery must preserve fractions, negative coordinates and stacking order");
    mock.engine.evaluate("workspace.currentDesktopForScreen = undefined; secondScreen.desktops = [desktopOne];");
    check(request() == windows, "Older KWin APIs must use the workspace's current desktop");
    mock.engine.evaluate("workspace.currentActivity = '';");
    check(request().size() == windows.size() + 1, "Inactive activity support must not exclude otherwise visible windows");
    mock.engine.evaluate("workspace.currentActivity = 'active';");

    QDir().mkpath("__tmp");
    QTemporaryDir directory(QDir::current().absoluteFilePath("__tmp/geometry-test-XXXXXX"));
    check(directory.isValid(), "Cannot create geometry test directory");
    Settings settings(directory.path());
    QRect desktopBounds;
    for (auto* screen : QGuiApplication::screens()) desktopBounds = desktopBounds.united(screen->geometry());
    QImage image(desktopBounds.size() * 2, QImage::Format_RGB32);
    image.fill(Qt::white);
    auto* overlay = new CaptureOverlay(image, settings, "cap", false, windows);
    auto enter = [&](QPointF global) {
        const auto local = global - overlay->property("captureOrigin").toPoint();
        QEnterEvent event(local, local, global);
        QCoreApplication::sendEvent(overlay, &event);
    };
    enter({20, 40});
    const auto clipped = windows.first().intersected(desktopBounds);
    const QRectF pixels((clipped.topLeft() - desktopBounds.topLeft()) * 2, clipped.size() * 2);
    check(overlay->selectedPixels() == pixels.toAlignedRect(), "Hover must select the topmost window and round only at image pixels");
    enter({89.6, 40});
    check(overlay->selectedPixels() == pixels.toAlignedRect(), "Fractional pointer positions inside the edge must retain the frontmost window");
    enter({9.1, 40});
    check(overlay->selectedPixels().isValid() && overlay->selectedPixels() != pixels.toAlignedRect(),
          "Fractional pointer positions must not round into a window outside their bounds");
    enter({400, 400});
    check(!overlay->selectedPixels().isValid(), "Empty desktop space must not snap to hidden or unsupported windows");
    delete overlay;

    mock.response = R"JSON([{},null,{"x":"bad","y":0,"width":40,"height":40},{"x":0,"y":0,"width":-10,"height":40},{"x":1e308,"y":0,"width":1e308,"height":40},{"x":-10.25,"y":-20.75,"width":40.5,"height":30.25}])JSON";
    check(request() == QList<QRectF>{QRectF(-10.25, -20.75, 40.5, 30.25)}, "Malformed rectangles must not become snap candidates");
    QJsonArray many;
    for (int i = 0; i < 4100; ++i) many.append(QJsonObject{{"x", i}, {"y", 0}, {"width", 40}, {"height", 40}});
    mock.response = QString::fromUtf8(QJsonDocument(many).toJson(QJsonDocument::Compact));
    check(request().size() == 4096, "Discovery must bound the candidate list");
    mock.response = "invalid JSON";
    check(request().isEmpty(), "Invalid KWin responses must allow manual selection");
    mock.response = "{}";
    check(request().isEmpty(), "Non-array KWin responses must allow manual selection");
    mock.fail = true;
    check(request().isEmpty(), "KWin service errors must allow manual selection");
    mock.fail = false;
    mock.silent = true;
    check(request().isEmpty(), "Unresponsive KWin scripts must time out to manual selection");
    mock.silent = false;
    mock.response = QString();
    check(request() == windows, "Discovery must recover after failure and timeout");
    qInfo("KWin script, fractional bounds, window types, visibility, stacking, hover and fallback checks passed");
}
