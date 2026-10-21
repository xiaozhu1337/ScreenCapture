#pragma once
#include <QObject>
#include <QRect>
#include <QTemporaryFile>
#include <QTimer>

class WindowGeometry : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.xland.ScreenCapture.Geometry")
public:
    explicit WindowGeometry(QObject* parent = nullptr);
    ~WindowGeometry() override;
    void request();
signals:
    void ready(QList<QRect> windows);
public slots:
    Q_SCRIPTABLE void setWindows(const QString& json);
private:
    void finish(QList<QRect> windows = {});
    QTemporaryFile script;
    QTimer timer;
    QString plugin;
    bool active{false};
};
