#pragma once
#include <QMainWindow>
#include <QImage>
#include <functional>

class Canvas;
class Editor : public QMainWindow
{
public:
    Editor(QImage image, bool autoQuit, QWidget* parent = nullptr);
    void recognize();
    void decodeQr();
    std::function<void()> newCapture;
private:
    void copy();
    void save();
    Canvas* canvas;
    bool autoQuit;
};
