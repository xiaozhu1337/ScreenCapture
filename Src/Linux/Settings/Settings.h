#pragma once
#include <QColor>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QStringList>

struct ToolStyle
{
    int size{2};
    QColor color{"#cf1322"};
    bool fill{false}, transparent{false}, bold{false}, italic{false}, rectangle{false};
    bool operator==(const ToolStyle&) const = default;
};
struct Preferences
{
    Preferences();
    bool startCapture{true};
    bool toolbarBottom{false};
    QColor color{"#cf1322"};
    int lineWidth{12};
    QString saveDirectory;
    QString language{"zh-CN"};
    QKeySequence captureKey{QStringLiteral("Ctrl+Alt+A")};
    QKeySequence ocrKey;
    QKeySequence pinKey{QStringLiteral("F3")};
    QStringList captureToolbar{"mark", "long", "video", "ocr", "qrcode"};
    QMap<QString, ToolStyle> toolPin;
    QString restoreToken;
};
class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QString directory = {}, QObject* parent = nullptr);
    bool load();
    static QString validate(const Preferences& preferences);
    bool save(bool replaceInvalid = false);
    bool autoStart() const;
    bool setAutoStart(bool enabled);
    QString path() const { return filePath; }
    QString error;
    Preferences values;
signals:
    void changed();
private:
    QString filePath;
    bool valid{true};
};
