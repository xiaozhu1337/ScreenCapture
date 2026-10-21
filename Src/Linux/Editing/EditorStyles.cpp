#include "Editor.h"
#include "Canvas.h"
#include "Settings.h"
#include "Ui.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QToolTip>

namespace {
class ToolSlider final : public QSlider
{
public:
    ToolSlider(QWidget* parent, bool preview) : QSlider(Qt::Horizontal, parent), preview(preview) { setMouseTracking(true); }
protected:
    void paintEvent(QPaintEvent* event) override
    {
        QSlider::paintEvent(event);
        if (!preview) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(property("ink").value<QColor>());
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(34, height() / 2.), value() / 2., value() / 2.);
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        QSlider::mouseMoveEvent(event);
        QToolTip::showText(QCursor::pos(), QString::number(value()), this);
    }
private:
    bool preview;
};
}
void Editor::persistStyle()
{
    if (selected.isEmpty()) return;
    if (settings) { settings->values.toolPin[selected] = canvas->style(); settings->save(); }
    layoutTools();
}
void Editor::rebuildStyles()
{
    slider = nullptr;
    if (auto* layout = styles->layout()) {
        while (auto* item = layout->takeAt(0)) { delete item->widget(); delete item; }
        delete layout;
    }
    if (selected.isEmpty()) return;
    const auto style = canvas->style();
    auto* layout = new QHBoxLayout(styles);
    layout->setSpacing(0);
    layout->setContentsMargins(1, 4, 1, 1);
    auto toggle = [this, layout, style](const QString& id, const QString& label, bool ToolStyle::*flag) {
        auto* button = Ui::button(styles, id);
        button->setToolTip(Ui::text(label));
        button->setAccessibleName(Ui::text(label));
        button->setCheckable(true);
        button->setChecked(style.*flag);
        connect(button, &QToolButton::toggled, this, [this, flag](bool checked) {
            auto current = canvas->style();
            current.*flag = checked;
            canvas->setStyle(current);
            persistStyle();
        });
        layout->addWidget(button);
    };
    if (selected == "text") {
        toggle("bold", "tool.bold", &ToolStyle::bold);
        toggle("italic", "tool.italic", &ToolStyle::italic);
    } else if (selected == "line") toggle("transparent", "tool.semiTransparent", &ToolStyle::transparent);
    else if (selected == "mosaic" || selected == "eraser") toggle("fill", "tool.rectFill", &ToolStyle::rectangle);
    else toggle("fill-" + selected, "tool." + selected + "Fill", &ToolStyle::fill);
    slider = new ToolSlider(styles, selected == "line");
    slider->setObjectName("toolSize");
    slider->setFocusPolicy(Qt::NoFocus);
    const auto range = Ui::toolSize(selected);
    slider->setRange(range.minimum, range.maximum);
    slider->setValue(style.size);
    slider->setFixedWidth(selected == "line" ? 150 : 86);
    slider->setProperty("ink", style.color);
    slider->setToolTip(QString::number(style.size));
    slider->setStyleSheet(selected == "line" ? "QSlider { padding-left: 64px; padding-right: 3px; }" : "QSlider { padding-left: 3px; padding-right: 3px; }");
    connect(slider, &QSlider::valueChanged, this, [this](int value) {
        auto current = canvas->style();
        current.size = value;
        canvas->setStyle(current);
        slider->setToolTip(QString::number(value));
        persistStyle();
    });
    layout->addWidget(slider);
    if (selected == "mosaic" || selected == "eraser") { styles->adjustSize(); return; }
    const QStringList colors{"#cf1322", "#d48806", "#389e0d", "#13c2c2", "#0958d9", "#722ed1", "#eb2f96", "#000000", "#ffffff"};
    const QStringList names{"red", "yellow", "green", "cyan", "blue", "purple", "pink", "black", "white"};
    for (int i = 0; i < colors.size(); ++i) {
        const QColor ink(colors[i]);
        auto* button = new QToolButton(styles);
        button->setObjectName("color-" + names[i]);
        button->setFixedSize(32, 32);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCheckable(true);
        button->setChecked(ink == style.color);
        button->setToolTip(Ui::text("color." + names[i]));
        button->setAccessibleName(button->toolTip());
        QPixmap pixmap(26, 26);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(QPen(i == 8 ? QColor("#a8a8a8") : ink, 1));
        painter.setBrush(ink);
        painter.drawRoundedRect(QRectF(.5, .5, 12, 12), 2, 2);
        button->setIcon(QIcon(pixmap));
        button->setIconSize(QSize(13, 13));
        connect(button, &QToolButton::clicked, this, [this, ink, button] {
            auto current = canvas->style();
            current.color = ink;
            canvas->setStyle(current);
            slider->setProperty("ink", ink);
            slider->update();
            for (auto* choice : styles->findChildren<QToolButton*>())
                if (choice->objectName().startsWith("color-")) choice->setChecked(choice == button);
            persistStyle();
        });
        layout->addWidget(button);
    }
    styles->adjustSize();
}
