#include "Editor.h"
#include "Canvas.h"
#include "Settings.h"
#include "Ui.h"
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSlider>
#include <QToolButton>
#include <cmath>

Editor::Editor(QImage image, bool quit, Settings* preferences, QWidget* parent)
    : QMainWindow(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      settings(preferences), autoQuit(quit)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("ScreenCapture");
    canvas = new Canvas(std::move(image), this);
    canvas->setPinned(true);
    setCentralWidget(canvas);
    tools = new QWidget(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    tools->setAttribute(Qt::WA_ShowWithoutActivating);
    Ui::stylePanel(tools);
    auto* layout = new QHBoxLayout(tools);
    layout->setSpacing(0);
    layout->setContentsMargins(1, 1, 1, 1);
    QStringList ids = Ui::drawingTools();
    ids << "|" << "undo" << "redo" << "|" << "close" << "save" << "clipboard";
    for (const auto& id : ids) {
        if (id == "|") {
            auto* line = new QLabel(tools);
            line->setFixedSize(1, 18);
            line->setStyleSheet("background: #dddddd");
            layout->addWidget(line);
            continue;
        }
        auto* button = Ui::button(tools, id);
        button->setCheckable(Ui::drawingTools().contains(id));
        connect(button, &QToolButton::clicked, this, [this, id] {
            if (id == "close") close();
            else if (id == "save") save();
            else if (id == "clipboard") copy();
            else if (id == "undo") canvas->undo();
            else if (id == "redo") canvas->redo();
            else selectTool(selected == id ? QString() : id);
        });
        layout->addWidget(button);
    }
    tools->adjustSize();
    styles = new QWidget(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    styles->setAttribute(Qt::WA_ShowWithoutActivating);
    Ui::stylePanel(styles);
    canvas->copied = [this] { copy(); };
    canvas->rightClicked = [this] {
        if (!selected.isEmpty()) selectTool({});
        else { toolsVisible = !toolsVisible; layoutTools(); }
    };
    canvas->clicked = [this] { toolsVisible = true; layoutTools(); };
    canvas->moved = [this](QPoint delta) { tools->hide(); styles->hide(); place(placement.translated(delta)); };
    canvas->zoomed = [this](int steps, QPoint anchor) { zoom(steps, anchor); };
    canvas->sizeChanged = [this](const QString& id, int size) {
        if (settings) { settings->values.toolPin[id].size = size; settings->save(); }
        if (id == selected && slider) slider->setValue(size);
    };
    if (settings) connect(settings, &Settings::changed, this, &Editor::applySettings);
    const auto* screen = QGuiApplication::primaryScreen();
    const QRect work = screen->availableGeometry();
    const qreal dpr = screen->devicePixelRatio();
    QSize preferred = canvas->imageSize() / dpr;
    preferred.scale(preferred.boundedTo(work.size()), Qt::KeepAspectRatio);
    place(QRect(work.center() - QPoint(preferred.width() / 2, preferred.height() / 2), preferred));
    applySettings();
}
void Editor::applySettings()
{
    if (settings && !selected.isEmpty()) canvas->setStyle(settings->values.toolPin.value(selected));
    for (auto* button : tools->findChildren<QToolButton*>()) {
        const QString key = "tool." + button->objectName();
        button->setToolTip(Ui::text(key));
        button->setAccessibleName(Ui::text(key));
    }
    layoutTools();
}
void Editor::selectTool(const QString& id)
{
    selected = Ui::drawingTools().contains(id) ? id : QString();
    canvas->setTool(Canvas::fromId(selected));
    if (!selected.isEmpty()) canvas->setStyle(settings ? settings->values.toolPin.value(selected) : Preferences().toolPin.value(selected));
    for (auto* button : tools->findChildren<QToolButton*>()) button->setChecked(button->objectName() == selected);
    toolsVisible = true;
    rebuildStyles();
    layoutTools();
}
void Editor::place(QRect geometry)
{
    placement = geometry;
    placing = true;
    Ui::positionWindow(this, geometry);
    placing = false;
    auto* screen = QGuiApplication::screenAt(geometry.center());
    canvas->pixelRatio = screen ? screen->devicePixelRatio() : 1;
    canvas->setStyle(canvas->style());
    layoutTools();
}
void Editor::layoutTools()
{
    if (!isVisible() || !toolsVisible) { tools->hide(); styles->hide(); return; }
    QScreen* screen = QGuiApplication::screenAt(placement.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto work = screen->availableGeometry();
    const int subHeight = selected == "line" ? qMax(37, canvas->lineWidth + 5) : 37;
    const int totalHeight = tools->height() + 2 + subHeight;
    const auto pos = Ui::toolbarPosition(placement, work, tools->size(), totalHeight,
                                       settings && settings->values.toolbarBottom);
    Ui::positionWindow(tools, QRect(pos, tools->size()), false);
    tools->show();
    if (selected.isEmpty()) { styles->hide(); return; }
    styles->adjustSize();
    const int width = styles->sizeHint().width();
    int x = pos.x();
    if (selected == "mosaic" || selected == "eraser") {
        const auto* button = tools->findChild<QToolButton*>(selected);
        x = pos.x() + button->geometry().center().x() - width / 2;
    }
    x = qBound(work.left(), x, qMax(work.left(), work.right() + 1 - width));
    Ui::positionWindow(styles, QRect(x, pos.y() + tools->height() + 2, width, subHeight), false);
    styles->show();
}
void Editor::zoom(int steps, QPoint anchor)
{
    canvas->finishText();
    const double current = placement.width() / double(canvas->imageSize().width());
    const double maximum = qMax(1., qMin(8., 16000. / qMax(canvas->imageSize().width(), canvas->imageSize().height())));
    const double next = qBound(.1, current * std::pow(1.1, steps), maximum);
    const QSize size(qMax(1, qRound(canvas->imageSize().width() * next)), qMax(1, qRound(canvas->imageSize().height() * next)));
    const QPoint offset(qRound(anchor.x() * (1 - next / current)), qRound(anchor.y() * (1 - next / current)));
    place(QRect(placement.topLeft() + offset, size));
    canvas->setTool(canvas->selectedTool());
}
void Editor::keyPressEvent(QKeyEvent* event)
{
    if (auto* input = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()); input && isAncestorOf(input)) {
        if (event->key() == Qt::Key_Escape) canvas->finishText();
        else QMainWindow::keyPressEvent(event);
        return;
    }
    if (event->matches(QKeySequence::Undo)) canvas->undo();
    else if (event->matches(QKeySequence::Redo)) canvas->redo();
    else if (event->matches(QKeySequence::Copy) || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) copy();
    else if (event->matches(QKeySequence::Save)) save();
    else if (event->key() == Qt::Key_Escape) close();
    else QMainWindow::keyPressEvent(event);
}
void Editor::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); layoutTools(); canvas->setFocus(); }
void Editor::hideEvent(QHideEvent* event) { tools->hide(); styles->hide(); QMainWindow::hideEvent(event); }
void Editor::resizeEvent(QResizeEvent* event) { QMainWindow::resizeEvent(event); if (!placing) layoutTools(); }
void Editor::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (!placing && QGuiApplication::platformName() != "wayland") { placement.moveTopLeft(pos()); layoutTools(); }
}
void Editor::closeEvent(QCloseEvent* event)
{
    canvas->finishText();
    tools->hide();
    styles->hide();
    QMainWindow::closeEvent(event);
}
void Editor::copy() { canvas->finishText(); Ui::copyImage(canvas->result(), autoQuit); close(); }
void Editor::save()
{
    canvas->finishText();
    QString directory = settings ? settings->values.saveDirectory : QString();
    if (!Ui::saveImage(this, canvas->result(), directory)) return;
    if (settings) { settings->values.saveDirectory = directory; settings->save(); }
    close();
}
