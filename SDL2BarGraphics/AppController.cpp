#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "BarCountSteps.h"

namespace
{
	bool sameAudio(const SoundEngine::Parameters& a, const SoundEngine::Parameters& b)
	{
		if (a.masterLevel != b.masterLevel || a.bedLevel != b.bedLevel || a.bedHarmonicExponent != b.bedHarmonicExponent ||
			a.includeSub300HzBed != b.includeSub300HzBed || a.bedComb.enabled != b.bedComb.enabled ||
			a.bedComb.applyPhase != b.bedComb.applyPhase || a.bedComb.delayMicroseconds != b.bedComb.delayMicroseconds ||
			a.bedComb.phaseOffsetDegrees != b.bedComb.phaseOffsetDegrees || a.bedComb.wetAtLowFrequency != b.bedComb.wetAtLowFrequency ||
			a.bedComb.wetAtHighFrequency != b.bedComb.wetAtHighFrequency || a.bedComb.fadeStartHz != b.bedComb.fadeStartHz ||
			a.bedComb.fadeEndHz != b.bedComb.fadeEndHz || a.bedComb.fadeExponent != b.bedComb.fadeExponent ||
			a.comparisonBoostDb != b.comparisonBoostDb || a.heldKeyBoostDb != b.heldKeyBoostDb ||
			a.tauBelowBins != b.tauBelowBins || a.tauAboveBins != b.tauAboveBins || a.chorusMix != b.chorusMix ||
			a.chorusRateHz != b.chorusRateHz || a.chorusDepthMs != b.chorusDepthMs || a.chorusWeighting != b.chorusWeighting ||
			a.attackMs != b.attackMs || a.releaseMs != b.releaseMs || a.mappingMode != b.mappingMode ||
			a.comparisonSpreadMode != b.comparisonSpreadMode || a.enabled != b.enabled)
			return false;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			if (a.bandTrimDb[i] != b.bandTrimDb[i]) return false;
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
			if (a.roleBoostDb[i] != b.roleBoostDb[i]) return false;
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
			if (a.accentRouting[i] != b.accentRouting[i] || a.accentGain[i] != b.accentGain[i]) return false;
		return true;
	}

	bool sameLevels(const AlgorithmAudioLevels& a, const AlgorithmAudioLevels& b)
	{
		return a.decibels == b.decibels;
	}

	template <typename Key>
	bool sameLevelMap(const std::map<Key, AlgorithmAudioLevels>& a, const std::map<Key, AlgorithmAudioLevels>& b)
	{
		if (a.size() != b.size()) return false;
		typename std::map<Key, AlgorithmAudioLevels>::const_iterator left = a.begin();
		typename std::map<Key, AlgorithmAudioLevels>::const_iterator right = b.begin();
		for (; left != a.end(); ++left, ++right)
			if (left->first != right->first || !sameLevels(left->second, right->second)) return false;
		return true;
	}

	bool samePreferences(const UserPreferences& left, const UserPreferences& right)
	{
		return left.visual.palette == right.visual.palette &&
			left.visual.spacing == right.visual.spacing &&
			left.visual.panelExpanded == right.visual.panelExpanded &&
			left.simulation.itemCount == right.simulation.itemCount &&
			left.simulation.durationSeconds == right.simulation.durationSeconds &&
			left.simulation.descending == right.simulation.descending &&
			left.simulation.algorithm == right.simulation.algorithm &&
			left.simulation.familyId == right.simulation.familyId &&
			left.simulation.variantId == right.simulation.variantId &&
			left.simulation.profile == right.simulation.profile &&
			left.simulation.domain == right.simulation.domain &&
			left.simulation.pattern == right.simulation.pattern &&
			left.simulation.shellGapSequence == right.simulation.shellGapSequence &&
			left.simulation.mergeWritebackMode == right.simulation.mergeWritebackMode &&
		left.simulation.mergeExecutionMode == right.simulation.mergeExecutionMode && sameAudio(left.audio, right.audio) &&
			sameLevelMap(left.familyAudioDefaults, right.familyAudioDefaults) &&
			sameLevelMap(left.variantAudioOffsets, right.variantAudioOffsets) &&
			left.algorithmOptions == right.algorithmOptions;
	}

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
	persistenceStatus_("Preferences loaded.")
{
	simulation_.setDuration(static_cast<float>(preferences_.simulation.durationSeconds));
	simulation_.setDirection(preferences_.simulation.descending
		? sortinglogic::SortDirection::Descending
		: sortinglogic::SortDirection::Ascending);
	const sortinglogic::AlgorithmDescriptor* savedAlgorithm = sortinglogic::algorithmDescriptorById(
		preferences_.simulation.familyId, preferences_.simulation.variantId);
	if (savedAlgorithm && sortinglogic::algorithmSupports(savedAlgorithm->id, preferences_.simulation.profile))
		simulation_.selectAlgorithm(savedAlgorithm->id);
	simulation_.setShellGapSequence(preferences_.simulation.shellGapSequence);
	simulation_.setMergeWritebackMode(preferences_.simulation.mergeWritebackMode);
	simulation_.setMergeExecutionMode(preferences_.simulation.mergeExecutionMode);
}

AppController::~AppController() = default;

sortinglogic::CommandResult AppController::dispatch(const AppAction& action)
{
	sortinglogic::CommandResult result(true, "");
	const UserPreferences previous = preferences_;
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
	case AppActionKind::Run:
	{
		sortinglogic::TraceBuildRequest request;
		result = simulation_.beginTraceBuild(request);
		if (result.accepted)
		{
			std::string error;
			if (!traceBuildService_.submit(request, &error))
			{
				sortinglogic::TraceBuildResult failed;
				failed.error = error.empty() ? "Trace worker is unavailable." : error;
				simulation_.completeTraceBuild(failed);
				result = sortinglogic::CommandResult(false, failed.error);
			}
		}
		break;
	}
	case AppActionKind::TogglePlayback:
		result = simulation_.togglePlayback();
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
	case AppActionKind::DecreaseDuration:
		result = simulation_.setDuration(std::max(sortinglogic::MinRunDurationSeconds, simulation_.durationSeconds() - 1.0f));
		break;
	case AppActionKind::IncreaseDuration:
		result = simulation_.setDuration(std::min(sortinglogic::MaxRunDurationSeconds, simulation_.durationSeconds() + 1.0f));
		break;
	case AppActionKind::SeekFraction:
		result = simulation_.seekToFraction(action.value);
		break;
	case AppActionKind::SetPalette:
		if (action.selection < static_cast<int>(PaletteId::Cyan) || action.selection > static_cast<int>(PaletteId::Light))
			result = sortinglogic::CommandResult(false, "The selected color palette is invalid.");
		else
			preferences_.visual.palette = static_cast<PaletteId>(action.selection);
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
			action.selection > static_cast<int>(sortinglogic::InputPattern::OrganPipe))
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
			result = sortinglogic::CommandResult(false, "No option handler is registered for this algorithm option group.");
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

	if (result.accepted)
		synchronizeSimulationPreferences();
	if (!samePreferences(previous, preferences_))
	{
		persistIfChanged(previous);
		if (result.accepted && persistenceStatus_.find("Could not save") == 0)
			result.message += " " + persistenceStatus_;
	}
	return result;
}

void AppController::update(float deltaSeconds)
{
	sortinglogic::TraceBuildResult completed;
	if (traceBuildService_.poll(completed))
		simulation_.completeTraceBuild(std::move(completed));
	simulation_.advance(deltaSeconds);
}

sortinglogic::RenderSnapshot AppController::snapshot() const
{
	return simulation_.snapshot();
}

SoundEngine::Parameters AppController::effectiveAudio() const
{
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(simulation_.algorithm());
	return descriptor ? effectiveAudioParameters(preferences_, *descriptor) : preferences_.audio;
}

void AppController::synchronizeSimulationPreferences()
{
	preferences_.simulation.itemCount = static_cast<int>(simulation_.itemCount());
	preferences_.simulation.durationSeconds = static_cast<int>(simulation_.durationSeconds());
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
}

void AppController::persistIfChanged(const UserPreferences& previous)
{
	if (samePreferences(previous, preferences_))
		return;
	std::string error;
	if (!settingsPersistence_.save(preferences_, &error))
		persistenceStatus_ = "Could not save preferences" + (error.empty() ? std::string(".") : ": " + error);
	else
		persistenceStatus_ = "Preferences saved.";
}
