#pragma once
#include <QImage>
#include <QPainterPath>
#include <QWidget>
#include <functional>

class Canvas : public QWidget
{
public:
    enum class Tool { Select, Pen, Rectangle, Ellipse, Arrow, Text };
    explicit Canvas(QImage image, QWidget* parent = nullptr);
    QImage result() const;
    QRect imageRect() const;
    void crop();
    void undo();
    void redo();
    void setTool(Tool value);
    QColor color{Qt::red};
    int lineWidth{3};
    std::function<void()> copied;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    struct Stroke {
        Tool tool;
        QPainterPath path;
        QPointF start, end;
        QColor color;
        int width;
        QString text;
    };
    void drawStroke(QPainter& painter, const Stroke& stroke) const;
    QPointF imagePoint(QPointF point) const;
    QImage image;
    QList<Stroke> strokes, undone;
    QRect selection;
    QPointF origin, mouseOrigin;
    bool selectionStarted{false};
    Stroke current{};
    Tool tool{Tool::Select};
    bool dragging{false};
};
