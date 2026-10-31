#pragma once

#include <string>
#include <vector>

#include "UiAppearance.h"

struct AppearanceTokenOption
{
	std::string stableId;
	std::string label;
	AppearanceTokenOption(const std::string& id = std::string(), const std::string& displayedLabel = std::string())
		: stableId(id), label(displayedLabel) {}
};

// Platform-neutral search and selection state for the appearance token picker.
// The owning view remains responsible for widget synchronization and input APIs.
class AppearanceEditorModel
{
public:
	AppearanceEditorModel();
	void begin(UiPresetId preset);
	void setPreset(UiPresetId preset);
	UiPresetId preset() const { return preset_; }
	void setCategoryFilter(int value);
	int categoryFilter() const { return categoryFilter_; }
	void setSearch(const std::string& value);
	void appendSearch(const std::string& value);
	void eraseSearchCharacter();
	void clearSearch();
	const std::string& search() const { return search_; }
	void selectOption(int index);
	const std::string& selectedToken() const { return selectedToken_; }
	int selectedIndex() const;
	const std::vector<AppearanceTokenOption>& options() const { return options_; }

private:
	UiPresetId preset_;
	int categoryFilter_;
	std::string search_;
	std::string selectedToken_;
	std::vector<AppearanceTokenOption> options_;
	void refresh();
};

// Owns the transactional state of one appearance-editing session. The view can
// preview a draft without changing persisted preferences, then either restore
// the original value or accept the committed value after the app controller
// reports a successful save.
class AppearanceEditorSession
{
public:
	AppearanceEditorSession();
	void begin(const UiAppearancePreferences& current);
	void restore();
	void accept(const UiAppearancePreferences& committed);
	bool dirty() const { return !sameUiAppearancePreferences(original_, draft_); }

	AppearanceEditorModel& model() { return model_; }
	const AppearanceEditorModel& model() const { return model_; }
	UiAppearancePreferences& draft() { return draft_; }
	const UiAppearancePreferences& draft() const { return draft_; }
	std::string& error() { return error_; }
	const std::string& error() const { return error_; }

private:
	UiAppearancePreferences original_;
	UiAppearancePreferences draft_;
	AppearanceEditorModel model_;
	std::string error_;
};

std::string appearanceTokenLabel(const UiTokenDescriptor& descriptor);
