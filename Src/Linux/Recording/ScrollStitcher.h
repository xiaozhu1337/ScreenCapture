#pragma once
#include <QImage>

class ScrollStitcher
{
public:
    enum class Result { Appended, RetryFrame, RetryScroll, Bottom, HeightLimit, Invalid };
    explicit ScrollStitcher(QImage initial = {});
    Result append(const QImage& frame);
    const QImage& image() const { return result; }
    static constexpr int maximumHeight = 36000;
    static constexpr int maximumDismissals = 8;
private:
    QImage previous, result;
    int changeStart{-1}, dismissals{0}, settleChecks{0};
};
