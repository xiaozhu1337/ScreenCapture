#include "Canvas.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <cmath>

namespace {
class CanvasTextEdit : public QPlainTextEdit
{
public:
    explicit CanvasTextEdit(QWidget* parent) : QPlainTextEdit(parent) {}
    void setPadding(int padding) { setViewportMargins(padding, padding, padding, padding); }
};
}
void Canvas::editText(int index)
{
    if (textEditor) finishText();
    textIndex = index;
    auto& stroke = strokes[index];
    stroke.style = activeStyle;
    stroke.style.color = color;
    stroke.style.size = lineWidth;
    auto* editor = new CanvasTextEdit(this);
    textEditor = editor;
    editor->setFrameStyle(QFrame::NoFrame);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->document()->setDocumentMargin(0);
    editor->setPlainText(stroke.text);
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
    connect(editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!textEditor || textIndex < 0) return;
        strokes[textIndex].text = textEditor->toPlainText();
        layoutText();
        update();
    });
    layoutText();
    editor->show();
    editor->setFocus();
    editor->moveCursor(QTextCursor::End);
    update();
}
void Canvas::layoutText()
{
    if (!textEditor || textIndex < 0) return;
    const auto& stroke = strokes[textIndex];
    const auto area = imageRect();
    const qreal scale = double(area.width()) / image.width();
    auto font = stroke.font();
    font.setPointSizeF(stroke.style.size * stroke.ratio * scale * 72 / logicalDpiY());
    textEditor->setFont(font);
    textEditor->setTabStopDistance(80 * scale);
    textEditor->setStyleSheet(QString("QPlainTextEdit { background: transparent; color: %1; border: none; }")
        .arg(stroke.style.color.name(QColor::HexArgb)));
    static_cast<CanvasTextEdit*>(textEditor.data())->setPadding(qRound(6 * stroke.ratio * scale));
    const auto bounds = stroke.bounds();
    const auto topLeft = QPointF(area.topLeft()) + bounds.topLeft() * scale;
    textEditor->setGeometry(QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())),
        QSize(std::ceil(bounds.width() * scale) + 2, std::ceil(bounds.height() * scale) + 2)));
    textEditor->horizontalScrollBar()->setValue(0);
    textEditor->verticalScrollBar()->setValue(0);
}
void Canvas::finishText()
{
    if (!textEditor) return;
    auto* editor = textEditor.data();
    const int index = textIndex;
    const QString text = editor->toPlainText();
    textEditor = nullptr;
    textIndex = -1;
    editor->removeEventFilter(this);
    editor->viewport()->removeEventFilter(this);
    editor->hide();
    editor->deleteLater();
    if (index >= 0 && index < strokes.size()) {
        if (text.isEmpty()) strokes.removeAt(index);
        else strokes[index].text = text;
    }
    lastInserted = textInserted && !text.isEmpty();
    textInserted = false;
    hover = hoverHandle = -1;
    updateCursor();
    update();
}
bool Canvas::eventFilter(QObject* watched, QEvent* event)
{
    if (!textEditor) return QWidget::eventFilter(watched, event);
    const bool editorEvent = watched == textEditor || watched == textEditor->viewport();
    if (!editorEvent) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        finishText();
        setFocus();
        return true;
    }
    if (watched == textEditor && event->type() == QEvent::FocusOut) {
        const QPointer<QPlainTextEdit> editor = textEditor;
        QTimer::singleShot(0, this, [this, editor] {
            if (editor && editor == textEditor && !editor->hasFocus()) finishText();
        });
    }
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const auto position = static_cast<QWidget*>(watched)->mapTo(this, mouse->position());
        const int textHandle = handleAt(strokes[textIndex], imagePoint(position));
        if (event->type() == QEvent::MouseMove)
            static_cast<QWidget*>(watched)->setCursor(textHandle == 8 ? Qt::SizeAllCursor : Qt::IBeamCursor);
        else if (mouse->button() == Qt::LeftButton && textHandle == 8) {
            finishText();
            QMouseEvent press(QEvent::MouseButtonPress, position, mouse->globalPosition(), mouse->button(), mouse->buttons(), mouse->modifiers());
            mousePressEvent(&press);
            if (dragging) grabMouse();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
