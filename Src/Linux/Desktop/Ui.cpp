#include "Ui.h"
#include "Tool/ToolRegistry.h"
#include <LayerShellQt/Window>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QSaveFile>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#include <optional>

namespace {
QJsonObject catalog, fallback;
QString languageCode;
QJsonObject readCatalog(const QString& code)
{
    const QString directory = QCoreApplication::applicationDirPath() + "/Lang";
    for (const auto& name : QDir(directory).entryList({"*." + code + ".json"}, QDir::Files, QDir::Name)) {
        QFile external(QDir(directory).filePath(name));
        if (external.size() > 1024 * 1024 || !external.open(QIODevice::ReadOnly)) continue;
        const auto object = QJsonDocument::fromJson(external.readAll()).object();
        if (!object.isEmpty()) return object;
    }
    QFile file(":/languages/" + code + ".json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString lookup(QJsonObject object, const QString& key)
{
    const auto parts = key.split('.');
    QJsonValue value(object);
    for (const auto& part : parts) value = value.toObject().value(part);
    return value.toString();
}
}

QString Ui::cmyk(const QColor& color)
{
    const double black = 1 - qMax(color.redF(), qMax(color.greenF(), color.blueF()));
    const auto channel = [black](double value) { return black == 1 ? 0 : qRound((1 - value - black) / (1 - black) * 100); };
    return QString("%1,%2,%3,%4").arg(channel(color.redF())).arg(channel(color.greenF())).arg(channel(color.blueF())).arg(qRound(black * 100));
}
QString Ui::text(const QString& key)
{
    if (fallback.isEmpty()) setLanguage("zh-CN");
    QString value = lookup(catalog, key);
    if (value.isEmpty()) value = lookup(fallback, key);
    return value.isEmpty() ? key : value;
}

void Ui::setLanguage(const QString& language)
{
    fallback = readCatalog("zh-CN");
    languageCode = language;
    catalog = readCatalog(language);
}
QStringList Ui::languages()
{
    QStringList result{"zh-CN", "zh-TW", "en-US", "ja-JP", "ru-RU", "id-ID"};
    for (const auto& name : QDir(QCoreApplication::applicationDirPath() + "/Lang").entryList({"*.json"}, QDir::Files, QDir::Name)) {
        const QString code = QFileInfo(name).completeBaseName().section('.', -1);
        if (!code.isEmpty() && !result.contains(code)) result.append(code);
    }
    return result;
}
QStringList Ui::captureTools()
{
    QStringList tools;
    for (const auto& tool : captureToolDefinitions()) tools.append(QString::fromStdWString(tool.id));
    return tools;
}
QStringList Ui::drawingTools()
{
    QStringList tools;
    for (const auto& tool : captureToolDefinitions()) if (tool.drawing) tools.append(QString::fromStdWString(tool.id));
    return tools;
}
Ui::ToolSize Ui::toolSize(const QString& tool)
{
    if (tool == "rect" || tool == "ellipse") return {1, 26, 2};
    if (tool == "arrow") return {1, 16, 3};
    if (tool == "number") return {6, 86, 16};
    if (tool == "line") return {1, 60, 12};
    if (tool == "text") return {10, 60, 20};
    return {18, 68, 28};
}
QIcon Ui::icon(const QString& id)
{
    static const QString family = [] {
        const int font = QFontDatabase::addApplicationFont(":/iconfont.ttf");
        const auto names = QFontDatabase::applicationFontFamilies(font);
        return names.isEmpty() ? QString() : names.first();
    }();
    QString glyph;
    if (const auto* definition = findCaptureTool(id.toStdWString())) glyph = QString::fromStdWString(definition->icon);
    else if (id == "close") glyph = QChar(0xe62d);
    else if (id == "save") glyph = QChar(0xe608);
    else if (id == "clipboard") glyph = QChar(0xe6ad);
    else if (id == "undo") glyph = QChar(0xed85);
    else if (id == "redo") glyph = QChar(0xed8a);
    else if (id == "fill" || id == "fill-rect") glyph = QChar(0xe602);
    else if (id == "fill-ellipse") glyph = QChar(0xe600);
    else if (id == "fill-arrow") glyph = QChar(0xe604);
    else if (id == "fill-number") glyph = QChar(0xe605);
    else if (id == "transparent") glyph = QChar(0xe607);
    else if (id == "bold") glyph = QChar(0xe634);
    else if (id == "italic") glyph = QChar(0xe682);
    else if (id == "speaker") glyph = QChar(0xe654);
    else if (id == "microphone") glyph = QChar(0xe73b);
    else if (id == "start") glyph = QChar(0xe660);
    else if (id == "stop") glyph = QChar(0xe662);
    else if (id == "toggle-off") glyph = QChar(0xe687);
    else if (id == "toggle-on") glyph = QChar(0xe688);
    QPixmap pixels(40, 40);
    pixels.setDevicePixelRatio(2);
    pixels.fill(Qt::transparent);
    QPainter painter(&pixels);
    painter.setPen(QColor("#333333"));
    QFont font(family);
    font.setPixelSize(15);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 20, 20), Qt::AlignCenter, glyph);
    return QIcon(pixels);
}
QToolButton* Ui::button(QWidget* parent, const QString& id)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(id);
    button->setIcon(icon(id));
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(32, 32);
    QString key = "tool." + id;
    if (const auto* tool = findCaptureTool(id.toStdWString())) key = QString::fromStdWString(tool->label);
    button->setToolTip(text(key));
    button->setAccessibleName(text(key));
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}
void Ui::stylePanel(QWidget* panel)
{
    panel->setObjectName("floatingPanel");
    panel->setStyleSheet("#floatingPanel { background: white; border: 1px solid #a8a8a8; }"
        "QToolButton { border: none; border-radius: 0; background: transparent; color: #333333; padding: 0; }"
        "QToolButton:hover { background: #f2f2f2; } QToolButton:checked { background: #e6f4ff; }"
        "QLabel { color: #333333; background: transparent; }");
}
QPoint Ui::toolbarPosition(QRect region, QRect work, QSize size, int requiredHeight, bool bottom, int inset)
{
    int x = region.right() + 1 - size.width(), y;
    if (region.bottom() + 6 + requiredHeight <= work.bottom()) y = region.bottom() + 6;
    else if (!bottom && region.top() - 5 - requiredHeight >= work.top()) y = region.top() - 5 - size.height();
    else { x -= inset; y = region.bottom() + 1 - size.height() - inset; }
    return {qBound(work.left(), x, qMax(work.left(), work.right() + 1 - size.width())),
            qBound(work.top(), y, qMax(work.top(), work.bottom() + 1 - size.height()))};
}
void Ui::positionWindow(QWidget* window, QRect geometry, bool keyboard)
{
    window->setGeometry(geometry);
    if (QGuiApplication::platformName() != "wayland") return;
    window->winId();
    QScreen* screen = QGuiApplication::screenAt(geometry.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    auto* layer = LayerShellQt::Window::get(window->windowHandle());
    // KWin must not animate capture, pin or floating control surfaces as normal windows.
    layer->setScope("utility");
    layer->setScreen(screen);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) | LayerShellQt::Window::AnchorLeft);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(-1);
    layer->setDesiredSize(geometry.size());
    layer->setMargins(QMargins(geometry.x() - screen->geometry().x(), geometry.y() - screen->geometry().y(), 0, 0));
    layer->setKeyboardInteractivity(keyboard ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                           : LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(keyboard);
}
bool Ui::saveImage(QWidget* parent, const QImage& image, QString& directory)
{
    QString filter;
    const QString path = QFileDialog::getSaveFileName(parent, text("tool.save"),
        QDir(directory).filePath(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz") + ".png"),
        "PNG (*.png)", &filter);
    if (path.isEmpty()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        qCritical("Cannot save screenshot: %s", qPrintable(path));
        QMessageBox::warning(parent, text("tool.save"), QObject::tr("无法保存图片，请检查目标路径和可用空间。"));
        return false;
    }
    directory = QFileInfo(path).absolutePath();
    return true;
}
namespace {
void retainClipboard(QByteArray data, const QString& type, bool autoQuit)
{
    struct Request { QByteArray data; QString type; bool autoQuit; };
    static QProcess* active = nullptr;
    static std::optional<Request> pending;
    if (autoQuit) {
        qApp->setQuitOnLastWindowClosed(false);
        qApp->setProperty("retainClipboard", true);
    }
    const auto fallback = [autoQuit] {
        if (autoQuit) QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, qApp, [] {
            if (!QApplication::clipboard()->ownsClipboard()) qApp->quit();
        });
    };
    if (QGuiApplication::platformName() != "wayland") { fallback(); return; }
    if (active) { pending = Request{std::move(data), type, autoQuit}; return; }
    const QString helper = QStandardPaths::findExecutable("wl-copy");
    if (helper.isEmpty()) { fallback(); return; }
    // Serialize ownership changes so a slow older copy cannot overwrite the latest request.
    auto* process = new QProcess(qApp);
    active = process;
    const auto completed = [process, autoQuit, fallback](bool success) {
        active = nullptr;
        process->deleteLater();
        if (pending) {
            auto next = std::move(*pending);
            pending.reset();
            retainClipboard(std::move(next.data), next.type, next.autoQuit);
        } else if (!success) fallback();
        else if (autoQuit) qApp->quit();
    };
    QObject::connect(process, &QProcess::finished, qApp, [process, completed](int code, QProcess::ExitStatus status) {
        const bool success = code == 0 && status == QProcess::NormalExit;
        if (!success) qWarning("Clipboard helper failed: %s", process->readAllStandardError().constData());
        completed(success);
    });
    QObject::connect(process, &QProcess::errorOccurred, qApp, [process, completed](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning("Cannot start clipboard helper: %s", qPrintable(process->errorString()));
            completed(false);
        }
    });
    process->start(helper, {"--type", type});
    process->write(data);
    process->closeWriteChannel();
}
}
void Ui::copyImage(const QImage& image, bool autoQuit)
{
    auto* data = new QMimeData;
    data->setImageData(image);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    data->setData("image/png", png);
    QApplication::clipboard()->setMimeData(data);
    retainClipboard(png, "image/png", autoQuit);
}
void Ui::copyText(const QString& text, bool autoQuit)
{
    QApplication::clipboard()->setText(text);
    retainClipboard(text.toUtf8(), "text/plain;charset=utf-8", autoQuit);
}
