#include "Canvas.h"
#include "DesktopApp.h"
#include "Editor.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "GlobalShortcuts.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

static void check(bool value, const char* message) { if (!value) qFatal("%s", message); }

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ScreenCaptureTests");
    app.setOrganizationName("xland");
    QDir().mkpath("__tmp");
    QTemporaryDir temporary(QDir::current().absoluteFilePath("__tmp/settings-test-XXXXXX"));
    check(temporary.isValid(), "Cannot prepare settings tests");
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    Settings settings(temporary.filePath("application"));
    check(settings.values.captureKey == QKeySequence("Ctrl+Alt+A") && settings.values.pinKey == QKeySequence("F3"), "Default shortcuts changed");
    settings.values.captureKey = {};
    settings.values.color = QColor("#123456");
    settings.values.lineWidth = 9;
    settings.values.toolbarBottom = true;
    settings.values.startCapture = false;
    settings.values.tools = {"text", "pen"};
    settings.values.saveDirectory = temporary.filePath("图片目录");
    check(settings.save(), "Cannot save settings");
    Settings restored(temporary.filePath("application"));
    check(restored.error.isEmpty() && restored.values.color == QColor("#123456") && restored.values.lineWidth == 9,
          "Annotation preferences must survive restart");
    check(!restored.values.startCapture && restored.values.toolbarBottom && restored.values.tools == QStringList{"text", "pen"}
          && restored.values.saveDirectory == settings.values.saveDirectory && restored.values.captureKey.isEmpty(),
          "Toolbar, startup, save directory and cleared shortcuts must survive restart");
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPointer<Editor> editor = new Editor(image, false, &restored);
    editor->show();
    app.processEvents();
    check(dynamic_cast<Canvas*>(editor->centralWidget())->color == QColor("#123456"), "Editor must apply saved color");
    const auto toolbars = editor->findChildren<QToolBar*>();
    check(toolbars.size() == 2 && editor->toolBarArea(toolbars.first()) == Qt::BottomToolBarArea, "Editor must place toolbars at bottom");
    QStringList visible;
    for (auto* action : toolbars.first()->actions()) if (action->data().isValid() && action->isVisible()) visible.append(action->data().toString());
    check(visible == QStringList{"select", "text", "pen"}, "Editor must honor enabled tools and their order");
    auto* width = editor->findChild<QSpinBox*>();
    width->setValue(11);
    Settings changed(temporary.filePath("application"));
    check(changed.values.lineWidth == 11, "Changing editor line width must persist");
    restored.values.toolbarBottom = false;
    restored.values.tools = {"rectangle"};
    check(restored.save(), "Cannot apply changed settings");
    check(editor->toolBarArea(toolbars.first()) == Qt::TopToolBarArea, "Settings changes must update an open editor");
    editor->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(editor.isNull(), "Closing an editor must release it");

    Preferences unsafe;
    unsafe.captureKey = QKeySequence("Ctrl+C");
    check(!Settings::validate(unsafe).isEmpty(), "Reserved editing keys must not become global shortcuts");
    unsafe.captureKey = QKeySequence("A");
    check(!Settings::validate(unsafe).isEmpty(), "Unmodified letters must not become global shortcuts");
    unsafe.captureKey = QKeySequence("F3");
    check(!Settings::validate(unsafe).isEmpty(), "Global and editor pin shortcuts must not overlap");
    QPointer<Editor> single = new Editor(image, true, &restored);
    single->findChild<QSpinBox*>()->setValue(27);
    check(Settings(temporary.filePath("application")).values.lineWidth == 11, "One-shot editors must not overwrite resident preferences");
    single->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(single.isNull(), "One-shot editor must be released");

    QPointer<SettingsDialog> dialog = new SettingsDialog(restored);
    check(dialog->findChild<QTabWidget*>()->count() == 4, "Settings must contain all four pages");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    dialog->findChild<QSpinBox*>()->setValue(15);
    buttons->button(QDialogButtonBox::Save)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(restored.values.lineWidth == 15, "Settings save button must apply changes");
    check(restored.setAutoStart(true) && restored.autoStart(), "Autostart must enable a real desktop entry");
    check(restored.setAutoStart(false) && !restored.autoStart(), "Autostart must be disabled without leaving an active entry");
    const QString startup = temporary.filePath("autostart/io.github.xland.ScreenCapture.desktop");
    check(QFile::remove(startup), "Cannot clean test autostart entry");
    check(QDir().rmdir(temporary.filePath("autostart")), "Cannot clean test autostart directory");

    QFile corrupt(restored.path());
    check(corrupt.open(QIODevice::WriteOnly), "Cannot create corrupt settings");
    corrupt.write("line_width: [broken\n");
    corrupt.close();
    Settings invalid(temporary.filePath("application"));
    check(!invalid.error.isEmpty() && !invalid.save(), "Invalid settings must not be silently overwritten");
    check(invalid.save(true), "Explicit settings replacement must recover from invalid YAML");
    check(Settings(temporary.filePath("application")).error.isEmpty(), "Recovered YAML must be readable");

    const bool available = QSystemTrayIcon::isSystemTrayAvailable();
    if (available) {
        QAction action;
        action.setObjectName("capture");
        action.setText("ScreenCapture temporary shortcut check");
        const QKeySequence key("Ctrl+Alt+Shift+F12");
        check(KGlobalAccel::isGlobalShortcutAvailable(key), "Native shortcut fixture must be unoccupied");
        check(KGlobalAccel::self()->setShortcut(&action, {key}, KGlobalAccel::NoAutoloading), "Cannot register native shortcut fixture");
        check(shortcutAvailable(key), "An app must accept its own registered shortcut");
        restored.values.captureKey = key;
        auto* ownKeyDialog = new SettingsDialog(restored);
        ownKeyDialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        check(ownKeyDialog->result() == QDialog::Accepted, "Saving unchanged registered shortcuts must succeed");
        KGlobalAccel::self()->removeAllShortcuts(&action);
        restored.values.captureKey = {};
        check(restored.save(), "Cannot restore empty test shortcut");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    {
        DesktopApp desktop(restored, false, {});
        const bool initialized = desktop.initialize();
        check(initialized == available, "Resident initialization must reflect tray availability");
        check(desktop.trayMenu()->actions().size() == 8, "Tray must provide capture, OCR, image, settings, about and quit");
        if (available) check(desktop.hasTray() && !app.quitOnLastWindowClosed(), "Resident app must keep its tray after last window closes");
        desktop.openSettings();
        desktop.openSettings();
        int count = 0;
        for (auto* window : app.topLevelWidgets()) if (dynamic_cast<SettingsDialog*>(window) && window->isVisible()) ++count;
        check(count == 1, "Repeated settings actions must reuse one window");
    }
    qInfo("YAML, recovery, editor preferences, settings pages, autostart and %s tray checks passed", available ? "native desktop" : "offscreen fallback");
    return 0;
}
