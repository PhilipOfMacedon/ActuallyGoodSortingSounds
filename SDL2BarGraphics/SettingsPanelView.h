#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "UiArchitecture.h"
#include "SettingsPanelModel.h"

struct UserPreferences;

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

// Owns retained settings controls and session navigation/scroll state. Renderer
// resources remain in the platform adapter; the view submits shared paint commands.
class SettingsPanelView
{
public:
	void initializeControls(const UserPreferences& preferences);
	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }
	std::map<std::string, std::string>& captions() { return captions_; }
	const std::map<std::string, std::string>& captions() const { return captions_; }

	SettingsPanelPage page() const { return model_.page(); }
	int pageIndex() const { return model_.pageIndex(); }
	SettingsAudioGroup audioGroup() const { return model_.audioGroup(); }
	bool isPage(SettingsPanelPage value) const { return model_.isPage(value); }
	float scrollOffset() const { return model_.scrollOffset(); }
	void selectAudioBand(int index, int bandCount);
	bool handleNavigation(UiAction::ViewCommand command);
	bool isScrollablePage() const;
	bool handleScrollWheel(float x, float y, int wheelDelta);
	void setPanelBounds(const UiRect& panelBounds, bool modalOpen);
	void setModalActive(bool active);
	const UiRect& panelBounds() const { return panelBounds_; }
	const UiRect& contentBounds() const { return contentBounds_; }
	void layoutControls(const std::vector<std::string>& activeControlIds, float scrollbarThickness);

	void compose(const SettingsPanelChrome& chrome, const SettingsPanelPalette& palette,
		UiPaintContext& paintContext) const;

private:
	UIRoot controls_;
	SettingsPanelModel model_;
	std::map<std::string, std::string> captions_;
	UiRect panelBounds_;
	UiRect contentBounds_;
};
