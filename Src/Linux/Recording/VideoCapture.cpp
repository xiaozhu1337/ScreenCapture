#include "VideoCapture.h"
#include "RecordingUi.h"
#include "LiveRegion.h"
#include "../Desktop/Ui.h"
#include "../Settings/Settings.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QSaveFile>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>

VideoCapture::VideoCapture(QRect globalRegion, Settings& preferences, bool quit)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), settings(preferences), region(globalRegion),
      autoQuit(quit), stream(this), recorder(this)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(Ui::text("tool.video"));
    setObjectName("videoToolbar");
    Ui::stylePanel(this);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    pages = new QStackedWidget(this);
    layout->addWidget(pages);
    auto* options = new QWidget;
    auto* buttons = new QHBoxLayout(options);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(0);
    const auto separator = [](QHBoxLayout* row) {
        auto* line = new QLabel;
        line->setFixedSize(1, 18);
        line->setStyleSheet("background:#dddddd;");
        row->addWidget(line);
    };
    for (int index = 0; index < 2; ++index) {
        auto* button = new QToolButton(options);
        formats[index] = button;
        button->setText(index == 0 ? "MP4" : "GIF");
        button->setObjectName(index == 0 ? "formatMp4" : "formatGif");
        button->setToolTip(Ui::text(index == 0 ? "video.outputMp4" : "video.outputGif"));
        button->setAccessibleName(button->toolTip());
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setFixedSize(42, 32);
        buttons->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, index] {
            formatIndex = index;
            for (int i = 0; i < 2; ++i) formats[i]->setChecked(i == index);
            if (index == 1) { speaker->setChecked(false); microphone->setChecked(false); }
            speaker->setEnabled(index == 0);
            microphone->setEnabled(index == 0);
        });
    }
    separator(buttons);
    speaker = Ui::button(options, "speaker");
    microphone = Ui::button(options, "microphone");
    speaker->setObjectName("recordSpeaker");
    microphone->setObjectName("recordMicrophone");
    speaker->setToolTip(Ui::text("video.recordSystem"));
    microphone->setToolTip(Ui::text("video.recordMic"));
    for (auto* button : {speaker, microphone}) {
        button->setAccessibleName(button->toolTip());
        button->setCheckable(true);
        buttons->addWidget(button);
    }
    speaker->setChecked(true);
    separator(buttons);
    startButton = Ui::button(options, "start");
    startButton->setToolTip(Ui::text("video.startRecord"));
    startButton->setAccessibleName(startButton->toolTip());
    auto* cancel = Ui::button(options, "close");
    cancel->setToolTip(Ui::text("video.exit"));
    buttons->addWidget(startButton);
    buttons->addWidget(cancel);
    pages->addWidget(options);
    auto* recording = new QWidget;
    auto* recordingButtons = new QHBoxLayout(recording);
    recordingButtons->setContentsMargins(0, 0, 0, 0);
    recordingButtons->setSpacing(0);
    status = new QLabel("00:00 / 120:00", recording);
    status->setAlignment(Qt::AlignCenter);
    status->setFixedSize(112, 32);
    recordingButtons->addWidget(status);
    separator(recordingButtons);
    auto* discard = Ui::button(recording, "close");
    saveButton = Ui::button(recording, "save");
    copyButton = Ui::button(recording, "clipboard");
    discard->setToolTip(Ui::text("video.stopExit"));
    saveButton->setToolTip(Ui::text("video.stopFile"));
    copyButton->setToolTip(Ui::text("video.stopClipboard"));
    for (auto* button : {discard, saveButton, copyButton}) {
        button->setAccessibleName(button->toolTip());
        recordingButtons->addWidget(button);
    }
    pages->addWidget(recording);
    connect(startButton, &QToolButton::clicked, this, &VideoCapture::start);
    connect(saveButton, &QToolButton::clicked, this, [this] { finish(Action::Save); });
    connect(copyButton, &QToolButton::clicked, this, [this] { finish(Action::Copy); });
    connect(cancel, &QToolButton::clicked, this, &QWidget::close);
    connect(discard, &QToolButton::clicked, this, &QWidget::close);
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] { finish(Action::Save); });
    connect(new QShortcut(QKeySequence::Copy, this), &QShortcut::activated, this, [this] { finish(Action::Copy); });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, &QWidget::close);
    connect(&stream, &PortalStream::frameReady, this, [this](QImage image) {
        if (waiting) {
            waiting = false;
            recorder.start(formatIndex == 0 ? Recording::Format::Mp4 : Recording::Format::Gif,
                std::move(image), speaker->isChecked(), microphone->isChecked());
        } else recorder.updateFrame(std::move(image));
    });
    connect(&stream, &PortalStream::cancelled, this, [this] { waiting = false; close(); });
    connect(&stream, &PortalStream::failed, this, [this](const QString& message) {
        waiting = false;
        if (recorder.state() == Recorder::State::Recording) {
            pending = Action::None;
            recorder.stop();
        } else recorder.cancel();
        controls(false, false, false);
        RecordingUi::error(this, message);
    });
    connect(&recorder, &Recorder::recordingStarted, this, [this] {
        controls(false, true, false);
        clock.start(200);
        status->setText(formatIndex == 0 ? "00:00 / 120:00" : "00:00 / 06:00");
    });
    connect(&recorder, &Recorder::finalizing, this, [this] {
        stream.stop(); clock.stop(); controls(true, false, false);
        status->setText(tr("正在完成编码…"));
    });
    connect(&recorder, &Recorder::ready, this, [this](const QString&) {
        stream.stop(); clock.stop(); controls(false, false, true);
        const Action action = pending; pending = Action::None;
        if (action != Action::None) exportFile(action);
    });
    connect(&recorder, &Recorder::failed, this, [this](const QString& message) {
        stream.stop(); clock.stop(); waiting = false; pending = Action::None;
        controls(false, false, false); RecordingUi::error(this, message);
    });
    connect(&recorder, &Recorder::timeLimitReached, this, [this] { pending = Action::Save; });
    connect(&clock, &QTimer::timeout, this, [this] {
        const auto seconds = recorder.elapsedMilliseconds() / 1000;
        status->setText(QString("%1:%2 / %3:00").arg(seconds / 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0')).arg(formatIndex == 0 ? "120" : "06"));
    });
    saving.setInterval(0);
    connect(&saving, &QTimer::timeout, this, &VideoCapture::copyChunk);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (clipboardFile && !QApplication::clipboard()->ownsClipboard()) close();
    });
    border = new LiveRegion(this, region);
    controls(false, false, false);
}
VideoCapture::~VideoCapture()
{
    stream.stop(); recorder.cancel();
    if (destination) destination->cancelWriting();
}
void VideoCapture::controls(bool preparing, bool recording, bool ready)
{
    pages->setCurrentIndex(preparing || recording || ready ? 1 : 0);
    saveButton->setEnabled(recording || ready);
    copyButton->setEnabled(recording || ready);
    setFixedSize((pages->currentIndex() == 0 ? 214 : 209) + 2, 34);
    RecordingUi::place(this, region, settings.values.toolbarBottom);
}
void VideoCapture::start()
{
    if (waiting || recorder.state() == Recorder::State::Recording || recorder.state() == Recorder::State::Finishing) return;
    waiting = true;
    controls(true, false, false);
    status->setText(tr("等待共享授权…"));
    stream.start(region, false, true);
}
void VideoCapture::finish(Action action)
{
    if (saving.isActive()) return;
    if (recorder.state() == Recorder::State::Ready) { exportFile(action); return; }
    if (recorder.state() != Recorder::State::Recording) return;
    pending = action;
    stream.stop();
    recorder.stop();
}
void VideoCapture::exportFile(Action action)
{
    if (action == Action::None || recorder.state() != Recorder::State::Ready) return;
    const QString path = recorder.filePath();
    if (action == Action::Copy) {
        recorder.retainFile();
        border->hide();
        auto* data = new QMimeData;
        data->setUrls({QUrl::fromLocalFile(path)});
        data->setData("x-special/gnome-copied-files", "copy\n" + QUrl::fromLocalFile(path).toEncoded());
        clipboardFile = true;
        if (autoQuit) qApp->setQuitOnLastWindowClosed(false);
        QApplication::clipboard()->setMimeData(data);
        hide();
        return;
    }
    border->hide();
    hide();
    const QString suffix = formatIndex == 0 ? "mp4" : "gif";
    QString target = QFileDialog::getSaveFileName(nullptr, tr("保存录制"), QDir(settings.values.saveDirectory).filePath(
        QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + "." + suffix),
        suffix == "mp4" ? "MP4 (*.mp4)" : "GIF (*.gif)");
    if (target.isEmpty()) { close(); return; }
    if (QFileInfo(target).suffix().isEmpty()) target += "." + suffix;
    source = std::make_unique<QFile>(path);
    destination = std::make_unique<QSaveFile>(target);
    if (!source->open(QIODevice::ReadOnly) || !destination->open(QIODevice::WriteOnly)) {
        source.reset(); destination.reset(); show(); RecordingUi::error(this, tr("无法打开保存路径，请检查权限和空间。")); return;
    }
    controls(true, false, false);
    status->setText(tr("正在保存…"));
    saving.start();
}
void VideoCapture::copyChunk()
{
    const QByteArray bytes = source->read(1024 * 1024);
    if (source->error() != QFile::NoError || destination->write(bytes) != bytes.size()) {
        saving.stop(); destination->cancelWriting(); source.reset(); destination.reset();
        controls(false, false, true); show(); RecordingUi::error(this, tr("保存失败，请检查可用磁盘空间。")); return;
    }
    if (!source->atEnd()) return;
    saving.stop();
    const QString target = destination->fileName();
    const bool success = destination->commit();
    source.reset(); destination.reset();
    controls(false, false, true);
    if (!success) { show(); RecordingUi::error(this, tr("无法提交录制文件，请检查目标路径。")); return; }
    settings.values.saveDirectory = QFileInfo(target).absolutePath();
    settings.save();
    close();
}
void VideoCapture::closeEvent(QCloseEvent* event)
{
    saving.stop(); clock.stop();
    border->hide();
    if (destination) destination->cancelWriting();
    stream.stop(); recorder.cancel();
    emit finished();
    QWidget::closeEvent(event);
}
