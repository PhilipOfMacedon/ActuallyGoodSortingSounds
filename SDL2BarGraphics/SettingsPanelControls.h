#pragma once

#include <string>
#include <vector>

#include "SettingsPanelModel.h"
#include "../SortingLogic/SortingLogic.h"

std::string settingsAlgorithmOptionControlId(const sortinglogic::AlgorithmDescriptor& algorithm,
	const std::string& groupId);
std::string settingsAlgorithmOptionCaption(const std::string& groupId);
std::vector<std::string> settingsPanelControlIds(SettingsPanelPage page, SettingsAudioGroup audioGroup,
	const sortinglogic::AlgorithmDescriptor* selectedAlgorithm);
