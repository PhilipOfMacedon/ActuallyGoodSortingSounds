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

std::string appearanceTokenLabel(const UiTokenDescriptor& descriptor);
