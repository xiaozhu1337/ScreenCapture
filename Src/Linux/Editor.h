#pragma once
#include <QMainWindow>
#include <QImage>
#include <functional>

class Canvas;
class Settings;
class QToolBar;
class QSpinBox;
class Editor : public QMainWindow
{
public:
    Editor(QImage image, bool autoQuit, Settings* settings = nullptr, QWidget* parent = nullptr);
    void recognize();
    void decodeQr();
    std::function<void()> newCapture;
private:
    void copy();
    void save();
    void applySettings();
    Canvas* canvas;
    Settings* settings;
    QToolBar* tools;
    QToolBar* actions;
    QSpinBox* width;
    QAction* pin;
    bool autoQuit;
};
