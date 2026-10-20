#pragma once

#include <string>
#include <array>
#include <map>
#include "../SortingLogic/SortingLogic.h"
#include "../SoundEngine/SoundEngine.h"
#include "../SoundEngine/ReconstructedSound.h"
#include "../SoundEngine/AudioTimeline.h"
#include "UiAppearance.h"
#include "VisualSettingsTypes.h"

static const int UserSettingsCurrentVersion = 8;

struct VisualPreferences
{
	PaletteId palette;
	BarSpacingMode spacing;
	bool panelExpanded;
	VisualPreferences();
	bool operator==(const VisualPreferences& other) const;
};

struct SimulationPreferences
{
	int itemCount;
	float durationSeconds;
	SoundEngine::PlaybackTimingMode playbackTimingMode;
	bool descending;
	int algorithm;
	sortinglogic::FamilyId familyId;
	sortinglogic::VariantId variantId;
	sortinglogic::InputDomain domain;
	sortinglogic::InputPattern pattern;
	bool mergeBufferVisualizationEnabled;
	bool completionScanEnabled;
	SimulationPreferences();
	bool operator==(const SimulationPreferences& other) const;
};

enum class AlgorithmAudioChannel : std::size_t
{
	Comparison = 0,
	ProbeAccent,
	MainChangeAccent,
	AuxiliaryAccent,
	DiscardAccent,
	HeldKeyRole,
	PivotRole,
	CurrentMinimumRole,
	CurrentMaximumRole,
	WriteTargetRole,
	ProbedItemRole,
	ProcessedItemRole,
	Count
};

struct AlgorithmAudioLevels
{
	std::array<float, static_cast<std::size_t>(AlgorithmAudioChannel::Count)> decibels;
	AlgorithmAudioLevels() { decibels.fill(0.0f); }
	bool operator==(const AlgorithmAudioLevels& other) const { return decibels == other.decibels; }
};

struct UserPreferences
{
	int version;
	VisualPreferences visual;
	UiAppearancePreferences appearance;
	SimulationPreferences simulation;
	SoundEngine::Parameters audio;
	SoundEngine::ReconstructedParameters reconstructedAudio;
	// Family values are inherited absolute dB levels. Variant values are dB
	// offsets from their family, so zero preserves the family's sound design.
	std::map<sortinglogic::FamilyId, AlgorithmAudioLevels> familyAudioDefaults;
	std::map<std::string, AlgorithmAudioLevels> variantAudioOffsets;
	// Algorithm options use a stable family.variant.group key and stable option ID.
	std::map<std::string, std::string> algorithmOptions;

	UserPreferences();
	bool isValid() const;
	bool operator==(const UserPreferences& other) const;
};

std::string variantAudioSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant);
std::string algorithmOptionSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
	const std::string& groupId);
std::string algorithmOptionStableId(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId);
int algorithmOptionValue(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId);
AlgorithmAudioLevels defaultFamilyAudioLevels(const SoundEngine::Parameters& audio);
AlgorithmAudioLevels familyAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family);
AlgorithmAudioLevels variantAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family,
	const sortinglogic::VariantId& variant);
SoundEngine::Parameters effectiveAudioParameters(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm);
std::vector<AlgorithmAudioChannel> supportedAudioRoleChannels(const sortinglogic::AlgorithmDescriptor& algorithm);
const char* algorithmAudioChannelName(AlgorithmAudioChannel channel);

typedef UserPreferences UserSettings;

class ISettingsPersistence
{
public:
	virtual ~ISettingsPersistence() {}
	virtual UserPreferences load() const = 0;
	virtual bool save(const UserPreferences& settings, std::string* error = nullptr) const = 0;
};

class SettingsStore : public ISettingsPersistence
{
public:
	SettingsStore();
	explicit SettingsStore(const std::wstring& path);

	UserPreferences load() const override;
	bool save(const UserPreferences& settings, std::string* error = nullptr) const override;
	const std::wstring& path() const { return path_; }

private:
	std::wstring path_;
};
