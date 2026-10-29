#pragma once

#include <memory>
#include <string>

#include "AppActions.h"
#include "PlaybackCoordinator.h"
#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"
#include "UserSettings.h"
#include "SeedLibrary.h"
#include "DemoModels.h"
#include "DemoRunner.h"

class ISeedBookmarkPersistence
{
public:
	virtual ~ISeedBookmarkPersistence() {}
	virtual bool load(std::vector<SeedBookmark>& bookmarks, std::string* error = nullptr) = 0;
	virtual bool save(const std::vector<SeedBookmark>& bookmarks, std::string* error = nullptr) = 0;
};

class IDemoPresetPersistence
{
public:
	virtual ~IDemoPresetPersistence() {}
	virtual bool load(std::vector<DemoPreset>& presets, std::vector<std::string>* issues = nullptr,
		std::string* error = nullptr) = 0;
	virtual bool save(const std::vector<DemoPreset>& presets, std::string* error = nullptr) = 0;
};

struct RunSession
{
	sortinglogic::Simulation simulation;
	PlaybackCoordinator playback;
	std::uint64_t identity;
	std::uint64_t requestGeneration;
	bool temporary;
	SoundEngine::PlaybackTimingMode timingMode;
	DemoStep step;
	DemoInputRecipe effectiveInput;
	RunSession(std::size_t itemCount, std::uint32_t seed, sortinglogic::InputDomain domain,
		sortinglogic::InputPattern pattern, SoundEngine::PlaybackTimingMode timing,
		std::uint64_t sessionIdentity, bool isTemporary);
};

class AppController
{
public:
	explicit AppController(ISettingsPersistence& settingsPersistence,
		ISeedBookmarkPersistence* seedBookmarkPersistence = nullptr,
		IDemoPresetPersistence* demoPresetPersistence = nullptr);
	~AppController();

	AppController(const AppController&) = delete;
	AppController& operator=(const AppController&) = delete;

	sortinglogic::CommandResult dispatch(const AppAction& action);
	void update(float deltaSeconds, bool audioClockAvailable = false);
	void setAudioPreparationPending(bool pending);
	bool applyAppearance(const UiAppearancePreferences& appearance, std::string* error = nullptr);
	bool playbackPending() const { return activeSession().playback.playbackQueued(); }
	PlaybackCoordinatorState playbackState() const;
	void applyAudioPlayhead(const SoundEngine::AudioPlayheadFeedback& feedback,
		const AudioPlaybackRevision& currentRevision);
	void snapshotInto(sortinglogic::RenderSnapshot& destination) const;
	sortinglogic::RenderSnapshot snapshot() const;
	const UserPreferences& preferences() const { return preferences_; }
	SoundEngine::Parameters effectiveAudio() const;
	SoundEngine::ReconstructedParameters effectiveReconstructedAudio() const { return preferences_.reconstructedAudio; }
	const sortinglogic::Simulation& simulation() const { return activeSession().simulation; }
	const std::string& persistenceStatus() const { return persistenceStatus_; }
	void setCommandFeedback(const std::string& message) { commandStatus_ = message; }
	const SeedBookmarkStore& seedBookmarks() const { return seedBookmarks_; }
	const SeedHistory& seedHistory() const { return seedHistory_; }
	const DemoPresetStore& demoPresets() const { return demoPresetStore_; }
	const DemoRunner& demoRunner() const { return demoRunner_; }
	const std::string& selectedDemoPresetId() const { return selectedDemoPresetId_; }
	const DemoPreset& demoEditorDraft() const { return demoEditorDraft_; }
	std::size_t demoEditorSelectedStep() const { return demoEditorSelectedStep_; }
	bool demoEditorOpen() const { return demoEditorOpen_; }
	bool demoActive() const { return demoRunner_.active(); }
	SoundEngine::PlaybackTimingMode activeTimingMode() const;
	std::uint64_t activeSessionIdentity() const { return activeSession().identity; }

private:
	ISettingsPersistence& settingsPersistence_;
	ISeedBookmarkPersistence* seedBookmarkPersistence_;
	IDemoPresetPersistence* demoPresetPersistence_;
	UserPreferences preferences_;
	RunSession manualSession_;
	std::unique_ptr<RunSession> demoSession_;
	sortinglogic::TraceBuildService traceBuildService_;
	std::string persistenceStatus_;
	std::string commandStatus_;
	SeedHistory seedHistory_;
	SeedBookmarkStore seedBookmarks_;
	DemoPresetStore demoPresetStore_;
	DemoRunner demoRunner_;
	DemoPreset demoEditorDraft_;
	std::string selectedDemoPresetId_;
	std::size_t demoEditorSelectedStep_;
	std::string editorPrefabSourceId_;
	bool demoEditorOpen_;
	bool pendingDemoStepPreparation_;
	std::uint64_t nextSessionIdentity_;
	std::string demoPersistenceIssue_;

	static UserPreferences loadPreferences(ISettingsPersistence& persistence);
	sortinglogic::CommandResult requestTraceBuild(bool playWhenReady);
	void startPendingPlaybackIfReady();
	void synchronizeSimulationPreferences();
	void persistPreferences();
	bool persistSeedBookmarks(std::string* error = nullptr);
	bool persistDemoPresets(std::string* error = nullptr);
	RunSession& activeSession();
	const RunSession& activeSession() const;
	sortinglogic::Simulation& activeSimulation();
	const sortinglogic::Simulation& activeSimulation() const;
	PlaybackCoordinator& activePlaybackCoordinator();
	const PlaybackCoordinator& activePlaybackCoordinator() const;
	sortinglogic::CommandResult dispatchDemoAction(const AppAction& action);
	sortinglogic::CommandResult startDemo(const std::string& presetId);
	sortinglogic::CommandResult stopDemo(const std::string& message = std::string());
	void prepareRequestedDemoStep();
	void createDemoSessionForCurrentStep();
	void handleDemoBuildCompletion(sortinglogic::TraceBuildResult result);
	void finishDemoIfNeeded();
	const DemoPreset* findDemoPreset(const std::string& id) const;
	double referenceDurationSeconds() const;
	double referenceElapsedAtCursor(std::size_t cursor) const;
	void synchronizeReferenceTransport(bool wasPlaying);
};
