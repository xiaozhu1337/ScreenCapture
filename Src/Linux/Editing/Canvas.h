#pragma once
#include "Settings.h"
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QWidget>
#include <functional>
class QPlainTextEdit;

class Canvas : public QWidget
{
public:
    enum class Tool { Select, Pen, Rectangle, Ellipse, Arrow, Number, Text, Mosaic, Eraser };
    explicit Canvas(QImage image, QWidget* parent = nullptr);
    QImage result() const;
    QRectF imageRect() const;
    QSize imageSize() const { return image.size(); }
    void crop();
    void undo();
    void redo();
    void setTool(Tool value);
    Tool selectedTool() const { return tool; }
    void setPinned(bool value) { pinned = value; setMinimumSize(1, 1); }
    void setPinnedArea(QRectF area);
    void setStyle(const ToolStyle& value);
    ToolStyle style() const { return activeStyle; }
    void finishText();
    bool isDrawing() const { return dragging; }
    static Tool fromId(const QString& id);
    static QString toolId(Tool tool);
    QColor color{Qt::red};
    int lineWidth{3};
    qreal pixelRatio{1};
    std::function<void()> copied, rightClicked, clicked;
    std::function<void(QPoint)> moved;
    std::function<void(int, QPoint)> zoomed;
    std::function<void(const QString&, int)> sizeChanged;
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    friend class CanvasParityTests;
    struct Stroke {
        Tool tool{Tool::Select};
        QPainterPath path;
        QList<QPointF> points;
        QPointF start, end;
        ToolStyle style;
        QString text;
        int number{1};
        qreal ratio{1}, angle{270}, radius{0};
        bool undone{false}, preview{false};
        QImage pixels;
        QPoint pixelOrigin;
        QRectF bounds() const;
        QFont font() const;
        bool freehand() const;
        void rebuildPath();
        void translate(QPointF delta);
    };
    void discardUndone();
    void buildMosaic(int index);
    void drawStroke(QPainter& painter, const Stroke& stroke) const;
    void drawHandles(QPainter& painter, const Stroke& stroke) const;
    QPainterPath outline(const Stroke& stroke) const;
    QPainterPath mask(const Stroke& stroke) const;
    QList<QPointF> handles(const Stroke& stroke) const;
    QPointF imagePoint(QPointF point) const;
    bool hits(const Stroke& stroke, QPointF point) const;
    int hitTest(QPointF point) const;
    int handleAt(const Stroke& stroke, QPointF point) const;
    void dragStroke(Stroke& stroke, QPointF point, bool shift);
    void updateCursor();
    void editText(int index);
    void layoutText();
    QImage image;
    QList<Stroke> strokes;
    QRect selection;
    QRectF pinnedArea;
    QPointF origin, mouseOrigin, lastGlobal, lastPoint, anchor;
    Stroke current{};
    ToolStyle activeStyle;
    Tool tool{Tool::Select};
    bool selectionStarted{false}, dragging{false}, pinned{false}, editingExisting{false}, hasDragged{false};
    bool lastInserted{false}, textInserted{false};
    int hover{-1}, active{-1}, handle{-1}, hoverHandle{-1}, wheelRemainder{0};
    QPointer<QPlainTextEdit> textEditor;
    int textIndex{-1};
};
