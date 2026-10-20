#include "SettingsPanelView.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

#include "../SortingLogic/SortingLogic.h"
#include "AppActions.h"
#include "SettingsPanelCatalog.h"
#include "UiActionRouting.h"
#include "SettingsPanelLayout.h"
#include "UserSettings.h"

namespace
{
	AppAction reconstructedSettingAction(ReconstructedAudioParameterId parameter)
	{
		return AppAction(AppActionKind::SetReconstructedAudioParameter, parameter);
	}

	const UiThemeCustomization& activeTheme(const UserPreferences& preferences)
	{
		const std::map<UiPresetId, UiThemeCustomization>::const_iterator found =
			preferences.appearance.themes.find(preferences.appearance.activePreset);
		if (found != preferences.appearance.themes.end()) return found->second;
		static const UiThemeCustomization fallback;
		return fallback;
	}
}

SettingsPanelView::SettingsPanelView()
	: controls_(), model_(), captions_(), algorithmDescriptorsById_(), algorithmOptionsById_(),
	  activeAlgorithmOptionControlIds_(), activeAlgorithmOptionId_(-1), panelBounds_(), contentBounds_()
{
}

void SettingsPanelView::initializeControls(const UserPreferences& preferences)
{
	controls_.addButton("appearance_tab", "Appearance", AppAction(), UiAction::ViewCommand::ShowAppearanceSettings);
	controls_.addButton("algorithm_tab", "Algorithm", AppAction(), UiAction::ViewCommand::ShowAlgorithmSettings);
	controls_.addButton("audio_tab", "General Audio", AppAction(), UiAction::ViewCommand::ShowGeneralAudioSettings);
	controls_.addButton("audio_basic_tab", "Basic", AppAction(), UiAction::ViewCommand::ShowAudioBasic);
	controls_.addButton("audio_advanced_tab", "Advanced", AppAction(), UiAction::ViewCommand::ShowAudioAdvanced);
	controls_.addComboBox("preset", settingsUiPresetOptions(), static_cast<int>(preferences.appearance.activePreset),
		AppAction(AppActionKind::SetUiPreset));
	controls_.addComboBox("palette", settingsPaletteOptions(), static_cast<int>(activeTheme(preferences).palette),
		AppAction(AppActionKind::SetPalette));
	controls_.addComboBox("spacing", settingsBarSpacingOptions(), static_cast<int>(preferences.visual.spacing),
		AppAction(AppActionKind::SetSpacing));
	controls_.addButton("appearance_customize", "Customize appearance...", AppAction(),
		UiAction::ViewCommand::OpenAppearanceEditor);
	controls_.addComboBox("domain", settingsInputDomainOptions(), static_cast<int>(preferences.simulation.domain),
		AppAction(AppActionKind::SetInputDomain));
	controls_.addComboBox("pattern", settingsInputPatternOptions(), static_cast<int>(preferences.simulation.pattern),
		AppAction(AppActionKind::SetInputPattern));
	controls_.addComboBox("playback_timing", std::vector<std::string>{ "Fixed duration", "Reference cadence" },
		static_cast<int>(preferences.simulation.playbackTimingMode), AppAction(AppActionKind::SetPlaybackTimingMode));
	controls_.addComboBox("merge_buffer_visualization", settingsBooleanOptions(),
		preferences.simulation.mergeBufferVisualizationEnabled ? 1 : 0,
		AppAction(AppActionKind::SetMergeBufferVisualization));
	controls_.addComboBox("completion_scan", settingsBooleanOptions(), preferences.simulation.completionScanEnabled ? 1 : 0,
		AppAction(AppActionKind::SetCompletionScan));

	captions_["preset"] = "Interface preset";
	captions_["palette"] = "Palette";
	captions_["spacing"] = "Bar spacing";
	captions_["appearance_customize"] = "";
	captions_["domain"] = "Input domain";
	captions_["pattern"] = "Input pattern";
	captions_["playback_timing"] = "Playback timing";
	captions_["merge_buffer_visualization"] = "Merge buffer visualization";
	captions_["completion_scan"] = "Completion scan";

	const std::vector<sortinglogic::AlgorithmDescriptor>& algorithms = sortinglogic::algorithmCatalog();
	algorithmDescriptorsById_.clear();
	algorithmOptionsById_.clear();
	activeAlgorithmOptionControlIds_.clear();
	activeAlgorithmOptionId_ = -1;
	for (std::size_t algorithmIndex = 0; algorithmIndex < algorithms.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithm = algorithms[algorithmIndex];
		const int algorithmKey = static_cast<int>(algorithm.id);
		algorithmDescriptorsById_[algorithmKey] = &algorithm;
		std::set<std::string> groups;
		for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
			const std::string groupId(option.groupId);
			if (!groups.insert(groupId).second) continue;
			std::vector<std::string> optionNames;
			int selectedIndex = 0;
			const std::string selectedId = algorithmOptionStableId(preferences, algorithm, groupId);
			for (std::size_t candidateIndex = 0; candidateIndex < algorithm.supportedOptions.size(); ++candidateIndex)
			{
				const sortinglogic::AlgorithmOptionDescriptor& candidate = algorithm.supportedOptions[candidateIndex];
				if (groupId != candidate.groupId) continue;
				if (candidate.stableId == selectedId) selectedIndex = static_cast<int>(optionNames.size());
				optionNames.push_back(candidate.displayName);
			}
			const std::string id = settingsAlgorithmOptionControlId(algorithm.familyId, algorithm.variantId, groupId);
			controls_.addComboBox(id, optionNames, selectedIndex,
				AppAction(AppActionKind::SetAlgorithmOption, algorithm.familyId, algorithm.variantId, groupId));
			controls_.setWidgetEnabled(id, false);
			AlgorithmOptionBinding binding;
			binding.controlId = id;
			binding.groupId = groupId;
			algorithmOptionsById_[algorithmKey].push_back(binding);
			captions_[id] = settingsAlgorithmOptionCaption(groupId);
		}
	}

	const SoundEngine::ReconstructedParameters& audio = preferences.reconstructedAudio;
	controls_.addComboBox("recon_enabled", settingsBooleanOptions(), audio.audioEnabled ? 1 : 0,
		reconstructedSettingAction(ReconstructedAudioParameterId::Enabled));
	controls_.addSlider("recon_master", "Master volume", 0.0f, 1.0f, 0.01f, static_cast<float>(audio.masterLevel),
		reconstructedSettingAction(ReconstructedAudioParameterId::MasterLevel));
	controls_.addComboBox("recon_idle_policy", std::vector<std::string>{ "Hold selection", "Release selection" },
		static_cast<int>(audio.idlePolicy), reconstructedSettingAction(ReconstructedAudioParameterId::IdlePolicy));
	controls_.addSlider("recon_release", "Special release (ms)", 0.0f, 2000.0f, 10.0f,
		static_cast<float>(audio.specialReleaseMilliseconds), reconstructedSettingAction(ReconstructedAudioParameterId::SpecialReleaseMilliseconds));
	controls_.addButton("recon_restore", "Restore reconstructed defaults",
		AppAction(AppActionKind::RestoreReconstructedDefaults));
	controls_.addSlider("recon_min_pitch", "Minimum pitch (Hz)", 50.0f, 1000.0f, 1.0f,
		static_cast<float>(audio.minimumFrequencyHz), reconstructedSettingAction(ReconstructedAudioParameterId::MinimumFrequencyHz));
	controls_.addSlider("recon_max_pitch", "Maximum pitch (Hz)", 1000.0f, 8000.0f, 1.0f,
		static_cast<float>(audio.maximumFrequencyHz), reconstructedSettingAction(ReconstructedAudioParameterId::MaximumFrequencyHz));
	controls_.addComboBox("recon_mapping", std::vector<std::string>{ "Linear Hz", "Log frequency" },
		static_cast<int>(audio.mapping), reconstructedSettingAction(ReconstructedAudioParameterId::Mapping));
	controls_.addSlider("recon_clock", "Excitation clock (Hz)", 100.0f, 600.0f, 1.0f,
		static_cast<float>(audio.excitationClockHz), reconstructedSettingAction(ReconstructedAudioParameterId::ExcitationClockHz));
	controls_.addSlider("recon_cycles", "Carrier cycles", 1.0f, 16.0f, 0.1f, static_cast<float>(audio.carrierCycles),
		reconstructedSettingAction(ReconstructedAudioParameterId::CarrierCycles));
	controls_.addSlider("recon_tukey", "Tukey taper", 0.0f, 1.0f, 0.01f, static_cast<float>(audio.tukeyFraction),
		reconstructedSettingAction(ReconstructedAudioParameterId::TukeyFraction));
	controls_.addSlider("recon_delay", "Second operand delay (us)", 0.0f, 2000.0f, 1.0f,
		static_cast<float>(audio.secondOperandDelayMicroseconds), reconstructedSettingAction(ReconstructedAudioParameterId::SecondOperandDelayMicroseconds));
	controls_.addComboBox("recon_lowpass", settingsBooleanOptions(), audio.lowPassEnabled ? 1 : 0,
		reconstructedSettingAction(ReconstructedAudioParameterId::LowPassEnabled));
	controls_.addSlider("recon_cutoff", "Low-pass cutoff (Hz)", 500.0f, 20000.0f, 10.0f,
		static_cast<float>(audio.lowPassCutoffHz), reconstructedSettingAction(ReconstructedAudioParameterId::LowPassCutoffHz));

	const char* audioIds[] = { "recon_enabled", "recon_master", "recon_idle_policy", "recon_release", "recon_min_pitch",
		"recon_max_pitch", "recon_mapping", "recon_clock", "recon_cycles", "recon_tukey", "recon_delay", "recon_lowpass", "recon_cutoff" };
	const char* audioCaptions[] = { "Audio enabled", "Master volume", "Idle selection", "Special release (ms)", "Minimum pitch (Hz)",
		"Maximum pitch (Hz)", "Value mapping", "Excitation clock (Hz)", "Carrier cycles", "Tukey taper", "Second operand delay (us)",
		"Tone low-pass filter", "Low-pass cutoff (Hz)" };
	for (std::size_t i = 0; i < sizeof(audioIds) / sizeof(audioIds[0]); ++i)
		captions_[audioIds[i]] = audioCaptions[i];

	for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
	{
		const AlgorithmAudioChannel level = static_cast<AlgorithmAudioChannel>(channel);
		const std::string id = std::string("algorithm_audio_") + (channel < 10 ? "0" : "") + std::to_string(channel);
		controls_.addSlider(id, algorithmAudioChannelName(level), -60.0f, 12.0f, 0.5f, 0.0f,
			AppAction(AppActionKind::SetVariantAudioLevel, preferences.simulation.familyId,
				preferences.simulation.variantId, 0.0f, channel));
		captions_[id] = algorithmAudioChannelName(level);
	}
	controls_.addButton("algorithm_audio_reset", "Reset to family defaults",
		AppAction(AppActionKind::ResetVariantAudioLevels, preferences.simulation.familyId, preferences.simulation.variantId));
	controls_.addButton("audio_retry", "Retry audio device", AppAction(AppActionKind::RetryAudioDevice));
	controls_.addButton("settings_close", "Done", AppAction(), UiAction::ViewCommand::CloseSettings);
	controls_.addContainer("settings_panel", UiContainerKind::Panel);
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
		if (controls_.widgets()[i]->kind() == UiWidgetKind::ComboBox)
			controls_.widgets()[i]->setRole(UiControlRole::Choice);
}

void SettingsPanelView::synchronizeControls(const UserPreferences& preferences,
	const UserPreferences& renderPreferences, const sortinglogic::RenderSnapshot& snapshot, bool setupBusy)
{
	if (!controls_.find("preset")) return;
	controls_.find("preset")->setSelectedIndex(static_cast<int>(renderPreferences.appearance.activePreset));
	controls_.find("palette")->setSelectedIndex(static_cast<int>(activeTheme(renderPreferences).palette));
	controls_.find("spacing")->setSelectedIndex(static_cast<int>(preferences.visual.spacing));
	controls_.find("domain")->setSelectedIndex(static_cast<int>(preferences.simulation.domain));
	controls_.find("pattern")->setSelectedIndex(static_cast<int>(preferences.simulation.pattern));
	controls_.find("playback_timing")->setSelectedIndex(static_cast<int>(preferences.simulation.playbackTimingMode));
	controls_.find("merge_buffer_visualization")->setSelectedIndex(preferences.simulation.mergeBufferVisualizationEnabled ? 1 : 0);
	controls_.find("completion_scan")->setSelectedIndex(preferences.simulation.completionScanEnabled ? 1 : 0);

	controls_.setWidgetEnabled("domain", !setupBusy);
	controls_.setWidgetEnabled("pattern", !setupBusy);
	controls_.setWidgetEnabled("playback_timing", !setupBusy);
	controls_.setWidgetEnabled("merge_buffer_visualization",
		!setupBusy && snapshot.algorithm == sortinglogic::SortAlgorithm::Merge);
	controls_.setWidgetEnabled("completion_scan", !setupBusy);

	const int algorithmKey = static_cast<int>(snapshot.algorithm);
	if (activeAlgorithmOptionId_ != algorithmKey)
	{
		for (std::size_t i = 0; i < activeAlgorithmOptionControlIds_.size(); ++i)
			controls_.setWidgetEnabled(activeAlgorithmOptionControlIds_[i], false);
		activeAlgorithmOptionControlIds_.clear();
		const std::map<int, std::vector<AlgorithmOptionBinding> >::const_iterator groups =
			algorithmOptionsById_.find(algorithmKey);
		if (groups != algorithmOptionsById_.end())
		{
			for (std::size_t i = 0; i < groups->second.size(); ++i)
				activeAlgorithmOptionControlIds_.push_back(groups->second[i].controlId);
		}
		activeAlgorithmOptionId_ = algorithmKey;
	}

	const std::map<int, const sortinglogic::AlgorithmDescriptor*>::const_iterator selectedAlgorithm =
		algorithmDescriptorsById_.find(algorithmKey);
	const sortinglogic::AlgorithmDescriptor* selectedDescriptor = selectedAlgorithm == algorithmDescriptorsById_.end()
		? nullptr : selectedAlgorithm->second;
	if (selectedDescriptor)
	{
		const std::map<int, std::vector<AlgorithmOptionBinding> >::const_iterator groups =
			algorithmOptionsById_.find(algorithmKey);
		if (groups != algorithmOptionsById_.end())
			for (std::size_t i = 0; i < groups->second.size(); ++i)
			{
				const AlgorithmOptionBinding& binding = groups->second[i];
				UiWidget* widget = controls_.find(binding.controlId);
				if (!widget) continue;

				const std::string selectedId = algorithmOptionStableId(preferences, *selectedDescriptor, binding.groupId);
				int selectedIndex = 0;
				for (std::size_t candidateIndex = 0, visibleIndex = 0;
					candidateIndex < selectedDescriptor->supportedOptions.size(); ++candidateIndex)
				{
					const sortinglogic::AlgorithmOptionDescriptor& candidate = selectedDescriptor->supportedOptions[candidateIndex];
					if (binding.groupId != candidate.groupId) continue;
					if (selectedId == candidate.stableId) selectedIndex = static_cast<int>(visibleIndex);
					++visibleIndex;
				}
				widget->setSelectedIndex(selectedIndex);
				controls_.setWidgetEnabled(binding.controlId, !setupBusy);
			}
	}

	const UiWidget* pattern = controls_.find("pattern");
	if (pattern)
		for (int value = static_cast<int>(sortinglogic::InputPattern::Random);
			value <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++value)
			controls_.find("pattern")->setOptionEnabled(static_cast<std::size_t>(value),
				settingsInputPatternEnabled(snapshot.domain, snapshot.algorithm,
					static_cast<sortinglogic::InputPattern>(value)));

	synchronizeAudioControls(preferences, selectedDescriptor);
}

void SettingsPanelView::synchronizeAudioControls(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor* algorithm)
{
	const SoundEngine::ReconstructedParameters& audio = preferences.reconstructedAudio;
	auto slider = [this](const char* id, float value) { controls_.find(id)->setSliderValue(value); };
	auto selection = [this](const char* id, int value) { controls_.find(id)->setSelectedIndex(value); };
	selection("recon_enabled", audio.audioEnabled ? 1 : 0);
	slider("recon_master", static_cast<float>(audio.masterLevel));
	selection("recon_idle_policy", static_cast<int>(audio.idlePolicy));
	slider("recon_release", static_cast<float>(audio.specialReleaseMilliseconds));
	slider("recon_min_pitch", static_cast<float>(audio.minimumFrequencyHz));
	slider("recon_max_pitch", static_cast<float>(audio.maximumFrequencyHz));
	selection("recon_mapping", static_cast<int>(audio.mapping));
	slider("recon_clock", static_cast<float>(audio.excitationClockHz));
	slider("recon_cycles", static_cast<float>(audio.carrierCycles));
	slider("recon_tukey", static_cast<float>(audio.tukeyFraction));
	slider("recon_delay", static_cast<float>(audio.secondOperandDelayMicroseconds));
	selection("recon_lowpass", audio.lowPassEnabled ? 1 : 0);
	slider("recon_cutoff", static_cast<float>(audio.lowPassCutoffHz));
	controls_.setWidgetEnabled("recon_release",
		audio.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
	controls_.setWidgetEnabled("recon_cutoff", audio.lowPassEnabled);

	if (!algorithm) return;
	const AlgorithmAudioLevels inherited = familyAudioLevels(preferences, algorithm->familyId);
	const AlgorithmAudioLevels offsets = variantAudioLevels(preferences, algorithm->familyId, algorithm->variantId);
	for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
	{
		const AlgorithmAudioChannel level = static_cast<AlgorithmAudioChannel>(channel);
		const std::size_t index = static_cast<std::size_t>(level);
		const std::string id = std::string("algorithm_audio_") + (channel < 10 ? "0" : "") + std::to_string(channel);
		UiWidget* widget = controls_.find(id);
		if (!widget) continue;
		widget->setSliderValue(offsets.decibels[index]);
		widget->setAction(AppAction(AppActionKind::SetVariantAudioLevel, algorithm->familyId, algorithm->variantId,
			offsets.decibels[index], channel));
		std::ostringstream caption;
		caption << algorithmAudioChannelName(level) << " (family " << std::fixed << std::setprecision(1)
			<< std::showpos << inherited.decibels[index] << " dB)";
		captions_[id] = caption.str();
	}
	controls_.find("algorithm_audio_reset")->setAction(AppAction(AppActionKind::ResetVariantAudioLevels,
		algorithm->familyId, algorithm->variantId));
}

void SettingsPanelView::selectAudioBand(int index, int bandCount)
{
	model_.selectAudioBand(index, bandCount);
}

bool SettingsPanelView::handleNavigation(UiAction::ViewCommand command)
{
	const ui::SettingsNavigation navigation = ui::settingsNavigationFor(command);
	if (navigation.kind == ui::SettingsNavigationKind::None) return false;
	if (navigation.kind == ui::SettingsNavigationKind::AudioGroup)
		model_.showAudioGroup(navigation.audioGroup);
	else
		model_.showPage(navigation.page);
	controls_.closeCombo();
	return true;
}

bool SettingsPanelView::isScrollablePage() const
{
	return model_.isPage(SettingsPanelPage::Algorithm) || model_.isPage(SettingsPanelPage::GeneralAudio);
}

bool SettingsPanelView::handleScrollWheel(float x, float y, int wheelDelta)
{
	return isScrollablePage() && model_.scrollViewport().scrollWheel(x, y, wheelDelta);
}

void SettingsPanelView::setPanelBounds(const UiRect& panelBounds, bool modalOpen)
{
	panelBounds_ = panelBounds;
	controls_.layoutContainer("settings_panel", panelBounds_);
	contentBounds_ = calculateSettingsPanelGeometry(panelBounds_, model_.page()).content;
	if (modalOpen)
		controls_.setModalBounds(panelBounds_, UiAction::ViewCommand::CloseSettings);
}

void SettingsPanelView::setModalActive(bool active)
{
	controls_.setInputCaptured(active);
	if (active && panelBounds_.width > 0.0f)
		controls_.setModalBounds(panelBounds_, UiAction::ViewCommand::CloseSettings);
}

void SettingsPanelView::layoutForAlgorithmSelection(const std::string& familyId,
	const std::string& variantId, float scrollbarThickness)
{
	const sortinglogic::AlgorithmDescriptor* selected = sortinglogic::algorithmDescriptorById(familyId, variantId);
	const SettingsPanelAlgorithmPresentation presentation = settingsPanelAlgorithmPresentation(selected);
	const std::vector<std::string> activeControlIds = settingsPanelControlIds(
		model_.page(), model_.audioGroup(), &presentation);
	layoutControls(activeControlIds, scrollbarThickness);
}

void SettingsPanelView::layoutControls(const std::vector<std::string>& activeControlIds,
	float scrollbarThickness)
{
	if (panelBounds_.width <= 0.0f) return;
	const float x = panelBounds_.x + 16.0f;
	const float width = std::max(1.0f, panelBounds_.width - 32.0f);
	const SettingsPanelGeometry panelGeometry = calculateSettingsPanelGeometry(panelBounds_, model_.page());
	controls_.find("appearance_tab")->setBounds(panelGeometry.tabs[0]);
	controls_.find("algorithm_tab")->setBounds(panelGeometry.tabs[1]);
	controls_.find("audio_tab")->setBounds(panelGeometry.tabs[2]);
	controls_.find("audio_basic_tab")->setBounds(panelGeometry.audioGroups[0]);
	controls_.find("audio_advanced_tab")->setBounds(panelGeometry.audioGroups[1]);
	contentBounds_ = panelGeometry.content;
	controls_.find("settings_close")->setBounds(panelGeometry.closeButton);
	controls_.find("audio_retry")->setBounds(panelGeometry.audioRetryButton);

	std::set<std::string> visibleWidgetIds;
	visibleWidgetIds.insert("appearance_tab");
	visibleWidgetIds.insert("algorithm_tab");
	visibleWidgetIds.insert("audio_tab");
	visibleWidgetIds.insert("settings_close");
	if (model_.isPage(SettingsPanelPage::GeneralAudio))
	{
		visibleWidgetIds.insert("audio_retry");
		visibleWidgetIds.insert("audio_basic_tab");
		visibleWidgetIds.insert("audio_advanced_tab");
	}
	const float rowHeight = 48.0f;
	const float contentBottom = contentBounds_.y + contentBounds_.height;
	const float totalHeight = static_cast<float>(activeControlIds.size()) * rowHeight;
	model_.scrollViewport().configure(contentBounds_, totalHeight, rowHeight, model_.scrollOffset());
	for (std::size_t i = 0; i < activeControlIds.size(); ++i)
	{
		const UiRect rowBounds = model_.scrollViewport().itemBounds(i, rowHeight);
		const float rowY = rowBounds.y;
		const bool visible = model_.scrollViewport().itemIntersectsViewport(i, rowHeight) &&
			rowY + rowHeight <= contentBottom + 0.5f;
		if (visible) visibleWidgetIds.insert(activeControlIds[i]);
		UiWidget* widget = controls_.find(activeControlIds[i]);
		if (!widget) continue;
		const SettingsWidgetGeometry geometry = calculateSettingsWidgetGeometry(*widget, rowBounds,
			x, width, panelBounds_, scrollbarThickness);
		widget->setBounds(geometry.bounds);
		widget->setInteractiveSubregions(geometry.interactiveSubregions);
		if (geometry.setPopupAbove) widget->setPopupAbove(geometry.popupAbove);
	}
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const std::string& id = controls_.widgets()[i]->id();
		controls_.setWidgetVisible(id, visibleWidgetIds.find(id) != visibleWidgetIds.end());
	}
}

void SettingsPanelView::compose(int screenWidth, int screenHeight, const std::string& title,
	const std::string& footer, const SettingsPanelPalette& palette, UiPaintContext& paintContext) const
{
	const UiRect screen(0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, screen, palette.overlayDim, std::string(), std::string(),
		UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop, UiRect(), false, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, panelBounds_, palette.panel, std::string(), std::string(),
		UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle,
		UiRect(panelBounds_.x, panelBounds_.y, panelBounds_.width, 2.0f), palette.accent,
		std::string(), std::string(), UiPaintMaterial::Accent, UiPaintLayer::Accent, screen, true, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Text,
		UiRect(panelBounds_.x + 16.0f, panelBounds_.y + 13.0f, panelBounds_.width - 32.0f, 28.0f),
		palette.text, title, "#settings_title", UiPaintMaterial::Solid, UiPaintLayer::SettingsText,
		screen, true, 17.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, contentBounds_, palette.background,
		std::string(), std::string(), UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true, 0.0f));
	const float footerWidth = panelBounds_.width - (model_.isPage(SettingsPanelPage::GeneralAudio) ? 130.0f : 120.0f);
	const float footerOffset = model_.isPage(SettingsPanelPage::GeneralAudio) ? 62.0f : 58.0f;
	const UiRect footerBounds(panelBounds_.x + 16.0f, panelBounds_.y + panelBounds_.height - footerOffset,
		footerWidth, 16.0f);
	if (!footer.empty() && footerBounds.width > 0.0f && footerBounds.height > 0.0f)
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Text, footerBounds, palette.mutedText, footer,
			"#settings_footer", UiPaintMaterial::Solid, UiPaintLayer::SettingsText, screen, true, 11.0f));

	if (!controls_.find("appearance_tab")) return;
	const auto submitControl = [&paintContext](const UiWidget* widget, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer, const UiRect& clip, bool hasClip, bool popup)
	{
		if (!widget || !widget->visible()) return;
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Control, widget->bounds(), color, std::string(),
			widget->id(), material, layer, clip, hasClip, 0.0f, popup));
	};

	const char* tabIds[] = { "appearance_tab", "algorithm_tab", "audio_tab" };
	const UiWidget* tabs[3] = { controls_.find(tabIds[0]), controls_.find(tabIds[1]), controls_.find(tabIds[2]) };
	for (int tab = 0; tab < 3; ++tab)
		submitControl(tabs[tab], tab == model_.pageIndex() ? palette.accent : palette.button,
			tab == model_.pageIndex() ? UiPaintMaterial::Accent : UiPaintMaterial::Button,
			UiPaintLayer::SettingsControls, screen, true, false);
	if (model_.pageIndex() >= 0 && model_.pageIndex() < 3 && tabs[model_.pageIndex()])
	{
		const UiRect& activeTab = tabs[model_.pageIndex()]->bounds();
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle,
			UiRect(activeTab.x, activeTab.y + activeTab.height - 2.0f, activeTab.width, 2.0f),
			palette.accent, std::string(), std::string(), UiPaintMaterial::Accent,
			UiPaintLayer::SettingsControlsOverlay, screen, true, 0.0f));
	}

	if (model_.isPage(SettingsPanelPage::GeneralAudio))
	{
		const char* groupIds[] = { "audio_basic_tab", "audio_advanced_tab" };
		for (int group = 0; group < 2; ++group)
		{
			const bool selected = (model_.audioGroup() == SettingsAudioGroup::Advanced) == (group == 1);
			submitControl(controls_.find(groupIds[group]), selected ? palette.accent : palette.button,
				selected ? UiPaintMaterial::Accent : UiPaintMaterial::Button,
				UiPaintLayer::SettingsControls, screen, true, false);
		}
	}

	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const UiWidget* widget = controls_.widgets()[i].get();
		const std::string& id = widget->id();
		if (id == "appearance_tab" || id == "algorithm_tab" || id == "audio_tab" ||
			id == "audio_basic_tab" || id == "audio_advanced_tab" ||
			id == "settings_close" || id == "audio_retry") continue;
		submitControl(widget, palette.text, UiPaintMaterial::Solid,
			UiPaintLayer::SettingsControls, contentBounds_, true, false);
	}
	submitControl(controls_.find("settings_close"), palette.button, UiPaintMaterial::Solid,
		UiPaintLayer::SettingsControls, screen, true, false);
	if (model_.isPage(SettingsPanelPage::GeneralAudio))
		submitControl(controls_.find("audio_retry"), palette.button, UiPaintMaterial::Solid,
			UiPaintLayer::SettingsControls, screen, true, false);

	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const UiWidget* widget = controls_.widgets()[i].get();
		if (widget->visible() && widget->expanded())
			submitControl(widget, palette.text, UiPaintMaterial::Popup,
				UiPaintLayer::Content, screen, true, true);
	}
}
