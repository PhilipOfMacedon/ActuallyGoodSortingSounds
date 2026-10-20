#pragma once

#include <vector>

#include "SettingsPanelModel.h"
#include "UiLayer.h"

struct SettingsPanelGeometry
{
	UiRect tabs[3];
	UiRect audioGroups[2];
	UiRect content;
	UiRect closeButton;
	UiRect audioRetryButton;
};

struct SettingsWidgetGeometry
{
	UiRect bounds;
	std::vector<UiRect> interactiveSubregions;
	bool setPopupAbove;
	bool popupAbove;
	SettingsWidgetGeometry() : bounds(), interactiveSubregions(), setPopupAbove(false), popupAbove(false) {}
};

SettingsPanelGeometry calculateSettingsPanelGeometry(const UiRect& panelBounds, SettingsPanelPage page);
SettingsWidgetGeometry calculateSettingsWidgetGeometry(const UiWidget& widget, const UiRect& rowBounds,
	float x, float width, const UiRect& panelBounds, float scrollbarThickness);
