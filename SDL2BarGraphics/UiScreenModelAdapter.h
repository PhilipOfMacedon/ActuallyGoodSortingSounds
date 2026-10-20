#pragma once

#include <vector>

#include "UiArchitecture.h"
#include "UserSettings.h"
#include "../SortingLogic/SortingLogic.h"

// Translates simulation snapshots and saved preferences into UI-owned values.
// General UI layout and paint code depend on UiArchitecture, not SortingLogic.
UiScreenModel buildUiScreenModel(const sortinglogic::RenderSnapshot& snapshot,
	const UserPreferences& preferences, bool playbackPending, const std::string& applicationStatus = std::string());

// Expose semantic roles on the main chart lane when a workspace is hidden.
std::vector<sortinglogic::ActiveRole> rolesVisibleOnMainArray(const sortinglogic::RenderSnapshot& snapshot);
