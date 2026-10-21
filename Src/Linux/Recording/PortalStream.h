#pragma once
#include "PipeWireFrames.h"
#include <QDBusObjectPath>
#include <QRect>
#include <QTimer>
#include <QVariantMap>

class PortalStream : public QObject
{
    Q_OBJECT
public:
    explicit PortalStream(QObject* parent = nullptr);
    ~PortalStream() override;
    void start(QRect globalRegion, bool pointerControl, bool cursor);
    void stop();
    void scroll();
    static QRect pixelRegion(QRect region, QRect screen, QSize pixels);
signals:
    void frameReady(QImage image);
    void failed(QString message);
    void cancelled();
private slots:
    void response(uint code, const QVariantMap& results);
    void sessionClosed(const QVariantMap& details);
private:
    enum class Step { Idle, Create, Devices, Sources, Start, Open };
    void request(QString interface, QString method, QVariantList args, QVariantMap options, Step next);
    void open(const QVariantMap& results);
    void fail(const QString& message);
    void disconnectRequest();
    PipeWireFrames frames;
    QTimer timer;
    QRect region, screen;
    QDBusObjectPath session;
    QString requestPath;
    Step step{Step::Idle};
    bool pointer{false}, withCursor{false}, delivered{false};
    uint node{};
};
