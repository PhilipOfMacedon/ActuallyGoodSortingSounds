#pragma once

#include "UiLayer.h"

namespace ui
{
	UiAction mainMenuSelectionAction(const std::string& menuId, int selection);
	AppAction normalizeCatalogAction(const AppAction& action);
	AppAction mainControlActivationAction(const UiEvent& event);
	float stepDeltaWithModifiers(float baseStep, bool control, bool shift);
}
