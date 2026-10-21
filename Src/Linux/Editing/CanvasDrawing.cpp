#include "Canvas.h"
#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <limits>

QFont Canvas::Stroke::font() const
{
    auto font = QApplication::font();
    font.setPointSizeF(qMax(1., style.size * ratio) * 72 / QFontMetricsF(font).fontDpi());
    font.setBold(style.bold);
    font.setItalic(style.italic);
    return font;
}
bool Canvas::Stroke::freehand() const
{
    return tool == Tool::Pen || ((tool == Tool::Mosaic || tool == Tool::Eraser) && !style.rectangle);
}
void Canvas::Stroke::rebuildPath()
{
    path = {};
    if (points.isEmpty()) return;
    start = points.first();
    end = points.last();
    path.moveTo(start);
    for (int i = 1; i < points.size(); ++i) path.lineTo(points[i]);
}
void Canvas::Stroke::translate(QPointF delta)
{
    start += delta;
    end += delta;
    for (auto& point : points) point += delta;
    path.translate(delta);
}
QRectF Canvas::Stroke::bounds() const
{
    if (tool == Tool::Number) return QRectF(start - QPointF(radius, radius), QSizeF(radius * 2, radius * 2));
    if (tool == Tool::Text) {
        const auto textFont = font();
        const QFontMetricsF metrics(textFont);
        qreal width = 0;
        const auto lines = text.split('\n');
        for (const auto& line : lines) {
            QTextLayout layout(line, textFont);
            layout.beginLayout();
            auto textLine = layout.createLine();
            if (textLine.isValid()) {
                textLine.setLineWidth(std::numeric_limits<int>::max() / 64);
                width = qMax(width, textLine.naturalTextWidth());
            }
            layout.endLayout();
        }
        const qreal padding = 6 * ratio;
        return QRectF(start - QPointF(padding, padding), QSizeF(qMax(1., width) + padding * 2,
            qMax(metrics.height(), metrics.lineSpacing() * lines.size()) + padding * 2));
    }
    if (freehand()) return path.boundingRect();
    return QRectF(start, end).normalized();
}
QPainterPath Canvas::mask(const Stroke& stroke) const
{
    QPainterPath mask;
    if (stroke.style.rectangle) mask.addRect(stroke.bounds());
    else {
        QPainterPathStroker stroker;
        stroker.setWidth(stroke.style.size * stroke.ratio);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        mask = stroker.createStroke(stroke.path);
    }
    return mask;
}
void Canvas::drawStroke(QPainter& painter, const Stroke& stroke) const
{
    painter.save();
    const auto& style = stroke.style;
    QColor ink = style.color;
    if (stroke.tool == Tool::Pen && style.transparent) ink.setAlphaF(0.5);
    const qreal width = style.size * stroke.ratio;
    painter.setPen(QPen(ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const auto bounds = stroke.bounds();
    switch (stroke.tool) {
    case Tool::Pen: painter.drawPath(stroke.path); break;
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Arrow:
    case Tool::Number: {
        QPen pen(ink, stroke.tool == Tool::Arrow || stroke.tool == Tool::Number ? stroke.ratio : width,
            Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        pen.setMiterLimit(5);
        painter.setPen(pen);
        const auto shape = outline(stroke);
        if (style.fill) painter.fillPath(shape, ink);
        else painter.drawPath(shape);
        if (stroke.tool == Tool::Number) {
            auto font = QApplication::font();
            font.setPointSizeF(qMax(1., stroke.radius) * 72 / QFontMetricsF(font).fontDpi());
            painter.setFont(font);
            painter.setPen(style.fill ? QColor(Qt::white) : ink);
            painter.drawText(bounds, Qt::AlignCenter, QString::number(stroke.number));
        }
        break;
    }
    case Tool::Text: {
        const auto font = stroke.font();
        const QFontMetricsF metrics(font);
        painter.setPen(ink);
        qreal y = stroke.start.y();
        for (const auto& line : stroke.text.split('\n')) {
            QTextLayout layout(line, font);
            layout.beginLayout();
            auto textLine = layout.createLine();
            if (textLine.isValid()) textLine.setLineWidth(std::numeric_limits<int>::max() / 64);
            layout.endLayout();
            layout.draw(&painter, QPointF(stroke.start.x(), y));
            y += metrics.lineSpacing();
        }
        break;
    }
    case Tool::Mosaic:
    case Tool::Eraser: {
        const auto coverage = mask(stroke);
        if (stroke.preview) painter.fillPath(coverage, QColor(240, 15, 240, qRound(255 * 0.38)));
        else {
            painter.setClipPath(coverage, Qt::IntersectClip);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            if (stroke.tool == Tool::Mosaic) painter.drawImage(stroke.pixelOrigin, stroke.pixels);
            else {
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(QPoint(), image);
            }
        }
        break;
    }
    default: break;
    }
    painter.restore();
}
void Canvas::buildMosaic(int index)
{
    auto& stroke = strokes[index];
    const int block = qMax(6, qRound(stroke.style.size * stroke.ratio / 3));
    const QRectF bounds = mask(stroke).boundingRect();
    const QRect area = QRect(QPoint(int(std::floor(bounds.left())) - block, int(std::floor(bounds.top())) - block),
        QPoint(int(std::ceil(bounds.right())) + block, int(std::ceil(bounds.bottom())) + block)).intersected(image.rect());
    if (area.isEmpty()) { stroke.pixels = {}; return; }
    QImage pixels(area.size(), QImage::Format_ARGB32_Premultiplied);
    pixels.fill(Qt::transparent);
    {
        QPainter painter(&pixels);
        painter.translate(-area.topLeft());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawImage(QPoint(), image);
        for (int i = 0; i < index; ++i) if (!strokes[i].undone) drawStroke(painter, strokes[i]);
    }
    for (int y = 0; y < pixels.height(); y += block) {
        const int bottom = qMin(y + block, pixels.height());
        for (int x = 0; x < pixels.width(); x += block) {
            const int right = qMin(x + block, pixels.width());
            quint64 r = 0, g = 0, b = 0, a = 0;
            const int count = (bottom - y) * (right - x);
            for (int yy = y; yy < bottom; ++yy) {
                const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(yy));
                for (int xx = x; xx < right; ++xx) {
                    r += qRed(row[xx]); g += qGreen(row[xx]); b += qBlue(row[xx]); a += qAlpha(row[xx]);
                }
            }
            const QRgb average = qRgba(r / count, g / count, b / count, a / count);
            for (int yy = y; yy < bottom; ++yy) {
                auto* row = reinterpret_cast<QRgb*>(pixels.scanLine(yy));
                std::fill(row + x, row + right, average);
            }
        }
    }
    stroke.pixelOrigin = area.topLeft();
    stroke.pixels = std::move(pixels);
}
