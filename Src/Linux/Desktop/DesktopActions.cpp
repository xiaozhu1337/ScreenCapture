#include "DesktopApp.h"
#include "ImageActions.h"
#include "LongCapture.h"
#include "Settings.h"
#include "Ui.h"
#include "VideoCapture.h"
#include <QApplication>
#include <QTimer>
#include <algorithm>

void DesktopApp::dispatch(const QString& id, QImage image, QRectF geometry)
{
    if (id == "ocr") {
        const bool started = ImageActions::recognize(image);
        if (!started) openImage(std::move(image), geometry);
        else if (oneShot || !tray.isVisible()) QTimer::singleShot(0, qApp, [] { qApp->quit(); });
    } else if (id == "qrcode" || id == "qr") {
        ImageActions::decodeQr(image, nullptr, oneShot || !tray.isVisible());
        quitIfIdle();
    } else if (id == "long" || id == "video") {
        QWidget* session;
        if (id == "long") {
            auto* scrolling = new LongCapture(std::move(image), geometry.toAlignedRect(), settings, oneShot || !tray.isVisible());
            connect(scrolling, &LongCapture::pinRequested, this, [this](QImage result, QRect region) {
                openImage(std::move(result), region);
            });
            session = scrolling;
        } else session = new VideoCapture(geometry.toAlignedRect(), settings, oneShot || !tray.isVisible());
        recordings.append(session);
        connect(session, &QObject::destroyed, this, [this] {
            recordings.removeIf([](const auto& item) { return item.isNull(); });
            quitIfIdle();
        });
        if (id == "video") session->show();
    } else openImage(std::move(image), geometry, Ui::drawingTools().contains(id) ? id : QString());
}
