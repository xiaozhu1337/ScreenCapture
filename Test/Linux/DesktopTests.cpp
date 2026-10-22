#include "Canvas.h"
#include "CaptureOverlay.h"
#include "CapturePortal.h"
#include "DesktopApp.h"
#include "Editor.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Ui.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

static void check(bool value, const char* message) { if (!value) qFatal("%s", message); }
static void destroyClosed() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

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
    QPointer<Editor> editor = new Editor(image, false, &restored);
    editor->show();
    app.processEvents();
    check(editor->drawingCanvas()->selectedTool() == Canvas::Tool::Select, "A fresh pin must have no drawing tool selected");
    editor->selectTool("text");
    check(editor->drawingCanvas()->style() == restored.values.toolPin["text"], "Selecting a tool must restore its own style");
    auto* slider = editor->findChild<QSlider*>("toolSize");
    check(slider && slider->minimum() == 10 && slider->maximum() == 60, "Text size range must match Windows");
    slider->setValue(44);
    check(Settings(temporary.filePath("application")).values.toolPin["text"].size == 44, "Changing a tool size must persist immediately");
    check(restored.values.toolPin["rect"].size == 2, "Changing text style must not change other tools");
    editor->selectTool("eraser");
    check(editor->findChild<QToolButton*>("fill"), "Eraser must offer rectangular mode");
    editor->close();
    destroyClosed();
    check(editor.isNull(), "Closing a pin must release all its owned windows");

    auto* capture = new CaptureOverlay(image, restored, "cap", false);
    capture->showCapture();
    app.processEvents();
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
        check(screenshot.rect().contains(pixel) && screenshot.pixelColor(pixel).lightness() > 230,
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
    QTest::keyClick(capture, Qt::Key_F3);
    check(pins.size() == 1 && pins[0][0].toString() == "pin", "F3 must pin the current selection");
    check(pins[0][1].value<QImage>() == image.copy(pixels), "Pinning must preserve the exact selected source pixels");
    destroyClosed();

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
