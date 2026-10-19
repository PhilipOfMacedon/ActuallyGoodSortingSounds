#pragma once

#include <memory>

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
	sortinglogic::FamilyId familyId;
	sortinglogic::VariantId variantId;
	std::string optionGroupId;
	std::string optionStableId;
	AudioParameterId audioParameter;
	ReconstructedAudioParameterId reconstructedAudioParameter;
	int index;
	std::shared_ptr<const UiAppearancePreferences> appearance;

	AppAction(AppActionKind action = AppActionKind::None, float argument = 0.0f, int selected = 0,
		AudioParameterId parameter = AudioParameterId::MasterLevel, int arrayIndex = 0)
		: kind(action), value(argument), selection(selected), familyId(), variantId(), optionGroupId(), optionStableId(), audioParameter(parameter), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(arrayIndex) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		const std::string& groupId)
		: kind(action), value(0.0f), selection(-1), familyId(family), variantId(variant),
			optionGroupId(groupId), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		const std::string& groupId, const std::string& stableOptionId, int optionValue)
		: kind(action), value(0.0f), selection(optionValue), familyId(family), variantId(variant),
			optionGroupId(groupId), optionStableId(stableOptionId), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(0) {}
	AppAction(AppActionKind action, const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
		float audioOffsetDb, int channel)
		: kind(action), value(audioOffsetDb), selection(-1), familyId(family), variantId(variant),
			optionGroupId(), optionStableId(), audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(ReconstructedAudioParameterId::MasterLevel), index(channel) {}
	AppAction(AppActionKind action, ReconstructedAudioParameterId parameter, float argument = 0.0f, int selected = 0)
		: kind(action), value(argument), selection(selected), familyId(), variantId(), optionGroupId(), optionStableId(),
			audioParameter(AudioParameterId::MasterLevel), reconstructedAudioParameter(parameter), index(0) {}
};

class AppController
{
public:
	explicit AppController(ISettingsPersistence& settingsPersistence);
	~AppController();

	AppController(const AppController&) = delete;
	AppController& operator=(const AppController&) = delete;

	sortinglogic::CommandResult dispatch(const AppAction& action);
	void update(float deltaSeconds, bool audioClockAvailable = false);
	void setAudioPreparationPending(bool pending);
	bool applyAppearance(const UiAppearancePreferences& appearance, std::string* error = nullptr);
	bool playbackPending() const { return playbackPending_; }
	void applyAudioPlayhead(const SoundEngine::AudioPlayheadFeedback& feedback,
		std::uint64_t currentTimelineGeneration, std::uint64_t currentTransportRevision);
	sortinglogic::RenderSnapshot snapshot() const;
	const UserPreferences& preferences() const { return preferences_; }
	SoundEngine::Parameters effectiveAudio() const;
	SoundEngine::ReconstructedParameters effectiveReconstructedAudio() const { return preferences_.reconstructedAudio; }
	const sortinglogic::Simulation& simulation() const { return simulation_; }
	const std::string& persistenceStatus() const { return persistenceStatus_; }

private:
	ISettingsPersistence& settingsPersistence_;
	UserPreferences preferences_;
	sortinglogic::Simulation simulation_;
	sortinglogic::TraceBuildService traceBuildService_;
	std::string persistenceStatus_;
	double hostReferenceElapsedSeconds_;
	bool previousAudioClockAvailable_;
	bool audioPreparationGateEnabled_;
	bool audioPreparationPending_;
	bool playbackPending_;

	static UserPreferences loadPreferences(ISettingsPersistence& persistence);
	sortinglogic::CommandResult requestTraceBuild(bool playWhenReady);
	void startPendingPlaybackIfReady();
	void synchronizeSimulationPreferences();
	void persistIfChanged(const UserPreferences& previous);
	double referenceDurationSeconds() const;
	double referenceElapsedAtCursor(std::size_t cursor) const;
	void synchronizeReferenceTransport(bool wasPlaying);
};
