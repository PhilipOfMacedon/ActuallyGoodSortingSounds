#pragma once

#include "SettingsPanelControls.h"

namespace sortinglogic
{
	struct AlgorithmDescriptor;
}

// Converts sorting registry capabilities into values used by the settings UI.
SettingsPanelAlgorithmPresentation settingsPanelAlgorithmPresentation(
	const sortinglogic::AlgorithmDescriptor* algorithm);
