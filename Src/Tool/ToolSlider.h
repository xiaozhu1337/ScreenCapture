#pragma once
#include <include/Ling.h>

class ToolSlider : public Ling::Node
{
public:
    explicit ToolSlider(Ling::WinBase* win);
    ~ToolSlider();
    void setRange(float min, float max);
    void setValue(float value);
    float getValue() const { return value; }
    float getValueAt(float px) const;
    void setStep(float step);
    void setSizePreview(bool enabled);
    winrt::event<winrt::delegate<ToolSlider*, float>> onValueChanged;
private:
    void layout() override;
    void syncVisual();
    float maxRadius() const;
    float inset() const;
    float snap(float value) const;
    winrt::Windows::UI::Composition::ShapeVisual shapes{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry track{ nullptr };
    winrt::Windows::UI::Composition::CompositionEllipseGeometry thumb{ nullptr };
    winrt::Windows::UI::Composition::CompositionSpriteShape trackShape{ nullptr }, thumbShape{ nullptr };
    winrt::event_token downToken{}, moveToken{}, upToken{}, wheelToken{};
    float minValue{ 0.f }, maxValue{ 100.f }, value{ 0.f }, step{ 0.f };
    bool sizePreview{ false }, dragging{ false };
};
