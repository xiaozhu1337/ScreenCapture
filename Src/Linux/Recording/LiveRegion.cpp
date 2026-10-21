#include "LiveRegion.h"
#include "../Desktop/Ui.h"
#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWindow>

LiveRegion::LiveRegion(QWidget* owner, QRect area, bool showPrompt)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      region(area), cursor(area.center()), prompt(showPrompt)
{
    setObjectName("liveCaptureRegion");
    auto screens = QGuiApplication::screens();
    auto* primary = QGuiApplication::screenAt(area.center());
    if (!primary) primary = QGuiApplication::primaryScreen();
    screens.removeOne(primary);
    screens.prepend(primary);
    for (auto* screen : screens) {
        auto* view = views.isEmpty() ? this : new QWidget(this, windowFlags());
        view->setAttribute(Qt::WA_TranslucentBackground);
        view->setMouseTracking(true);
        view->setFocusPolicy(Qt::StrongFocus);
        view->setProperty("liveGeometry", screen->geometry());
        view->setAttribute(Qt::WA_TransparentForMouseEvents, !prompt);
        if (!prompt) view->setWindowFlag(Qt::WindowTransparentForInput);
        view->setCursor(prompt ? Qt::BlankCursor : Qt::ArrowCursor);
        view->installEventFilter(this);
        Ui::positionWindow(view, screen->geometry(), prompt);
        views.append(view);
    }
    for (auto it = views.crbegin(); it != views.crend(); ++it) (*it)->show();
    if (prompt && QGuiApplication::platformName() == "wayland")
        LayerShellQt::Window::get(windowHandle())->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    if (prompt) { activateWindow(); setFocus(); }
}
void LiveRegion::begin()
{
    prompt = false;
    for (auto* view : views) {
        view->setCursor(Qt::ArrowCursor);
        view->setAttribute(Qt::WA_TransparentForMouseEvents);
        view->setWindowFlag(Qt::WindowTransparentForInput);
        Ui::positionWindow(view, view->property("liveGeometry").toRect(), false);
        view->show();
        view->update();
    }
}
void LiveRegion::message(QString text)
{
    status = std::move(text);
    for (auto* view : views) view->update();
}
bool LiveRegion::eventFilter(QObject* object, QEvent* event)
{
    auto* view = qobject_cast<QWidget*>(object);
    if (!view || !views.contains(view)) return QWidget::eventFilter(object, event);
    const QRect geometry = view->property("liveGeometry").toRect();
    if (event->type() == QEvent::Paint) {
        QPainter painter(view);
        painter.translate(-geometry.topLeft());
        QPainterPath outside, inside;
        outside.addRect(geometry);
        inside.addRect(region);
        painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 117));
        painter.setPen(QPen(QColor("#1677ff"), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(region.adjusted(-1, -1, 1, 1));
        painter.setRenderHint(QPainter::Antialiasing);
        if (prompt && region.contains(cursor.toPoint())) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 173));
            painter.drawEllipse(cursor, 30, 30);
            painter.setPen(Qt::white);
            QFont font = painter.font(); font.setPixelSize(16); painter.setFont(font);
            painter.drawText(QRectF(cursor - QPointF(30, 30), QSizeF(60, 60)), Qt::AlignCenter, Ui::text("long.start"));
        }
        if (!status.isEmpty()) {
            QFont font = painter.font(); font.setPixelSize(13); painter.setFont(font);
            QRect label(0, 0, painter.fontMetrics().horizontalAdvance(status) + 16, 30);
            label.moveCenter({region.center().x(), region.bottom() - 23});
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 173)); painter.drawRoundedRect(label, 4, 4);
            painter.setPen(Qt::white); painter.drawText(label, Qt::AlignCenter, status);
        }
        return true;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (cancelled) cancelled();
        return true;
    }
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        cursor = geometry.topLeft() + mouse->position();
        for (auto* item : views) item->update();
        if (prompt && event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && region.contains(cursor.toPoint())) {
            begin();
            if (started) started();
        }
        return true;
    }
    return QWidget::eventFilter(object, event);
}
