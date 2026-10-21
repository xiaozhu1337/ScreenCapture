#include "DesktopApp.h"
#include "CaptureOverlay.h"
#include "Editor.h"
#include "GlobalShortcuts.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Ui.h"
#include <QApplication>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>
#include <algorithm>

DesktopApp::DesktopApp(Settings& preferences, bool quit, QString destination, QObject* parent)
    : QObject(parent), settings(preferences), oneShot(quit), output(std::move(destination))
{
    const QIcon icon(":/logo.png");
    qApp->setWindowIcon(icon);
    tray.setIcon(icon);
    tray.setToolTip("Screen Capture");
    tray.setContextMenu(&menu);
    menu.addAction(QString(), this, &DesktopApp::openSettings)->setObjectName("settings");
    menu.addAction(QString(), qApp, &QApplication::quit)->setObjectName("quit");
    captureAction = new QAction(this);
    captureAction->setObjectName("capture");
    ocrAction = new QAction(this);
    ocrAction->setObjectName("ocr");
    connect(captureAction, &QAction::triggered, this, [this] { capture("cap"); });
    connect(ocrAction, &QAction::triggered, this, [this] { capture("ocr"); });
    connect(&tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) capture("cap");
    });
    connect(&settings, &Settings::changed, this, [this] { refreshLanguage(); applyShortcuts(); });
    connect(&portal, &CapturePortal::captured, this, &DesktopApp::captured);
    connect(&portal, &CapturePortal::failed, this, [this](const QString& reason) {
        pending = false;
        qCritical("Screenshot portal: %s", qPrintable(reason));
        if (!output.isEmpty()) { fprintf(stderr, "%s\n", qPrintable(reason)); qApp->exit(1); return; }
        QMessageBox::warning(nullptr, Ui::text("setting.title"), reason);
        if (!tray.isVisible() && editors.isEmpty()) qApp->exit(1);
    });
    connect(&portal, &CapturePortal::cancelled, this, [this] {
        pending = false;
        if (!tray.isVisible() && editors.isEmpty()) qApp->exit(0);
    });
    connect(&windowGeometry, &WindowGeometry::ready, this, [this](const QList<QRect>& windows) {
        if (overlay) return;
        // Resolve desktop windows before showing our own capture surfaces.
        portal.setProperty("captureWindows", QVariant::fromValue(windows));
        portal.capture();
    });
    refreshLanguage();
}
DesktopApp::~DesktopApp()
{
    delete dialog;
    delete overlay;
    const auto pins = editors;
    for (const auto& pin : pins) delete pin.data();
    const auto sessions = recordings;
    for (const auto& session : sessions) delete session.data();
}
bool DesktopApp::initialize()
{
    qApp->setQuitOnLastWindowClosed(false);
    if (oneShot || !output.isEmpty()) return true;
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning("Desktop system tray is unavailable; using capture-only mode");
        return false;
    }
    tray.show();
    qApp->setQuitOnLastWindowClosed(false);
    applyShortcuts();
    if (!settings.error.isEmpty()) tray.showMessage(Ui::text("setting.title"), settings.error, QSystemTrayIcon::Warning);
    return true;
}
void DesktopApp::refreshLanguage()
{
    Ui::setLanguage(settings.values.language);
    menu.actions()[0]->setText(Ui::text("tray.setting"));
    menu.actions()[1]->setText(Ui::text("tray.exit"));
    captureAction->setText(Ui::text("shortcut.cap"));
    ocrAction->setText(Ui::text("cap.ocr"));
}
void DesktopApp::applyShortcuts()
{
    if (oneShot || !output.isEmpty() || !tray.isVisible()) return;
    const QList<QPair<QAction*, QKeySequence>> bindings{{captureAction, settings.values.captureKey}, {ocrAction, settings.values.ocrKey}};
    auto* shortcuts = KGlobalAccel::self();
    bool changed = false;
    for (const auto& [action, key] : bindings) {
        const auto desired = key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key};
        changed |= shortcuts->shortcut(action) != desired;
    }
    if (!changed) return;
    for (const auto& [action, key] : bindings) shortcuts->setShortcut(action, {}, KGlobalAccel::NoAutoloading);
    for (const auto& [action, key] : bindings) {
        if (key.isEmpty()) continue;
        if (!shortcutAvailable(key)) {
            tray.showMessage(Ui::text("shortcut.cap"), Ui::text("shortcut.invalid"), QSystemTrayIcon::Warning);
            qWarning("Global shortcut is unavailable: %s", qPrintable(key.toString()));
        } else if (!shortcuts->setShortcut(action, {key}, KGlobalAccel::NoAutoloading))
            qWarning("Cannot register global shortcut: %s", qPrintable(key.toString()));
    }
}
void DesktopApp::capture(const QString& mode)
{
    if (pending || overlay || QApplication::activeModalWidget() || qApp->modalWindow()) return;
    pending = true;
    captureMode = mode;
    if (dialog) dialog->hide();
    QTimer::singleShot(150, &windowGeometry, &WindowGeometry::request);
}
void DesktopApp::captured(QImage image)
{
    pending = false;
    if (!output.isEmpty()) { openImage(std::move(image)); return; }
    overlay = new CaptureOverlay(std::move(image), settings, captureMode, oneShot || !tray.isVisible(),
                                 portal.property("captureWindows").value<QList<QRect>>());
    connect(overlay, &CaptureOverlay::actionRequested, this, &DesktopApp::dispatch);
    connect(overlay, &QObject::destroyed, this, &DesktopApp::quitIfIdle);
    overlay->showCapture();
}
void DesktopApp::openImage(QImage image, QRect geometry, const QString& tool)
{
    if (!output.isEmpty()) {
        QSaveFile file(output);
        const bool ok = file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
        if (!ok) { qCritical("Cannot save screenshot to output"); fprintf(stderr, "Cannot save screenshot to output\n"); }
        qApp->exit(ok ? 0 : 1);
        return;
    }
    auto* editor = new Editor(std::move(image), oneShot || !tray.isVisible(), &settings);
    editors.append(editor);
    connect(editor, &QObject::destroyed, this, [this] {
        editors.removeIf([](const auto& item) { return item.isNull(); });
        quitIfIdle();
    });
    editor->newCapture = [this] { capture("cap"); };
    if (geometry.isValid()) editor->place(geometry);
    if (!tool.isEmpty()) editor->selectTool(tool);
    editor->show();
}
void DesktopApp::activate(const QString& mode, const QString& imagePath)
{
    if (mode == "settings") { openSettings(); return; }
    if (!imagePath.isEmpty()) {
        if (pending || QApplication::activeModalWidget()) return;
        const QImage image(imagePath);
        if (image.isNull()) {
            qCritical("Cannot read image: %s", qPrintable(imagePath));
            if (oneShot || !output.isEmpty()) { fprintf(stderr, "Cannot read image\n"); qApp->exit(1); }
            else QMessageBox::warning(nullptr, "ScreenCapture", tr("无法读取图片：%1").arg(imagePath));
            return;
        }
        if (mode == "ocr" || mode == "qrcode" || mode == "qr") dispatch(mode, image, {});
        else openImage(image);
    } else if (mode != "tray") capture(mode);
}
void DesktopApp::quitIfIdle()
{
    QTimer::singleShot(0, this, [this] {
        if ((oneShot || !tray.isVisible()) && !pending && !overlay && editors.isEmpty() && recordings.isEmpty()
            && !qApp->property("retainClipboard").toBool()) qApp->quit();
    });
}
void DesktopApp::openSettings()
{
    if (QApplication::activeModalWidget()) return;
    if (!dialog) dialog = new SettingsDialog(settings);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
