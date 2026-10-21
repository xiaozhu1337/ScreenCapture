#pragma once
#include <QMainWindow>
#include <QImage>
#include <QPointer>
#include <functional>
class Canvas;
class Settings;
class QSlider;
class Editor : public QMainWindow
{
public:
    Editor(QImage image, bool autoQuit, Settings* settings = nullptr, QWidget* parent = nullptr);
    void recognize();
    void decodeQr();
    void selectTool(const QString& id);
    void place(QRect geometry);
    Canvas* drawingCanvas() const { return canvas; }
    QRect imageGeometry() const { return placement; }
    std::function<void()> newCapture;
protected:
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    void copy();
    void save();
    void applySettings();
    void rebuildStyles();
    void layoutTools();
    void persistStyle();
    void zoom(int steps, QPoint anchor);
    Canvas* canvas;
    Settings* settings;
    QWidget* tools;
    QWidget* styles;
    QSlider* slider{nullptr};
    QString selected;
    QRect placement;
    bool autoQuit, toolsVisible{true}, placing{false};
};
