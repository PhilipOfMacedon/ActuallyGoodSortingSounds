#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "UiArchitecture.h"

struct UserPreferences;
struct AppAction;
class InlineEditModel;
class DemoRunner;
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
	void synchronizeDemoTransport(const DemoRunner& runner);
	void compose(MainControlsPaintSection section, const UiRect& screen, UiPaintContext& paintContext) const;
	const UiWidget* findControl(const std::string& id) const { return controls_.find(id); }
	std::size_t controlCount() const { return controls_.widgets().size(); }
	const UiWidget* controlAt(std::size_t index) const
	{
		return index < controls_.widgets().size() ? controls_.widgets()[index].get() : nullptr;
	}
	std::vector<std::string> controlIds() const;
	bool handleInput(const UiInputEvent& event, UiAction& action) { return controls_.handleEvent(event, action); }
	bool hasOpenCombo() const { return controls_.hasOpenCombo(); }
	void closeCombo() { controls_.closeCombo(); }
	void setInputCaptured(bool captured) { controls_.setInputCaptured(captured); }
	void resetMenuSelection(const std::string& id);
	void setControlVisible(const std::string& id, bool visible) { controls_.setWidgetVisible(id, visible); }
	void setControlEnabled(const std::string& id, bool enabled) { controls_.setWidgetEnabled(id, enabled); }
	void setControlLabel(const std::string& id, const std::string& label);
	void setControlActionAndPresentation(const std::string& id, const AppAction& action,
		const std::string& label, bool enabled);
	bool beginInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds, std::uint32_t seed,
		std::string& initialText) const;
	bool beginInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds,
		std::string& initialText) const
	{
		return beginInlineValueEdit(target, itemCount, durationSeconds, 0u, initialText);
	}
	void showInlineValueEdit(InlineEditTarget target, const std::string& text);
	void restoreInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds, std::uint32_t seed);
	void restoreInlineValueEdit(InlineEditTarget target, int itemCount, float durationSeconds)
	{
		restoreInlineValueEdit(target, itemCount, durationSeconds, 0u);
	}
	bool commitInlineValueEdit(InlineEditTarget target, const InlineEditModel& editor, AppAction& action) const;
	void setSeedBookmarks(const std::vector<std::string>& labels, std::size_t selectedIndex);
	void setSeedHistoryNavigation(bool canPrevious, bool canNext);

	const MainControlsText& text() const { return text_; }

private:
	UIRoot controls_;
	MainControlsText text_;
	std::uint32_t lastLiveTextUpdateMs_;
};
