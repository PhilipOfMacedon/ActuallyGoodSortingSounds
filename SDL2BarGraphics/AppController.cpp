#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "BarCountSteps.h"

namespace
{
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

	void setAudioParameter(SoundEngine::Parameters& audio, const AppAction& action)
	{
		switch (action.audioParameter)
		{
		case AudioParameterId::Enabled: audio.enabled = action.selection != 0; break;
		case AudioParameterId::MasterLevel: audio.masterLevel = action.value; break;
		case AudioParameterId::BedLevel: audio.bedLevel = action.value; break;
		case AudioParameterId::BedHarmonicExponent: audio.bedHarmonicExponent = action.value; break;
		case AudioParameterId::IncludeSub300HzBed: audio.includeSub300HzBed = action.selection != 0; break;
		case AudioParameterId::CombEnabled: audio.bedComb.enabled = action.selection != 0; break;
		case AudioParameterId::CombApplyPhase: audio.bedComb.applyPhase = action.selection != 0; break;
		case AudioParameterId::CombDelayMicroseconds: audio.bedComb.delayMicroseconds = action.value; break;
		case AudioParameterId::CombPhaseOffsetDegrees: audio.bedComb.phaseOffsetDegrees = action.value; break;
		case AudioParameterId::CombWetLow: audio.bedComb.wetAtLowFrequency = action.value; break;
		case AudioParameterId::CombWetHigh: audio.bedComb.wetAtHighFrequency = action.value; break;
		case AudioParameterId::CombFadeStartHz: audio.bedComb.fadeStartHz = action.value; break;
		case AudioParameterId::CombFadeEndHz: audio.bedComb.fadeEndHz = action.value; break;
		case AudioParameterId::CombFadeExponent: audio.bedComb.fadeExponent = action.value; break;
		case AudioParameterId::ComparisonBoostDb: audio.comparisonBoostDb = action.value; break;
		case AudioParameterId::HeldKeyBoostDb: audio.heldKeyBoostDb = action.value; break;
		case AudioParameterId::TauBelowBins: audio.tauBelowBins = action.value; break;
		case AudioParameterId::TauAboveBins: audio.tauAboveBins = action.value; break;
		case AudioParameterId::ChorusMix: audio.chorusMix = action.value; break;
		case AudioParameterId::ChorusRateHz: audio.chorusRateHz = action.value; break;
		case AudioParameterId::ChorusDepthMs: audio.chorusDepthMs = action.value; break;
		case AudioParameterId::ChorusWeighting: audio.chorusWeighting = static_cast<SoundEngine::ChorusWeighting>(action.selection); break;
		case AudioParameterId::AttackMs: audio.attackMs = action.value; break;
		case AudioParameterId::ReleaseMs: audio.releaseMs = action.value; break;
		case AudioParameterId::MappingMode: audio.mappingMode = static_cast<SoundEngine::MappingMode>(action.selection); break;
		case AudioParameterId::SpreadMode: audio.comparisonSpreadMode = static_cast<SoundEngine::ComparisonSpreadMode>(action.selection); break;
		case AudioParameterId::BandTrim:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::BandCount)
				audio.bandTrimDb[static_cast<std::size_t>(action.index)] = action.value;
			break;
		case AudioParameterId::AccentRouting:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::AccentCategoryCount)
				audio.accentRouting[static_cast<std::size_t>(action.index)] = action.selection != 0;
			break;
		case AudioParameterId::AccentGain:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::AccentCategoryCount)
				audio.accentGain[static_cast<std::size_t>(action.index)] = action.value;
			break;
		}
		// Reuse the DSP's input sanitation at the app boundary so an invalid action
		// cannot poison settings persistence or callback parameters.
		SoundEngine::Synthesizer validator;
		validator.setParameters(audio);
		audio = validator.parameters();
	}

	void setReconstructedParameter(SoundEngine::ReconstructedParameters& audio, const AppAction& action)
	{
		switch (action.reconstructedAudioParameter)
		{
		case ReconstructedAudioParameterId::Enabled: audio.audioEnabled = action.selection != 0; break;
		case ReconstructedAudioParameterId::MasterLevel: audio.masterLevel = action.value; break;
		case ReconstructedAudioParameterId::IdlePolicy: audio.idlePolicy = static_cast<SoundEngine::ReconstructedIdlePolicy>(action.selection); break;
		case ReconstructedAudioParameterId::SpecialReleaseMilliseconds: audio.specialReleaseMilliseconds = action.value; break;
		case ReconstructedAudioParameterId::MinimumFrequencyHz: audio.minimumFrequencyHz = action.value; break;
		case ReconstructedAudioParameterId::MaximumFrequencyHz: audio.maximumFrequencyHz = action.value; break;
		case ReconstructedAudioParameterId::Mapping: audio.mapping = static_cast<SoundEngine::ReconstructedMapping>(action.selection); break;
		case ReconstructedAudioParameterId::ExcitationClockHz: audio.excitationClockHz = action.value; break;
		case ReconstructedAudioParameterId::CarrierCycles: audio.carrierCycles = action.value; break;
		case ReconstructedAudioParameterId::TukeyFraction: audio.tukeyFraction = action.value; break;
		case ReconstructedAudioParameterId::SecondOperandDelayMicroseconds: audio.secondOperandDelayMicroseconds = action.value; break;
		case ReconstructedAudioParameterId::LowPassEnabled: audio.lowPassEnabled = action.selection != 0; break;
		case ReconstructedAudioParameterId::LowPassCutoffHz: audio.lowPassCutoffHz = action.value; break;
		}
		SoundEngine::ReconstructedSynthesizer validator;
		validator.setParameters(audio);
		audio = validator.parameters();
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
	traceBuildService_(),
	persistenceStatus_("Preferences loaded."), hostReferenceElapsedSeconds_(0.0), previousAudioClockAvailable_(false),
	audioPreparationGateEnabled_(false),
	audioPreparationPending_(false), playbackPending_(false)
{
	simulation_.setDuration(static_cast<float>(preferences_.simulation.durationSeconds));
	simulation_.setDirection(preferences_.simulation.descending
		? sortinglogic::SortDirection::Descending
		: sortinglogic::SortDirection::Ascending);
	const sortinglogic::AlgorithmDescriptor* savedAlgorithm = sortinglogic::algorithmDescriptorById(
		preferences_.simulation.familyId, preferences_.simulation.variantId);
	if (savedAlgorithm && sortinglogic::algorithmSupports(savedAlgorithm->id, preferences_.simulation.domain))
		simulation_.selectAlgorithm(savedAlgorithm->id);
	simulation_.setShellGapSequence(preferences_.simulation.shellGapSequence);
	simulation_.setMergeWritebackMode(preferences_.simulation.mergeWritebackMode);
	simulation_.setMergeExecutionMode(preferences_.simulation.mergeExecutionMode);
	simulation_.setMergeBufferVisualizationEnabled(preferences_.simulation.mergeBufferVisualizationEnabled);
	simulation_.setCompletionScanEnabled(preferences_.simulation.completionScanEnabled);
}

AppController::~AppController() = default;

sortinglogic::CommandResult AppController::requestTraceBuild(bool playWhenReady)
{
	playbackPending_ = false;
	sortinglogic::TraceBuildRequest request;
	sortinglogic::CommandResult result = simulation_.beginTraceBuild(request);
	if (!result.accepted) return result;
	const sortinglogic::AlgorithmDescriptor* traceAlgorithm = sortinglogic::algorithmDescriptor(request.algorithm);
	if (traceAlgorithm)
	{
		std::set<std::string> configuredGroups;
		for (std::size_t optionIndex = 0; optionIndex < traceAlgorithm->supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = traceAlgorithm->supportedOptions[optionIndex];
			const std::string group(option.groupId);
			if (!configuredGroups.insert(group).second) continue;
			const int value = algorithmOptionValue(preferences_, *traceAlgorithm, group);
			if (group == "heap_arity" || group == "bucket_count") request.bucketCount = value;
			else if (group == "grail_buffer_mode") request.bucketCount = value;
			else if (group == "radix_base") request.radixBase = value;
			else if (group == "time_magnitude") request.timeMagnitude = value;
			else if (group == "rotate_buffer_capacity") request.rotateMergeBufferSize = value;
		}
	}

	playbackPending_ = playWhenReady;
	std::string error;
	if (!traceBuildService_.submit(request, &error))
	{
		sortinglogic::TraceBuildResult failed;
		failed.error = error.empty() ? "Trace worker is unavailable." : error;
		simulation_.completeTraceBuild(failed);
		playbackPending_ = false;
		return sortinglogic::CommandResult(false, failed.error);
	}
	if (playWhenReady)
		result.message = "Generating the trace; playback will start automatically when preparation finishes.";
	return result;
}

void AppController::setAudioPreparationPending(bool pending)
{
	audioPreparationGateEnabled_ = true;
	audioPreparationPending_ = pending;
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
	if (!playbackPending_) return;
	if (simulation_.isBuildingTrace()) return;
	if (!simulation_.currentTrace())
	{
		playbackPending_ = false;
		return;
	}
	if (audioPreparationGateEnabled_ && audioPreparationPending_) return;

	playbackPending_ = false;
	const bool wasPlaying = simulation_.isPlaying();
	const sortinglogic::CommandResult started = simulation_.togglePlayback();
	if (started.accepted && preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
		synchronizeReferenceTransport(wasPlaying);
}

sortinglogic::CommandResult AppController::dispatch(const AppAction& action)
{
	sortinglogic::CommandResult result(true, "");
	const UserPreferences previous = preferences_;
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
			playbackPending_ = false;
			result = simulation_.togglePlayback();
		}
		else if (playbackPending_)
		{
			playbackPending_ = false;
			result = sortinglogic::CommandResult(true, "Queued playback cancelled.");
		}
		else if (simulation_.isBuildingTrace())
		{
			playbackPending_ = true;
			result = sortinglogic::CommandResult(true, "Playback queued; it will start when the trace and audio timeline are ready.");
		}
		else if (!simulation_.currentTrace())
			result = requestTraceBuild(true);
		else if (audioPreparationGateEnabled_ && audioPreparationPending_)
		{
			playbackPending_ = true;
			result = sortinglogic::CommandResult(true, "Playback queued until audio preparation finishes.");
		}
		else
			result = simulation_.togglePlayback();
		break;
	case AppActionKind::TogglePlayback:
		if (audioPreparationGateEnabled_ && audioPreparationPending_ && !simulation_.isPlaying())
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
		else if (descriptor->id == sortinglogic::SortAlgorithm::Shell && action.optionGroupId == "gap_sequence")
		{
			result = simulation_.setShellGapSequence(static_cast<sortinglogic::ShellGapSequence>(option->value));
			if (result.accepted) preferences_.simulation.shellGapSequence = simulation_.shellGapSequence();
		}
		else if (descriptor->id == sortinglogic::SortAlgorithm::Merge && action.optionGroupId == "execution_mode")
		{
			result = simulation_.setMergeExecutionMode(static_cast<sortinglogic::MergeExecutionMode>(option->value));
			if (result.accepted) preferences_.simulation.mergeExecutionMode = simulation_.mergeExecutionMode();
		}
		else if (descriptor->id == sortinglogic::SortAlgorithm::Merge && action.optionGroupId == "writeback_mode")
		{
			result = simulation_.setMergeWritebackMode(static_cast<sortinglogic::MergeWritebackMode>(option->value));
			if (result.accepted) preferences_.simulation.mergeWritebackMode = simulation_.mergeWritebackMode();
		}
		else
		{
			const sortinglogic::RenderSnapshot snapshot = simulation_.snapshot();
			if (snapshot.buildingTrace || snapshot.playing)
				result = sortinglogic::CommandResult(false, "Algorithm options are locked while a trace is building or playing.");
			else
				result = sortinglogic::CommandResult(true, "The selected option will be used for the next trace.");
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
				preferences_.simulation.shellGapSequence = static_cast<sortinglogic::ShellGapSequence>(action.selection);
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
				preferences_.simulation.mergeWritebackMode = simulation_.mergeWritebackMode();
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
				preferences_.simulation.mergeExecutionMode = simulation_.mergeExecutionMode();
				const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
				if (descriptor) storeAlgorithmOption(preferences_, *descriptor, "execution_mode", action.selection);
			}
		}
		break;
	case AppActionKind::SetAudioParameter:
		setAudioParameter(preferences_.audio, action);
		break;
	case AppActionKind::SetReconstructedAudioParameter:
		setReconstructedParameter(preferences_.reconstructedAudio, action);
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
	if (result.accepted && simulation_.snapshot().hasTrace &&
		(action.kind == AppActionKind::SetPlaybackTimingMode || action.kind == AppActionKind::DecreaseDuration ||
		 action.kind == AppActionKind::IncreaseDuration || action.kind == AppActionKind::AdjustDuration || action.kind == AppActionKind::SetDuration))
		audioPreparationPending_ = true;
	if (result.accepted)
		synchronizeSimulationPreferences();
	if (result.accepted && previousTrace && simulation_.currentTrace() != previousTrace &&
		action.kind != AppActionKind::BuildTrace && action.kind != AppActionKind::GenerateAndPlay)
		playbackPending_ = false;
	if (result.accepted && (action.kind == AppActionKind::Reset || action.kind == AppActionKind::PreviousEvent ||
		action.kind == AppActionKind::NextEvent || action.kind == AppActionKind::SeekFraction))
		playbackPending_ = false;
	if (!(previous == preferences_))
	{
		persistIfChanged(previous);
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
		hostReferenceElapsedSeconds_ = 0.0;
		audioPreparationPending_ = static_cast<bool>(simulation_.currentTrace());
		if (!simulation_.currentTrace()) playbackPending_ = false;
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			synchronizeReferenceTransport(false);
	}
	startPendingPlaybackIfReady();
	if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence && simulation_.isPlaying())
	{
		if (!audioClockAvailable && !(audioPreparationGateEnabled_ && audioPreparationPending_))
		{
			if (previousAudioClockAvailable_)
				hostReferenceElapsedSeconds_ = referenceElapsedAtCursor(simulation_.snapshot().cursor);
			hostReferenceElapsedSeconds_ += (std::max)(0.0f, deltaSeconds);
			const double duration = referenceDurationSeconds();
			const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
			std::size_t cursor = 0;
			if (trace && !trace->events.empty())
			{
				const std::vector<std::size_t>& eligible = trace->audioEventIndices;
				const std::size_t due = (std::min)(eligible.size(), static_cast<std::size_t>(std::floor(hostReferenceElapsedSeconds_ * 300.0)) + 1);
				if (hostReferenceElapsedSeconds_ >= duration) cursor = trace->events.size();
				else if (due > 0) cursor = eligible[due - 1] + 1;
			}
			simulation_.synchronizePlaybackPosition(cursor, hostReferenceElapsedSeconds_, duration,
				hostReferenceElapsedSeconds_ < duration);
		}
	}
	else if (!audioClockAvailable && !(audioPreparationGateEnabled_ && audioPreparationPending_))
		simulation_.advance(deltaSeconds);
	previousAudioClockAvailable_ = audioClockAvailable;
}

sortinglogic::RenderSnapshot AppController::snapshot() const
{
	sortinglogic::RenderSnapshot result = simulation_.snapshot();
	if (playbackPending_)
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
	if (!simulation_.isPlaying() || feedback.generation == 0 || feedback.generation != currentTimelineGeneration ||
		feedback.transportRevision != currentTransportRevision || feedback.durationFrames == 0)
		return;
	const double duration = preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence
		? static_cast<double>(feedback.durationFrames) / 44100.0
		: simulation_.durationSeconds();
	const double elapsed = static_cast<double>(feedback.timelineFrame) / 44100.0;
	simulation_.synchronizePlaybackPosition(feedback.sourceCursor, elapsed, duration, feedback.playing);
	hostReferenceElapsedSeconds_ = elapsed;
}

double AppController::referenceDurationSeconds() const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
	if (!trace) return 0.0;
	return static_cast<double>(trace->audioEventIndices.size()) / 300.0;
}

double AppController::referenceElapsedAtCursor(std::size_t cursor) const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation_.currentTrace();
	if (!trace) return 0.0;
	const std::vector<std::size_t>& eligible = trace->audioEventIndices;
	const auto end = std::lower_bound(eligible.begin(), eligible.end(), cursor);
	return static_cast<double>(end - eligible.begin()) / 300.0;
}

void AppController::synchronizeReferenceTransport(bool wasPlaying)
{
	if (preferences_.simulation.playbackTimingMode != SoundEngine::PlaybackTimingMode::ReferenceCadence) return;
	const sortinglogic::RenderSnapshot current = simulation_.snapshot();
	const double duration = referenceDurationSeconds();
	if (!wasPlaying || !simulation_.isPlaying())
		hostReferenceElapsedSeconds_ = referenceElapsedAtCursor(current.cursor);
	simulation_.synchronizePlaybackPosition(current.cursor, hostReferenceElapsedSeconds_, duration, simulation_.isPlaying());
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
	preferences_.simulation.shellGapSequence = simulation_.shellGapSequence();
	preferences_.simulation.mergeWritebackMode = simulation_.mergeWritebackMode();
	preferences_.simulation.mergeExecutionMode = simulation_.mergeExecutionMode();
	preferences_.simulation.mergeBufferVisualizationEnabled = simulation_.mergeBufferVisualizationEnabled();
	preferences_.simulation.completionScanEnabled = simulation_.completionScanEnabled();
}

void AppController::persistIfChanged(const UserPreferences& previous)
{
	if (previous == preferences_)
		return;
	std::string error;
	if (!settingsPersistence_.save(preferences_, &error))
		persistenceStatus_ = "Could not save preferences" + (error.empty() ? std::string(".") : ": " + error);
	else
		persistenceStatus_ = "Preferences saved.";
}
