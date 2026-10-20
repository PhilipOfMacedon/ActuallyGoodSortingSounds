#pragma once

#include <string>
#include <vector>

#include "SettingsPanelControls.h"

namespace sortinglogic
{
	struct AlgorithmDescriptor;
}

// Converts sorting registry capabilities into values used by the settings UI.
SettingsPanelAlgorithmPresentation settingsPanelAlgorithmPresentation(
	const sortinglogic::AlgorithmDescriptor* algorithm);

std::vector<std::string> settingsPaletteOptions();
std::vector<std::string> settingsUiPresetOptions();
std::vector<std::string> settingsBarSpacingOptions();
std::vector<std::string> settingsInputDomainOptions();
std::vector<std::string> settingsInputPatternOptions();
std::vector<std::string> settingsBooleanOptions();
