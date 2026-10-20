#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

#include "BarCountSteps.h"
#include "AudioSettingsActions.h"
#include "PlaybackTimebase.h"

namespace
{
	bool needsFullPreferenceSnapshot(AppActionKind action)
	{
		// The simulation settings are captured separately for every dispatch.
		// These actions can also mutate non-simulation preferences and need the
		// complete snapshot for change detection.
		switch (action)
		{
		case AppActionKind::SetUiPreset:
		case AppActionKind::SetPalette:
		case AppActionKind::SetSpacing:
		case AppActionKind::SetPanelExpanded:
		case AppActionKind::TogglePanelExpanded:
		case AppActionKind::SetAlgorithmOption:
		case AppActionKind::SetShellGapSequence:
		case AppActionKind::SetMergeWritebackMode:
		case AppActionKind::SetMergeExecutionMode:
		case AppActionKind::SetAudioParameter:
		case AppActionKind::SetReconstructedAudioParameter:
		case AppActionKind::RestoreReconstructedDefaults:
		case AppActionKind::SetVariantAudioLevel:
		case AppActionKind::ResetVariantAudioLevels:
			return true;
		case AppActionKind::Shuffle:
		case AppActionKind::InstaSort:
		case AppActionKind::InstaInvert:
		case AppActionKind::SelectAlgorithm:
		case AppActionKind::SetDirection:
		case AppActionKind::SetPlaybackTimingMode:
		case AppActionKind::SetCompletionScan:
		case AppActionKind::SetMergeBufferVisualization:
		case AppActionKind::BuildTrace:
		case AppActionKind::GenerateAndPlay:
		case AppActionKind::TogglePlayback:
		case AppActionKind::Reset:
		case AppActionKind::PreviousEvent:
		case AppActionKind::NextEvent:
		case AppActionKind::DecreaseCountFine:
		case AppActionKind::DecreaseCountCoarse:
		case AppActionKind::IncreaseCountFine:
		case AppActionKind::IncreaseCountCoarse:
		case AppActionKind::AdjustItemCount:
		case AppActionKind::SetItemCount:
		case AppActionKind::DecreaseDuration:
		case AppActionKind::AdjustDuration:
		case AppActionKind::IncreaseDuration:
		case AppActionKind::SetDuration:
		case AppActionKind::SeekFraction:
		case AppActionKind::ApplyAppearance:
		case AppActionKind::SetInputProfile:
		case AppActionKind::SetInputDomain:
		case AppActionKind::SetInputPattern:
		case AppActionKind::ToggleBorderlessFullscreen:
		case AppActionKind::SelectAudioBand:
		case AppActionKind::RetryAudioDevice:
		case AppActionKind::None:
			return false;
		default:
			// New commands take the conservative path until explicitly classified.
			return true;
		}
	}

	class PreferenceChangeSnapshot
	{
	public:
		PreferenceChangeSnapshot(const UserPreferences& current, bool captureFullPreferences)
			: simulation_(current.simulation), full_(captureFullPreferences ? new UserPreferences(current) : nullptr)
		{
		}

		bool changed(const UserPreferences& current) const
		{
			return !(simulation_ == current.simulation) || (full_ && !(*full_ == current));
		}

	private:
		SimulationPreferences simulation_;
		std::unique_ptr<UserPreferences> full_;
	};

	void storeAlgorithmOption(UserPreferences& preferences, const sortinglogic::AlgorithmDescriptor& algorithm,
		const std::string& groupId, int value)
	{
		for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[i];
			if (std::string(option.groupId) == groupId && option.value == value)
			{
				preferences.algorithmOptions[algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, groupId)] = option.stableId;
				return;
			}
		}
	}

}

UserPreferences AppController::loadPreferences(ISettingsPersistence& persistence)
{
	const UserPreferences loaded = persistence.load();
	return loaded.isValid() ? loaded : UserPreferences();
}

AppController::AppController(ISettingsPersistence& settingsPersistence)
	: settingsPersistence_(settingsPersistence),
	preferences_(loadPreferences(settingsPersistence)),
		simulation_(static_cast<std::size_t>(preferences_.simulation.itemCount), sortinglogic::Simulation::createFreshSeed(),
			preferences_.simulation.domain, preferences_.simulation.pattern),
		traceBuildService_(), playbackCoordinator_(), persistenceStatus_("Preferences loaded.")
{
	simulation_.setDuration(static_cast<float>(preferences_.simulation.durationSeconds));
	simulation_.setDirection(preferences_.simulation.descending
		? sortinglogic::SortDirection::Descending
		: sortinglogic::SortDirection::Ascending);
	const sortinglogic::AlgorithmDescriptor* savedAlgorithm = sortinglogic::algorithmDescriptorById(
		preferences_.simulation.familyId, preferences_.simulation.variantId);
	if (savedAlgorithm && sortinglogic::algorithmSupports(savedAlgorithm->id, preferences_.simulation.domain))
		simulation_.selectAlgorithm(savedAlgorithm->id);
	simulation_.setMergeBufferVisualizationEnabled(preferences_.simulation.mergeBufferVisualizationEnabled);
	simulation_.setCompletionScanEnabled(preferences_.simulation.completionScanEnabled);
	const sortinglogic::AlgorithmDescriptor* activeAlgorithm = sortinglogic::algorithmDescriptor(simulation_.algorithm());
	if (activeAlgorithm)
	{
		std::set<std::string> configuredGroups;
		for (std::size_t i = 0; i < activeAlgorithm->supportedOptions.size(); ++i)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = activeAlgorithm->supportedOptions[i];
			const std::string group(option.groupId);
			if (!configuredGroups.insert(group).second) continue;
			const std::string stableId = algorithmOptionStableId(preferences_, *activeAlgorithm, group);
			if (!stableId.empty()) simulation_.setAlgorithmOption(group, stableId);
		}
	}
}

AppController::~AppController() = default;

sortinglogic::CommandResult AppController::requestTraceBuild(bool playWhenReady)
{
	playbackCoordinator_.traceBuildRequested(false);
	sortinglogic::TraceBuildRequest request;
	sortinglogic::CommandResult result = simulation_.beginTraceBuild(request);
	if (!result.accepted) return result;
	request.setConfiguration(simulation_.algorithmConfiguration());
	const sortinglogic::AlgorithmDescriptor* traceAlgorithm = sortinglogic::algorithmDescriptor(request.algorithm);
	if (traceAlgorithm)
	{
		std::set<std::string> configuredGroups;
		for (std::size_t optionIndex = 0; optionIndex < traceAlgorithm->supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = traceAlgorithm->supportedOptions[optionIndex];
			const std::string group(option.groupId);
			if (!configuredGroups.insert(group).second) continue;
			const std::string stableId = algorithmOptionStableId(preferences_, *traceAlgorithm, group);
			if (!stableId.empty()) request.optionSelections[group] = stableId;
		}
	}

	playbackCoordinator_.traceBuildRequested(playWhenReady);
	std::string error;
	if (!traceBuildService_.submit(request, &error))
	{
		sortinglogic::TraceBuildResult failed;
		failed.error = error.empty() ? "Trace worker is unavailable." : error;
		simulation_.completeTraceBuild(failed);
		playbackCoordinator_.traceBuildFailed();
		return sortinglogic::CommandResult(false, failed.error);
	}
	if (playWhenReady)
		result.message = "Generating the trace; playback will start automatically when preparation finishes.";
	return result;
}

void AppController::setAudioPreparationPending(bool pending)
{
	playbackCoordinator_.setAudioPreparationPending(pending);
	startPendingPlaybackIfReady();
}

bool AppController::applyAppearance(const UiAppearancePreferences& appearance, std::string* error)
{
	if (static_cast<int>(appearance.activePreset) < static_cast<int>(UiPresetId::Current) ||
		static_cast<int>(appearance.activePreset) > static_cast<int>(UiPresetId::ClassicWindows95))
	{
		if (error) *error = "The selected interface preset is unavailable.";
		return false;
	}
	for (std::size_t i = 0; i < uiPresetCatalog().size(); ++i)
	{
		const UiPresetId preset = uiPresetCatalog()[i];
		const std::map<UiPresetId, UiThemeCustomization>::const_iterator found = appearance.themes.find(preset);
		if (found == appearance.themes.end() || !validateUiThemeCustomization(preset, found->second, error)) return false;
	}
	UserPreferences candidate = preferences_;
	candidate.appearance = appearance;
	candidate.visual.palette = appearance.themes.find(UiPresetId::Current)->second.palette;
	if (!candidate.isValid())
	{
		if (error && error->empty()) *error = "The appearance settings are invalid.";
		return false;
	}
	if (!settingsPersistence_.save(candidate, error))
	{
		persistenceStatus_ = "Could not save appearance" + (error && !error->empty() ? ": " + *error : ".");
		return false;
	}
	preferences_ = candidate;
	persistenceStatus_ = "Appearance saved.";
	if (error) error->clear();
	return true;
}

void AppController::startPendingPlaybackIfReady()
{
	if (!playbackCoordinator_.consumeQueuedPlayback(simulation_.isBuildingTrace(), simulation_.hasTrace())) return;
	const bool wasPlaying = simulation_.isPlaying();
	const sortinglogic::CommandResult started = simulation_.togglePlayback();
	if (started.accepted && preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
		synchronizeReferenceTransport(wasPlaying);
}

sortinglogic::CommandResult AppController::dispatch(const AppAction& action)
{
	sortinglogic::CommandResult result(true, "");
	const PreferenceChangeSnapshot previous(preferences_, needsFullPreferenceSnapshot(action.kind));
	const bool wasPlaying = simulation_.isPlaying();
	const std::shared_ptr<const sortinglogic::SortTrace> previousTrace = simulation_.currentTrace();
	switch (action.kind)
	{
	case AppActionKind::Shuffle:
		result = simulation_.prepareShuffle(sortinglogic::Simulation::createFreshSeed());
		break;
	case AppActionKind::InstaSort:
		result = simulation_.prepareInstaSort();
		break;
	case AppActionKind::InstaInvert:
		result = simulation_.prepareInstaInvert();
		break;
	case AppActionKind::SelectAlgorithm:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		if (!descriptor)
			result = sortinglogic::CommandResult(false, "The selected algorithm family and variant are not registered.");
		else
			result = simulation_.selectAlgorithm(descriptor->id);
		break;
	}
	case AppActionKind::SetDirection:
		result = simulation_.setDirection(action.selection == 1
			? sortinglogic::SortDirection::Descending
			: sortinglogic::SortDirection::Ascending);
		break;
	case AppActionKind::SetPlaybackTimingMode:
		if (simulation_.isPlaying() || simulation_.isBuildingTrace())
			result = sortinglogic::CommandResult(false, "Pause playback and wait for trace preparation before changing timing mode.");
		else if (action.selection < static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration) ||
			action.selection > static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence))
			result = sortinglogic::CommandResult(false, "The selected playback timing mode is invalid.");
		else
			preferences_.simulation.playbackTimingMode = static_cast<SoundEngine::PlaybackTimingMode>(action.selection);
		break;
	case AppActionKind::SetCompletionScan:
		if (action.selection < 0 || action.selection > 1)
			result = sortinglogic::CommandResult(false, "The selected completion scan setting is invalid.");
		else
		{
			result = simulation_.setCompletionScanEnabled(action.selection == 1);
			if (result.accepted)
				preferences_.simulation.completionScanEnabled = simulation_.completionScanEnabled();
		}
		break;
	case AppActionKind::SetMergeBufferVisualization:
		if (action.selection < 0 || action.selection > 1)
			result = sortinglogic::CommandResult(false, "The selected Merge Sort buffer visualization setting is invalid.");
		else
		{
			result = simulation_.setMergeBufferVisualizationEnabled(action.selection == 1);
			if (result.accepted)
				preferences_.simulation.mergeBufferVisualizationEnabled = simulation_.mergeBufferVisualizationEnabled();
		}
		break;
	case AppActionKind::BuildTrace:
		result = requestTraceBuild(false);
		break;
	case AppActionKind::GenerateAndPlay:
		if (simulation_.isPlaying())
		{
			playbackCoordinator_.cancelQueuedPlayback();
			result = simulation_.togglePlayback();
		}
		else if (playbackCoordinator_.playbackQueued())
		{
			playbackCoordinator_.cancelQueuedPlayback();
			result = sortinglogic::CommandResult(true, "Queued playback cancelled.");
		}
		else if (simulation_.isBuildingTrace())
		{
			playbackCoordinator_.queuePlayback();
			result = sortinglogic::CommandResult(true, "Playback queued; it will start when the trace and audio timeline are ready.");
		}
		else if (!simulation_.currentTrace())
			result = requestTraceBuild(true);
		else if (playbackCoordinator_.audioPreparationBlocksStart())
		{
			playbackCoordinator_.queuePlayback();
			result = sortinglogic::CommandResult(true, "Playback queued until audio preparation finishes.");
		}
		else
			result = simulation_.togglePlayback();
		break;
	case AppActionKind::TogglePlayback:
		if (playbackCoordinator_.audioPreparationBlocksStart() && !simulation_.isPlaying())
			result = sortinglogic::CommandResult(false, "Preparing audio timeline. Playback will be available as soon as preparation finishes.");
		else result = simulation_.togglePlayback();
		break;
	case AppActionKind::Reset:
		result = simulation_.reset();
		break;
	case AppActionKind::PreviousEvent:
		result = simulation_.previousEvent();
		break;
	case AppActionKind::NextEvent:
		result = simulation_.nextEvent();
		break;
	case AppActionKind::DecreaseCountFine:
	case AppActionKind::DecreaseCountCoarse:
	case AppActionKind::IncreaseCountFine:
	case AppActionKind::IncreaseCountCoarse:
	{
		const bool increase = action.kind == AppActionKind::IncreaseCountFine || action.kind == AppActionKind::IncreaseCountCoarse;
		const bool coarse = action.kind == AppActionKind::DecreaseCountCoarse || action.kind == AppActionKind::IncreaseCountCoarse;
		const int next = adjustBarCount(static_cast<int>(simulation_.itemCount()), increase, coarse,
			static_cast<int>(sortinglogic::MinItemCount), static_cast<int>(sortinglogic::MaxItemCount));
		result = simulation_.setItemCount(static_cast<std::size_t>(next));
		break;
	}
	case AppActionKind::AdjustItemCount:
	{
		const int delta = static_cast<int>(std::lround(action.value));
		const int next = std::max(static_cast<int>(sortinglogic::MinItemCount),
			std::min(static_cast<int>(sortinglogic::MaxItemCount), static_cast<int>(simulation_.itemCount()) + delta));
		result = simulation_.setItemCount(static_cast<std::size_t>(next));
		break;
	}
	case AppActionKind::SetItemCount:
		result = simulation_.setItemCount(action.selection < 0 ? 0U : static_cast<std::size_t>(action.selection));
		break;
	case AppActionKind::DecreaseDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = simulation_.setDuration(std::max(sortinglogic::MinRunDurationSeconds, simulation_.durationSeconds() - 1.0f));
		break;
	case AppActionKind::AdjustDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = simulation_.setDuration(std::max(sortinglogic::MinRunDurationSeconds,
			std::min(sortinglogic::MaxRunDurationSeconds, simulation_.durationSeconds() + action.value)));
		break;
	case AppActionKind::IncreaseDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = simulation_.setDuration(std::min(sortinglogic::MaxRunDurationSeconds, simulation_.durationSeconds() + 1.0f));
		break;
	case AppActionKind::SetDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = simulation_.setDuration(action.value);
		break;
	case AppActionKind::SeekFraction:
		result = simulation_.seekToFraction(action.value);
		break;
	case AppActionKind::SetUiPreset:
		if (action.selection < static_cast<int>(UiPresetId::Current) || action.selection > static_cast<int>(UiPresetId::ClassicWindows95))
			result = sortinglogic::CommandResult(false, "The selected interface preset is unavailable.");
		else
			preferences_.appearance.activePreset = static_cast<UiPresetId>(action.selection);
		break;
	case AppActionKind::ApplyAppearance:
	{
		if (!action.appearance) return sortinglogic::CommandResult(false, "The appearance draft is missing.");
		std::string error;
		if (!applyAppearance(*action.appearance, &error))
			return sortinglogic::CommandResult(false, error.empty() ? "Could not apply the appearance." : error);
		return sortinglogic::CommandResult(true, "Appearance saved.");
	}
	case AppActionKind::SetPalette:
		if (action.selection < static_cast<int>(PaletteId::Cyan) || action.selection > static_cast<int>(PaletteId::Windows95))
			result = sortinglogic::CommandResult(false, "The selected color palette is invalid.");
		else
		{
			const UiPresetId preset = preferences_.appearance.activePreset;
			UiThemeCustomization& theme = preferences_.appearance.themes[preset];
			selectUiPalette(theme, static_cast<PaletteId>(action.selection));
			if (preset == UiPresetId::Current) preferences_.visual.palette = theme.palette;
		}
		break;
	case AppActionKind::SetSpacing:
		if (action.selection < static_cast<int>(BarSpacingMode::None) || action.selection > static_cast<int>(BarSpacingMode::Comfortable))
			result = sortinglogic::CommandResult(false, "The selected bar spacing is invalid.");
		else
			preferences_.visual.spacing = static_cast<BarSpacingMode>(action.selection);
		break;
	case AppActionKind::SetPanelExpanded:
		preferences_.visual.panelExpanded = action.selection != 0;
		break;
	case AppActionKind::TogglePanelExpanded:
		preferences_.visual.panelExpanded = !preferences_.visual.panelExpanded;
		break;
	case AppActionKind::ToggleBorderlessFullscreen:
		// Fullscreen is a window-session action handled by the SDL host.
		break;
	case AppActionKind::SetInputProfile:
		if (action.selection < static_cast<int>(sortinglogic::InputProfile::NormalizedFloat) ||
			action.selection > static_cast<int>(sortinglogic::InputProfile::IntegerKeys))
			result = sortinglogic::CommandResult(false, "The selected data profile is invalid.");
		else
		{
			result = simulation_.setInputProfile(static_cast<sortinglogic::InputProfile>(action.selection));
			if (result.accepted)
			{
				preferences_.simulation.profile = simulation_.profile();
				preferences_.simulation.domain = simulation_.domain();
			}
		}
		break;
	case AppActionKind::SetInputDomain:
		if (action.selection < static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) ||
			action.selection > static_cast<int>(sortinglogic::InputDomain::IntegerPermutation))
			result = sortinglogic::CommandResult(false, "The selected input domain is invalid.");
		else
		{
			result = simulation_.setInputDomain(static_cast<sortinglogic::InputDomain>(action.selection));
			if (result.accepted)
			{
				preferences_.simulation.domain = simulation_.domain();
				preferences_.simulation.profile = simulation_.profile();
			}
		}
		break;
	case AppActionKind::SetInputPattern:
		if (action.selection < static_cast<int>(sortinglogic::InputPattern::Random) ||
			action.selection > static_cast<int>(sortinglogic::InputPattern::AndrutReference))
			result = sortinglogic::CommandResult(false, "The selected input pattern is invalid.");
		else
		{
			result = simulation_.setInputPattern(static_cast<sortinglogic::InputPattern>(action.selection));
			if (result.accepted) preferences_.simulation.pattern = simulation_.pattern();
		}
		break;
	case AppActionKind::SetAlgorithmOption:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		const sortinglogic::AlgorithmOptionDescriptor* option = nullptr;
			if (descriptor)
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
				if (std::string(descriptor->supportedOptions[i].groupId) == action.optionGroupId &&
					descriptor->supportedOptions[i].stableId == action.optionStableId)
				{
					option = &descriptor->supportedOptions[i];
					break;
				}
		const std::string previousStableId = descriptor && option
			? algorithmOptionStableId(preferences_, *descriptor, action.optionGroupId) : std::string();
		if (!descriptor || !option || static_cast<sortinglogic::SortAlgorithm>(preferences_.simulation.algorithm) != descriptor->id)
			result = sortinglogic::CommandResult(false, "The selected algorithm option is not available for the active algorithm.");
		else
		{
			result = simulation_.setAlgorithmOption(action.optionGroupId, action.optionStableId);
		}
		if (result.accepted && previousStableId != option->stableId)
			preferences_.algorithmOptions[algorithmOptionSettingsKey(descriptor->familyId, descriptor->variantId,
				action.optionGroupId)] = option->stableId;
		break;
	}
	case AppActionKind::SetShellGapSequence:
		if (action.selection < static_cast<int>(sortinglogic::ShellGapSequence::Halving) ||
			action.selection > static_cast<int>(sortinglogic::ShellGapSequence::Tokuda))
			result = sortinglogic::CommandResult(false, "The selected Shell gap sequence is invalid.");
		else
		{
			result = simulation_.setShellGapSequence(static_cast<sortinglogic::ShellGapSequence>(action.selection));
			if (result.accepted)
			{
				const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
				if (descriptor) storeAlgorithmOption(preferences_, *descriptor, "gap_sequence", action.selection);
			}
		}
		break;
	case AppActionKind::SetMergeWritebackMode:
		if (action.selection < static_cast<int>(sortinglogic::MergeWritebackMode::AnimatedWrites) ||
			action.selection > static_cast<int>(sortinglogic::MergeWritebackMode::AtomicRangeWrite))
			result = sortinglogic::CommandResult(false, "The selected Merge Sort writeback mode is invalid.");
		else
		{
			result = simulation_.setMergeWritebackMode(static_cast<sortinglogic::MergeWritebackMode>(action.selection));
			if (result.accepted)
			{
				const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
				if (descriptor) storeAlgorithmOption(preferences_, *descriptor, "writeback_mode", action.selection);
			}
		}
		break;
	case AppActionKind::SetMergeExecutionMode:
		if (action.selection < static_cast<int>(sortinglogic::MergeExecutionMode::BottomUp) ||
			action.selection > static_cast<int>(sortinglogic::MergeExecutionMode::TopDown))
			result = sortinglogic::CommandResult(false, "The selected Merge Sort execution mode is invalid.");
		else
		{
			result = simulation_.setMergeExecutionMode(static_cast<sortinglogic::MergeExecutionMode>(action.selection));
			if (result.accepted)
			{
				const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
				if (descriptor) storeAlgorithmOption(preferences_, *descriptor, "execution_mode", action.selection);
			}
		}
		break;
	case AppActionKind::SetAudioParameter:
		preferences_.audio = audioSettingsActions::apply(preferences_.audio, action);
		break;
	case AppActionKind::SetReconstructedAudioParameter:
		preferences_.reconstructedAudio = audioSettingsActions::apply(preferences_.reconstructedAudio, action);
		break;
	case AppActionKind::RestoreReconstructedDefaults:
		preferences_.reconstructedAudio = SoundEngine::ReconstructedParameters();
		preferences_.familyAudioDefaults.clear();
		preferences_.variantAudioOffsets.clear();
		result = sortinglogic::CommandResult(true, "Reconstructed audio defaults restored.");
		break;
	case AppActionKind::SetVariantAudioLevel:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		if (!descriptor || action.index < 0 || action.index >= static_cast<int>(AlgorithmAudioChannel::Count))
			result = sortinglogic::CommandResult(false, "The selected algorithm audio control is invalid.");
		else if (!std::isfinite(action.value) || action.value < -60.0f || action.value > 12.0f)
			result = sortinglogic::CommandResult(false, "Algorithm audio offsets must be between -60 and +12 dB.");
		else
		{
			const std::string key = variantAudioSettingsKey(action.familyId, action.variantId);
			AlgorithmAudioLevels& levels = preferences_.variantAudioOffsets[key];
			levels.decibels[static_cast<std::size_t>(action.index)] = action.value;
		}
		break;
	}
	case AppActionKind::ResetVariantAudioLevels:
	{
		if (!sortinglogic::algorithmDescriptorById(action.familyId, action.variantId))
			result = sortinglogic::CommandResult(false, "The selected algorithm audio settings are not registered.");
		else
			preferences_.variantAudioOffsets.erase(variantAudioSettingsKey(action.familyId, action.variantId));
		break;
	}
	case AppActionKind::SelectAudioBand:
		if (action.selection < 0 || static_cast<std::size_t>(action.selection) >= SoundEngine::BandCount)
			result = sortinglogic::CommandResult(false, "The selected frequency band is invalid.");
		break;
	case AppActionKind::RetryAudioDevice:
		// The SDL adapter handles this app-level command; the controller keeps it harmless.
		break;
	case AppActionKind::None:
		break;
	}

	if (result.accepted && preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence &&
		(action.kind == AppActionKind::BuildTrace || action.kind == AppActionKind::GenerateAndPlay ||
		 action.kind == AppActionKind::Shuffle || action.kind == AppActionKind::InstaSort ||
		 action.kind == AppActionKind::InstaInvert || action.kind == AppActionKind::TogglePlayback || action.kind == AppActionKind::Reset ||
		 action.kind == AppActionKind::PreviousEvent || action.kind == AppActionKind::NextEvent || action.kind == AppActionKind::SeekFraction ||
		 action.kind == AppActionKind::SetPlaybackTimingMode))
		synchronizeReferenceTransport(wasPlaying);
	if (result.accepted && simulation_.hasTrace() &&
		(action.kind == AppActionKind::SetPlaybackTimingMode || action.kind == AppActionKind::DecreaseDuration ||
		 action.kind == AppActionKind::IncreaseDuration || action.kind == AppActionKind::AdjustDuration || action.kind == AppActionKind::SetDuration))
		playbackCoordinator_.markAudioPreparationPending(true);
	if (result.accepted)
		synchronizeSimulationPreferences();
	if (result.accepted && previousTrace && simulation_.currentTrace() != previousTrace &&
		action.kind != AppActionKind::BuildTrace && action.kind != AppActionKind::GenerateAndPlay)
		playbackCoordinator_.cancelQueuedPlayback();
	if (result.accepted && (action.kind == AppActionKind::Reset || action.kind == AppActionKind::PreviousEvent ||
		action.kind == AppActionKind::NextEvent || action.kind == AppActionKind::SeekFraction))
		playbackCoordinator_.cancelQueuedPlayback();
	if (previous.changed(preferences_))
	{
		persistPreferences();
		if (result.accepted && persistenceStatus_.find("Could not save") == 0)
			result.message += " " + persistenceStatus_;
	}
	return result;
}

void AppController::update(float deltaSeconds, bool audioClockAvailable)
{
	sortinglogic::TraceBuildResult completed;
	if (traceBuildService_.poll(completed))
	{
		simulation_.completeTraceBuild(std::move(completed));
		playbackCoordinator_.traceBuildCompleted(simulation_.hasTrace());
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			synchronizeReferenceTransport(false);
	}
	startPendingPlaybackIfReady();
	const bool audioClockWasLost = playbackCoordinator_.observeAudioClockAvailability(audioClockAvailable);
	if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence && simulation_.isPlaying())
	{
		if (!audioClockAvailable && !playbackCoordinator_.audioPreparationBlocksStart())
		{
			if (audioClockWasLost)
				playbackCoordinator_.setReferenceElapsedSeconds(referenceElapsedAtCursor(simulation_.cursor()));
			playbackCoordinator_.advanceReferenceClock(deltaSeconds);
			const double elapsed = playbackCoordinator_.referenceElapsedSeconds();
			const double duration = referenceDurationSeconds();
			const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
			std::size_t cursor = 0;
			if (trace && !trace->events.empty())
			{
				const std::vector<std::size_t>& eligible = trace->audioEventIndices;
				cursor = playbacktimebase::cursorForReferenceElapsed(eligible, trace->events.size(),
				elapsed, duration);
			}
			simulation_.synchronizePlaybackPosition(cursor, elapsed, duration, elapsed < duration);
		}
	}
	else if (!audioClockAvailable && !playbackCoordinator_.audioPreparationBlocksStart())
		simulation_.advance(deltaSeconds);
}

sortinglogic::RenderSnapshot AppController::snapshot() const
{
	sortinglogic::RenderSnapshot result = simulation_.snapshot();
	if (playbackCoordinator_.state(result.buildingTrace, result.hasTrace, result.playing,
		result.cursor, result.eventCount) == PlaybackCoordinatorState::Queued)
		result.status = result.buildingTrace
			? "Playback is queued and will start after trace generation."
			: "Playback is queued and will start after preparation finishes.";
	if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence && result.hasTrace)
	{
		result.durationSeconds = static_cast<float>(referenceDurationSeconds());
		result.elapsedSeconds = static_cast<float>((std::max)(0.0, (std::min)(referenceDurationSeconds(),
			static_cast<double>(result.elapsedSeconds))));
	}
	return result;
}

void AppController::applyAudioPlayhead(const SoundEngine::AudioPlayheadFeedback& feedback,
	std::uint64_t currentTimelineGeneration, std::uint64_t currentTransportRevision)
{
	if (!playbackCoordinator_.acceptsAudioPlayhead(simulation_.isPlaying(), feedback.generation,
		feedback.transportRevision, currentTimelineGeneration, currentTransportRevision, feedback.durationFrames))
		return;
	const double duration = preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence
		? playbacktimebase::secondsFromReferenceFrames(feedback.durationFrames)
		: simulation_.durationSeconds();
	const double elapsed = playbacktimebase::secondsFromReferenceFrames(feedback.timelineFrame);
	simulation_.synchronizePlaybackPosition(feedback.sourceCursor, elapsed, duration, feedback.playing);
	playbackCoordinator_.setReferenceElapsedSeconds(elapsed);
}

PlaybackCoordinatorState AppController::playbackState() const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
	return playbackCoordinator_.state(simulation_.isBuildingTrace(), static_cast<bool>(trace),
		simulation_.isPlaying(), simulation_.cursor(), trace ? trace->events.size() : 0);
}

double AppController::referenceDurationSeconds() const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
	if (!trace) return 0.0;
	return playbacktimebase::durationForReferenceEvents(trace->audioEventIndices.size());
}

double AppController::referenceElapsedAtCursor(std::size_t cursor) const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
	if (!trace) return 0.0;
	return playbacktimebase::elapsedForReferenceCursor(trace->audioEventIndices, cursor);
}

void AppController::synchronizeReferenceTransport(bool wasPlaying)
{
	if (preferences_.simulation.playbackTimingMode != SoundEngine::PlaybackTimingMode::ReferenceCadence) return;
	const std::size_t currentCursor = simulation_.cursor();
	const double duration = referenceDurationSeconds();
	if (!wasPlaying || !simulation_.isPlaying())
		playbackCoordinator_.setReferenceElapsedSeconds(referenceElapsedAtCursor(currentCursor));
	simulation_.synchronizePlaybackPosition(currentCursor, playbackCoordinator_.referenceElapsedSeconds(),
		duration, simulation_.isPlaying());
}

SoundEngine::Parameters AppController::effectiveAudio() const
{
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(simulation_.algorithm());
	return descriptor ? effectiveAudioParameters(preferences_, *descriptor) : preferences_.audio;
}

void AppController::synchronizeSimulationPreferences()
{
	preferences_.simulation.itemCount = static_cast<int>(simulation_.itemCount());
	preferences_.simulation.durationSeconds = simulation_.durationSeconds();
	preferences_.simulation.descending = simulation_.direction() == sortinglogic::SortDirection::Descending;
	preferences_.simulation.algorithm = static_cast<int>(simulation_.algorithm());
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(simulation_.algorithm());
	if (descriptor)
	{
		preferences_.simulation.familyId = descriptor->familyId;
		preferences_.simulation.variantId = descriptor->variantId;
	}
	preferences_.simulation.profile = simulation_.profile();
	preferences_.simulation.domain = simulation_.domain();
	preferences_.simulation.pattern = simulation_.pattern();
	preferences_.simulation.mergeBufferVisualizationEnabled = simulation_.mergeBufferVisualizationEnabled();
	preferences_.simulation.completionScanEnabled = simulation_.completionScanEnabled();
}

void AppController::persistPreferences()
{
	std::string error;
	if (!settingsPersistence_.save(preferences_, &error))
		persistenceStatus_ = "Could not save preferences" + (error.empty() ? std::string(".") : ": " + error);
	else
		persistenceStatus_ = "Preferences saved.";
}
