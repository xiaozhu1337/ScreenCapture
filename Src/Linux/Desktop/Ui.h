#pragma once
#include <QColor>
#include <QIcon>
#include <QJsonObject>
#include <QRect>
#include <QStringList>
class QToolButton;
class QWidget;

namespace Ui {
struct ToolSize { int minimum, maximum, initial; };
QString text(const QString& key);
QString cmyk(const QColor& color);
void setLanguage(const QString& language);
QStringList languages();
QStringList captureTools();
QStringList drawingTools();
ToolSize toolSize(const QString& tool);
QIcon icon(const QString& id);
QToolButton* button(QWidget* parent, const QString& id);
void stylePanel(QWidget* panel);
QPoint toolbarPosition(QRect region, QRect work, QSize size, int requiredHeight, bool bottom, int inset = 5);
void positionWindow(QWidget* window, QRect geometry, bool keyboard = true);
bool saveImage(QWidget* parent, const QImage& image, QString& directory);
void copyImage(const QImage& image, bool autoQuit);
void copyText(const QString& text, bool autoQuit = false);
}
