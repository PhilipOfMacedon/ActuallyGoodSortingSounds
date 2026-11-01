#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "BarCountSteps.h"
#include "AudioSettingsActions.h"
#include "PlaybackTimebase.h"
#include "SettingsFileStorage.h"

namespace
{
	class LocalSeedBookmarkPersistence : public ISeedBookmarkPersistence
	{
	public:
		bool load(std::vector<SeedBookmark>& bookmarks, std::string* error) override
		{
			bookmarks.clear();
			const std::wstring path = settingsfile::pathForFile(L"seed-favorites.ini");
			std::string content;
			if (path.empty() || !settingsfile::readAll(path, content))
			{
				if (error) error->clear();
				return true;
			}
			return SeedBookmarkStore::decode(content, bookmarks, error);
		}

		bool save(const std::vector<SeedBookmark>& bookmarks, std::string* error) override
		{
			const std::wstring path = settingsfile::pathForFile(L"seed-favorites.ini");
			if (path.empty())
			{
				if (error) *error = "LOCALAPPDATA is unavailable.";
				return false;
			}
			return settingsfile::replaceAtomically(path, SeedBookmarkStore::encode(bookmarks),
				SeedBookmarkStore::CurrentVersion, error, false);
		}
	};

	ISeedBookmarkPersistence& defaultSeedBookmarkPersistence()
	{
		static LocalSeedBookmarkPersistence persistence;
		return persistence;
	}

	class LocalDemoPresetPersistence : public IDemoPresetPersistence
	{
	public:
		bool load(std::vector<DemoPreset>& presets, std::vector<std::string>* issues, std::string* error) override
		{
			presets.clear();
			if (issues) issues->clear();
			const std::wstring path = settingsfile::pathForFile(L"demo-presets.ini");
			std::string content;
			if (path.empty() || !settingsfile::readAll(path, content))
			{
				if (error) error->clear();
				return true;
			}
			return DemoPresetStore::decode(content, presets, issues, error);
		}

		bool save(const std::vector<DemoPreset>& presets, std::string* error) override
		{
			const std::wstring path = settingsfile::pathForFile(L"demo-presets.ini");
			if (path.empty())
			{
				if (error) *error = "LOCALAPPDATA is unavailable.";
				return false;
			}
			return settingsfile::replaceAtomically(path, DemoPresetStore::encode(presets),
				DemoPresetStore::CurrentVersion, error, false);
		}
	};

	IDemoPresetPersistence& defaultDemoPresetPersistence()
	{
		static LocalDemoPresetPersistence persistence;
		return persistence;
	}

	bool needsFullPreferenceSnapshot(AppActionKind action)
	{
		// The simulation settings are captured separately for every dispatch.
		// These actions can also mutate non-simulation preferences and need the
		// complete snapshot for change detection.
		switch (action)
		{
		case AppActionKind::SetUiPreset:
		case AppActionKind::SetPalette:
		case AppActionKind::SetSpacing:
		case AppActionKind::SetPanelExpanded:
		case AppActionKind::TogglePanelExpanded:
		case AppActionKind::SetAlgorithmOption:
		case AppActionKind::SetAudioParameter:
		case AppActionKind::SetReconstructedAudioParameter:
		case AppActionKind::RestoreReconstructedDefaults:
		case AppActionKind::SetVariantAudioLevel:
		case AppActionKind::ResetVariantAudioLevels:
			return true;
		case AppActionKind::Shuffle:
		case AppActionKind::InstaSort:
		case AppActionKind::InstaInvert:
		case AppActionKind::SelectAlgorithm:
		case AppActionKind::SetDirection:
		case AppActionKind::SetPlaybackTimingMode:
		case AppActionKind::SetCompletionScan:
		case AppActionKind::BuildTrace:
		case AppActionKind::GenerateAndPlay:
		case AppActionKind::TogglePlayback:
		case AppActionKind::Reset:
		case AppActionKind::PreviousEvent:
		case AppActionKind::NextEvent:
		case AppActionKind::DecreaseCountFine:
		case AppActionKind::DecreaseCountCoarse:
		case AppActionKind::IncreaseCountFine:
		case AppActionKind::IncreaseCountCoarse:
		case AppActionKind::AdjustItemCount:
		case AppActionKind::SetItemCount:
		case AppActionKind::DecreaseDuration:
		case AppActionKind::AdjustDuration:
		case AppActionKind::IncreaseDuration:
		case AppActionKind::SetDuration:
		case AppActionKind::SeekFraction:
		case AppActionKind::ApplyAppearance:
		case AppActionKind::SetInputProfile:
		case AppActionKind::SetInputDomain:
		case AppActionKind::SetInputPattern:
		case AppActionKind::ToggleBorderlessFullscreen:
		case AppActionKind::SelectAudioBand:
		case AppActionKind::RetryAudioDevice:
		case AppActionKind::None:
		case AppActionKind::SetSeed:
		case AppActionKind::CopySeed:
		case AppActionKind::SaveSeedBookmark:
		case AppActionKind::SelectSeedBookmark:
		case AppActionKind::RenameSeedBookmark:
		case AppActionKind::RemoveSeedBookmark:
		case AppActionKind::PreviousSeed:
		case AppActionKind::NextSeed:
		case AppActionKind::OpenDemoEditor:
		case AppActionKind::CloseDemoEditor:
		case AppActionKind::SelectDemoPreset:
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
			return false;
		default:
			// New commands take the conservative path until explicitly classified.
			return true;
		}
	}

	class PreferenceChangeSnapshot
	{
	public:
		PreferenceChangeSnapshot(const UserPreferences& current, bool captureFullPreferences)
			: simulation_(current.simulation), full_(captureFullPreferences ? new UserPreferences(current) : nullptr)
		{
		}

		bool changed(const UserPreferences& current) const
		{
			return !(simulation_ == current.simulation) || (full_ && !(*full_ == current));
		}

	private:
		SimulationPreferences simulation_;
		std::unique_ptr<UserPreferences> full_;
	};

	void storeAlgorithmOption(UserPreferences& preferences, const sortinglogic::AlgorithmDescriptor& algorithm,
		const std::string& groupId, int value)
	{
		for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[i];
			if (std::string(option.groupId) == groupId && option.value == value)
			{
				preferences.algorithmOptions[algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, groupId)] = option.stableId;
				return;
			}
		}
	}

	std::uint32_t freshStartupOrShuffleSeed()
	{
		return sortinglogic::Simulation::createFreshSeed();
	}

	bool sameDemoRecipe(const DemoInputRecipe& a, const DemoInputRecipe& b)
	{
		return a.seed == b.seed && a.itemCount == b.itemCount && a.domain == b.domain && a.pattern == b.pattern;
	}

	bool sameDemoConfiguration(const DemoPreset& a, const DemoPreset& b)
	{
		if (a.name != b.name || a.intermissionSeconds != b.intermissionSeconds || a.loop != b.loop ||
			a.classInputs.size() != b.classInputs.size() || a.steps.size() != b.steps.size()) return false;
		std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator ar = a.classInputs.begin();
		std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator br = b.classInputs.begin();
		for (; ar != a.classInputs.end(); ++ar, ++br)
			if (ar->first != br->first || !sameDemoRecipe(ar->second, br->second)) return false;
		for (std::size_t i = 0; i < a.steps.size(); ++i)
		{
			const DemoStep& x = a.steps[i];
			const DemoStep& y = b.steps[i];
			if (x.familyId != y.familyId || x.variantId != y.variantId || x.optionSelections != y.optionSelections ||
				x.direction != y.direction || x.timingMode != y.timingMode || x.fixedDurationSeconds != y.fixedDurationSeconds ||
				x.completionScanEnabled != y.completionScanEnabled ||
				x.hasInputOverride != y.hasInputOverride ||
				(x.hasInputOverride && !sameDemoRecipe(x.inputOverride, y.inputOverride))) return false;
		}
		return true;
	}

}

UserPreferences AppController::loadPreferences(ISettingsPersistence& persistence)
{
	const UserPreferences loaded = persistence.load();
	return loaded.isValid() ? loaded : UserPreferences();
}

RunSession::RunSession(std::size_t itemCount, std::uint32_t seed, sortinglogic::InputDomain domain,
	sortinglogic::InputPattern pattern, SoundEngine::PlaybackTimingMode timing,
	std::uint64_t sessionIdentity, bool isTemporary)
	: simulation(itemCount, seed, domain, pattern), playback(), identity(sessionIdentity), requestGeneration(0),
	temporary(isTemporary), timingMode(timing), step(), effectiveInput(seed, itemCount, domain, pattern)
{
}

AppController::AppController(ISettingsPersistence& settingsPersistence,
	ISeedBookmarkPersistence* seedBookmarkPersistence, IDemoPresetPersistence* demoPresetPersistence)
	: settingsPersistence_(settingsPersistence),
	seedBookmarkPersistence_(seedBookmarkPersistence ? seedBookmarkPersistence : &defaultSeedBookmarkPersistence()),
	demoPresetPersistence_(demoPresetPersistence ? demoPresetPersistence : &defaultDemoPresetPersistence()),
	preferences_(loadPreferences(settingsPersistence)),
	manualSession_(static_cast<std::size_t>(preferences_.simulation.itemCount), freshStartupOrShuffleSeed(),
		preferences_.simulation.domain, preferences_.simulation.pattern, preferences_.simulation.playbackTimingMode, 1, false),
	demoSession_(), traceBuildService_(), persistenceStatus_("Preferences loaded."), commandStatus_(), seedHistory_(),
	seedBookmarks_(), demoPresetStore_(), demoRunner_(), demoEditorDraft_(), demoEditorBaseline_(), selectedDemoPresetId_("prefab.classic-five"),
	demoEditorSelectedStep_(0), editorPrefabSourceId_(), demoEditorOpen_(false), demoEditorDraftInitialized_(false),
	demoEditorDraftDirty_(false), pendingDemoPresetSwitchId_(), pendingDemoStepPreparation_(false), nextSessionIdentity_(2), demoPersistenceIssue_()
{
	seedHistory_.reset(activeSimulation().snapshot().inputSeed);
	std::vector<SeedBookmark> loadedBookmarks;
	if (seedBookmarkPersistence_->load(loadedBookmarks))
		for (std::size_t i = 0; i < loadedBookmarks.size(); ++i)
			seedBookmarks_.save(loadedBookmarks[i].seed, loadedBookmarks[i].name);
	activeSimulation().setDuration(static_cast<float>(preferences_.simulation.durationSeconds));
	activeSimulation().setDirection(preferences_.simulation.descending
		? sortinglogic::SortDirection::Descending
		: sortinglogic::SortDirection::Ascending);
	const sortinglogic::AlgorithmDescriptor* savedAlgorithm = sortinglogic::algorithmDescriptorById(
		preferences_.simulation.familyId, preferences_.simulation.variantId);
	if (savedAlgorithm && sortinglogic::algorithmSupports(savedAlgorithm->id, preferences_.simulation.domain))
		activeSimulation().selectAlgorithm(savedAlgorithm->id);
	activeSimulation().setCompletionScanEnabled(preferences_.simulation.completionScanEnabled);
	const sortinglogic::AlgorithmDescriptor* activeAlgorithm = sortinglogic::algorithmDescriptor(activeSimulation().algorithm());
	if (activeAlgorithm)
	{
		std::set<std::string> configuredGroups;
		for (std::size_t i = 0; i < activeAlgorithm->supportedOptions.size(); ++i)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = activeAlgorithm->supportedOptions[i];
			const std::string group(option.groupId);
			if (!configuredGroups.insert(group).second) continue;
			const std::string stableId = algorithmOptionStableId(preferences_, *activeAlgorithm, group);
			if (!stableId.empty()) activeSimulation().setAlgorithmOption(group, stableId);
		}
	}
	std::vector<DemoPreset> loadedPresets;
	std::vector<std::string> loadIssues;
	std::string loadError;
	if (demoPresetPersistence_->load(loadedPresets, &loadIssues, &loadError))
	{
		for (std::size_t i = 0; i < loadedPresets.size(); ++i)
		{
			std::string presetError;
			if (!demoPresetStore_.saveUserPreset(loadedPresets[i], &presetError) && demoPersistenceIssue_.empty())
				demoPersistenceIssue_ = "A saved demo could not be loaded: " + presetError;
		}
		if (!loadIssues.empty() && demoPersistenceIssue_.empty()) demoPersistenceIssue_ = loadIssues.front();
	}
	else demoPersistenceIssue_ = loadError.empty() ? "Saved demos could not be read." : loadError;
	const DemoPreset* initialDraft = findDemoPreset(selectedDemoPresetId_);
	if (!initialDraft) { selectedDemoPresetId_ = "prefab.classic-five"; initialDraft = findDemoPreset(selectedDemoPresetId_); }
	if (initialDraft)
	{
		demoEditorDraft_ = *initialDraft;
		adoptDemoDraftBaseline();
	}
}

AppController::~AppController() = default;

RunSession& AppController::activeSession()
{
	return demoSession_ ? *demoSession_ : manualSession_;
}

const RunSession& AppController::activeSession() const
{
	return demoSession_ ? *demoSession_ : manualSession_;
}

sortinglogic::Simulation& AppController::activeSimulation() { return activeSession().simulation; }
const sortinglogic::Simulation& AppController::activeSimulation() const { return activeSession().simulation; }
PlaybackCoordinator& AppController::activePlaybackCoordinator() { return activeSession().playback; }
const PlaybackCoordinator& AppController::activePlaybackCoordinator() const { return activeSession().playback; }
SoundEngine::PlaybackTimingMode AppController::activeTimingMode() const { return activeSession().timingMode; }

sortinglogic::CommandResult AppController::requestTraceBuild(bool playWhenReady)
{
	activePlaybackCoordinator().traceBuildRequested(false);
	sortinglogic::TraceBuildRequest request;
	sortinglogic::CommandResult result = activeSimulation().beginTraceBuild(request);
	if (!result.accepted) return result;
	request.sessionId = activeSession().identity;
	request.requestGeneration = ++activeSession().requestGeneration;
	const sortinglogic::AlgorithmDescriptor* traceAlgorithm = sortinglogic::algorithmDescriptor(request.algorithm);
	if (traceAlgorithm)
	{
		std::set<std::string> configuredGroups;
		for (std::size_t optionIndex = 0; optionIndex < traceAlgorithm->supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = traceAlgorithm->supportedOptions[optionIndex];
			const std::string group(option.groupId);
			if (!configuredGroups.insert(group).second) continue;
			std::string stableId;
			if (activeSession().temporary)
			{
				const std::map<std::string, std::string>::const_iterator selected = activeSession().step.optionSelections.find(group);
				if (selected != activeSession().step.optionSelections.end()) stableId = selected->second;
			}
			else stableId = algorithmOptionStableId(preferences_, *traceAlgorithm, group);
			if (!stableId.empty()) request.optionSelections[group] = stableId;
		}
	}

	activePlaybackCoordinator().traceBuildRequested(playWhenReady);
	std::string error;
	if (!traceBuildService_.submit(request, &error))
	{
		sortinglogic::TraceBuildResult failed;
		failed.error = error.empty() ? "Trace worker is unavailable." : error;
		failed.sessionId = request.sessionId;
		failed.requestGeneration = request.requestGeneration;
		activeSimulation().completeTraceBuild(failed);
		activePlaybackCoordinator().traceBuildFailed();
		return sortinglogic::CommandResult(false, failed.error);
	}
	if (playWhenReady)
		result.message = "Generating the trace; playback will start automatically when preparation finishes.";
	return result;
}

void AppController::setAudioPreparationPending(bool pending)
{
	activePlaybackCoordinator().setAudioPreparationPending(pending);
	// Readiness arrives after the bridge published this frame's transport. Start
	// on the next update so feedback for that paused transport cannot undo Play.
}

bool AppController::applyAppearance(const UiAppearancePreferences& appearance, std::string* error)
{
	if (static_cast<int>(appearance.activePreset) < static_cast<int>(UiPresetId::Current) ||
		static_cast<int>(appearance.activePreset) > static_cast<int>(UiPresetId::ClassicWindows95))
	{
		if (error) *error = "The selected interface preset is unavailable.";
		return false;
	}
	for (std::size_t i = 0; i < uiPresetCatalog().size(); ++i)
	{
		const UiPresetId preset = uiPresetCatalog()[i];
		const std::map<UiPresetId, UiThemeCustomization>::const_iterator found = appearance.themes.find(preset);
		if (found == appearance.themes.end() || !validateUiThemeCustomization(preset, found->second, error)) return false;
	}
	UserPreferences candidate = preferences_;
	candidate.appearance = appearance;
	candidate.visual.palette = appearance.themes.find(UiPresetId::Current)->second.palette;
	if (!candidate.isValid())
	{
		if (error && error->empty()) *error = "The appearance settings are invalid.";
		return false;
	}
	if (sameUiAppearancePreferences(preferences_.appearance, candidate.appearance))
	{
		if (error) error->clear();
		return true;
	}
	if (!settingsPersistence_.save(candidate, error))
	{
		persistenceStatus_ = "Could not save appearance" + (error && !error->empty() ? ": " + *error : ".");
		return false;
	}
	preferences_ = candidate;
	persistenceStatus_ = "Appearance saved.";
	if (error) error->clear();
	return true;
}

void AppController::startPendingPlaybackIfReady()
{
	if (!activePlaybackCoordinator().consumeQueuedPlayback(activeSimulation().isBuildingTrace(), activeSimulation().hasTrace())) return;
	const bool wasPlaying = activeSimulation().isPlaying();
	const sortinglogic::CommandResult started = activeSimulation().togglePlayback();
	if (started.accepted && activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence)
		synchronizeReferenceTransport(wasPlaying);
	if (started.accepted && demoRunner_.active()) demoRunner_.markPlaying();
}

sortinglogic::CommandResult AppController::dispatch(const AppAction& action)
{
	if (isDemoActionKind(action.kind))
		return dispatchDemoAction(action);
	if (demoRunner_.active() && (action.kind == AppActionKind::TogglePlayback || action.kind == AppActionKind::GenerateAndPlay))
		return dispatchDemoAction(AppAction(AppActionKind::ToggleDemoPlayback));
	if (demoRunner_.active())
	{
		bool permitted = false;
		switch (action.kind)
		{
		case AppActionKind::TogglePlayback:
		case AppActionKind::GenerateAndPlay:
		case AppActionKind::PreviousEvent:
		case AppActionKind::NextEvent:
		case AppActionKind::SeekFraction:
		case AppActionKind::SetUiPreset:
		case AppActionKind::ApplyAppearance:
		case AppActionKind::SetPalette:
		case AppActionKind::SetSpacing:
		case AppActionKind::SetPanelExpanded:
		case AppActionKind::TogglePanelExpanded:
		case AppActionKind::ToggleBorderlessFullscreen:
		case AppActionKind::SetAudioParameter:
		case AppActionKind::SetVariantAudioLevel:
		case AppActionKind::ResetVariantAudioLevels:
		case AppActionKind::SetReconstructedAudioParameter:
		case AppActionKind::RestoreReconstructedDefaults:
		case AppActionKind::SelectAudioBand:
		case AppActionKind::RetryAudioDevice:
		case AppActionKind::CopySeed:
		case AppActionKind::SaveSeedBookmark:
			permitted = true;
			break;
		default:
			break;
		}
		if (!permitted)
			return sortinglogic::CommandResult(false, "Manual setup is locked during a demo. Stop the demo to change its inputs or run configuration.");
	}
	if (!sortinglogic::inputPatternUsesSeed(manualSession_.simulation.pattern()))
	{
		switch (action.kind)
		{
		case AppActionKind::Shuffle:
		case AppActionKind::SetSeed:
		case AppActionKind::SelectSeedBookmark:
		case AppActionKind::PreviousSeed:
		case AppActionKind::NextSeed:
			return sortinglogic::CommandResult(false, "The selected input pattern does not use a seed. Change the pattern before applying or navigating seeds.");
		default:
			break;
		}
	}
	if (sortinglogic::inputPatternFixedItemCount(manualSession_.simulation.pattern()) != 0)
	{
		switch (action.kind)
		{
		case AppActionKind::DecreaseCountFine:
		case AppActionKind::DecreaseCountCoarse:
		case AppActionKind::IncreaseCountFine:
		case AppActionKind::IncreaseCountCoarse:
		case AppActionKind::AdjustItemCount:
		case AppActionKind::SetItemCount:
			return sortinglogic::CommandResult(false, "The selected input pattern requires exactly 100 items. Change the pattern before editing the item count.");
		default:
			break;
		}
	}
	sortinglogic::CommandResult result(true, "");
	const PreferenceChangeSnapshot previous(preferences_, needsFullPreferenceSnapshot(action.kind));
	const bool wasPlaying = activeSimulation().isPlaying();
	const std::shared_ptr<const sortinglogic::SortTrace> previousTrace = activeSimulation().currentTrace();
	switch (action.kind)
	{
	case AppActionKind::Shuffle:
		result = activeSimulation().prepareShuffle(freshStartupOrShuffleSeed());
		if (result.accepted) seedHistory_.append(activeSimulation().snapshot().inputSeed);
		break;
	case AppActionKind::SetSeed:
		if (activeSimulation().isPlaying() || activeSimulation().isBuildingTrace())
			result = sortinglogic::CommandResult(false, "Pause playback and wait for trace preparation before applying a seed.");
		else if (activeSimulation().snapshot().inputSeed == action.seed)
			result = sortinglogic::CommandResult(true, "Seed unchanged.");
		else
		{
			result = activeSimulation().prepareShuffle(action.seed);
			if (result.accepted) seedHistory_.append(action.seed);
		}
		break;
	case AppActionKind::CopySeed:
		result = sortinglogic::CommandResult(true, "Copy seed " + std::to_string(activeSimulation().snapshot().inputSeed) + " to the clipboard.");
		break;
	case AppActionKind::SaveSeedBookmark:
	{
		const std::uint32_t seed = activeSimulation().snapshot().inputSeed;
		const bool existed = seedBookmarks_.contains(seed);
		const SeedBookmarkStore previousBookmarks = seedBookmarks_;
		const bool added = seedBookmarks_.save(seed, action.text);
		if (!added && !existed)
			result = sortinglogic::CommandResult(false, "Could not save this seed; the bookmark limit or name limit was reached.");
		else if (added)
		{
			std::string error;
			if (!persistSeedBookmarks(&error))
			{
				seedBookmarks_ = previousBookmarks;
				result = sortinglogic::CommandResult(false, "Could not save seed favorites: " + error);
			}
			else result = sortinglogic::CommandResult(true, "Saved seed " + std::to_string(seed) + " to favorites.");
		}
		else result = sortinglogic::CommandResult(true, "This seed is already saved; selected its existing bookmark.");
		break;
	}
	case AppActionKind::SelectSeedBookmark:
		if (action.selection < 0 || static_cast<std::size_t>(action.selection) >= seedBookmarks_.bookmarks().size())
			result = sortinglogic::CommandResult(false, "The selected saved seed is unavailable.");
		else
		{
			const std::uint32_t seed = seedBookmarks_.bookmarks()[static_cast<std::size_t>(action.selection)].seed;
			if (activeSimulation().isPlaying() || activeSimulation().isBuildingTrace())
				result = sortinglogic::CommandResult(false, "Pause playback and wait for trace preparation before applying a saved seed.");
			else
			{
				result = seed == activeSimulation().snapshot().inputSeed
					? sortinglogic::CommandResult(true, "Saved seed selected.") : activeSimulation().prepareShuffle(seed);
				if (result.accepted)
				{
					seedBookmarks_.select(static_cast<std::size_t>(action.selection));
					seedHistory_.append(seed);
				}
			}
		}
		break;
	case AppActionKind::PreviousSeed:
	case AppActionKind::NextSeed:
	{
		const bool previousSeed = action.kind == AppActionKind::PreviousSeed;
		if (activeSimulation().isPlaying() || activeSimulation().isBuildingTrace())
			result = sortinglogic::CommandResult(false, "Pause playback and wait for trace preparation before browsing seed history.");
		else if (previousSeed ? !seedHistory_.canGoPrevious() : !seedHistory_.canGoNext())
			result = sortinglogic::CommandResult(false, previousSeed ? "There is no previous seed." : "There is no next seed.");
		else
		{
			const std::uint32_t seed = previousSeed ? seedHistory_.previous() : seedHistory_.next();
			result = activeSimulation().prepareShuffle(seed);
			if (result.accepted)
				previousSeed ? seedHistory_.movePrevious() : seedHistory_.moveNext();
		}
		break;
	}
	case AppActionKind::RenameSeedBookmark:
	case AppActionKind::RemoveSeedBookmark:
	{
		const std::size_t index = action.selection < 0 ? seedBookmarks_.selectedIndex() : static_cast<std::size_t>(action.selection);
		if (index >= seedBookmarks_.bookmarks().size())
			result = sortinglogic::CommandResult(false, "Select a saved seed first.");
		else
		{
			const SeedBookmarkStore previousBookmarks = seedBookmarks_;
			const bool changed = action.kind == AppActionKind::RenameSeedBookmark
				? seedBookmarks_.rename(index, action.text) : seedBookmarks_.remove(index);
			if (!changed) result = sortinglogic::CommandResult(action.kind == AppActionKind::RenameSeedBookmark,
				action.kind == AppActionKind::RenameSeedBookmark ? "Saved seed name was unchanged." : "Saved seed removed.");
			else
			{
				std::string error;
				if (!persistSeedBookmarks(&error))
				{
					seedBookmarks_ = previousBookmarks;
					result = sortinglogic::CommandResult(false, "Could not update seed favorites: " + error);
				}
				else result = sortinglogic::CommandResult(true, action.kind == AppActionKind::RenameSeedBookmark
					? "Saved seed renamed." : "Saved seed removed.");
			}
		}
		break;
	}
	case AppActionKind::InstaSort:
		result = activeSimulation().prepareInstaSort();
		break;
	case AppActionKind::InstaInvert:
		result = activeSimulation().prepareInstaInvert();
		break;
	case AppActionKind::SelectAlgorithm:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		if (!descriptor)
			result = sortinglogic::CommandResult(false, "The selected algorithm family and variant are not registered.");
		else
			result = activeSimulation().selectAlgorithm(descriptor->id);
		break;
	}
	case AppActionKind::SetDirection:
		result = activeSimulation().setDirection(action.selection == 1
			? sortinglogic::SortDirection::Descending
			: sortinglogic::SortDirection::Ascending);
		break;
	case AppActionKind::SetPlaybackTimingMode:
		if (activeSimulation().isPlaying() || activeSimulation().isBuildingTrace())
			result = sortinglogic::CommandResult(false, "Pause playback and wait for trace preparation before changing timing mode.");
		else if (action.selection < static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration) ||
			action.selection > static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence))
			result = sortinglogic::CommandResult(false, "The selected playback timing mode is invalid.");
		else
		{
			preferences_.simulation.playbackTimingMode = static_cast<SoundEngine::PlaybackTimingMode>(action.selection);
			manualSession_.timingMode = preferences_.simulation.playbackTimingMode;
		}
		break;
	case AppActionKind::SetCompletionScan:
		if (action.selection < 0 || action.selection > 1)
			result = sortinglogic::CommandResult(false, "The selected completion scan setting is invalid.");
		else
		{
			result = activeSimulation().setCompletionScanEnabled(action.selection == 1);
			if (result.accepted)
				preferences_.simulation.completionScanEnabled = activeSimulation().completionScanEnabled();
		}
		break;
	case AppActionKind::BuildTrace:
		result = requestTraceBuild(false);
		break;
	case AppActionKind::GenerateAndPlay:
		if (activeSimulation().isPlaying())
		{
			activePlaybackCoordinator().cancelQueuedPlayback();
			result = activeSimulation().togglePlayback();
		}
		else if (activePlaybackCoordinator().playbackQueued())
		{
			activePlaybackCoordinator().cancelQueuedPlayback();
			result = sortinglogic::CommandResult(true, "Queued playback cancelled.");
		}
		else if (activeSimulation().isBuildingTrace())
		{
			activePlaybackCoordinator().queuePlayback();
			result = sortinglogic::CommandResult(true, "Playback queued; it will start when the trace and audio timeline are ready.");
		}
		else if (!activeSimulation().currentTrace())
			result = requestTraceBuild(true);
		else if (activePlaybackCoordinator().audioPreparationBlocksStart())
		{
			activePlaybackCoordinator().queuePlayback();
			result = sortinglogic::CommandResult(true, "Playback queued until audio preparation finishes.");
		}
		else
			result = activeSimulation().togglePlayback();
		break;
	case AppActionKind::TogglePlayback:
		if (activePlaybackCoordinator().audioPreparationBlocksStart() && !activeSimulation().isPlaying())
			result = sortinglogic::CommandResult(false, "Preparing audio timeline. Playback will be available as soon as preparation finishes.");
		else result = activeSimulation().togglePlayback();
		break;
	case AppActionKind::Reset:
		result = activeSimulation().reset();
		break;
	case AppActionKind::PreviousEvent:
		result = activeSimulation().previousEvent();
		break;
	case AppActionKind::NextEvent:
		result = activeSimulation().nextEvent();
		break;
	case AppActionKind::DecreaseCountFine:
	case AppActionKind::DecreaseCountCoarse:
	case AppActionKind::IncreaseCountFine:
	case AppActionKind::IncreaseCountCoarse:
	{
		const bool increase = action.kind == AppActionKind::IncreaseCountFine || action.kind == AppActionKind::IncreaseCountCoarse;
		const bool coarse = action.kind == AppActionKind::DecreaseCountCoarse || action.kind == AppActionKind::IncreaseCountCoarse;
		const int next = adjustBarCount(static_cast<int>(activeSimulation().itemCount()), increase, coarse,
			static_cast<int>(sortinglogic::MinItemCount), static_cast<int>(sortinglogic::MaxItemCount));
		result = activeSimulation().setItemCount(static_cast<std::size_t>(next));
		break;
	}
	case AppActionKind::AdjustItemCount:
	{
		const int delta = static_cast<int>(std::lround(action.value));
		const int next = std::max(static_cast<int>(sortinglogic::MinItemCount),
			std::min(static_cast<int>(sortinglogic::MaxItemCount), static_cast<int>(activeSimulation().itemCount()) + delta));
		result = activeSimulation().setItemCount(static_cast<std::size_t>(next));
		break;
	}
	case AppActionKind::SetItemCount:
		result = activeSimulation().setItemCount(action.selection < 0 ? 0U : static_cast<std::size_t>(action.selection));
		break;
	case AppActionKind::DecreaseDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = activeSimulation().setDuration(std::max(sortinglogic::MinRunDurationSeconds, activeSimulation().durationSeconds() - 1.0f));
		break;
	case AppActionKind::AdjustDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = activeSimulation().setDuration(std::max(sortinglogic::MinRunDurationSeconds,
			std::min(sortinglogic::MaxRunDurationSeconds, activeSimulation().durationSeconds() + action.value)));
		break;
	case AppActionKind::IncreaseDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = activeSimulation().setDuration(std::min(sortinglogic::MaxRunDurationSeconds, activeSimulation().durationSeconds() + 1.0f));
		break;
	case AppActionKind::SetDuration:
		if (preferences_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			result = sortinglogic::CommandResult(false, "Reference cadence calculates run duration from the number of audible events.");
		else result = activeSimulation().setDuration(action.value);
		break;
	case AppActionKind::SeekFraction:
		result = activeSimulation().seekToFraction(action.value);
		break;
	case AppActionKind::SetUiPreset:
		if (action.selection < static_cast<int>(UiPresetId::Current) || action.selection > static_cast<int>(UiPresetId::ClassicWindows95))
			result = sortinglogic::CommandResult(false, "The selected interface preset is unavailable.");
		else
			preferences_.appearance.activePreset = static_cast<UiPresetId>(action.selection);
		break;
	case AppActionKind::ApplyAppearance:
	{
		if (!action.appearance) return sortinglogic::CommandResult(false, "The appearance draft is missing.");
		std::string error;
		if (!applyAppearance(*action.appearance, &error))
			return sortinglogic::CommandResult(false, error.empty() ? "Could not apply the appearance." : error);
		return sortinglogic::CommandResult(true, "Appearance saved.");
	}
	case AppActionKind::SetPalette:
		if (action.selection < static_cast<int>(PaletteId::Cyan) || action.selection > static_cast<int>(PaletteId::Customized))
			result = sortinglogic::CommandResult(false, "The selected color palette is invalid.");
		else
		{
			const UiPresetId preset = preferences_.appearance.activePreset;
			UiThemeCustomization& theme = preferences_.appearance.themes[preset];
			selectUiPalette(preset, theme, static_cast<PaletteId>(action.selection));
			if (preset == UiPresetId::Current) preferences_.visual.palette = theme.palette;
		}
		break;
	case AppActionKind::SetSpacing:
		if (action.selection < static_cast<int>(BarSpacingMode::None) || action.selection > static_cast<int>(BarSpacingMode::Comfortable))
			result = sortinglogic::CommandResult(false, "The selected bar spacing is invalid.");
		else
			preferences_.visual.spacing = static_cast<BarSpacingMode>(action.selection);
		break;
	case AppActionKind::SetPanelExpanded:
		preferences_.visual.panelExpanded = action.selection != 0;
		break;
	case AppActionKind::TogglePanelExpanded:
		preferences_.visual.panelExpanded = !preferences_.visual.panelExpanded;
		break;
	case AppActionKind::ToggleBorderlessFullscreen:
		// Fullscreen is a window-session action handled by the SDL host.
		break;
	case AppActionKind::SetInputProfile:
		if (action.selection < static_cast<int>(sortinglogic::InputProfile::NormalizedFloat) ||
			action.selection > static_cast<int>(sortinglogic::InputProfile::IntegerKeys))
			result = sortinglogic::CommandResult(false, "The selected data profile is invalid.");
		else
		{
			result = activeSimulation().setInputProfile(static_cast<sortinglogic::InputProfile>(action.selection));
			if (result.accepted)
				preferences_.simulation.domain = activeSimulation().domain();
		}
		break;
	case AppActionKind::SetInputDomain:
		if (action.selection < static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) ||
			action.selection > static_cast<int>(sortinglogic::InputDomain::IntegerPermutation))
			result = sortinglogic::CommandResult(false, "The selected input domain is invalid.");
		else
		{
			result = activeSimulation().setInputDomain(static_cast<sortinglogic::InputDomain>(action.selection));
			if (result.accepted)
				preferences_.simulation.domain = activeSimulation().domain();
		}
		break;
	case AppActionKind::SetInputPattern:
		if (action.selection < static_cast<int>(sortinglogic::InputPattern::Random) ||
			action.selection > static_cast<int>(sortinglogic::InputPattern::AndrutReference))
			result = sortinglogic::CommandResult(false, "The selected input pattern is invalid.");
		else
		{
			result = activeSimulation().setInputPattern(static_cast<sortinglogic::InputPattern>(action.selection));
			if (result.accepted) preferences_.simulation.pattern = activeSimulation().pattern();
		}
		break;
	case AppActionKind::SetAlgorithmOption:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		const sortinglogic::AlgorithmOptionDescriptor* option = nullptr;
			if (descriptor)
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
				if (std::string(descriptor->supportedOptions[i].groupId) == action.optionGroupId &&
					descriptor->supportedOptions[i].stableId == action.optionStableId)
				{
					option = &descriptor->supportedOptions[i];
					break;
				}
		const std::string previousStableId = descriptor && option
			? algorithmOptionStableId(preferences_, *descriptor, action.optionGroupId) : std::string();
		if (!descriptor || !option || static_cast<sortinglogic::SortAlgorithm>(preferences_.simulation.algorithm) != descriptor->id)
			result = sortinglogic::CommandResult(false, "The selected algorithm option is not available for the active algorithm.");
		else
		{
			result = activeSimulation().setAlgorithmOption(action.optionGroupId, action.optionStableId);
		}
		if (result.accepted && previousStableId != option->stableId)
			preferences_.algorithmOptions[algorithmOptionSettingsKey(descriptor->familyId, descriptor->variantId,
				action.optionGroupId)] = option->stableId;
		break;
	}
	case AppActionKind::SetAudioParameter:
		preferences_.audio = audioSettingsActions::apply(preferences_.audio, action);
		break;
	case AppActionKind::SetReconstructedAudioParameter:
		preferences_.reconstructedAudio = audioSettingsActions::apply(preferences_.reconstructedAudio, action);
		break;
	case AppActionKind::RestoreReconstructedDefaults:
		preferences_.reconstructedAudio = SoundEngine::ReconstructedParameters();
		preferences_.familyAudioDefaults.clear();
		preferences_.variantAudioOffsets.clear();
		result = sortinglogic::CommandResult(true, "Reconstructed audio defaults restored.");
		break;
	case AppActionKind::SetVariantAudioLevel:
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(action.familyId, action.variantId);
		if (!descriptor || action.index < 0 || action.index >= static_cast<int>(AlgorithmAudioChannel::Count))
			result = sortinglogic::CommandResult(false, "The selected algorithm audio control is invalid.");
		else if (!std::isfinite(action.value) || action.value < -60.0f || action.value > 12.0f)
			result = sortinglogic::CommandResult(false, "Algorithm audio offsets must be between -60 and +12 dB.");
		else
		{
			const std::string key = variantAudioSettingsKey(action.familyId, action.variantId);
			AlgorithmAudioLevels& levels = preferences_.variantAudioOffsets[key];
			levels.decibels[static_cast<std::size_t>(action.index)] = action.value;
		}
		break;
	}
	case AppActionKind::ResetVariantAudioLevels:
	{
		if (!sortinglogic::algorithmDescriptorById(action.familyId, action.variantId))
			result = sortinglogic::CommandResult(false, "The selected algorithm audio settings are not registered.");
		else
			preferences_.variantAudioOffsets.erase(variantAudioSettingsKey(action.familyId, action.variantId));
		break;
	}
	case AppActionKind::SelectAudioBand:
		if (action.selection < 0 || static_cast<std::size_t>(action.selection) >= SoundEngine::BandCount)
			result = sortinglogic::CommandResult(false, "The selected frequency band is invalid.");
		break;
	case AppActionKind::RetryAudioDevice:
		// The SDL adapter handles this app-level command; the controller keeps it harmless.
		break;
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
		return dispatchDemoAction(action);
	case AppActionKind::None:
		break;
	}

	if (result.accepted && activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence &&
		(action.kind == AppActionKind::BuildTrace || action.kind == AppActionKind::GenerateAndPlay ||
		 action.kind == AppActionKind::Shuffle || action.kind == AppActionKind::InstaSort ||
		 action.kind == AppActionKind::InstaInvert || action.kind == AppActionKind::TogglePlayback || action.kind == AppActionKind::Reset ||
		 action.kind == AppActionKind::PreviousEvent || action.kind == AppActionKind::NextEvent || action.kind == AppActionKind::SeekFraction ||
		 action.kind == AppActionKind::SetPlaybackTimingMode))
		synchronizeReferenceTransport(wasPlaying);
	if (result.accepted && activeSimulation().hasTrace() &&
		(action.kind == AppActionKind::SetPlaybackTimingMode || action.kind == AppActionKind::DecreaseDuration ||
		 action.kind == AppActionKind::IncreaseDuration || action.kind == AppActionKind::AdjustDuration || action.kind == AppActionKind::SetDuration))
		activePlaybackCoordinator().markAudioPreparationPending(true);
	if (result.accepted && !demoRunner_.active())
		synchronizeSimulationPreferences();
	if (result.accepted && previousTrace && activeSimulation().currentTrace() != previousTrace &&
		action.kind != AppActionKind::BuildTrace && action.kind != AppActionKind::GenerateAndPlay)
		activePlaybackCoordinator().cancelQueuedPlayback();
	if (result.accepted && (action.kind == AppActionKind::Reset || action.kind == AppActionKind::PreviousEvent ||
		action.kind == AppActionKind::NextEvent || action.kind == AppActionKind::SeekFraction))
		activePlaybackCoordinator().cancelQueuedPlayback();
	if (result.accepted && demoRunner_.active() &&
		(action.kind == AppActionKind::TogglePlayback || action.kind == AppActionKind::GenerateAndPlay))
	{
		if (activeSimulation().isPlaying()) demoRunner_.markPlaying();
		else demoRunner_.markPaused();
	}
	if (previous.changed(preferences_))
	{
		persistPreferences();
		if (result.accepted && persistenceStatus_.find("Could not save") == 0)
			result.message += " " + persistenceStatus_;
	}
	if (action.kind == AppActionKind::CopySeed || action.kind == AppActionKind::SaveSeedBookmark ||
		action.kind == AppActionKind::SelectSeedBookmark || action.kind == AppActionKind::RenameSeedBookmark ||
		action.kind == AppActionKind::RemoveSeedBookmark || action.kind == AppActionKind::PreviousSeed ||
		action.kind == AppActionKind::NextSeed)
		commandStatus_ = result.message;
	else commandStatus_.clear();
	return result;
}

const DemoPreset* AppController::findDemoPreset(const std::string& id) const
{
	if (const DemoPreset* prefab = demoPrefabPreset(id)) return prefab;
	return demoPresetStore_.findUserPreset(id);
}

void AppController::refreshDemoDraftDirty()
{
	demoEditorDraftDirty_ = demoEditorDraftInitialized_ && !sameDemoConfiguration(demoEditorDraft_, demoEditorBaseline_);
}

void AppController::adoptDemoDraftBaseline()
{
	demoEditorDraftInitialized_ = true;
	demoEditorBaseline_ = demoEditorDraft_;
	demoEditorDraftDirty_ = false;
}

bool AppController::persistDemoPresets(std::string* error)
{
	const bool saved = demoPresetPersistence_ && demoPresetPersistence_->save(demoPresetStore_.userPresets(), error);
	if (!saved)
		persistenceStatus_ = "Could not save demo presets" + (error && !error->empty() ? ": " + *error : ".");
	else persistenceStatus_ = "Demo presets saved.";
	return saved;
}

sortinglogic::CommandResult AppController::startDemo(const std::string& presetId)
{
	if (demoRunner_.active()) return sortinglogic::CommandResult(false, "A demo is already active.");
	if (traceBuildService_.isBusy() || manualSession_.simulation.isBuildingTrace())
		return sortinglogic::CommandResult(false, "Wait for the current trace build to finish before starting a demo.");
	if (!presetId.empty() && presetId != selectedDemoPresetId_)
	{
		if (demoEditorDraftDirty_)
			return sortinglogic::CommandResult(false, "Select the desired preset in the editor after resolving the unsaved draft.");
		const DemoPreset* requested = findDemoPreset(presetId);
		if (!requested) return sortinglogic::CommandResult(false, "The selected demo preset is unavailable.");
		selectedDemoPresetId_ = requested->id;
		demoEditorDraft_ = *requested;
		demoEditorSelectedStep_ = 0;
		editorPrefabSourceId_ = requested->prefab ? requested->id : std::string();
		adoptDemoDraftBaseline();
	}
	const DemoPreset* source = demoEditorDraftInitialized_ ? &demoEditorDraft_ : findDemoPreset(selectedDemoPresetId_);
	if (!source) return sortinglogic::CommandResult(false, "The selected demo preset is unavailable.");
	const std::vector<DemoPresetIssue> validation = validateDemoPreset(*source);
	if (!validation.empty()) return sortinglogic::CommandResult(false, validation.front().message);
	std::string error;
	if (!demoRunner_.begin(*source, &error))
		return sortinglogic::CommandResult(false, error.empty() ? "The selected demo preset is invalid." : error);
	if (manualSession_.simulation.isPlaying()) manualSession_.simulation.stopPlayback();
	manualSession_.playback.cancelQueuedPlayback();
	if (manualSession_.simulation.currentTrace())
		manualSession_.playback.setReferenceElapsedSeconds(referenceElapsedAtCursor(manualSession_.simulation.cursor()));
	selectedDemoPresetId_ = source->id;
	demoSession_.reset();
	demoEditorOpen_ = false;
	prepareRequestedDemoStep();
	return sortinglogic::CommandResult(true, "Starting " + source->name + ". " + demoRunner_.message());
}

sortinglogic::CommandResult AppController::stopDemo(const std::string& message)
{
	if (!demoRunner_.active() && !demoSession_)
		return sortinglogic::CommandResult(true, message.empty() ? "No demo is active." : message);
	if (demoSession_ && demoSession_->simulation.isBuildingTrace()) traceBuildService_.cancelActive();
	if (demoSession_) demoSession_->playback.cancelQueuedPlayback();
	demoSession_.reset();
	pendingDemoStepPreparation_ = false;
	demoRunner_.stop();
	commandStatus_ = message.empty() ? "Demo stopped. Manual session restored paused at its saved event." : message;
	return sortinglogic::CommandResult(true, commandStatus_);
}

void AppController::prepareRequestedDemoStep()
{
	if (!demoRunner_.active() || !demoRunner_.preparationPending() || traceBuildService_.isBusy()) return;
	if (!demoRunner_.takePreparationRequest()) return;
	const DemoStep* step = demoRunner_.currentStep();
	if (!step)
	{
		demoRunner_.fail("The active demo has no algorithm step.");
		finishDemoIfNeeded();
		return;
	}
	if (!demoRunner_.hasEffectiveInput())
	{
		std::string error;
		const DemoInputRecipe recipe = demoRecipeForStep(demoRunner_.preset(), *step, &error);
		if (!error.empty())
		{
			demoRunner_.fail(error);
			finishDemoIfNeeded();
			return;
		}
		const DemoInputAdjustment adjusted = adaptDemoInput(demoRunner_.preset(), *step, recipe);
		if (!adjusted.error.empty())
		{
			demoRunner_.fail(adjusted.error);
			finishDemoIfNeeded();
			return;
		}
		demoRunner_.setEffectiveInput(adjusted.recipe, adjusted.messages);
	}
	createDemoSessionForCurrentStep();
}

void AppController::createDemoSessionForCurrentStep()
{
	if (!demoRunner_.active() || traceBuildService_.isBusy()) return;
	const DemoStep* step = demoRunner_.currentStep();
	const sortinglogic::AlgorithmDescriptor* descriptor = step
		? sortinglogic::algorithmDescriptorById(step->familyId, step->variantId) : nullptr;
	if (!step || !descriptor)
	{
		demoRunner_.fail("The requested demo algorithm is unavailable.");
		finishDemoIfNeeded();
		return;
	}
	const DemoInputRecipe& input = demoRunner_.effectiveInput();
	try
	{
		demoSession_.reset(new RunSession(input.itemCount, input.seed, input.domain, input.pattern,
			step->timingMode, nextSessionIdentity_++, true));
	}
	catch (const std::exception& exception)
	{
		demoRunner_.fail(std::string("Could not create the effective demo input: ") + exception.what());
		finishDemoIfNeeded();
		return;
	}
	demoSession_->step = *step;
	demoSession_->effectiveInput = input;
	demoSession_->simulation.setDirection(step->direction);
	sortinglogic::CommandResult configured = demoSession_->simulation.setCompletionScanEnabled(step->completionScanEnabled);
	if (!configured.accepted)
	{
		demoRunner_.fail("Could not apply the step completion scan setting: " + configured.message);
		finishDemoIfNeeded();
		return;
	}
	configured = demoSession_->simulation.selectAlgorithm(descriptor->id);
	if (!configured.accepted)
	{
		demoRunner_.fail("Could not configure " + std::string(descriptor->displayName) + ": " + configured.message);
		finishDemoIfNeeded();
		return;
	}
	if (step->timingMode == SoundEngine::PlaybackTimingMode::FixedDuration)
	{
		configured = demoSession_->simulation.setDuration(step->fixedDurationSeconds);
		if (!configured.accepted)
		{
			demoRunner_.fail("Could not apply the fixed step duration: " + configured.message);
			finishDemoIfNeeded();
			return;
		}
	}
	for (std::map<std::string, std::string>::const_iterator option = step->optionSelections.begin();
		option != step->optionSelections.end(); ++option)
	{
		configured = demoSession_->simulation.setAlgorithmOption(option->first, option->second);
		if (!configured.accepted)
		{
			demoRunner_.fail("Could not apply option " + option->first + ": " + configured.message);
			finishDemoIfNeeded();
			return;
		}
	}
	configured = requestTraceBuild(!demoRunner_.paused());
	if (!configured.accepted)
	{
		demoRunner_.fail("Could not prepare " + std::string(descriptor->displayName) + ": " + configured.message);
		finishDemoIfNeeded();
	}
}

void AppController::handleDemoBuildCompletion(sortinglogic::TraceBuildResult result)
{
	if (!demoSession_ || result.sessionId != demoSession_->identity ||
		result.requestGeneration != demoSession_->requestGeneration) return;
	if (result.status == sortinglogic::TraceBuildStatus::Cancelled) return;
	if (result.succeeded && !result.trace.complete)
	{
		DemoInputRecipe smaller;
		std::string error;
		if (demoRunner_.retryWithSmallerInput(smaller, &error))
		{
			const DemoStep* step = demoRunner_.currentStep();
			const DemoInputAdjustment adjusted = step
				? adaptDemoInput(demoRunner_.preset(), *step, smaller) : DemoInputAdjustment();
			if (!step || !adjusted.error.empty())
			{
				demoRunner_.fail(step ? adjusted.error : "The retry lost its active step.");
				finishDemoIfNeeded();
				return;
			}
			std::vector<std::string> messages = demoRunner_.adjustments();
			messages.insert(messages.end(), adjusted.messages.begin(), adjusted.messages.end());
			demoRunner_.setEffectiveInput(adjusted.recipe, messages, false);
			demoSession_.reset();
			return;
		}
		demoRunner_.fail("Step " + std::to_string(demoRunner_.stepIndex() + 1) + " could not fit the trace budgets: " +
			(result.trace.completionReason.empty() ? error : result.trace.completionReason));
		finishDemoIfNeeded();
		return;
	}
	if (!result.succeeded)
	{
		demoRunner_.fail("Step " + std::to_string(demoRunner_.stepIndex() + 1) + " failed: " +
			(result.error.empty() ? "trace builder reported an unknown failure." : result.error));
		finishDemoIfNeeded();
		return;
	}
	activeSimulation().completeTraceBuild(std::move(result));
	activePlaybackCoordinator().traceBuildCompleted(activeSimulation().hasTrace());
	if (!activeSimulation().hasTrace())
	{
		demoRunner_.fail("The completed trace was not installed in its owning demo session.");
		finishDemoIfNeeded();
		return;
	}
	demoRunner_.markPrepared();
	if (activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence)
		synchronizeReferenceTransport(false);
}

void AppController::finishDemoIfNeeded()
{
	if (demoRunner_.state() == DemoRunnerState::Finished || demoRunner_.state() == DemoRunnerState::Failed)
	{
		const std::string message = demoRunner_.message();
		stopDemo(message);
	}
}

sortinglogic::CommandResult AppController::dispatchDemoAction(const AppAction& action)
{
	const DemoActionPayload empty;
	const DemoActionPayload& payload = action.demo ? *action.demo : empty;
	const std::string presetId = !payload.presetId.empty() ? payload.presetId : action.text;
	if (demoRunner_.active() && isDemoDraftMutationAction(action.kind))
		return sortinglogic::CommandResult(false, "Demo configuration is read-only while a demo is active, including while paused.");
	if (action.kind == AppActionKind::OpenDemoEditor)
	{
		demoEditorOpen_ = true;
		if (!demoEditorDraftInitialized_)
		{
			const DemoPreset* preset = findDemoPreset(selectedDemoPresetId_);
			if (preset) { demoEditorDraft_ = *preset; adoptDemoDraftBaseline(); }
		}
		return sortinglogic::CommandResult(true, "Demo editor opened.");
	}
	if (action.kind == AppActionKind::CloseDemoEditor)
	{
		demoEditorOpen_ = false;
		return sortinglogic::CommandResult(true, "Demo editor closed.");
	}
	if (action.kind == AppActionKind::SelectDemoPreset)
	{
		const DemoPreset* preset = findDemoPreset(presetId);
		if (!preset) return sortinglogic::CommandResult(false, "The selected demo preset is unavailable.");
		if (preset->id == selectedDemoPresetId_)
			return sortinglogic::CommandResult(true, "The selected preset is already open.");
		if (demoEditorDraftInitialized_ && demoEditorDraftDirty_)
		{
			pendingDemoPresetSwitchId_ = preset->id;
			return sortinglogic::CommandResult(false, "This draft has unsaved changes. Save, discard, or cancel the preset switch.");
		}
		selectedDemoPresetId_ = preset->id;
		demoEditorDraft_ = *preset;
		demoEditorSelectedStep_ = 0;
		editorPrefabSourceId_ = preset->prefab ? preset->id : std::string();
		pendingDemoPresetSwitchId_.clear();
		adoptDemoDraftBaseline();
		return sortinglogic::CommandResult(true, "Selected " + preset->name + ".");
	}
	if (action.kind == AppActionKind::ResolveDemoPresetSwitch)
	{
		const int choice = action.demo ? payload.direction : action.selection;
		if (pendingDemoPresetSwitchId_.empty()) return sortinglogic::CommandResult(false, "There is no preset switch to resolve.");
		if (choice == 2)
		{
			pendingDemoPresetSwitchId_.clear();
			return sortinglogic::CommandResult(true, "Preset switch cancelled; the current draft was kept.");
		}
		const std::string destinationId = pendingDemoPresetSwitchId_;
		if (choice == 0)
		{
			AppAction saveAction(AppActionKind::SaveDemoPreset);
			const sortinglogic::CommandResult saved = dispatchDemoAction(saveAction);
			if (!saved.accepted) return saved;
		}
		else if (choice != 1)
			return sortinglogic::CommandResult(false, "The preset switch choice is invalid.");
		const DemoPreset* destination = findDemoPreset(destinationId);
		if (!destination) return sortinglogic::CommandResult(false, "The requested preset is no longer available; the draft was kept.");
		selectedDemoPresetId_ = destination->id;
		demoEditorDraft_ = *destination;
		demoEditorSelectedStep_ = 0;
		editorPrefabSourceId_ = destination->prefab ? destination->id : std::string();
		pendingDemoPresetSwitchId_.clear();
		adoptDemoDraftBaseline();
		return sortinglogic::CommandResult(true, "Switched to " + destination->name + ".");
	}
	if (action.kind == AppActionKind::SelectDemoStep)
	{
		const std::size_t selected = action.demo ? action.demo->stepIndex : static_cast<std::size_t>((std::max)(0, action.selection));
		if (selected >= demoEditorDraft_.steps.size()) return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		demoEditorSelectedStep_ = selected;
		return sortinglogic::CommandResult(true, "Selected demo step " + std::to_string(selected + 1) + ".");
	}
	if (action.kind == AppActionKind::StartDemo)
		return startDemo(presetId.empty() ? selectedDemoPresetId_ : presetId);
	if (action.kind == AppActionKind::StopDemo)
		return stopDemo();
	if (action.kind == AppActionKind::ToggleDemoPlayback)
	{
		if (!demoRunner_.active()) return startDemo(presetId.empty() ? selectedDemoPresetId_ : presetId);
		if (!demoRunner_.paused())
		{
			activePlaybackCoordinator().cancelQueuedPlayback();
			if (activeSimulation().isPlaying()) activeSimulation().stopPlayback();
			demoRunner_.markPaused();
			return sortinglogic::CommandResult(true, "Demo paused.");
		}
		demoRunner_.resume();
		if (demoSession_ && demoRunner_.state() != DemoRunnerState::Intermission)
			activePlaybackCoordinator().queuePlayback();
		return sortinglogic::CommandResult(true, "Demo resumed; playback continues when preparation finishes.");
	}
	if (action.kind == AppActionKind::PreviousDemoStep || action.kind == AppActionKind::NextDemoStep)
	{
		if (!demoRunner_.active()) return sortinglogic::CommandResult(false, "No demo is active.");
		const bool moved = action.kind == AppActionKind::PreviousDemoStep ? demoRunner_.previousStep() : demoRunner_.nextStep();
		if (!moved) return sortinglogic::CommandResult(false, "There is no neighboring algorithm step in that direction.");
		if (demoSession_ && demoSession_->simulation.isBuildingTrace()) traceBuildService_.cancelActive();
		if (demoSession_) demoSession_->playback.cancelQueuedPlayback();
		demoSession_.reset();
		prepareRequestedDemoStep();
		return sortinglogic::CommandResult(true, demoRunner_.message());
	}
	if (!demoEditorOpen_ && isDemoDraftMutationAction(action.kind))
		return sortinglogic::CommandResult(false, "Open the demo editor before changing its draft.");

	if (action.kind == AppActionKind::DemoSetClassRecipe)
	{
		if (payload.complexityClass < static_cast<int>(sortinglogic::DemoComplexityClass::Linear) ||
			payload.complexityClass > static_cast<int>(sortinglogic::DemoComplexityClass::DemonstrationUnknown) ||
			payload.seed > (std::numeric_limits<std::uint32_t>::max)())
			return sortinglogic::CommandResult(false, "The input recipe is outside its supported range.");
		DemoInputRecipe recipe(payload.seed, payload.itemCount, static_cast<sortinglogic::InputDomain>(payload.inputDomain),
			static_cast<sortinglogic::InputPattern>(payload.inputPattern));
		if (static_cast<int>(recipe.domain) < static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) ||
			static_cast<int>(recipe.domain) > static_cast<int>(sortinglogic::InputDomain::IntegerPermutation) ||
			static_cast<int>(recipe.pattern) < static_cast<int>(sortinglogic::InputPattern::Random) ||
			static_cast<int>(recipe.pattern) > static_cast<int>(sortinglogic::InputPattern::AndrutReference))
			return sortinglogic::CommandResult(false, "The input recipe domain or pattern is invalid.");
		const sortinglogic::DemoComplexityClass complexity = static_cast<sortinglogic::DemoComplexityClass>(payload.complexityClass);
		const std::size_t fixedCount = sortinglogic::inputPatternFixedItemCount(recipe.pattern);
		if (fixedCount != 0)
		{
			recipe.itemCount = fixedCount;
			if (recipe.pattern == sortinglogic::InputPattern::AndrutReference)
				recipe.domain = sortinglogic::InputDomain::NormalizedFloats;
		}
		if (!sortinglogic::inputPatternUsesSeed(recipe.pattern))
		{
			const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator existing = demoEditorDraft_.classInputs.find(complexity);
			if (existing != demoEditorDraft_.classInputs.end()) recipe.seed = existing->second.seed;
		}
		if (recipe.itemCount < sortinglogic::MinItemCount || recipe.itemCount > sortinglogic::MaxItemCount)
			return sortinglogic::CommandResult(false, "The input recipe item count is outside its supported range.");
		demoEditorDraft_.classInputs[complexity] = recipe;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Complexity-class input updated in the draft.");
	}
	if (action.kind == AppActionKind::DemoSetIntermission)
	{
		if (!std::isfinite(payload.value) || payload.value < 0.0f || payload.value > 120.0f)
			return sortinglogic::CommandResult(false, "Intermission must be between 0 and 120 seconds.");
		demoEditorDraft_.intermissionSeconds = payload.value;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Demo intermission updated.");
	}
	if (action.kind == AppActionKind::DemoSetLoop)
	{
		demoEditorDraft_.loop = payload.flag;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Demo looping updated.");
	}
	if (action.kind == AppActionKind::DemoAddStep)
	{
		if (demoEditorDraft_.steps.size() >= DemoPresetStore::MaximumStepsPerPreset)
			return sortinglogic::CommandResult(false, "The demo step limit has been reached.");
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(
			payload.familyId.empty() ? action.familyId : payload.familyId,
			payload.variantId.empty() ? action.variantId : payload.variantId);
		if (!descriptor) return sortinglogic::CommandResult(false, "Select a registered algorithm to add a demo step.");
		DemoStep step;
		step.familyId = descriptor->familyId;
		step.variantId = descriptor->variantId;
		std::set<std::string> optionGroups;
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			if (descriptor->supportedOptions[i].isDefault && optionGroups.insert(descriptor->supportedOptions[i].groupId).second)
				step.optionSelections[descriptor->supportedOptions[i].groupId] = descriptor->supportedOptions[i].stableId;
		if (demoEditorDraft_.classInputs.find(descriptor->demoComplexity) == demoEditorDraft_.classInputs.end())
			demoEditorDraft_.classInputs[descriptor->demoComplexity] = DemoInputRecipe(
			manualSession_.simulation.snapshot().inputSeed, manualSession_.simulation.itemCount(),
			manualSession_.simulation.domain(), manualSession_.simulation.pattern());
		const std::size_t insertionIndex = std::min(payload.stepIndex, demoEditorDraft_.steps.size());
		demoEditorDraft_.steps.insert(demoEditorDraft_.steps.begin() + static_cast<std::ptrdiff_t>(insertionIndex), step);
		demoEditorSelectedStep_ = insertionIndex;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Added " + std::string(descriptor->displayName) + " to the demo draft.");
	}
	if (action.kind == AppActionKind::DemoRemoveStep || action.kind == AppActionKind::DemoMoveStepUp ||
		action.kind == AppActionKind::DemoMoveStepDown)
	{
		if (payload.stepIndex >= demoEditorDraft_.steps.size())
			return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		if (action.kind == AppActionKind::DemoRemoveStep)
		{
			demoEditorDraft_.steps.erase(demoEditorDraft_.steps.begin() + static_cast<std::ptrdiff_t>(payload.stepIndex));
			demoEditorSelectedStep_ = demoEditorDraft_.steps.empty() ? 0 : (std::min)(payload.stepIndex, demoEditorDraft_.steps.size() - 1);
		}
		else
		{
			const std::size_t destination = action.kind == AppActionKind::DemoMoveStepUp
				? (payload.stepIndex == 0 ? 0 : payload.stepIndex - 1)
				: (std::min)(demoEditorDraft_.steps.size() - 1, payload.stepIndex + 1);
			if (destination == payload.stepIndex)
				return sortinglogic::CommandResult(false, "The selected step is already at that end of the sequence.");
			std::swap(demoEditorDraft_.steps[payload.stepIndex], demoEditorDraft_.steps[destination]);
			demoEditorSelectedStep_ = destination;
		}
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Demo step order updated.");
	}
	if (action.kind == AppActionKind::DemoSetStepAlgorithm)
	{
		if (payload.stepIndex >= demoEditorDraft_.steps.size()) return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(payload.familyId, payload.variantId);
		if (!descriptor) return sortinglogic::CommandResult(false, "The selected algorithm is unavailable.");
		DemoStep& step = demoEditorDraft_.steps[payload.stepIndex];
		step.familyId = descriptor->familyId;
		step.variantId = descriptor->variantId;
		step.optionSelections.clear();
		std::set<std::string> optionGroups;
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			if (descriptor->supportedOptions[i].isDefault && optionGroups.insert(descriptor->supportedOptions[i].groupId).second)
				step.optionSelections[descriptor->supportedOptions[i].groupId] = descriptor->supportedOptions[i].stableId;
		if (!step.hasInputOverride && demoEditorDraft_.classInputs.find(descriptor->demoComplexity) == demoEditorDraft_.classInputs.end())
			demoEditorDraft_.classInputs[descriptor->demoComplexity] = DemoInputRecipe(
				manualSession_.simulation.snapshot().inputSeed, manualSession_.simulation.itemCount(),
				manualSession_.simulation.domain(), manualSession_.simulation.pattern());
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Demo step algorithm updated.");
	}
	if (action.kind == AppActionKind::DemoSetStepTiming || action.kind == AppActionKind::DemoSetStepDirection ||
		action.kind == AppActionKind::DemoSetStepCompletionScan)
	{
		if (payload.stepIndex >= demoEditorDraft_.steps.size()) return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		DemoStep& step = demoEditorDraft_.steps[payload.stepIndex];
		if (action.kind == AppActionKind::DemoSetStepTiming)
		{
			if (payload.timingMode < static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration) ||
				payload.timingMode > static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence) ||
				!std::isfinite(payload.value) || payload.value < 1.0f || payload.value > 120.0f)
				return sortinglogic::CommandResult(false, "Step timing or fixed duration is invalid.");
			step.timingMode = static_cast<SoundEngine::PlaybackTimingMode>(payload.timingMode);
			step.fixedDurationSeconds = payload.value;
		}
		else if (action.kind == AppActionKind::DemoSetStepDirection)
		{
			if (payload.direction < 0 || payload.direction > 1) return sortinglogic::CommandResult(false, "Step direction is invalid.");
			step.direction = static_cast<sortinglogic::SortDirection>(payload.direction);
		}
		else step.completionScanEnabled = payload.flag;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Demo step settings updated.");
	}
	if (action.kind == AppActionKind::DemoSetStepOption)
	{
		if (payload.stepIndex >= demoEditorDraft_.steps.size()) return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		DemoStep& step = demoEditorDraft_.steps[payload.stepIndex];
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
		if (!descriptor) return sortinglogic::CommandResult(false, "The step algorithm is unavailable; its option cannot be validated.");
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			if (payload.optionGroupId == descriptor->supportedOptions[i].groupId &&
				payload.optionStableId == descriptor->supportedOptions[i].stableId)
			{
				step.optionSelections[payload.optionGroupId] = payload.optionStableId;
				refreshDemoDraftDirty();
				return sortinglogic::CommandResult(true, "Demo step option updated.");
			}
		return sortinglogic::CommandResult(false, "That option is not registered for the selected algorithm step.");
	}
	if (action.kind == AppActionKind::DemoSetStepOverride || action.kind == AppActionKind::DemoClearStepOverride)
	{
		if (payload.stepIndex >= demoEditorDraft_.steps.size()) return sortinglogic::CommandResult(false, "The selected demo step is unavailable.");
		DemoStep& step = demoEditorDraft_.steps[payload.stepIndex];
		if (action.kind == AppActionKind::DemoClearStepOverride) step.hasInputOverride = false;
		else
		{
			DemoInputRecipe recipe(payload.seed, payload.itemCount, static_cast<sortinglogic::InputDomain>(payload.inputDomain),
				static_cast<sortinglogic::InputPattern>(payload.inputPattern));
			if (static_cast<int>(recipe.domain) < static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) ||
				static_cast<int>(recipe.domain) > static_cast<int>(sortinglogic::InputDomain::IntegerPermutation) ||
				static_cast<int>(recipe.pattern) < static_cast<int>(sortinglogic::InputPattern::Random) ||
				static_cast<int>(recipe.pattern) > static_cast<int>(sortinglogic::InputPattern::AndrutReference))
				return sortinglogic::CommandResult(false, "Step input override is invalid.");
			const std::size_t fixedCount = sortinglogic::inputPatternFixedItemCount(recipe.pattern);
			if (fixedCount != 0)
			{
				recipe.itemCount = fixedCount;
				if (recipe.pattern == sortinglogic::InputPattern::AndrutReference)
					recipe.domain = sortinglogic::InputDomain::NormalizedFloats;
			}
			if (!sortinglogic::inputPatternUsesSeed(recipe.pattern))
			{
				if (step.hasInputOverride) recipe.seed = step.inputOverride.seed;
				else
				{
					const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
					if (descriptor)
					{
						const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator shared = demoEditorDraft_.classInputs.find(descriptor->demoComplexity);
						if (shared != demoEditorDraft_.classInputs.end()) recipe.seed = shared->second.seed;
					}
				}
			}
			if (recipe.itemCount < sortinglogic::MinItemCount || recipe.itemCount > sortinglogic::MaxItemCount)
				return sortinglogic::CommandResult(false, "Step input override item count is outside its supported range.");
			step.inputOverride = recipe;
			step.hasInputOverride = true;
		}
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, action.kind == AppActionKind::DemoClearStepOverride
			? "Step input override removed." : "Step input override updated.");
	}
	if (action.kind == AppActionKind::DemoSetDraftName)
	{
		if (payload.presetName.size() > 64)
			return sortinglogic::CommandResult(false, "Preset names are limited to 64 characters.");
		demoEditorDraft_.name = payload.presetName;
		refreshDemoDraftDirty();
		return sortinglogic::CommandResult(true, "Draft name updated.");
	}
	if (action.kind == AppActionKind::SaveDemoPreset)
	{
		DemoPreset candidate = demoEditorDraft_;
		if (!payload.presetName.empty()) candidate.name = payload.presetName;
		if (candidate.steps.empty())
			return sortinglogic::CommandResult(false, "Add at least one algorithm before saving a demo preset.");
		if (candidate.name.empty())
			return sortinglogic::CommandResult(false, "Enter a preset name before saving.");
		if (candidate.prefab || candidate.id.empty() || demoPrefabPreset(candidate.id))
		{
			candidate.prefab = false;
			std::size_t serial = demoPresetStore_.userPresets().size() + 1;
			candidate.id = "user.demo." + std::to_string(serial);
			while (findDemoPreset(candidate.id)) candidate.id = "user.demo." + std::to_string(++serial);
		}
		const DemoPresetStore previous = demoPresetStore_;
		std::string error;
		if (!demoPresetStore_.saveUserPreset(candidate, &error)) return sortinglogic::CommandResult(false, error);
		if (!persistDemoPresets(&error))
		{
			demoPresetStore_ = previous;
			return sortinglogic::CommandResult(false, error.empty() ? "Could not save demo preset." : error);
		}
		demoEditorDraft_ = candidate;
		selectedDemoPresetId_ = candidate.id;
		editorPrefabSourceId_.clear();
		adoptDemoDraftBaseline();
		return sortinglogic::CommandResult(true, "Saved demo preset " + candidate.name + ".");
	}
	if (action.kind == AppActionKind::RenameDemoPreset)
	{
		const std::string targetId = presetId.empty() ? selectedDemoPresetId_ : presetId;
		const DemoPreset* source = findDemoPreset(targetId);
		if (!source) return sortinglogic::CommandResult(false, "The selected demo preset is unavailable.");
		DemoPreset candidate = demoEditorDraft_;
		candidate.name = payload.presetName.empty() ? action.text : payload.presetName;
		if (source->prefab)
		{
			// Renaming an edited prefab creates a user copy of the live draft. The
			// stored prefab is only the immutable source, not the editor's content.
			AppAction saveAction(AppActionKind::SaveDemoPreset);
			DemoActionPayload savePayload;
			savePayload.presetName = candidate.name;
			saveAction.demo.reset(new DemoActionPayload(savePayload));
			return dispatchDemoAction(saveAction);
		}
		const DemoPresetStore previous = demoPresetStore_;
		std::string error;
		candidate.id = targetId;
		candidate.prefab = false;
		if (!demoPresetStore_.saveUserPreset(candidate, &error))
			return sortinglogic::CommandResult(false, error);
		if (!persistDemoPresets(&error))
		{
			demoPresetStore_ = previous;
			return sortinglogic::CommandResult(false, error.empty() ? "Could not rename demo preset." : error);
		}
		demoEditorDraft_ = candidate;
		adoptDemoDraftBaseline();
		return sortinglogic::CommandResult(true, "Demo preset renamed.");
	}
	if (action.kind == AppActionKind::DeleteDemoPreset)
	{
		const std::string targetId = presetId.empty() ? selectedDemoPresetId_ : presetId;
		const DemoPreset* source = findDemoPreset(targetId);
		if (!source || source->prefab) return sortinglogic::CommandResult(false, "Built-in presets cannot be deleted.");
		const DemoPresetStore previous = demoPresetStore_;
		if (!demoPresetStore_.deleteUserPreset(targetId)) return sortinglogic::CommandResult(false, "The user preset was not found.");
		std::string error;
		if (!persistDemoPresets(&error))
		{
			demoPresetStore_ = previous;
			return sortinglogic::CommandResult(false, error.empty() ? "Could not delete demo preset." : error);
		}
		if (selectedDemoPresetId_ == targetId)
		{
			selectedDemoPresetId_ = "prefab.classic-five";
			demoEditorDraft_ = *demoPrefabPreset(selectedDemoPresetId_);
			adoptDemoDraftBaseline();
		}
		return sortinglogic::CommandResult(true, "Demo preset deleted.");
	}
	return sortinglogic::CommandResult(false, "The demo editor action is not supported.");
}

void AppController::update(float deltaSeconds, bool audioClockAvailable)
{
	sortinglogic::TraceBuildResult completed;
	if (traceBuildService_.poll(completed))
	{
		if (demoSession_ && completed.sessionId == demoSession_->identity &&
			completed.requestGeneration == demoSession_->requestGeneration)
			handleDemoBuildCompletion(std::move(completed));
		else if (!demoSession_ && completed.sessionId == manualSession_.identity &&
			completed.requestGeneration == manualSession_.requestGeneration)
		{
			manualSession_.simulation.completeTraceBuild(std::move(completed));
			manualSession_.playback.traceBuildCompleted(manualSession_.simulation.hasTrace());
			if (manualSession_.timingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
				synchronizeReferenceTransport(false);
		}
	}
	prepareRequestedDemoStep();
	startPendingPlaybackIfReady();
	const bool audioClockWasLost = activePlaybackCoordinator().observeAudioClockAvailability(audioClockAvailable);
	const bool wasPlaying = activeSimulation().isPlaying();
	if (activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence && activeSimulation().isPlaying())
	{
		if (!audioClockAvailable && !activePlaybackCoordinator().audioPreparationBlocksStart())
		{
			if (audioClockWasLost)
				activePlaybackCoordinator().setReferenceElapsedSeconds(referenceElapsedAtCursor(activeSimulation().cursor()));
			activePlaybackCoordinator().advanceReferenceClock(deltaSeconds);
			const double elapsed = activePlaybackCoordinator().referenceElapsedSeconds();
			const double duration = referenceDurationSeconds();
			const std::shared_ptr<const sortinglogic::SortTrace> trace = activeSimulation().currentTrace();
			std::size_t cursor = 0;
			if (trace && !trace->events.empty())
			{
				const std::vector<std::size_t>& eligible = trace->audioEventIndices;
				cursor = playbacktimebase::cursorForReferenceElapsed(eligible, trace->events.size(),
				elapsed, duration);
			}
			activeSimulation().synchronizePlaybackPosition(cursor, elapsed, duration, elapsed < duration);
		}
	}
	else if (!audioClockAvailable && !activePlaybackCoordinator().audioPreparationBlocksStart())
		activeSimulation().advance(deltaSeconds);
	if (demoRunner_.active())
	{
		const sortinglogic::RenderSnapshot playback = activeSimulation().snapshot();
		if (demoRunner_.state() == DemoRunnerState::Playing && !activeSimulation().isPlaying())
		{
			if (playback.complete) demoRunner_.playbackCompleted();
			else if (wasPlaying) demoRunner_.markPaused();
		}
		demoRunner_.update(deltaSeconds);
		prepareRequestedDemoStep();
		finishDemoIfNeeded();
	}
}

sortinglogic::RenderSnapshot AppController::snapshot() const
{
	sortinglogic::RenderSnapshot result;
	snapshotInto(result);
	return result;
}

void AppController::snapshotInto(sortinglogic::RenderSnapshot& result) const
{
	activeSimulation().snapshotInto(result);
	if (activePlaybackCoordinator().state(result.buildingTrace, result.hasTrace, result.playing,
		result.cursor, result.eventCount) == PlaybackCoordinatorState::Queued)
		result.status = result.buildingTrace
			? "Playback is queued and will start after trace generation."
			: "Playback is queued and will start after preparation finishes.";
	if (activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence && result.hasTrace)
	{
		result.durationSeconds = static_cast<float>(referenceDurationSeconds());
		result.elapsedSeconds = static_cast<float>((std::max)(0.0, (std::min)(referenceDurationSeconds(),
			static_cast<double>(result.elapsedSeconds))));
	}
	if (demoRunner_.active())
	{
		const DemoStep* step = demoRunner_.currentStep();
		const sortinglogic::AlgorithmDescriptor* algorithm = step
			? sortinglogic::algorithmDescriptorById(step->familyId, step->variantId) : nullptr;
		std::ostringstream demoStatus;
		demoStatus << "DEMO " << (demoRunner_.stepIndex() + 1) << '/' << demoRunner_.preset().steps.size();
		if (algorithm) demoStatus << " | " << algorithm->displayName;
		if (demoRunner_.hasEffectiveInput()) demoStatus << " | " << demoRunner_.effectiveInput().itemCount << " bars";
		if (activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			demoStatus << " | Reference cadence";
		else demoStatus << " | Fixed " << static_cast<int>(activeSimulation().durationSeconds()) << " sec";
		if (demoRunner_.state() == DemoRunnerState::Intermission)
			demoStatus << " | Intermission " << static_cast<int>(std::ceil(demoRunner_.intermissionRemaining())) << " sec";
		if (!demoRunner_.message().empty()) demoStatus << " | " << demoRunner_.message();
		for (std::size_t i = 0; i < demoRunner_.adjustments().size(); ++i)
			demoStatus << " | " << demoRunner_.adjustments()[i];
		result.status = demoStatus.str();
	}
	if (!demoRunner_.active() && !commandStatus_.empty()) result.status = commandStatus_;
}

void AppController::applyAudioPlayhead(const SoundEngine::AudioPlayheadFeedback& feedback,
	const AudioPlaybackRevision& currentRevision)
{
	if (!activePlaybackCoordinator().acceptsAudioPlayhead(activeSimulation().isPlaying(), feedback.generation,
		feedback.transportRevision, feedback.durationFrames, currentRevision))
		return;
	const double duration = activeTimingMode() == SoundEngine::PlaybackTimingMode::ReferenceCadence
		? playbacktimebase::secondsFromReferenceFrames(feedback.durationFrames)
		: activeSimulation().durationSeconds();
	const double elapsed = playbacktimebase::secondsFromReferenceFrames(feedback.timelineFrame);
	activeSimulation().synchronizePlaybackPosition(feedback.sourceCursor, elapsed, duration, feedback.playing);
	activePlaybackCoordinator().setReferenceElapsedSeconds(elapsed);
}

PlaybackCoordinatorState AppController::playbackState() const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = activeSimulation().currentTrace();
	return activePlaybackCoordinator().state(activeSimulation().isBuildingTrace(), static_cast<bool>(trace),
		activeSimulation().isPlaying(), activeSimulation().cursor(), trace ? trace->events.size() : 0);
}

double AppController::referenceDurationSeconds() const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = activeSimulation().currentTrace();
	if (!trace) return 0.0;
	return playbacktimebase::durationForReferenceEvents(trace->audioEventIndices.size());
}

double AppController::referenceElapsedAtCursor(std::size_t cursor) const
{
	const std::shared_ptr<const sortinglogic::SortTrace> trace = activeSimulation().currentTrace();
	if (!trace) return 0.0;
	return playbacktimebase::elapsedForReferenceCursor(trace->audioEventIndices, cursor);
}

void AppController::synchronizeReferenceTransport(bool wasPlaying)
{
	if (activeTimingMode() != SoundEngine::PlaybackTimingMode::ReferenceCadence) return;
	const std::size_t currentCursor = activeSimulation().cursor();
	const double duration = referenceDurationSeconds();
	if (!wasPlaying || !activeSimulation().isPlaying())
		activePlaybackCoordinator().setReferenceElapsedSeconds(referenceElapsedAtCursor(currentCursor));
	activeSimulation().synchronizePlaybackPosition(currentCursor, activePlaybackCoordinator().referenceElapsedSeconds(),
		duration, activeSimulation().isPlaying());
}

SoundEngine::Parameters AppController::effectiveAudio() const
{
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(activeSimulation().algorithm());
	return descriptor ? effectiveAudioParameters(preferences_, *descriptor) : preferences_.audio;
}

void AppController::synchronizeSimulationPreferences()
{
	preferences_.simulation.itemCount = static_cast<int>(activeSimulation().itemCount());
	preferences_.simulation.durationSeconds = activeSimulation().durationSeconds();
	preferences_.simulation.descending = activeSimulation().direction() == sortinglogic::SortDirection::Descending;
	preferences_.simulation.algorithm = static_cast<int>(activeSimulation().algorithm());
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(activeSimulation().algorithm());
	if (descriptor)
	{
		preferences_.simulation.familyId = descriptor->familyId;
		preferences_.simulation.variantId = descriptor->variantId;
	}
	preferences_.simulation.domain = activeSimulation().domain();
	preferences_.simulation.pattern = activeSimulation().pattern();
	preferences_.simulation.completionScanEnabled = activeSimulation().completionScanEnabled();
}

void AppController::persistPreferences()
{
	std::string error;
	if (!settingsPersistence_.save(preferences_, &error))
		persistenceStatus_ = "Could not save preferences" + (error.empty() ? std::string(".") : ": " + error);
	else
		persistenceStatus_ = "Preferences saved.";
}

bool AppController::persistSeedBookmarks(std::string* error)
{
	const bool saved = seedBookmarkPersistence_ && seedBookmarkPersistence_->save(seedBookmarks_.bookmarks(), error);
	if (!saved)
		persistenceStatus_ = "Could not save seed favorites" + (error && !error->empty() ? ": " + *error : ".");
	else persistenceStatus_ = "Seed favorites saved.";
	return saved;
}
