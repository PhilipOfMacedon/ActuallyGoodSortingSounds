#include "UserSettings.h"
#include "SettingsFileStorage.h"
#include "SettingsIniCodec.h"
#include "UserSettingsValidation.h"

#include <cmath>
#include <map>

std::string variantAudioSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
{
	return family + "." + variant;
}

std::string algorithmOptionSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
	const std::string& groupId)
{
	return family + "." + variant + "." + groupId;
}

std::string algorithmOptionStableId(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId)
{
	const std::string key = algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, groupId);
	const std::map<std::string, std::string>::const_iterator stored = preferences.algorithmOptions.find(key);
	if (stored != preferences.algorithmOptions.end())
		for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
			if (std::string(algorithm.supportedOptions[i].groupId) == groupId &&
				stored->second == algorithm.supportedOptions[i].stableId)
				return stored->second;
	const sortinglogic::AlgorithmOptionDescriptor* defaultOption = nullptr;
	for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
	{
		const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[i];
		if (std::string(option.groupId) != groupId) continue;
		if (!defaultOption && option.isDefault) defaultOption = &option;
	}
	return defaultOption ? defaultOption->stableId : std::string();
}

int algorithmOptionValue(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId)
{
	const std::string stableId = algorithmOptionStableId(preferences, algorithm, groupId);
	for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
		if (std::string(algorithm.supportedOptions[i].groupId) == groupId &&
			stableId == algorithm.supportedOptions[i].stableId)
			return algorithm.supportedOptions[i].value;
	return 0;
}

VisualPreferences::VisualPreferences() : palette(PaletteId::Cyan), spacing(BarSpacingMode::Compact), panelExpanded(true) {}

bool VisualPreferences::operator==(const VisualPreferences& other) const
{
	return palette == other.palette && spacing == other.spacing && panelExpanded == other.panelExpanded;
}

SimulationPreferences::SimulationPreferences() : itemCount(80), durationSeconds(10),
	playbackTimingMode(SoundEngine::PlaybackTimingMode::FixedDuration), descending(false),
	algorithm(static_cast<int>(sortinglogic::SortAlgorithm::Insertion)), familyId("insertion-methods"), variantId("insertion"),
	domain(sortinglogic::InputDomain::NormalizedFloats),
	pattern(sortinglogic::InputPattern::Random), mergeBufferVisualizationEnabled(true), completionScanEnabled(false) {}

bool SimulationPreferences::operator==(const SimulationPreferences& other) const
{
	return itemCount == other.itemCount && durationSeconds == other.durationSeconds &&
		playbackTimingMode == other.playbackTimingMode && descending == other.descending && algorithm == other.algorithm &&
		familyId == other.familyId && variantId == other.variantId && domain == other.domain &&
		pattern == other.pattern && mergeBufferVisualizationEnabled == other.mergeBufferVisualizationEnabled &&
		completionScanEnabled == other.completionScanEnabled;
}

UserPreferences::UserPreferences() : version(UserSettingsCurrentVersion), visual(), appearance(), simulation(), audio(), reconstructedAudio(), familyAudioDefaults(),
	variantAudioOffsets(), algorithmOptions() {}

bool UserPreferences::operator==(const UserPreferences& other) const
{
	return visual == other.visual && sameUiAppearancePreferences(appearance, other.appearance) &&
		simulation == other.simulation && audio == other.audio &&
		reconstructedAudio == other.reconstructedAudio && familyAudioDefaults == other.familyAudioDefaults &&
		variantAudioOffsets == other.variantAudioOffsets && algorithmOptions == other.algorithmOptions;
}

bool UserPreferences::isValid() const
{
	return version == UserSettingsCurrentVersion && simulation.itemCount >= static_cast<int>(sortinglogic::MinItemCount) &&
		simulation.itemCount <= static_cast<int>(sortinglogic::MaxItemCount) &&
		std::isfinite(simulation.durationSeconds) && simulation.durationSeconds >= 1.0f && simulation.durationSeconds <= 120.0f &&
		sortinglogic::algorithmDescriptorById(simulation.familyId, simulation.variantId) != nullptr &&
		sortinglogic::algorithmDescriptorById(simulation.familyId, simulation.variantId)->id ==
			static_cast<sortinglogic::SortAlgorithm>(simulation.algorithm) &&
		static_cast<int>(simulation.domain) >= static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) &&
		static_cast<int>(simulation.domain) <= static_cast<int>(sortinglogic::InputDomain::IntegerPermutation) &&
		sortinglogic::inputPatternCompatible(simulation.domain, simulation.pattern) &&
		(static_cast<int>(simulation.playbackTimingMode) == static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration) ||
		 static_cast<int>(simulation.playbackTimingMode) == static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence)) &&
		static_cast<int>(visual.palette) >= 0 && static_cast<int>(visual.palette) <= static_cast<int>(PaletteId::Windows95) &&
		static_cast<int>(visual.spacing) >= 0 && static_cast<int>(visual.spacing) <= static_cast<int>(BarSpacingMode::Comfortable) &&
		static_cast<int>(appearance.activePreset) >= static_cast<int>(UiPresetId::Current) &&
		static_cast<int>(appearance.activePreset) <= static_cast<int>(UiPresetId::ClassicWindows95) &&
		[&]()
		{
			const std::vector<UiPresetId>& presets = uiPresetCatalog();
			for (std::size_t i = 0; i < presets.size(); ++i)
			{
				const std::map<UiPresetId, UiThemeCustomization>::const_iterator theme = appearance.themes.find(presets[i]);
				if (theme == appearance.themes.end() || !validateUiThemeCustomization(presets[i], theme->second)) return false;
			}
			return true;
		}() &&
		userSettingsValidation::isValidAudioParameters(audio) &&
		userSettingsValidation::isValidReconstructedAudioParameters(reconstructedAudio) && [&]()
		{
			std::map<std::string, bool> knownFamilies;
			const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
			for (std::size_t i = 0; i < catalog.size(); ++i) knownFamilies[catalog[i].familyId] = true;
			for (std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator i = familyAudioDefaults.begin();
				i != familyAudioDefaults.end(); ++i)
			{
				if (knownFamilies.find(i->first) == knownFamilies.end()) return false;
				for (std::size_t level = 0; level < i->second.decibels.size(); ++level)
					if (!userSettingsValidation::isFiniteInRange(i->second.decibels[level], -60.0f, 30.0f)) return false;
			}
			for (std::map<std::string, AlgorithmAudioLevels>::const_iterator i = variantAudioOffsets.begin();
				i != variantAudioOffsets.end(); ++i)
			{
				const std::string::size_type split = i->first.find('.');
				if (split == std::string::npos || !sortinglogic::algorithmDescriptorById(i->first.substr(0, split), i->first.substr(split + 1))) return false;
				for (std::size_t level = 0; level < i->second.decibels.size(); ++level)
					if (!userSettingsValidation::isFiniteInRange(i->second.decibels[level], -60.0f, 12.0f)) return false;
			}
			for (std::map<std::string, std::string>::const_iterator i = algorithmOptions.begin(); i != algorithmOptions.end(); ++i)
			{
				bool knownOption = false;
				for (std::size_t algorithmIndex = 0; algorithmIndex < catalog.size() && !knownOption; ++algorithmIndex)
				{
					const sortinglogic::AlgorithmDescriptor& algorithm = catalog[algorithmIndex];
					for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
					{
						const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
						if (algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, option.groupId) == i->first &&
							option.stableId == i->second)
						{
							knownOption = true;
							break;
						}
					}
				}
				if (!knownOption) return false;
			}
			return true;
		}();
}

SettingsStore::SettingsStore() : path_(settingsfile::defaultPath()) {}
SettingsStore::SettingsStore(const std::wstring& path) : path_(path) {}

UserPreferences SettingsStore::load() const
{
	std::string content;
	if (!settingsfile::readAll(path_, content)) return UserPreferences();
	return settingsini::decode(content);
}

bool SettingsStore::save(const UserPreferences& settings, std::string* error) const
{
	if (!settings.isValid() || path_.empty())
	{
		if (error) *error = "Settings are invalid or the LocalAppData path is unavailable.";
		return false;
	}
	const std::string data = settingsini::encode(settings);
	return settingsfile::replaceAtomically(path_, data, UserSettingsCurrentVersion, error);
}
