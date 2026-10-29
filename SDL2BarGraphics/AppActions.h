#pragma once

#include <memory>
#include <cstdint>
#include <string>

struct UiAppearancePreferences;

// Value-only messages shared by the UI adapter and application coordinator.
// Keeping these outside AppController lets the retained widget layer avoid
// importing settings, simulation, or trace-worker ownership.
enum class AppActionKind
{
	None,
	Shuffle,
	InstaSort,
	InstaInvert,
	SelectAlgorithm,
	SetDirection,
	BuildTrace,
	Run = BuildTrace, // Compatibility name for existing shortcuts and callers.
	GenerateAndPlay,
	TogglePlayback,
	Reset,
	PreviousEvent,
	NextEvent,
	DecreaseCountFine,
	DecreaseCountCoarse,
	IncreaseCountFine,
	IncreaseCountCoarse,
	AdjustItemCount,
	SetItemCount,
	AdjustDuration,
	DecreaseDuration,
	IncreaseDuration,
	SetDuration,
	SeekFraction,
	SetUiPreset,
	ApplyAppearance,
	SetPalette,
	SetSpacing,
	SetPanelExpanded,
	TogglePanelExpanded,
	ToggleBorderlessFullscreen,
	SetInputProfile,
	SetInputDomain,
	SetInputPattern,
	SetAlgorithmOption,
	SetShellGapSequence,
	SetMergeWritebackMode,
	SetMergeExecutionMode,
	SetMergeBufferVisualization,
	SetCompletionScan,
	SetPlaybackTimingMode,
	SetAudioParameter,
	SetVariantAudioLevel,
	ResetVariantAudioLevels,
	SetSeed,
	CopySeed,
	SaveSeedBookmark,
	SelectSeedBookmark,
	RenameSeedBookmark,
	RemoveSeedBookmark,
	PreviousSeed,
	NextSeed,
	SetReconstructedAudioParameter,
	RestoreReconstructedDefaults,
	SelectAudioBand,
	RetryAudioDevice
};

enum class AudioParameterId
{
	Enabled, MasterLevel, BedLevel, BedHarmonicExponent, IncludeSub300HzBed,
	CombEnabled, CombApplyPhase, CombDelayMicroseconds, CombPhaseOffsetDegrees,
	CombWetLow, CombWetHigh, CombFadeStartHz, CombFadeEndHz, CombFadeExponent,
	ComparisonBoostDb, HeldKeyBoostDb, TauBelowBins, TauAboveBins,
	ChorusMix, ChorusRateHz, ChorusDepthMs, ChorusWeighting,
	AttackMs, ReleaseMs, MappingMode, SpreadMode, BandTrim, AccentRouting, AccentGain
};

enum class ReconstructedAudioParameterId
{
	Enabled, MasterLevel, IdlePolicy, SpecialReleaseMilliseconds,
	MinimumFrequencyHz, MaximumFrequencyHz, Mapping, ExcitationClockHz,
	CarrierCycles, TukeyFraction, SecondOperandDelayMicroseconds,
	LowPassEnabled, LowPassCutoffHz
};

struct AppAction
{
	AppActionKind kind;
	float value;
	int selection;
	std::string familyId;
	std::string variantId;
	std::string optionGroupId;
	std::string optionStableId;
	std::string text;
	std::uint32_t seed;
	AudioParameterId audioParameter;
	ReconstructedAudioParameterId reconstructedAudioParameter;
	int index;
	std::shared_ptr<const UiAppearancePreferences> appearance;

	AppAction(AppActionKind action = AppActionKind::None, float argument = 0.0f, int selected = 0,
		AudioParameterId parameter = AudioParameterId::MasterLevel, int arrayIndex = 0)
		: kind(action), value(argument), selection(selected), familyId(), variantId(), optionGroupId(), optionStableId(), text(), seed(0),
			audioParameter(parameter), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(arrayIndex) {}
	AppAction(AppActionKind action, const std::string& family, const std::string& variant)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant), text(), seed(0),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const std::string& family, const std::string& variant,
		const std::string& groupId)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant), text(), seed(0),
			optionGroupId(groupId), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const std::string& family, const std::string& variant,
		const std::string& groupId, const std::string& stableOptionId, int optionValue)
		: kind(action), value(0.0f), selection(optionValue), familyId(family), variantId(variant), text(), seed(0),
			optionGroupId(groupId), optionStableId(stableOptionId), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const std::string& family, const std::string& variant,
		float audioOffsetDb, int channel)
		: kind(action), value(audioOffsetDb), selection(-1), familyId(family), variantId(variant), text(), seed(0),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(channel) {}
	AppAction(AppActionKind action, ReconstructedAudioParameterId parameter, float argument = 0.0f, int selected = 0)
		: kind(action), value(argument), selection(selected), familyId(), variantId(), optionGroupId(), optionStableId(), text(), seed(0),
			audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(parameter), index(0) {}
	AppAction(AppActionKind action, std::uint32_t seedValue)
		: kind(action), value(0.0f), selection(0), familyId(), variantId(), optionGroupId(), optionStableId(), text(), seed(seedValue),
			audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
};
