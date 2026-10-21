#pragma once
#include "PortalStream.h"
#include "Recorder.h"
#include <QWidget>
#include <memory>
class Settings;
class QStackedWidget;
class LiveRegion;
class QLabel;
class QToolButton;
class QFile;
class QSaveFile;

class VideoCapture : public QWidget
{
    Q_OBJECT
public:
    VideoCapture(QRect globalRegion, Settings& settings, bool autoQuit = false);
    ~VideoCapture() override;
signals:
    void finished();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    enum class Action { None, Save, Copy };
    void start();
    void finish(Action action);
    void exportFile(Action action);
    void copyChunk();
    void controls(bool preparing, bool recording, bool ready);
    Settings& settings;
    QRect region;
    bool autoQuit, waiting{false}, clipboardFile{false};
    Action pending{Action::None};
    PortalStream stream;
    Recorder recorder;
    QTimer clock, saving;
    LiveRegion* border;
    QLabel* status;
    QStackedWidget* pages;
    int formatIndex{0};
    QToolButton* formats[2];
    QToolButton* speaker;
    QToolButton* microphone;
    QToolButton* startButton;
    QToolButton* saveButton;
    QToolButton* copyButton;
    std::unique_ptr<QFile> source;
    std::unique_ptr<QSaveFile> destination;
};
