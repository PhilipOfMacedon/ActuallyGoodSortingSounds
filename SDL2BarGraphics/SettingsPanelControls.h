#pragma once

#include <string>
#include <vector>

#include "SettingsPanelModel.h"

struct SettingsPanelAlgorithmPresentation
{
	std::string familyId;
	std::string variantId;
	bool showsMergeBufferVisualization;
	std::vector<std::string> optionGroupIds;
	std::vector<std::string> visibleAudioControlIds;

	SettingsPanelAlgorithmPresentation()
		: familyId(), variantId(), showsMergeBufferVisualization(false), optionGroupIds(), visibleAudioControlIds() {}
};

std::string settingsAlgorithmOptionControlId(const std::string& familyId,
	const std::string& variantId, const std::string& groupId);
std::string settingsAlgorithmOptionCaption(const std::string& groupId);
std::vector<std::string> settingsPanelControlIds(SettingsPanelPage page, SettingsAudioGroup audioGroup,
	const SettingsPanelAlgorithmPresentation* selectedAlgorithm);
