#include "ImageActions.h"
#include "Editor.h"
#include "Canvas.h"
#include "Ui.h"
extern "C" {
#include "quirc/quirc.h"
}
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <iconv.h>
#include <cstring>
#include <memory>

bool ImageActions::recognize(const QImage& image, QWidget* parent)
{
    const QString adjacent = QCoreApplication::applicationDirPath() + "/ImageReader";
    const QString program = QFileInfo(adjacent).isExecutable() ? adjacent : QStandardPaths::findExecutable("ImageReader");
    QString error;
    if (program.isEmpty()) error = QObject::tr("未找到 ImageReader，请先构建 Linux OCR 插件。");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    QTemporaryDir temp(cache + "/ocr-XXXXXX");
    const QString input = temp.filePath("capture.png");
    if (error.isEmpty() && (!temp.isValid() || !image.save(input, "PNG"))) error = QObject::tr("无法创建识别用的临时图片。");
    if (error.isEmpty() && !QProcess::startDetached(program, {"--image-path=" + input, "--del-image=true"}))
        error = QObject::tr("无法启动 ImageReader。");
    if (!error.isEmpty()) {
        qCritical("OCR: %s", qPrintable(error));
        QMessageBox::warning(parent, Ui::text("cap.ocr"), error);
        return false;
    }
    temp.setAutoRemove(false);
    return true;
}
QString ImageActions::decodeQrText(const QByteArray& bytes, int eci, bool kanji)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = utf8(bytes);
    if (!utf8.hasError()) return decoded;
    const char* encoding = kanji || eci == QUIRC_ECI_SHIFT_JIS ? "SHIFT-JIS" : "GB18030";
    iconv_t converter = iconv_open("UTF-8", encoding);
    if (converter == reinterpret_cast<iconv_t>(-1)) return QString::fromLocal8Bit(bytes);
    QByteArray output(bytes.size() * 4 + 16, '\0');
    char* input = const_cast<char*>(bytes.constData());
    char* dest = output.data();
    size_t remaining = bytes.size(), capacity = output.size();
    const size_t result = iconv(converter, &input, &remaining, &dest, &capacity);
    iconv_close(converter);
    if (result == size_t(-1)) return QString::fromLocal8Bit(bytes);
    output.resize(output.size() - capacity);
    return QString::fromUtf8(output);
}
QStringList ImageActions::qrResults(const QImage& image)
{
    const QImage pixels = image.convertToFormat(QImage::Format_Grayscale8);
    const auto deleter = [](quirc* value) { quirc_destroy(value); };
    std::unique_ptr<quirc, decltype(deleter)> decoder(quirc_new(), deleter);
    if (!decoder || quirc_resize(decoder.get(), pixels.width(), pixels.height()) < 0) {
        qCritical("Cannot allocate QR decoder");
        return {};
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
        if (!error) results.append(decodeQrText(QByteArray(reinterpret_cast<const char*>(result.payload), result.payload_len),
                                              result.eci, result.data_type == QUIRC_DATA_TYPE_KANJI));
    }
    return results;
}
void ImageActions::decodeQr(const QImage& image, QWidget* parent, bool autoQuit)
{
    const auto results = qrResults(image);
    QMessageBox box(parent);
    box.setWindowTitle(Ui::text("cap.qrcode"));
    box.setTextFormat(Qt::PlainText);
    box.setText(results.isEmpty() ? Ui::text("cap.qrcodeEmpty") : results.join("\n") + "\n\n" + Ui::text("cap.qrcodeCopy"));
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.setStandardButtons(results.isEmpty() ? QMessageBox::Ok : QMessageBox::Ok | QMessageBox::Cancel);
    if (box.exec() == QMessageBox::Ok && !results.isEmpty()) Ui::copyText(results.join("\n"), autoQuit);
}
void Editor::recognize() { canvas->finishText(); if (ImageActions::recognize(canvas->result(), this)) close(); }
void Editor::decodeQr() { canvas->finishText(); ImageActions::decodeQr(canvas->result(), this, autoQuit); close(); }
