#pragma once

#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"
#include "UserSettings.h"

enum class AppActionKind
{
	None,
	Shuffle,
	InstaSort,
	InstaInvert,
	SelectAlgorithm,
	SetDirection,
	Run,
	TogglePlayback,
	Reset,
	PreviousEvent,
	NextEvent,
	DecreaseCountFine,
	DecreaseCountCoarse,
	IncreaseCountFine,
	IncreaseCountCoarse,
	DecreaseDuration,
	IncreaseDuration,
	SeekFraction,
	SetPalette,
	SetSpacing,
	SetPanelExpanded,
	TogglePanelExpanded,
	SetInputProfile,
	SetInputDomain,
	SetInputPattern,
	SetAlgorithmOption,
	SetShellGapSequence,
	SetMergeWritebackMode,
	SetMergeExecutionMode,
	SetAudioParameter,
	SetVariantAudioLevel,
	ResetVariantAudioLevels,
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

struct AppAction
{
	AppActionKind kind;
	float value;
	int selection;
	sortinglogic::FamilyId familyId;
	sortinglogic::VariantId variantId;
	std::string optionGroupId;
	std::string optionStableId;
	AudioParameterId audioParameter;
	int index;

	AppAction(AppActionKind action = AppActionKind::None, float argument = 0.0f, int selected = 0,
		AudioParameterId parameter = AudioParameterId::MasterLevel, int arrayIndex = 0)
		: kind(action), value(argument), selection(selected), familyId(), variantId(), optionGroupId(), optionStableId(), audioParameter(parameter), index(arrayIndex) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		const std::string& groupId)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant),
			optionGroupId(groupId), optionStableId(), audioParameter(AudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		const std::string& groupId, const std::string& stableOptionId, int optionValue)
		: kind(action), value(0.0f), selection(optionValue), familyId(family), variantId(variant),
			optionGroupId(groupId), optionStableId(stableOptionId), audioParameter(AudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		float audioOffsetDb, int channel)
		: kind(action), value(audioOffsetDb), selection(-1), familyId(family), variantId(variant),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), index(channel) {}
};

class AppController
{
public:
	explicit AppController(ISettingsPersistence& settingsPersistence);
	~AppController();

	AppController(const AppController&) = delete;
	AppController& operator=(const AppController&) = delete;

	sortinglogic::CommandResult dispatch(const AppAction& action);
	void update(float deltaSeconds);
	sortinglogic::RenderSnapshot snapshot() const;
	const UserPreferences& preferences() const { return preferences_; }
	SoundEngine::Parameters effectiveAudio() const;
	const sortinglogic::Simulation& simulation() const { return simulation_; }
	const std::string& persistenceStatus() const { return persistenceStatus_; }

private:
	ISettingsPersistence& settingsPersistence_;
	UserPreferences preferences_;
	sortinglogic::Simulation simulation_;
	sortinglogic::TraceBuildService traceBuildService_;
	std::string persistenceStatus_;

	static UserPreferences loadPreferences(ISettingsPersistence& persistence);
	void synchronizeSimulationPreferences();
	void persistIfChanged(const UserPreferences& previous);
};
