#pragma once
#include <include/Ling.h>
#include "ToolRegistry.h"

class WinCap;
class Tip;
// 框选完成后出现在选区右下方的工具条。往右走是图像标记 / 截长图 / 屏幕录制 /
// 文字识别 / 二维码识别，竖线之后是关闭 / 存文件 / 存剪切板。
class ToolCap : public Ling::WinBase
{
public:
	ToolCap(WinCap* win);
	~ToolCap();
private:
	void onCreated() override;
	void onMinMaxInfo(MINMAXINFO* mmi) override;
	void onClick(Ling::Button* btn);
	// 按当前 dpi 把窗口尺寸算出来并应用。构造时算一次，DPI 变了再算一次
	void refreshSize();
	void buildButtons();
private:
	WinCap* win;
	// onDpiChanged 与 onSizeChanged 之间的接力标记，见构造函数里的注释
	bool dpiChanged{ false };

	// 悬停提示。要 hwnd，所以在 onCreated 里才建得起来
	std::unique_ptr<Tip> tip;
	std::vector<CaptureToolDefinition> buttons;
	std::vector<size_t> rowStarts;
	// 以下都是逻辑像素，交给 Ling 的 setter 时由其内部乘 dpi
	static constexpr float btnSize{ 32.f };
	static constexpr float spliterW{ 1.f };
};
