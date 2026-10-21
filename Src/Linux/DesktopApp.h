#pragma once
#include "CapturePortal.h"
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
class Editor;
class Settings;
class SettingsDialog;

class DesktopApp : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.xland.ScreenCapture")
public:
    DesktopApp(Settings& settings, bool oneShot, QString output, QObject* parent = nullptr);
    ~DesktopApp() override;
    bool initialize();
    void openSettings();
    bool hasTray() const { return tray.isVisible(); }
    QMenu* trayMenu() { return &menu; }
public slots:
    Q_SCRIPTABLE void activate(const QString& mode, const QString& imagePath);
private:
    void capture(const QString& mode);
    void openImage(QImage image);
    void applyShortcuts();
    Settings& settings;
    bool oneShot;
    QString output;
    QString captureMode;
    bool pending{false};
    CapturePortal portal;
    QMenu menu;
    QSystemTrayIcon tray;
    QPointer<Editor> editor;
    QPointer<SettingsDialog> dialog;
    QAction* captureAction;
    QAction* ocrAction;
};
