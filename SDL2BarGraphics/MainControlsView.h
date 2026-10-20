#pragma once

#include <string>

#include "UiArchitecture.h"

struct UserPreferences;
namespace sortinglogic
{
	struct RenderSnapshot;
}

enum class MainControlsPaintSection { HeaderButtons, Menus, ExpandedControls, CollapsedStart, Popups };

// Owns the retained main-control tree and the typed actions emitted by it.
// SDL input translation, responsive placement, and renderer resources stay in
// the application adapter.
class MainControlsView
{
public:
	void initializeControls(const UserPreferences& preferences, const std::string& algorithmLabel);
	void applyLayout(UiPresetId preset, const UiLayoutResult& layout, int logicalWidth, bool expanded);
	void synchronize(const UserPreferences& preferences, UiPresetId activePreset,
		const sortinglogic::RenderSnapshot& snapshot, const UiScreenModel& screenModel,
		const std::string& algorithmLabel);
	void compose(MainControlsPaintSection section, const UiRect& screen, UiPaintContext& paintContext) const;

	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }

private:
	UIRoot controls_;
};
