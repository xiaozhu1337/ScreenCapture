#pragma once
#include <QRect>
class QWidget;
namespace RecordingUi {
void place(QWidget* window, QRect region, bool bottom = false);
void error(QWidget* window, const QString& message);
}
