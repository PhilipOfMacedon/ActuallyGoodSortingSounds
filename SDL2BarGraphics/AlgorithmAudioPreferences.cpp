#include "UserSettings.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
	float dbToGain(float decibels)
	{
		return decibels <= -60.0f ? 0.0f : std::pow(10.0f, decibels / 20.0f);
	}

	const AlgorithmAudioLevels* findVariantOverride(const UserPreferences& preferences,
		const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
	{
		const std::map<std::string, AlgorithmAudioLevels>::const_iterator found =
			preferences.variantAudioOffsets.find(variantAudioSettingsKey(family, variant));
		return found == preferences.variantAudioOffsets.end() ? nullptr : &found->second;
	}
}

AlgorithmAudioLevels defaultFamilyAudioLevels(const SoundEngine::Parameters& audio)
{
	(void)audio;
	// Reconstructed comparison and role defaults use unity gain. Legacy global
	// audio values stay available for migration and preview compatibility.
	return AlgorithmAudioLevels();
}

AlgorithmAudioLevels familyAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family)
{
	const std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator found =
		preferences.familyAudioDefaults.find(family);
	return found == preferences.familyAudioDefaults.end() ? defaultFamilyAudioLevels(preferences.audio) : found->second;
}

AlgorithmAudioLevels variantAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family,
	const sortinglogic::VariantId& variant)
{
	const AlgorithmAudioLevels* found = findVariantOverride(preferences, family, variant);
	return found ? *found : AlgorithmAudioLevels();
}

SoundEngine::Parameters effectiveAudioParameters(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm)
{
	SoundEngine::Parameters audio = preferences.audio;
	const AlgorithmAudioLevels family = familyAudioLevels(preferences, algorithm.familyId);
	const AlgorithmAudioLevels offsets = variantAudioLevels(preferences, algorithm.familyId, algorithm.variantId);
	const auto level = [&family, &offsets](AlgorithmAudioChannel channel)
	{
		const std::size_t index = static_cast<std::size_t>(channel);
		return (std::max)(-60.0f, (std::min)(30.0f, family.decibels[index] + offsets.decibels[index]));
	};
	audio.comparisonBoostDb = level(AlgorithmAudioChannel::Comparison);
	audio.heldKeyBoostDb = level(AlgorithmAudioChannel::HeldKeyRole);
	const AlgorithmAudioChannel accentChannels[SoundEngine::AccentCategoryCount] = {
		AlgorithmAudioChannel::ProbeAccent, AlgorithmAudioChannel::MainChangeAccent,
		AlgorithmAudioChannel::AuxiliaryAccent, AlgorithmAudioChannel::DiscardAccent
	};
	for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
	{
		const float db = level(accentChannels[i]);
		audio.accentRouting[i] = db > -60.0f;
		audio.accentGain[i] = audio.accentRouting[i] ? (std::min)(0.5f, dbToGain(db)) : 0.0f;
	}
	const struct RoleBinding { sortinglogic::SemanticRole semantic; SoundEngine::ComparisonRole sound; AlgorithmAudioChannel channel; } bindings[] = {
		{ sortinglogic::SemanticRole::Pivot, SoundEngine::ComparisonRole::Pivot, AlgorithmAudioChannel::PivotRole },
		{ sortinglogic::SemanticRole::CurrentMinimum, SoundEngine::ComparisonRole::CurrentMinimum, AlgorithmAudioChannel::CurrentMinimumRole },
		{ sortinglogic::SemanticRole::CurrentMaximum, SoundEngine::ComparisonRole::CurrentMaximum, AlgorithmAudioChannel::CurrentMaximumRole },
		{ sortinglogic::SemanticRole::WriteTarget, SoundEngine::ComparisonRole::WriteTarget, AlgorithmAudioChannel::WriteTargetRole },
		{ sortinglogic::SemanticRole::ProbedItem, SoundEngine::ComparisonRole::ProbedItem, AlgorithmAudioChannel::ProbedItemRole },
		{ sortinglogic::SemanticRole::ProcessedItem, SoundEngine::ComparisonRole::ProcessedItem, AlgorithmAudioChannel::ProcessedItemRole }
	};
	for (std::size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
		audio.roleBoostDb[static_cast<std::size_t>(bindings[i].sound)] = level(bindings[i].channel);
	return audio;
}

std::vector<AlgorithmAudioChannel> supportedAudioRoleChannels(const sortinglogic::AlgorithmDescriptor& algorithm)
{
	std::vector<AlgorithmAudioChannel> channels;
	for (std::size_t i = 0; i < algorithm.comparisonAudioRoles.size(); ++i)
	{
		switch (algorithm.comparisonAudioRoles[i])
		{
		case sortinglogic::SemanticRole::HeldKey: channels.push_back(AlgorithmAudioChannel::HeldKeyRole); break;
		case sortinglogic::SemanticRole::Pivot: channels.push_back(AlgorithmAudioChannel::PivotRole); break;
		case sortinglogic::SemanticRole::CurrentMinimum: channels.push_back(AlgorithmAudioChannel::CurrentMinimumRole); break;
		case sortinglogic::SemanticRole::CurrentMaximum: channels.push_back(AlgorithmAudioChannel::CurrentMaximumRole); break;
		case sortinglogic::SemanticRole::WriteTarget: channels.push_back(AlgorithmAudioChannel::WriteTargetRole); break;
		case sortinglogic::SemanticRole::ProbedItem: channels.push_back(AlgorithmAudioChannel::ProbedItemRole); break;
		case sortinglogic::SemanticRole::ProcessedItem: channels.push_back(AlgorithmAudioChannel::ProcessedItemRole); break;
		default: break;
		}
	}
	return channels;
}

const char* algorithmAudioChannelName(AlgorithmAudioChannel channel)
{
	switch (channel)
	{
	case AlgorithmAudioChannel::Comparison: return "Comparisons";
	case AlgorithmAudioChannel::ProbeAccent: return "Probe accents";
	case AlgorithmAudioChannel::MainChangeAccent: return "Main-array changes";
	case AlgorithmAudioChannel::AuxiliaryAccent: return "Auxiliary and counter updates";
	case AlgorithmAudioChannel::DiscardAccent: return "Discard accents";
	case AlgorithmAudioChannel::HeldKeyRole: return "Held-key emphasis";
	case AlgorithmAudioChannel::PivotRole: return "Pivot emphasis";
	case AlgorithmAudioChannel::CurrentMinimumRole: return "Current-minimum emphasis";
	case AlgorithmAudioChannel::CurrentMaximumRole: return "Current-maximum emphasis";
	case AlgorithmAudioChannel::WriteTargetRole: return "Write-target emphasis";
	case AlgorithmAudioChannel::ProbedItemRole: return "Probed-item emphasis";
	case AlgorithmAudioChannel::ProcessedItemRole: return "Processed-item emphasis";
	default: return "Audio level";
	}
}
