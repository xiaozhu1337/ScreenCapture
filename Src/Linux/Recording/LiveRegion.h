#pragma once
#include <QWidget>
#include <functional>

class LiveRegion : public QWidget
{
public:
    LiveRegion(QWidget* owner, QRect region, bool prompt = false);
    void begin();
    void message(QString text);
    std::function<void()> started;
    std::function<void()> cancelled;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QRect region;
    QList<QWidget*> views;
    QPointF cursor;
    QString status;
    bool prompt;
};
