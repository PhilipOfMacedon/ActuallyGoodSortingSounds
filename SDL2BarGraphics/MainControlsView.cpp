#include "MainControlsView.h"

#include <vector>

#include "AppActions.h"
#include "SettingsPanelCatalog.h"
#include "UiActionRouting.h"
#include "UserSettings.h"

void MainControlsView::initializeControls(const UserPreferences& preferences, const std::string& algorithmLabel)
{
	controls_.addButton("panel_toggle", ">>", AppAction(AppActionKind::TogglePanelExpanded));
	controls_.addButton("settings", "[=]", AppAction(), UiAction::ViewCommand::OpenSettings);
	controls_.addComboBox("menu_file", std::vector<std::string>{ "File", "Build Trace", "Generate & Play", "Reset" }, 0);
	controls_.addComboBox("menu_view", std::vector<std::string>{ "View", "Toggle Setup", "Borderless Fullscreen" }, 0);
	controls_.addComboBox("menu_tools", std::vector<std::string>{ "Tools", "Algorithm Browser...", "Settings...", "Appearance..." }, 0);
	controls_.find("menu_file")->setOptionEnabled(0, false);
	controls_.find("menu_view")->setOptionEnabled(0, false);
	controls_.find("menu_tools")->setOptionEnabled(0, false);
	controls_.find("menu_file")->setPopupAbove(false);
	controls_.find("menu_view")->setPopupAbove(false);
	controls_.find("menu_tools")->setPopupAbove(false);
	controls_.addButton("shuffle", "Shuffle", AppAction(AppActionKind::Shuffle));
	controls_.addButton("insta_sort", "Insta-sort", AppAction(AppActionKind::InstaSort));
	controls_.addButton("insta_invert", "Insta-invert", AppAction(AppActionKind::InstaInvert));
	controls_.addButton("algorithm", algorithmLabel, AppAction(), UiAction::ViewCommand::OpenAlgorithmMenu);
	controls_.addComboBox("direction", std::vector<std::string>{ "Ascending", "Descending" },
		preferences.simulation.descending ? 1 : 0, AppAction(AppActionKind::SetDirection));
	controls_.addButton("run", "Build Trace", AppAction(AppActionKind::BuildTrace));
	controls_.addButton("play", "Generate & Play", AppAction(AppActionKind::GenerateAndPlay));
	controls_.addButton("reset", "Reset", AppAction(AppActionKind::Reset));
	controls_.addButton("previous", "< Event", AppAction(AppActionKind::PreviousEvent));
	controls_.addButton("next", "Event >", AppAction(AppActionKind::NextEvent));
	controls_.addButton("bars_down", "Bars --", AppAction(AppActionKind::AdjustItemCount, -10.0f));
	controls_.addButton("bars_value", "80 bars");
	controls_.addButton("bars_up", "Bars ++", AppAction(AppActionKind::AdjustItemCount, 10.0f));
	controls_.addButton("time_down", "Time --", AppAction(AppActionKind::AdjustDuration, -1.0f));
	controls_.addButton("time_value", "10 sec");
	controls_.addButton("time_up", "Time ++", AppAction(AppActionKind::AdjustDuration, 1.0f));
	controls_.addComboBox("domain", settingsInputDomainOptions(), static_cast<int>(preferences.simulation.domain),
		AppAction(AppActionKind::SetInputDomain));
	controls_.addComboBox("pattern", settingsInputPatternOptions(), static_cast<int>(preferences.simulation.pattern),
		AppAction(AppActionKind::SetInputPattern));
	controls_.addButton("seed_readout", "Seed");
	controls_.addButton("start_overlay", "Generate & Play", AppAction(AppActionKind::GenerateAndPlay));

	controls_.find("panel_toggle")->setRole(UiControlRole::PanelToggle);
	controls_.find("settings")->setRole(UiControlRole::Settings);
	controls_.find("algorithm")->setRole(UiControlRole::AlgorithmChoice);
	controls_.find("run")->setRole(UiControlRole::BuildTrace);
	controls_.find("play")->setRole(UiControlRole::Playback);
	controls_.find("reset")->setRole(UiControlRole::Reset);
	controls_.find("bars_value")->setRole(UiControlRole::EditableValue);
	controls_.find("time_value")->setRole(UiControlRole::EditableValue);
	controls_.find("start_overlay")->setRole(UiControlRole::Playback);
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
		if (controls_.widgets()[i]->kind() == UiWidgetKind::ComboBox)
			controls_.widgets()[i]->setRole(UiControlRole::Choice);
}
