#include "AppearanceEditorModel.h"

#include <algorithm>

#include "TextSearch.h"

std::string appearanceTokenLabel(const UiTokenDescriptor& descriptor)
{
	return std::string(uiTokenCategoryName(descriptor.category)) + " | " + descriptor.description + " [" + descriptor.stableId + "]";
}

AppearanceEditorModel::AppearanceEditorModel()
	: preset_(UiPresetId::Current), categoryFilter_(0), search_(), selectedToken_(), options_()
{
	refresh();
}

void AppearanceEditorModel::begin(UiPresetId preset)
{
	search_.clear();
	setPreset(preset);
}

void AppearanceEditorModel::setPreset(UiPresetId preset)
{
	preset_ = preset;
	refresh();
}

void AppearanceEditorModel::setCategoryFilter(int value)
{
	categoryFilter_ = std::max(0, std::min(value, 4));
	refresh();
}

void AppearanceEditorModel::setSearch(const std::string& value)
{
	search_ = value;
	refresh();
}

void AppearanceEditorModel::appendSearch(const std::string& value)
{
	if (search_.size() < 96) search_ += value;
	refresh();
}

void AppearanceEditorModel::eraseSearchCharacter()
{
	if (!search_.empty()) search_.erase(search_.size() - 1);
	refresh();
}

void AppearanceEditorModel::clearSearch()
{
	search_.clear();
	refresh();
}

void AppearanceEditorModel::selectOption(int index)
{
	if (index >= 0 && static_cast<std::size_t>(index) < options_.size())
		selectedToken_ = options_[static_cast<std::size_t>(index)].stableId;
}

int AppearanceEditorModel::selectedIndex() const
{
	for (std::size_t i = 0; i < options_.size(); ++i)
		if (options_[i].stableId == selectedToken_) return static_cast<int>(i);
	return options_.empty() ? -1 : 0;
}

void AppearanceEditorModel::refresh()
{
	const UiPresetDefinition& definition = uiPresetDefinition(preset_);
	options_.clear();
	for (std::size_t i = 0; i < definition.tokens.size(); ++i)
	{
		const UiTokenDescriptor& descriptor = definition.tokens[i];
		if (categoryFilter_ > 0 && static_cast<int>(descriptor.category) != categoryFilter_ - 1) continue;
		const std::string searchable = std::string(uiTokenCategoryName(descriptor.category)) + " " +
			descriptor.description + " " + descriptor.stableId;
		if (!containsTextInsensitive(searchable, search_)) continue;
		options_.push_back(AppearanceTokenOption(descriptor.stableId, appearanceTokenLabel(descriptor)));
	}
	if (options_.empty()) selectedToken_.clear();
	else
	{
		bool selectedExists = false;
		for (std::size_t i = 0; i < options_.size(); ++i)
			if (options_[i].stableId == selectedToken_) selectedExists = true;
		if (!selectedExists) selectedToken_ = options_[0].stableId;
	}
}
