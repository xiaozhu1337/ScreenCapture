#include "CaptureOverlay.h"
#include "Ui.h"
#include <QFontMetrics>
#include <QPainter>
#include <QScreen>
#include <cmath>

void CaptureOverlay::paint(QWidget* view, QPainter& painter)
{
    const auto viewGeometry = view->property("captureGeometry").toRect();
    painter.translate(-viewGeometry.topLeft());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(desktop, image);
    const QColor shade(0, 0, 0, 117);
    const QRectF region(globalPoint(selection.area.topLeft()), globalPoint(selection.area.bottomRight()));
    if (selection.valid()) {
        painter.fillRect(QRectF(desktop.left(), desktop.top(), desktop.width(), region.top() - desktop.top()), shade);
        painter.fillRect(QRectF(desktop.left(), region.bottom(), desktop.width(), desktop.bottom() + 1 - region.bottom()), shade);
        painter.fillRect(QRectF(desktop.left(), region.top(), region.left() - desktop.left(), region.height()), shade);
        painter.fillRect(QRectF(region.right(), region.top(), desktop.right() + 1 - region.right(), region.height()), shade);
        painter.setPen(QPen(QColor("#1677ff"), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(region.adjusted(-1, -1, 1, 1));
        const auto pixels = selectedPixels();
        const QString text = QString("X:%1 Y:%2 R:%3 B:%4 W:%5 H:%6").arg(pixels.x()).arg(pixels.y())
            .arg(pixels.x() + pixels.width()).arg(pixels.y() + pixels.height()).arg(pixels.width()).arg(pixels.height());
        QFont font = painter.font();
        font.setPixelSize(10);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QSize size(metrics.horizontalAdvance(text) + 6, metrics.height() + 6);
        QPointF pos(region.left(), region.top() - 3 - size.height());
        if (pos.y() < desktop.top()) pos = region.topLeft() + QPointF(3, 1.5);
        painter.fillRect(QRectF(pos, size), shade);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(pos + QPointF(3, 3), size - QSize(6, 6)), Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    if (pointerKnown && !adjusted && !pressed && viewGeometry.contains(globalPoint(cursor).toPoint())) paintMagnifier(painter, viewGeometry);
    if (virtualCursor) {
        const auto position = globalPoint(cursor);
        painter.setPen(QPen(Qt::white, 3));
        painter.drawLine(position - QPointF(9, 0), position + QPointF(9, 0));
        painter.drawLine(position - QPointF(0, 9), position + QPointF(0, 9));
        painter.setPen(QPen(Qt::black, 1));
        painter.drawLine(position - QPointF(9, 0), position + QPointF(9, 0));
        painter.drawLine(position - QPointF(0, 9), position + QPointF(0, 9));
    }
}
void CaptureOverlay::paintMagnifier(QPainter& painter, const QRect& screen)
{
    const double ratio = image.width() / double(desktop.width());
    const QSizeF enlarged(250 / ratio, 150 / ratio);
    const QSizeF size(enlarged.width(), enlarged.height() + 76);
    const auto position = globalPoint(cursor);
    QPointF point = position + QPointF(11, 11);
    if (point.x() + size.width() > screen.right() + 1) point.setX(position.x() - 10 - size.width());
    if (point.y() + size.height() > screen.bottom() + 1) point.setY(position.y() - 10 - size.height());
    point.setX(qMax(qreal(screen.left()), point.x()));
    point.setY(qMax(qreal(screen.top()), point.y()));
    const QRectF panel(point, size);
    painter.fillRect(panel, QColor(0, 0, 0, 143));
    const QRect sample(int(cursor.x()) - 25, int(cursor.y()) - 15, 50, 30);
    const auto visible = sample.intersected(image.rect());
    const QRectF target(point + QPointF((visible.x() - sample.x()) * 5 / ratio, (visible.y() - sample.y()) * 5 / ratio),
                        QSizeF(visible.width() * 5 / ratio, visible.height() * 5 / ratio));
    painter.drawImage(target, image, visible);
    const QPointF center = point + QPointF(enlarged.width() / 2., enlarged.height() / 2.);
    const QColor cross(26, 128, 255, 127);
    painter.fillRect(QRectF(point.x(), center.y() - 4, enlarged.width() / 2. - 4, 8), cross);
    painter.fillRect(QRectF(center.x() + 4, center.y() - 4, enlarged.width() / 2. - 4, 8), cross);
    painter.fillRect(QRectF(center.x() - 4, point.y(), 8, enlarged.height() / 2. - 4), cross);
    painter.fillRect(QRectF(center.x() - 4, center.y() + 4, 8, enlarged.height() / 2. - 4), cross);
    const auto color = image.pixelColor(qBound(0, int(cursor.x()), image.width() - 1), qBound(0, int(cursor.y()), image.height() - 1));
    const QPoint physicalOrigin = (QPointF(desktop.topLeft()) * ratio).toPoint();
    const QStringList rows{
        "HEX (Ctrl+H) : " + color.name(QColor::HexRgb).toUpper(),
        QString("RGB (Ctrl+R) : %1,%2,%3").arg(color.red()).arg(color.green()).arg(color.blue()),
        "CMYK (Ctrl+K) : " + Ui::cmyk(color),
        QString("POS (Ctrl+P) : X:%1 Y:%2").arg(int(cursor.x()) + physicalOrigin.x()).arg(int(cursor.y()) + physicalOrigin.y())};
    QFont font = painter.font();
    font.setPixelSize(10);
    painter.setFont(font);
    painter.setPen(Qt::white);
    for (int index = 0; index < rows.size(); ++index)
        painter.drawText(QRectF(point + QPointF(7, enlarged.height() + 7 + index * 17), QSizeF(size.width() - 14, 13)), Qt::AlignVCenter, rows[index]);
}
