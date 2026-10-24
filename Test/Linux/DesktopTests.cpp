#include "Canvas.h"
#include "CaptureOverlay.h"
#include "CapturePortal.h"
#include "DesktopApp.h"
#include "Editor.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Ui.h"
#include <LayerShellQt/Window>
#include <QApplication>
#include <QDBusInterface>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QRandomGenerator>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

static void check(bool value, const char* message) { if (!value) qFatal("%s", message); }
static void destroyClosed() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
static void checkUtilitySurfaces(QWidget* root)
{
    if (QGuiApplication::platformName() != "wayland") return;
    auto surfaces = root->findChildren<QWidget*>();
    surfaces.prepend(root);
    for (auto* surface : surfaces) {
        if (!surface->isWindow() || !surface->isVisible() || surface->windowType() != Qt::Tool) continue;
        check(LayerShellQt::Window::get(surface->windowHandle())->scope() == "utility",
              "Capture, pin and floating control surfaces must bypass normal-window animations");
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ScreenCaptureDesktopTests");
    app.setOrganizationName("xland");
    app.setDesktopFileName("io.github.xland.ScreenCapture");
    app.setQuitOnLastWindowClosed(false);
    QDir().mkpath("__tmp");
    QTemporaryDir temporary(QDir::current().absoluteFilePath("__tmp/desktop-test-XXXXXX"));
    check(temporary.isValid(), "Cannot prepare desktop tests");
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    Settings settings(temporary.filePath("application"));
    settings.values.captureKey = {};
    check(settings.values.startCapture && settings.values.pinKey == QKeySequence("F3"), "Windows startup and pin defaults must match");
    settings.values.captureToolbar = {"text", "line"};
    settings.values.toolPin["text"].size = 33;
    settings.values.toolPin["text"].bold = true;
    settings.values.toolPin["text"].color = QColor("#0958d9");
    settings.values.toolbarBottom = true;
    check(settings.save(), "Cannot save preferences");
    Settings restored(temporary.filePath("application"));
    check(restored.values.toolPin == settings.values.toolPin && restored.values.captureToolbar == settings.values.captureToolbar,
          "Per-tool styles and capture action order must survive restart");
    check(restored.values.toolbarBottom, "Toolbar placement must survive restart");
    Preferences invalid;
    invalid.captureKey = QKeySequence("Ctrl+C");
    check(!Settings::validate(invalid).isEmpty(), "Editing shortcuts cannot become global capture keys");
    invalid.captureKey = QKeySequence("F3");
    check(!Settings::validate(invalid).isEmpty(), "Capture and pin shortcuts must not overlap");

    QImage image(320, 180, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QList<QRectF> snapWindows;
    for (auto* screen : QGuiApplication::screens()) snapWindows.append(QRect(screen->geometry().topLeft() + QPoint(40, 30), QSize(80, 60)));
    auto* coldCapture = new CaptureOverlay(image, restored, "cap", false, snapWindows);
    QImage firstFrame(coldCapture->size(), QImage::Format_ARGB32);
    firstFrame.fill(Qt::transparent);
    coldCapture->render(&firstFrame);
    QImage background(firstFrame.size(), firstFrame.format());
    background.fill(Qt::white);
    check(firstFrame == background, "Before the first pointer event, capture must not flash a magnifier or guessed window selection");
    const QPoint local(60, 50);
    QEnterEvent entered(local, local, coldCapture->property("captureOrigin").toPoint() + local);
    QCoreApplication::sendEvent(coldCapture, &entered);
    check(coldCapture->selectedPixels().isValid(), "The first surface enter must initialize window snapping at the actual pointer");
    coldCapture->close();
    destroyClosed();
    QImage pinSource(277, 139, QImage::Format_RGB32);
    for (int y = 0; y < pinSource.height(); ++y)
        for (int x = 0; x < pinSource.width(); ++x)
            pinSource.setPixelColor(x, y, QColor(x % 2 ? 30 : 220, y % 2 ? 40 : 210, 90));
    QPointer<Editor> fractionalPin = new Editor(pinSource, false, &restored);
    const QRectF pinGeometry(QPointF(83.25, 62.25), QSizeF(pinSource.size()) / 1.35);
    fractionalPin->place(pinGeometry);
    fractionalPin->show();
    check(QTest::qWaitFor([&] { return fractionalPin->drawingCanvas()->size() == fractionalPin->imageGeometry().size(); }),
          "The configured pin and canvas must match the enclosing window size");
    auto* pinCanvas = fractionalPin->drawingCanvas();
    const auto pinArea = pinCanvas->imageRect();
    check(qFuzzyCompare(pinArea.width() / pinSource.width(), pinArea.height() / pinSource.height()),
          "Pinning a fractionally scaled crop must use the same horizontal and vertical scale");
    check(QLineF(QPointF(fractionalPin->imageGeometry().topLeft()) + pinArea.topLeft(), pinGeometry.topLeft()).length() < 1e-9
              && qAbs(pinArea.width() - pinGeometry.width()) < 1e-9 && qAbs(pinArea.height() - pinGeometry.height()) < 1e-9,
          "Pinning must preserve the fractional position and size of the selected source pixels");
    QImage pinRaster(pinSource.size(), QImage::Format_RGB32);
    pinRaster.setDevicePixelRatio(1.35);
    pinRaster.fill(Qt::black);
    {
        QPainter painter(&pinRaster);
        painter.translate(-pinArea.topLeft());
        pinCanvas->render(&painter);
    }
    const QRect interior(8, 8, pinSource.width() - 16, pinSource.height() - 16);
    check(pinRaster.copy(interior) == pinSource.copy(interior),
          "Fractional pin rendering must preserve one-pixel details without stretching or repeating rows or columns");
    const QPointF anchor = QPointF(fractionalPin->imageGeometry().topLeft()) + QPointF(70, 40);
    for (int delta : {120, -120}) {
        QWheelEvent wheel(anchor - fractionalPin->imageGeometry().topLeft(), anchor, {}, QPoint(0, delta), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(pinCanvas, &wheel);
        app.processEvents();
        const auto area = pinCanvas->imageRect();
        check(qFuzzyCompare(area.width() / pinSource.width(), area.height() / pinSource.height()),
              "Pinned zoom must preserve the source aspect ratio");
    }
    const auto returnedArea = pinCanvas->imageRect();
    check(QLineF(QPointF(fractionalPin->imageGeometry().topLeft()) + returnedArea.topLeft(), pinGeometry.topLeft()).length() < 1e-9
              && qAbs(returnedArea.width() - pinArea.width()) < 1e-9 && qAbs(returnedArea.height() - pinArea.height()) < 1e-9,
          "Zooming a pin in and out must restore its exact scale and position");
    pinCanvas->moved(QPoint(7, -9));
    check(QLineF(QPointF(fractionalPin->imageGeometry().topLeft()) + pinCanvas->imageRect().topLeft(),
                 pinGeometry.topLeft() + QPoint(7, -9)).length() < 1e-9
              && pinCanvas->result().convertToFormat(pinSource.format()) == pinSource,
          "Moving a pin must preserve its fractional placement and all exported pixels");
    fractionalPin->resize(101, 61);
    check(QTest::qWaitFor([&] { return pinCanvas->size() == fractionalPin->size(); }),
          "A resized native pin must configure its canvas dimensions");
    const auto resizedArea = pinCanvas->imageRect();
    check(QRectF(pinCanvas->rect()).contains(resizedArea)
              && qFuzzyCompare(resizedArea.width() / pinSource.width(), resizedArea.height() / pinSource.height()),
          "Resizing outside place must keep the complete image visible at a uniform scale");
    const QSize resized = fractionalPin->size();
    pinCanvas->moved(QPoint(4, 3));
    app.processEvents();
    check(fractionalPin->size() == resized, "Moving a resized pin must not restore its previous dimensions");
    fractionalPin->close();
    destroyClosed();
    QPointer<Editor> editor = new Editor(image, false, &restored);
    editor->show();
    app.processEvents();
    check(editor->drawingCanvas()->selectedTool() == Canvas::Tool::Select, "A fresh pin must have no drawing tool selected");
    editor->selectTool("text");
    checkUtilitySurfaces(editor);
    check(editor->drawingCanvas()->style() == restored.values.toolPin["text"], "Selecting a tool must restore its own style");
    auto* slider = editor->findChild<QSlider*>("toolSize");
    check(slider && slider->minimum() == 10 && slider->maximum() == 60, "Text size range must match Windows");
    slider->setValue(44);
    check(Settings(temporary.filePath("application")).values.toolPin["text"].size == 44, "Changing a tool size must persist immediately");
    check(restored.values.toolPin["rect"].size == 2, "Changing text style must not change other tools");
    editor->selectTool("line");
    check(editor->drawingCanvas()->selectedTool() == Canvas::Tool::Pen, "Line must activate in the pinned image");
    checkUtilitySurfaces(editor);
    editor->selectTool("eraser");
    check(editor->findChild<QToolButton*>("fill"), "Eraser must offer rectangular mode");
    editor->close();
    destroyClosed();
    check(editor.isNull(), "Closing a pin must release all its owned windows");

    auto* capture = new CaptureOverlay(image, restored, "cap", false);
    capture->showCapture();
    app.processEvents();
    checkUtilitySurfaces(capture);
    auto* initialAction = capture->findChild<QToolButton*>("text");
    check(initialAction && !initialAction->isVisible(), "Capture actions must stay hidden until a selection is complete");
    QTest::mousePress(capture, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseMove(capture, QPoint(220, 140));
    QTest::mouseRelease(capture, Qt::LeftButton, Qt::NoModifier, QPoint(220, 140));
    const QRect pixels = capture->selectedPixels();
    check(pixels.isValid() && image.rect().contains(pixels), "Selection must stay in source image pixels");
    auto* action = capture->findChild<QToolButton*>("text");
    check(action && action->isVisible(), "Configured actions must appear after selection");
    check(!capture->findChild<QToolButton*>("ocr"), "Disabled capture tools must remain hidden");
    auto* panel = action->parentWidget();
    check(panel && !panel->isWindow(), "The capture toolbar must share its capture surface");
    const QPoint panelOrigin = panel->mapTo(panel->window(), QPoint());
    check(panel->window()->rect().contains(QRect(panelOrigin, panel->size())),
          "The capture toolbar must stay inside the selected screen");
    // Opt in because this checks compositor stacking through the real screenshot portal.
    if (qEnvironmentVariableIsSet("SCREENCAPTURE_NATIVE_UI_TEST")) {
        check(QGuiApplication::platformName() == "wayland", "Native capture checks require Wayland");
        CapturePortal portal;
        QSignalSpy snapshots(&portal, &CapturePortal::captured);
        QSignalSpy errors(&portal, &CapturePortal::failed);
        QSignalSpy cancelled(&portal, &CapturePortal::cancelled);
        portal.capture();
        check(QTest::qWaitFor([&] { return !snapshots.isEmpty() || !errors.isEmpty() || !cancelled.isEmpty(); }, 5000),
              "Native toolbar screenshot timed out");
        check(!snapshots.isEmpty(), "Native toolbar screenshot failed or was cancelled");
        const auto screenshot = snapshots[0][0].value<QImage>();
        screenshot.save("__tmp/capture-menu-native.png");
        QRect desktop;
        for (auto* screen : QGuiApplication::screens()) desktop = desktop.united(screen->geometry());
        const QPoint point = panel->window()->property("captureOrigin").toPoint() + panelOrigin + QPoint(3, 3) - desktop.topLeft();
        const QPoint pixel(qRound(point.x() * screenshot.width() / double(desktop.width())),
                           qRound(point.y() * screenshot.height() / double(desktop.height())));
        const QPoint selected = (capture->selectedGeometry().center() - desktop.topLeft()).toPoint();
        const QPoint reference(qRound(selected.x() * screenshot.width() / double(desktop.width())),
                               qRound(selected.y() * screenshot.height() / double(desktop.height())));
        // Compare against the white selection so desktop-wide dimming does not affect this stacking check.
        check(screenshot.rect().contains(pixel) && screenshot.rect().contains(reference)
                  && screenshot.pixelColor(reference).lightness() > 0
                  && screenshot.pixelColor(pixel).lightness() * 255 >= screenshot.pixelColor(reference).lightness() * 230,
              "The capture toolbar must render above the full-screen overlay before pinning");
    }
    QSignalSpy requests(capture, &CaptureOverlay::actionRequested);
    QTest::mouseClick(action, Qt::LeftButton);
    check(requests.size() == 1 && requests[0][0].toString() == "text", "Clicking a capture tool must dispatch that action");
    check(requests[0][1].value<QImage>() == image.copy(pixels), "Toolbar actions must preserve the exact selected source pixels");
    destroyClosed();
    capture = new CaptureOverlay(image, restored, "cap", false);
    capture->showCapture();
    QTest::mousePress(capture, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseMove(capture, QPoint(220, 140));
    QTest::mouseRelease(capture, Qt::LeftButton, Qt::NoModifier, QPoint(220, 140));
    QSignalSpy pins(capture, &CaptureOverlay::actionRequested);
    const QRectF sourceGeometry = capture->selectedGeometry();
    QTest::keyClick(capture, Qt::Key_F3);
    check(pins.size() == 1 && pins[0][0].toString() == "pin", "F3 must pin the current selection");
    check(pins[0][1].value<QImage>() == image.copy(pixels), "Pinning must preserve the exact selected source pixels");
    check(pins[0][2].value<QRectF>() == sourceGeometry, "Pin actions must preserve fractional selection bounds");
    destroyClosed();
    if (qEnvironmentVariableIsSet("SCREENCAPTURE_NATIVE_UI_TEST")) {
        for (bool oneShot : {false, true}) {
            QImage source = image;
            source.fill(oneShot ? Qt::blue : Qt::red);
            QPointer<CaptureOverlay> copying = new CaptureOverlay(source, restored, "cap", oneShot);
            copying->showCapture();
            QTest::mousePress(copying, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
            QTest::mouseMove(copying, QPoint(220, 140));
            QTest::mouseRelease(copying, Qt::LeftButton, Qt::NoModifier, QPoint(220, 140));
            const auto expected = source.copy(copying->selectedPixels());
            QTest::mouseClick(copying->findChild<QToolButton*>("clipboard"), Qt::LeftButton);
            destroyClosed();
            check(copying.isNull(), "Copying must close the capture overlay");
            check(QTest::qWaitFor([&] { return qApp->findChildren<QProcess*>().isEmpty(); }, 5000),
                  "Clipboard publication must complete after the capture closes");
            for (int paste = 0; paste < 2; ++paste) {
                QProcess reader;
                reader.start("wl-paste", {"--no-newline", "--type", "image/png"});
                check(QTest::qWaitFor([&] { return reader.state() == QProcess::NotRunning; }, 3000), "External image paste timed out");
                check(reader.exitCode() == 0 && QImage::fromData(reader.readAllStandardOutput(), "PNG") == expected,
                      "A separate process must paste the exact latest crop after the overlay closes");
            }
        }
        QImage large(2048, 1024, QImage::Format_ARGB32);
        QRandomGenerator random(1);
        for (int y = 0; y < large.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(large.scanLine(y));
            for (int x = 0; x < large.width(); ++x) row[x] = random.generate();
        }
        QImage latest(48, 32, QImage::Format_ARGB32);
        latest.fill(Qt::green);
        Ui::copyImage(large, false);
        Ui::copyText("intermediate clipboard", false);
        Ui::copyImage(latest, false);
        check(QTest::qWaitFor([&] { return qApp->findChildren<QProcess*>().isEmpty(); }, 5000),
              "Queued clipboard publications must complete");
        QProcess reader;
        reader.start("wl-paste", {"--no-newline", "--type", "image/png"});
        check(QTest::qWaitFor([&] { return reader.state() == QProcess::NotRunning; }, 3000), "Latest image paste timed out");
        check(reader.exitCode() == 0 && QImage::fromData(reader.readAllStandardOutput(), "PNG") == latest,
              "An older large image or text copy must never replace the latest image");
    }

    const QString path = temporary.filePath("source.png");
    check(image.save(path), "Cannot prepare pin image");
    {
        DesktopApp desktop(restored, false, {});
        const bool tray = QSystemTrayIcon::isSystemTrayAvailable();
        check(desktop.initialize() == tray, "Resident initialization must reflect tray availability");
        const auto menu = desktop.trayMenu()->actions();
        check(menu.size() == 2 && menu[0]->objectName() == "settings" && menu[1]->objectName() == "quit",
              "Tray menu must match the two Windows actions");
        desktop.activate("pin", path);
        desktop.activate("pin", path);
        app.processEvents();
        QList<Editor*> pins;
        for (auto* window : app.topLevelWidgets()) if (auto* pin = dynamic_cast<Editor*>(window); pin && pin->isVisible()) pins.append(pin);
        check(pins.size() == 2, "Opening a pin must preserve existing pinned images");
        pins.first()->close();
        destroyClosed();
        check(pins.last()->isVisible(), "Closing one pin must not close another");
        if (qEnvironmentVariableIsSet("SCREENCAPTURE_NATIVE_UI_TEST")) {
            desktop.activate("cap", {});
            QPointer<CaptureOverlay> selection;
            check(QTest::qWaitFor([&] {
                for (auto* window : app.topLevelWidgets())
                    if (auto* overlay = dynamic_cast<CaptureOverlay*>(window); overlay && overlay->isVisible()) selection = overlay;
                return !selection.isNull();
            }, 5000), "Native capture must open a selection overlay");
            QTest::mousePress(selection, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
            QTest::mouseMove(selection, QPoint(220, 140));
            QTest::mouseRelease(selection, Qt::LeftButton, Qt::NoModifier, QPoint(220, 140));
            const QRectF geometry = selection->selectedGeometry();
            const QSize pixels = selection->selectedPixels().size();
            auto* line = selection->findChild<QToolButton*>("line");
            check(line && line->isVisible(), "The line action must be available after selecting a region");
            QTest::mouseClick(line, Qt::LeftButton);
            destroyClosed();
            check(selection.isNull(), "Selecting line must close the selection overlay");
            Editor* pin = nullptr;
            for (auto* window : app.topLevelWidgets())
                if (auto* candidate = dynamic_cast<Editor*>(window); candidate && candidate->isVisible()
                    && candidate->drawingCanvas()->selectedTool() == Canvas::Tool::Pen) pin = candidate;
            check(pin && pin->imageGeometry() == geometry.toAlignedRect() && pin->drawingCanvas()->imageSize() == pixels,
                  "Selecting line must open a pin at the selected region with the line tool active");
            check(QTest::qWaitFor([&] { return pin->size() == pin->imageGeometry().size()
                                               && pin->drawingCanvas()->size() == pin->size(); }),
                  "A native pin must receive the actual selected window and canvas dimensions");
            const auto area = pin->drawingCanvas()->imageRect();
            check(qFuzzyCompare(area.width() / pixels.width(), area.height() / pixels.height()),
                  "Native selection-to-pin must preserve the source aspect ratio");
            checkUtilitySurfaces(pin);
            QTest::qWait(60);
            QDBusInterface effects("org.kde.KWin", "/Effects", "org.kde.kwin.Effects");
            if (effects.isValid())
                check(!effects.property("activeEffects").toStringList().contains("scale"),
                      "Switching from selection to line editing must not trigger KWin scale");
            pin->close();
            destroyClosed();
        }
        desktop.openSettings();
        desktop.openSettings();
        int dialogs = 0;
        for (auto* window : app.topLevelWidgets()) if (dynamic_cast<SettingsDialog*>(window) && window->isVisible()) ++dialogs;
        check(dialogs == 1, "Repeated settings actions must reuse one window");
    }
    destroyClosed();
    qInfo("Windows tray, per-tool preferences, floating pins, capture selection and lifecycle checks passed");
    return 0;
}
