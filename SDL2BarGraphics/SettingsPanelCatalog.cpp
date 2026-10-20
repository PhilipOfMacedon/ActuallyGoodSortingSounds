#include "SettingsPanelCatalog.h"

#include <set>

#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TracePresentationPolicy.h"
#include "UserSettings.h"

namespace
{
	std::string audioControlId(int channelIndex)
	{
		return std::string("algorithm_audio_") + (channelIndex < 10 ? "0" : "") +
			std::to_string(channelIndex);
	}
}

std::vector<std::string> settingsPaletteOptions()
{
	std::vector<std::string> names;
	const std::vector<PaletteId>& catalog = paletteCatalog();
	for (std::size_t i = 0; i < catalog.size(); ++i)
		names.push_back(paletteDisplayName(catalog[i]));
	return names;
}

std::vector<std::string> settingsUiPresetOptions()
{
	std::vector<std::string> names;
	const std::vector<UiPresetId>& catalog = uiPresetCatalog();
	for (std::size_t i = 0; i < catalog.size(); ++i)
		names.push_back(uiPresetDefinition(catalog[i]).displayName);
	return names;
}

std::vector<std::string> settingsBarSpacingOptions()
{
	return std::vector<std::string>{ "None", "Compact", "Comfortable" };
}

std::vector<std::string> settingsInputDomainOptions()
{
	std::vector<std::string> names;
	for (int domain = static_cast<int>(sortinglogic::InputDomain::NormalizedFloats);
		domain <= static_cast<int>(sortinglogic::InputDomain::IntegerPermutation); ++domain)
		names.push_back(sortinglogic::inputDomainName(static_cast<sortinglogic::InputDomain>(domain)));
	return names;
}

std::vector<std::string> settingsInputPatternOptions()
{
	std::vector<std::string> names;
	for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
		pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
		names.push_back(sortinglogic::inputPatternName(static_cast<sortinglogic::InputPattern>(pattern)));
	return names;
}

std::vector<std::string> settingsBooleanOptions()
{
	return std::vector<std::string>{ "Off", "On" };
}

SettingsPanelAlgorithmPresentation settingsPanelAlgorithmPresentation(
	const sortinglogic::AlgorithmDescriptor* algorithm)
{
	SettingsPanelAlgorithmPresentation presentation;
	if (algorithm)
	{
		presentation.familyId = algorithm->familyId;
		presentation.variantId = algorithm->variantId;
		presentation.showsMergeBufferVisualization = algorithm->id == sortinglogic::SortAlgorithm::Merge;

		std::set<std::string> groups;
		for (std::size_t option = 0; option < algorithm->supportedOptions.size(); ++option)
		{
			const std::string groupId(algorithm->supportedOptions[option].groupId);
			if (groups.insert(groupId).second) presentation.optionGroupIds.push_back(groupId);
		}
	}

	const std::vector<AlgorithmAudioChannel> roleChannels = algorithm
		? supportedAudioRoleChannels(*algorithm) : std::vector<AlgorithmAudioChannel>();
	for (int channelIndex = 0; channelIndex < static_cast<int>(AlgorithmAudioChannel::Count); ++channelIndex)
	{
		const AlgorithmAudioChannel channel = static_cast<AlgorithmAudioChannel>(channelIndex);
		bool visible = false;
		switch (channel)
		{
		case AlgorithmAudioChannel::Comparison:
			visible = true;
			break;
		case AlgorithmAudioChannel::ProbeAccent:
			visible = algorithm && sortinglogic::supportsAudioControlCategory(*algorithm,
				sortinglogic::AudioControlCategory::Probe);
			break;
		case AlgorithmAudioChannel::MainChangeAccent:
			visible = algorithm && sortinglogic::supportsAudioControlCategory(*algorithm,
				sortinglogic::AudioControlCategory::MainChange);
			break;
		case AlgorithmAudioChannel::AuxiliaryAccent:
			visible = algorithm && sortinglogic::supportsAudioControlCategory(*algorithm,
				sortinglogic::AudioControlCategory::Auxiliary);
			break;
		case AlgorithmAudioChannel::DiscardAccent:
			visible = algorithm && sortinglogic::supportsAudioControlCategory(*algorithm,
				sortinglogic::AudioControlCategory::Discard);
			break;
		default:
			for (std::size_t role = 0; role < roleChannels.size(); ++role)
				visible = visible || roleChannels[role] == channel;
			break;
		}
		if (visible) presentation.visibleAudioControlIds.push_back(audioControlId(channelIndex));
	}
	return presentation;
}
