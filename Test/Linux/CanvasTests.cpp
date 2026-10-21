#include "Canvas.h"
#include "Editor.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTest>

static void check(bool value, const char* message)
{
    if (!value) qFatal("%s", message);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QImage source(200, 100, QImage::Format_ARGB32);
    source.fill(QColor(20, 40, 60, 128));
    source.setDevicePixelRatio(2);
    Canvas canvas(source);
    canvas.resize(400, 200);
    canvas.show();
    app.processEvents();
    check(canvas.result().size() == QSize(200, 100), "DPI scaling must preserve export dimensions");
    const auto pixel = canvas.result().pixelColor(0, 0);
    check(pixel.alpha() == 128 && qAbs(pixel.red() - 20) <= 1 && qAbs(pixel.green() - 40) <= 1 && qAbs(pixel.blue() - 60) <= 1,
          "Export must preserve alpha and color within premultiplied rounding");
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(240, 140));
    check(canvas.result().size() == QSize(101, 51), "Selection must use original pixel coordinates");
    QSize copiedSize;
    canvas.copied = [&] { copiedSize = canvas.result().size(); };
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    check(copiedSize == QSize(101, 51), "A complete double-click sequence must preserve the selected area");
    canvas.crop();
    check(canvas.result().size() == QSize(101, 51), "Crop must retain selected pixel dimensions");
    canvas.setTool(Canvas::Tool::Rectangle);
    const auto area = canvas.imageRect();
    const QPoint start = area.topLeft() + QPoint(20, 20);
    const QPoint end = area.bottomRight() - QPoint(20, 20);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
    const auto annotated = canvas.result();
    canvas.undo();
    check(canvas.result() != annotated, "Undo must remove the annotation");
    canvas.redo();
    check(canvas.result() == annotated, "Redo must restore the exact pixels");
    canvas.undo();
    canvas.setTool(Canvas::Tool::Arrow);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
    const auto arrow = canvas.result();
    canvas.redo();
    check(canvas.result() == arrow, "Drawing after undo must invalidate redo history");
    canvas.setTool(Canvas::Tool::Select);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, area.topLeft());
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, area.bottomRight());
    check(canvas.result().size() == QSize(101, 51), "Whole-image selection must include the last row and column");
    QTest::mouseClick(&canvas, Qt::RightButton);
    check(canvas.result().size() == QSize(101, 51), "Clearing selection must retain the original image");
    QImage large(3840, 2160, QImage::Format_RGB32);
    large.fill(Qt::white);
    Canvas scaled(large);
    scaled.resize(1100, 700);
    scaled.show();
    app.processEvents();
    const auto scaledArea = scaled.imageRect();
    for (const bool reversed : {false, true}) {
        const auto start = reversed ? scaledArea.bottomRight() : scaledArea.topLeft();
        const auto end = reversed ? scaledArea.topLeft() : scaledArea.bottomRight();
        QTest::mousePress(&scaled, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseRelease(&scaled, Qt::LeftButton, Qt::NoModifier, end);
        check(scaled.result().size() == large.size(), "Downscaled selection must include every edge pixel in both drag directions");
    }
    Canvas row(QImage(3840, 1, QImage::Format_RGB32));
    row.resize(1100, 700);
    check(row.imageRect().width() > 0 && row.imageRect().height() > 0, "Single-row images must retain a visible drawing area");
    qInfo("Canvas pixel, crop, DPI, double-click, thin-image, and history checks passed");
    QImage sample(1100, 240, QImage::Format_RGB32);
    sample.fill(Qt::white);
    {
        QPainter painter(&sample);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Noto Sans CJK SC", 36));
        painter.drawText(35, 85, "Screen Capture 12345");
        painter.drawText(35, 185, QString::fromUtf8("中文文字识别测试"));
    }
    Editor editor(sample, false);
    editor.recognize();
    auto* text = editor.findChild<QTextEdit*>();
    check(text != nullptr, "OCR must open a result dialog");
    QElapsedTimer timer;
    timer.start();
    while (!text->toPlainText().contains("12345") && timer.elapsed() < 15000) QTest::qWait(20);
    check(text->toPlainText().contains("12345") && text->toPlainText().contains(QString::fromUtf8("中文")),
          "Screenshot editor must recognize English and Chinese through ImageReader");
    QDir().mkpath("__tmp");
    QTemporaryDir directory("__tmp/linux-test-XXXXXX");
    check(directory.isValid(), "Cannot create temporary test directory");
    const QString input = directory.filePath("input.png"), output = directory.filePath("output.png");
    check(sample.save(input), "Cannot write test image");
    QProcess process;
    process.start(QDir::currentPath() + "/build/linux/x86_64/release/ScreenCapture", {"--image", input, "--output", output});
    timer.restart();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 10000) QTest::qWait(20);
    check(process.state() == QProcess::NotRunning && process.exitCode() == 0, "CLI image export must succeed");
    check(QImage(output).convertToFormat(QImage::Format_RGB32) == sample, "CLI export must preserve all source pixels");
    qInfo("Editor OCR integration and CLI image export checks passed");
    return 0;
}
