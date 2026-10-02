#include "pch.h"
#include "../Win/WinCap.h"
#include "../Lang.h"
#include "../Tip.h"
#include "../Setting.h"
#include "../Win/CutMask.h"
#include "ToolCap.h"

ToolCap::ToolCap(WinCap* win) : Ling::WinBase(), win(win)
{
	for (const auto& id : Setting::get()->getCaptureToolbar()) {
		if (auto tool = findCaptureTool(id)) buttons.push_back(*tool);
	}
	if (!buttons.empty()) buttons.push_back({ L"spliter", L"", L"", false });
	buttons.push_back({ L"close", L"\ue62d", L"tool.close", false });
	buttons.push_back({ L"save", L"\ue608", L"tool.save", false });
	buttons.push_back({ L"clipboard", L"\ue6ad", L"tool.clipboard", false });
	// 跟着宿主窗口的缩放走：WinBase 构造里取的是系统 dpi，宿主可能在另一块缩放比例不同的屏上
	dpi = win->dpi;
	// 位置由 WinCap::layoutTool() 在 createNativeWindow 之前设好，这里只算尺寸
	refreshSize();
	// 点按钮会把 ToolCap 激活，键盘消息进的是它，转发给 WinCap 让 ESC 一致生效
	onKeyDown.add([this](UINT key) { this->win->onKeyDown(key); });
	// DPI 变了（工具条被挪到缩放比例不同的显示器上，或者用户改了系统缩放）：
	// Ling 只会把窗口按系统给的建议矩形整体缩放一遍，我们自己定的那套摆放规则不会重跑，
	// 工具条就歪在别处了。位置也不能在 onDpiChanged 里直接改 —— 那个事件在 Ling 应用建议矩形
	// 之前触发，改了马上被覆盖，所以这里只记个标记，等建议矩形应用后紧随而来的 WM_SIZE 再动手
	onDpiChanged.add([this]() { dpiChanged = true; });
	onSizeChanged.add([this]() {
		if (!dpiChanged) return;
		dpiChanged = false;
		refreshSize();                    //宿主的摆放规则要用宽高，先按新 dpi 把尺寸定下来
		buildButtons();
		this->win->layoutTool(this);
	});
}

void ToolCap::refreshSize()
{
	const auto& mask = win->cutMask->maskRect;
	RECT region{ win->x + static_cast<LONG>(mask.left), win->y + static_cast<LONG>(mask.top),
		win->x + static_cast<LONG>(mask.right), win->y + static_cast<LONG>(mask.bottom) };
	MONITORINFO monitor{ sizeof(MONITORINFO) };
	const bool found = GetMonitorInfo(MonitorFromRect(&region, MONITOR_DEFAULTTONEAREST), &monitor);
	const float workWidth = static_cast<float>(found ? monitor.rcWork.right - monitor.rcWork.left : GetSystemMetrics(SM_CXSCREEN));
	const float maxWidth = std::max(btnSize, workWidth / dpi);
	rowStarts = { 0 };
	float logicW{ 0.f };
	float widest{ 0.f };
	for (size_t i = 0; i < buttons.size(); ++i) {
		const float width = buttons[i].id == L"spliter" ? spliterW : btnSize;
		if (logicW > 0.f && logicW + width > maxWidth) {
			widest = std::max(widest, logicW);
			rowStarts.push_back(i);
			logicW = 0.f;
		}
		logicW += width;
	}
	setSize(std::max(widest, logicW), btnSize * rowStarts.size());
}

ToolCap::~ToolCap()
{
}

void ToolCap::onCreated()
{
	tip = std::make_unique<Tip>(this);
	body->setBg(0xFFFFFFFF);
	body->setBorder(1.f, 0xA8A8A8ff);
	body->setFlexDirection(Ling::FlexDirection::Column);
	buildButtons();
	show();
}

void ToolCap::buildButtons()
{
	if (tip) tip->hide();
	body->removeAllChildren();
	Ling::Node* row{ nullptr };
	for (size_t i = 0; i < buttons.size(); i++)
	{
		if (std::find(rowStarts.begin(), rowStarts.end(), i) != rowStarts.end()) {
			row = body->makeChild<Ling::Node>();
			row->setWidthPercent(100.f);
			row->setHeight(btnSize);
			row->setFlexShrink(0.f);
			row->setFlexDirection(Ling::FlexDirection::Row);
			row->setAlignItems(Ling::Align::Center);
		}
		const auto& tool = buttons[i];
		if (tool.id == L"spliter") {
			auto spliter = row->makeChild<Ling::Node>();
			spliter->setSize(spliterW, 18.f);
			spliter->setBg(0xDDDDDDff);
			continue;
		}
		auto btn = row->makeChild<Ling::Button>();
		btn->setId(tool.id);
		btn->setText(tool.icon);
		btn->setWidth(btnSize);
		btn->setHeightPercent(100.f);
		btn->setHoverBg(0xF2F2F2ff);
		btn->setFontFamily(L"icon");
		btn->setFontSize(13.f);
		btn->onClick.add([this](Ling::Button* btn) { onClick(btn); });
		if (!tool.label.empty()) {
			tip->bind(btn, Lang::get(tool.label));
		}
	}
}

void ToolCap::onClick(Ling::Button* btn)
{
	// 提示框跟着按钮所在的窗口走，这里马上要换阶段或者关窗口，先把它收掉
	tip->hide();
	if (auto tool = findCaptureTool(btn->id); tool && tool->drawing) {
		win->startPin(tool->id);
		return;
	}
	if (btn->id == L"mark") {
		win->startPin();
	}
	else if (btn->id == L"long") {
		win->startLong();
	}
	else if (btn->id == L"video") {
		win->startVideo();
	}
	else if (btn->id == L"ocr") {
		win->startOcr();
	}
	else if (btn->id == L"qrcode") {
		win->startQrcode();
	}
	else if (btn->id == L"save") {
		win->saveToFile();
	}
	else if (btn->id == L"clipboard") {
		win->copyToClipboard();
	}
	else if (btn->id == L"close") {
		win->close();
	}
}

void ToolCap::onMinMaxInfo(MINMAXINFO* mmi)
{
	mmi->ptMinTrackSize.x = 1;
	mmi->ptMinTrackSize.y = 1;
}
