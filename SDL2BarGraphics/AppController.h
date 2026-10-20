#pragma once

#include <memory>

#include "AppActions.h"
#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"
#include "UserSettings.h"

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
	void persistPreferences();
	double referenceDurationSeconds() const;
	double referenceElapsedAtCursor(std::size_t cursor) const;
	void synchronizeReferenceTransport(bool wasPlaying);
};
