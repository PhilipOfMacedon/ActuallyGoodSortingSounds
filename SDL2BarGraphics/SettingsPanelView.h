#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "UiArchitecture.h"
#include "SettingsPanelModel.h"

struct UserPreferences;
namespace sortinglogic
{
	struct AlgorithmDescriptor;
	struct RenderSnapshot;
}

struct SettingsPanelPalette
{
	UiColor panel;
	UiColor accent;
	UiColor button;
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
	SettingsPanelView();
	void initializeControls(const UserPreferences& preferences);
	void synchronizeControls(const UserPreferences& preferences, const UserPreferences& renderPreferences,
		const sortinglogic::RenderSnapshot& snapshot, bool setupBusy);
	void synchronizeAudioControls(const UserPreferences& preferences,
		const sortinglogic::AlgorithmDescriptor* algorithm);
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
	bool handleScrollbarPointerDown(float x, float y);
	bool handleScrollbarPointerMove(float x, float y);
	bool handleScrollbarPointerUp(float x, float y);
	bool scrollbarCaptured() const { return scrollbarController_.captured(); }
	const UiScrollbar& scrollbar() const { return scrollbarController_; }
	void cancelScrollbarPointer() { scrollbarController_.cancelCapture(); }
	void setPanelBounds(const UiRect& panelBounds, bool modalOpen);
	void setModalActive(bool active);
	const UiRect& panelBounds() const { return panelBounds_; }
	const UiRect& contentBounds() const { return contentBounds_; }
	void layoutForAlgorithmSelection(const std::string& familyId, const std::string& variantId,
		float scrollbarThickness);
	void layoutControls(const std::vector<std::string>& activeControlIds, float scrollbarThickness);

	void compose(int screenWidth, int screenHeight, const std::string& title, const std::string& footer,
		const SettingsPanelPalette& palette, UiPaintContext& paintContext) const;

private:
	struct AlgorithmOptionBinding
	{
		std::string controlId;
		std::string groupId;
	};

	UIRoot controls_;
	SettingsPanelModel model_;
	std::map<std::string, std::string> captions_;
	std::map<int, const sortinglogic::AlgorithmDescriptor*> algorithmDescriptorsById_;
	std::map<int, std::vector<AlgorithmOptionBinding> > algorithmOptionsById_;
	std::vector<std::string> activeAlgorithmOptionControlIds_;
	int activeAlgorithmOptionId_;
	UiRect panelBounds_;
	UiRect contentBounds_;
	UiRect scrollTrackBounds_;
	UiScrollbar scrollbarController_;
};
