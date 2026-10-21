#include "LongCapture.h"
#include "LiveRegion.h"
#include "RecordingUi.h"
#include "../Desktop/Ui.h"
#include "../Settings/Settings.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>

LongCapture::LongCapture(QImage initial, QRect globalRegion, Settings& preferences, bool quit)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), settings(preferences), region(globalRegion), autoQuit(quit),
      stream(this), latest(std::move(initial)), stitcher(std::make_unique<ScrollStitcher>(latest))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(Ui::text("tool.long"));
    Ui::stylePanel(this);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    for (const auto& id : {"pin", "close", "save", "clipboard"}) {
        auto* button = Ui::button(this, id);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, id = QString(id)] {
            if (id == "close") { close(); return; }
            stop();
            if (id == "pin") emit pinRequested(stitcher->image(), region);
            else if (id == "clipboard") Ui::copyImage(stitcher->image(), autoQuit);
            else {
                if (!Ui::saveImage(nullptr, stitcher->image(), settings.values.saveDirectory)) return;
                settings.save();
            }
            close();
        });
    }
    setFixedSize(130, 34);
    preview = new QLabel(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    preview->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    connect(new QShortcut(QKeySequence::Copy, this), &QShortcut::activated, this, [this] {
        stop(); Ui::copyImage(stitcher->image(), autoQuit); close();
    });
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] {
        stop();
        if (Ui::saveImage(nullptr, stitcher->image(), settings.values.saveDirectory)) { settings.save(); close(); }
    });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, &QWidget::close);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &LongCapture::step);
    connect(&stream, &PortalStream::frameReady, this, [this](QImage image) {
        if (!running) return;
        latest = std::move(image);
        if (waiting) {
            waiting = false;
            stitcher = std::make_unique<ScrollStitcher>(latest);
            updatePreview();
            timer.start(88);
        }
    });
    connect(&stream, &PortalStream::cancelled, this, &QWidget::close);
    connect(&stream, &PortalStream::failed, this, [this](const QString& message) {
        stop(message); RecordingUi::error(this, message);
    });
    border = new LiveRegion(this, region, true);
    border->started = [this] { start(); };
    border->cancelled = [this] { close(); };
}
LongCapture::~LongCapture() { timer.stop(); stream.stop(); }
void LongCapture::start()
{
    if (running) return;
    running = waiting = true;
    auto* screen = QGuiApplication::screenAt(region.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto desktop = screen->geometry();
    const int x = region.right() + width() + 5 <= desktop.right() ? region.right() + 5 : region.left() - width() - 4;
    Ui::positionWindow(this, QRect(QPoint(x, region.bottom() + 1 - height()), size()));
    show();
    stream.start(region, true, false);
}
void LongCapture::stop(const QString& reason)
{
    timer.stop();
    stream.stop();
    running = waiting = false;
    if (!reason.isEmpty()) border->message(reason);
}
void LongCapture::step()
{
    if (!running || waiting) return;
    if (timer.property("collect").toBool()) {
        timer.setProperty("collect", false);
        const auto result = stitcher->append(latest);
        if (result == ScrollStitcher::Result::Invalid) {
            stop(tr("采集尺寸发生变化或内存不足，已保留当前结果。")); return;
        }
        if (result == ScrollStitcher::Result::Bottom) { stop(Ui::text("long.reachedBottom")); return; }
        if (result == ScrollStitcher::Result::HeightLimit) { updatePreview(); stop(Ui::text("long.tooLong")); return; }
        if (result == ScrollStitcher::Result::RetryFrame) {
            timer.setProperty("collect", true); timer.start(250); return;
        }
        if (result == ScrollStitcher::Result::Appended) updatePreview();
        timer.start(500);
    } else {
        if (QGuiApplication::platformName() != "wayland" && !region.contains(QCursor::pos())) { timer.start(500); return; }
        stream.scroll();
        timer.setProperty("collect", true);
        timer.start(250);
    }
}
void LongCapture::updatePreview()
{
    const QImage& image = stitcher->image();
    if (image.isNull()) return;
    auto* screen = QGuiApplication::screenAt(region.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const int available = qMax(1, geometry().top() - screen->geometry().top() - 2);
    const int sourceHeight = qMin(image.height(), qMax(1, int(qint64(available) * image.width() / width())));
    const QImage visible = image.copy(0, image.height() - sourceHeight, image.width(), sourceHeight);
    const auto pixels = QPixmap::fromImage(visible.scaledToWidth(width(), Qt::SmoothTransformation));
    preview->setPixmap(pixels);
    Ui::positionWindow(preview, QRect(QPoint(geometry().left(), geometry().top() - pixels.height() - 2), pixels.size()), false);
    preview->show();
}
void LongCapture::closeEvent(QCloseEvent* event)
{
    stop();
    preview->hide();
    border->hide();
    emit finished();
    QWidget::closeEvent(event);
}
