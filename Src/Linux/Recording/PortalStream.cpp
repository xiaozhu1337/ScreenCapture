#include "PortalStream.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <cmath>
#include <unistd.h>

namespace {
const QString service = "org.freedesktop.portal.Desktop";
const QString path = "/org/freedesktop/portal/desktop";
const QString requests = "org.freedesktop.portal.Request";
const QString screencast = "org.freedesktop.portal.ScreenCast";
const QString remote = "org.freedesktop.portal.RemoteDesktop";
QDBusInterface portal(const QString& interface) { return QDBusInterface(service, path, interface); }
QSize readSize(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>()) return {};
    const auto arg = value.value<QDBusArgument>();
    int w = 0, h = 0;
    arg.beginStructure(); arg >> w >> h; arg.endStructure();
    return {w, h};
}
QPoint readPoint(const QVariant& value)
{
    const auto size = readSize(value);
    return {size.width(), size.height()};
}
}

PortalStream::PortalStream(QObject* parent) : QObject(parent), frames(this)
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [this] { fail(tr("屏幕共享请求超时或采集流已中断。")); });
    connect(&frames, &PipeWireFrames::failed, this, [this](const QString& message) {
        if (step != Step::Idle) fail(message);
    });
    connect(&frames, &PipeWireFrames::frameReady, this, [this](const QImage& image) {
        if (step != Step::Open) return;
        const QRect crop = pixelRegion(region, screen, image.size());
        if (crop.isEmpty() || !image.rect().contains(crop)) { fail(tr("共享屏幕与截图区域不一致。")); return; }
        timer.stop();
        if (pointer && !delivered) {
            portal(remote).asyncCall("NotifyPointerMotionAbsolute", QVariant::fromValue(session), QVariantMap{}, node,
                double(region.center().x() - screen.x()), double(region.center().y() - screen.y()));
        }
        delivered = true;
        emit frameReady(image.copy(crop));
    });
}
PortalStream::~PortalStream() { stop(); }

QRect PortalStream::pixelRegion(QRect region, QRect screen, QSize pixels)
{
    if (screen.isEmpty() || !screen.contains(region) || pixels.isEmpty()) return {};
    const double sx = double(pixels.width()) / screen.width(), sy = double(pixels.height()) / screen.height();
    const int left = qRound((region.left() - screen.left()) * sx);
    const int top = qRound((region.top() - screen.top()) * sy);
    const int right = qRound((region.x() + region.width() - screen.x()) * sx);
    const int bottom = qRound((region.y() + region.height() - screen.y()) * sy);
    return {left, top, right - left, bottom - top};
}

void PortalStream::start(QRect globalRegion, bool pointerControl, bool cursor)
{
    stop();
    region = globalRegion;
    auto* output = QGuiApplication::screenAt(region.center());
    if (!output || !output->geometry().contains(region)) {
        fail(tr("请将长截图或录制区域限制在同一块屏幕内。")); return;
    }
    screen = output->geometry();
    pointer = pointerControl;
    withCursor = cursor;
    delivered = false;
    const auto token = "session_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    // The token fixes this path before CreateSession responds, allowing cancellation to close it.
    session = QDBusObjectPath(path + "/session/" + sender + "/" + token);
    request(pointer ? remote : screencast, "CreateSession", {}, {{"session_handle_token", token}}, Step::Create);
}

void PortalStream::request(QString interface, QString method, QVariantList args, QVariantMap options, Step next)
{
    disconnectRequest();
    auto bus = QDBusConnection::sessionBus();
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const auto token = "recording_" + QUuid::createUuid().toString(QUuid::Id128);
    requestPath = path + "/request/" + sender + "/" + token;
    step = next;
    if (!bus.connect(service, requestPath, requests, "Response", this, SLOT(response(uint,QVariantMap)))) {
        fail(tr("无法监听屏幕共享授权。")); return;
    }
    options.insert("handle_token", token);
    args.append(options);
    timer.start(120000);
    auto* watcher = new QDBusPendingCallWatcher(portal(interface).asyncCallWithArgumentList(method, args), this);
    const QString expected = requestPath;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, expected] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (requestPath != expected) return;
        if (reply.isError()) { fail(tr("屏幕共享请求失败：%1").arg(reply.error().message())); return; }
        if (reply.value().path() != requestPath) {
            disconnectRequest();
            requestPath = reply.value().path();
            if (!QDBusConnection::sessionBus().connect(service, requestPath, requests, "Response",
                this, SLOT(response(uint,QVariantMap)))) fail(tr("无法监听屏幕共享响应。"));
        }
    });
}

void PortalStream::disconnectRequest()
{
    if (!requestPath.isEmpty()) QDBusConnection::sessionBus().disconnect(service, requestPath, requests,
        "Response", this, SLOT(response(uint,QVariantMap)));
    requestPath.clear();
}
void PortalStream::response(uint code, const QVariantMap& results)
{
    if (step == Step::Idle) return;
    disconnectRequest();
    timer.stop();
    if (code == 1) { stop(); emit cancelled(); return; }
    if (code != 0) { fail(tr("桌面拒绝了屏幕共享或自动滚动授权。")); return; }
    const auto object = [this] { return QVariant::fromValue(session); };
    switch (step) {
    case Step::Create:
        session = QDBusObjectPath(results.value("session_handle").toString());
        if (session.path().isEmpty() || session.path() == "/") { fail(tr("门户返回了无效会话。")); return; }
        QDBusConnection::sessionBus().connect(service, session.path(), "org.freedesktop.portal.Session",
            "Closed", this, SLOT(sessionClosed(QVariantMap)));
        if (pointer) {
            request(remote, "SelectDevices", {object()}, {{"types", uint(2)}}, Step::Devices);
            break;
        }
        [[fallthrough]];
    case Step::Devices:
        request(screencast, "SelectSources", {object()},
            {{"types", uint(1)}, {"multiple", false}, {"cursor_mode", uint(withCursor ? 2 : 1)}}, Step::Sources);
        break;
    case Step::Sources:
        request(pointer ? remote : screencast, "Start", {object(), QString()}, {}, Step::Start);
        break;
    case Step::Start:
        if (pointer && !(results.value("devices").toUInt() & 2)) { fail(tr("未授权自动滚动所需的指针控制。")); return; }
        open(results);
        break;
    default: break;
    }
}

void PortalStream::open(const QVariantMap& results)
{
    const auto streams = results.value("streams").value<QDBusArgument>();
    uint streamNode = 0;
    QVariantMap properties;
    streams.beginArray();
    if (!streams.atEnd()) { streams.beginStructure(); streams >> streamNode >> properties; streams.endStructure(); }
    streams.endArray();
    if (!streamNode) { fail(tr("门户未返回屏幕视频流。")); return; }
    // ScreenCast coordinates are compositor logical coordinates, not video pixels.
    if (properties.contains("position") && properties.contains("size")) {
        const QRect actual(readPoint(properties.value("position")), readSize(properties.value("size")));
        if (actual != screen) { fail(tr("请选择截图区域所在的屏幕；本次选择与截图屏幕不一致。")); return; }
    } else if (QGuiApplication::screens().size() != 1) {
        fail(tr("桌面未提供屏幕坐标，无法安全裁剪多屏录制区域。")); return;
    }
    node = streamNode;
    step = Step::Open;
    timer.start(15000);
    auto* watcher = new QDBusPendingCallWatcher(portal(screencast).asyncCall("OpenPipeWireRemote",
        QVariant::fromValue(session), QVariantMap{}), this);
    const auto current = session.path();
    const auto serial = properties.value("pipewire-serial").toULongLong();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, current, serial] {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        watcher->deleteLater();
        if (step != Step::Open || session.path() != current) return;
        if (reply.isError()) { fail(tr("无法打开屏幕采集流：%1").arg(reply.error().message())); return; }
        const int fd = dup(reply.value().fileDescriptor());
        if (fd < 0 || !frames.start(fd, node, serial)) fail(tr("无法连接 PipeWire 屏幕采集流。"));
    });
}

void PortalStream::scroll()
{
    if (!pointer || step != Step::Open || !delivered) return;
    auto* watcher = new QDBusPendingCallWatcher(portal(remote).asyncCall("NotifyPointerAxisDiscrete",
        QVariant::fromValue(session), QVariantMap{}, uint(0), int(1)), this);
    const auto current = session.path();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, current] {
        QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (step == Step::Open && session.path() == current && reply.isError())
            fail(tr("自动滚动失败：%1").arg(reply.error().message()));
    });
}
void PortalStream::sessionClosed(const QVariantMap&)
{
    if (step != Step::Idle) fail(tr("桌面已结束屏幕共享。"));
}
void PortalStream::stop()
{
    step = Step::Idle;
    timer.stop();
    if (!requestPath.isEmpty()) QDBusInterface(service, requestPath, requests).asyncCall("Close");
    disconnectRequest();
    frames.stop();
    if (!session.path().isEmpty()) {
        QDBusConnection::sessionBus().disconnect(service, session.path(), "org.freedesktop.portal.Session",
            "Closed", this, SLOT(sessionClosed(QVariantMap)));
        QDBusInterface(service, session.path(), "org.freedesktop.portal.Session").asyncCall("Close");
        session = QDBusObjectPath();
    }
}
void PortalStream::fail(const QString& message)
{
    stop();
    qCritical("%s", qPrintable(message));
    emit failed(message);
}
