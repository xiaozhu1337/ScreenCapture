#include "WindowGeometry.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <cmath>

namespace {
const QString service = "org.kde.KWin";
const QString interface = "org.kde.kwin.Scripting";
}
WindowGeometry::WindowGeometry(QObject* parent) : QObject(parent)
{
    QDBusConnection::sessionBus().registerObject("/WindowGeometry", this, QDBusConnection::ExportScriptableSlots);
    timer.setSingleShot(true);
    timer.setInterval(1500);
    connect(&timer, &QTimer::timeout, this, [this] { finish(); });
}
WindowGeometry::~WindowGeometry()
{
    QDBusConnection::sessionBus().unregisterObject("/WindowGeometry");
    if (!plugin.isEmpty()) {
        QDBusInterface scripting(service, "/Scripting", interface);
        scripting.asyncCall("unloadScript", plugin);
    }
}
void WindowGeometry::request()
{
    if (active) return;
    active = true;
    timer.start();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(cache)) { finish(); return; }
    script.setFileTemplate(cache + "/window-geometry-XXXXXX.js");
    if (!script.open()) { finish(); return; }
    plugin = "ScreenCapture-window-geometry-" + QString::number(QCoreApplication::applicationPid());
    // KWin exposes native Wayland window bounds through its scripting API.
    const QByteArray code = QString(R"JS(
var bounds = [];
var windows = workspace.stackingOrder;
for (var i = windows.length - 1; i >= 0; --i) {
    var win = windows[i];
    if (win.minimized || win.deleted || win.hidden) continue;
    var desktop = workspace.currentDesktopForScreen ? workspace.currentDesktopForScreen(win.output) : workspace.currentDesktop;
    if (win.desktops.length && win.desktops.indexOf(desktop) === -1) continue;
    if (workspace.currentActivity && win.activities.length && win.activities.indexOf(workspace.currentActivity) === -1) continue;
    if (win.desktopWindow || win.tooltip || win.notification || win.criticalNotification ||
        win.onScreenDisplay || win.dndIcon || win.outline || win.inputMethod) continue;
    // Native Wayland menus may have an unknown type but still expose popupWindow.
    if (!win.normalWindow && !win.dialog && !win.dock && !win.toolbar && !win.utility &&
        !win.menu && !win.dropdownMenu && !win.popupMenu && !win.popupWindow &&
        !win.appletPopup && !win.comboBox && !win.splash) continue;
    var rect = win.frameGeometry;
    if (rect.width <= 6 || rect.height <= 6) continue;
    bounds.push({x: rect.x, y: rect.y, width: rect.width, height: rect.height});
}
callDBus("%1", "/WindowGeometry", "io.github.xland.ScreenCapture.Geometry", "setWindows", JSON.stringify(bounds));
)JS").arg(QDBusConnection::sessionBus().baseService()).toUtf8();
    if (script.write(code) != code.size() || !script.flush()) { finish(); return; }
    QDBusInterface scripting(service, "/Scripting", interface);
    auto* watcher = new QDBusPendingCallWatcher(scripting.asyncCall("loadScript", script.fileName(), plugin), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<int> reply = *watcher;
        watcher->deleteLater();
        if (!active) return;
        if (reply.isError() || reply.value() < 0) { finish(); return; }
        QDBusInterface loaded(service, "/Scripting/Script" + QString::number(reply.value()), "org.kde.kwin.Script");
        auto* run = new QDBusPendingCallWatcher(loaded.asyncCall("run"), this);
        connect(run, &QDBusPendingCallWatcher::finished, this, [this, run] {
            const QDBusPendingReply<> result = *run;
            run->deleteLater();
            if (result.isError()) finish();
        });
    });
}
void WindowGeometry::setWindows(const QString& json)
{
    if (!active) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) { finish(); return; }
    QList<QRectF> rectangles;
    for (const auto& entry : document.array()) {
        if (rectangles.size() >= 4096) break;
        const auto object = entry.toObject();
        if (!object["x"].isDouble() || !object["y"].isDouble() ||
            !object["width"].isDouble() || !object["height"].isDouble()) continue;
        const QRectF rectangle(object["x"].toDouble(), object["y"].toDouble(), object["width"].toDouble(), object["height"].toDouble());
        if (rectangle.width() > 6 && rectangle.height() > 6 &&
            std::isfinite(rectangle.right()) && std::isfinite(rectangle.bottom())) rectangles.append(rectangle);
    }
    finish(rectangles);
}
void WindowGeometry::finish(QList<QRectF> windows)
{
    if (!active) return;
    active = false;
    timer.stop();
    if (!plugin.isEmpty()) {
        QDBusInterface scripting(service, "/Scripting", interface);
        scripting.asyncCall("unloadScript", plugin);
        plugin.clear();
    }
    script.close();
    script.remove();
    emit ready(windows);
}
