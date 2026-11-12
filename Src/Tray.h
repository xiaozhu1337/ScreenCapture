#pragma once
#include <include/Ling.h>
#include "Desktop/TrayIcon.h"

class Tray
{
public:
	~Tray();
	static void init();
	static Tray* get();
	static void dispose();
private:
	Tray();
	void onTrayRightClick();
	std::unique_ptr<TrayIcon> icon;
};
