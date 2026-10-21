#include "DesktopApp.h"
#include "ErrorLog.h"
#include "Settings.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
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
    parser.setApplicationDescription(QStringLiteral("ScreenCapture Linux：Wayland/X11 门户截图、托盘、选区标注与中英文 OCR。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"image", "打开已有图片进行编辑。", "file"});
    parser.addOption({"output", "直接保存 PNG 到指定路径并退出。", "file"});
    parser.addOption({"enter", "启动模式：cap、pin、ocr、qr、long、video、tray、settings。", "mode", "cap"});
    parser.addOption({"auto-start", "登录时启动并在托盘待命：true/false。", "bool", "false"});
    parser.addOption({"auto-quit", "完成操作后退出：true/false。", "bool", "false"});
    parser.process(app);
    QString mode = parser.value("enter");
    if (!QStringList{"cap", "pin", "ocr", "qr", "qrcode", "long", "video", "tray", "settings"}.contains(mode)) {
        fprintf(stderr, "Unsupported Linux entry mode: %s\n", qPrintable(mode));
        return 2;
    }
    if (parser.value("auto-start") != "true" && parser.value("auto-start") != "false") {
        fprintf(stderr, "--auto-start must be true or false\n");
        return 2;
    }
    if (parser.value("auto-start") == "true") mode = "tray";
    const QString quit = parser.value("auto-quit");
    if (quit != "true" && quit != "false") { fprintf(stderr, "--auto-quit must be true or false\n"); return 2; }
    const bool oneShot = quit == "true" || parser.isSet("output");
    if (oneShot && (mode == "tray" || mode == "settings")) {
        fprintf(stderr, "Tray and settings modes require resident operation\n");
        return 2;
    }
    const QString imagePath = parser.isSet("image") ? QFileInfo(parser.value("image")).absoluteFilePath() : QString{};
    Settings settings;
    if (!oneShot && !parser.isSet("enter") && imagePath.isEmpty() && !settings.values.startCapture) mode = "tray";
    auto bus = QDBusConnection::sessionBus();
    const QString service = "io.github.xland.ScreenCapture";
    DesktopApp desktop(settings, oneShot, parser.value("output"));
    if (!oneShot && bus.isConnected() && !bus.registerObject("/ScreenCapture", &desktop, QDBusConnection::ExportScriptableSlots)) {
        qCritical("Cannot register the ScreenCapture D-Bus object");
        return 1;
    }
    if (!oneShot && bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface existing(service, "/ScreenCapture", service, bus);
        const QDBusReply<void> reply = existing.call("activate", mode, imagePath);
        if (!reply.isValid()) {
            qCritical("Cannot contact existing ScreenCapture: %s", qPrintable(reply.error().message()));
            return 1;
        }
        return 0;
    }
    if (!oneShot && !QFileInfo::exists(settings.path())) settings.save();
    const bool initialized = desktop.initialize();
    if (!initialized && mode == "tray") mode = "settings";
    QTimer::singleShot(0, &desktop, [&] { desktop.activate(mode, imagePath); });
    return app.exec();
}
