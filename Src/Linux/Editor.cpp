#include "Editor.h"
#include "Canvas.h"
extern "C" {
#include "quirc/quirc.h"
}
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <cstring>
#include <memory>

Editor::Editor(QImage image, bool quit, QWidget* parent) : QMainWindow(parent), autoQuit(quit)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("ScreenCapture - 截图编辑"));
    canvas = new Canvas(std::move(image), this);
    setCentralWidget(canvas);
    auto* tools = addToolBar(tr("标注"));
    tools->setMovable(false);
    auto* group = new QActionGroup(this);
    const QList<QPair<QString, Canvas::Tool>> definitions{
        {tr("选择"), Canvas::Tool::Select}, {tr("画笔"), Canvas::Tool::Pen},
        {tr("矩形"), Canvas::Tool::Rectangle}, {tr("椭圆"), Canvas::Tool::Ellipse},
        {tr("箭头"), Canvas::Tool::Arrow}, {tr("文字"), Canvas::Tool::Text}};
    for (const auto& [name, tool] : definitions) {
        auto* action = tools->addAction(name);
        action->setCheckable(true);
        action->setActionGroup(group);
        action->setChecked(tool == Canvas::Tool::Select);
        connect(action, &QAction::triggered, this, [this, tool] { canvas->setTool(tool); });
    }
    tools->addAction(tr("颜色"), this, [this] {
        const QColor color = QColorDialog::getColor(canvas->color, this, tr("标注颜色"));
        if (color.isValid()) canvas->color = color;
    });
    auto* width = new QSpinBox(tools);
    width->setRange(1, 100);
    width->setValue(canvas->lineWidth);
    width->setPrefix(tr("线宽 "));
    connect(width, &QSpinBox::valueChanged, this, [this](int value) { canvas->lineWidth = value; });
    tools->addWidget(width);
    tools->addAction(tr("裁剪"), canvas, &Canvas::crop);
    tools->addAction(tr("撤销"), QKeySequence::Undo, canvas, &Canvas::undo);
    tools->addAction(tr("重做"), QKeySequence::Redo, canvas, &Canvas::redo);
    addToolBarBreak();
    auto* actions = addToolBar(tr("截图"));
    actions->setMovable(false);
    actions->addAction(tr("新截图"), QKeySequence("Ctrl+N"), this, [this] { if (newCapture) newCapture(); });
    actions->addAction(tr("复制图片"), QKeySequence::Copy, this, &Editor::copy);
    actions->addAction(tr("保存图片"), QKeySequence::Save, this, &Editor::save);
    auto* pin = actions->addAction(tr("置顶钉图"));
    pin->setCheckable(true);
    pin->setShortcut(QKeySequence("F3"));
    connect(pin, &QAction::toggled, this, [this](bool checked) {
        setWindowFlag(Qt::WindowStaysOnTopHint, checked);
        show();
    });
    actions->addAction(tr("文字识别 OCR"), QKeySequence("Ctrl+O"), this, &Editor::recognize);
    actions->addAction(tr("识别二维码"), this, &Editor::decodeQr);
    actions->addAction(tr("关闭"), QKeySequence(Qt::Key_Escape), this, &QWidget::close);
    canvas->copied = [this] { copy(); };
    statusBar()->showMessage(tr("拖动框选区域；右键清除选区。保存、复制和 OCR 使用当前选区，未框选时使用整张图片。"));
    resize(1100, 760);
}

void Editor::copy()
{
    QApplication::clipboard()->setImage(canvas->result());
    // shortcut: keep the Wayland selection owner alive until the user closes the window.
    statusBar()->showMessage(tr("已复制图片；粘贴完成后可关闭窗口。"), 8000);
}

void Editor::save()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存截图"), "Screenshot.png", tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !canvas->result().save(&file, "PNG") || !file.commit()) {
        qCritical("Failed to save screenshot: %s", qPrintable(path));
        QMessageBox::warning(this, tr("保存失败"), tr("无法保存图片，请检查目标路径和可用空间。"));
        return;
    }
    statusBar()->showMessage(tr("已保存：%1").arg(path), 8000);
    if (autoQuit) close();
}

void Editor::recognize()
{
    const QString adjacent = QCoreApplication::applicationDirPath() + "/ImageReader";
    const QString program = QFileInfo(adjacent).isExecutable() ? adjacent : QStandardPaths::findExecutable("ImageReader");
    if (program.isEmpty()) {
        QMessageBox::warning(this, tr("OCR"), tr("未找到 ImageReader，请先构建 Linux OCR 插件。"));
        return;
    }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    auto temp = std::make_shared<QTemporaryDir>(cache + "/ocr-XXXXXX");
    const QString input = temp->filePath("capture.png");
    if (!temp->isValid() || !canvas->result().save(input, "PNG")) {
        qCritical("Cannot prepare OCR image");
        QMessageBox::warning(this, tr("OCR"), tr("无法创建识别用的临时图片。"));
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("文字识别 OCR"));
    auto* layout = new QVBoxLayout(dialog);
    auto* text = new QTextEdit(dialog);
    text->setPlainText(tr("正在识别……"));
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto* copy = buttons->addButton(tr("复制全部"), QDialogButtonBox::ActionRole);
    copy->setEnabled(false);
    connect(copy, &QPushButton::clicked, dialog, [text] { QApplication::clipboard()->setText(text->toPlainText()); });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    auto* process = new QProcess(dialog);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(120000);
    connect(timer, &QTimer::timeout, dialog, [process] { process->kill(); });
    connect(process, &QProcess::finished, dialog, [=](int code, QProcess::ExitStatus status) {
        timer->stop();
        if (code == 0 && status == QProcess::NormalExit) {
            text->setPlainText(QString::fromUtf8(process->readAllStandardOutput()));
            copy->setEnabled(true);
        } else {
            const QString reason = QString::fromUtf8(process->readAllStandardError());
            qCritical("OCR process failed: %s", qPrintable(reason));
            text->setPlainText(tr("识别失败：%1").arg(reason));
        }
        temp->remove();
    });
    connect(process, &QProcess::errorOccurred, dialog, [=](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        timer->stop();
        qCritical("Cannot start OCR: %s", qPrintable(process->errorString()));
        text->setPlainText(tr("无法启动 OCR：%1").arg(process->errorString()));
        temp->remove();
    });
    connect(dialog, &QDialog::finished, process, [process, temp] { if (process->state() != QProcess::NotRunning) process->kill(); });
    process->start(program, {"--text", input});
    timer->start();
    dialog->resize(650, 420);
    dialog->show();
}

void Editor::decodeQr()
{
    const QImage pixels = canvas->result().convertToFormat(QImage::Format_Grayscale8);
    const auto deleter = [](quirc* value) { quirc_destroy(value); };
    std::unique_ptr<quirc, decltype(deleter)> decoder(quirc_new(), deleter);
    if (!decoder || quirc_resize(decoder.get(), pixels.width(), pixels.height()) < 0) {
        qCritical("Cannot allocate QR decoder");
        QMessageBox::warning(this, tr("二维码"), tr("无法分配二维码识别内存。"));
        return;
    }
    auto* data = quirc_begin(decoder.get(), nullptr, nullptr);
    for (int y = 0; y < pixels.height(); ++y)
        std::memcpy(data + y * pixels.width(), pixels.constScanLine(y), pixels.width());
    quirc_end(decoder.get());
    QStringList results;
    for (int index = 0; index < quirc_count(decoder.get()); ++index) {
        quirc_code code{};
        quirc_data result{};
        quirc_extract(decoder.get(), index, &code);
        auto error = quirc_decode(&code, &result);
        if (error == QUIRC_ERROR_DATA_ECC) { quirc_flip(&code); error = quirc_decode(&code, &result); }
        if (!error) results.append(QString::fromUtf8(reinterpret_cast<const char*>(result.payload), result.payload_len));
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("二维码"));
    box.setTextFormat(Qt::PlainText);
    box.setText(results.isEmpty() ? tr("未找到可识别的二维码。") : results.join("\n\n"));
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.exec();
}
