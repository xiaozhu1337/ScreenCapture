#pragma once
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class CapturePortal : public QObject
{
    Q_OBJECT
public:
    explicit CapturePortal(QObject* parent = nullptr);
    void capture();
signals:
    void captured(QImage image);
    void failed(QString reason);
    void cancelled();
private slots:
    void response(uint code, const QVariantMap& results);
private:
    void finish();
    QTimer timer;
    QString request;
    bool active{false};
};
