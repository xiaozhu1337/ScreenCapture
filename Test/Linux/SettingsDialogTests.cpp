#include "Settings.h"
#include "SettingsDialog.h"
#include "Ui.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QWindow>

class LinkHandler : public QObject
{
    Q_OBJECT
public:
    QUrl opened;
public slots:
    void open(const QUrl& url) { opened = url; }
};

static void check(bool value, const char* message) { if (!value) qFatal("%s", message); }

static QColor pixelColor(const QImage& image, int x, int y)
{
    return image.pixelColor(qRound(x * image.devicePixelRatio()), qRound(y * image.devicePixelRatio()));
}

static void click(SettingsDialog* dialog, const QString& name)
{
    auto* button = dialog->findChild<QPushButton*>(name);
    check(button && button->isEnabled(), qPrintable("Missing or disabled button: " + name));
    button->click();
}

static void dismiss(QMessageBox::StandardButton answer = QMessageBox::Ok)
{
    QTimer::singleShot(0, [answer] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        check(message, "Expected a validation or persistence message");
        auto* button = message->button(answer);
        check(button, "Expected a matching message-box button");
        button->click();
    });
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ScreenCaptureSettingsTests");
    app.setApplicationVersion("1.2.3");
    app.setQuitOnLastWindowClosed(false);
    QDir().mkpath("__tmp");
    QTemporaryDir temporary(QDir::current().absoluteFilePath("__tmp/settings-dialog-XXXXXX"));
    check(temporary.isValid(), "Cannot prepare settings tests");
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    Settings settings(temporary.filePath("application"));
    settings.values.captureKey = {};
    settings.values.ocrKey = {};
    check(settings.save(), "Cannot prepare persisted settings");
    const auto styles = settings.values.toolPin;
    QSignalSpy changes(&settings, &Settings::changed);
    QPointer<SettingsDialog> dialog = new SettingsDialog(settings);
    dialog->show();
    app.processEvents();
    auto* tabs = dialog->findChild<QTabWidget*>();
    check(tabs && tabs->count() == 4, "Settings must contain four Windows groups");
    check(dialog->size() == QSize(680, 560) && dialog->windowFlags().testFlag(Qt::FramelessWindowHint),
          qPrintable(QString("Settings must use a 680x560 frameless window (actual %1x%2, minimum %3x%4)")
              .arg(dialog->width()).arg(dialog->height()).arg(dialog->minimumWidth()).arg(dialog->minimumHeight())));
    check(dialog->windowHandle() && !tabs->tabBar()->isVisible(), "Settings must use a native window and hide the tab bar");
    auto* sidebar = dialog->findChild<QWidget*>("settingsSidebar");
    check(sidebar && sidebar->geometry() == QRect(0, 0, 160, 560), "The sidebar must span the left 160 pixels");
    const QStringList groups{"setting.common", "setting.shortcut", "setting.toolbar", "setting.about"};
    for (int index = 0; index < groups.size(); ++index) {
        auto* item = dialog->findChild<QPushButton*>(groups[index]);
        check(item && item->geometry() == QRect(0, 40 + index * 40, 160, 40)
              && item->font().pixelSize() == 14, "Sidebar menu geometry must match Windows");
        QTest::mouseClick(item, Qt::LeftButton);
        check(tabs->currentIndex() == index && item->isChecked(), "Sidebar clicks must switch the existing pages");
        check(tabs->currentWidget()->layout()->contentsMargins() == QMargins(20, 40, 20, 40),
              "Every page must preserve Windows content padding");
    }
    tabs->setCurrentIndex(0);
    check(dialog->findChild<QPushButton*>(groups[0])->isChecked(), "Programmatic page changes must synchronize the sidebar");
    app.processEvents();
    auto colors = dialog->grab().toImage();
    check(pixelColor(colors, 300, 10) == QColor("#fafafa") && pixelColor(colors, 10, 10) == QColor("#eeeef0")
          && pixelColor(colors, 5, 45) == QColor("#597ef7"), "Window, sidebar and selection colors must match Windows");
    auto* close = dialog->findChild<QPushButton*>("settingsClose");
    check(close && close->geometry() == QRect(638, 0, 42, 32), "Close button must occupy the top-right 42x32 pixels");
    QTest::mouseMove(close);
    app.processEvents();
    check(pixelColor(close->grab().toImage(), 2, 2) == QColor("#e81123"), "Close hover must be Windows red");
    QTest::mouseMove(sidebar, QPoint(10, 10));
    dialog->resize(720, 600);
    app.processEvents();
    check(close->pos() == QPoint(678, 0), "Close button must remain anchored when the window resizes");
    dialog->resize(680, 560);
    app.processEvents();
    check(dialog->findChildren<QKeySequenceEdit*>().size() == 2 && !dialog->findChild<QKeySequenceEdit*>("ocrKey"),
          "Only the two Windows shortcut fields may be visible");
    check(!dialog->findChild<QLabel*>("linuxShortcuts"), "Linux implementation details must not appear in settings");
    check(!dialog->findChild<QPushButton*>("setting.getMoreLang"), "More languages must not occupy an extra settings row");
    const auto separators = tabs->widget(0)->findChildren<QFrame*>("separator");
    check(separators.size() == 3, "General settings must contain three separated Windows rows");
    for (auto* separator : separators) check(separator->height() == 1, "Settings row separators must be one pixel high");
    check(dialog->findChildren<QDialogButtonBox*>().isEmpty(), "Settings must save without apply or cancel buttons");
    auto* menu = dialog->findChild<QTableWidget*>("captureToolbar");
    check(menu && menu->rowCount() == 13, "All 13 actions must be configurable");
    check(!menu->showGrid() && menu->rowHeight(0) == 26 && menu->columnWidth(1) == 44
          && menu->columnWidth(2) == 44 && menu->columnWidth(3) == 64, "Toolbar rows and action columns must match Windows");
    auto persisted = [&] { return Settings(temporary.filePath("application")).values; };
    auto* bottom = dialog->findChild<QCheckBox*>("toolbarBottom");
    check(bottom->size() == QSize(60, 39) && bottom->parentWidget()->height() == 39,
          "Toggle buttons must occupy the full Windows 60x39 click area");
    check(bottom->mapTo(dialog, QPoint(0, 0)) == QPoint(600, 80), "General rows must start at x180,y40 with 40-pixel steps");
    QTest::mouseClick(bottom, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    check(changes.count() == 1 && persisted().toolbarBottom, "A toggle must persist immediately while the dialog stays open");
    check(dialog->isVisible(), "Immediate save must keep settings open");
    auto* startup = dialog->findChild<QCheckBox*>("autoStart");
    startup->setChecked(true);
    check(settings.autoStart(), "Autostart must write a desktop entry immediately");
    startup->setChecked(false);
    check(!settings.autoStart(), "Autostart must be disabled immediately");

    auto* up = dialog->findChild<QPushButton*>("up-mark");
    check(up && !up->isEnabled(), "The first enabled action cannot move up");
    check(!dialog->findChild<QPushButton*>("down-eraser")->isEnabled(), "Disabled tools cannot move");
    click(dialog, "toggle-rect");
    check(persisted().captureToolbar == QStringList{"mark", "long", "video", "ocr", "qrcode", "rect"},
          "Adding a drawing action must append and persist it");
    click(dialog, "up-rect");
    check(persisted().captureToolbar == QStringList{"mark", "long", "video", "ocr", "rect", "qrcode"},
          "Moving an enabled drawing action must persist its order");
    click(dialog, "down-rect");
    click(dialog, "toggle-ocr");
    check(!persisted().captureToolbar.contains("ocr"), "OCR must be removable like every configurable action");
    const auto enabled = persisted().captureToolbar;
    for (const auto& id : enabled) click(dialog, "toggle-" + id);
    check(persisted().captureToolbar.isEmpty(), "Every first-level action may be hidden");
    for (const auto& id : Ui::captureTools()) click(dialog, "toggle-" + id);
    check(persisted().captureToolbar == Ui::captureTools(), "Every registered tool may be enabled");
    click(dialog, "toolbar.reset");
    check(persisted().captureToolbar == Preferences{}.captureToolbar, "Reset must restore only the five default capture actions");
    check(persisted().toolPin == styles, "Menu configuration must preserve per-tool styles");

    auto* language = dialog->findChild<QComboBox*>("language");
    check(language->size() == QSize(160, 28) && language->count() == Ui::languages().size() + 1
          && language->itemData(language->count() - 1).toString() == "setting.getMoreLang",
          "Language selection must be 160x28 with the more-languages entry last");
    language->setCurrentIndex(language->findData("en-US"));
    check(persisted().language == "en-US", "Language must persist immediately");
    check(dialog->windowTitle() == "ScreenCapture Settings" && tabs->tabText(0) == "General"
          && dialog->findChild<QPushButton*>("setting.common")->text() == "General",
          "The open settings window and sidebar must refresh their language");
    LinkHandler links;
    QDesktopServices::setUrlHandler("https", &links, "open");
    const int beforeMoreLanguages = changes.count();
    language->setCurrentIndex(language->count() - 1);
    check(links.opened == QUrl("https://github.com/xland/ScreenCapture/tree/main/Lang")
          && language->currentData().toString() == "en-US" && persisted().language == "en-US"
          && changes.count() == beforeMoreLanguages, "More languages must open the existing URL without saving a pseudo-language");
    check(language->itemText(language->count() - 1) == Ui::text("setting.getMoreLang"),
          "The more-languages item must also be translated");
    QDesktopServices::unsetUrlHandler("https");
    check(menu->item(0, 0)->text() == "1. Annotate Image", "Menu labels must use the shared language catalog");
    dismiss();
    click(dialog, "about.wechat");
    check(QApplication::clipboard()->text() == "liulun_007", "Author action must match Windows clipboard behavior");

    auto* pin = dialog->findChild<QKeySequenceEdit*>("pinKey");
    check(pin->size() == QSize(160, 28) && dialog->findChild<QPushButton*>("pinKeyClear")->size() == QSize(56, 28)
          && !dialog->findChild<QPushButton*>("captureKeyClear"), "Shortcut controls must match the Windows layout");
    pin->setKeySequence(QKeySequence("F8"));
    QMetaObject::invokeMethod(pin, "editingFinished", Qt::DirectConnection);
    check(persisted().pinKey == QKeySequence("F8"), "A completed shortcut edit must persist immediately");
    const int beforeInvalid = changes.count();
    pin->setKeySequence(QKeySequence("Ctrl+H"));
    dismiss();
    QMetaObject::invokeMethod(pin, "editingFinished", Qt::DirectConnection);
    check(changes.count() == beforeInvalid && pin->keySequence() == QKeySequence("F8")
          && persisted().pinKey == QKeySequence("F8"), "Conflicting editor shortcuts must revert without writing");
    auto* capture = dialog->findChild<QKeySequenceEdit*>("captureKey");
    capture->setKeySequence(QKeySequence("Ctrl+C"));
    dismiss();
    QMetaObject::invokeMethod(capture, "editingFinished", Qt::DirectConnection);
    check(capture->keySequence().isEmpty() && persisted().captureKey.isEmpty(), "Reserved global keys must not be saved");
    click(dialog, "pinKeyClear");
    check(persisted().pinKey.isEmpty(), "Clearing a shortcut must persist its disabled state");
    click(dialog, "settingsClose");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(dialog.isNull(), "Closing the dialog must release it");

    QFile blocker(temporary.filePath("blocked"));
    check(blocker.open(QIODevice::WriteOnly), "Cannot prepare failed-save fixture");
    blocker.close();
    Settings blocked(blocker.fileName());
    blocked.values.captureKey = {};
    QPointer<SettingsDialog> failing = new SettingsDialog(blocked);
    auto* failingBottom = failing->findChild<QCheckBox*>("toolbarBottom");
    QSignalSpy failedChanges(&blocked, &Settings::changed);
    dismiss();
    failingBottom->setChecked(true);
    check(!failingBottom->isChecked() && !blocked.values.toolbarBottom && failedChanges.isEmpty(),
          "Failed persistence must restore the model and control without emitting changed");
    failing->show();
    app.processEvents();
    QTest::keyClick(failing, Qt::Key_Escape);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(failing.isNull(), "Escape must close and release the frameless dialog");

    QFile corrupt(settings.path());
    check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot prepare corrupt YAML");
    const QByteArray invalidYaml("line_width: [broken\n");
    corrupt.write(invalidYaml);
    corrupt.close();
    Settings invalid(temporary.filePath("application"));
    invalid.values.captureKey = {};
    check(!invalid.error.isEmpty(), "Corrupt YAML fixture must fail to load");
    QPointer<SettingsDialog> recovery = new SettingsDialog(invalid);
    auto* recoveryBottom = recovery->findChild<QCheckBox*>("toolbarBottom");
    dismiss(QMessageBox::No);
    recoveryBottom->setChecked(true);
    check(!recoveryBottom->isChecked() && corrupt.open(QIODevice::ReadOnly), "Declining recovery must revert the control");
    check(corrupt.readAll() == invalidYaml, "Declining recovery must preserve the corrupt file");
    corrupt.close();
    dismiss(QMessageBox::Yes);
    recoveryBottom->setChecked(true);
    check(persisted().toolbarBottom && Settings(temporary.filePath("application")).error.isEmpty(),
          "Explicit recovery must persist valid YAML");
    recovery->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (const auto& name : {"captureKey", "pinKey"}) {
        QPointer<SettingsDialog> escape = new SettingsDialog(settings);
        escape->findChild<QTabWidget*>()->setCurrentIndex(1);
        escape->show();
        escape->activateWindow();
        auto* edit = escape->findChild<QKeySequenceEdit*>(name);
        edit->setFocus();
        app.processEvents();
        const auto previous = persisted();
        const int beforeEscape = changes.count();
        edit->setKeySequence(QKeySequence("F9"));
        QTest::keyClick(edit->focusProxy() ? edit->focusProxy() : edit, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(escape.isNull() && changes.count() == beforeEscape
              && persisted().captureKey == previous.captureKey && persisted().pinKey == previous.pinKey,
              "Escape in either shortcut editor must close without saving unfinished input");
    }
    qInfo("Settings dialog: Windows layout, sidebar, language links, immediate persistence, shortcuts and rollback passed");
    return 0;
}

#include "SettingsDialogTests.moc"
