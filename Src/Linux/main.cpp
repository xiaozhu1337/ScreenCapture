#include "CapturePortal.h"
#include "Editor.h"
#include "ErrorLog.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ScreenCapture");
    app.setOrganizationName("xland");
    app.setDesktopFileName("io.github.xland.ScreenCapture");
    app.setApplicationVersion("2.5.15-linux");
    qInstallMessageHandler(ErrorLog::message);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ScreenCapture Linux：Wayland/X11 门户截图、选区标注与中英文 OCR。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"image", "打开已有图片进行编辑。", "file"});
    parser.addOption({"output", "直接保存 PNG 到指定路径。", "file"});
    parser.addOption({"enter", "启动模式：cap、pin、ocr、tray。", "mode", "cap"});
    parser.addOption({"auto-quit", "保存后退出：true/false；复制后保留窗口以提供 Wayland 剪贴板。", "bool", "false"});
    parser.process(app);
    const QString mode = parser.value("enter");
    if (!QStringList{"cap", "pin", "ocr", "tray"}.contains(mode)) {
        fprintf(stderr, "Unsupported Linux entry mode: %s\n", qPrintable(mode));
        return 2;
    }
    const QString quit = parser.value("auto-quit");
    if (quit != "true" && quit != "false") { fprintf(stderr, "--auto-quit must be true or false\n"); return 2; }
    app.setQuitOnLastWindowClosed(mode != "tray");
    CapturePortal portal;
    QPointer<Editor> editor;
    QMenu menu;
    QSystemTrayIcon tray;
    const QIcon icon = QIcon::fromTheme("camera-photo", app.style()->standardIcon(QStyle::SP_ComputerIcon));
    app.setWindowIcon(icon);
    tray.setIcon(icon);
    tray.setToolTip("ScreenCapture");
    tray.setContextMenu(&menu);
    auto capture = [&] {
        if (QApplication::activeModalWidget() || app.modalWindow()) return;
        if (editor) editor->hide();
        QTimer::singleShot(250, &portal, &CapturePortal::capture);
    };
    auto open = [&](QImage image) {
        if (parser.isSet("output")) {
            QSaveFile file(parser.value("output"));
            const bool ok = file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
            if (!ok) {
                qCritical("Cannot save screenshot to output");
                fprintf(stderr, "Cannot save screenshot to output\n");
            }
            app.exit(ok ? 0 : 1);
            return;
        }
        const QPointer<Editor> previous = editor;
        editor = new Editor(std::move(image), quit == "true");
        editor->newCapture = capture;
        editor->show();
        if (previous) previous->close();
        if (mode == "ocr") QTimer::singleShot(0, editor, &Editor::recognize);
    };
    QObject::connect(&portal, &CapturePortal::captured, &app, open);
    QObject::connect(&portal, &CapturePortal::failed, &app, [&](const QString& reason) {
        qCritical("Screenshot portal: %s", qPrintable(reason));
        if (parser.isSet("output")) { fprintf(stderr, "%s\n", qPrintable(reason)); app.exit(1); return; }
        if (editor) editor->show();
        QMessageBox::warning(editor, QStringLiteral("截图失败"), reason);
        if (!editor && mode != "tray") app.exit(1);
    });
    QObject::connect(&portal, &CapturePortal::cancelled, &app, [&] {
        if (editor) editor->show();
        else if (mode != "tray") app.exit(0);
    });
    menu.addAction(QStringLiteral("截图"), &app, capture);
    menu.addAction(QStringLiteral("退出"), &app, &QApplication::quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) capture();
    });
    QTimer::singleShot(0, &app, [&] {
        if (parser.isSet("image")) {
            const QImage image(parser.value("image"));
            if (image.isNull()) {
                fprintf(stderr, "Cannot read image: %s\n", qPrintable(parser.value("image")));
                qCritical("Cannot read image");
                app.exit(1);
            } else open(image);
        } else if (mode != "tray") capture();
    });
    if (mode == "tray") {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            fprintf(stderr, "Desktop system tray is unavailable\n");
            return 1;
        }
        tray.show();
    }
    return app.exec();
}
