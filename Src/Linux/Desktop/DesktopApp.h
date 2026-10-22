#pragma once
#include "CapturePortal.h"
#include "WindowGeometry.h"
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
class CaptureOverlay;
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
    void captured(QImage image);
    void dispatch(const QString& id, QImage image, QRectF geometry);
    void openImage(QImage image, QRectF geometry = {}, const QString& tool = {});
    void applyShortcuts();
    void refreshLanguage();
    void quitIfIdle();
    Settings& settings;
    bool oneShot;
    QString output, captureMode;
    bool pending{false};
    CapturePortal portal;
    WindowGeometry windowGeometry;
    QMenu menu;
    QSystemTrayIcon tray;
    QList<QPointer<Editor>> editors;
    QList<QPointer<QWidget>> recordings;
    QPointer<CaptureOverlay> overlay;
    QPointer<SettingsDialog> dialog;
    QAction* captureAction;
    QAction* ocrAction;
};
