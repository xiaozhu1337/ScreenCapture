#include "Canvas.h"
#include <QApplication>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTest>
#include <QTextDocument>
#include <QWheelEvent>

class CanvasParityTests : public QObject
{
    Q_OBJECT
    using Tool = Canvas::Tool;
    static QImage source(QColor color = Qt::white) {
        QImage image(320, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(color);
        return image;
    }
    static ToolStyle style(int size = 3, bool fill = false) {
        ToolStyle value;
        value.size = size;
        value.color = Qt::red;
        value.fill = fill;
        return value;
    }
    static void prepare(Canvas& canvas, Tool tool, ToolStyle value = style()) {
        canvas.resize(canvas.imageSize());
        canvas.show();
        canvas.setTool(tool);
        canvas.setStyle(value);
        QApplication::processEvents();
    }
    static void move(Canvas& canvas, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(QEvent::MouseMove, point, canvas.mapToGlobal(point), Qt::NoButton,
            canvas.dragging ? Qt::LeftButton : Qt::NoButton, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }
    static void drag(Canvas& canvas, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, from);
        move(canvas, to, modifiers);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, to);
    }
    static void wheel(Canvas& canvas, QPoint point, int delta = 120) {
        QWheelEvent event(point, canvas.mapToGlobal(point), {}, QPoint(0, delta), Qt::NoButton,
            Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&canvas, &event);
    }
private slots:
    void rectangularGeometry() {
        for (const auto tool : {Tool::Rectangle, Tool::Ellipse, Tool::Mosaic, Tool::Eraser}) {
            Canvas canvas(source());
            auto value = style(26, true);
            value.rectangle = true;
            prepare(canvas, tool, value);
            drag(canvas, {40, 40}, {160, 120});
            QCOMPARE(canvas.strokes.size(), 1);
            const auto& stroke = canvas.strokes.first();
            QCOMPARE(canvas.handles(stroke).size(), 8);
            QVERIFY(!canvas.hits(stroke, {100, 80}));
            QVERIFY(canvas.hits(stroke, {100, 40}));
            for (const auto point : canvas.handles(stroke)) QVERIFY(canvas.handleAt(stroke, point) >= 0);
            drag(canvas, {160, 120}, {180, 100}, Qt::ShiftModifier);
            QCOMPARE(stroke.bounds(), QRectF(40, 40, 140, 140));
            canvas.undo();
            QCOMPARE(canvas.result(), source());
            canvas.redo();
            QCOMPARE(canvas.strokes.first().bounds(), QRectF(40, 40, 140, 140));
        }
        Canvas reverse(source());
        prepare(reverse, Tool::Rectangle);
        drag(reverse, {140, 120}, {70, 90}, Qt::ShiftModifier);
        QCOMPARE(reverse.strokes.first().bounds(), QRectF(70, 90, 70, 70));
        const QRectF expected[] = {{20, 30, 140, 90}, {40, 30, 120, 90}, {40, 30, 140, 90}, {40, 40, 140, 80},
            {40, 40, 140, 110}, {40, 40, 120, 110}, {20, 40, 140, 110}, {20, 40, 140, 80}};
        const QPoint targets[] = {{20, 30}, {100, 30}, {180, 30}, {180, 80}, {180, 150}, {100, 150}, {20, 150}, {20, 80}};
        for (int i = 0; i < 8; ++i) {
            Canvas canvas(source());
            prepare(canvas, Tool::Rectangle);
            drag(canvas, {40, 40}, {160, 120});
            const auto grip = canvas.handles(canvas.strokes.first())[i].toPoint();
            drag(canvas, grip, targets[i]);
            QCOMPARE(canvas.strokes.first().bounds(), expected[i]);
        }
    }
    void arrowContourAndShift() {
        Canvas canvas(source());
        prepare(canvas, Tool::Arrow, style(3, true));
        drag(canvas, {30, 50}, {150, 65}, Qt::ShiftModifier);
        QCOMPARE(canvas.strokes.first().end, QPointF(150, 50));
        QVERIFY(canvas.hits(canvas.strokes.first(), {120, 50}));
        QVERIFY(!canvas.hits(canvas.strokes.first(), {120, 60}));
        const auto contour = canvas.outline(canvas.strokes.first());
        QCOMPARE(contour.elementCount(), 7);
        QCOMPARE(QPointF(contour.elementAt(1)), QPointF(138, 47));
        QCOMPARE(canvas.result().pixelColor(120, 50), QColor(Qt::red));
        canvas.strokes.first().style.fill = false;
        QCOMPARE(canvas.outline(canvas.strokes.first()), contour);
        QCOMPARE(canvas.result().pixelColor(120, 50), QColor(Qt::white));
        drag(canvas, {150, 50}, {180, 140}, Qt::ShiftModifier);
        QCOMPARE(canvas.strokes.first().end, QPointF(180, 200));
        drag(canvas, {30, 50}, {160, 30}, Qt::ShiftModifier);
        QCOMPARE(canvas.strokes.first().start.x(), canvas.strokes.first().end.x());
    }
    void numberControlsAndHistory() {
        Canvas canvas(source());
        prepare(canvas, Tool::Number, style(16, true));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {160, 60});
        QCOMPARE(canvas.strokes.size(), 2);
        QCOMPARE(canvas.strokes.last().number, 2);
        QCOMPARE(canvas.handles(canvas.strokes.first()).size(), 3);
        QVERIFY(canvas.outline(canvas.strokes.first()).boundingRect().bottom() > 81);
        QVERIFY(!canvas.hits(canvas.strokes.first(), {65, 60}));
        canvas.undo();
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {160, 160});
        QCOMPARE(canvas.strokes.size(), 2);
        QCOMPARE(canvas.strokes.last().number, 2);
        canvas.redo();
        QVERIFY(!canvas.strokes.last().undone);
        drag(canvas, {60, 60}, {80, 80});
        QCOMPARE(canvas.strokes.first().start, QPointF(80, 80));
        auto tip = canvas.handles(canvas.strokes.first())[1].toPoint();
        drag(canvas, tip, {115, 80});
        QVERIFY(qAbs(canvas.strokes.first().angle) < 0.01);
        QString changedId;
        int changedSize = 0;
        canvas.sizeChanged = [&](const QString& id, int size) { changedId = id; changedSize = size; };
        auto radius = canvas.handles(canvas.strokes.first())[2].toPoint();
        drag(canvas, radius, {20, 80});
        QCOMPARE(canvas.strokes.first().radius, 60.);
        QCOMPARE(changedId, QString("number"));
        QCOMPARE(changedSize, 60);
        canvas.undo();
        QVERIFY(canvas.strokes.last().undone);
        QVERIFY(!canvas.strokes.first().undone);
        canvas.redo();
        move(canvas, canvas.strokes.last().start);
        QTest::keyClick(&canvas, Qt::Key_Delete);
        QCOMPARE(canvas.strokes.size(), 1);
        canvas.undo();
        QVERIFY(canvas.strokes.first().undone);
        canvas.redo();
        QCOMPARE(canvas.strokes.first().start, QPointF(80, 80));
    }
    void freehandAndEmptyClicks() {
        for (auto tool : {Tool::Pen, Tool::Rectangle, Tool::Ellipse, Tool::Arrow, Tool::Mosaic, Tool::Eraser}) {
            Canvas canvas(source());
            prepare(canvas, tool, style(24));
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {40, 40});
            QCOMPARE(canvas.strokes.size(), 0);
        }
        Canvas canvas(source());
        auto value = style(24);
        value.transparent = true;
        prepare(canvas, Tool::Pen, value);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::ShiftModifier, {20, 40});
        move(canvas, {70, 40}, Qt::ShiftModifier);
        move(canvas, {70, 80});
        move(canvas, {120, 80});
        move(canvas, {140, 100}, Qt::ShiftModifier);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ShiftModifier, {140, 100});
        QCOMPARE(canvas.strokes.first().points, QList<QPointF>({{20, 40}, {70, 40}, {70, 80}, {140, 100}}));
        const auto pixel = canvas.result().pixelColor(40, 40);
        QCOMPARE(pixel.red(), 255);
        QVERIFY(qAbs(pixel.green() - 127) <= 1);
        QVERIFY(!canvas.hits(canvas.strokes.first(), {40, 40}));
        QVERIFY(canvas.hits(canvas.strokes.first(), {40, 52}));
        canvas.setTool(Tool::Eraser);
        auto endpoint = canvas.strokes.first().end.toPoint();
        drag(canvas, endpoint, {180, 120});
        QCOMPARE(canvas.strokes.size(), 1);
        QCOMPARE(canvas.strokes.first().points.size(), 5);
        QCOMPARE(canvas.strokes.first().points[1], QPointF(70, 40));
        canvas.undo();
        QCOMPARE(canvas.result(), source());
    }
    void mosaicCompositionPreviewAndEraser() {
        QImage image = source();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, QColor(x % 256, y, (x + y) % 256));
        Canvas canvas(image);
        prepare(canvas, Tool::Rectangle, style(3, true));
        drag(canvas, {40, 40}, {180, 140});
        canvas.setTool(Tool::Mosaic);
        auto value = style(30);
        value.rectangle = true;
        canvas.setStyle(value);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 60});
        move(canvas, {160, 120});
        QVERIFY(canvas.current.preview);
        QImage preview(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        canvas.render(&preview);
        QVERIFY(preview.pixelColor(90, 90).blue() > 30);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {160, 120});
        QVERIFY(!canvas.strokes.last().preview);
        QCOMPARE(canvas.result().pixelColor(90, 90), QColor(Qt::red));
        canvas.undo();
        canvas.undo();
        canvas.redo();
        canvas.redo();
        QCOMPARE(canvas.result().pixelColor(90, 90), QColor(Qt::red));
        canvas.setTool(Tool::Eraser);
        canvas.setStyle(value);
        drag(canvas, {80, 80}, {140, 110});
        QCOMPARE(canvas.result().pixelColor(100, 100), image.pixelColor(100, 100));
        canvas.undo();
        QCOMPARE(canvas.result().pixelColor(100, 100), QColor(Qt::red));
        Canvas blocks(image);
        prepare(blocks, Tool::Mosaic, value);
        drag(blocks, {60, 60}, {160, 120});
        const auto output = blocks.result();
        QCOMPARE(output.pixelColor(60, 60), QColor(64, 64, 129));
        QCOMPARE(output.pixelColor(60, 60), output.pixelColor(69, 69));
        QVERIFY(output.pixelColor(70, 60) != output.pixelColor(69, 60));
        const auto originalPoints = blocks.strokes.last().points;
        blocks.setTool(Tool::Pen);
        blocks.setStyle(style(10));
        drag(blocks, {200, 100}, {240, 150});
        blocks.setTool(Tool::Eraser);
        drag(blocks, {240, 150}, {260, 160});
        QCOMPARE(blocks.strokes.size(), 2);
        QCOMPARE(blocks.strokes.last().points.size(), 3);
        QCOMPARE(blocks.strokes.first().points, originalPoints);
    }
    void textLifecycle() {
        Canvas canvas(source());
        prepare(canvas, Tool::Text, style(20));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QVERIFY(canvas.textEditor);
        auto* editor = canvas.textEditor.data();
        QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::NoWrap);
        const int initialWidth = editor->width();
        editor->setPlainText("A long line that must grow far beyond the initial editor width");
        QVERIFY(editor->width() > initialWidth + 300);
        const int oneLineHeight = editor->height();
        editor->appendPlainText("Second line");
        QVERIFY(editor->height() > oneLineHeight);
        auto value = style(30);
        value.bold = value.italic = true;
        value.color = Qt::blue;
        canvas.setStyle(value);
        QVERIFY(editor->font().bold() && editor->font().italic());
        QCOMPARE(canvas.strokes.first().style.color, QColor(Qt::blue));
        QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(editor, &shortcut);
        QVERIFY(shortcut.isAccepted());
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(!canvas.textEditor);
        QCOMPARE(canvas.strokes.size(), 1);
        const auto before = canvas.strokes.first().start;
        const auto border = canvas.strokes.first().bounds().topLeft().toPoint();
        drag(canvas, border, border + QPoint(25, 35));
        QCOMPARE(canvas.strokes.first().start, before + QPointF(25, 35));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.strokes.first().start.toPoint() + QPoint(2, 2));
        QVERIFY(canvas.textEditor);
        QTest::keyClick(canvas.textEditor, Qt::Key_Delete, Qt::AltModifier);
        QCOMPARE(canvas.strokes.size(), 1);
        QVERIFY(canvas.textEditor);
        canvas.textEditor->setPlainText("");
        canvas.finishText();
        QCOMPARE(canvas.strokes.size(), 0);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
        canvas.textEditor->setPlainText("Export commits live text");
        const auto exported = canvas.result();
        QVERIFY(!canvas.textEditor);
        QVERIFY(exported != source());
        canvas.undo();
        QCOMPARE(canvas.result(), source());
        canvas.redo();
        QCOMPARE(canvas.result(), exported);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {32, 32});
        QVERIFY(canvas.textEditor);
        canvas.setFocus();
        QApplication::processEvents();
        QVERIFY(!canvas.textEditor);
    }
    void textScalingAndEditingBorder() {
        Canvas canvas(source());
        prepare(canvas, Tool::Text, style(19));
        canvas.pixelRatio = 1.25;
        canvas.resize(640, 480);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {80, 80});
        auto* editor = canvas.textEditor.data();
        editor->setPlainText("a\tb");
        QCOMPARE(editor->tabStopDistance(), 160.);
        const auto originalFont = canvas.strokes.first().font();
        const qreal originalPixels = originalFont.pointSizeF() * QFontMetricsF(originalFont).fontDpi() / 72;
        QVERIFY(qAbs(originalPixels - 19 * 1.25) < 0.01);
        const auto before = canvas.strokes.first().start;
        const auto border = canvas.strokes.first().bounds().topLeft() * 2;
        const auto localBorder = QPointF(editor->mapFrom(&canvas, border.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, localBorder, editor->mapToGlobal(localBorder),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor, &press);
        QVERIFY(!canvas.textEditor);
        QVERIFY(canvas.dragging);
        move(canvas, border + QPointF(20, 30));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, (border + QPointF(20, 30)).toPoint());
        QCOMPARE(canvas.strokes.first().start, before + QPointF(10, 15));
        const auto exported = canvas.result();
        QVERIFY(exported != source());
        canvas.resize(320, 240);
        QCOMPARE(canvas.result(), exported);
    }
    void callbacksWheelAndScale() {
        Canvas canvas(source());
        prepare(canvas, Tool::Rectangle, style(3));
        drag(canvas, {20, 20}, {120, 100});
        canvas.setTool(Tool::Arrow);
        canvas.setStyle(style(5));
        wheel(canvas, {20, 20});
        QCOMPARE(canvas.style().size, 6);
        QCOMPARE(canvas.strokes.first().style.size, 3);
        canvas.pixelRatio = 2;
        canvas.resize(640, 480);
        drag(canvas, {300, 100}, {500, 200});
        QCOMPARE(canvas.strokes.last().ratio, 2.);
        QCOMPARE(canvas.result().size(), QSize(320, 240));
        QCOMPARE(canvas.handles(canvas.strokes.first()).first(), QPointF(20, 20));
        QVERIFY(canvas.handleAt(canvas.strokes.first(), {22, 22}) >= 0);
        QVERIFY(canvas.handleAt(canvas.strokes.first(), {24, 24}) < 0);
        canvas.setTool(Tool::Select);
        canvas.setPinned(true);
        int zoomSteps = 0, copies = 0, rights = 0, clicks = 0;
        QPoint movement, zoomAnchor;
        canvas.zoomed = [&](int steps, QPoint point) { zoomSteps += steps; zoomAnchor = point; };
        canvas.copied = [&] { ++copies; };
        canvas.rightClicked = [&] { ++rights; };
        canvas.clicked = [&] { ++clicks; };
        canvas.moved = [&](QPoint delta) { movement += delta; };
        wheel(canvas, {200, 200});
        QCOMPARE(zoomSteps, 1);
        QCOMPARE(zoomAnchor, QPoint(200, 200));
        for (int i = 0; i < 4; ++i) wheel(canvas, {200, 200}, 30);
        QCOMPARE(zoomSteps, 2);
        drag(canvas, {200, 200}, {240, 230});
        QCOMPARE(movement, QPoint(40, 30));
        QCOMPARE(clicks, 0);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {200, 200});
        QCOMPARE(clicks, 1);
        QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, {200, 200});
        QCOMPARE(rights, 1);
        canvas.setTool(Tool::Number);
        canvas.setStyle(style(16, true));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {300, 300});
        const auto count = canvas.strokes.size();
        QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, {300, 300});
        QCOMPARE(copies, 1);
        QCOMPARE(canvas.strokes.size(), count - 1);
    }
};
QTEST_MAIN(CanvasParityTests)
#include "CanvasParityTests.moc"
