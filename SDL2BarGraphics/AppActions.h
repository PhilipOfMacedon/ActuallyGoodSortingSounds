#pragma once

#include <memory>
#include <cstddef>
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
	OpenDemoEditor,
	CloseDemoEditor,
	SelectDemoPreset,
	SelectDemoStep,
	StartDemo,
	StopDemo,
	ToggleDemoPlayback,
	PreviousDemoStep,
	NextDemoStep,
	DemoSetClassRecipe,
	DemoSetIntermission,
	DemoSetLoop,
	DemoAddStep,
	DemoRemoveStep,
	DemoMoveStepUp,
	DemoMoveStepDown,
	DemoSetStepAlgorithm,
	DemoSetStepTiming,
	DemoSetStepDirection,
	DemoSetStepCompletionScan,
	DemoSetStepOption,
	DemoSetStepOverride,
	DemoClearStepOverride,
	DemoSetDraftName,
	ResolveDemoPresetSwitch,
	SaveDemoPreset,
	RenameDemoPreset,
	DeleteDemoPreset,
	SetReconstructedAudioParameter,
	RestoreReconstructedDefaults,
	SelectAudioBand,
	RetryAudioDevice
};

inline bool isDemoActionKind(AppActionKind kind)
{
	switch (kind)
	{
	case AppActionKind::OpenDemoEditor:
	case AppActionKind::CloseDemoEditor:
	case AppActionKind::SelectDemoPreset:
	case AppActionKind::SelectDemoStep:
	case AppActionKind::StartDemo:
	case AppActionKind::StopDemo:
	case AppActionKind::ToggleDemoPlayback:
	case AppActionKind::PreviousDemoStep:
	case AppActionKind::NextDemoStep:
	case AppActionKind::DemoSetClassRecipe:
	case AppActionKind::DemoSetIntermission:
	case AppActionKind::DemoSetLoop:
	case AppActionKind::DemoAddStep:
	case AppActionKind::DemoRemoveStep:
	case AppActionKind::DemoMoveStepUp:
	case AppActionKind::DemoMoveStepDown:
	case AppActionKind::DemoSetStepAlgorithm:
	case AppActionKind::DemoSetStepTiming:
	case AppActionKind::DemoSetStepDirection:
	case AppActionKind::DemoSetStepCompletionScan:
	case AppActionKind::DemoSetStepOption:
	case AppActionKind::DemoSetStepOverride:
	case AppActionKind::DemoClearStepOverride:
	case AppActionKind::DemoSetDraftName:
	case AppActionKind::ResolveDemoPresetSwitch:
	case AppActionKind::SaveDemoPreset:
	case AppActionKind::RenameDemoPreset:
	case AppActionKind::DeleteDemoPreset:
		return true;
	default:
		return false;
	}
}

inline bool isDemoDraftMutationAction(AppActionKind kind)
{
	switch (kind)
	{
	case AppActionKind::SelectDemoPreset:
	case AppActionKind::DemoSetClassRecipe:
	case AppActionKind::DemoSetIntermission:
	case AppActionKind::DemoSetLoop:
	case AppActionKind::DemoAddStep:
	case AppActionKind::DemoRemoveStep:
	case AppActionKind::DemoMoveStepUp:
	case AppActionKind::DemoMoveStepDown:
	case AppActionKind::DemoSetStepAlgorithm:
	case AppActionKind::DemoSetStepTiming:
	case AppActionKind::DemoSetStepDirection:
	case AppActionKind::DemoSetStepCompletionScan:
	case AppActionKind::DemoSetStepOption:
	case AppActionKind::DemoSetStepOverride:
	case AppActionKind::DemoClearStepOverride:
	case AppActionKind::DemoSetDraftName:
	case AppActionKind::SaveDemoPreset:
	case AppActionKind::RenameDemoPreset:
	case AppActionKind::DeleteDemoPreset:
		return true;
	default:
		return false;
	}
}

// The action kind defines the operation; this payload keeps its editor fields
// strongly grouped and independent from SDL/UI widget classes.
struct DemoActionPayload
{
	std::string presetId;
	std::string presetName;
	std::string familyId;
	std::string variantId;
	std::string optionGroupId;
	std::string optionStableId;
	std::size_t stepIndex;
	int complexityClass;
	std::uint32_t seed;
	std::size_t itemCount;
	int inputDomain;
	int inputPattern;
	int direction;
	int timingMode;
	float value;
	bool flag;
	bool hasInputOverride;
	DemoActionPayload() : presetId(), presetName(), familyId(), variantId(), optionGroupId(), optionStableId(), stepIndex(0),
		complexityClass(0), seed(0), itemCount(0), inputDomain(0), inputPattern(0), direction(0), timingMode(0), value(0.0f),
		flag(false), hasInputOverride(false) {}
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
	std::shared_ptr<const DemoActionPayload> demo;

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
