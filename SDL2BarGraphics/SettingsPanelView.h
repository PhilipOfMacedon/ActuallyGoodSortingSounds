#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "UiArchitecture.h"
#include "SettingsPanelModel.h"

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
	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }
	std::map<std::string, std::string>& captions() { return captions_; }
	const std::map<std::string, std::string>& captions() const { return captions_; }

	SettingsPanelPage page() const { return model_.page(); }
	int pageIndex() const { return model_.pageIndex(); }
	SettingsAudioGroup audioGroup() const { return model_.audioGroup(); }
	bool isPage(SettingsPanelPage value) const { return model_.isPage(value); }
	float scrollOffset() const { return model_.scrollOffset(); }
	void configureScroll(const UiRect& viewport, float contentExtent, float rowExtent, float requestedOffset);
	UiRect scrollItemBounds(std::size_t index, float itemExtent) const;
	bool scrollItemIntersectsViewport(std::size_t index, float itemExtent) const;
	void selectAudioBand(int index, int bandCount);
	bool handleNavigation(UiAction::ViewCommand command);
	bool isScrollablePage() const;
	bool handleScrollWheel(float x, float y, int wheelDelta);

	void compose(const SettingsPanelChrome& chrome, const SettingsPanelPalette& palette,
		UiPaintContext& paintContext) const;

private:
	UIRoot controls_;
	SettingsPanelModel model_;
	std::map<std::string, std::string> captions_;
};
