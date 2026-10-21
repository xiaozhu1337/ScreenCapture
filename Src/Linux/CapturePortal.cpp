#include "CapturePortal.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QUuid>

namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString interface = QStringLiteral("org.freedesktop.portal.Request");
}

CapturePortal::CapturePortal(QObject* parent) : QObject(parent)
{
    timer.setSingleShot(true);
    timer.setInterval(120000);
    connect(&timer, &QTimer::timeout, this, [this] {
        QDBusInterface pending(service, request, interface, QDBusConnection::sessionBus());
        pending.asyncCall("Close");
        finish();
        emit failed(tr("截图请求超时，请检查桌面门户服务。"));
    });
}

void CapturePortal::capture()
{
    if (active) return;
    auto bus = QDBusConnection::sessionBus();
    QDBusInterface portal(service, "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.Screenshot", bus);
    if (!portal.isValid()) {
        emit failed(tr("无法连接截图桌面门户，请检查 xdg-desktop-portal 和桌面后端。"));
        return;
    }
    const QString token = "capture_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    request = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    if (!bus.connect(service, request, interface, "Response", this, SLOT(response(uint,QVariantMap)))) {
        request.clear();
        emit failed(tr("无法监听截图桌面门户的响应。"));
        return;
    }
    active = true;
    timer.start();
    QVariantMap options{{"handle_token", token}, {"interactive", true}, {"modal", false}};
    auto* watcher = new QDBusPendingCallWatcher(portal.asyncCall("Screenshot", QString(), options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (!active) return;
        if (reply.isError()) {
            const QString error = reply.error().message();
            finish();
            emit failed(tr("截图请求失败：%1").arg(error));
        } else if (reply.value().path() != request) {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(service, request, interface, "Response", this, SLOT(response(uint,QVariantMap)));
            request = reply.value().path();
            if (!bus.connect(service, request, interface, "Response", this, SLOT(response(uint,QVariantMap)))) {
                finish();
                emit failed(tr("无法监听截图响应。"));
            }
        }
    });
}

void CapturePortal::finish()
{
    timer.stop();
    QDBusConnection::sessionBus().disconnect(service, request, interface, "Response", this, SLOT(response(uint,QVariantMap)));
    request.clear();
    active = false;
}

void CapturePortal::response(uint code, const QVariantMap& results)
{
    if (!active) return;
    finish();
    if (code == 1) { emit cancelled(); return; }
    if (code != 0) { emit failed(tr("桌面门户未能完成截图。")); return; }
    const QUrl uri(results.value("uri").toString());
    if (!uri.isLocalFile()) { emit failed(tr("截图门户返回了无效的图片地址。")); return; }
    const QImage image(uri.toLocalFile());
    if (image.isNull()) { emit failed(tr("无法读取桌面门户返回的图片。")); return; }
    emit captured(image);
}
