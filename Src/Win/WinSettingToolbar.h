#pragma once
#include <include/Ling.h>

class WinSettingToolbar : public Ling::Node
{
public:
    WinSettingToolbar(Ling::WinBase* win);
private:
    void rebuild();
    void scheduleRebuild();
    void toggle(const std::wstring& id);
    void move(const std::wstring& id, int direction);
    Ling::Node* list{ nullptr };
};
