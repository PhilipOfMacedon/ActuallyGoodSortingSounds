#pragma once

#include <memory>
#include <string>

#include "AppActions.h"
#include "PlaybackCoordinator.h"
#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"
#include "UserSettings.h"
#include "SeedLibrary.h"

class ISeedBookmarkPersistence
{
public:
	virtual ~ISeedBookmarkPersistence() {}
	virtual bool load(std::vector<SeedBookmark>& bookmarks, std::string* error = nullptr) = 0;
	virtual bool save(const std::vector<SeedBookmark>& bookmarks, std::string* error = nullptr) = 0;
};

class AppController
{
public:
	explicit AppController(ISettingsPersistence& settingsPersistence,
		ISeedBookmarkPersistence* seedBookmarkPersistence = nullptr);
	~AppController();

	AppController(const AppController&) = delete;
	AppController& operator=(const AppController&) = delete;

	sortinglogic::CommandResult dispatch(const AppAction& action);
	void update(float deltaSeconds, bool audioClockAvailable = false);
	void setAudioPreparationPending(bool pending);
	bool applyAppearance(const UiAppearancePreferences& appearance, std::string* error = nullptr);
	bool playbackPending() const { return playbackCoordinator_.playbackQueued(); }
	PlaybackCoordinatorState playbackState() const;
	void applyAudioPlayhead(const SoundEngine::AudioPlayheadFeedback& feedback,
		const AudioPlaybackRevision& currentRevision);
	void snapshotInto(sortinglogic::RenderSnapshot& destination) const;
	sortinglogic::RenderSnapshot snapshot() const;
	const UserPreferences& preferences() const { return preferences_; }
	SoundEngine::Parameters effectiveAudio() const;
	SoundEngine::ReconstructedParameters effectiveReconstructedAudio() const { return preferences_.reconstructedAudio; }
	const sortinglogic::Simulation& simulation() const { return simulation_; }
	const std::string& persistenceStatus() const { return persistenceStatus_; }
	void setCommandFeedback(const std::string& message) { commandStatus_ = message; }
	const SeedBookmarkStore& seedBookmarks() const { return seedBookmarks_; }
	const SeedHistory& seedHistory() const { return seedHistory_; }

private:
	ISettingsPersistence& settingsPersistence_;
	ISeedBookmarkPersistence* seedBookmarkPersistence_;
	UserPreferences preferences_;
	sortinglogic::Simulation simulation_;
	sortinglogic::TraceBuildService traceBuildService_;
	PlaybackCoordinator playbackCoordinator_;
	std::string persistenceStatus_;
	std::string commandStatus_;
	SeedHistory seedHistory_;
	SeedBookmarkStore seedBookmarks_;

	static UserPreferences loadPreferences(ISettingsPersistence& persistence);
	sortinglogic::CommandResult requestTraceBuild(bool playWhenReady);
	void startPendingPlaybackIfReady();
	void synchronizeSimulationPreferences();
	void persistPreferences();
	bool persistSeedBookmarks(std::string* error = nullptr);
	double referenceDurationSeconds() const;
	double referenceElapsedAtCursor(std::size_t cursor) const;
	void synchronizeReferenceTransport(bool wasPlaying);
};
