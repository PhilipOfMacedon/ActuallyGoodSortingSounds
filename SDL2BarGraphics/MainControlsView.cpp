#include "MainControlsView.h"

#include <map>
#include <vector>

#include "AppActions.h"
#include "SettingsPanelCatalog.h"
#include "UiActionRouting.h"
#include "UserSettings.h"
#include "../SortingLogic/SortingLogic.h"

namespace
{
	void submitControl(const UIRoot& controls, UiPaintContext& paintContext, const std::string& id,
		UiPaintLayer layer, UiPaintMaterial material, const UiRect& screen, bool popup = false)
	{
		const UiWidget* widget = controls.find(id);
		if (!widget || !widget->visible()) return;
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Control, widget->bounds(), UiColor(), std::string(),
			id, material, layer, screen, true, 0.0f, popup));
	}
}

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

void MainControlsView::applyLayout(UiPresetId preset, const UiLayoutResult& layout, int logicalWidth, bool expanded)
{
	controls_.setFocusOrder(layout.focusOrder);
	for (std::map<std::string, UiRect>::const_iterator bounds = layout.controlBounds.begin();
		bounds != layout.controlBounds.end(); ++bounds)
	{
		UiWidget* widget = controls_.find(bounds->first);
		if (widget) widget->setBounds(bounds->second);
	}
	UiWidget* direction = controls_.find("direction");
	direction->setPresentation(preset != UiPresetId::Current
		? UiWidgetPresentation::RadioGroup : UiWidgetPresentation::Standard);
	const std::map<std::string, std::vector<UiRect> >::const_iterator directionRegions =
		layout.interactiveSubregions.find("direction");
	direction->setInteractiveSubregions(directionRegions == layout.interactiveSubregions.end()
		? std::vector<UiRect>() : directionRegions->second);
	controls_.find("bars_value")->setPresentation(UiWidgetPresentation::EditableLabel);
	controls_.find("time_value")->setPresentation(UiWidgetPresentation::EditableLabel);

	if (!expanded)
	{
		for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
		{
			const std::string& id = controls_.widgets()[i]->id();
			const bool headerControl = (preset != UiPresetId::ClassicWindows95 && (id == "panel_toggle" || id == "settings")) ||
				(preset != UiPresetId::Current && (id == "menu_file" || id == "menu_view" || id == "menu_tools"));
			controls_.setWidgetVisible(id, headerControl || id == "start_overlay");
		}
	}
	else
	{
		for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
			controls_.setWidgetVisible(controls_.widgets()[i]->id(), true);
	}

	const bool hasClassicRail = preset != UiPresetId::Current;
	controls_.setWidgetVisible("menu_file", hasClassicRail);
	controls_.setWidgetVisible("menu_view", hasClassicRail);
	controls_.setWidgetVisible("menu_tools", hasClassicRail);
	controls_.setWidgetVisible("run", !hasClassicRail);
	controls_.setWidgetVisible("panel_toggle", preset != UiPresetId::ClassicWindows95);
	controls_.setWidgetVisible("settings", preset != UiPresetId::ClassicWindows95);
	const bool wideLayout = logicalWidth >= 900;
	controls_.setWidgetVisible("domain", hasClassicRail && wideLayout && expanded);
	controls_.setWidgetVisible("pattern", hasClassicRail && wideLayout && expanded);
	controls_.setWidgetVisible("seed_readout", hasClassicRail && wideLayout && expanded);
	controls_.setWidgetVisible("start_overlay", !expanded);
	controls_.find("panel_toggle")->setLabel(expanded ? "v" : ">");
}

void MainControlsView::synchronize(const UserPreferences& preferences, UiPresetId activePreset,
	const sortinglogic::RenderSnapshot& snapshot, const UiScreenModel& screenModel,
	const std::string& algorithmLabel)
{
	const bool busy = !screenModel.setupEnabled;
	const char* setupIds[] = { "shuffle", "insta_sort", "insta_invert", "algorithm", "direction", "run",
		"bars_down", "bars_up", "time_down", "time_up" };
	for (std::size_t i = 0; i < sizeof(setupIds) / sizeof(setupIds[0]); ++i)
		controls_.setWidgetEnabled(setupIds[i], !busy);
	controls_.setWidgetEnabled("run", screenModel.runEnabled);
	controls_.setWidgetEnabled("play", screenModel.playEnabled);
	controls_.find("algorithm")->setLabel(algorithmLabel);

	std::string playCaption = screenModel.playCaption;
	if (activePreset != UiPresetId::Current)
	{
		switch (screenModel.transport)
		{
		case UiTransportState::NoTrace: playCaption = "Play"; break;
		case UiTransportState::Ready: playCaption = "Play"; break;
		case UiTransportState::Playing: playCaption = "Pause"; break;
		case UiTransportState::Queued: playCaption = "Cancel"; break;
		case UiTransportState::Building: playCaption = "Queue Play"; break;
		case UiTransportState::Preparing: playCaption = "Preparing"; break;
		case UiTransportState::Replay: playCaption = "Replay"; break;
		}
	}
	controls_.find("play")->setLabel(playCaption);
	controls_.find("bars_value")->setLabel(std::to_string(snapshot.itemCount) + " bars");
	controls_.find("seed_readout")->setLabel("Seed " + std::to_string(snapshot.inputSeed));
	controls_.setWidgetEnabled("seed_readout", false);
	const bool referenceCadence = preferences.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence;
	controls_.setWidgetEnabled("bars_value", !busy);
	controls_.setWidgetEnabled("time_value", !busy && !referenceCadence);
	controls_.find("time_value")->setLabel(screenModel.durationCaption);
	controls_.find("direction")->setSelectedIndex(snapshot.direction == sortinglogic::SortDirection::Descending ? 1 : 0);
	controls_.find("domain")->setSelectedIndex(static_cast<int>(snapshot.domain));
	controls_.find("pattern")->setSelectedIndex(static_cast<int>(snapshot.pattern));
	controls_.setWidgetEnabled("domain", !busy);
	controls_.setWidgetEnabled("pattern", !busy);
	controls_.setWidgetVisible("start_overlay", !preferences.visual.panelExpanded && !snapshot.hasTrace && !snapshot.buildingTrace);
	controls_.setWidgetEnabled("start_overlay", screenModel.playEnabled);
	controls_.find("menu_file")->setOptionEnabled(1, screenModel.runEnabled);
	controls_.find("menu_file")->setOptionEnabled(2, screenModel.playEnabled);
	controls_.find("menu_file")->setOptionEnabled(3, true);
	for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
		pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
		controls_.find("pattern")->setOptionEnabled(static_cast<std::size_t>(pattern),
			settingsInputPatternEnabled(snapshot.domain, snapshot.algorithm,
				static_cast<sortinglogic::InputPattern>(pattern)));
	controls_.setWidgetEnabled("time_down", !busy && !referenceCadence);
	controls_.setWidgetEnabled("time_up", !busy && !referenceCadence);
	controls_.find("next")->setLabel(activePreset == UiPresetId::ClassicWindows95 ? "Step" : "Event >");
}

void MainControlsView::compose(MainControlsPaintSection section, const UiRect& screen,
	UiPaintContext& paintContext) const
{
	if (screen.width <= 0.0f || screen.height <= 0.0f) return;
	const char* headerButtons[] = { "panel_toggle", "settings" };
	const char* menus[] = { "menu_file", "menu_view", "menu_tools" };
	if (section == MainControlsPaintSection::HeaderButtons)
	{
		for (std::size_t i = 0; i < sizeof(headerButtons) / sizeof(headerButtons[0]); ++i)
			submitControl(controls_, paintContext, headerButtons[i], UiPaintLayer::MainControls,
				UiPaintMaterial::Button, screen);
		return;
	}
	if (section == MainControlsPaintSection::Menus)
	{
		for (std::size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); ++i)
			submitControl(controls_, paintContext, menus[i], UiPaintLayer::MainControls,
				UiPaintMaterial::Button, screen);
		return;
	}
	if (section == MainControlsPaintSection::CollapsedStart)
	{
		submitControl(controls_, paintContext, "start_overlay", UiPaintLayer::MainControls,
			UiPaintMaterial::Button, screen);
		return;
	}
	if (section == MainControlsPaintSection::ExpandedControls)
	{
		for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
		{
			const UiWidget& widget = *controls_.widgets()[i];
			const std::string& id = widget.id();
			if (id == "panel_toggle" || id == "settings" || id == "menu_file" ||
				id == "menu_view" || id == "menu_tools") continue;
			submitControl(controls_, paintContext, id, UiPaintLayer::MainControls,
				UiPaintMaterial::Button, screen);
		}
		return;
	}
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const UiWidget& widget = *controls_.widgets()[i];
		if (widget.visible() && widget.expanded())
			submitControl(controls_, paintContext, widget.id(), UiPaintLayer::MainDropdown,
				UiPaintMaterial::Popup, screen, true);
	}
}
