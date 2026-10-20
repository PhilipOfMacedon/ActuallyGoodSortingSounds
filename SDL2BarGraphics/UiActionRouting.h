#pragma once

#include "SettingsPanelModel.h"
#include "UiLayer.h"

namespace ui
{
	enum class SettingsNavigationKind { None, Page, AudioGroup };

	struct SettingsNavigation
	{
		SettingsNavigationKind kind;
		SettingsPanelPage page;
		SettingsAudioGroup audioGroup;
		SettingsNavigation() : kind(SettingsNavigationKind::None), page(SettingsPanelPage::Appearance),
			audioGroup(SettingsAudioGroup::Basic) {}
	};

	UiAction mainMenuSelectionAction(const std::string& menuId, int selection);
	AppAction normalizeCatalogAction(const AppAction& action);
	AppAction mainControlActivationAction(const UiEvent& event);
	float stepDeltaWithModifiers(float baseStep, bool control, bool shift);
	SettingsNavigation settingsNavigationFor(UiAction::ViewCommand command);
}
