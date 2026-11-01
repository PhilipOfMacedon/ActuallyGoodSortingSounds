#include "UiActionRouting.h"

#include "../SortingLogic/SortingLogic.h"

namespace ui
{
	UiAction mainMenuSelectionAction(const std::string& menuId, int selection)
	{
		if (menuId == "menu_file")
		{
			if (selection == 1) return UiAction(AppAction(AppActionKind::BuildTrace));
			if (selection == 2) return UiAction(AppAction(AppActionKind::GenerateAndPlay));
			if (selection == 3) return UiAction(AppAction(AppActionKind::Reset));
		}
		else if (menuId == "menu_view")
		{
			if (selection == 1) return UiAction(AppAction(AppActionKind::TogglePanelExpanded));
			if (selection == 2) return UiAction(AppAction(AppActionKind::ToggleBorderlessFullscreen));
		}
		else if (menuId == "menu_tools")
		{
			if (selection == 1) return UiAction(AppAction(), UiAction::ViewCommand::OpenAlgorithmMenu);
			if (selection == 2) return UiAction(AppAction(), UiAction::ViewCommand::OpenSettings);
			if (selection == 3) return UiAction(AppAction(), UiAction::ViewCommand::OpenAppearanceEditor);
			if (selection == 4) return UiAction(AppAction(AppActionKind::OpenDemoEditor));
		}
		return UiAction();
	}

	AppAction normalizeCatalogAction(const AppAction& action)
	{
		if (action.kind == AppActionKind::SelectAlgorithm)
		{
			const sortinglogic::AlgorithmDescriptor* descriptor = nullptr;
			if (!action.familyId.empty() || !action.variantId.empty())
				descriptor = sortinglogic::algorithmDescriptorById(
					sortinglogic::FamilyId(action.familyId), sortinglogic::VariantId(action.variantId));
			else if (action.selection >= 0 && static_cast<std::size_t>(action.selection) < sortinglogic::algorithmCatalog().size())
				descriptor = &sortinglogic::algorithmCatalog()[static_cast<std::size_t>(action.selection)];
			return descriptor
				? AppAction(AppActionKind::SelectAlgorithm, descriptor->familyId, descriptor->variantId)
				: AppAction();
		}

		if (action.kind == AppActionKind::SetAlgorithmOption)
		{
			const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(
				sortinglogic::FamilyId(action.familyId), sortinglogic::VariantId(action.variantId));
			if (!descriptor) return AppAction();
			const std::string requested = !action.optionStableId.empty() ? action.optionStableId : action.text;
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			{
				const sortinglogic::AlgorithmOptionDescriptor& property = descriptor->supportedOptions[i];
				if (action.optionGroupId != property.groupId) continue;
				if (property.valueType == sortinglogic::AlgorithmPropertyValueType::BoundedInteger ||
					property.valueType == sortinglogic::AlgorithmPropertyValueType::FiniteDecimal)
					return requested.empty() ? AppAction() : AppAction(AppActionKind::SetAlgorithmOption,
						descriptor->familyId, descriptor->variantId, action.optionGroupId, requested, 0);
				break;
			}

			int optionIndex = 0;
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			{
				const sortinglogic::AlgorithmOptionDescriptor& option = descriptor->supportedOptions[i];
				if (action.optionGroupId != option.groupId) continue;
				const bool stableIdMatches = !requested.empty() && requested == option.stableId;
				const bool selectionMatches = requested.empty() && optionIndex == action.selection;
				if (stableIdMatches || selectionMatches)
					return AppAction(AppActionKind::SetAlgorithmOption, descriptor->familyId, descriptor->variantId,
						action.optionGroupId, option.stableId, option.value);
				++optionIndex;
			}
			return AppAction();
		}

		return action;
	}

	float stepDeltaWithModifiers(float baseStep, bool control, bool shift)
	{
		if (control == shift) return baseStep;
		return baseStep * (shift ? 10.0f : 0.1f);
	}

	AppAction mainControlActivationAction(const UiEvent& event)
	{
		if (event.kind != UiEventKind::Activated) return AppAction();
		if (event.controlId == "bars_down" || event.controlId == "bars_up")
		{
			const float direction = event.controlId == "bars_down" ? -1.0f : 1.0f;
			return AppAction(AppActionKind::AdjustItemCount,
				direction * stepDeltaWithModifiers(10.0f, event.control, event.shift));
		}
		if (event.controlId == "time_down" || event.controlId == "time_up")
		{
			const float direction = event.controlId == "time_down" ? -1.0f : 1.0f;
			return AppAction(AppActionKind::AdjustDuration,
				direction * stepDeltaWithModifiers(1.0f, event.control, event.shift));
		}
		return AppAction();
	}

	SettingsNavigation settingsNavigationFor(UiAction::ViewCommand command)
	{
		SettingsNavigation navigation;
		switch (command)
		{
		case UiAction::ViewCommand::ShowAudioBasic:
			navigation.kind = SettingsNavigationKind::AudioGroup;
			navigation.audioGroup = SettingsAudioGroup::Basic;
			break;
		case UiAction::ViewCommand::ShowAudioAdvanced:
			navigation.kind = SettingsNavigationKind::AudioGroup;
			navigation.audioGroup = SettingsAudioGroup::Advanced;
			break;
		case UiAction::ViewCommand::ShowAppearanceSettings:
			navigation.kind = SettingsNavigationKind::Page;
			navigation.page = SettingsPanelPage::Appearance;
			break;
		case UiAction::ViewCommand::ShowAlgorithmSettings:
			navigation.kind = SettingsNavigationKind::Page;
			navigation.page = SettingsPanelPage::Algorithm;
			break;
		case UiAction::ViewCommand::ShowAudioSettings:
		case UiAction::ViewCommand::ShowGeneralAudioSettings:
			navigation.kind = SettingsNavigationKind::Page;
			navigation.page = SettingsPanelPage::GeneralAudio;
			break;
		default:
			break;
		}
		return navigation;
	}
}
