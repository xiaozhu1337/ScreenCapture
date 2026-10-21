#include "CaptureOverlay.h"
#include "Settings.h"
#include "Ui.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cmath>

void CaptureOverlay::pointer(QWidget* view, QMouseEvent* event)
{
    const QPointF global = view->property("captureOrigin").toPoint() + event->position();
    if (!virtualCursor || event->type() == QEvent::MouseMove) {
        cursor = pixelPoint(global);
        virtualCursor = false;
    }
    cursor.setX(qBound(0., cursor.x(), double(image.width())));
    cursor.setY(qBound(0., cursor.y(), double(image.height())));
    if (event->type() == QEvent::MouseButtonPress) {
        if (event->button() == Qt::RightButton) { close(); return; }
        if (event->button() != Qt::LeftButton) return;
        pressed = true;
        dragged = false;
        selection.begin(cursor, adjusted);
        tools->hide();
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (event->button() != Qt::LeftButton || !pressed) return;
        pressed = false;
        if (!selection.valid()) { refresh(); return; }
        const bool wasAdjusted = adjusted;
        adjusted = true;
        if (!wasAdjusted) {
            if (entry != "cap" && !entry.isEmpty()) { trigger(entry == "qr" ? "qrcode" : entry); return; }
            if (event->modifiers().testFlag(Qt::ControlModifier)) { trigger("pin"); return; }
        }
        layoutTools();
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        if (event->button() == Qt::LeftButton && adjusted) trigger("clipboard");
        return;
    } else if (pressed) {
        dragged = true;
        selection.move(cursor);
    } else highlight();
    Qt::CursorShape shape = Qt::CrossCursor;
    if (adjusted) {
        const int hit = selection.hit(cursor);
        shape = hit == 4 ? Qt::SizeAllCursor : hit == 0 || hit == 8 ? Qt::SizeFDiagCursor :
                hit == 2 || hit == 6 ? Qt::SizeBDiagCursor : hit == 1 || hit == 7 ? Qt::SizeVerCursor : Qt::SizeHorCursor;
    }
    for (auto* item : views) item->setCursor(shape);
    refresh();
}
void CaptureOverlay::key(QKeyEvent* event)
{
    const auto key = event->key();
    if (key == Qt::Key_Escape) { close(); return; }
    if (adjusted && !pressed && !settings.values.pinKey.isEmpty() &&
        settings.values.pinKey == QKeySequence(event->keyCombination())) { trigger("pin"); return; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { if (adjusted) trigger("clipboard"); return; }
    if (key >= Qt::Key_Left && key <= Qt::Key_Down) {
        cursor += QPoint(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0,
                         key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
        cursor.setX(qBound(0., cursor.x(), image.width() - 1.));
        cursor.setY(qBound(0., cursor.y(), image.height() - 1.));
        if (QGuiApplication::platformName() == "wayland") {
            virtualCursor = true;
            for (auto* view : views) view->setCursor(Qt::BlankCursor);
            if (pressed) selection.move(cursor); else highlight();
            refresh();
        } else QCursor::setPos(globalPoint(cursor).toPoint());
        return;
    }
    if (!event->modifiers().testFlag(Qt::ControlModifier)) return;
    const QPoint point(qBound(0, int(cursor.x()), image.width() - 1), qBound(0, int(cursor.y()), image.height() - 1));
    const auto color = image.pixelColor(point);
    QString text;
    if (key == Qt::Key_H) text = color.name(QColor::HexRgb).mid(1).toUpper();
    else if (key == Qt::Key_R) text = QString("rgb(%1,%2,%3)").arg(color.red()).arg(color.green()).arg(color.blue());
    else if (key == Qt::Key_K) text = "cmyk(" + Ui::cmyk(color) + ")";
    else if (key == Qt::Key_P) {
        const QPoint origin = (QPointF(desktop.topLeft()) * (image.width() / double(desktop.width()))).toPoint();
        text = QString("%1,%2").arg(point.x() + origin.x()).arg(point.y() + origin.y());
    }
    if (text.isEmpty()) return;
    Ui::copyText(text, autoQuit);
    close();
}
