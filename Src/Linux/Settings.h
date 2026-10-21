#pragma once
#include <QColor>
#include <QKeySequence>
#include <QObject>
#include <QStringList>

struct Preferences
{
    bool startCapture{false};
    bool toolbarBottom{false};
    QColor color{Qt::red};
    int lineWidth{3};
    QString saveDirectory;
    QKeySequence captureKey{QStringLiteral("Ctrl+Alt+A")};
    QKeySequence ocrKey;
    QKeySequence pinKey{QStringLiteral("F3")};
    QStringList tools{"pen", "rectangle", "ellipse", "arrow", "text"};
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
