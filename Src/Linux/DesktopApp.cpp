#include "DesktopApp.h"
#include "Editor.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "GlobalShortcuts.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>

DesktopApp::DesktopApp(Settings& preferences, bool quit, QString destination, QObject* parent)
    : QObject(parent), settings(preferences), oneShot(quit), output(std::move(destination))
{
    const QIcon icon = QIcon::fromTheme("camera-photo", qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    qApp->setWindowIcon(icon);
    tray.setIcon(icon);
    tray.setToolTip("ScreenCapture");
    tray.setContextMenu(&menu);
    captureAction = menu.addAction(tr("截图"), this, [this] { capture("cap"); });
    captureAction->setObjectName("capture");
    ocrAction = menu.addAction(tr("截图并识别文字 OCR"), this, [this] { capture("ocr"); });
    ocrAction->setObjectName("ocr");
    menu.addAction(tr("打开图片"), this, [this] {
        if (QApplication::activeModalWidget()) return;
        const QString path = QFileDialog::getOpenFileName(nullptr, tr("打开图片"), settings.values.saveDirectory,
            tr("图片 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));
        if (!path.isEmpty()) activate("pin", path);
    });
    menu.addSeparator();
    menu.addAction(tr("设置"), this, &DesktopApp::openSettings);
    menu.addAction(tr("关于"), this, [this] { openSettings(); if (dialog) dialog->findChild<QTabWidget*>()->setCurrentIndex(3); });
    menu.addSeparator();
    menu.addAction(tr("退出"), qApp, &QApplication::quit);
    connect(&tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) capture("cap");
    });
    connect(&settings, &Settings::changed, this, &DesktopApp::applyShortcuts);
    connect(&portal, &CapturePortal::captured, this, [this](QImage image) {
        pending = false;
        openImage(std::move(image));
    });
    connect(&portal, &CapturePortal::failed, this, [this](const QString& reason) {
        pending = false;
        qCritical("Screenshot portal: %s", qPrintable(reason));
        if (!output.isEmpty()) { fprintf(stderr, "%s\n", qPrintable(reason)); qApp->exit(1); return; }
        if (editor) editor->show();
        QMessageBox::warning(editor, tr("截图失败"), reason);
        if (!editor && !tray.isVisible()) qApp->exit(1);
    });
    connect(&portal, &CapturePortal::cancelled, this, [this] {
        pending = false;
        if (editor) editor->show();
        else if (!tray.isVisible()) qApp->exit(0);
    });
}

DesktopApp::~DesktopApp()
{
    delete dialog;
    delete editor;
}

bool DesktopApp::initialize()
{
    if (oneShot || !output.isEmpty()) return true;
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning("Desktop system tray is unavailable; using editor-only mode");
        return false;
    }
    tray.show();
    qApp->setQuitOnLastWindowClosed(false);
    applyShortcuts();
    if (!settings.error.isEmpty()) tray.showMessage(tr("配置读取失败"), settings.error, QSystemTrayIcon::Warning);
    return true;
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
    // Release both actions first so their shortcuts can be swapped without conflicts.
    for (const auto& [action, key] : bindings) shortcuts->setShortcut(action, {}, KGlobalAccel::NoAutoloading);
    for (const auto& [action, key] : bindings) {
        if (key.isEmpty()) continue;
        if (!shortcutAvailable(key)) {
            tray.showMessage(tr("快捷键被占用"), tr("无法注册 %1，请在设置中更改。").arg(key.toString()), QSystemTrayIcon::Warning);
            qWarning("Global shortcut is unavailable: %s", qPrintable(key.toString()));
            continue;
        }
        if (!shortcuts->setShortcut(action, {key}, KGlobalAccel::NoAutoloading))
            qWarning("Cannot register global shortcut: %s", qPrintable(key.toString()));
    }
}

void DesktopApp::capture(const QString& mode)
{
    if (pending || QApplication::activeModalWidget() || qApp->modalWindow()) return;
    pending = true;
    captureMode = mode;
    if (editor) editor->hide();
    if (dialog) dialog->hide();
    QTimer::singleShot(250, &portal, &CapturePortal::capture);
}

void DesktopApp::openImage(QImage image)
{
    if (!output.isEmpty()) {
        QSaveFile file(output);
        const bool ok = file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
        if (!ok) {
            qCritical("Cannot save screenshot to output");
            fprintf(stderr, "Cannot save screenshot to output\n");
        }
        qApp->exit(ok ? 0 : 1);
        return;
    }
    const QPointer<Editor> previous = editor;
    editor = new Editor(std::move(image), oneShot, &settings);
    editor->newCapture = [this] { capture("cap"); };
    editor->show();
    if (previous) previous->close();
    if (captureMode == "ocr") QTimer::singleShot(0, editor, &Editor::recognize);
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
            else QMessageBox::warning(editor, tr("打开图片失败"), tr("无法读取图片：%1").arg(imagePath));
            return;
        }
        captureMode = mode;
        openImage(image);
    } else if (mode != "tray") capture(mode);
}

void DesktopApp::openSettings()
{
    if (QApplication::activeModalWidget()) return;
    if (!dialog) dialog = new SettingsDialog(settings);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
