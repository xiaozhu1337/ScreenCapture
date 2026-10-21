#pragma once
#include <QImage>
#include <QString>
class QWidget;
namespace ImageActions {
bool recognize(const QImage& image, QWidget* parent = nullptr);
QStringList qrResults(const QImage& image);
QString decodeQrText(const QByteArray& bytes, int eci, bool kanji);
void decodeQr(const QImage& image, QWidget* parent = nullptr, bool autoQuit = false);
}
