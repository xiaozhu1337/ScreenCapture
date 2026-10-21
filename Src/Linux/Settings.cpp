#include "Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace {
QString startupPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/autostart/io.github.xland.ScreenCapture.desktop";
}
QString portable(const QKeySequence& key) { return key.toString(QKeySequence::PortableText); }
QKeySequence readKey(const YAML::Node& node, const char* name, const QKeySequence& fallback)
{
    if (!node[name]) return fallback;
    const QString text = QString::fromStdString(node[name].as<std::string>());
    const QKeySequence result(text, QKeySequence::PortableText);
    if (!text.isEmpty() && (result.isEmpty() || result.count() != 1 || result[0].key() == Qt::Key_unknown))
        throw std::runtime_error("Invalid shortcut");
    return result;
}
}

Settings::Settings(QString directory, QObject* parent) : QObject(parent)
{
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    filePath = QDir(directory).filePath("config.yaml");
    values.saveDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    load();
}

QString Settings::validate(const Preferences& preferences)
{
    if (!preferences.color.isValid() || preferences.lineWidth < 1 || preferences.lineWidth > 100)
        return tr("标注颜色或线宽无效。");
    const QSet<QKeySequence> reserved{QKeySequence::Copy, QKeySequence::Paste, QKeySequence::Cut,
        QKeySequence::Undo, QKeySequence::Redo, QKeySequence::SelectAll, QKeySequence::Save,
        QKeySequence("Alt+Tab"), QKeySequence("Alt+F4"), QKeySequence("Meta+L")};
    for (const auto& key : {preferences.captureKey, preferences.ocrKey}) {
        if (key.isEmpty()) continue;
        const auto code = key[0].key();
        if (key.count() != 1 || code == Qt::Key_unknown || code == Qt::Key_Control || code == Qt::Key_Alt
            || code == Qt::Key_Shift || code == Qt::Key_Meta || reserved.contains(key)
            || (key[0].keyboardModifiers() == Qt::NoModifier && code != Qt::Key_Print && code != Qt::Key_Pause
                && code != Qt::Key_ScrollLock && (code < Qt::Key_F1 || code > Qt::Key_F35)))
            return tr("全局快捷键请使用带修饰键的组合、功能键或 PrintScreen，避免占用常用编辑键。");
    }
    if (!preferences.captureKey.isEmpty() && preferences.captureKey == preferences.ocrKey)
        return tr("截图与 OCR 不能使用同一快捷键。");
    const auto pin = preferences.pinKey;
    if (!pin.isEmpty() && (pin.count() != 1 || pin[0].key() == Qt::Key_unknown || pin == preferences.captureKey || pin == preferences.ocrKey
        || QSet<QKeySequence>{QKeySequence("Ctrl+N"), QKeySequence::Copy, QKeySequence::Save, QKeySequence("Ctrl+O"),
            QKeySequence::Undo, QKeySequence::Redo, QKeySequence(Qt::Key_Escape)}.contains(pin)))
        return tr("钉图快捷键无效或与现有操作冲突。");
    return {};
}

bool Settings::load()
{
    error.clear();
    valid = true;
    if (!QFile::exists(filePath)) return true;
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read settings file");
        if (file.size() > 1024 * 1024) throw std::runtime_error("Settings file is too large");
        const auto root = YAML::Load(file.readAll().toStdString());
        if (!root.IsMap()) throw std::runtime_error("Expected a YAML mapping");
        auto loaded = values;
        loaded.startCapture = root["capture_on_start"].as<bool>(loaded.startCapture);
        loaded.toolbarBottom = root["toolbar_bottom"].as<bool>(loaded.toolbarBottom);
        loaded.lineWidth = root["line_width"].as<int>(loaded.lineWidth);
        loaded.color = QColor(QString::fromStdString(root["color"].as<std::string>(loaded.color.name().toStdString())));
        loaded.saveDirectory = QString::fromStdString(root["save_directory"].as<std::string>(loaded.saveDirectory.toStdString()));
        loaded.captureKey = readKey(root, "capture_shortcut", loaded.captureKey);
        loaded.ocrKey = readKey(root, "ocr_shortcut", loaded.ocrKey);
        loaded.pinKey = readKey(root, "pin_shortcut", loaded.pinKey);
        if (root["tools"]) {
            if (!root["tools"].IsSequence()) throw std::runtime_error("Expected a tools list");
            loaded.tools.clear();
            const QStringList supported = Preferences{}.tools;
            for (const auto& value : root["tools"]) {
                const QString tool = QString::fromStdString(value.as<std::string>());
                if (!supported.contains(tool) || loaded.tools.contains(tool)) throw std::runtime_error("Invalid or duplicate tool");
                loaded.tools.append(tool);
            }
        }
        const QString problem = validate(loaded);
        if (!problem.isEmpty()) throw std::runtime_error(problem.toStdString());
        values = loaded;
        return true;
    } catch (const std::exception& exception) {
        valid = false;
        error = tr("无法读取配置 %1：%2").arg(filePath, QString::fromUtf8(exception.what()));
        qCritical("%s", qPrintable(error));
        return false;
    }
}

bool Settings::save(bool replaceInvalid)
{
    if (!valid && !replaceInvalid) return false;
    const QString problem = validate(values);
    if (!problem.isEmpty()) { error = problem; qCritical("%s", qPrintable(error)); return false; }
    YAML::Emitter yaml;
    yaml << YAML::BeginMap
         << YAML::Comment("Capture immediately on ordinary startup; tray mode always stays idle.")
         << YAML::Key << "capture_on_start" << YAML::Value << values.startCapture
         << YAML::Comment("Place the annotation and capture toolbars at the bottom.")
         << YAML::Key << "toolbar_bottom" << YAML::Value << values.toolbarBottom
         << YAML::Comment("Annotation color in hexadecimal RGB format.")
         << YAML::Key << "color" << YAML::Value << values.color.name().toStdString()
         << YAML::Comment("Annotation line width in source-image pixels, from 1 to 100.")
         << YAML::Key << "line_width" << YAML::Value << values.lineWidth
         << YAML::Comment("Initial directory of the PNG save dialog.")
         << YAML::Key << "save_directory" << YAML::Value << values.saveDirectory.toStdString()
         << YAML::Comment("KDE global capture shortcut; an empty value disables it.")
         << YAML::Key << "capture_shortcut" << YAML::Value << portable(values.captureKey).toStdString()
         << YAML::Comment("Optional KDE global screenshot-and-OCR shortcut.")
         << YAML::Key << "ocr_shortcut" << YAML::Value << portable(values.ocrKey).toStdString()
         << YAML::Comment("Pin/unpin shortcut inside the editor; an empty value disables it.")
         << YAML::Key << "pin_shortcut" << YAML::Value << portable(values.pinKey).toStdString()
         << YAML::Comment("Enabled annotation tools, in toolbar order; selection is always available.")
         << YAML::Key << "tools" << YAML::Value << YAML::BeginSeq;
    for (const auto& tool : values.tools) yaml << tool.toStdString();
    yaml << YAML::EndSeq << YAML::EndMap;
    const QByteArray contents = QByteArray(yaml.c_str()) + '\n';
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = tr("无法保存配置 %1：%2").arg(filePath, file.errorString());
        qCritical("%s", qPrintable(error));
        return false;
    }
    valid = true;
    error.clear();
    emit changed();
    return true;
}

bool Settings::autoStart() const
{
    const QString path = startupPath();
    if (!QFile::exists(path)) return false;
    return !QSettings(path, QSettings::IniFormat).value("Desktop Entry/Hidden", false).toBool();
}

bool Settings::setAutoStart(bool enabled)
{
    if (enabled == autoStart()) return true;
    QString executable = QCoreApplication::applicationFilePath();
    executable.replace("\\", "\\\\\\\\").replace("\"", "\\\\\\\"").replace("`", "\\\\`").replace("$", "\\\\$").replace("%", "%%");
    const QByteArray contents = QString("[Desktop Entry]\nType=Application\nName=ScreenCapture\n"
        "Exec=\"%1\" --enter=tray\nIcon=camera-photo\nTerminal=false\nHidden=%2\n")
        .arg(executable, enabled ? "false" : "true").toUtf8();
    const QString path = startupPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = tr("无法更新登录自启：%1").arg(file.errorString());
        qCritical("%s", qPrintable(error));
        return false;
    }
    return true;
}
