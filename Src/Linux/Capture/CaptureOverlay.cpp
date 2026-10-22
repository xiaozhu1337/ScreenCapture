#include "CaptureOverlay.h"
#include "Settings.h"
#include "Ui.h"
#include <LayerShellQt/Window>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QEnterEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolButton>
#include <QWindow>
#include <cmath>

CaptureOverlay::CaptureOverlay(QImage source, Settings& preferences, QString mode, bool quit, const QList<QRect>& rectangles)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      image(std::move(source)), settings(preferences), entry(std::move(mode)), autoQuit(quit), selection(image.size()), windows(rectangles)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Screen Capture");
    for (auto* screen : QGuiApplication::screens()) desktop = desktop.united(screen->geometry());
    auto* primary = QGuiApplication::screenAt(QCursor::pos());
    if (!primary) primary = QGuiApplication::primaryScreen();
    auto screens = QGuiApplication::screens();
    screens.removeOne(primary);
    screens.prepend(primary);
    for (auto* screen : screens) {
        auto* view = views.isEmpty() ? this : new QWidget(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        view->setObjectName("captureOverlay");
        view->setProperty("captureOrigin", screen->geometry().topLeft());
        view->setProperty("captureGeometry", screen->geometry());
        view->setMouseTracking(true);
        view->setCursor(Qt::CrossCursor);
        view->setFocusPolicy(Qt::StrongFocus);
        view->installEventFilter(this);
        Ui::positionWindow(view, screen->geometry());
        views.append(view);
    }
    cursor = pixelPoint(QCursor::pos());
    tools = new QWidget(this);
    tools->hide();
    Ui::stylePanel(tools);
    buildTools();
    highlight();
    connect(&settings, &Settings::changed, this, [this] { buildTools(); layoutTools(); refresh(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { close(); });
}
void CaptureOverlay::showCapture()
{
    for (auto it = views.crbegin(); it != views.crend(); ++it) (*it)->show();
    if (QGuiApplication::platformName() == "wayland") {
        auto* layer = LayerShellQt::Window::get(windowHandle());
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    }
    raise();
    activateWindow();
    setFocus();
}
QPointF CaptureOverlay::pixelPoint(QPointF point) const
{
    return {(point.x() - desktop.x()) * image.width() / desktop.width(),
            (point.y() - desktop.y()) * image.height() / desktop.height()};
}
QPointF CaptureOverlay::globalPoint(QPointF point) const
{
    return {desktop.x() + point.x() * desktop.width() / image.width(),
            desktop.y() + point.y() * desktop.height() / image.height()};
}
QRect CaptureOverlay::selectedPixels() const
{
    const auto area = selection.area;
    const int left = qBound(0, int(std::floor(area.left())), image.width());
    const int top = qBound(0, int(std::floor(area.top())), image.height());
    const int right = qBound(left, int(std::ceil(area.right())), image.width());
    const int bottom = qBound(top, int(std::ceil(area.bottom())), image.height());
    return QRect(left, top, right - left, bottom - top);
}
QRectF CaptureOverlay::selectedGeometry() const
{
    const auto pixels = selectedPixels();
    return QRectF(globalPoint(pixels.topLeft()), globalPoint(pixels.topLeft() + QPoint(pixels.width(), pixels.height())));
}
void CaptureOverlay::highlight()
{
    if (!pointerKnown || adjusted || pressed) return;
    const auto global = globalPoint(cursor).toPoint();
    for (const auto& rect : windows) {
        if (!rect.contains(global)) continue;
        const auto clipped = rect.intersected(desktop);
        selection.area = QRectF(pixelPoint(clipped.topLeft()), pixelPoint(clipped.topLeft() + QPoint(clipped.width(), clipped.height())));
        return;
    }
    selection.area = {};
}
void CaptureOverlay::refresh() { for (auto* view : views) view->update(); }
void CaptureOverlay::buildTools()
{
    if (auto* layout = tools->layout()) {
        while (auto* item = layout->takeAt(0)) { delete item->widget(); delete item; }
        delete layout;
    }
    auto* layout = new QGridLayout(tools);
    layout->setSpacing(0);
    layout->setContentsMargins(1, 1, 1, 1);
    QStringList ids = settings.values.captureToolbar;
    ids << "close" << "save" << "clipboard";
    const auto* screen = QGuiApplication::screenAt(globalPoint(cursor).toPoint());
    const int width = screen ? screen->availableGeometry().width() : desktop.width();
    const int columns = qMax(1, (width - 2) / 32);
    int index = 0;
    for (const auto& id : ids) {
        auto* button = Ui::button(tools, id);
        connect(button, &QToolButton::clicked, this, [this, id] { trigger(id); });
        layout->addWidget(button, index / columns, index % columns);
        ++index;
    }
    tools->adjustSize();
}
void CaptureOverlay::layoutTools()
{
    if (!adjusted || pressed || !selection.valid()) { tools->hide(); return; }
    const QRect region = selectedGeometry().toAlignedRect();
    auto* screen = QGuiApplication::screenAt(region.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto work = screen->availableGeometry();
    const auto point = Ui::toolbarPosition(region, work, tools->size(), tools->height(), settings.values.toolbarBottom, 7);
    // Share the capture surface so Wayland cannot stack controls below the active overlay.
    QWidget* view = this;
    for (auto* candidate : views) {
        if (candidate->property("captureGeometry").toRect() == screen->geometry()) { view = candidate; break; }
    }
    if (tools->parentWidget() != view) tools->setParent(view);
    tools->move(point - view->property("captureOrigin").toPoint());
    tools->show();
    tools->raise();
}
void CaptureOverlay::trigger(const QString& id)
{
    if (id == "close") { close(); return; }
    if (!selection.valid()) return;
    const QImage crop = image.copy(selectedPixels());
    if (id == "save") {
        tools->hide();
        for (auto* view : views) view->hide();
        if (!Ui::saveImage(nullptr, crop, settings.values.saveDirectory)) { showCapture(); layoutTools(); return; }
        settings.save();
    } else if (id == "clipboard") Ui::copyImage(crop, autoQuit);
    else {
        tools->hide();
        for (auto* view : views) view->hide();
        emit actionRequested(id, crop, selectedGeometry());
    }
    close();
}
bool CaptureOverlay::eventFilter(QObject* watched, QEvent* event)
{
    auto* view = qobject_cast<QWidget*>(watched);
    if (!view || !views.contains(view)) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::Paint) { QPainter painter(view); paint(view, painter); return true; }
    if (event->type() == QEvent::Enter) {
        // Wayland cannot report the global pointer position before the first surface enter.
        cursor = pixelPoint(view->property("captureOrigin").toPoint() + static_cast<QEnterEvent*>(event)->position());
        pointerKnown = true;
        virtualCursor = false;
        highlight();
        refresh();
    }
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) {
        pointer(view, static_cast<QMouseEvent*>(event));
        return true;
    }
    if (event->type() == QEvent::KeyPress) { key(static_cast<QKeyEvent*>(event)); return true; }
    return QWidget::eventFilter(watched, event);
}
void CaptureOverlay::closeEvent(QCloseEvent* event)
{
    tools->hide();
    for (auto* view : views) if (view != this) view->hide();
    QWidget::closeEvent(event);
}
