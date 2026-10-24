#pragma once
#include "Selection.h"
#include <QImage>
#include <QPointer>
#include <QWidget>
class QKeyEvent;
class QPainter;
class Settings;

class CaptureOverlay : public QWidget
{
    Q_OBJECT
public:
    CaptureOverlay(QImage image, Settings& settings, QString entry, bool autoQuit, const QList<QRectF>& windows = {});
    QRect selectedPixels() const;
    QRectF selectedGeometry() const;
    void showCapture();
signals:
    void actionRequested(QString id, QImage image, QRectF geometry);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent*) override;
private:
    void pointer(QWidget* view, QMouseEvent* event);
    void key(QKeyEvent* event);
    void highlight();
    void refresh();
    void buildTools();
    void layoutTools();
    void trigger(const QString& id);
    void paint(QWidget* view, QPainter& painter);
    void paintMagnifier(QPainter& painter, const QRect& screen);
    QPointF pixelPoint(QPointF global) const;
    QPointF globalPoint(QPointF pixel) const;
    QImage image;
    Settings& settings;
    QString entry;
    bool autoQuit;
    Selection selection;
    QRect desktop;
    QList<QRectF> windows;
    QList<QWidget*> views;
    QWidget* tools;
    QPointF cursor;
    bool adjusted{false}, pressed{false}, dragged{false}, virtualCursor{false}, pointerKnown{false};
};
