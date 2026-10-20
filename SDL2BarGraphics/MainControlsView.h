#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "UiArchitecture.h"

struct UserPreferences;
struct AppAction;
class InlineEditModel;
enum class InlineEditTarget;
namespace sortinglogic
{
	struct RenderSnapshot;
}

enum class MainControlsPaintSection { HeaderButtons, Menus, ExpandedControls, CollapsedStart, Popups };

struct MainControlsText
{
	std::string title;
	std::string detail;
	std::string status;
	std::string collapsedSummary;

	MainControlsText();
};

// Owns the retained main-control tree and the typed actions emitted by it.
// SDL input translation, responsive placement, and renderer resources stay in
// the application adapter.
class MainControlsView
{
public:
	MainControlsView();
	void initializeControls(const UserPreferences& preferences, const std::string& algorithmLabel);
	void applyLayout(UiPresetId preset, const UiLayoutResult& layout, int logicalWidth, bool expanded);
	void layoutComboPopups(int logicalWidth, int logicalHeight,
		const std::function<float(const std::string&)>& measureOptionWidth);
	void synchronize(const UserPreferences& preferences, UiPresetId activePreset,
		const sortinglogic::RenderSnapshot& snapshot, const UiScreenModel& screenModel,
		const std::string& algorithmLabel);
	void synchronizeText(const UserPreferences& preferences, UiPresetId activePreset,
		const sortinglogic::RenderSnapshot& snapshot, const UiScreenModel& screenModel, std::uint32_t nowMs);
	void compose(MainControlsPaintSection section, const UiRect& screen, UiPaintContext& paintContext) const;
	bool beginInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds,
		std::string& initialText) const;
	void showInlineValueEdit(InlineEditTarget target, const std::string& text);
	void restoreInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds);
	bool commitInlineValueEdit(InlineEditTarget target, const InlineEditModel& editor, AppAction& action) const;

	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }
	const MainControlsText& text() const { return text_; }

private:
	UIRoot controls_;
	MainControlsText text_;
	std::uint32_t lastLiveTextUpdateMs_;
};
