#pragma once

#include <string>

#include "UiArchitecture.h"

struct SettingsPanelChrome
{
	int screenWidth;
	int screenHeight;
	UiRect panelBounds;
	UiRect contentBounds;
	UiRect footerBounds;
	std::string title;
	std::string footer;
	SettingsPanelChrome();
};

struct SettingsPanelPalette
{
	UiColor panel;
	UiColor accent;
	UiColor text;
	UiColor mutedText;
	UiColor background;
	UiColor overlayDim;
};

// Paints the shared settings-window chrome. Interactive widgets remain owned
// by ControlPanel, while layout and fixed chrome use the common paint contract.
class SettingsPanelView
{
public:
	void compose(const SettingsPanelChrome& chrome, const SettingsPanelPalette& palette,
		UiPaintContext& paintContext) const;
};
