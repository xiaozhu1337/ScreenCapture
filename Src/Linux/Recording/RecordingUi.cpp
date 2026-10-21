#include "RecordingUi.h"
#include "../Desktop/Ui.h"
#include <QGuiApplication>
#include <QScreen>
#include <QMessageBox>
#include <QWidget>

void RecordingUi::place(QWidget* window, QRect region, bool bottom)
{
    auto* screen = QGuiApplication::screenAt(region.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto work = screen->availableGeometry();
    window->adjustSize();
    const QSize size = window->size();
    const QPoint position = Ui::toolbarPosition(region, work, size, size.height(), bottom);
    Ui::positionWindow(window, QRect(position, size));
}
void RecordingUi::error(QWidget* window, const QString& message)
{
    qCritical("%s", qPrintable(message));
    QMessageBox::warning(window, QObject::tr("屏幕捕获"), message);
}
