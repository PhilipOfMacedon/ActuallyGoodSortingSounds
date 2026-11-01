#include "UiScreenModelAdapter.h"

#include "PlaybackTimebase.h"

#include <iomanip>
#include <set>
#include <sstream>

namespace
{
	std::string formatDuration(float seconds)
	{
		std::ostringstream value;
		value << std::setprecision(6) << seconds << " sec";
		return value.str();
	}
}

UiScreenModel buildUiScreenModel(const sortinglogic::RenderSnapshot& snapshot,
	const UserPreferences& preferences, bool playbackPending, const std::string& applicationStatus)
{
	UiScreenModel model;
	model.preset = preferences.appearance.activePreset;
	model.setupEnabled = !snapshot.buildingTrace && !snapshot.playing && !playbackPending;
	model.runEnabled = model.setupEnabled && snapshot.runCompatible;
	model.playEnabled = snapshot.playing || snapshot.buildingTrace || snapshot.hasTrace || snapshot.runCompatible;
	model.countEditable = model.setupEnabled;
	const bool referenceCadence = preferences.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence;
	model.durationEditable = model.setupEnabled && !referenceCadence;
	model.descending = snapshot.direction == sortinglogic::SortDirection::Descending;
	model.hasTrace = snapshot.hasTrace;
	model.buildingTrace = snapshot.buildingTrace;
	model.playing = snapshot.playing;
	model.completed = snapshot.complete;
	model.itemCount = snapshot.itemCount;
	model.cursor = snapshot.cursor;
	model.eventCount = snapshot.eventCount;
	model.audioEligibleEventCount = snapshot.audioEligibleEventCount;
	model.seed = snapshot.inputSeed;
	model.algorithm = sortinglogic::algorithmDisplayName(snapshot.algorithm);
	model.inputDomain = sortinglogic::inputDomainName(snapshot.domain);
	model.inputPattern = sortinglogic::inputPatternName(snapshot.pattern);
	model.direction = model.descending ? "Descending" : "Ascending";
	const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptor(snapshot.algorithm);
	if (algorithm)
	{
		std::set<std::string> seenProperties;
		for (std::size_t optionIndex = 0; optionIndex < algorithm->supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm->supportedOptions[optionIndex];
			const std::string propertyId(option.groupId);
			if (!seenProperties.insert(propertyId).second) continue;
			int selectedValue = option.value;
			snapshot.configuration.optionValue(propertyId, selectedValue);
			for (std::size_t candidateIndex = 0; candidateIndex < algorithm->supportedOptions.size(); ++candidateIndex)
			{
				const sortinglogic::AlgorithmOptionDescriptor& candidate = algorithm->supportedOptions[candidateIndex];
				if (propertyId != candidate.groupId || candidate.value != selectedValue) continue;
				if (!model.algorithmOptionsSummary.empty()) model.algorithmOptionsSummary += " | ";
				model.algorithmOptionsSummary += (candidate.caption ? candidate.caption : candidate.groupId);
				model.algorithmOptionsSummary += ": ";
				model.algorithmOptionsSummary += candidate.displayName;
				break;
			}
		}
		UiAlgorithmCapabilities& capabilities = model.algorithmCapabilities;
		capabilities.available = true;
		capabilities.stable = algorithm->stable;
		capabilities.inPlace = algorithm->inPlace;
		capabilities.supportsNormalizedFloat = algorithm->supportsNormalizedFloat;
		capabilities.supportsIntegerKeys = algorithm->supportsIntegerKeys;
		capabilities.safeMaximumItemCount = algorithm->safeMaxItemCount;
		capabilities.timeComplexity = algorithm->timeComplexity;
		capabilities.spaceComplexity = algorithm->spaceComplexity;
		capabilities.behavior = algorithm->behavior;
		capabilities.compatibilityMessage = snapshot.compatibilityMessage;
		for (std::size_t i = 0; i < algorithm->metrics.size(); ++i)
		{
			const std::map<std::string, std::uint64_t>::const_iterator value = snapshot.metrics.algorithm.find(algorithm->metrics[i].key);
			if (value != snapshot.metrics.algorithm.end())
				capabilities.metrics.push_back(UiTelemetryValue(algorithm->metrics[i].key, algorithm->metrics[i].displayName,
					std::to_string(value->second)));
		}
	}
	if (referenceCadence)
	{
		std::ostringstream duration;
		duration << std::fixed << std::setprecision(2)
			<< playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount) << " sec";
		model.durationCaption = duration.str();
	}
	else model.durationCaption = formatDuration(snapshot.durationSeconds);
	if (snapshot.playing) { model.transport = UiTransportState::Playing; model.playCaption = "Pause"; }
	else if (playbackPending) { model.transport = UiTransportState::Queued; model.playCaption = "Cancel queued"; }
	else if (snapshot.buildingTrace) { model.transport = UiTransportState::Building; model.playCaption = "Play when ready"; }
	else if (!snapshot.hasTrace) { model.transport = UiTransportState::NoTrace; model.playCaption = "Generate & Play"; }
	else if (snapshot.complete) { model.transport = UiTransportState::Replay; model.playCaption = "Replay"; }
	else { model.transport = UiTransportState::Ready; model.playCaption = "Play"; }
	model.status = applicationStatus.empty() ? snapshot.status : applicationStatus;
	model.telemetry.push_back(UiTelemetryValue("cursor", "Event", std::to_string(snapshot.cursor) + "/" + std::to_string(snapshot.eventCount)));
	model.telemetry.push_back(UiTelemetryValue("seed", "Seed", std::to_string(snapshot.inputSeed)));
	if (snapshot.hasTrace)
	{
		model.telemetry.push_back(UiTelemetryValue("comparisons", "Cmp", std::to_string(snapshot.metrics.comparisons)));
		model.telemetry.push_back(UiTelemetryValue("swaps", "Swap", std::to_string(snapshot.metrics.swaps)));
		model.telemetry.push_back(UiTelemetryValue("main_writes", "Main", std::to_string(snapshot.metrics.primaryWrites)));
		model.telemetry.push_back(UiTelemetryValue("auxiliary", "Aux", std::to_string(snapshot.metrics.auxiliaryItemWrites) + "/" +
			std::to_string(snapshot.metrics.auxiliaryCounterUpdates)));
		model.telemetry.push_back(UiTelemetryValue("peak_auxiliary", "Peak", std::to_string(snapshot.metrics.peakAuxiliaryCapacity)));
	}
	return model;
}
