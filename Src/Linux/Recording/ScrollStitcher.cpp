#include "ScrollStitcher.h"
#include <QPainter>
#include <cstring>
#include <limits>

namespace {
QImage gray(const QImage& image)
{
    QImage output(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        auto* dst = output.scanLine(y);
        const auto* src = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            dst[x] = (qBlue(src[x]) * 114 + qGreen(src[x]) * 587 + qRed(src[x]) * 299) / 1000;
    }
    return output;
}
double error(const QImage& a, int ay, const QImage& b, int by, int rows, int step = 1)
{
    double sum = 0;
    for (int row = 0; row < rows; ++row) {
        const uchar* left = a.constScanLine(ay + row), *right = b.constScanLine(by + row);
        for (int x = 0; x < a.width(); x += step) {
            int diff = int(left[x]) - int(right[x]);
            sum += diff * diff;
        }
    }
    return sum / (double(rows) * ((a.width() + step - 1) / step));
}
}
ScrollStitcher::ScrollStitcher(QImage initial)
    : previous(initial.convertToFormat(QImage::Format_RGB32)), result(previous) {}

ScrollStitcher::Result ScrollStitcher::append(const QImage& source)
{
    if (previous.isNull() || source.size() != previous.size()) return Result::Invalid;
    const QImage frame = source.convertToFormat(QImage::Format_RGB32);
    bool differs = false;
    for (int row = 0; row < frame.height(); ++row) {
        if (std::memcmp(frame.constScanLine(row), previous.constScanLine(row), size_t(frame.width()) * 4)) {
            differs = true;
            if (changeStart < 0 || row < changeStart) changeStart = row;
            break;
        }
    }
    int shift = 0;
    if (changeStart >= 0) {
        const QImage oldGray = gray(previous), newGray = gray(frame);
        const int strip = qMin(100, frame.height() - changeStart);
        const int columnStep = qMax(1, (frame.width() + 31) / 32);
        const auto search = [&](bool bottom) {
            const int start = bottom ? frame.height() - strip : changeStart;
            const int limit = bottom ? strip - 1 : frame.height() - changeStart - strip;
            const auto matchError = [&](int s, int step) {
                return error(oldGray, start + s, newGray, start, bottom ? strip - s : strip, step);
            };
            double best = std::numeric_limits<double>::max();
            int candidate = 0;
            for (int s = 0; s <= limit; ++s) {
                const double value = matchError(s, columnStep);
                if (value < best) { best = value; candidate = s; }
            }
            // Sparse columns select candidates; only full-width validation can accept a match.
            best = std::numeric_limits<double>::max();
            int accepted = 0;
            for (int s = qMax(0, candidate - 2); s <= qMin(limit, candidate + 2); ++s) {
                const double value = matchError(s, 1);
                if (value < best) { best = value; accepted = s; }
            }
            const double zero = matchError(0, 1);
            return best <= 400 && best < zero * 0.9 ? accepted : 0;
        };
        shift = search(false);
        if (!shift) shift = search(true);
    }
    if (!shift) {
        if (differs && settleChecks < 2) { ++settleChecks; return Result::RetryFrame; }
        settleChecks = 0;
        return ++dismissals > maximumDismissals ? Result::Bottom : Result::RetryScroll;
    }
    QImage next(result.width(), result.height() + shift, QImage::Format_RGB32);
    if (next.isNull()) return Result::Invalid;
    QPainter painter(&next);
    painter.drawImage(0, 0, result);
    const int paintStart = result.height() - (frame.height() - shift - changeStart);
    painter.drawImage(QPoint(0, paintStart), frame, QRect(0, changeStart, frame.width(), frame.height() - changeStart));
    painter.end();
    result = std::move(next);
    previous = frame;
    dismissals = settleChecks = 0;
    return result.height() > maximumHeight ? Result::HeightLimit : Result::Appended;
}
