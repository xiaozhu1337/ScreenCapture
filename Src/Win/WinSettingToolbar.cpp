#include "pch.h"
#include "WinSettingToolbar.h"
#include "../Setting.h"
#include "../Lang.h"
#include "../Tool/ToolRegistry.h"

WinSettingToolbar::WinSettingToolbar(Ling::WinBase* win) : Ling::Node(win)
{
    auto title = makeChild<Ling::Label>();
    title->setText(Lang::get(L"toolbar.title"));
    title->setFontSize(14.f);
    title->setHeight(28.f);
    title->setFlexShrink(0.f);
    auto hint = makeChild<Ling::Label>();
    hint->setText(Lang::get(L"toolbar.hint"));
    hint->setFontSize(12.f);
    hint->setColor(0x666666FF);
    hint->setHeight(64.f);
    hint->setFlexShrink(0.f);

    auto reset = makeChild<Ling::Button>();
    reset->setId(L"resetToolbar");
    reset->setText(Lang::get(L"toolbar.reset"));
    reset->setSize(104.f, 28.f);
    reset->setFlexShrink(0.f);
    reset->setBorder(1.f, 0xE0E0E0FF);
    reset->setHoverBg(0xF2F2F2FF);
    reset->onClick.add([this](Ling::Button*) {
        Setting::get()->setCaptureToolbar(defaultCaptureToolbar());
        scheduleRebuild();
    });
    list = makeChild<Ling::Node>();
    list->setWidthPercent(100.f);
    list->setFlexDirection(Ling::FlexDirection::Column);
    rebuild();
}

void WinSettingToolbar::rebuild()
{
    list->removeAllChildren();
    auto enabled = Setting::get()->getCaptureToolbar();
    auto order = enabled;
    for (const auto& tool : captureToolDefinitions()) {
        if (std::find(order.begin(), order.end(), tool.id) == order.end()) order.push_back(tool.id);
    }
    for (const auto& id : order) {
        auto definition = findCaptureTool(id);
        auto it = std::find(enabled.begin(), enabled.end(), id);
        const bool selected = it != enabled.end();
        const int index = selected ? static_cast<int>(it - enabled.begin()) : -1;
        auto row = list->makeChild<Ling::Node>();
        row->setId(L"item-" + id);
        row->setHeight(26.f);
        row->setFlexShrink(0.f);
        row->setFlexDirection(Ling::FlexDirection::Row);
        row->setAlignItems(Ling::Align::Center);
        auto icon = row->makeChild<Ling::Label>();
        icon->setText(definition->icon);
        icon->setFontFamily(L"icon");
        icon->setSize(28.f, 24.f);
        icon->setColor(selected ? 0x597ef7FF : 0x999999FF);
        auto label = row->makeChild<Ling::Label>();
        label->setText((selected ? std::to_wstring(index + 1) + L". " : L"") + Lang::get(definition->label));
        label->setHeight(24.f);
        label->setFlexGrow(1.f);
        for (const int direction : { -1, 1 }) {
            auto button = row->makeChild<Ling::Button>();
            button->setId((direction < 0 ? L"up-" : L"down-") + id);
            button->setText(Lang::get(direction < 0 ? L"toolbar.up" : L"toolbar.down"));
            button->setSize(44.f, 24.f);
            const bool available = selected && index + direction >= 0 && index + direction < static_cast<int>(enabled.size());
            button->setColor(available ? 0x333333FF : 0xCCCCCCFF);
            if (available) {
                button->setHoverBg(0xF2F2F2FF);
                button->onClick.add([this, id, direction](Ling::Button*) { move(id, direction); });
            }
        }
        auto button = row->makeChild<Ling::Button>();
        button->setId(L"toggle-" + id);
        button->setText(Lang::get(selected ? L"toolbar.remove" : L"toolbar.add"));
        button->setSize(64.f, 24.f);
        button->setColor(0x597ef7FF);
        button->setHoverBg(0xE6F4FFFF);
        button->onClick.add([this, id](Ling::Button*) { toggle(id); });
    }
}

void WinSettingToolbar::scheduleRebuild()
{
    auto weakThis = getWeakThis();
    Ling::App::get()->dq.TryEnqueue([this, weakThis]() {
        if (weakThis.lock()) rebuild();
    });
}

void WinSettingToolbar::toggle(const std::wstring& id)
{
    auto items = Setting::get()->getCaptureToolbar();
    auto it = std::find(items.begin(), items.end(), id);
    if (it == items.end()) items.push_back(id);
    else items.erase(it);
    Setting::get()->setCaptureToolbar(items);
    scheduleRebuild();
}

void WinSettingToolbar::move(const std::wstring& id, int direction)
{
    auto items = Setting::get()->getCaptureToolbar();
    auto it = std::find(items.begin(), items.end(), id);
    if (it == items.end()) return;
    const int index = static_cast<int>(it - items.begin());
    const int next = index + direction;
    if (next < 0 || next >= static_cast<int>(items.size())) return;
    std::swap(items[index], items[next]);
    Setting::get()->setCaptureToolbar(items);
    scheduleRebuild();
}
