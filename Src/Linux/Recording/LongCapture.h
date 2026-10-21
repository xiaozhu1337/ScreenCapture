#pragma once
#include "PortalStream.h"
#include "ScrollStitcher.h"
#include <QWidget>
#include <memory>
class Settings;
class LiveRegion;
class QLabel;
class LongCapture : public QWidget
{
    Q_OBJECT
public:
    LongCapture(QImage initial, QRect globalRegion, Settings& settings, bool autoQuit = false);
    ~LongCapture() override;
signals:
    void pinRequested(QImage image, QRect globalRegion);
    void finished();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void start();
    void stop(const QString& reason = {});
    void step();
    void updatePreview();
    Settings& settings;
    QRect region;
    bool autoQuit, running{false}, waiting{false};
    PortalStream stream;
    QTimer timer;
    QImage latest;
    std::unique_ptr<ScrollStitcher> stitcher;
    LiveRegion* border;
    QLabel* preview;
};
