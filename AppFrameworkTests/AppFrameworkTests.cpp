#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <iostream>
#include <cmath>
#include <cstring>
#include <string>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <atomic>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../SDL2BarGraphics/LayoutMetrics.h"
#include "../SDL2BarGraphics/MainControlsView.h"
#include "../SDL2BarGraphics/TimelineView.h"
#include "../SDL2BarGraphics/InlineEditModel.h"
#include "../SDL2BarGraphics/ActiveRoleLookup.h"
#include "../SDL2BarGraphics/AlgorithmProfileView.h"
#include "../SDL2BarGraphics/AppearanceEditorLayout.h"
#include "../SDL2BarGraphics/AppearanceEditorModel.h"
#include "../SDL2BarGraphics/AppearanceEditorView.h"
#include "../SDL2BarGraphics/AudioSettingsActions.h"
#include "../SDL2BarGraphics/BarCountSteps.h"
#include "../SDL2BarGraphics/AlgorithmBrowserCatalog.h"
#include "../SDL2BarGraphics/AlgorithmBrowserModel.h"
#include "../SDL2BarGraphics/AlgorithmBrowserView.h"
#include "../SDL2BarGraphics/AppController.h"
#include "../SDL2BarGraphics/AudioPresentationBridge.h"
#include "../SDL2BarGraphics/PlaybackCoordinator.h"
#include "../SDL2BarGraphics/PlaybackTimebase.h"
#include "../SDL2BarGraphics/RenderPipeline.h"
#include "../SDL2BarGraphics/RenderingTypes.h"
#include "../SDL2BarGraphics/SettingsPanelView.h"
#include "../SDL2BarGraphics/SettingsPanelModel.h"
#include "../SDL2BarGraphics/SettingsPanelControls.h"
#include "../SDL2BarGraphics/SettingsPanelCatalog.h"
#include "../SDL2BarGraphics/SettingsPanelLayout.h"
#include "../SDL2BarGraphics/SettingsScroll.h"
#define SDL_MAIN_HANDLED
#include "../SDL2BarGraphics/SdlUiEventAdapter.h"
#include "../SDL2BarGraphics/TextBlockLayout.h"
#include "../SDL2BarGraphics/UiLayer.h"
#include "../SDL2BarGraphics/UiArchitecture.h"
#include "../SDL2BarGraphics/UiScreenModelAdapter.h"
#include "../SDL2BarGraphics/UiActionRouting.h"
#include "../SDL2BarGraphics/UserSettings.h"
#include "../SDL2BarGraphics/WindowsFontCatalog.h"

namespace
{
	int failures = 0;

	void check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	void testActiveRoleLookup()
	{
		using sortinglogic::ActiveRole;
		using sortinglogic::SemanticRole;
		const std::vector<ActiveRole> roles = {
			ActiveRole(SemanticRole::ComparedItem, 11, 0, "main"),
			ActiveRole(SemanticRole::Pivot, 11, 0, "main"),
			ActiveRole(SemanticRole::DiscardedItem, 11, 0, "main"),
			ActiveRole(SemanticRole::WriteTarget, 12, 2, "auxiliary"),
			ActiveRole(SemanticRole::AuxiliaryCopy, 13, 3, "auxiliary"),
			ActiveRole(SemanticRole::Gap, 11, 0, "main")
		};
		SemanticRole selected = SemanticRole::Gap;
		check(findDominantActiveRole(roles, 11, "main", selected) && selected == SemanticRole::Pivot,
			"dominant active-role lookup preserves priority and first-role tie behavior");
		check(findDominantActiveRole(roles, 12, "auxiliary", selected) && selected == SemanticRole::WriteTarget,
			"active-role lookup qualifies stable IDs by workspace lane");
		check(!findDominantActiveRole(roles, 12, "main", selected),
			"active-role lookup ignores matches from another lane");

		std::vector<ActiveRole> manyRoles;
		for (std::uint64_t itemId = 0; itemId < 20; ++itemId)
		{
			manyRoles.push_back(ActiveRole(SemanticRole::ComparedItem, itemId, static_cast<std::size_t>(itemId), "main"));
			manyRoles.push_back(ActiveRole(SemanticRole::WriteTarget, itemId, static_cast<std::size_t>(itemId), "auxiliary"));
		}
		manyRoles.push_back(ActiveRole(SemanticRole::ComparedItem, 7, 7, "main"));
		manyRoles.push_back(ActiveRole(SemanticRole::HeldKey, 7, 7, "main"));
		manyRoles.push_back(ActiveRole(SemanticRole::Pivot, 7, 7, "main"));
		manyRoles.push_back(ActiveRole(SemanticRole::DiscardedItem, 7, 7, "main"));
		ActiveRoleLookup lookup;
		lookup.rebuild(manyRoles);
		for (std::uint64_t itemId = 0; itemId < 22; ++itemId)
			for (const char* lane : { "main", "auxiliary", "missing" })
			{
				SemanticRole linearRole = SemanticRole::Gap;
				SemanticRole indexedRole = SemanticRole::Gap;
				const bool linearFound = findDominantActiveRole(manyRoles, itemId, lane, linearRole);
				const bool indexedFound = lookup.find(itemId, lane, indexedRole);
				check(linearFound == indexedFound && (!linearFound || linearRole == indexedRole),
					"indexed active-role lookup matches the linear reference across lanes and duplicate roles");
			}
		lookup.release();
	}

	void testInlineEditModel()
	{
		InlineEditModel editor;
		int integerValue = 99;
		float floatValue = 99.0f;
		double doubleValue = 99.0;
		check(!editor.active() && !editor.tryParseInteger(integerValue) && !editor.tryParseFloat(floatValue),
			"inline edit model starts inactive and refuses empty numeric input");

		editor.begin(InlineEditTarget::ItemCount, "80");
		check(editor.appendText("x") && editor.text() == "80",
			"item-count editing ignores invalid characters without consuming first-character replacement");
		editor.appendText("7");
		editor.appendText("1a2");
		check(editor.text() == "712" && editor.tryParseInteger(integerValue) && integerValue == 712,
			"item-count editing replaces the initial value on first accepted input and keeps ASCII digits");
		editor.begin(InlineEditTarget::ItemCount, "2147483648");
		check(!editor.tryParseInteger(integerValue) && integerValue == 712,
			"item-count parsing rejects integer overflow without changing its output argument");
		editor.begin(InlineEditTarget::ItemCount, "8");
		editor.appendText("-1.2");
		check(editor.text() == "12",
			"item-count editing rejects signs and decimal separators");

		editor.begin(InlineEditTarget::Duration, "10.5");
		editor.backspace();
		editor.appendText("3");
		check(editor.text() == "10.3" && editor.tryParseFloat(floatValue) && std::fabs(floatValue - 10.3f) < 0.0001f,
			"duration editing permits decimal input and parses a finite float");
		editor.begin(InlineEditTarget::Duration, "1");
		editor.appendText("2.3.4");
		check(editor.text() == "2.3.4" && !editor.tryParseFloat(floatValue),
			"duration editing preserves accepted text while rejecting an invalid multi-decimal commit");

		editor.begin(InlineEditTarget::AppearanceNumber, "0.5");
		check(editor.isAppearanceEdit(), "appearance scalar editing is identified independently from setup controls");
		editor.appendText("-1.25");
		check(editor.text() == "-1.25" && editor.tryParseDouble(doubleValue) && doubleValue == -1.25,
			"appearance scalar editing permits signed finite decimal values");
		editor.begin(InlineEditTarget::AppearanceAlpha, "1.000");
		check(editor.isAppearanceEdit() && editor.target() == InlineEditTarget::AppearanceAlpha,
			"appearance alpha uses the same text model with a distinct semantic target");

		editor.begin(InlineEditTarget::Duration, "1234567890");
		editor.backspace();
		editor.appendText("12");
		check(editor.text() == "1234567891",
			"inline editing retains the existing ten-character input limit");
		editor.clear();
		editor.appendText("4");
		check(editor.text() == "4", "clearing input disables replacement before the next character");
		editor.finish();
		check(!editor.active() && editor.text().empty() && !editor.tryParseDouble(doubleValue),
			"finishing an inline edit clears its target and input buffer");
	}

	void testSimulationPlaybackState()
	{
		sortinglogic::Simulation simulation(4, 0xA710u);
		sortinglogic::SimulationPlaybackState playback = simulation.playbackState();
		sortinglogic::RenderSnapshot render = simulation.snapshot();
		sortinglogic::RenderSnapshot reusable;
		reusable.activeRoles.push_back(sortinglogic::ActiveRole(sortinglogic::SemanticRole::Pivot, 99, 0));
		reusable.activeRegions.push_back(sortinglogic::RegionAnnotation());
		reusable.completionReason = "stale completion";
		reusable.compatibilityMessage = "stale compatibility";
		simulation.snapshotInto(reusable);
		check(!playback.hasTrace && !playback.complete && !playback.playing && playback.cursor == render.cursor &&
			playback.durationSeconds == render.durationSeconds && reusable.state.slots.size() == render.state.slots.size() &&
			reusable.activeRoles.empty() && reusable.activeRegions.empty() && reusable.completionReason.empty() &&
			reusable.compatibilityMessage.empty(),
			"compact playback state and reusable snapshot match before trace creation and clear stale vectors and strings");

		simulation.prepareInstaInvert();
		sortinglogic::TraceBuildRequest request;
		check(simulation.beginTraceBuild(request).accepted, "playback-state fixture starts a trace build");
		sortinglogic::TraceBuildResult result;
		result.succeeded = true;
		result.trace = sortinglogic::SortTraceBuilder::build(request);
		simulation.completeTraceBuild(std::move(result));
		playback = simulation.playbackState();
		render = simulation.snapshot();
		simulation.snapshotInto(reusable);
		check(playback.hasTrace && !playback.complete && !playback.playing && playback.cursor == render.cursor &&
			playback.durationSeconds == render.durationSeconds && reusable.hasTrace && reusable.state.slots.size() == render.state.slots.size() &&
			reusable.eventCount == render.eventCount && reusable.activeRoles.size() == render.activeRoles.size() &&
			reusable.activeRegions.size() == render.activeRegions.size(),
			"compact playback state and reusable snapshot match at the initial trace cursor");

		check(simulation.nextEvent().accepted, "playback-state fixture advances one event");
		playback = simulation.playbackState();
		render = simulation.snapshot();
		simulation.snapshotInto(reusable);
		check(playback.hasTrace == render.hasTrace && playback.complete == render.complete &&
			playback.playing == render.playing && playback.cursor == render.cursor &&
			playback.durationSeconds == render.durationSeconds && reusable.cursor == render.cursor &&
			reusable.activeRoles.size() == render.activeRoles.size() && reusable.activeRegions.size() == render.activeRegions.size(),
			"compact playback state and reusable snapshot track a stepped trace cursor");
		check(simulation.togglePlayback().accepted && simulation.playbackState().playing,
			"compact playback state reflects transport start");
		const std::size_t eventCount = simulation.currentTrace()->events.size();
		check(simulation.seekTo(eventCount).accepted, "playback-state fixture seeks to trace completion");
		playback = simulation.playbackState();
		render = simulation.snapshot();
		simulation.snapshotInto(reusable);
		check(playback.hasTrace == render.hasTrace && playback.complete == render.complete &&
			playback.playing == render.playing && playback.cursor == render.cursor &&
			playback.durationSeconds == render.durationSeconds && reusable.complete == render.complete &&
			reusable.cursor == render.cursor && reusable.activeRoles.size() == render.activeRoles.size(),
			"compact playback state and reusable snapshot match at trace completion");
		check(simulation.prepareShuffle(0xA711u).accepted, "playback-state fixture prepares a replacement input");
		simulation.snapshotInto(reusable);
		check(!reusable.hasTrace && !reusable.complete && reusable.cursor == 0 && reusable.activeRoles.empty() &&
			reusable.activeRegions.empty() && reusable.completionReason.empty(),
			"reused snapshot clears trace-only roles, regions, and completion information after replacement");
	}

	double medianSnapshotMicros(sortinglogic::Simulation& simulation, std::size_t cursor, std::size_t iterations,
		std::size_t& sink)
	{
		if (!simulation.seekTo(cursor).accepted) throw std::runtime_error("could not position simulation for snapshot profiling");
		for (std::size_t i = 0; i < 50; ++i)
		{
			const sortinglogic::RenderSnapshot warmup = simulation.snapshot();
			sink += warmup.state.slots.size() + warmup.activeRoles.size() + warmup.activeRegions.size();
		}
		std::vector<double> samples;
		for (std::size_t batch = 0; batch < 7; ++batch)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			for (std::size_t i = 0; i < iterations; ++i)
			{
				const sortinglogic::RenderSnapshot value = simulation.snapshot();
				sink += value.state.slots.size() + value.activeRoles.size() + value.activeRegions.size();
			}
			const double elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			samples.push_back(elapsed / static_cast<double>(iterations));
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	double medianReusedSnapshotMicros(sortinglogic::Simulation& simulation, std::size_t cursor,
		std::size_t iterations, std::size_t& sink)
	{
		if (!simulation.seekTo(cursor).accepted) throw std::runtime_error("could not position simulation for reused snapshot profiling");
		sortinglogic::RenderSnapshot reusable;
		for (std::size_t i = 0; i < 20; ++i)
		{
			simulation.snapshotInto(reusable);
			sink += reusable.state.slots.size() + reusable.activeRoles.size() + reusable.activeRegions.size();
		}
		std::vector<double> samples;
		for (std::size_t batch = 0; batch < 5; ++batch)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			for (std::size_t i = 0; i < iterations; ++i)
			{
				simulation.snapshotInto(reusable);
				sink += reusable.state.slots.size() + reusable.activeRoles.size() + reusable.activeRegions.size();
			}
			const double elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			samples.push_back(elapsed / static_cast<double>(iterations));
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	double medianRegionFilterMicros(const sortinglogic::SortTrace& trace, std::size_t cursor,
		std::size_t iterations, std::size_t& sink)
	{
		std::vector<sortinglogic::RegionAnnotation> activeRegions;
		std::vector<double> samples;
		for (std::size_t batch = 0; batch < 5; ++batch)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			for (std::size_t pass = 0; pass < iterations; ++pass)
			{
				activeRegions.clear();
				for (std::size_t i = 0; i < trace.annotations.size(); ++i)
					if (trace.annotations[i].beginCursor <= cursor && cursor < trace.annotations[i].endCursor)
						activeRegions.push_back(trace.annotations[i]);
				sink += activeRegions.size();
				if (!activeRegions.empty()) sink += activeRegions.front().first + activeRegions.front().last;
			}
			const double elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			samples.push_back(elapsed / static_cast<double>(iterations));
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	double medianRoleProjectionMillis(const sortinglogic::RenderSnapshot& snapshot, std::size_t passes,
		std::size_t& sink)
	{
		std::vector<std::uint64_t> itemIds;
		for (std::size_t i = 0; i < snapshot.state.slots.size(); ++i)
			if (snapshot.state.slots[i].occupied) itemIds.push_back(snapshot.state.slots[i].item.stableId);
		std::vector<double> samples;
		for (std::size_t batch = 0; batch < 5; ++batch)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			for (std::size_t pass = 0; pass < passes; ++pass)
				for (std::size_t i = 0; i < itemIds.size(); ++i)
				{
					sortinglogic::SemanticRole role;
					if (findDominantActiveRole(snapshot.activeRoles, itemIds[i], "main", role))
						sink += static_cast<std::size_t>(role) + 1;
				}
			const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			samples.push_back(elapsed / static_cast<double>(passes));
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	double medianIndexedRoleProjectionMillis(const sortinglogic::RenderSnapshot& snapshot, std::size_t passes,
		std::size_t& sink)
	{
		std::vector<std::uint64_t> itemIds;
		for (std::size_t i = 0; i < snapshot.state.slots.size(); ++i)
			if (snapshot.state.slots[i].occupied) itemIds.push_back(snapshot.state.slots[i].item.stableId);
		ActiveRoleLookup lookup;
		std::vector<double> samples;
		for (std::size_t batch = 0; batch < 5; ++batch)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			for (std::size_t pass = 0; pass < passes; ++pass)
			{
				lookup.rebuild(snapshot.activeRoles);
				for (std::size_t i = 0; i < itemIds.size(); ++i)
				{
					sortinglogic::SemanticRole role;
					if (lookup.find(itemIds[i], "main", role)) sink += static_cast<std::size_t>(role) + 1;
				}
				lookup.release();
			}
			const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			samples.push_back(elapsed / static_cast<double>(passes));
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	void runRenderCostProfile()
	{
		using namespace sortinglogic;
		Simulation simulation(800, 0xA61u);
		if (!simulation.selectAlgorithm(SortAlgorithm::Merge).accepted ||
			!simulation.setMergeWritebackMode(MergeWritebackMode::AtomicRangeWrite).accepted)
			throw std::runtime_error("could not configure Merge for the render-cost profile");
		TraceBuildRequest request;
		if (!simulation.beginTraceBuild(request).accepted) throw std::runtime_error("could not begin the profiling trace");
		TraceBuildResult completed;
		completed.succeeded = true;
		completed.trace = SortTraceBuilder::build(request);
		simulation.completeTraceBuild(std::move(completed));
		const std::shared_ptr<const SortTrace> trace = simulation.currentTrace();
		if (!trace) throw std::runtime_error("profiling trace was not published");

		std::size_t largestRangeCursor = 0;
		std::size_t largestRangeRoleCount = 0;
		std::size_t comparisonCursor = 0;
		for (std::size_t i = 0; i < trace->events.size(); ++i)
		{
			const SemanticEvent& event = trace->events[i];
			if (comparisonCursor == 0 && event.type == SemanticEventType::Compare) comparisonCursor = i + 1;
			if (event.type == SemanticEventType::WriteRange && event.afterSlots.size() > largestRangeRoleCount)
			{
				largestRangeRoleCount = event.afterSlots.size();
				largestRangeCursor = i + 1;
			}
		}
		if (largestRangeRoleCount == 0 || comparisonCursor == 0)
			throw std::runtime_error("Merge trace did not contain comparison and range-write profile points");

		std::size_t sink = 0;
		const double comparisonSnapshotMicros = medianSnapshotMicros(simulation, comparisonCursor, 1000, sink);
		const RenderSnapshot comparison = simulation.snapshot();
		const double rangeSnapshotMicros = medianSnapshotMicros(simulation, largestRangeCursor, 1000, sink);
		const RenderSnapshot range = simulation.snapshot();
		const double reusedRangeSnapshotMicros = medianReusedSnapshotMicros(simulation, largestRangeCursor, 500, sink);
		const double comparisonRegionFilterMicros = medianRegionFilterMicros(*trace, comparisonCursor, 1000, sink);
		const double rangeRegionFilterMicros = medianRegionFilterMicros(*trace, largestRangeCursor, 1000, sink);
		const double roleProjectionMillis = medianRoleProjectionMillis(range, 25, sink);
		const double indexedRoleProjectionMillis = medianIndexedRoleProjectionMillis(range, 25, sink);
		std::cout << std::fixed << std::setprecision(3)
			<< "render-cost-profile items=800 events=" << trace->events.size()
			<< " annotations=" << trace->annotations.size()
			<< " comparison_roles=" << comparison.activeRoles.size()
			<< " comparison_regions=" << comparison.activeRegions.size()
			<< " comparison_snapshot_median_us=" << comparisonSnapshotMicros
			<< " range_roles=" << range.activeRoles.size()
			<< " range_regions=" << range.activeRegions.size()
			<< " range_snapshot_median_us=" << rangeSnapshotMicros
			<< " range_reused_snapshot_median_us=" << reusedRangeSnapshotMicros
			<< " comparison_region_filter_median_us=" << comparisonRegionFilterMicros
			<< " range_region_filter_median_us=" << rangeRegionFilterMicros
			<< " role_scan_median_ms_per_800_items=" << roleProjectionMillis
			<< " role_index_median_ms_per_800_items=" << indexedRoleProjectionMillis
			<< " sink=" << sink << '\n';
	}

	void waitForAudioTimeline(AudioPresentationBridge& bridge, const sortinglogic::Simulation& simulation,
		const SoundEngine::Parameters& parameters, LatestAudioStateMailbox& mailbox,
		const SoundEngine::ReconstructedParameters& reconstructed = SoundEngine::ReconstructedParameters(),
		SoundEngine::PlaybackTimingMode mode = SoundEngine::PlaybackTimingMode::FixedDuration,
		bool audioDeviceAvailable = false)
	{
		for (unsigned int attempt = 0; attempt < 5000; ++attempt)
		{
			bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, audioDeviceAvailable);
			if (!bridge.timelinePreparing()) return;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	class MemorySettings : public ISettingsPersistence
	{
	public:
		MemorySettings() : value(), writes(0), failWrites(false) {}
		UserPreferences load() const override { return value; }
		bool save(const UserPreferences& settings, std::string* error = nullptr) const override
		{
			++writes;
			if (failWrites)
			{
				if (error) *error = "test persistence failure";
				return false;
			}
			value = settings;
			if (error) error->clear();
			return true;
		}
		mutable UserPreferences value;
		mutable int writes;
		mutable bool failWrites;
	};

	void writeFile(const std::wstring& path, const std::string& text)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			check(false, "create test settings file");
			return;
		}
		DWORD written = 0;
		const BOOL result = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
		check(result && written == text.size(), "write test settings file");
		CloseHandle(file);
	}

	void testSpacing()
	{
		const float slot80 = 968.0f / 80.0f;
		const BarSlotMetrics none = calculateBarSlotMetrics(slot80, BarSpacingMode::None);
		const BarSlotMetrics compact = calculateBarSlotMetrics(slot80, BarSpacingMode::Compact);
		const BarSlotMetrics comfortable = calculateBarSlotMetrics(slot80, BarSpacingMode::Comfortable);
		check(none.gutter == 0.0f && none.barWidth == slot80, "None spacing removes the visual gutter");
		check(compact.gutter == slot80 * 0.16f, "Compact preserves the existing adaptive gutter at 80 bars");
		check(comfortable.gutter > compact.gutter, "Comfortable is wider than Compact at 80 bars");

		const float slot800 = 968.0f / 800.0f;
		const BarSlotMetrics denseCompact = calculateBarSlotMetrics(slot800, BarSpacingMode::Compact);
		const BarSlotMetrics denseComfortable = calculateBarSlotMetrics(slot800, BarSpacingMode::Comfortable);
		check(denseComfortable.gutter > denseCompact.gutter, "Comfortable remains distinct at 800 bars");
		check(denseComfortable.barWidth > slot800 * 0.70f, "800-bar Comfortable mode keeps most of each slot visible");

		const ResponsivePanelLayout widePanel = calculatePanelLayout(1000, true);
		const ResponsivePanelLayout narrowPanel = calculatePanelLayout(640, true);
		const ResponsivePanelLayout collapsedPanel = calculatePanelLayout(1000, false);
		check(widePanel.wide && widePanel.rowCount == 2 && widePanel.chartTop == 184.0f, "wide panel uses two rows and leaves more chart space");
		check(!narrowPanel.wide && narrowPanel.rowCount == 4 && narrowPanel.chartTop > widePanel.chartTop, "narrow panel reflows into four rows");
		check(collapsedPanel.chartTop < widePanel.chartTop, "collapsed panel reclaims chart height");
		const LayoutCacheKey base(1000, 700, 2000, 1400, true);
		check(sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2000, 1400, true)),
			"layout cache stays valid when logical size, drawable size, and panel state are unchanged");
		check(!sameLayoutInputs(base, LayoutCacheKey(900, 700, 1800, 1400, true)) &&
			!sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2500, 1750, true)) &&
			!sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2000, 1400, false)),
			"layout cache invalidates on resize, DPI change, or persisted panel-state change");
		const int dpiScales[] = { 100, 150, 200 };
		for (std::size_t scale = 0; scale < sizeof(dpiScales) / sizeof(dpiScales[0]); ++scale)
		{
			const int drawableWidth = 1000 * dpiScales[scale] / 100;
			const int drawableHeight = 700 * dpiScales[scale] / 100;
			check(sameLayoutInputs(LayoutCacheKey(1000, 700, drawableWidth, drawableHeight, true),
				LayoutCacheKey(1000, 700, drawableWidth, drawableHeight, true)),
				"layout cache remains stable at synthetic 100%, 150%, and 200% DPI scales");
		}
	}

	void testPreferenceValueEquality()
	{
		const VisualPreferences visualDefaults;
		check(visualDefaults == VisualPreferences(), "visual preference defaults compare equal by value");
		VisualPreferences changedVisual = visualDefaults;
		changedVisual.spacing = BarSpacingMode::Comfortable;
		check(!(changedVisual == visualDefaults), "visual preference equality detects spacing changes");

		const SimulationPreferences simulationDefaults;
		check(simulationDefaults == SimulationPreferences(), "simulation preference defaults compare equal by value");
		SimulationPreferences changedSimulation = simulationDefaults;
		changedSimulation.pattern = sortinglogic::InputPattern::Sorted;
		check(!(changedSimulation == simulationDefaults), "simulation preference equality detects input pattern changes");

		const UserPreferences defaults;
		check(defaults == UserPreferences(), "user preference equality delegates to value groups");
		UserPreferences changedUser = defaults;
		changedUser.simulation = changedSimulation;
		check(!(changedUser == defaults), "user preference equality includes simulation value groups");
	}

	void testBarCountSteps()
	{
		check(adjustBarCount(2, true, false, 2, 800) == 3, "Bars + increments by one from the minimum");
		check(adjustBarCount(2, true, true, 2, 800) == 10, "Bars ++ snaps the minimum to the next decade");
		check(adjustBarCount(10, true, true, 2, 800) == 20, "Bars ++ advances from a clean decade");
		check(adjustBarCount(12, true, true, 2, 800) == 20, "Bars ++ advances to the next decade from an offset count");
		check(adjustBarCount(12, false, true, 2, 800) == 10, "Bars -- returns to the previous decade");
		check(adjustBarCount(10, false, true, 2, 800) == 2, "Bars -- respects the minimum below the first decade");
		check(adjustBarCount(3, false, false, 2, 800) == 2, "Bars - clamps at the minimum");
		check(adjustBarCount(799, true, true, 2, 800) == 800, "Bars ++ clamps at the maximum");
		check(adjustBarCount(800, true, false, 2, 800) == 800, "Bars + clamps at the maximum");
	}

	void testModifierStepSizes()
	{
		check(ui::stepDeltaWithModifiers(10.0f, false, false) == 10.0f &&
			ui::stepDeltaWithModifiers(10.0f, true, false) == 1.0f &&
			ui::stepDeltaWithModifiers(10.0f, false, true) == 100.0f &&
			ui::stepDeltaWithModifiers(10.0f, true, true) == 10.0f,
			"bar step uses 10 units by default, Ctrl divides by ten, and Shift multiplies by ten");
		check(ui::stepDeltaWithModifiers(1.0f, false, false) == 1.0f &&
			std::fabs(ui::stepDeltaWithModifiers(1.0f, true, false) - 0.1f) < 0.00001f &&
			ui::stepDeltaWithModifiers(1.0f, false, true) == 10.0f &&
			ui::stepDeltaWithModifiers(1.0f, true, true) == 1.0f,
			"duration step uses one second by default, Ctrl selects a tenth, and Shift selects ten seconds");

		UIRoot root;
		root.addButton("modifier", "Modifier");
		root.find("modifier")->setBounds(UiRect(0, 0, 80, 24));
		UiInputEvent down(UiInputType::PointerDown, 10, 10);
		down.control = true;
		UiAction action;
		root.handleEvent(down, action);
		root.handleEvent(UiInputEvent(UiInputType::PointerUp, 10, 10), action);
		check(action.event.kind == UiEventKind::Activated && action.event.control,
			"shared button activation retains Ctrl held across the pointer press and release");

		UiEvent bars;
		bars.controlId = "bars_up";
		bars.kind = UiEventKind::Activated;
		bars.control = true;
		check(ui::mainControlActivationAction(bars).kind == AppActionKind::AdjustItemCount &&
			ui::mainControlActivationAction(bars).value == 1.0f,
			"bar activation maps through the shared action router and Ctrl selects a single-item increment");
		bars.controlId = "bars_down";
		bars.control = false;
		bars.shift = true;
		check(ui::mainControlActivationAction(bars).value == -100.0f,
			"bar activation preserves the existing Shift decade decrement");
		UiEvent time;
		time.controlId = "time_down";
		time.kind = UiEventKind::Activated;
		time.control = true;
		check(ui::mainControlActivationAction(time).kind == AppActionKind::AdjustDuration &&
			std::fabs(ui::mainControlActivationAction(time).value + 0.1f) < 0.00001f,
			"duration activation maps through the shared router with Ctrl selecting a tenth-second decrement");
		time.controlId = "unknown";
		check(ui::mainControlActivationAction(time).kind == AppActionKind::None,
			"unrelated activated controls do not produce numeric step commands");
	}

	void testAppearanceAndPresetArchitecture()
	{
		const UserPreferences defaults;
		check(defaults.appearance.activePreset == UiPresetId::Current &&
			defaults.appearance.themes.find(UiPresetId::Current) != defaults.appearance.themes.end() &&
			defaults.appearance.themes.find(UiPresetId::FlatVgaDos) != defaults.appearance.themes.end() &&
			defaults.appearance.themes.find(UiPresetId::FlatVgaDos)->second.palette == PaletteId::VgaClassic &&
			defaults.appearance.themes.find(UiPresetId::ClassicWindows95) != defaults.appearance.themes.end() &&
			defaults.appearance.themes.find(UiPresetId::ClassicWindows95)->second.palette == PaletteId::Windows95,
			"appearance preferences register all built-in bases and start in Current");
		check(uiPresetDefinition(UiPresetId::Current).tokens.size() >= 33 &&
			uiPresetDefinition(UiPresetId::FlatVgaDos).tokens.size() > uiPresetDefinition(UiPresetId::Current).tokens.size(),
			"each preset declares its appearance schema and VGA-only decoration tokens");
		check(effectiveUiTokenValue(UiPresetId::ClassicWindows95,
			defaults.appearance.themes.find(UiPresetId::ClassicWindows95)->second, "layout.control_height").number == 40.0,
			"Classic Windows 95 starts with larger, reference-sized setup controls");
		check(!findUiToken(uiPresetDefinition(UiPresetId::Current), "scrollbar.thickness") &&
			findUiToken(uiPresetDefinition(UiPresetId::FlatVgaDos), "scrollbar.thickness") &&
			effectiveUiTokenValue(UiPresetId::Current, defaults.appearance.themes.find(UiPresetId::Current)->second,
				"layout.chart_inset").number == 0.0 &&
			effectiveUiTokenValue(UiPresetId::FlatVgaDos, defaults.appearance.themes.find(UiPresetId::FlatVgaDos)->second,
				"layout.chart_inset").number == 16.0,
			"preset schemas omit unsupported scrollbar controls and preserve each layout's factory chart inset");

		UiThemeCustomization current = defaultUiThemeCustomization(UiPresetId::Current, PaletteId::Amber);
		std::string error;
		check(setUiTokenValue(UiPresetId::Current, current, "color.bar", UiTokenValue::fromColor(UiColor(0.1f, 0.2f, 0.3f, 0.4f)), &error),
			"color token accepts a typed override");
		check(setUiTokenValue(UiPresetId::Current, current, "typography.body_size", UiTokenValue::fromNumber(19.0), &error),
			"typography token accepts a bounded numeric override");
		check(setUiTokenValue(UiPresetId::Current, current, "typeface.primary",
			UiTokenValue::fromFont(UiFontReference("Noto Sans 日本語", "Regular")), &error),
			"font token retains a Unicode family and face identity");
		selectUiPalette(current, PaletteId::Monochrome);
		check(current.palette == PaletteId::Monochrome && current.overrides.find("color.bar") == current.overrides.end() &&
			current.overrides.find("typography.body_size") != current.overrides.end() && current.overrides.find("typeface.primary") != current.overrides.end(),
			"palette selection clears color overrides while retaining type and metric overrides");
		std::map<std::string, UiColor> amberColors;
		amberColors["color.button"] = UiColor(0.27f, 0.18f, 0.09f, 1.0f);
		const UiControlPaintStyle amberButton = resolveUiControlPaintStyle(UiPresetId::Current,
			defaultUiThemeCustomization(UiPresetId::Current, PaletteId::Amber), "button", true, false, false, false, amberColors);
		check(std::fabs(amberButton.fill.r - 0.27f) < 0.0001f &&
			std::fabs(amberButton.fill.g - 0.18f) < 0.0001f,
			"semantic button painter respects the selected base palette when no color override exists");

		UiThemeCustomization vga = defaultUiThemeCustomization(UiPresetId::FlatVgaDos, PaletteId::Amber);
		check(setUiTokenValue(UiPresetId::FlatVgaDos, vga, "scrollbar.show_arrows", UiTokenValue::fromBoolean(false), &error),
			"VGA scrollbar decoration has a typed boolean override");
		check(setUiTokenValue(UiPresetId::FlatVgaDos, vga, "typeface.primary",
			UiTokenValue::fromFont(UiFontReference("Noto Sans 日本語", "Regular")), &error),
			"VGA font token accepts Unicode installed-font references");
		UiAppearanceProfile profile;
		const std::string encoded = serializeUiAppearanceProfile(UiPresetId::FlatVgaDos, vga);
		check(parseUiAppearanceProfile(encoded, profile, &error) && profile.preset == UiPresetId::FlatVgaDos &&
			profile.theme.palette == PaletteId::Amber && profile.theme.overrides.find("scrollbar.show_arrows") != profile.theme.overrides.end() &&
			profile.theme.overrides.find("typeface.primary") != profile.theme.overrides.end() &&
			profile.theme.overrides.find("typeface.primary")->second.font.family == "Noto Sans 日本語",
			"appearance profile round-trips its base identity, palette, and typed overrides");
		check(parseUiAppearanceProfile(std::string("\xEF\xBB\xBF") + encoded, profile, &error),
			"appearance profile parser accepts a UTF-8 byte-order mark");
		check(parseUiAppearanceProfile(encoded + "override.future.token=number:4\n", profile, &error) &&
			profile.ignoredTokens.size() == 1 && profile.ignoredTokens[0] == "future.token",
			"profile import reports unknown appearance tokens while preserving known values");
		UiAppearanceProfile unchangedProfile;
		unchangedProfile.preset = UiPresetId::Current;
		check(!parseUiAppearanceProfile("format=agss-ui\nversion=2\npreset=current\npalette=cyan\n", unchangedProfile, &error) &&
			unchangedProfile.preset == UiPresetId::Current,
			"unsupported profile versions fail without partially changing the output profile");
		check(!parseUiAppearanceProfile("format=agss-ui\nversion=1\npreset=unavailable\npalette=cyan\n", unchangedProfile, &error),
			"profile import rejects unavailable base presets");
		check(!parseUiAppearanceProfile("format=agss-ui\nversion=1\npreset=current\npalette=cyan\noverride.typography.body_size=number:999\n", unchangedProfile, &error),
			"profile import rejects invalid known values");
		const std::string windows95Profile = serializeUiAppearanceProfile(UiPresetId::ClassicWindows95,
			defaultUiThemeCustomization(UiPresetId::ClassicWindows95));
		check(parseUiAppearanceProfile(windows95Profile, profile, &error) && profile.preset == UiPresetId::ClassicWindows95 &&
			profile.theme.palette == PaletteId::Windows95,
			"Classic Windows 95 identity and factory palette survive portable profile round-trips");

		const char* const ids[] = { "panel_toggle", "settings", "menu_file", "menu_view", "menu_tools",
			"shuffle", "insta_sort", "insta_invert", "algorithm",
			"direction", "run", "play", "reset", "previous", "next", "bars_down", "bars_value", "bars_up",
			"time_down", "time_value", "time_up", "domain", "pattern", "seed_readout" };
		std::vector<std::string> controlIds(ids, ids + sizeof(ids) / sizeof(ids[0]));
		const int sizes[][2] = { { 640, 480 }, { 1000, 640 }, { 1920, 1080 } };
		for (int p = 0; p < 3; ++p)
		for (std::size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
		{
			UiLayoutInput input;
			input.width = static_cast<float>(sizes[s][0]);
			input.height = static_cast<float>(sizes[s][1]);
			input.expanded = true;
			const UiPresetId preset = p == 0 ? UiPresetId::Current : p == 1 ? UiPresetId::FlatVgaDos : UiPresetId::ClassicWindows95;
			const UiLayoutResult layout = calculateUiLayout(preset, input, controlIds);
			check(layout.chartViewport.width >= 0.0f && layout.chartViewport.height >= 0.0f &&
				layout.chartViewport.x >= 0.0f && layout.chartViewport.y >= 0.0f,
				"responsive layout keeps a nonnegative chart viewport");
			for (std::map<std::string, UiRect>::const_iterator item = layout.controlBounds.begin(); item != layout.controlBounds.end(); ++item)
				check(item->second.width >= 0.0f && item->second.height >= 0.0f,
					"responsive layout keeps every control bound nonnegative");
		}
		UiLayoutInput narrowVga;
		narrowVga.width = 640; narrowVga.height = 480; narrowVga.menuHeight = 26;
		const UiLayoutResult narrow = calculateUiLayout(UiPresetId::FlatVgaDos, narrowVga, controlIds);
		check(narrow.control("shuffle").y < narrow.chartViewport.y &&
			narrow.timeline.y >= narrow.chartViewport.y + narrow.chartViewport.height && narrow.chartViewport.height >= 120.0f &&
			narrow.transportArea.height <= 110.0f,
			"narrow VGA stacks setup and a compact footer around a usable chart at 640x480");
		check(narrow.control("algorithm").y < narrow.control("direction").y &&
			narrow.control("bars_value").y < narrow.chartViewport.y &&
			narrow.control("play").width > 100.0f && narrow.control("play").y > narrow.timeline.y,
			"narrow VGA keeps setup rows compact and gives transport buttons readable width below the seek rail");
		UiLayoutInput wideVga;
		wideVga.width = 1000; wideVga.height = 640; wideVga.menuHeight = 26;
		const UiLayoutResult wide = calculateUiLayout(UiPresetId::FlatVgaDos, wideVga, controlIds);
		check(wide.control("shuffle").x < wide.chartViewport.x && wide.timeline.y > wide.chartViewport.y &&
			wide.chartViewport.height >= 400.0f && wide.setupRail.height > 300.0f,
			"wide VGA composition keeps a compact setup rail beside a tall chart");
		check(wide.control("time_value").y < wide.setupRail.y + 230.0f &&
			wide.control("play").x < wide.timeline.x && wide.control("reset").x < wide.timeline.x &&
			wide.telemetry.x == wide.timeline.x && wide.telemetry.y > wide.timeline.y,
			"wide VGA groups transport buttons on the left and places seek and telemetry in the right footer");
		check(wide.control("seed_readout").y > wide.control("time_up").y,
			"wide VGA gives the seed readout its own row beneath the duration controls");
		check(wide.menuBar.y == 8.0f && wide.control("menu_file").y == wide.menuBar.y &&
			wide.control("menu_file").y + wide.control("menu_file").height <= wide.header.height &&
			wide.control("menu_file").x >= wide.control("panel_toggle").x + wide.control("panel_toggle").width,
			"VGA menus share the top header beside the setup toggle instead of consuming a separate row");
		const UiLayoutResult wideWindows95 = calculateUiLayout(UiPresetId::ClassicWindows95, wideVga, controlIds);
		check(wideWindows95.control("play").x < wideWindows95.control("next").x &&
			wideWindows95.control("next").x < wideWindows95.control("reset").x &&
			wideWindows95.control("previous").width == 0.0f &&
			wideWindows95.timelineLeftArrow.width > 0.0f && wideWindows95.timelineRightArrow.width > 0.0f,
			"wide Windows 95 transport presents Play, Step, and Reset while seek arrows retain previous/next access");
		UiLayoutInput classicFrame;
		classicFrame.width = 1000; classicFrame.height = 640; classicFrame.headerHeight = 0; classicFrame.menuHeight = 36;
		const UiLayoutResult classicFrameLayout = calculateUiLayout(UiPresetId::ClassicWindows95, classicFrame, controlIds);
		check(classicFrameLayout.menuBar.height == 36.0f && classicFrameLayout.control("shuffle").y >= 42.0f,
			"Windows 95 composition starts at the native caption and places the menu before setup controls");
		UiLayoutInput hdVgaInput;
		hdVgaInput.width = 1920; hdVgaInput.height = 1080; hdVgaInput.menuHeight = 26;
		const UiLayoutResult hdVga = calculateUiLayout(UiPresetId::FlatVgaDos, hdVgaInput, controlIds);
		check(hdVga.chartViewport.width >= 1400.0f && hdVga.chartViewport.height >= 800.0f &&
			hdVga.setupRail.height >= 800.0f && hdVga.telemetry.width >= 1200.0f,
			"1920x1080 VGA keeps the chart dominant and uses the bottom-right area for readable telemetry");
		check(hdVga.transportArea.height <= hdVgaInput.transportHeight &&
			hdVga.control("play").height == 58.0f && hdVga.control("reset").height == 58.0f &&
			hdVga.timeline.y + hdVga.timeline.height <= hdVga.transportArea.y + hdVga.transportArea.height &&
			hdVga.telemetry.y + hdVga.telemetry.height <= hdVga.transportArea.y + hdVga.transportArea.height,
			"large VGA windows keep the footer to its compact transport height and fit the seek rail and telemetry");
		UiLayoutInput constrainedVga;
		constrainedVga.width = 320; constrainedVga.height = 240; constrainedVga.margin = 48;
		constrainedVga.gap = 24; constrainedVga.controlHeight = 64; constrainedVga.railWidth = 440;
		constrainedVga.transportHeight = 240; constrainedVga.contentGap = 64;
		const UiLayoutResult constrained = calculateUiLayout(UiPresetId::FlatVgaDos, constrainedVga, controlIds);
		bool constrainedGeometryValid = constrained.chartViewport.height >= 0.0f &&
			constrained.timeline.y + constrained.timeline.height <= constrainedVga.height &&
			constrained.telemetry.y + constrained.telemetry.height <= constrainedVga.height;
		for (std::map<std::string, UiRect>::const_iterator item = constrained.controlBounds.begin();
			item != constrained.controlBounds.end(); ++item)
			constrainedGeometryValid = constrainedGeometryValid && item->second.x >= 0.0f && item->second.y >= 0.0f &&
				item->second.width >= 0.0f && item->second.height >= 0.0f &&
				item->second.x + item->second.width <= constrainedVga.width &&
				item->second.y + item->second.height <= constrainedVga.height;
		check(constrainedGeometryValid && constrained.chartViewport.height == 0.0f,
			"small VGA windows and extreme valid metrics collapse safely without negative or off-window controls");
		bool narrowControlsDoNotOverlap = true;
		for (std::map<std::string, UiRect>::const_iterator first = narrow.controlBounds.begin(); first != narrow.controlBounds.end(); ++first)
		{
			if (first->second.width <= 0.0f || first->second.height <= 0.0f) continue;
			for (std::map<std::string, UiRect>::const_iterator second = narrow.controlBounds.begin(); second != narrow.controlBounds.end(); ++second)
			{
				if (first == second || second->second.width <= 0.0f || second->second.height <= 0.0f) continue;
				const float overlapWidth = (std::min)(first->second.x + first->second.width, second->second.x + second->second.width) -
					(std::max)(first->second.x, second->second.x);
				const float overlapHeight = (std::min)(first->second.y + first->second.height, second->second.y + second->second.height) -
					(std::max)(first->second.y, second->second.y);
				if (overlapWidth > 0.0f && overlapHeight > 0.0f) narrowControlsDoNotOverlap = false;
			}
		}
		check(narrowControlsDoNotOverlap, "640x480 VGA hit regions do not overlap after reflow");
		UiLayoutInput tallerHeader;
		tallerHeader.width = 1000; tallerHeader.height = 640; tallerHeader.expanded = false; tallerHeader.headerHeight = 80;
		const UiLayoutResult collapsedCurrent = calculateUiLayout(UiPresetId::Current, tallerHeader, controlIds);
		check(collapsedCurrent.chartViewport.y >= tallerHeader.headerHeight + tallerHeader.gap,
			"collapsed Current layout keeps the chart below a customized header");

		const TextQuadLayout marqueeStart = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(300.0f, 20.0f), 18.0f, TextOverflowPolicy::Marquee, 0);
		const TextQuadLayout marqueeMidpoint = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(300.0f, 20.0f), 18.0f, TextOverflowPolicy::Marquee, 3207);
		const TextQuadLayout clampText = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(300.0f, 20.0f), 18.0f, TextOverflowPolicy::Clamp, 0);
		const TextQuadLayout fittedText = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(300.0f, 20.0f), 18.0f, TextOverflowPolicy::Fit, 0);
		const TextQuadLayout centerAlignedLabel = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(40.0f, 18.0f), 18.0f, TextOverflowPolicy::Marquee, 0);
		const TextQuadLayout leftAlignedLabel = layoutTextQuad(ClipBounds(10.0f, 20.0f, 80.0f, 24.0f),
			glm::vec2(40.0f, 18.0f), 18.0f, TextOverflowPolicy::Marquee, 0, ClipBounds(), TextHorizontalAlignment::Left);
		check(marqueeStart.visible && marqueeMidpoint.visible && marqueeStart.size.x > 80.0f &&
			marqueeStart.size.y == 18.0f && marqueeMidpoint.position.x < marqueeStart.position.x &&
			marqueeStart.clip.width == 80.0f,
			"shared marquee overflow keeps the requested font size, scrolls text, and clips to control bounds");
		check(clampText.visible && clampText.size.y == 18.0f && clampText.position.x == marqueeStart.position.x &&
			fittedText.visible && fittedText.size.x <= 72.0f && fittedText.size.y < clampText.size.y,
			"shared text layout lets skins choose full-size clamping or legacy fitting");
		check(centerAlignedLabel.visible && leftAlignedLabel.visible && centerAlignedLabel.position.x == 30.0f &&
			leftAlignedLabel.position.x == 14.0f,
			"shared text layout supports an explicit left alignment for short labels while preserving centered defaults");

		RecordingUiPaintContext recorder;
		recorder.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, UiRect(1, 2, 30, 20), UiColor(1, 0, 0, 1), "button"));
		check(recorder.commands().size() == 1 && recorder.commands()[0].bounds.width == 30.0f &&
			recorder.commands()[0].text == "button",
			"recording paint context captures deterministic geometry and semantic labels for headless checks");

		sortinglogic::RenderSnapshot snapshot;
		snapshot.itemCount = 80;
		snapshot.runCompatible = true;
		UserPreferences preferences;
		const UiScreenModel noTrace = buildUiScreenModel(snapshot, preferences, false);
		const UiScreenModel queued = buildUiScreenModel(snapshot, preferences, true);
		check(noTrace.transport == UiTransportState::NoTrace && noTrace.playCaption == "Generate & Play" &&
			queued.transport == UiTransportState::Queued && queued.playCaption == "Cancel queued" && !queued.setupEnabled,
			"presentation model centralizes transport captions and queued-play setup availability");
		check(noTrace.algorithmCapabilities.available && noTrace.algorithmCapabilities.safeMaximumItemCount > 0 &&
			!noTrace.algorithmCapabilities.timeComplexity.empty() && !noTrace.inputDomain.empty() && !noTrace.inputPattern.empty(),
			"presentation model carries algorithm compatibility and complexity capabilities for shared presets");
		check(traceProgressPercent(false, false, 0, 0) == 0 &&
			traceProgressPercent(true, false, 63, 100) == 63 &&
			traceProgressPercent(true, false, 120, 100) == 100 &&
			traceProgressPercent(true, true, 0, 0) == 100,
			"collapsed trace progress handles missing, partial, clamped, and completed empty traces");
		sortinglogic::RenderSnapshot mergeSnapshot;
		mergeSnapshot.algorithm = sortinglogic::SortAlgorithm::Merge;
		mergeSnapshot.mergeBufferVisualizationEnabled = false;
		mergeSnapshot.activeRoles.push_back(sortinglogic::ActiveRole(sortinglogic::SemanticRole::ComparedItem, 11, 0, "merge"));
		mergeSnapshot.activeRoles.push_back(sortinglogic::ActiveRole(sortinglogic::SemanticRole::WriteTarget, 12, 1, "main"));
		const std::vector<sortinglogic::ActiveRole> visibleMergeRoles = rolesVisibleOnMainArray(mergeSnapshot);
		check(visibleMergeRoles.size() == 2 && visibleMergeRoles[0].lane == "main" && visibleMergeRoles[1].lane == "main",
			"hidden Merge buffer keeps comparison and write roles visible on the primary bars");
		mergeSnapshot.mergeBufferVisualizationEnabled = true;
		check(rolesVisibleOnMainArray(mergeSnapshot)[0].lane == "merge",
			"visible Merge buffer keeps comparison roles attached to the auxiliary lane");
	}

	void testWindowsFontCatalog()
	{
		const WindowsFontCatalog catalog;
		WindowsFontFace courier;
		std::string error;
		check(catalog.resolve("Courier New", "Bold", courier, &error) && !courier.path.empty() &&
			!courier.pathUtf8.empty() && courier.collectionIndex >= 0,
			"DirectWrite resolves an installed Courier New face to a local font file and collection index");
		WindowsFontFace missing;
		check(!catalog.resolve("AGSS Font That Does Not Exist", "Regular", missing, &error) && !error.empty(),
			"DirectWrite reports missing installed family and face references for visible fallback handling");
	}

	void testTextBlockLayoutCache()
	{
		int measurementCount = 0;
		const TextBlockLayout::MeasureTextWidth measureWidth = [&measurementCount](const std::string& text,
			const ResolvedTextStyle& style)
		{
			++measurementCount;
			return static_cast<float>(text.size()) * style.rasterSize * 0.5f;
		};
		ResolvedTextStyle style(UiFontReference("Monospace", "Regular"), 10, 96, 10.0f);
		TextBlockLayout layout(measureWidth);
		check(layout.update("red blue green", 35.0f, style),
			"text block computes its first measured line layout");
		check(layout.lines().size() == 3 && layout.lines()[0] == "red" && layout.lines()[1] == "blue" &&
			layout.lines()[2] == "green", "text block wraps at measured logical width");
		const int initialMeasurements = measurementCount;
		check(!layout.update("red blue green", 35.0f, style) && measurementCount == initialMeasurements,
			"unchanged text and style reuse cached line breaks without measuring words again");
		check(layout.update("red blue green", 45.0f, style) && layout.lines().size() == 2 &&
			layout.lines()[0] == "red blue", "available width invalidates cached wrapping");
		style.rasterSize = 20;
		check(layout.update("red blue green", 45.0f, style) && layout.lines().size() == 3,
			"font raster size invalidates cached wrapping");
		style.dpi = 144;
		const int measurementsBeforeDpiChange = measurementCount;
		check(layout.update("red blue green", 45.0f, style),
			"DPI changes invalidate cached text layout");
		check(measurementCount > measurementsBeforeDpiChange,
			"DPI changes remeasure text using the resolved style");
		style.font.family = "Serif";
		check(layout.update("red blue green", 45.0f, style),
			"font family changes invalidate cached text layout");
		style.logicalHeight = 13.0f;
		check(layout.update("red blue green", 45.0f, style),
			"logical line height changes invalidate cached text layout");
		check(layout.update(std::string(), 45.0f, style) && layout.lines().size() == 1 &&
			layout.lines()[0].empty(), "empty text retains one empty line for stable layout");
	}

	void testRenderPipelineContracts()
	{
		const MaterialTable materials;
		const MaterialState solid = materials.stateFor(MaterialId::Bar);
		const MaterialState text = materials.stateFor(MaterialId::Text);
		const MaterialState dim = materials.stateFor(MaterialId::ModalDim);
		check(solid.blendMode == BlendMode::Opaque && solid.textureBinding == TextureBinding::None,
			"bar material has explicit opaque and texture-free state");
		check(text.blendMode == BlendMode::Alpha && text.textureBinding == TextureBinding::CommandTexture,
			"text material binds the submitting label texture with alpha blending");
		check(dim.blendMode == BlendMode::Alpha && dim.textureBinding == TextureBinding::None,
			"modal dim material uses alpha blending without a texture");
		MaterialTable extensibleMaterials;
		const MaterialId custom = static_cast<MaterialId>(2001);
		extensibleMaterials.registerMaterial(custom, MaterialState(BlendMode::Alpha, TextureBinding::CommandTexture));
		check(extensibleMaterials.stateFor(custom).blendMode == BlendMode::Alpha,
			"renderer material table accepts an additional material contract");
		const RenderBatchKey first(RenderLayer::MainControls, MaterialId::Button, 0);
		const RenderBatchKey same(RenderLayer::MainControls, MaterialId::Button, 0);
		const RenderBatchKey otherTexture(RenderLayer::MainControls, MaterialId::Button, 42);
		const RenderBatchKey otherLayer(RenderLayer::MainText, MaterialId::Button, 0);
		const RenderBatchKey otherClip(RenderLayer::MainControls, MaterialId::Button, 0, true, 0, 0, 20, 20);
		check(compatibleBatch(first, same), "adjacent commands with matching material state can batch");
		check(!compatibleBatch(first, otherTexture) && !compatibleBatch(first, otherLayer),
			"texture and painter layer changes break a batch");
		check(!compatibleBatch(first, otherClip), "clip state changes break a batch");
		check(static_cast<unsigned int>(RenderLayer::ChartBars) < static_cast<unsigned int>(RenderLayer::MainControls) &&
			static_cast<unsigned int>(RenderLayer::MainControls) < static_cast<unsigned int>(RenderLayer::ModalDim),
			"typed layers preserve chart, control, and modal painter order");
		check(painterLayerBefore(RenderLayer::ChartBars, RenderLayer::MainControls) &&
			!painterLayerBefore(RenderLayer::MainControls, RenderLayer::ChartBars),
			"renderer queue comparator orders chart before controls and controls before modal layers");
	}

	void testLayoutAndWidgets()
	{
		UiScrollViewport scroll;
		scroll.configure(UiRect(100.0f, 200.0f, 80.0f, 50.0f), 240.0f, 24.0f);
		check(!scroll.scrollWheel(20.0f, 20.0f, -1) && scroll.offset() == 0.0f,
			"a scroll viewport ignores wheel input outside its clipped bounds");
		check(scroll.scrollWheel(110.0f, 220.0f, -1) && scroll.offset() == 72.0f,
			"a shared scroll viewport advances three rows per wheel notch inside its bounds");
		check(scroll.itemAt(110.0f, 205.0f, 10, 24.0f) == 3,
			"scroll viewport hit testing accounts for the current content offset");
		scroll.ensureItemVisible(9, 24.0f);
		check(scroll.itemBounds(9, 24.0f).y >= scroll.viewport().y &&
			scroll.itemBounds(9, 24.0f).y + 24.0f <= scroll.viewport().y + scroll.viewport().height,
			"keyboard navigation can scroll a selected row fully into view");
		scroll.configure(UiRect(100.0f, 200.0f, 80.0f, 50.0f), 60.0f, 24.0f, 900.0f);
		check(scroll.offset() == 10.0f && scroll.visibleItemCount(3, 24.0f) == 3,
			"layout updates clamp stale offsets when content shrinks or the viewport changes");

		UIRoot orderedRoot;
		orderedRoot.addButton("first", "First");
		orderedRoot.addButton("second", "Second");
		orderedRoot.setFocusOrder(std::vector<std::string>{ "second", "first" });
		UiAction orderedAction;
		orderedRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), orderedAction);
		check(orderedRoot.find("second")->focused(), "keyboard focus follows the layout-provided control order");
		orderedRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), orderedAction);
		check(orderedRoot.find("first")->focused(), "layout focus order wraps after its final control");

		const float afterWheel = settingsScrollOffsetAfterWheel(0.0f, -1, 48.0f, 28.0f * 48.0f, 400.0f);
		check(afterWheel == 144.0f && settingsScrollOffsetAfterWheel(afterWheel, 1, 48.0f, 28.0f * 48.0f, 400.0f) == 0.0f,
			"audio settings scrolling advances and reverses by three rows per wheel notch");
		check(settingsScrollOffsetAfterWheel(5000.0f, -1, 48.0f, 28.0f * 48.0f, 400.0f) == 944.0f,
			"audio settings scrolling clamps at the content end");

		UIRoot root;
	root.addButton("action", "Action", AppAction(AppActionKind::Run));
	root.addComboBox("choice", std::vector<std::string>{ "One", "Two", "Three" }, 0,
		AppAction(AppActionKind::SelectAlgorithm));
		root.addContainer("panel", UiContainerKind::Panel);
		root.addContainer("row", UiContainerKind::Row, "panel");
		root.attachWidget("action", "row");
		root.attachWidget("choice", "row");
		root.layoutContainer("panel", UiRect(0.0f, 0.0f, 240.0f, 60.0f));
		root.layoutContainer("row", UiRect(10.0f, 20.0f, 210.0f, 30.0f), 10.0f);
		check(root.find("action")->bounds().x == 10.0f, "row layout starts at the requested inset");
		check(root.find("action")->bounds().width == 100.0f, "row layout distributes width after gaps");
		check(root.find("choice")->bounds().x == 120.0f, "row layout places the next widget after its gap");

		UiAction action;
		const UiRect combo = root.find("choice")->bounds();
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action), "pointer opens a combo box");
		check(root.hasOpenCombo(), "combo box retains its expanded state");
		const UiRect secondOption = root.find("choice")->optionBounds(1);
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, secondOption.x + 5.0f, secondOption.y + 5.0f), action), "pointer selects a dropdown item");
		check(action.appAction.kind == AppActionKind::SelectAlgorithm && action.appAction.selection == 1,
			"dropdown emits a typed selected action");
		check(!root.hasOpenCombo(), "selection closes the dropdown");
		UIRoot widePopupRoot;
		widePopupRoot.addComboBox("menu", std::vector<std::string>{ "File", "A much longer menu command" }, 0,
			AppAction(AppActionKind::Run));
		widePopupRoot.find("menu")->setBounds(UiRect(10.0f, 10.0f, 60.0f, 24.0f));
		widePopupRoot.find("menu")->setPopupBounds(UiRect(10.0f, 34.0f, 190.0f, 48.0f));
		check(widePopupRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 15.0f, 15.0f), action),
			"wide-popup combo opens from its original compact trigger");
		const UiRect wideOption = widePopupRoot.find("menu")->optionBounds(1);
		check(wideOption.width == 190.0f && wideOption.x + wideOption.width > 70.0f &&
			widePopupRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 180.0f, wideOption.y + 4.0f), action) &&
			action.appAction.kind == AppActionKind::Run && action.event.selection == 1,
			"dropdown paints and hit-tests the same popup bounds even beyond its invoking menu width");
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action),
			"visible combo can be reopened after selection");
		root.setWidgetVisible("choice", true);
		check(root.hasOpenCombo(), "reapplying visible state preserves an open combo during layout refresh");
		root.closeCombo();

		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), action), "Tab moves keyboard focus");
		check(root.find("action")->focused(), "first widget receives keyboard focus");
		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action), "Enter activates a focused button");
		check(action.appAction.kind == AppActionKind::Run, "focused button emits a typed action");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), action);
		check(root.find("choice")->focused(), "Tab advances to the next widget");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action);
		check(root.hasOpenCombo(), "Enter opens a focused combo box");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Down), action);
		check(action.appAction.kind == AppActionKind::SelectAlgorithm && action.appAction.selection == 2,
			"arrow keys emit the selected typed action");
		root.setWidgetEnabled("choice", false);
		check(!root.find("choice")->expanded(), "disabling a combo closes it");
		root.setWidgetEnabled("choice", true);
		root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action);
		root.setWidgetEnabled("choice", false);
		check(!root.hasOpenCombo(), "disabling an open combo releases root pointer capture");
		root.setInputCaptured(true);
		root.setModalBounds(UiRect(100.0f, 100.0f, 120.0f, 80.0f), UiAction::ViewCommand::CloseSettings);
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, 500.0f, 500.0f), action), "modal root captures pointer input outside widgets");
		check(action.viewCommand == UiAction::ViewCommand::CloseSettings,
			"clicking outside the modal emits its typed dismissal action");
		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Escape), action), "Escape is captured by the modal root");
		check(action.viewCommand == UiAction::ViewCommand::CloseSettings,
			"Escape emits the modal dismissal action");
		UIRoot tabRoot;
		tabRoot.addButton("appearance", "Appearance", AppAction(), UiAction::ViewCommand::ShowAppearanceSettings);
		tabRoot.addButton("audio", "Audio", AppAction(), UiAction::ViewCommand::ShowAudioSettings);
		tabRoot.find("appearance")->setBounds(UiRect(0.0f, 0.0f, 80.0f, 24.0f));
		tabRoot.find("audio")->setBounds(UiRect(90.0f, 0.0f, 80.0f, 24.0f));
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 10.0f, 10.0f), action);
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 10.0f, 10.0f), action);
		check(action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings,
			"Appearance tab emits a typed view-selection command");
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 100.0f, 10.0f), action);
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 100.0f, 10.0f), action);
		check(action.viewCommand == UiAction::ViewCommand::ShowAudioSettings,
			"Audio tab emits a typed view-selection command");
		UIRoot capabilityRoot;
		capabilityRoot.addComboBox("capability", std::vector<std::string>{ "Float", "Integer only", "Float too" }, 0,
			AppAction(AppActionKind::SelectAlgorithm));
		capabilityRoot.find("capability")->setBounds(UiRect(10.0f, 10.0f, 120.0f, 24.0f));
		capabilityRoot.find("capability")->setOptionEnabled(1, false);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 15.0f, 15.0f), action);
		const UiRect disabledOption = capabilityRoot.find("capability")->optionBounds(1);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, disabledOption.x + 4.0f, disabledOption.y + 4.0f), action);
		check(capabilityRoot.find("capability")->selectedIndex() == 0,
			"combo box does not allow choosing an incompatible option");
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Down), action);
		check(capabilityRoot.find("capability")->selectedIndex() == 2,
			"keyboard navigation skips disabled options");

		UIRoot sliderRoot;
		sliderRoot.addSlider("gain", "Gain", 0.0f, 100.0f, 1.0f, 25.0f,
			AppAction(AppActionKind::SetAudioParameter, 0.0f, 0, AudioParameterId::MasterLevel));
		sliderRoot.find("gain")->setBounds(UiRect(100.0f, 200.0f, 200.0f, 40.0f));
		check(sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 200.0f, 220.0f), action),
			"slider captures pointer down");
		check(action.appAction.kind == AppActionKind::SetAudioParameter && action.appAction.value == 50.0f,
			"slider pointer down emits its normalized parameter value");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerMove, 260.0f, 500.0f), action);
		check(action.appAction.value == 80.0f, "captured slider continues tracking the pointer outside its row");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 260.0f, 500.0f), action);
		check(sliderRoot.find("gain")->value() == 80.0f, "pointer release ends slider capture without losing the value");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Right), action);
		check(sliderRoot.find("gain")->value() == 81.0f && action.appAction.value == 81.0f,
			"focused slider supports keyboard adjustment");

		std::vector<std::string> sixtyBands;
		for (int i = 0; i < 60; ++i) sixtyBands.push_back("band " + std::to_string(i));
		UIRoot bandRoot;
		bandRoot.addComboBox("band", sixtyBands, 59, AppAction(AppActionKind::SelectAudioBand));
		bandRoot.find("band")->setBounds(UiRect(20.0f, 100.0f, 240.0f, 24.0f));
		bandRoot.find("band")->setPopupAbove(true);
		const UiRect visibleBand = bandRoot.find("band")->optionBounds(55);
		check(visibleBand.y < 100.0f && bandRoot.find("band")->optionBounds(0).width == 0.0f,
			"long selectors expose a bounded visible option window near the current selection");
		bandRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 25.0f, 105.0f), action);
		bandRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, visibleBand.x + 4.0f, visibleBand.y + 4.0f), action);
		check(action.appAction.kind == AppActionKind::SelectAudioBand && action.appAction.selection == 55,
			"a high frequency band remains directly selectable in the bounded dropdown");

		UIRoot popupScrollRoot;
		std::vector<std::string> popupOptions;
		for (int i = 0; i < 60; ++i) popupOptions.push_back("option " + std::to_string(i));
		popupScrollRoot.addComboBox("long_popup", popupOptions, 0, AppAction(AppActionKind::SelectAudioBand));
		popupScrollRoot.find("long_popup")->setBounds(UiRect(20.0f, 20.0f, 180.0f, 20.0f));
		popupScrollRoot.find("long_popup")->setPopupBounds(UiRect(20.0f, 40.0f, 180.0f, 160.0f));
		popupScrollRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 25.0f, 25.0f), action);
		check(popupScrollRoot.handleEvent(UiInputEvent(UiInputType::Wheel, 40.0f, 50.0f, -1), action) &&
			popupScrollRoot.find("long_popup")->firstVisibleOption() == 3,
			"open long combo popups scroll with the shared viewport even when the pointer is outside the widget bounds");
		const UiRect scrolledOption = popupScrollRoot.find("long_popup")->optionBounds(3);
		popupScrollRoot.handleEvent(UiInputEvent(UiInputType::PointerDown,
			scrolledOption.x + 4.0f, scrolledOption.y + 4.0f), action);
		check(popupScrollRoot.find("long_popup")->selectedIndex() == 3,
			"scrolled popup rows remain clipped and hit-test to their corresponding option");
		popupScrollRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Enter), action);
		for (int i = 0; i < 12; ++i) popupScrollRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), action);
		check(popupScrollRoot.find("long_popup")->selectedIndex() == 15 &&
			popupScrollRoot.find("long_popup")->firstVisibleOption() <= 15 &&
			popupScrollRoot.find("long_popup")->firstVisibleOption() + popupScrollRoot.find("long_popup")->visibleOptionCount() > 15,
			"keyboard navigation keeps the focused combo selection visible after scrolling");

		UIRoot radioRoot;
		radioRoot.addComboBox("direction", std::vector<std::string>{ "Ascending", "Descending" }, 0,
			AppAction(AppActionKind::SetDirection));
		radioRoot.find("direction")->setPresentation(UiWidgetPresentation::RadioGroup);
		radioRoot.find("direction")->setBounds(UiRect(10, 10, 200, 30));
		radioRoot.find("direction")->setInteractiveSubregions(std::vector<UiRect>{ UiRect(10, 10, 100, 30), UiRect(110, 10, 100, 30) });
		radioRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 150, 20), action);
		check(radioRoot.find("direction")->selectedIndex() == 1 && action.event.kind == UiEventKind::SelectionChanged,
			"radio group pointer selection changes one exclusive value and emits a semantic event");
		radioRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Left), action);
		check(radioRoot.find("direction")->selectedIndex() == 0,
			"radio group arrow navigation changes the selection without opening a popup");

		UIRoot dosRoot;
		dosRoot.addSlider("dos", "DOS slider", 0, 10, 1, 5, AppAction(AppActionKind::SetDuration));
		dosRoot.find("dos")->setPresentation(UiWidgetPresentation::DosScrollbar);
		dosRoot.find("dos")->setScrollbarArrowSize(10);
		dosRoot.find("dos")->setBounds(UiRect(20, 20, 100, 30));
		dosRoot.find("dos")->setInteractiveSubregions(std::vector<UiRect>{ UiRect(20, 26, 10, 18), UiRect(30, 28, 80, 14), UiRect(110, 26, 10, 18) });
		dosRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 115, 30), action);
		check(dosRoot.find("dos")->value() == 6.0f,
			"DOS scrollbar right arrow is an independent interactive subregion");
		dosRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 70, 35), action);
		check(dosRoot.find("dos")->value() == 5.0f,
			"DOS scrollbar track maps pointer position onto its value range");

		UIRoot textRoot;
		textRoot.addButton("search", "Search");
		textRoot.find("search")->setBounds(UiRect(5, 5, 80, 24));
		textRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 10, 10), action);
		textRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 10, 10), action);
		check(textRoot.handleEvent(UiInputEvent(UiInputType::TextInput, std::string("token")), action) &&
			action.event.kind == UiEventKind::TextEntered && action.event.text == "token",
			"normalized text input is delivered to the focused shared control");
		check(textRoot.handleEvent(UiInputEvent(UiInputType::Wheel, 10, 10, -2), action) &&
			action.event.kind == UiEventKind::Wheel && action.event.value == -2.0f,
			"normalized wheel input identifies its target control");
		textRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 10, 10), action);
		check(textRoot.find("search")->pressed(), "pointer down enters the shared pressed state");
		textRoot.handleEvent(UiInputEvent(UiInputType::FocusLost), action);
		check(!textRoot.find("search")->pressed() && action.event.kind == UiEventKind::FocusLost,
			"focus loss releases pointer capture and emits a lifecycle event");
	}

	void testAlgorithmBrowserModel()
	{
		AlgorithmBrowserModel browser(algorithmBrowserCatalog());
		const std::vector<AlgorithmFamilyEntry>& catalog = browser.families();
		check(!catalog.empty(), "algorithm browser model exposes the registered family catalog");
		const std::vector<sortinglogic::AlgorithmDescriptor>& registry = sortinglogic::algorithmCatalog();
		std::size_t mappedVariants = 0;
		bool allVariantsMapped = true;
		for (std::size_t algorithm = 0; algorithm < registry.size(); ++algorithm)
		{
			bool found = false;
			for (std::size_t family = 0; family < catalog.size() && !found; ++family)
				if (catalog[family].id == registry[algorithm].familyId)
					for (std::size_t variant = 0; variant < catalog[family].variants.size(); ++variant)
						if (catalog[family].variants[variant].variantId == registry[algorithm].variantId &&
							catalog[family].variants[variant].displayName == registry[algorithm].displayName)
						{
							found = true;
							++mappedVariants;
							break;
						}
			if (!found) allVariantsMapped = false;
		}
		check(allVariantsMapped && mappedVariants == registry.size(),
			"the presentation catalog maps every registered variant by stable family and variant ID");
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		check(insertion != nullptr, "algorithm browser fixture resolves the registered insertion method");
		if (!insertion) return;

		browser.begin(insertion->familyId);
		check(browser.searchFocused && browser.selectedFamily == insertion->familyId,
			"opening the browser resets its search state and focuses the query field");
		check(browser.visibleFamilies().size() == catalog.size(),
			"an empty search displays every family in catalog order");
		const std::vector<const AlgorithmFamilyEntry*> matches = browser.visibleFamilies();
		const AlgorithmFamilyEntry* insertionFamily = nullptr;
		for (std::size_t i = 0; i < matches.size(); ++i)
			if (matches[i]->id == insertion->familyId) insertionFamily = matches[i];
		if (!insertionFamily)
		{
			check(false, "the insertion family is present in the browser catalog");
			return;
		}
		const std::vector<const AlgorithmBrowserVariant*> insertionVariants = browser.visibleVariants(insertionFamily);
		check(insertionFamily && !insertionVariants.empty() && insertionVariants[0]->variantId == insertion->variantId &&
			insertionVariants[0]->displayName == insertion->displayName,
			"the selected family exposes its registered variants in catalog order");

		browser.search = "insertion sort";
		const std::vector<const AlgorithmFamilyEntry*> lowercaseFamilies = browser.visibleFamilies();
		const std::vector<const AlgorithmBrowserVariant*> lowercaseVariants = browser.visibleVariants(insertionFamily);
		browser.search = "INSERTION SORT";
		const std::vector<const AlgorithmFamilyEntry*> filteredFamilies = browser.visibleFamilies();
		const AlgorithmFamilyEntry* filteredFamily = nullptr;
		for (std::size_t i = 0; i < filteredFamilies.size(); ++i)
			if (filteredFamilies[i]->id == insertion->familyId) filteredFamily = filteredFamilies[i];
		const std::vector<const AlgorithmBrowserVariant*> filteredVariants = browser.visibleVariants(filteredFamily);
		bool insertionFound = false;
		bool variantsRemainInCatalogOrder = true;
		std::size_t previousCatalogIndex = 0;
		for (std::size_t i = 0; i < filteredVariants.size(); ++i)
		{
			if (filteredVariants[i]->variantId == insertion->variantId) insertionFound = true;
			std::size_t catalogIndex = 0;
			while (catalogIndex < insertionFamily->variants.size() &&
				insertionFamily->variants[catalogIndex].variantId != filteredVariants[i]->variantId)
				++catalogIndex;
			if (catalogIndex == insertionFamily->variants.size() || (i > 0 && catalogIndex <= previousCatalogIndex))
				variantsRemainInCatalogOrder = false;
			previousCatalogIndex = catalogIndex;
		}
		bool equivalentCaseResults = lowercaseFamilies.size() == filteredFamilies.size() &&
			lowercaseVariants.size() == filteredVariants.size();
		for (std::size_t i = 0; equivalentCaseResults && i < filteredFamilies.size(); ++i)
			equivalentCaseResults = lowercaseFamilies[i] == filteredFamilies[i];
		for (std::size_t i = 0; equivalentCaseResults && i < filteredVariants.size(); ++i)
			equivalentCaseResults = lowercaseVariants[i] == filteredVariants[i];
		check(filteredFamily && insertionFound && variantsRemainInCatalogOrder && equivalentCaseResults,
			"search filters matching families and variants case-insensitively while preserving catalog order");

		browser.familyScroll = 4;
		browser.variantScroll = 3;
		browser.familySelection = 4;
		browser.variantSelection = 2;
		browser.appendSearch(" sort");
		check(browser.search == "INSERTION SORT sort" && browser.familyScroll == 0 && browser.variantScroll == 0 &&
			browser.familySelection == 0 && browser.variantSelection == 0,
			"typing into algorithm search resets filtered selection and scroll positions");
		browser.familySelection = 3;
		browser.variantSelection = 2;
		browser.familyScroll = 4;
		browser.variantScroll = 5;
		browser.eraseSearchCharacter();
		check(browser.search == "INSERTION SORT sor" && browser.familySelection == 3 && browser.variantSelection == 2 &&
			browser.familyScroll == 0 && browser.variantScroll == 0,
			"backspace resets filtered scroll positions while preserving the current selection");
		browser.begin(insertion->familyId);
		check(browser.search.empty() && browser.familyScroll == 0 && browser.variantScroll == 0 && !browser.variantPane,
			"reopening the browser starts a clean selection session");
		browser.reset();
		check(!browser.searchFocused && !browser.pendingSelection && !browser.variantPane,
			"resetting browser interaction clears modal search and pending-selection state");
	}

	void testUiOverlayState()
	{
		UiOverlayState overlays;
		check(overlays.active() == UiOverlayId::None && !overlays.modalOpen() && !overlays.settingsSurfaceOpen(),
			"overlay ownership starts closed without claiming modal input");
		check(overlays.openSettings() && overlays.is(UiOverlayId::Settings) && overlays.settingsSurfaceOpen(),
			"opening settings establishes a single settings owner");
		check(!overlays.openAlgorithmBrowser(),
			"the algorithm browser cannot take ownership while settings is modal");
		check(overlays.openAppearanceEditor() && overlays.is(UiOverlayId::AppearanceEditor) && overlays.settingsSurfaceOpen(),
			"the appearance editor replaces settings while retaining its return destination");
		check(!overlays.openSettings() && !overlays.openAlgorithmBrowser(),
			"other modal requests cannot displace the appearance editor");
		check(overlays.closeAppearanceEditor() && overlays.is(UiOverlayId::Settings),
			"closing the appearance editor restores the settings panel");
		check(overlays.closeSettings() && overlays.active() == UiOverlayId::None && !overlays.modalOpen(),
			"closing settings releases modal ownership");
		check(overlays.openAlgorithmBrowser() && overlays.is(UiOverlayId::AlgorithmBrowser),
			"the algorithm browser can own the modal after settings close");
		check(!overlays.openSettings(),
			"settings cannot open over the algorithm browser");
		check(overlays.openAppearanceEditor() && overlays.is(UiOverlayId::AppearanceEditor),
			"appearance editing can replace a browser request without retaining stale browser ownership");
		check(overlays.closeAppearanceEditor() && overlays.active() == UiOverlayId::None,
			"appearance editing entered outside settings returns to the main view");
	}

	void testMainMenuSelectionActions()
	{
		const UiAction build = ui::mainMenuSelectionAction("menu_file", 1);
		const UiAction generate = ui::mainMenuSelectionAction("menu_file", 2);
		const UiAction reset = ui::mainMenuSelectionAction("menu_file", 3);
		check(build.appAction.kind == AppActionKind::BuildTrace &&
			generate.appAction.kind == AppActionKind::GenerateAndPlay && reset.appAction.kind == AppActionKind::Reset,
			"File menu selections map to the existing trace, generate-play, and reset commands");

		const UiAction collapse = ui::mainMenuSelectionAction("menu_view", 1);
		const UiAction fullscreen = ui::mainMenuSelectionAction("menu_view", 2);
		check(collapse.appAction.kind == AppActionKind::TogglePanelExpanded &&
			fullscreen.appAction.kind == AppActionKind::ToggleBorderlessFullscreen,
			"View menu selections map to the existing panel and fullscreen commands");

		const UiAction browser = ui::mainMenuSelectionAction("menu_tools", 1);
		const UiAction settings = ui::mainMenuSelectionAction("menu_tools", 2);
		const UiAction appearance = ui::mainMenuSelectionAction("menu_tools", 3);
		check(browser.viewCommand == UiAction::ViewCommand::OpenAlgorithmMenu &&
			settings.viewCommand == UiAction::ViewCommand::OpenSettings &&
			appearance.viewCommand == UiAction::ViewCommand::OpenAppearanceEditor,
			"Tools menu selections map to typed view commands rather than string-dispatched actions");

		const UiAction invalidSelection = ui::mainMenuSelectionAction("menu_file", 99);
		const UiAction unknownMenu = ui::mainMenuSelectionAction("menu_other", 1);
		check(invalidSelection.appAction.kind == AppActionKind::None &&
			invalidSelection.viewCommand == UiAction::ViewCommand::None &&
			unknownMenu.appAction.kind == AppActionKind::None &&
			unknownMenu.viewCommand == UiAction::ViewCommand::None,
			"unknown menu IDs and invalid selection indices produce an empty command");
	}

	void testSettingsNavigationRouting()
	{
		const ui::SettingsNavigation appearance = ui::settingsNavigationFor(UiAction::ViewCommand::ShowAppearanceSettings);
		const ui::SettingsNavigation algorithm = ui::settingsNavigationFor(UiAction::ViewCommand::ShowAlgorithmSettings);
		const ui::SettingsNavigation audio = ui::settingsNavigationFor(UiAction::ViewCommand::ShowGeneralAudioSettings);
		const ui::SettingsNavigation legacyAudio = ui::settingsNavigationFor(UiAction::ViewCommand::ShowAudioSettings);
		check(appearance.kind == ui::SettingsNavigationKind::Page && appearance.page == SettingsPanelPage::Appearance &&
			algorithm.kind == ui::SettingsNavigationKind::Page && algorithm.page == SettingsPanelPage::Algorithm &&
			audio.kind == ui::SettingsNavigationKind::Page && audio.page == SettingsPanelPage::GeneralAudio &&
			legacyAudio.kind == ui::SettingsNavigationKind::Page && legacyAudio.page == SettingsPanelPage::GeneralAudio,
			"settings page commands resolve to their current page, including the legacy audio alias");

		const ui::SettingsNavigation basic = ui::settingsNavigationFor(UiAction::ViewCommand::ShowAudioBasic);
		const ui::SettingsNavigation advanced = ui::settingsNavigationFor(UiAction::ViewCommand::ShowAudioAdvanced);
		check(basic.kind == ui::SettingsNavigationKind::AudioGroup && basic.audioGroup == SettingsAudioGroup::Basic &&
			advanced.kind == ui::SettingsNavigationKind::AudioGroup && advanced.audioGroup == SettingsAudioGroup::Advanced,
			"audio group commands resolve to the existing Basic and Advanced settings groups");

		const ui::SettingsNavigation unrelated = ui::settingsNavigationFor(UiAction::ViewCommand::OpenSettings);
		check(unrelated.kind == ui::SettingsNavigationKind::None,
			"modal opening commands are not mistaken for settings navigation");
	}

	void testAudioSettingsActionAdapter()
	{
		SoundEngine::Parameters audio;
		AppAction master(AppActionKind::SetAudioParameter, 1.5f, 0, AudioParameterId::MasterLevel);
		audio = audioSettingsActions::apply(audio, master);
		check(audio.masterLevel == 1.0f,
			"audio settings action applies the master value and shared sound-engine bounds");

		AppAction invalidBand(AppActionKind::SetAudioParameter, 9.0f, 0, AudioParameterId::BandTrim,
			static_cast<int>(SoundEngine::BandCount));
		const SoundEngine::Parameters unchangedBand = audioSettingsActions::apply(audio, invalidBand);
		check(unchangedBand.bandTrimDb == audio.bandTrimDb,
			"audio settings action ignores an invalid per-band index");

		AppAction accent(AppActionKind::SetAudioParameter, 0.8f, 0, AudioParameterId::AccentGain,
			static_cast<int>(SoundEngine::AccentCategory::Distribution));
		const SoundEngine::Parameters accentChanged = audioSettingsActions::apply(audio, accent);
		check(accentChanged.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] == 0.5f,
			"audio settings action applies category gain through the shared 0.5 cap");
		const SoundEngine::Parameters mismatched = audioSettingsActions::apply(audio,
			AppAction(AppActionKind::ResetVariantAudioLevels));
		check(mismatched == audio,
			"audio settings adapter ignores app actions outside its declared command kind");

		SoundEngine::ReconstructedParameters reconstructed;
		reconstructed.minimumFrequencyHz = 500.0;
		reconstructed.maximumFrequencyHz = 1000.0;
		AppAction conflictingRange(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::MinimumFrequencyHz, 1000.0f);
		reconstructed = audioSettingsActions::apply(reconstructed, conflictingRange);
		check(reconstructed.minimumFrequencyHz == SoundEngine::ReconstructedParameters().minimumFrequencyHz &&
			reconstructed.maximumFrequencyHz == SoundEngine::ReconstructedParameters().maximumFrequencyHz,
			"reconstructed audio actions retain the prior fresh-validator default fallback for a conflicting frequency range");

		AppAction cutoff(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::LowPassCutoffHz, 24000.0f);
		reconstructed = audioSettingsActions::apply(reconstructed, cutoff);
		check(reconstructed.lowPassCutoffHz == 20000.0,
			"reconstructed audio action applies the existing cutoff maximum");
		check(audioSettingsActions::apply(reconstructed, AppAction(AppActionKind::RetryAudioDevice)) == reconstructed,
			"reconstructed audio settings adapter ignores unrelated app commands");
	}

	void testUiCatalogActionNormalization()
	{
		const sortinglogic::AlgorithmDescriptor* insertion =
			sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		const sortinglogic::AlgorithmDescriptor* shell =
			sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
		const sortinglogic::AlgorithmDescriptor* merge =
			sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		check(insertion && shell && merge && !shell->supportedOptions.empty() && !merge->supportedOptions.empty(),
			"catalog action fixtures resolve insertion and option-bearing Shell/Merge descriptors");
		if (!insertion || !shell || !merge || shell->supportedOptions.empty() || merge->supportedOptions.empty()) return;

		int insertionIndex = -1;
		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			if (&catalog[i] == insertion) insertionIndex = static_cast<int>(i);
		const AppAction numericSelection = ui::normalizeCatalogAction(
			AppAction(AppActionKind::SelectAlgorithm, 0.0f, insertionIndex));
		const AppAction stableSelection = ui::normalizeCatalogAction(
			AppAction(AppActionKind::SelectAlgorithm, insertion->familyId, insertion->variantId));
		const AppAction invalidSelection = ui::normalizeCatalogAction(
			AppAction(AppActionKind::SelectAlgorithm, 0.0f, static_cast<int>(catalog.size())));
		check(numericSelection.familyId == insertion->familyId && numericSelection.variantId == insertion->variantId &&
			stableSelection.familyId == insertion->familyId && stableSelection.variantId == insertion->variantId &&
			invalidSelection.kind == AppActionKind::None,
			"algorithm selection converts legacy catalog indices to stable IDs and rejects invalid selections");

		AppAction shellSelection(AppActionKind::SetShellGapSequence, 0.0f, 2);
		const AppAction normalizedShell = ui::normalizeCatalogAction(shellSelection);
		AppAction invalidShell(AppActionKind::SetShellGapSequence, 0.0f, 99);
		const AppAction fallbackShell = ui::normalizeCatalogAction(invalidShell);
		check(normalizedShell.selection == shell->supportedOptions[2].value &&
			fallbackShell.selection == static_cast<int>(sortinglogic::ShellGapSequence::Halving),
			"Shell option indices resolve through catalog values with the established safe fallback");

		const sortinglogic::AlgorithmOptionDescriptor& option = merge->supportedOptions[0];
		int groupIndex = 0;
		for (std::size_t i = 0; i < merge->supportedOptions.size(); ++i)
		{
			if (&merge->supportedOptions[i] == &option) break;
			if (merge->supportedOptions[i].groupId == option.groupId) ++groupIndex;
		}
		AppAction indexedOption(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId, option.groupId);
		indexedOption.selection = groupIndex;
		const AppAction normalizedOption = ui::normalizeCatalogAction(indexedOption);
		const AppAction stableOption(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			option.groupId, option.stableId, option.value + 1);
		const AppAction normalizedStableOption = ui::normalizeCatalogAction(stableOption);
		AppAction invalidOption(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId, option.groupId);
		invalidOption.selection = 999;
		check(normalizedOption.optionStableId == option.stableId && normalizedOption.selection == option.value &&
			normalizedStableOption.optionStableId == option.stableId && normalizedStableOption.selection == option.value &&
			ui::normalizeCatalogAction(invalidOption).kind == AppActionKind::None,
			"algorithm option indices and stable IDs resolve to catalog values and invalid options are rejected");
	}

	void testSdlUiEventAdapter()
	{
		SDL_Event event = {};
		UiInputEvent input(UiInputType::FocusLost);
		event.type = SDL_MOUSEMOTION;
		event.motion.x = 17;
		event.motion.y = 29;
		check(translateSdlUiEvent(event, input) && input.type == UiInputType::PointerMove &&
			input.x == 17.0f && input.y == 29.0f,
			"SDL motion translates into pointer movement with logical coordinates");

		event = SDL_Event();
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = SDL_BUTTON_RIGHT;
		check(!translateSdlUiEvent(event, input),
			"unsupported mouse buttons remain outside retained widget input");
		event.button.button = SDL_BUTTON_LEFT;
		event.button.x = 41;
		event.button.y = 53;
		check(translateSdlUiEvent(event, input) && input.type == UiInputType::PointerDown &&
			input.x == 41.0f && input.y == 53.0f,
			"left-button SDL input translates into pointer capture coordinates");

		event = SDL_Event();
		event.type = SDL_MOUSEWHEEL;
		event.wheel.y = -2;
		SdlUiEventContext wheelContext;
		wheelContext.pointerX = 63.0f;
		wheelContext.pointerY = 72.0f;
		check(translateSdlUiEvent(event, input, SdlUiKeyProfile::Controls, wheelContext) &&
			input.type == UiInputType::Wheel && input.wheelDelta == -2 && input.x == 63.0f && input.y == 72.0f,
			"SDL wheel translation preserves its signed delta and platform-supplied pointer position");

		event = SDL_Event();
		event.type = SDL_KEYDOWN;
		event.key.keysym.sym = SDLK_SPACE;
		event.key.keysym.mod = static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT);
		check(translateSdlUiEvent(event, input) && input.key == UiKey::Space && input.control && input.shift,
			"control-profile keyboard translation preserves supported keys and modifiers");
		event.key.keysym.sym = SDLK_BACKSPACE;
		check(!translateSdlUiEvent(event, input) &&
			translateSdlUiEvent(event, input, SdlUiKeyProfile::Search) && input.key == UiKey::Backspace,
			"search-profile translation enables backspace without changing control-profile keys");
		event.key.keysym.sym = SDLK_SPACE;
		check(!translateSdlUiEvent(event, input, SdlUiKeyProfile::Search),
			"search-profile translation retains the browser's prior unsupported-space behavior");

		event = SDL_Event();
		event.type = SDL_TEXTINPUT;
		std::memcpy(event.text.text, "quick", sizeof("quick"));
		check(translateSdlUiEvent(event, input) && input.type == UiInputType::TextInput && input.text == "quick",
			"SDL text input translates into the shared text event");

		event = SDL_Event();
		event.type = SDL_WINDOWEVENT;
		event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
		check(translateSdlUiEvent(event, input) && input.type == UiInputType::FocusLost,
			"window focus loss translates into a UI lifecycle event");
		event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		check(!translateSdlUiEvent(event, input),
			"unrelated SDL window events are not presented as widget input");
	}

	void testAppearanceEditorLayout()
	{
		const AppearanceEditorLayoutResult wide = calculateAppearanceEditorLayout(1920.0f, 1080.0f);
		check(wide.valid && wide.panelBounds.x == 520.0f && wide.panelBounds.y == 210.0f &&
			wide.panelBounds.width == 880.0f && wide.panelBounds.height == 660.0f,
			"appearance editor centers and caps its panel at the established wide-window size");
		const UiRect preset = wide.control("appearance_preset");
		const UiRect palette = wide.control("appearance_palette");
		const UiRect apply = wide.control("appearance_apply");
		check(preset.x == 538.0f && preset.y == 268.0f && preset.height == 30.0f &&
			palette.x > preset.x + preset.width && apply.y + apply.height <= wide.panelBounds.y + wide.panelBounds.height,
			"appearance controls retain their two-column positions and bottom actions stay inside the panel");
		check(wide.contentClip.x == wide.panelBounds.x + 1.0f && wide.contentClip.y == wide.panelBounds.y + 40.0f &&
			wide.colorSwatchBounds.y == 420.0f && wide.titleBounds.y == 224.0f &&
			wide.descriptionBounds.y == 380.0f && wide.statusBounds.height == 18.0f && wide.controls.size() == 25,
			"appearance title, description, content clip, status, and color preview share one resolved layout");

		const AppearanceEditorLayoutResult compact = calculateAppearanceEditorLayout(320.0f, 240.0f);
		check(compact.valid && compact.panelBounds.x == 12.0f && compact.panelBounds.y == 12.0f &&
			compact.panelBounds.width == 296.0f && compact.panelBounds.height == 216.0f && compact.scale < 1.0f,
			"appearance editor scales its centered layout to a compact window");
		const UiRect compactApply = compact.control("appearance_apply");
		check(compactApply.y + compactApply.height <= compact.panelBounds.y + compact.panelBounds.height + 0.001f,
			"compact appearance actions remain within the dialog bounds");
		check(!calculateAppearanceEditorLayout(0.0f, 480.0f).valid &&
			!calculateAppearanceEditorLayout(640.0f, 0.0f).valid,
			"appearance layout stays invalid until the window has positive logical dimensions");
	}

	void testAppearanceEditorViewComposition()
	{
		UiAppearancePreferences initial;
		const std::vector<std::string> presets = { "Current", "Flat VGA DOS", "Classic Windows 95" };
		const std::vector<std::string> palettes = { "Cyan", "Amber", "Monochrome", "Light", "VGA Classic", "Windows 95" };
		AppearanceEditorView view(presets, palettes, initial, 0);
		view.updateLayout(1280, 720);
		view.setDescription("Color | Interface and chart color token.");
		const UiRect expectedPreset = view.layoutResult().control("appearance_preset");
		const UiWidget* presetWidget = view.controls().find("appearance_preset");
		check(view.layoutResult().valid && view.controls().widgets().size() == 25 && presetWidget &&
			presetWidget->bounds().x == expectedPreset.x && presetWidget->bounds().y == expectedPreset.y,
			"appearance view owns its retained controls and applies one responsive layout to them");

		AppearanceEditorPalette palette;
		palette.panel = UiColor(0.1f, 0.12f, 0.14f, 1.0f);
		palette.border = UiColor(0.3f, 0.4f, 0.45f, 1.0f);
		palette.accent = UiColor(0.1f, 0.8f, 0.7f, 1.0f);
		palette.text = UiColor(0.9f, 0.95f, 1.0f, 1.0f);
		palette.mutedText = UiColor(0.65f, 0.7f, 0.75f, 1.0f);
		palette.overlayDim = UiColor(0.0f, 0.0f, 0.0f, 0.5f);
		RecordingUiPaintContext paint;
		view.compose(1280, 720, "Changes preview behind this editor.", false,
			UiTokenValue::fromColor(UiColor(0.2f, 0.4f, 0.6f, 1.0f)), palette, paint);
		const std::vector<UiPaintCommand>& commands = paint.commands();
		check(commands.size() == 11 && commands[0].layer == UiPaintLayer::Backdrop &&
			commands[0].bounds.width == 1280.0f && commands[0].bounds.height == 720.0f &&
			commands[1].layer == UiPaintLayer::Panel && commands[2].layer == UiPaintLayer::Accent,
			"appearance view submits the modal backdrop and panel chrome in painter order");
		check(commands.size() == 11 && commands[3].elementId == "#appearance_title" &&
			commands[4].layer == UiPaintLayer::SettingsControls && commands[4].hasClip &&
			commands[9].text == "Color | Interface and chart color token." &&
			commands[9].textOverflow == UiTextOverflow::Fit &&
			commands[10].elementId == "#appearance_status" && commands[10].color.r == palette.mutedText.r,
			"appearance title, clipped swatch, description, and status use the shared paint contract");
		paint.clear();
		view.compose(1280, 720, "Validation failed.", true,
			UiTokenValue::fromNumber(1.0), palette, paint);
		check(paint.commands().size() == 6 && paint.commands()[5].color.r == palette.accent.r,
			"appearance error state uses the accent color without requiring a color-token swatch");

		view.controls().setInputCaptured(true);
		UiAction outsideClick;
		const bool consumed = view.controls().handleEvent(UiInputEvent(UiInputType::PointerDown, 0.0f, 0.0f), outsideClick);
		check(consumed && outsideClick.viewCommand == UiAction::ViewCommand::CloseAppearanceEditor,
			"appearance view retains modal pointer capture and outside-click dismissal");
	}

	void testAppearanceEditorViewSynchronizesControls()
	{
		UiAppearancePreferences initial;
		AppearanceEditorView view(std::vector<std::string>{ "Current", "Flat VGA DOS", "Classic Windows 95" },
			std::vector<std::string>{ "Cyan", "Amber", "Monochrome", "Light" }, initial, 0);
		const AppearanceFontFamilyProvider fontFamilies = []()
		{
			return std::vector<std::string>{ "Courier New", "Consolas" };
		};
		const AppearanceFontStyleProvider fontStyles = [](const std::string& family)
		{
			return family == "Consolas" ? std::vector<std::string>{ "Regular", "Bold" } : std::vector<std::string>{ "Regular", "Bold", "Italic" };
		};
		view.updateLayout(1000, 700);
		view.synchronizeControls(fontFamilies, fontStyles);

		const UiWidget* search = view.controls().find("appearance_search");
		const UiWidget* tokens = view.controls().find("appearance_token");
		check(search && search->label() == "Search tokens" && tokens && !tokens->options().empty() &&
			tokens->selectedIndex() == view.session().model().selectedIndex(),
			"appearance view synchronizes search and filtered token choices from its model");
		check(view.layoutResult().valid && view.controls().find("appearance_preset")->bounds().x ==
			view.layoutResult().control("appearance_preset").x,
			"appearance control synchronization retains the view's responsive layout contract");

		const std::vector<UiPresetId>& presets = uiPresetCatalog();
		for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
		{
			const UiPresetId preset = presets[presetIndex];
			view.session().model().setPreset(preset);
			const UiPresetDefinition& definition = uiPresetDefinition(preset);
			for (int rawType = static_cast<int>(UiTokenType::Color); rawType <= static_cast<int>(UiTokenType::Font); ++rawType)
			{
				const UiTokenType type = static_cast<UiTokenType>(rawType);
				const UiTokenDescriptor* example = nullptr;
				for (std::size_t i = 0; i < definition.tokens.size(); ++i)
					if (definition.tokens[i].type == type) { example = &definition.tokens[i]; break; }
				if (!example) continue;

				view.session().model().setSearch(example->stableId);
				view.synchronizeControls(fontFamilies, fontStyles);
				check(view.session().model().selectedToken() == example->stableId &&
					view.description().find(example->stableId) != std::string::npos,
					"appearance view selects and describes the token matching its search");
				check(view.controls().find("appearance_value_slider")->visible() == (type == UiTokenType::Number) &&
					view.controls().find("appearance_color_r")->visible() == (type == UiTokenType::Color) &&
					view.controls().find("appearance_boolean")->visible() == (type == UiTokenType::Boolean) &&
					view.controls().find("appearance_choice")->visible() == (type == UiTokenType::Choice) &&
					view.controls().find("appearance_font_family")->visible() == (type == UiTokenType::Font),
					"appearance view shows only controls for the selected token's value type");

				if (type == UiTokenType::Font)
				{
					const UiWidget* family = view.controls().find("appearance_font_family");
					const UiWidget* style = view.controls().find("appearance_font_style");
					check(family && std::find(family->options().begin(), family->options().end(), "Courier New") != family->options().end() &&
						style && !style->options().empty() && style->selectedIndex() >= 0,
						"font choices are supplied by the host adapter and selected value is preserved");
				}
			}
		}

		view.session().model().setSearch("no matching token");
		view.synchronizeControls(fontFamilies, fontStyles);
		check(view.description() == "No appearance tokens match this search." &&
			!view.controls().find("appearance_token")->visible(),
			"appearance view reports an empty token search and hides its selector");
	}

	void testAppearanceEditorViewActions()
	{
		UiAppearancePreferences initial;
		AppearanceEditorView view(std::vector<std::string>{ "Current", "Flat VGA DOS", "Classic Windows 95" },
			std::vector<std::string>{ "Cyan", "Amber", "Monochrome", "Light", "VGA Classic", "Windows 95" }, initial, 0);
		const AppearanceFontFamilyProvider fontFamilies = []()
		{
			return std::vector<std::string>{ "Courier New", "Consolas" };
		};
		const AppearanceFontStyleProvider fontStyles = [](const std::string&)
		{
			return std::vector<std::string>{ "Regular", "Bold", "Italic" };
		};
		const auto tokenOfType = [](UiPresetId preset, UiTokenType type) -> const UiTokenDescriptor*
		{
			const std::vector<UiTokenDescriptor>& tokens = uiPresetDefinition(preset).tokens;
			for (std::size_t i = 0; i < tokens.size(); ++i)
				if (tokens[i].type == type) return &tokens[i];
			return nullptr;
		};

		UiAction action;
		action.event.controlId = "appearance_preset";
		action.event.selection = static_cast<int>(UiPresetId::FlatVgaDos);
		AppearanceEditorActionResult result = view.handleAction(action, fontStyles);
		check(result.handled && result.requiresSynchronization && view.session().model().preset() == UiPresetId::FlatVgaDos &&
			view.session().draft().activePreset == UiPresetId::FlatVgaDos,
			"appearance view handles preset selection and updates the draft's active preset");

		action = UiAction();
		action.event.controlId = "appearance_palette";
		action.event.selection = 2;
		result = view.handleAction(action, fontStyles);
		check(result.handled && result.requiresSynchronization &&
			view.session().draft().themes[UiPresetId::FlatVgaDos].palette == PaletteId::Monochrome,
			"appearance view routes palette selection into its draft theme");

		const UiTokenDescriptor* number = tokenOfType(UiPresetId::Current, UiTokenType::Number);
		check(number != nullptr, "appearance catalog provides a numeric token for value-edit coverage");
		if (number)
		{
			view.session().model().setPreset(UiPresetId::Current);
			view.session().model().setCategoryFilter(0);
			view.session().model().setSearch(number->stableId);
			view.synchronizeControls(fontFamilies, fontStyles);
			const UiTokenValue before = effectiveUiTokenValue(UiPresetId::Current,
				view.session().draft().themes[UiPresetId::Current], number->stableId);
			action = UiAction();
			action.event.controlId = "appearance_value_up";
			result = view.handleAction(action, fontStyles);
			const UiTokenValue increased = effectiveUiTokenValue(UiPresetId::Current,
				view.session().draft().themes[UiPresetId::Current], number->stableId);
			const double step = number->maximum - number->minimum > 100.0 ? 1.0 : 0.5;
			check(result.handled && result.requiresSynchronization &&
				increased.number == (std::min)(number->maximum, before.number + step),
				"appearance view applies the catalog-bounded numeric increment");

			action = UiAction();
			action.event.controlId = "appearance_reset_token";
			result = view.handleAction(action, fontStyles);
			check(result.handled && result.requiresSynchronization &&
				view.session().draft().themes[UiPresetId::Current].overrides.count(number->stableId) == 0,
				"appearance view resets the selected token override");

			view.session().model().setSearch("no token can match this text");
			view.session().error() = "keep existing feedback";
			action = UiAction();
			action.event.controlId = "appearance_reset_token";
			result = view.handleAction(action, fontStyles);
			check(result.handled && !result.requiresSynchronization &&
				view.session().error() == "keep existing feedback",
				"appearance reset remains a no-op when the current search has no selected token");

			view.session().model().setSearch(number->stableId);
			action = UiAction();
			action.event.controlId = "appearance_value";
			result = view.handleAction(action, fontStyles);
			check(result.effect == AppearanceEditorEffect::BeginNumericEdit,
				"appearance view asks the host to start numeric text entry");
		}

		const UiTokenDescriptor* color = tokenOfType(UiPresetId::Current, UiTokenType::Color);
		if (color)
		{
			view.session().model().setPreset(UiPresetId::Current);
			view.session().model().setSearch(color->stableId);
			view.synchronizeControls(fontFamilies, fontStyles);
			action = UiAction();
			action.event.controlId = "appearance_color_g";
			action.event.kind = UiEventKind::ValueChanged;
			action.event.value = 0.73f;
			result = view.handleAction(action, fontStyles);
			const UiTokenValue updated = effectiveUiTokenValue(UiPresetId::Current,
				view.session().draft().themes[UiPresetId::Current], color->stableId);
			check(result.requiresSynchronization && updated.color.g == 0.73f,
				"appearance view routes color-channel changes into the selected token");

			action = UiAction();
			action.event.controlId = "appearance_color_alpha";
			result = view.handleAction(action, fontStyles);
			check(result.effect == AppearanceEditorEffect::BeginAlphaEdit,
				"appearance view asks the host to start alpha text entry");
			action.event.controlId = "appearance_color";
			result = view.handleAction(action, fontStyles);
			check(result.effect == AppearanceEditorEffect::ChooseColor,
				"appearance view returns color-picker work to the native-dialog adapter");
		}

		const UiTokenDescriptor* boolean = tokenOfType(UiPresetId::FlatVgaDos, UiTokenType::Boolean);
		if (boolean)
		{
			view.session().model().setPreset(UiPresetId::FlatVgaDos);
			view.session().model().setSearch(boolean->stableId);
			view.synchronizeControls(fontFamilies, fontStyles);
			const bool before = effectiveUiTokenValue(UiPresetId::FlatVgaDos,
				view.session().draft().themes[UiPresetId::FlatVgaDos], boolean->stableId).boolean;
			action = UiAction();
			action.event.controlId = "appearance_boolean";
			result = view.handleAction(action, fontStyles);
			const bool after = effectiveUiTokenValue(UiPresetId::FlatVgaDos,
				view.session().draft().themes[UiPresetId::FlatVgaDos], boolean->stableId).boolean;
			check(result.requiresSynchronization && after != before,
				"appearance view toggles boolean appearance tokens");
		}

		const UiTokenDescriptor* font = tokenOfType(UiPresetId::Current, UiTokenType::Font);
		if (font)
		{
			view.session().model().setPreset(UiPresetId::Current);
			view.session().model().setSearch(font->stableId);
			view.synchronizeControls(fontFamilies, fontStyles);
			const UiWidget* family = view.controls().find("appearance_font_family");
			const std::vector<std::string>::const_iterator consolas = std::find(family->options().begin(), family->options().end(), "Consolas");
			if (consolas != family->options().end())
			{
				action = UiAction();
				action.event.controlId = "appearance_font_family";
				action.event.selection = static_cast<int>(consolas - family->options().begin());
				result = view.handleAction(action, fontStyles);
				const UiTokenValue updated = effectiveUiTokenValue(UiPresetId::Current,
					view.session().draft().themes[UiPresetId::Current], font->stableId);
				check(result.requiresSynchronization && updated.font.family == "Consolas" && updated.font.style == "Regular",
					"appearance view changes font family and selects the host-provided first face style");
			}
		}

		action = UiAction();
		action.event.controlId = "appearance_import";
		check(view.handleAction(action, fontStyles).effect == AppearanceEditorEffect::ImportProfile,
			"appearance view leaves profile file access to the host adapter");
		action.event.controlId = "appearance_export";
		check(view.handleAction(action, fontStyles).effect == AppearanceEditorEffect::ExportProfile,
			"appearance view leaves profile export to the host adapter");
		action.event.controlId = "appearance_cancel";
		check(view.handleAction(action, fontStyles).effect == AppearanceEditorEffect::Cancel,
			"appearance view returns Cancel as a host effect");
		action.event.controlId = "appearance_apply";
		check(view.handleAction(action, fontStyles).effect == AppearanceEditorEffect::Apply &&
			view.session().draft().activePreset == view.session().model().preset(),
			"appearance view prepares the active preset before returning Apply");
	}

	void testAlgorithmProfileView()
	{
		TextBlockLayout::MeasureTextWidth measure = [](const std::string& text, const ResolvedTextStyle& style)
		{
			return static_cast<float>(text.size()) * style.logicalHeight * 0.55f;
		};
		AlgorithmProfileView view(measure);
		UiAlgorithmCapabilities capabilities;
		capabilities.available = true;
		capabilities.stable = true;
		capabilities.inPlace = false;
		capabilities.supportsNormalizedFloat = true;
		capabilities.supportsIntegerKeys = true;
		capabilities.safeMaximumItemCount = 1024;
		capabilities.timeComplexity = "O(n log n)";
		capabilities.spaceComplexity = "O(n)";
		for (int i = 0; i < 80; ++i) capabilities.behavior += "Stable merge phases preserve item order and use a reversible auxiliary workspace. ";

		AlgorithmProfilePalette palette;
		palette.background = UiColor(0.05f, 0.06f, 0.07f, 1.0f);
		palette.panel = UiColor(0.1f, 0.12f, 0.14f, 1.0f);
		palette.border = UiColor(0.3f, 0.4f, 0.45f, 1.0f);
		palette.timelineRail = UiColor(0.2f, 0.25f, 0.28f, 1.0f);
		palette.accent = UiColor(0.1f, 0.8f, 0.7f, 1.0f);
		palette.text = UiColor(0.9f, 0.95f, 1.0f, 1.0f);
		palette.mutedText = UiColor(0.65f, 0.7f, 0.75f, 1.0f);
		const ResolvedTextStyle profileStyle(UiFontReference(), 13, 96, 13.0f);
		const ResolvedTextStyle complexityStyle(UiFontReference(), 11, 96, 11.0f);
		const UiRect rail(0.0f, 30.0f, 360.0f, 1000.0f);
		const UiRect seed(0.0f, 270.0f, 360.0f, 30.0f);
		RecordingUiPaintContext paint;
		view.compose("merge", capabilities, true, 1920, rail, seed, profileStyle, complexityStyle, palette, paint);
		check(view.visible() && view.maximumScrollOffset() > 0.0f && view.scrollTrackBounds().width > 0.0f,
			"algorithm profile composes a scroll track when descriptive text exceeds its clipped viewport");

		bool hasLeftClippedText = false;
		bool hasTitle = false;
		for (std::size_t i = 0; i < paint.commands().size(); ++i)
		{
			const UiPaintCommand& command = paint.commands()[i];
			if (command.elementId == "#rail_profile_title") hasTitle = command.layer == UiPaintLayer::MainText;
			if (command.elementId.find("#rail_profile_line_") == 0)
				hasLeftClippedText = hasLeftClippedText || (command.hasClip && command.textAlignment == UiTextAlignment::Left &&
					command.textOverflow == UiTextOverflow::Clamp);
		}
		check(hasTitle && hasLeftClippedText, "algorithm profile paint commands retain title layering and left-clipped paragraph text");

		const UiRect viewport = view.scrollViewportBounds();
		const UiRect track = view.scrollTrackBounds();
		const float initialOffset = view.scrollOffset();
		check(!view.handleWheel(viewport.x - 1.0f, viewport.y + 1.0f, -1) && view.scrollOffset() == initialOffset,
			"algorithm profile consumes wheel input only when the pointer is inside its text viewport");
		check(view.handleWheel(viewport.x + 1.0f, viewport.y + 1.0f, -2) && view.scrollOffset() > initialOffset,
			"algorithm profile wheel scrolling advances its own viewport");
		check(view.pointerDown(track.x + 1.0f, track.y + track.height - 1.0f),
			"algorithm profile scrollbar captures pointer input");
		const float lowerOffset = view.scrollOffset();
		check(view.pointerMove(track.y + 1.0f) && view.scrollOffset() < lowerOffset,
			"algorithm profile scrollbar drag maps pointer motion back to the text offset");
		check(view.pointerUp() && !view.pointerUp(), "algorithm profile releases scrollbar pointer capture exactly once");

		paint.clear();
		view.compose("other", capabilities, true, 1920, rail, seed, profileStyle, complexityStyle, palette, paint);
		check(view.scrollOffset() == 0.0f, "changing the selected algorithm resets the profile description to its beginning");
		view.beginFrame();
		view.compose("other", capabilities, false, 800, rail, seed, profileStyle, complexityStyle, palette, paint);
		check(!view.visible() && !view.handleWheel(viewport.x + 1.0f, viewport.y + 1.0f, -1),
			"algorithm profile stays hidden and ignores scroll input when its responsive layout is unavailable");
	}

	void testAppearanceEditorModel()
	{
		AppearanceEditorModel model;
		model.begin(UiPresetId::Current);
		check(!model.options().empty() && model.selectedToken() == model.options()[0].stableId,
			"appearance editor starts with the first token in catalog order selected");
		model.setSearch("APPLICATION BACKGROUND");
		check(model.options().size() == 1 && model.options()[0].stableId == "color.background" &&
			model.selectedToken() == "color.background",
			"appearance token search is case-insensitive across descriptions and preserves stable IDs");

		model.clearSearch();
		model.setCategoryFilter(static_cast<int>(UiTokenCategory::Typography) + 1);
		check(!model.options().empty(), "appearance category filter retains matching typography tokens");
		bool allTypography = true;
		for (std::size_t i = 0; i < model.options().size(); ++i)
		{
			const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(model.preset()), model.options()[i].stableId);
			allTypography = allTypography && descriptor && descriptor->category == UiTokenCategory::Typography;
		}
		check(allTypography, "appearance category filtering returns only tokens from the selected category");
		model.selectOption(static_cast<int>(model.options().size()) - 1);
		check(model.selectedIndex() == static_cast<int>(model.options().size()) - 1,
			"appearance token selection resolves back to its filtered-list index");

		model.setSearch("no matching token");
		check(model.options().empty() && model.selectedToken().empty() && model.selectedIndex() == -1,
			"empty appearance search clears stale selection and exposes no selected index");
		model.setSearch(std::string());
		check(!model.options().empty() && model.selectedToken() == model.options()[0].stableId,
			"clearing an unmatched search selects the first remaining token deterministically");
		model.setPreset(UiPresetId::FlatVgaDos);
		check(model.preset() == UiPresetId::FlatVgaDos && !model.options().empty(),
			"changing appearance preset refreshes the token catalog without losing the category filter");
		model.begin(UiPresetId::Current);
		check(model.search().empty(), "reopening appearance editing clears search while retaining its category preference");
		model.setCategoryFilter(0);
		model.setSearch(std::string(96, 'x'));
		model.appendSearch("y");
		check(model.search().size() == 96, "appearance search remains bounded to the existing 96-character limit");
		model.eraseSearchCharacter();
		check(model.search().size() == 95, "appearance search backspace removes one character");
		model.clearSearch();
		check(model.search().empty() && !model.options().empty(), "clearing appearance search restores the unfiltered catalog");
	}

	void testAppearanceEditorSession()
	{
		UiAppearancePreferences original;
		original.activePreset = UiPresetId::Current;
		AppearanceEditorSession session;
		session.begin(original);
		check(sameUiAppearancePreferences(session.draft(), original) &&
			session.model().preset() == original.activePreset && session.error().empty(),
			"appearance editor session begins with a clean draft copied from the current preferences");

		std::string validationError;
		const UiColor editedColor(0.12f, 0.34f, 0.56f, 1.0f);
		check(setUiTokenValue(UiPresetId::Current, session.draft().themes[UiPresetId::Current],
			"color.bar", UiTokenValue::fromColor(editedColor), &validationError),
			"appearance session accepts a valid draft token edit");
		session.draft().activePreset = UiPresetId::FlatVgaDos;
		session.error() = "temporary validation feedback";
		session.restore();
		check(sameUiAppearancePreferences(session.draft(), original) &&
			session.model().preset() == UiPresetId::Current && session.error().empty(),
			"cancel restores the session baseline, selected preset, and clean status");

		UiAppearancePreferences committed = original;
		committed.activePreset = UiPresetId::FlatVgaDos;
		check(setUiTokenValue(UiPresetId::FlatVgaDos, committed.themes[UiPresetId::FlatVgaDos],
			"scrollbar.show_arrows", UiTokenValue::fromBoolean(false), &validationError),
			"appearance session fixture accepts a committed preference edit");
		session.begin(original);
		session.error() = "stale error";
		session.accept(committed);
		session.draft().activePreset = UiPresetId::Current;
		session.error() = "later draft error";
		session.restore();
		check(sameUiAppearancePreferences(session.draft(), committed) &&
			session.model().preset() == UiPresetId::FlatVgaDos && session.error().empty(),
			"successful apply advances the session baseline used by a later restore");
	}

	void testAlgorithmBrowserInputAndLayout()
	{
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		if (!insertion) return;

		AlgorithmBrowserModel browser(algorithmBrowserCatalog());
		browser.begin(insertion->familyId);
		browser.layout(UiRect(540.0f, 444.0f, 80.0f, 30.0f), 640, 480);
		check(browser.bounds.x >= 0.0f && browser.bounds.y >= 0.0f &&
			browser.bounds.x + browser.bounds.width <= 640.0f && browser.bounds.y + browser.bounds.height <= 480.0f,
			"algorithm browser layout clamps the modal inside the logical window");
		check(browser.familyBounds.x >= browser.bounds.x && browser.variantBounds.x + browser.variantBounds.width <=
			browser.bounds.x + browser.bounds.width,
			"algorithm browser layout keeps both list panes inside the modal bounds");

		browser.prepare();
		check(browser.familyViewport.maximumOffset() > 0.0f,
			"large family catalogs expose a scrollable family viewport");
		const UiInputEvent wheel(UiInputType::Wheel, browser.familyBounds.x + 4.0f,
			browser.familyBounds.y + 4.0f, -1);
		browser.handleInput(wheel, true, insertion->familyId, insertion->variantId);
		check(browser.familyScroll > 0 && browser.familyViewport.offset() > 0.0f,
			"browser wheel input updates the family viewport scroll position");
		const std::vector<AlgorithmFamilyEntry>& allFamilies = browser.families();
		const AlgorithmFamilyEntry* largestFamily = allFamilies.empty() ? nullptr : &allFamilies[0];
		for (std::size_t i = 1; i < allFamilies.size(); ++i)
			if (!largestFamily || allFamilies[i].variants.size() > largestFamily->variants.size())
				largestFamily = &allFamilies[i];
		browser.layout(UiRect(540.0f, 194.0f, 80.0f, 30.0f), 640, 240);
		browser.selectedFamily = largestFamily ? largestFamily->id : std::string();
		browser.variantScroll = 0;
		browser.prepare();
		check(browser.variantViewport.maximumOffset() > 0.0f,
			"the largest registered family exposes a scrollable variant viewport");
		const UiInputEvent variantWheel(UiInputType::Wheel, browser.variantBounds.x + 4.0f,
			browser.variantBounds.y + 4.0f, -1);
		browser.handleInput(variantWheel, true, insertion->familyId, insertion->variantId);
		check(largestFamily && browser.variantScroll > 0 && browser.variantViewport.offset() > 0.0f,
			"browser wheel input updates the selected family variant viewport");

		AlgorithmBrowserModel keyboard(algorithmBrowserCatalog());
		keyboard.begin(insertion->familyId);
		keyboard.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
			insertion->familyId, insertion->variantId);
		check(keyboard.familySelection == 1 && !keyboard.searchFocused,
			"down arrow leaves search and moves keyboard focus to the next family");
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Tab), true,
			insertion->familyId, insertion->variantId);
		check(keyboard.searchFocused,
			"tab returns keyboard focus to the browser search field");
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Tab), true,
			insertion->familyId, insertion->variantId);
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Right), true,
			insertion->familyId, insertion->variantId);
		check(keyboard.variantPane,
			"right arrow opens the selected family variant pane");
		const std::vector<const AlgorithmFamilyEntry*> keyboardFamilies = keyboard.visibleFamilies();
		const AlgorithmFamilyEntry* keyboardFamily = keyboardFamilies[static_cast<std::size_t>(keyboard.familySelection)];
		const std::vector<const AlgorithmBrowserVariant*> keyboardVariants = keyboard.visibleVariants(keyboardFamily);
		if (keyboardVariants.size() > 1)
			keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
				insertion->familyId, insertion->variantId);
		const std::size_t selectedVariant = static_cast<std::size_t>(keyboard.variantSelection);
		const AlgorithmBrowserInputResult selected = keyboard.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Enter), true,
			insertion->familyId, insertion->variantId);
		check(selected.selectionRequested && keyboard.pendingSelection && selectedVariant < keyboardVariants.size() &&
			selected.familyId == keyboardFamily->id &&
			selected.variantId == keyboardVariants[selectedVariant]->variantId,
			"enter emits a typed selection for the keyboard-highlighted catalog variant");

		AlgorithmBrowserModel busy(algorithmBrowserCatalog());
		busy.begin(insertion->familyId);
		busy.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
			insertion->familyId, insertion->variantId);
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Right), true,
			insertion->familyId, insertion->variantId);
		const AlgorithmBrowserInputResult rejected = busy.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Enter), false,
			insertion->familyId, insertion->variantId);
		check(!rejected.selectionRequested && !busy.pendingSelection && !busy.status.empty(),
			"busy setup state rejects keyboard selection with visible feedback");
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Escape), true,
			insertion->familyId, insertion->variantId);
		check(!busy.variantPane,
			"escape backs out of the variant pane without closing the browser");
		const AlgorithmBrowserInputResult close = busy.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Escape), true,
			insertion->familyId, insertion->variantId);
		check(close.closeRequested,
			"escape from the family pane requests modal dismissal");

		AlgorithmBrowserModel pointer(algorithmBrowserCatalog());
		pointer.begin(insertion->familyId);
		pointer.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		pointer.handleInput(UiInputEvent(UiInputType::TextInput, std::string("quick")), true,
			insertion->familyId, insertion->variantId);
		pointer.prepare();
		const std::vector<const AlgorithmFamilyEntry*> quickFamilies = pointer.visibleFamilies();
		check(!quickFamilies.empty(),
			"text input filters the browser catalog before pointer selection");
		if (!quickFamilies.empty())
		{
			pointer.handleInput(UiInputEvent(UiInputType::PointerDown, pointer.familyBounds.x + 4.0f,
				pointer.familyBounds.y + 4.0f), true, insertion->familyId, insertion->variantId);
			const std::vector<const AlgorithmBrowserVariant*> quickVariants = pointer.visibleVariants(
				quickFamilies[0]);
			const UiRect firstVariant = pointer.variantViewport.itemBounds(0, 30.0f);
			const AlgorithmBrowserInputResult clicked = pointer.handleInput(UiInputEvent(UiInputType::PointerDown,
				firstVariant.x + 4.0f, firstVariant.y + 4.0f), true, insertion->familyId, insertion->variantId);
			check(clicked.selectionRequested && !quickVariants.empty() && clicked.familyId == quickFamilies[0]->id &&
				clicked.variantId == quickVariants[0]->variantId,
				"pointer selection emits the typed variant action through the same browser model");
		}

		AlgorithmBrowserModel outside(algorithmBrowserCatalog());
		outside.begin(insertion->familyId);
		outside.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		const AlgorithmBrowserInputResult dismissed = outside.handleInput(
			UiInputEvent(UiInputType::PointerDown, 799.0f, 599.0f), true,
			insertion->familyId, insertion->variantId);
		check(dismissed.closeRequested,
			"outside pointer input requests modal dismissal");
	}

	void testAlgorithmBrowserPaintComposition()
	{
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		check(insertion != nullptr, "browser paint fixture uses a registered algorithm");
		if (!insertion) return;
		AlgorithmBrowserModel browser(algorithmBrowserCatalog());
		browser.begin(insertion->familyId);
		browser.layout(UiRect(20.0f, 20.0f, 120.0f, 30.0f), 800, 600);
		browser.prepare();
		AlgorithmBrowserPalette palette;
		palette.background = UiColor(0.1f, 0.1f, 0.1f, 1.0f);
		palette.panel = UiColor(0.2f, 0.2f, 0.2f, 1.0f);
		palette.button = UiColor(0.3f, 0.3f, 0.3f, 1.0f);
		palette.disabledButton = UiColor(0.25f, 0.25f, 0.25f, 1.0f);
		palette.valueButton = UiColor(0.35f, 0.35f, 0.35f, 1.0f);
		palette.accent = UiColor(0.4f, 0.4f, 0.4f, 1.0f);
		palette.text = UiColor(0.9f, 0.9f, 0.9f, 1.0f);
		palette.mutedText = UiColor(0.6f, 0.6f, 0.6f, 1.0f);
		palette.overlayDim = UiColor(0.0f, 0.0f, 0.0f, 0.5f);

		AlgorithmBrowserView view;
		RecordingUiPaintContext recorder;
		view.compose(browser, palette, true, insertion->familyId, insertion->variantId, 800, 600, recorder);
		const std::vector<UiPaintCommand>& commands = recorder.commands();
		check(commands.size() > 8 && commands[0].primitive == UiPaintPrimitive::Rectangle &&
			commands[0].material == UiPaintMaterial::ModalDim && commands[0].layer == UiPaintLayer::Backdrop,
			"browser composition begins with the dimmed backdrop in painter order");
		bool hasSearchText = false;
		bool hasFamilyClip = false;
		bool hasVariantText = false;
		for (std::size_t i = 0; i < commands.size(); ++i)
		{
			if (commands[i].elementId == "#algorithm_search" && commands[i].text.find("Search algorithms:") == 0)
				hasSearchText = true;
			if (commands[i].primitive == UiPaintPrimitive::Rectangle && commands[i].material == UiPaintMaterial::Popup &&
				commands[i].hasClip && commands[i].clip.width == browser.familyBounds.width)
				hasFamilyClip = true;
			if (commands[i].primitive == UiPaintPrimitive::Text && commands[i].elementId.find("#menu_variant_") == 0)
				hasVariantText = true;
		}
		check(hasSearchText && hasFamilyClip && hasVariantText,
			"browser composition emits searchable text, clipped family rows, and registered variant labels");

		browser.search = "no-such-algorithm";
		browser.configureViewports();
		recorder.clear();
		view.compose(browser, palette, false, insertion->familyId, insertion->variantId, 800, 600, recorder);
		bool hasEmptyFamilyMessage = false;
		bool hasBusyStatus = false;
		for (std::size_t i = 0; i < recorder.commands().size(); ++i)
		{
			const UiPaintCommand& command = recorder.commands()[i];
			hasEmptyFamilyMessage = hasEmptyFamilyMessage || command.text == "No matching families";
			hasBusyStatus = hasBusyStatus || command.text == "Pause playback before changing algorithms.";
		}
		check(hasEmptyFamilyMessage && hasBusyStatus,
			"browser composition includes empty-search and busy-state feedback in the shared command stream");
	}

	void testSettingsPanelPaintComposition()
	{
		SettingsPanelChrome chrome;
		chrome.screenWidth = 1280;
		chrome.screenHeight = 720;
		chrome.panelBounds = UiRect(760.0f, 40.0f, 500.0f, 620.0f);
		chrome.contentBounds = UiRect(776.0f, 112.0f, 468.0f, 500.0f);
		chrome.footerBounds = UiRect(776.0f, 598.0f, 370.0f, 16.0f);
		chrome.title = "Settings | Appearance";
		chrome.footer = "Preferences saved.";
		SettingsPanelPalette palette;
		palette.panel = UiColor(0.1f, 0.1f, 0.1f, 1.0f);
		palette.accent = UiColor(0.2f, 0.6f, 0.8f, 1.0f);
		palette.text = UiColor(0.9f, 0.9f, 0.9f, 1.0f);
		palette.mutedText = UiColor(0.6f, 0.6f, 0.6f, 1.0f);
		palette.background = UiColor(0.05f, 0.05f, 0.05f, 1.0f);
		palette.overlayDim = UiColor(0.0f, 0.0f, 0.0f, 0.5f);

		SettingsPanelView view;
		RecordingUiPaintContext recorder;
		view.compose(chrome, palette, recorder);
		const std::vector<UiPaintCommand>& commands = recorder.commands();
		check(commands.size() == 6 && commands[0].layer == UiPaintLayer::Backdrop &&
			commands[0].material == UiPaintMaterial::ModalDim &&
			commands[0].bounds.width == 1280.0f && commands[0].bounds.height == 720.0f,
			"settings chrome starts with a full-window dim command");
		check(commands.size() > 4 && commands[1].bounds.x == chrome.panelBounds.x &&
			commands[1].layer == UiPaintLayer::Panel && commands[2].layer == UiPaintLayer::Accent &&
			commands[2].bounds.height == 2.0f && commands[4].bounds.y == chrome.contentBounds.y,
			"settings chrome keeps panel, accent strip, and content surface in painter order");
		check(commands.size() == 6 && commands[3].elementId == "#settings_title" &&
			commands[3].text == chrome.title && commands[3].layer == UiPaintLayer::SettingsText &&
			commands[5].elementId == "#settings_footer" && commands[5].text == chrome.footer &&
			commands[5].color.r == palette.mutedText.r,
			"settings title and muted footer are emitted through the shared text contract");

		chrome.footer.clear();
		recorder.clear();
		view.compose(chrome, palette, recorder);
		check(recorder.commands().size() == 5,
			"settings chrome omits an empty footer instead of emitting an invisible label");
	}

	void testSettingsPanelViewComposesControlPaintCommands()
	{
		UserPreferences preferences;
		SettingsPanelView view;
		view.initializeControls(preferences);
		const UiRect panel(680.0f, 40.0f, 500.0f, 650.0f);
		view.setPanelBounds(panel, true);
		view.layoutControls(settingsPanelControlIds(SettingsPanelPage::Appearance, SettingsAudioGroup::Basic, nullptr), 8.0f);

		SettingsPanelChrome chrome;
		chrome.screenWidth = 1280;
		chrome.screenHeight = 800;
		chrome.panelBounds = panel;
		chrome.contentBounds = view.contentBounds();
		SettingsPanelPalette palette;
		palette.panel = UiColor(0.1f, 0.1f, 0.1f, 1.0f);
		palette.accent = UiColor(0.2f, 0.6f, 0.8f, 1.0f);
		palette.button = UiColor(0.15f, 0.22f, 0.3f, 1.0f);
		palette.text = UiColor(0.9f, 0.9f, 0.9f, 1.0f);
		palette.mutedText = UiColor(0.6f, 0.6f, 0.6f, 1.0f);
		palette.background = UiColor(0.05f, 0.05f, 0.05f, 1.0f);
		palette.overlayDim = UiColor(0.0f, 0.0f, 0.0f, 0.5f);

		RecordingUiPaintContext recorder;
		view.compose(chrome, palette, recorder);
		std::vector<std::string> controlIds;
		bool selectedAppearanceTab = false;
		bool activeUnderline = false;
		bool paletteControlClipped = false;
		for (std::size_t i = 0; i < recorder.commands().size(); ++i)
		{
			const UiPaintCommand& command = recorder.commands()[i];
			if (command.primitive == UiPaintPrimitive::Control && !command.popup)
			{
				controlIds.push_back(command.elementId);
				if (command.elementId == "appearance_tab")
					selectedAppearanceTab = command.material == UiPaintMaterial::Accent;
				if (command.elementId == "palette")
					paletteControlClipped = command.hasClip && command.clip.y == chrome.contentBounds.y &&
						command.clip.height == chrome.contentBounds.height;
			}
			if (command.layer == UiPaintLayer::SettingsControlsOverlay && command.bounds.height == 2.0f)
				activeUnderline = true;
		}
		const std::vector<std::string> expectedAppearance{ "appearance_tab", "algorithm_tab", "audio_tab",
			"preset", "palette", "spacing", "appearance_customize", "settings_close" };
		check(controlIds == expectedAppearance && selectedAppearanceTab && activeUnderline && paletteControlClipped,
			"settings view composes visible controls in painter order with active-tab and content-clip metadata");

		UiAction opened;
		const UiRect paletteBounds = view.controls().find("palette")->bounds();
		view.controls().handleEvent(UiInputEvent(UiInputType::PointerDown,
			paletteBounds.x + paletteBounds.width * 0.5f, paletteBounds.y + paletteBounds.height * 0.5f), opened);
		recorder.clear();
		view.compose(chrome, palette, recorder);
		bool palettePopup = false;
		for (std::size_t i = 0; i < recorder.commands().size(); ++i)
			if (recorder.commands()[i].primitive == UiPaintPrimitive::Control &&
				recorder.commands()[i].elementId == "palette" && recorder.commands()[i].popup &&
				recorder.commands()[i].layer == UiPaintLayer::Content)
				palettePopup = true;
		check(view.controls().hasOpenCombo() && palettePopup,
			"settings view submits an open dropdown as a clipped popup paint command");

		view.handleNavigation(UiAction::ViewCommand::ShowGeneralAudioSettings);
		view.layoutControls(settingsPanelControlIds(SettingsPanelPage::GeneralAudio, SettingsAudioGroup::Basic, nullptr), 8.0f);
		chrome.contentBounds = view.contentBounds();
		recorder.clear();
		view.compose(chrome, palette, recorder);
		bool audioSubtabs = false;
		bool audioRetry = false;
		bool audioContent = false;
		bool appearanceHidden = true;
		for (std::size_t i = 0; i < recorder.commands().size(); ++i)
		{
			const UiPaintCommand& command = recorder.commands()[i];
			if (command.primitive != UiPaintPrimitive::Control) continue;
			audioSubtabs = audioSubtabs || command.elementId == "audio_basic_tab";
			audioRetry = audioRetry || command.elementId == "audio_retry";
			audioContent = audioContent || command.elementId == "recon_master";
			if (command.elementId == "preset") appearanceHidden = false;
		}
		check(audioSubtabs && audioRetry && audioContent && appearanceHidden,
			"settings view composes the active audio subpage and omits controls from other pages");
	}

	void testSettingsPanelModel()
	{
		SettingsPanelModel model;
		check(model.page() == SettingsPanelPage::Appearance && model.pageIndex() == 0 &&
			model.audioGroup() == SettingsAudioGroup::Basic && model.scrollOffset() == 0.0f &&
			model.selectedAudioBand() == 3,
			"settings panel starts with the existing appearance, basic-audio, and scroll defaults");
		model.scrollViewport().configure(UiRect(0.0f, 0.0f, 200.0f, 100.0f), 1000.0f, 48.0f);

		model.setScrollOffset(144.0f);
		model.showPage(SettingsPanelPage::Algorithm);
		check(model.page() == SettingsPanelPage::Algorithm && model.pageIndex() == 1 && model.scrollOffset() == 0.0f,
			"navigating to Algorithm uses a typed page and resets transient scroll");

		model.setScrollOffset(96.0f);
		model.showAudioGroup(SettingsAudioGroup::Advanced);
		check(model.page() == SettingsPanelPage::GeneralAudio && model.pageIndex() == 2 &&
			model.audioGroup() == SettingsAudioGroup::Advanced && model.scrollOffset() == 0.0f,
			"audio group selection enters General Audio and resets its scroll position");
		model.showPage(SettingsPanelPage::GeneralAudio);
		check(model.audioGroup() == SettingsAudioGroup::Advanced,
			"opening General Audio directly preserves its selected basic or advanced group");

		model.setScrollOffset(-2.0f);
		check(model.scrollOffset() == 0.0f, "settings scroll state clamps negative offsets");
		model.setScrollOffset(std::numeric_limits<float>::quiet_NaN());
		check(model.scrollOffset() == 0.0f, "settings scroll state rejects non-finite offsets");
		model.selectAudioBand(-4, 60);
		check(model.selectedAudioBand() == 0, "audio band selection clamps below its catalog");
		model.selectAudioBand(80, 60);
		check(model.selectedAudioBand() == 59, "audio band selection clamps above its catalog");
		model.selectAudioBand(7, 0);
		check(model.selectedAudioBand() == 0, "empty audio band catalogs resolve safely to index zero");
	}

	void testSettingsPanelViewOwnsNavigationAndScrolling()
	{
		SettingsPanelView view;
		check(view.page() == SettingsPanelPage::Appearance && !view.isScrollablePage(),
			"settings view starts on Appearance and keeps that short page non-scrollable");
		view.controls().addContainer("settings_panel", UiContainerKind::Panel);
		view.controls().addButton("appearance_tab", "Appearance");
		view.controls().addButton("algorithm_tab", "Algorithm");
		view.controls().addButton("audio_tab", "General Audio");
		view.controls().addButton("audio_basic_tab", "Basic");
		view.controls().addButton("audio_advanced_tab", "Advanced");
		view.controls().addButton("settings_close", "Done");
		view.controls().addButton("audio_retry", "Retry audio device");
		view.controls().addButton("view_owned_control", "Owned by settings view");
		view.controls().addButton("inactive_control", "Inactive control");
		check(view.controls().find("view_owned_control") != nullptr,
			"settings view owns the retained widget root used by the platform adapter");

		check(view.handleNavigation(UiAction::ViewCommand::ShowAlgorithmSettings) &&
			view.page() == SettingsPanelPage::Algorithm && view.audioGroup() == SettingsAudioGroup::Basic,
			"settings view applies algorithm-tab navigation and preserves audio subpage state");
		const UiRect panelBounds(500.0f, 20.0f, 400.0f, 500.0f);
		std::vector<std::string> longControlList;
		longControlList.push_back("view_owned_control");
		for (int row = 1; row < 20; ++row) longControlList.push_back(std::string("catalog_row_") + std::to_string(row));
		view.setPanelBounds(panelBounds, false);
		view.layoutControls(longControlList, 8.0f);
		check(view.contentBounds().y == panelBounds.y + 82.0f &&
			view.controls().find("appearance_tab")->visible() && view.controls().find("settings_close")->visible() &&
			!view.controls().find("audio_basic_tab")->visible() && !view.controls().find("inactive_control")->visible(),
			"settings view owns tab geometry, page visibility, and active-row placement");
		check(view.isScrollablePage() && view.handleScrollWheel(520.0f, 125.0f, -1) && view.scrollOffset() > 0.0f,
			"settings view owns wheel scrolling for long algorithm settings");
		const float offsetAfterScroll = view.scrollOffset();
		check(!view.handleScrollWheel(400.0f, 125.0f, -1) && view.scrollOffset() == offsetAfterScroll,
			"settings view does not scroll when the pointer is outside its viewport");

		check(view.handleNavigation(UiAction::ViewCommand::ShowAudioAdvanced) &&
			view.page() == SettingsPanelPage::GeneralAudio && view.audioGroup() == SettingsAudioGroup::Advanced &&
			view.scrollOffset() == 0.0f,
			"audio subpage navigation changes the view state and resets its transient scroll offset");
		view.layoutControls(longControlList, 8.0f);
		check(view.contentBounds().y == panelBounds.y + 112.0f &&
			view.controls().find("audio_basic_tab")->visible() && view.controls().find("audio_retry")->visible(),
			"General Audio layout places subpage tabs and retry controls in their reserved rows");
		check(view.handleNavigation(UiAction::ViewCommand::ShowGeneralAudioSettings) &&
			view.page() == SettingsPanelPage::GeneralAudio && view.audioGroup() == SettingsAudioGroup::Advanced,
			"direct General Audio navigation preserves the selected audio subpage");
		check(view.handleScrollWheel(520.0f, 150.0f, -1) && view.scrollOffset() > 0.0f,
			"settings view can scroll the selected long audio subpage");
		check(view.handleNavigation(UiAction::ViewCommand::ShowAppearanceSettings) &&
			view.page() == SettingsPanelPage::Appearance && view.scrollOffset() == 0.0f && !view.isScrollablePage(),
			"switching back to Appearance resets transient scrolling and page-specific input behavior");
		check(!view.handleNavigation(UiAction::ViewCommand::OpenSettings),
			"settings view leaves modal opening to the app-level overlay coordinator");
	}

	void testSettingsPanelViewInitializesItsControls()
	{
		UserPreferences preferences;
		preferences.visual.spacing = BarSpacingMode::Comfortable;
		preferences.appearance.themes[preferences.appearance.activePreset].palette = PaletteId::Amber;
		SettingsPanelView view;
		view.initializeControls(preferences);

		const UiWidget* palette = view.controls().find("palette");
		const UiWidget* spacing = view.controls().find("spacing");
		const UiWidget* inputDomain = view.controls().find("domain");
		const UiWidget* reconstructedMaster = view.controls().find("recon_master");
		const UiWidget* variantGain = view.controls().find("algorithm_audio_00");
		check(palette && palette->selectedIndex() == static_cast<int>(PaletteId::Amber),
			"settings view initializes the palette selection from the active appearance preset");
		bool paletteLabelsMatchCatalog = palette && palette->options().size() == paletteCatalog().size();
		for (std::size_t i = 0; paletteLabelsMatchCatalog && i < paletteCatalog().size(); ++i)
			paletteLabelsMatchCatalog = palette->options()[i] == paletteDisplayName(paletteCatalog()[i]);
		check(paletteLabelsMatchCatalog,
			"settings palette labels and order come from the renderer-independent appearance catalog");
		check(spacing && spacing->selectedIndex() == static_cast<int>(BarSpacingMode::Comfortable),
			"settings view initializes visual preferences without platform adapter setup");
		check(inputDomain && inputDomain->action().kind == AppActionKind::SetInputDomain,
			"settings view builds simulation settings as typed app actions");
		check(reconstructedMaster && reconstructedMaster->action().kind == AppActionKind::SetReconstructedAudioParameter &&
			reconstructedMaster->action().reconstructedAudioParameter == ReconstructedAudioParameterId::MasterLevel,
			"settings view builds reconstructed-audio controls with their parameter identity");
		check(variantGain && variantGain->action().kind == AppActionKind::SetVariantAudioLevel &&
			variantGain->action().familyId == preferences.simulation.familyId &&
			variantGain->action().variantId == preferences.simulation.variantId,
			"settings view scopes variant audio controls to the currently selected stable algorithm identity");
		check(view.captions().find("playback_timing") != view.captions().end() &&
			view.controls().find("settings_close") && view.controls().findContainer("settings_panel"),
			"settings view constructs its captions, close action, and retained panel container");

		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		bool everyOptionWasBuilt = true;
		for (std::size_t algorithmIndex = 0; algorithmIndex < catalog.size(); ++algorithmIndex)
		{
			std::set<std::string> groups;
			for (std::size_t optionIndex = 0; optionIndex < catalog[algorithmIndex].supportedOptions.size(); ++optionIndex)
			{
				const std::string groupId = catalog[algorithmIndex].supportedOptions[optionIndex].groupId;
				if (!groups.insert(groupId).second) continue;
				const std::string id = settingsAlgorithmOptionControlId(catalog[algorithmIndex].familyId,
					catalog[algorithmIndex].variantId, groupId);
				if (!view.controls().find(id)) everyOptionWasBuilt = false;
			}
		}
		check(everyOptionWasBuilt,
			"settings view builds one option widget per catalog variant option group");
	}

	void testMainControlsViewInitializesTypedControls()
	{
		UserPreferences preferences;
		preferences.simulation.descending = true;
		preferences.simulation.domain = sortinglogic::InputDomain::SignedIntegerKeys;
		preferences.simulation.pattern = sortinglogic::InputPattern::NearlySorted;
		MainControlsView view;
		view.initializeControls(preferences, "Fixture Sort");

		const UIRoot& controls = view.controls();
		const UiWidget* algorithm = controls.find("algorithm");
		const UiWidget* direction = controls.find("direction");
		const UiWidget* domain = controls.find("domain");
		const UiWidget* pattern = controls.find("pattern");
		const UiWidget* shuffle = controls.find("shuffle");
		const UiWidget* run = controls.find("run");
		const UiWidget* fileMenu = controls.find("menu_file");
		check(algorithm && algorithm->label() == "Fixture Sort" && algorithm->viewCommand() == UiAction::ViewCommand::OpenAlgorithmMenu,
			"main controls view owns the algorithm menu item and its selected label");
		check(direction && direction->selectedIndex() == 1 && direction->action().kind == AppActionKind::SetDirection,
			"main controls view initializes direction state and typed command");
		check(domain && domain->selectedIndex() == static_cast<int>(preferences.simulation.domain) &&
			domain->action().kind == AppActionKind::SetInputDomain && pattern &&
			pattern->selectedIndex() == static_cast<int>(preferences.simulation.pattern) &&
			pattern->action().kind == AppActionKind::SetInputPattern,
			"main controls view initializes domain and pattern selectors from simulation preferences");
		check(shuffle && shuffle->action().kind == AppActionKind::Shuffle && run &&
			run->role() == UiControlRole::BuildTrace && fileMenu && !fileMenu->optionEnabled(0),
			"main controls view keeps action routing, semantic roles, and menu heading behavior in its tree");
	}

	void testMainControlsViewSynchronizesTextPresentation()
	{
		UserPreferences preferences;
		MainControlsView view;
		sortinglogic::RenderSnapshot snapshot;
		UiScreenModel screen;
		snapshot.algorithm = sortinglogic::SortAlgorithm::Insertion;
		snapshot.durationSeconds = 10.0f;
		snapshot.status = "Ready.";
		screen.algorithm = "Insertion Sort";
		screen.direction = "Ascending";
		screen.itemCount = 80;
		screen.inputDomain = "Normalized floats";
		screen.inputPattern = "Random";
		screen.algorithmCapabilities.available = true;
		screen.algorithmCapabilities.stable = true;
		screen.algorithmCapabilities.inPlace = true;
		screen.algorithmCapabilities.timeComplexity = "O(n^2)";
		screen.algorithmCapabilities.spaceComplexity = "O(1)";
		screen.algorithmCapabilities.metrics.push_back(UiTelemetryValue("passes", "Passes", "0"));

		view.synchronizeText(preferences, UiPresetId::Current, snapshot, screen, 1000);
		check(view.text().title == "Sorting Simulation | deterministic event trace" &&
			view.text().detail == "Insertion Sort  |  Ascending  |  80 bars  |  Normalized floats / Random  |  Stable, in-place  |  O(n^2) time, O(1) space  |  No trace  |  0.0/10.0 sec" &&
			view.text().status == "Ready." && view.text().collapsedSummary == "Insertion Sort - 0%",
			"main view formats the current preset's title, algorithm facts, status, and collapsed progress");

		snapshot.hasTrace = true;
		snapshot.playing = true;
		snapshot.cursor = 1;
		snapshot.eventCount = 10;
		snapshot.elapsedSeconds = 0.1f;
		snapshot.metrics.comparisons = 1;
		snapshot.status = "Playing.";
		view.synchronizeText(preferences, UiPresetId::Current, snapshot, screen, 1100);
		check(view.text().detail.find("Event 1/10") == std::string::npos && view.text().status == "Ready.",
			"live telemetry remains throttled between 200 ms updates during playback");
		snapshot.elapsedSeconds = 0.2f;
		snapshot.metrics.comparisons = 2;
		view.synchronizeText(preferences, UiPresetId::Current, snapshot, screen, 1200);
		check(view.text().detail.find("Event 1/10") != std::string::npos &&
			view.text().status.find("Cmp 2") != std::string::npos && view.text().collapsedSummary == "Insertion Sort - 10%",
			"main view refreshes telemetry and progress at the established live text cadence");

		view.synchronizeText(preferences, UiPresetId::ClassicWindows95, snapshot, screen, 1250);
		check(view.text().title == "Actually Good Sorting Sounds",
			"main view updates the title immediately when the active UI preset changes");
	}

	void testMainControlsViewAppliesLayoutAndVisibility()
	{
		UserPreferences preferences;
		MainControlsView view;
		view.initializeControls(preferences, "Fixture Sort");
		UiLayoutResult layout;
		layout.focusOrder.push_back("shuffle");
		layout.controlBounds["algorithm"] = UiRect(100.0f, 40.0f, 220.0f, 28.0f);
		layout.controlBounds["direction"] = UiRect(330.0f, 40.0f, 180.0f, 28.0f);
		layout.interactiveSubregions["direction"].push_back(UiRect(330.0f, 40.0f, 90.0f, 28.0f));
		layout.interactiveSubregions["direction"].push_back(UiRect(420.0f, 40.0f, 90.0f, 28.0f));

		view.applyLayout(UiPresetId::Current, layout, 1280, true);
		check(view.controls().find("algorithm")->bounds().x == 100.0f &&
			view.controls().find("direction")->presentation() == UiWidgetPresentation::Standard &&
			view.controls().find("direction")->interactiveSubregions().size() == 2 &&
			view.controls().find("bars_value")->presentation() == UiWidgetPresentation::EditableLabel,
			"main view applies cached layout geometry and control presentations");
		check(view.controls().find("shuffle")->visible() && !view.controls().find("menu_file")->visible() &&
			view.controls().find("run")->visible() && !view.controls().find("domain")->visible(),
			"current preset keeps its compact visible-control set");

		view.applyLayout(UiPresetId::FlatVgaDos, layout, 1280, true);
		check(view.controls().find("direction")->presentation() == UiWidgetPresentation::RadioGroup &&
			view.controls().find("menu_file")->visible() && !view.controls().find("run")->visible() &&
			view.controls().find("domain")->visible() && view.controls().find("pattern")->visible() &&
			view.controls().find("seed_readout")->visible(),
			"wide legacy preset promotes domain controls and radio presentation into its menu rail");

		view.applyLayout(UiPresetId::FlatVgaDos, layout, 1280, false);
		check(!view.controls().find("shuffle")->visible() && view.controls().find("panel_toggle")->visible() &&
			view.controls().find("menu_tools")->visible() && view.controls().find("start_overlay")->visible() &&
			view.controls().find("panel_toggle")->label() == ">",
			"collapsed legacy layout retains its header/menu and start action while hiding setup controls");

		view.applyLayout(UiPresetId::ClassicWindows95, layout, 1280, false);
		check(!view.controls().find("panel_toggle")->visible() && !view.controls().find("settings")->visible() &&
			view.controls().find("menu_file")->visible(),
			"Classic Windows 95 layout hides modern header buttons while preserving its menu bar");
	}

	void testTimelineViewOwnsSeekInputAndPaintComposition()
	{
		TimelineView view;
		UiLayoutResult layout;
		layout.timeline = UiRect(10.0f, 20.0f, 120.0f, 12.0f);
		layout.timelineTrack = UiRect(22.0f, 20.0f, 96.0f, 12.0f);
		layout.timelineLeftArrow = UiRect(10.0f, 20.0f, 12.0f, 12.0f);
		layout.timelineRightArrow = UiRect(118.0f, 20.0f, 12.0f, 12.0f);
		view.applyLayout(layout);
		AppAction action;
		check(view.pointerDown(11.0f, 26.0f, true, action) && action.kind == AppActionKind::PreviousEvent,
			"timeline left arrow emits the previous-event transport action");
		action = AppAction();
		check(view.pointerDown(129.0f, 26.0f, true, action) && action.kind == AppActionKind::NextEvent,
			"timeline right arrow emits the next-event transport action");
		action = AppAction();
		check(!view.pointerDown(70.0f, 26.0f, false, action) && action.kind == AppActionKind::None,
			"disabled timeline input does not capture the pointer or emit a command");
		action = AppAction();
		check(view.pointerDown(70.0f, 26.0f, true, action) && action.kind == AppActionKind::SeekFraction &&
			std::fabs(action.value - 0.5f) < 1.0e-6f && view.dragging(),
			"timeline track maps pointer position to a captured seek fraction");
		action = AppAction();
		check(view.pointerMove(200.0f, action) && std::fabs(action.value - 1.0f) < 1.0e-6f,
			"timeline drag clamps its seek fraction at the right edge");
		action = AppAction();
		check(view.pointerUp(-50.0f, action) && std::fabs(action.value) < 1.0e-6f && !view.dragging(),
			"timeline release applies its final clamped seek and releases capture");

		sortinglogic::RenderSnapshot snapshot;
		snapshot.hasTrace = true;
		snapshot.elapsedSeconds = 5.0f;
		TimelineViewPalette palette;
		palette.rail = UiColor(0.1f, 0.2f, 0.3f, 1.0f);
		palette.accent = UiColor(0.4f, 0.5f, 0.6f, 1.0f);
		palette.thumb = UiColor(0.7f, 0.8f, 0.9f, 1.0f);
		palette.button = UiColor(0.2f, 0.3f, 0.4f, 1.0f);
		palette.text = UiColor(1.0f, 1.0f, 1.0f, 1.0f);
		palette.border = UiColor(0.3f, 0.3f, 0.3f, 1.0f);
		palette.brightEdge = palette.text;
		palette.darkEdge = palette.border;
		RecordingUiPaintContext paint;
		view.compose(true, true, false, 10.0, snapshot, palette, UiRect(0.0f, 0.0f, 200.0f, 100.0f), paint);
		check(paint.commands().size() > 20 && paint.commands()[0].elementId == "timeline.track" &&
			paint.commands()[1].elementId == "timeline.progress" &&
			std::fabs(paint.commands()[1].bounds.width - 48.0f) < 1.0e-6f &&
			paint.commands()[2].elementId == "timeline.tick.0" &&
			paint.commands()[2].layer == UiPaintLayer::MainControlsTop,
			"timeline view emits progress, DOS ticks, and pixel controls in painter order");

		paint.clear();
		snapshot.hasTrace = false;
		view.compose(true, false, false, 10.0, snapshot, palette, UiRect(0.0f, 0.0f, 200.0f, 100.0f), paint);
		check(paint.commands().empty(), "timeline stays hidden without a trace outside the legacy presets");
		view.compose(false, true, false, 10.0, snapshot, palette, UiRect(0.0f, 0.0f, 200.0f, 100.0f), paint);
		check(paint.commands().empty(), "collapsed layout suppresses timeline paint even when a legacy ruler is available");
	}

	void testMainControlsViewSynchronizesPresentationState()
	{
		UserPreferences preferences;
		preferences.visual.panelExpanded = false;
		preferences.simulation.playbackTimingMode = SoundEngine::PlaybackTimingMode::ReferenceCadence;
		MainControlsView view;
		view.initializeControls(preferences, "Fixture Sort");

		sortinglogic::RenderSnapshot snapshot;
		snapshot.algorithm = sortinglogic::SortAlgorithm::Insertion;
		snapshot.direction = sortinglogic::SortDirection::Descending;
		snapshot.itemCount = 23;
		snapshot.inputSeed = 41;
		snapshot.durationSeconds = 8.0f;

		UiScreenModel screen;
		screen.setupEnabled = false;
		screen.runEnabled = false;
		screen.playEnabled = true;
		screen.playCaption = "Pause";
		screen.durationCaption = "8.0 sec";
		screen.transport = UiTransportState::Playing;
		view.synchronize(preferences, UiPresetId::ClassicWindows95, snapshot, screen, "Insertion Sort");

		const UIRoot& controls = view.controls();
		check(!controls.find("shuffle")->enabled() && !controls.find("run")->enabled() &&
			controls.find("play")->enabled() && controls.find("play")->label() == "Pause",
			"main controls view synchronizes busy setup state and available transport state");
		check(controls.find("algorithm")->label() == "Insertion Sort" &&
			controls.find("direction")->selectedIndex() == 1 && controls.find("bars_value")->label() == "23 bars" &&
			controls.find("seed_readout")->label() == "Seed 41",
			"main controls view synchronizes the algorithm and prepared-run readouts");
		check(controls.find("time_value")->label() == "8.0 sec" && !controls.find("time_value")->enabled() &&
			!controls.find("time_down")->enabled() && !controls.find("time_up")->enabled(),
			"reference-cadence runs disable duration editing while keeping its current caption visible");
		check(controls.find("start_overlay")->visible() && controls.find("start_overlay")->enabled() &&
			controls.find("next")->label() == "Step",
			"collapsed classic layout synchronizes its start overlay and preset-specific transport label");
	}

	void testMainControlsViewComposesControlPaintCommands()
	{
		UserPreferences preferences;
		MainControlsView view;
		view.initializeControls(preferences, "Fixture Sort");
		view.controls().setWidgetVisible("start_overlay", false);
		const UiRect screen(0.0f, 0.0f, 800.0f, 600.0f);
		RecordingUiPaintContext paint;

		view.compose(MainControlsPaintSection::HeaderButtons, screen, paint);
		check(paint.commands().size() == 2 && paint.commands()[0].elementId == "panel_toggle" &&
			paint.commands()[1].elementId == "settings" && paint.commands()[0].layer == UiPaintLayer::MainControls,
			"main control paint composition preserves header button order and layer");
		paint.clear();
		view.compose(MainControlsPaintSection::Menus, screen, paint);
		check(paint.commands().size() == 3 && paint.commands()[0].elementId == "menu_file" &&
			paint.commands()[1].elementId == "menu_view" && paint.commands()[2].elementId == "menu_tools",
			"main control paint composition preserves menu order");
		paint.clear();
		view.compose(MainControlsPaintSection::ExpandedControls, screen, paint);
		check(!paint.commands().empty() && paint.commands()[0].elementId == "shuffle" &&
			std::find_if(paint.commands().begin(), paint.commands().end(), [](const UiPaintCommand& command)
			{
				return command.elementId == "menu_tools" || command.elementId == "panel_toggle" || command.elementId == "settings";
			}) == paint.commands().end(),
			"expanded main paint commands include only visible content controls in retained order");
		paint.clear();
		view.controls().setWidgetVisible("start_overlay", true);
		view.compose(MainControlsPaintSection::CollapsedStart, screen, paint);
		check(paint.commands().size() == 1 && paint.commands()[0].elementId == "start_overlay",
			"collapsed main paint composition emits the start overlay only when visible");

		paint.clear();
		view.controls().find("domain")->setBounds(UiRect(32.0f, 48.0f, 180.0f, 26.0f));
		UiAction action;
		view.controls().handleEvent(UiInputEvent(UiInputType::PointerDown, 40.0f, 54.0f), action);
		view.compose(MainControlsPaintSection::Popups, screen, paint);
		check(view.controls().hasOpenCombo() && paint.commands().size() == 1 &&
			paint.commands()[0].elementId == "domain" && paint.commands()[0].popup &&
			paint.commands()[0].layer == UiPaintLayer::MainDropdown && paint.commands()[0].hasClip,
			"expanded main dropdowns use clipped popup commands on the dropdown layer");
	}

	void testSettingsPanelViewSynchronizesControlState()
	{
		const sortinglogic::AlgorithmDescriptor* merge = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		check(merge != nullptr, "settings synchronization fixture resolves the Merge descriptor");
		if (!merge) return;

		UserPreferences preferences;
		preferences.simulation.algorithm = static_cast<int>(merge->id);
		preferences.simulation.familyId = merge->familyId;
		preferences.simulation.variantId = merge->variantId;
		preferences.simulation.domain = sortinglogic::InputDomain::NonnegativeIntegerKeys;
		preferences.simulation.pattern = sortinglogic::InputPattern::FewUnique;
		preferences.simulation.profile = sortinglogic::InputProfile::IntegerKeys;
		preferences.simulation.playbackTimingMode = SoundEngine::PlaybackTimingMode::ReferenceCadence;
		preferences.simulation.mergeBufferVisualizationEnabled = false;
		preferences.simulation.completionScanEnabled = true;
		preferences.reconstructedAudio.masterLevel = 0.42;
		preferences.reconstructedAudio.idlePolicy = SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
		preferences.reconstructedAudio.lowPassEnabled = false;
		preferences.familyAudioDefaults[merge->familyId].decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = -7.0f;
		preferences.variantAudioOffsets[variantAudioSettingsKey(merge->familyId, merge->variantId)]
			.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = 3.5f;

		sortinglogic::RenderSnapshot snapshot;
		snapshot.algorithm = merge->id;
		snapshot.domain = preferences.simulation.domain;
		snapshot.pattern = preferences.simulation.pattern;
		SettingsPanelView view;
		view.initializeControls(preferences);
		view.synchronizeControls(preferences, preferences, snapshot, true);

		const UiWidget* domain = view.controls().find("domain");
		const UiWidget* pattern = view.controls().find("pattern");
		const UiWidget* timing = view.controls().find("playback_timing");
		const UiWidget* mergeBuffer = view.controls().find("merge_buffer_visualization");
		const UiWidget* completionScan = view.controls().find("completion_scan");
		check(domain && domain->selectedIndex() == static_cast<int>(preferences.simulation.domain) && !domain->enabled(),
			"settings view synchronizes simulation selections and disables setup while the run is busy");
		check(pattern && pattern->selectedIndex() == static_cast<int>(sortinglogic::InputPattern::FewUnique) &&
			!pattern->optionEnabled(static_cast<std::size_t>(sortinglogic::InputPattern::AndrutReference)),
			"settings view keeps pattern selection while disabling domain-incompatible choices");
		check(timing && timing->selectedIndex() == static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence) &&
			!timing->enabled(),
			"settings view preserves playback timing selection and busy-state gating");
		check(mergeBuffer && mergeBuffer->selectedIndex() == 0 && !mergeBuffer->enabled() &&
			completionScan && completionScan->selectedIndex() == 1 && !completionScan->enabled(),
			"settings view synchronizes algorithm-specific and completion controls under the busy rule");

		const UiWidget* master = view.controls().find("recon_master");
		const UiWidget* release = view.controls().find("recon_release");
		const UiWidget* cutoff = view.controls().find("recon_cutoff");
		const UiWidget* comparisonGain = view.controls().find("algorithm_audio_00");
		const UiWidget* audioReset = view.controls().find("algorithm_audio_reset");
		check(master && std::fabs(master->value() - 0.42f) < 0.0001f && release && release->enabled() &&
			cutoff && !cutoff->enabled(),
			"settings view synchronizes reconstructed audio values and dependent enabled states");
		const std::map<std::string, std::string>::const_iterator comparisonCaption =
			view.captions().find("algorithm_audio_00");
		check(comparisonGain && std::fabs(comparisonGain->value() - 3.5f) < 0.0001f,
			"settings view synchronizes the selected variant audio offset");
		check(comparisonGain && comparisonGain->action().familyId == merge->familyId &&
			comparisonGain->action().variantId == merge->variantId,
			"settings view binds audio changes to the selected family and variant");
		check(comparisonCaption != view.captions().end() && comparisonCaption->second.find("-7.0 dB") != std::string::npos,
			"settings view displays the inherited family audio level");
		check(audioReset && audioReset->action().familyId == merge->familyId &&
			audioReset->action().variantId == merge->variantId,
			"settings view binds reset-to-defaults to the selected family and variant");

		const std::string mergeOptionId = settingsAlgorithmOptionControlId(merge->familyId, merge->variantId,
			merge->supportedOptions.empty() ? std::string() : merge->supportedOptions.front().groupId);
		const UiWidget* mergeOption = view.controls().find(mergeOptionId);
		check(mergeOption && !mergeOption->enabled(),
			"settings view disables catalog-derived algorithm options while setup is busy");

		view.synchronizeControls(preferences, preferences, snapshot, false);
		check(domain->enabled() && pattern->enabled() && timing->enabled() && mergeBuffer->enabled() &&
			completionScan->enabled() && mergeOption->enabled(),
			"settings view restores setup controls when the simulation is no longer busy");
		check(settingsInputPatternEnabled(sortinglogic::InputDomain::NormalizedFloats, merge->id,
			sortinglogic::InputPattern::AndrutReference) &&
			!settingsInputPatternEnabled(sortinglogic::InputDomain::NonnegativeIntegerKeys, merge->id,
				sortinglogic::InputPattern::AndrutReference),
			"shared input-pattern compatibility policy is consistent for the main and settings controls");
	}

	void testSettingsPanelControlCatalog()
	{
		const std::vector<std::string> appearance = settingsPanelControlIds(
			SettingsPanelPage::Appearance, SettingsAudioGroup::Basic, nullptr);
		check(appearance.size() == 4 && appearance[0] == "preset" && appearance[1] == "palette" &&
			appearance[2] == "spacing" && appearance[3] == "appearance_customize",
			"Appearance exposes its established settings controls in row order");

		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		const sortinglogic::AlgorithmDescriptor* merge = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		const sortinglogic::AlgorithmDescriptor* counting = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Counting);
		check(insertion && merge && counting, "settings control catalog fixtures are registered");
		if (!insertion || !merge || !counting) return;
		const SettingsPanelAlgorithmPresentation insertionPresentation = settingsPanelAlgorithmPresentation(insertion);
		const SettingsPanelAlgorithmPresentation mergePresentation = settingsPanelAlgorithmPresentation(merge);
		const SettingsPanelAlgorithmPresentation countingPresentation = settingsPanelAlgorithmPresentation(counting);
		const std::vector<std::string> insertionControls = settingsPanelControlIds(
			SettingsPanelPage::Algorithm, SettingsAudioGroup::Basic, &insertionPresentation);
		const auto contains = [](const std::vector<std::string>& ids, const std::string& id)
		{
			for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return true;
			return false;
		};
		check(contains(insertionControls, "domain") && contains(insertionControls, "pattern") &&
			contains(insertionControls, "algorithm_audio_00") && contains(insertionControls, "algorithm_audio_reset") &&
			!contains(insertionControls, "algorithm_audio_01") && !contains(insertionControls, "algorithm_audio_03"),
			"algorithm settings include common controls and only the existing audio operations for Insertion Sort");

		const std::vector<std::string> mergeControls = settingsPanelControlIds(
			SettingsPanelPage::Algorithm, SettingsAudioGroup::Basic, &mergePresentation);
		check(contains(mergeControls, "merge_buffer_visualization") &&
			contains(mergeControls, settingsAlgorithmOptionControlId(merge->familyId, merge->variantId, "execution_mode")) &&
			contains(mergeControls, settingsAlgorithmOptionControlId(merge->familyId, merge->variantId, "writeback_mode")) &&
			contains(mergeControls, "algorithm_audio_03"),
			"Merge settings expose its registered options, buffer preference, and auxiliary audio level");

		const std::vector<std::string> countingControls = settingsPanelControlIds(
			SettingsPanelPage::Algorithm, SettingsAudioGroup::Basic, &countingPresentation);
		check(contains(countingControls, "algorithm_audio_01") && contains(countingControls, "algorithm_audio_03"),
			"distribution settings expose probe and auxiliary audio levels");
		const SettingsPanelAlgorithmPresentation unknownPresentation = settingsPanelAlgorithmPresentation(nullptr);
		const std::vector<std::string> unknownAlgorithm = settingsPanelControlIds(
			SettingsPanelPage::Algorithm, SettingsAudioGroup::Basic, &unknownPresentation);
		check(contains(unknownAlgorithm, "algorithm_audio_00") &&
			contains(unknownAlgorithm, "algorithm_audio_reset") &&
			!contains(unknownAlgorithm, "algorithm_audio_01"),
			"missing algorithm metadata keeps generic comparison controls visible through the catalog adapter");

		const std::vector<std::string> basicAudio = settingsPanelControlIds(
			SettingsPanelPage::GeneralAudio, SettingsAudioGroup::Basic, &insertionPresentation);
		const std::vector<std::string> advancedAudio = settingsPanelControlIds(
			SettingsPanelPage::GeneralAudio, SettingsAudioGroup::Advanced, &insertionPresentation);
		check(basicAudio.size() == 5 && basicAudio[0] == "recon_enabled" && basicAudio[4] == "recon_restore" &&
			advancedAudio.size() == 9 && advancedAudio[0] == "recon_min_pitch" && advancedAudio[8] == "recon_cutoff",
			"General Audio basic and advanced groups preserve their existing control lists");
	}

	void testSettingsPanelLayout()
	{
		const UiRect panel(100.0f, 50.0f, 600.0f, 500.0f);
		const SettingsPanelGeometry appearance = calculateSettingsPanelGeometry(panel, SettingsPanelPage::Appearance);
		const float tabWidth = (568.0f - 8.0f) / 3.0f;
		check(appearance.tabs[0].x == 116.0f && appearance.tabs[1].x == 116.0f + tabWidth + 4.0f &&
			appearance.tabs[2].x == 116.0f + 2.0f * (tabWidth + 4.0f) &&
			appearance.content.y == 132.0f && appearance.content.height == 358.0f &&
			appearance.closeButton.x == 608.0f && appearance.audioRetryButton.width == 190.0f,
			"settings panel geometry preserves Appearance tab, content, and footer placement");
		const SettingsPanelGeometry audio = calculateSettingsPanelGeometry(panel, SettingsPanelPage::GeneralAudio);
		check(audio.content.y == 162.0f && audio.content.height == 328.0f &&
			audio.audioGroups[0].y == 127.0f && audio.audioGroups[1].y == 127.0f,
			"General Audio geometry reserves its additional group-tab row");
		const SettingsPanelGeometry compact = calculateSettingsPanelGeometry(UiRect(5.0f, 7.0f, 80.0f, 40.0f),
			SettingsPanelPage::Algorithm);
		check(compact.content.width == 48.0f && compact.content.height == 28.0f,
			"small settings panes preserve the existing minimum content size");

		UiWidget combo(UiWidgetKind::ComboBox, "combo", "Choice", AppAction(), UiAction::ViewCommand::None);
		SettingsWidgetGeometry comboGeometry = calculateSettingsWidgetGeometry(combo, UiRect(20.0f, 100.0f, 300.0f, 48.0f),
			30.0f, 280.0f, panel, 8.0f);
		check(comboGeometry.bounds.x == 30.0f && comboGeometry.bounds.y == 117.0f &&
			comboGeometry.bounds.width == 280.0f && comboGeometry.bounds.height == 27.0f,
			"settings combo rows retain their label gap and control dimensions");

		UiWidget radio(UiWidgetKind::Button, "radio", "Radio", AppAction(), UiAction::ViewCommand::None);
		radio.setPresentation(UiWidgetPresentation::RadioGroup);
		radio.setOptions(std::vector<std::string>{ "One", "Two", "Three" });
		const SettingsWidgetGeometry radioGeometry = calculateSettingsWidgetGeometry(radio,
			UiRect(20.0f, 100.0f, 300.0f, 48.0f), 30.0f, 280.0f, panel, 8.0f);
		check(radioGeometry.bounds.y == 108.0f && radioGeometry.bounds.height == 28.0f &&
			radioGeometry.interactiveSubregions.size() == 3 && radioGeometry.interactiveSubregions[1].x ==
			30.0f + 280.0f / 3.0f,
			"radio settings retain equal pointer-hit segments");

		UiWidget dosScroll(UiWidgetKind::Slider, "scroll", "Scroll", AppAction(), UiAction::ViewCommand::None);
		dosScroll.setPresentation(UiWidgetPresentation::DosScrollbar);
		dosScroll.setScrollbarArrowSize(16.0f);
		const SettingsWidgetGeometry dosGeometry = calculateSettingsWidgetGeometry(dosScroll,
			UiRect(20.0f, 100.0f, 300.0f, 48.0f), 30.0f, 180.0f, panel, 8.0f);
		check(dosGeometry.interactiveSubregions.size() == 3 && dosGeometry.interactiveSubregions[0].x == 30.0f &&
			dosGeometry.interactiveSubregions[1].x == 46.0f && dosGeometry.interactiveSubregions[1].width == 148.0f &&
			dosGeometry.interactiveSubregions[2].x == 194.0f,
			"DOS scrollbar layout preserves arrow, track, and pointer subregions");

		UiWidget legacyBand(UiWidgetKind::ComboBox, "band_selector", "Band", AppAction(), UiAction::ViewCommand::None);
		const SettingsWidgetGeometry popupGeometry = calculateSettingsWidgetGeometry(legacyBand,
			UiRect(20.0f, 180.0f, 300.0f, 48.0f), 30.0f, 280.0f, UiRect(0.0f, 0.0f, 400.0f, 220.0f), 8.0f);
		check(popupGeometry.setPopupAbove && popupGeometry.popupAbove,
			"legacy band-selector popup placement remains panel-aware");
	}

	void testSettingsStore()
	{
		wchar_t temporary[MAX_PATH] = {};
		const DWORD tempCount = GetTempPathW(MAX_PATH, temporary);
		check(tempCount > 0 && tempCount < MAX_PATH, "resolve temporary directory");
		const std::wstring path = std::wstring(temporary) + L"agss-framework-tests-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
		DeleteFileW(path.c_str());
		SettingsStore store(path);
		const UserSettings defaults = store.load();
		check(defaults.visual.palette == PaletteId::Cyan && defaults.visual.spacing == BarSpacingMode::Compact, "missing settings use the documented appearance defaults");
		check(defaults.visual.panelExpanded && defaults.simulation.itemCount == 80 && defaults.simulation.durationSeconds == 10, "missing settings use the documented layout and run defaults");
		check(!defaults.simulation.descending && defaults.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion),
			"missing settings use ascending Insertion Sort");
		check(defaults.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::FixedDuration,
			"fixed-duration playback remains the default timing mode");
		check(defaults.version == 8 && defaults.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			defaults.simulation.familyId == "insertion-methods" && defaults.simulation.variantId == "insertion" &&
			defaults.simulation.profile == sortinglogic::InputProfile::NormalizedFloat,
			"new settings use stable algorithm IDs and showcase defaults");
		const sortinglogic::AlgorithmDescriptor* defaultShell = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
		check(defaultShell && algorithmOptionValue(defaults, *defaultShell, "gap_sequence") ==
			static_cast<int>(sortinglogic::ShellGapSequence::Halving),
			"catalog metadata supplies the Shell option default without duplicated preference state");
		check(defaults.audio.masterLevel == 0.5f && defaults.audio.bedLevel == 0.15f &&
			defaults.audio.bedHarmonicExponent == 1.5f && defaults.audio.bedComb.enabled && defaults.audio.bedComb.applyPhase,
			"missing settings use the selected preview audio profile");
		check(defaults.audio.attackMs == 8.0f && defaults.audio.releaseMs == 8.0f &&
			defaults.audio.tauBelowBins == 0.5f && defaults.audio.tauAboveBins == 0.5f &&
			defaults.audio.chorusMix == 0.0f, "audio default envelope and chorus profile matches the selected preview");
		check(defaults.reconstructedAudio.masterLevel == 1.0 &&
			std::fabs(defaults.reconstructedAudio.minimumFrequencyHz - 104.0322699094767) < 1e-12 &&
			std::fabs(defaults.reconstructedAudio.maximumFrequencyHz - 4054.6605862015895) < 1e-12 &&
			std::fabs(defaults.reconstructedAudio.secondOperandDelayMicroseconds - 225.66891905936825) < 1e-9,
			"version-8 defaults use the calibrated reconstructed burst profile");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			check(defaults.audio.bandTrimDb[i] == 0.0f, "every per-band trim defaults to zero");

		UserSettings saved;
		saved.visual.palette = PaletteId::Light;
		saved.visual.spacing = BarSpacingMode::Comfortable;
		saved.appearance.activePreset = UiPresetId::FlatVgaDos;
		saved.appearance.themes[UiPresetId::Current].palette = PaletteId::Light;
		saved.appearance.themes[UiPresetId::FlatVgaDos].palette = PaletteId::Amber;
		check(setUiTokenValue(UiPresetId::Current, saved.appearance.themes[UiPresetId::Current], "color.bar",
			UiTokenValue::fromColor(UiColor(0.2f, 0.3f, 0.4f, 1.0f))), "settings fixture accepts Current color override");
		check(setUiTokenValue(UiPresetId::FlatVgaDos, saved.appearance.themes[UiPresetId::FlatVgaDos], "scrollbar.show_arrows",
			UiTokenValue::fromBoolean(false)), "settings fixture accepts VGA decoration override");
		saved.visual.panelExpanded = false;
		saved.simulation.itemCount = 800;
		saved.simulation.durationSeconds = 17.25f;
		saved.simulation.playbackTimingMode = SoundEngine::PlaybackTimingMode::ReferenceCadence;
		saved.simulation.descending = true;
		saved.simulation.algorithm = static_cast<int>(sortinglogic::SortAlgorithm::Merge);
		saved.simulation.familyId = "merge-sort";
		saved.simulation.variantId = "merge";
		saved.simulation.profile = sortinglogic::InputProfile::IntegerKeys;
		saved.simulation.domain = sortinglogic::InputDomain::SignedIntegerKeys;
		saved.simulation.pattern = sortinglogic::InputPattern::NearlySorted;
		saved.algorithmOptions[algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence")] = "ciura";
		saved.algorithmOptions[algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode")] = "top_down";
		saved.algorithmOptions[algorithmOptionSettingsKey("merge-sort", "merge", "writeback_mode")] = "animated_writes";
		saved.algorithmOptions[algorithmOptionSettingsKey("merge-hybrid", "optimized-rotate", "rotate_buffer_capacity")] = "items_128";
		saved.audio.masterLevel = 0.73f;
		saved.audio.bedLevel = 0.42f;
		saved.audio.bedHarmonicExponent = 2.25f;
		saved.audio.includeSub300HzBed = true;
		saved.audio.bedComb.enabled = false;
		saved.audio.bedComb.delayMicroseconds = 350.0f;
		saved.audio.bedComb.applyPhase = false;
		saved.audio.bedComb.phaseOffsetDegrees = -42.0f;
		saved.audio.bedComb.wetAtLowFrequency = 0.45f;
		saved.audio.bedComb.wetAtHighFrequency = 0.34f;
		saved.audio.bedComb.fadeStartHz = 7300.0f;
		saved.audio.bedComb.fadeEndHz = 16000.0f;
		saved.audio.bedComb.fadeExponent = 2.4f;
		saved.audio.comparisonBoostDb = 8.25f;
		saved.audio.heldKeyBoostDb = 11.5f;
		saved.audio.tauBelowBins = 0.85f;
		saved.audio.tauAboveBins = 1.2f;
		saved.audio.chorusMix = 0.17f;
		saved.audio.chorusRateHz = 2.75f;
		saved.audio.chorusDepthMs = 4.1f;
		saved.audio.chorusWeighting = SoundEngine::ChorusWeighting::ComparisonEnvelope;
		saved.audio.attackMs = 13.0f;
		saved.audio.releaseMs = 40.0f;
		saved.audio.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
		saved.audio.comparisonSpreadMode = SoundEngine::ComparisonSpreadMode::PositionScaled;
		saved.audio.enabled = false;
		saved.reconstructedAudio.minimumFrequencyHz = 120.0;
		saved.reconstructedAudio.maximumFrequencyHz = 3900.0;
		saved.reconstructedAudio.excitationClockHz = 350.0;
		saved.reconstructedAudio.carrierCycles = 7.25;
		saved.reconstructedAudio.tukeyFraction = 0.12;
		saved.reconstructedAudio.secondOperandDelayMicroseconds = 900.0;
		saved.reconstructedAudio.masterLevel = 0.73;
		saved.reconstructedAudio.specialReleaseMilliseconds = 800.0;
		saved.reconstructedAudio.lowPassCutoffHz = 8200.0;
		saved.reconstructedAudio.audioEnabled = false;
		saved.reconstructedAudio.lowPassEnabled = true;
		saved.reconstructedAudio.mapping = SoundEngine::ReconstructedMapping::LogFrequency;
		saved.reconstructedAudio.idlePolicy = SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
		saved.audio.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = false;
		saved.audio.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] = 0.23f;
		saved.audio.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] = 0.31f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			saved.audio.bandTrimDb[i] = -30.0f + 0.5f * static_cast<float>(i);
		saved.audio.bandTrimDb[0] = -12.5f;
		saved.audio.bandTrimDb[59] = 4.0f;
		AlgorithmAudioLevels mergeFamilyLevels = defaultFamilyAudioLevels(saved.audio);
		mergeFamilyLevels.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = 9.5f;
		saved.familyAudioDefaults["merge-sort"] = mergeFamilyLevels;
		AlgorithmAudioLevels mergeOffsets;
		mergeOffsets.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = -12.0f;
		mergeOffsets.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::AuxiliaryAccent)] = -6.0f;
		saved.variantAudioOffsets[variantAudioSettingsKey("merge-sort", "merge")] = mergeOffsets;
		std::string error;
		check(store.save(saved, &error), "save a valid settings file");
		SettingsStore restartedStore(path);
		const UserSettings loaded = restartedStore.load();
		check(loaded.visual.palette == saved.visual.palette && loaded.visual.spacing == saved.visual.spacing, "settings round-trip palette and spacing");
		check(loaded.appearance.activePreset == saved.appearance.activePreset &&
			loaded.appearance.themes.at(UiPresetId::Current).palette == PaletteId::Light &&
			loaded.appearance.themes.at(UiPresetId::FlatVgaDos).palette == PaletteId::Amber &&
			loaded.appearance.themes.at(UiPresetId::Current).overrides.find("color.bar") != loaded.appearance.themes.at(UiPresetId::Current).overrides.end() &&
			loaded.appearance.themes.at(UiPresetId::FlatVgaDos).overrides.find("scrollbar.show_arrows") != loaded.appearance.themes.at(UiPresetId::FlatVgaDos).overrides.end(),
			"settings round-trip independent preset palettes, active base, and appearance overrides");
		check(loaded.visual.panelExpanded == saved.visual.panelExpanded && loaded.simulation.itemCount == saved.simulation.itemCount, "settings round-trip panel state and item count");
		check(loaded.simulation.durationSeconds == saved.simulation.durationSeconds && loaded.simulation.descending == saved.simulation.descending, "settings round-trip duration and direction");
		check(loaded.simulation.playbackTimingMode == saved.simulation.playbackTimingMode,
			"version-7 settings round-trip playback timing mode");
		check(loaded.simulation.algorithm == saved.simulation.algorithm, "settings round-trip selected algorithm");
		check(loaded.simulation.familyId == saved.simulation.familyId && loaded.simulation.variantId == saved.simulation.variantId,
			"settings round-trip stable family and variant IDs");
		check(loaded.simulation.profile == saved.simulation.profile &&
			algorithmOptionValue(loaded, *defaultShell, "gap_sequence") == static_cast<int>(sortinglogic::ShellGapSequence::Ciura),
			"settings round-trip profile and stable Shell option selection");
		check(loaded.simulation.domain == saved.simulation.domain && loaded.simulation.pattern == saved.simulation.pattern,
			"settings round-trip signed domain and input pattern");
		const sortinglogic::AlgorithmDescriptor* savedMerge = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		check(savedMerge && algorithmOptionValue(loaded, *savedMerge, "writeback_mode") ==
			static_cast<int>(sortinglogic::MergeWritebackMode::AnimatedWrites),
			"settings round-trip configurable Merge Sort writeback mode");
		check(savedMerge && algorithmOptionValue(loaded, *savedMerge, "execution_mode") ==
			static_cast<int>(sortinglogic::MergeExecutionMode::TopDown),
			"settings round-trip top-down and bottom-up Merge execution mode");
		check(loaded.algorithmOptions.find(algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence")) != loaded.algorithmOptions.end() &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence"))->second == "ciura" &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode"))->second == "top_down" &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "writeback_mode"))->second == "animated_writes" &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("merge-hybrid", "optimized-rotate", "rotate_buffer_capacity"))->second == "items_128",
			"algorithm option values round-trip by stable family, variant, and option IDs");
		check(loaded.familyAudioDefaults.find("merge-sort") != loaded.familyAudioDefaults.end() &&
			loaded.familyAudioDefaults.find("merge-sort")->second.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 9.5f &&
			loaded.variantAudioOffsets.find(variantAudioSettingsKey("merge-sort", "merge")) != loaded.variantAudioOffsets.end() &&
			loaded.variantAudioOffsets.find(variantAudioSettingsKey("merge-sort", "merge"))->second.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == -12.0f,
			"settings round-trip family audio defaults and stable per-variant dB offsets");
		const sortinglogic::AlgorithmDescriptor* mergeDescriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		const SoundEngine::Parameters effectiveMergeAudio = effectiveAudioParameters(loaded, *mergeDescriptor);
		check(std::fabs(effectiveMergeAudio.comparisonBoostDb - (-2.5f)) < 1e-5f,
			"variant comparison slider applies its dB offset relative to the family level");
		check(loaded.audio.masterLevel == saved.audio.masterLevel && loaded.audio.bedLevel == saved.audio.bedLevel &&
			loaded.audio.bedHarmonicExponent == saved.audio.bedHarmonicExponent &&
			loaded.audio.includeSub300HzBed == saved.audio.includeSub300HzBed,
			"settings round-trip master, bed, spectral rolloff, and low-bed toggle");
		check(loaded.audio.bedComb.enabled == saved.audio.bedComb.enabled && loaded.audio.bedComb.applyPhase == saved.audio.bedComb.applyPhase &&
			loaded.audio.bedComb.delayMicroseconds == saved.audio.bedComb.delayMicroseconds &&
			loaded.audio.bedComb.phaseOffsetDegrees == saved.audio.bedComb.phaseOffsetDegrees &&
			loaded.audio.bedComb.wetAtLowFrequency == saved.audio.bedComb.wetAtLowFrequency &&
			loaded.audio.bedComb.wetAtHighFrequency == saved.audio.bedComb.wetAtHighFrequency &&
			loaded.audio.bedComb.fadeStartHz == saved.audio.bedComb.fadeStartHz &&
			loaded.audio.bedComb.fadeEndHz == saved.audio.bedComb.fadeEndHz &&
			loaded.audio.bedComb.fadeExponent == saved.audio.bedComb.fadeExponent,
			"settings round-trip every comb-filter parameter");
		check(loaded.audio.comparisonBoostDb == saved.audio.comparisonBoostDb && loaded.audio.heldKeyBoostDb == saved.audio.heldKeyBoostDb &&
			loaded.audio.tauBelowBins == saved.audio.tauBelowBins && loaded.audio.tauAboveBins == saved.audio.tauAboveBins,
			"settings round-trip comparison gains and asymmetric envelope constants");
		check(loaded.audio.chorusMix == saved.audio.chorusMix && loaded.audio.chorusRateHz == saved.audio.chorusRateHz &&
			loaded.audio.chorusDepthMs == saved.audio.chorusDepthMs && loaded.audio.chorusWeighting == saved.audio.chorusWeighting &&
			loaded.audio.attackMs == saved.audio.attackMs && loaded.audio.releaseMs == saved.audio.releaseMs,
			"settings round-trip chorus and envelope controls");
		check(loaded.audio.mappingMode == saved.audio.mappingMode && loaded.audio.comparisonSpreadMode == saved.audio.comparisonSpreadMode &&
			loaded.audio.enabled == saved.audio.enabled, "settings round-trip mapping, spread, and synthesis enable");
		check(loaded.audio.accentRouting == saved.audio.accentRouting && loaded.audio.accentGain == saved.audio.accentGain,
			"settings round-trip semantic-event routing and gains");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			check(loaded.audio.bandTrimDb[i] == saved.audio.bandTrimDb[i], "settings round-trip every band trim");
		check(loaded.reconstructedAudio.minimumFrequencyHz == saved.reconstructedAudio.minimumFrequencyHz &&
			loaded.reconstructedAudio.maximumFrequencyHz == saved.reconstructedAudio.maximumFrequencyHz &&
			loaded.reconstructedAudio.excitationClockHz == saved.reconstructedAudio.excitationClockHz &&
			loaded.reconstructedAudio.carrierCycles == saved.reconstructedAudio.carrierCycles &&
			loaded.reconstructedAudio.tukeyFraction == saved.reconstructedAudio.tukeyFraction &&
			loaded.reconstructedAudio.secondOperandDelayMicroseconds == saved.reconstructedAudio.secondOperandDelayMicroseconds &&
			loaded.reconstructedAudio.masterLevel == saved.reconstructedAudio.masterLevel &&
			loaded.reconstructedAudio.specialReleaseMilliseconds == saved.reconstructedAudio.specialReleaseMilliseconds &&
			loaded.reconstructedAudio.lowPassCutoffHz == saved.reconstructedAudio.lowPassCutoffHz &&
			loaded.reconstructedAudio.audioEnabled == saved.reconstructedAudio.audioEnabled &&
			loaded.reconstructedAudio.lowPassEnabled == saved.reconstructedAudio.lowPassEnabled &&
			loaded.reconstructedAudio.mapping == saved.reconstructedAudio.mapping &&
			loaded.reconstructedAudio.idlePolicy == saved.reconstructedAudio.idlePolicy,
			"version-7 settings round-trip every reconstructed synthesis parameter");

		writeFile(path, "version=1\npalette=light\nspacing=none\npanel_expanded=0\nitem_count=123\nduration_seconds=42\ndescending=1\nalgorithm=0\n");
		const UserPreferences migrated = store.load();
		check(migrated.version == 8 && migrated.visual.palette == PaletteId::Light && migrated.visual.spacing == BarSpacingMode::None &&
			migrated.simulation.itemCount == 123 && migrated.simulation.durationSeconds == 42 && migrated.simulation.descending,
			"version-1 settings migrate without losing appearance or simulation values");
		check(migrated.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::FixedDuration,
			"older settings default to fixed-duration playback");
		check(migrated.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			migrated.simulation.profile == sortinglogic::InputProfile::NormalizedFloat &&
			algorithmOptionValue(migrated, *defaultShell, "gap_sequence") == static_cast<int>(sortinglogic::ShellGapSequence::Halving),
			"version-1 migration establishes showcase defaults safely");
		check(migrated.audio.masterLevel == defaults.audio.masterLevel && migrated.audio.bedComb.enabled == defaults.audio.bedComb.enabled,
			"version-1 settings receive the selected profile audio defaults");
		writeFile(path, "version=2\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=0\naudio_master=0.61\n");
		const UserPreferences migratedV2 = store.load();
		check(migratedV2.version == 8 && migratedV2.visual.palette == PaletteId::Amber && migratedV2.audio.masterLevel == 0.61f &&
			migratedV2.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			migratedV2.simulation.profile == sortinglogic::InputProfile::NormalizedFloat,
			"version-2 settings retain existing values and migrate new fields");
		writeFile(path, "version=3\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=999999\nprofile=integer_keys\nshell_gap_sequence=tokuda\n");
		const UserPreferences unknownAlgorithm = store.load();
		check(unknownAlgorithm.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			unknownAlgorithm.simulation.profile == sortinglogic::InputProfile::IntegerKeys &&
			algorithmOptionValue(unknownAlgorithm, *defaultShell, "gap_sequence") == static_cast<int>(sortinglogic::ShellGapSequence::Tokuda),
			"unknown saved algorithm IDs fall back while valid new settings survive");
		writeFile(path, "version=4\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=made-up\nvariant_id=missing\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserPreferences unknownStableId = store.load();
		check(unknownStableId.simulation.familyId == "insertion-methods" && unknownStableId.simulation.variantId == "insertion" &&
			unknownStableId.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion),
			"unknown version-4 family and variant IDs fall back to the safe catalog default");
		writeFile(path, "version=4\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=shell\nvariant_id=standard\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserPreferences legacyV4Family = store.load();
		check(legacyV4Family.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Shell) &&
			legacyV4Family.simulation.familyId == "insertion-methods" && legacyV4Family.simulation.variantId == "shell",
			"previous version-4 per-algorithm IDs migrate into the grouped family catalog");
		writeFile(path, "version=4\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\naudio_comparison_boost_db=10.5\naudio_held_key_boost_db=4.5\naudio_accent_gain_00=0.12\naudio_accent_gain_01=0.18\naudio_accent_gain_02=0.21\naudio_accent_gain_03=0.09\naudio_accent_route_01=0\n");
		const UserPreferences migratedV4Audio = store.load();
		const AlgorithmAudioLevels inheritedV4 = familyAudioLevels(migratedV4Audio, "insertion-methods");
		check(migratedV4Audio.simulation.familyId == "insertion-methods" && migratedV4Audio.simulation.variantId == "insertion" &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 0.0f &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::HeldKeyRole)] == 0.0f,
			"version-4 legacy gains migrate to neutral defaults for the reconstructed sound model");
		check(inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::ProbeAccent)] == 0.0f &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::MainChangeAccent)] == 0.0f,
			"version-4 accent settings do not leak into reconstructed comparison output");
		writeFile(path, "version=5\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nvariant_audio.insertion-methods.insertion.comparison=nan\n");
		const UserPreferences malformedVariantAudio = store.load();
		check(malformedVariantAudio.isValid() && variantAudioLevels(malformedVariantAudio, "insertion-methods", "insertion")
			.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 0.0f,
			"malformed v5 variant audio values safely fall back to the inherited level");
		const std::wstring migrationBackup = path + L".pre-v8.bak";
		DeleteFileW(migrationBackup.c_str());
		UserPreferences migratingV5 = store.load();
		migratingV5.visual.palette = PaletteId::Light;
		migratingV5.appearance.themes[UiPresetId::Current].palette = PaletteId::Light;
		check(store.save(migratingV5, &error) && GetFileAttributesW(migrationBackup.c_str()) != INVALID_FILE_ATTRIBUTES,
			"first successful version-8 save preserves the previous settings file");
		SettingsStore backupStore(migrationBackup);
		check(backupStore.load().visual.palette == PaletteId::Cyan && store.load().visual.palette == PaletteId::Light,
			"migration backup retains the old values while the active settings file upgrades");
		const UserPreferences roundTripVersion6 = store.load();
		check(store.save(roundTripVersion6, &error) && backupStore.load().visual.palette == PaletteId::Cyan,
			"later version-8 saves do not replace the one-time migration backup");
		writeFile(path, "version=5\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=merge-sort\nvariant_id=merge\nprofile=normalized_float\nmerge_execution_mode=bottom_up\noption.merge-sort.merge.execution_mode=unknown_mode\n");
		const UserPreferences malformedOption = store.load();
		check(malformedOption.isValid() && algorithmOptionValue(malformedOption, *savedMerge, "execution_mode") ==
			static_cast<int>(sortinglogic::MergeExecutionMode::BottomUp) &&
			malformedOption.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode")) == malformedOption.algorithmOptions.end(),
			"unknown v5 option IDs safely fall back to the catalog and typed default");
		writeFile(path, "version=6\npalette=light\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nfamily_audio.insertion-methods.comparison=8.5\nvariant_audio.insertion-methods.insertion.comparison=-6.5\n");
		const UserPreferences migratedV6 = store.load();
		check(migratedV6.version == 8 &&
			familyAudioLevels(migratedV6, "insertion-methods").decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 8.5f &&
			variantAudioLevels(migratedV6, "insertion-methods", "insertion").decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == -6.5f,
			"version-6 migration to version-8 preserves family and variant algorithm audio gains");
		writeFile(path, "version=7\npalette=light\nspacing=compact\npanel_expanded=1\nitem_count=91\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nui_active_preset=flat-vga-dos\nui.current.palette=light\nui.flat-vga-dos.palette=cyan\nui.flat-vga-dos.override.scrollbar.show_arrows=bogus\n");
		const UserPreferences malformedAppearance = store.load();
		check(malformedAppearance.isValid() && malformedAppearance.simulation.itemCount == 91 &&
			malformedAppearance.appearance.activePreset == UiPresetId::FlatVgaDos &&
			malformedAppearance.appearance.themes.at(UiPresetId::FlatVgaDos).palette == PaletteId::Light,
			"malformed saved appearance falls back independently without discarding otherwise valid application preferences");
		writeFile(path, "version=7\npalette=not-a-palette\nspacing=compact\npanel_expanded=1\nitem_count=92\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\n");
		const UserPreferences malformedLegacyAppearance = store.load();
		check(malformedLegacyAppearance.isValid() && malformedLegacyAppearance.simulation.itemCount == 92 &&
			malformedLegacyAppearance.appearance.themes.at(UiPresetId::Current).palette == PaletteId::Cyan,
			"malformed version-7 compatibility palette falls back without discarding valid simulation settings");
		writeFile(path, "version=7\npalette=amber\nspacing=comfortable\npanel_expanded=0\nitem_count=96\nduration_seconds=12\ndescending=1\nfamily_id=merge-sort\nvariant_id=merge\nprofile=normalized_float\nmerge_execution_mode=bottom_up\nmerge_writeback_mode=animated_writes\noption.merge-sort.merge.execution_mode=top_down\noption.merge-sort.merge.writeback_mode=animated_writes\nfamily_audio.merge-sort.comparison=5.5\nvariant_audio.merge-sort.merge.comparison=-2.5\n");
		const UserPreferences migratedV7 = store.load();
		check(migratedV7.version == 8 && migratedV7.simulation.familyId == "merge-sort" &&
			migratedV7.simulation.variantId == "merge" &&
			algorithmOptionValue(migratedV7, *savedMerge, "execution_mode") == static_cast<int>(sortinglogic::MergeExecutionMode::TopDown) &&
			algorithmOptionValue(migratedV7, *savedMerge, "writeback_mode") == static_cast<int>(sortinglogic::MergeWritebackMode::AnimatedWrites),
			"version-7 to version-8 migration preserves the selected algorithm and stable execution options");
		check(familyAudioLevels(migratedV7, "merge-sort").decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 5.5f &&
			variantAudioLevels(migratedV7, "merge-sort", "merge").decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == -2.5f,
			"version-7 to version-8 migration preserves family and variant audio gain overrides");

		writeFile(path, "version=garbage\npalette=banana\n");
		const UserSettings malformed = store.load();
		check(malformed.visual.palette == PaletteId::Cyan && malformed.simulation.itemCount == 80, "malformed settings safely fall back to defaults");
		writeFile(path, "version=3\npalette=light\nspacing=comfortable\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=1001\nprofile=normalized_float\nshell_gap_sequence=halving\naudio_master=nan\n");
		const UserPreferences malformedAudio = store.load();
		check(malformedAudio.visual.palette == PaletteId::Light && malformedAudio.audio.masterLevel == defaults.audio.masterLevel,
			"malformed audio values fall back to audio defaults without discarding valid app preferences");
		writeFile(path, "version=6\npalette=light\nspacing=comfortable\npanel_expanded=0\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nreconstructed_master=nan\nreconstructed_minimum_hz=9000\n");
		const UserPreferences malformedReconstructed = store.load();
		check(malformedReconstructed.visual.palette == PaletteId::Light &&
			malformedReconstructed.reconstructedAudio.masterLevel == defaults.reconstructedAudio.masterLevel &&
			malformedReconstructed.reconstructedAudio.minimumFrequencyHz == defaults.reconstructedAudio.minimumFrequencyHz,
			"malformed reconstructed audio settings fall back without discarding valid appearance settings");
		writeFile(path, "version=3\npalette=light\nspacing=comfortable\npanel_expanded=0\nitem_count=" +
			std::to_string(sortinglogic::MaxItemCount + 1) +
			"\nduration_seconds=10\ndescending=1\nalgorithm=1001\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserSettings invalidRange = store.load();
		check(invalidRange.visual.palette == PaletteId::Cyan && invalidRange.visual.spacing == BarSpacingMode::Compact, "out-of-range settings safely fall back to defaults");
		DeleteFileW(path.c_str());
		check(store.load().simulation.itemCount == 80, "deleted settings return defaults");
	}

	void testFractionalDurationStepPersists()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const sortinglogic::CommandResult adjusted = app.dispatch(AppAction(AppActionKind::AdjustDuration, 0.1f));
		check(adjusted.accepted && std::fabs(app.snapshot().durationSeconds - 10.1f) < 0.0001f &&
			std::fabs(persistence.value.simulation.durationSeconds - 10.1f) < 0.0001f,
			"fractional duration steps update the simulation and persist without integer truncation");
		check(adjusted.message.find("10.1 seconds") != std::string::npos,
			"duration feedback reports the fraction that was actually applied");
	}

	void testAppControllerDispatchAndPersistence()
	{
		MemorySettings persistence;
		AppController app(persistence);
		sortinglogic::RenderSnapshot reusableSnapshot;
		app.snapshotInto(reusableSnapshot);
		const sortinglogic::RenderSnapshot initialSnapshot = app.snapshot();
		check(reusableSnapshot.cursor == initialSnapshot.cursor && reusableSnapshot.eventCount == initialSnapshot.eventCount &&
			reusableSnapshot.durationSeconds == initialSnapshot.durationSeconds && reusableSnapshot.status == initialSnapshot.status &&
			reusableSnapshot.state.slots.size() == initialSnapshot.state.slots.size(),
			"AppController fills a reusable render snapshot with the same initial presentation state");
		check(persistence.writes == 0, "loading preferences does not write them back at startup");

		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		app.dispatch(AppAction(AppActionKind::PreviousEvent));
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.5f));
		check(persistence.writes == 0, "rejected transport and timeline actions never persist settings");

		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Cyan)));
		check(persistence.writes == 0, "reselecting the current palette does not write settings");
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		check(persistence.writes == 1 && persistence.value.visual.palette == PaletteId::Amber,
			"actual palette changes persist exactly once");
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		check(persistence.writes == 1, "repeating an unchanged preference does not write again");
		app.dispatch(AppAction(AppActionKind::SetSpacing, 0.0f, static_cast<int>(BarSpacingMode::None)));
		app.dispatch(AppAction(AppActionKind::TogglePanelExpanded));
		check(persistence.writes == 3 && !persistence.value.visual.panelExpanded,
			"spacing and panel state changes persist through the controller");
		const sortinglogic::CommandResult andrutArrangement = app.dispatch(AppAction(AppActionKind::SetInputPattern,
			0.0f, static_cast<int>(sortinglogic::InputPattern::AndrutReference)));
		check(andrutArrangement.accepted && app.snapshot().itemCount == 100 &&
			app.preferences().simulation.domain == sortinglogic::InputDomain::NormalizedFloats &&
			app.preferences().simulation.pattern == sortinglogic::InputPattern::AndrutReference,
			"selecting the Andrut arrangement immediately selects its 100-bar normalized input");
		check(persistence.writes == 4, "Andrut arrangement persists its item count and input profile as one preference change");
		app.dispatch(AppAction(AppActionKind::SetInputPattern, 0.0f, static_cast<int>(sortinglogic::InputPattern::Random)));

		app.dispatch(AppAction(AppActionKind::SetDirection, 0.0f, 1));
		app.dispatch(AppAction(AppActionKind::IncreaseCountFine));
		app.dispatch(AppAction(AppActionKind::IncreaseDuration));
		check(persistence.writes == 8 && persistence.value.simulation.itemCount == 101 &&
			persistence.value.simulation.durationSeconds == 11 && persistence.value.simulation.descending,
			"simulation configuration changes update the saved preference snapshot");
		app.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.63f, 0, AudioParameterId::MasterLevel));
		check(persistence.writes == 9 && persistence.value.audio.masterLevel == 0.63f,
			"audio preference changes persist immediately through the app controller");
		app.dispatch(AppAction(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::MasterLevel, 0.67f));
		check(persistence.writes == 10 && std::fabs(persistence.value.reconstructedAudio.masterLevel - 0.67) < 1e-6,
			"reconstructed audio preferences persist immediately through the app controller");
		app.dispatch(AppAction(AppActionKind::SelectAudioBand, 0.0f, 20));
		app.dispatch(AppAction(AppActionKind::RetryAudioDevice));
		check(persistence.writes == 10, "band selection and device retry do not write user settings");

		const sortinglogic::CommandResult run = app.dispatch(AppAction(AppActionKind::Run));
		check(run.accepted && persistence.writes == 10, "Run begins trace construction without an unnecessary settings write");
		const sortinglogic::CommandResult busy = app.dispatch(AppAction(AppActionKind::InstaInvert));
		check(!busy.accepted && persistence.writes == 10, "busy setup command is rejected without persistence");
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "controller receives and publishes the worker result on update");
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.5f));
		app.dispatch(AppAction(AppActionKind::NextEvent));
		app.dispatch(AppAction(AppActionKind::Reset));
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(persistence.writes == 10, "scrubbing, stepping, reset, and playback never write settings");
		app.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.71f, 0, AudioParameterId::BedLevel));
		check(app.snapshot().playing && persistence.writes == 11 && persistence.value.audio.bedLevel == 0.71f,
			"audio parameters remain adjustable and persist while trace playback is active");
		app.update(0.25f);
		const std::size_t progressedCursor = app.snapshot().cursor;
		check(app.snapshot().playing && progressedCursor > 0,
			"playback advances while live audio settings are changed");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(!app.snapshot().playing && app.snapshot().cursor == progressedCursor,
			"Pause stops playback without changing the current event cursor");
		app.dispatch(AppAction(AppActionKind::NextEvent));
		check(app.snapshot().cursor == progressedCursor + 1 && !app.snapshot().playing,
			"single-event stepping remains available while paused");
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.4f));
		const std::size_t seekCursor = static_cast<std::size_t>(0.4f * static_cast<double>(app.snapshot().eventCount));
		check(app.snapshot().cursor == seekCursor && !app.snapshot().playing,
			"timeline seeking reconstructs its destination while remaining paused");
		app.dispatch(AppAction(AppActionKind::Reset));
		check(app.snapshot().cursor == 0 && !app.snapshot().playing && persistence.writes == 11,
			"Reset restores the event cursor and array baseline without persisting transport state");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(app.snapshot().playing, "playback can restart after Reset");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(!app.snapshot().playing && persistence.writes == 11,
			"pausing after restart does not persist transport state");

		wchar_t temporary[MAX_PATH] = {};
		const DWORD tempCount = GetTempPathW(MAX_PATH, temporary);
		check(tempCount > 0 && tempCount < MAX_PATH, "resolve temporary directory for app restart persistence");
		const std::wstring restartPath = std::wstring(temporary) + L"agss-app-restart-tests-" +
			std::to_wstring(GetCurrentProcessId()) + L".ini";
		DeleteFileW(restartPath.c_str());
		{
			SettingsStore firstStore(restartPath);
			AppController firstRun(firstStore);
			firstRun.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Light)));
			firstRun.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.68f, 0, AudioParameterId::MasterLevel));
		}
		{
			SettingsStore restartedStore(restartPath);
			AppController restartedApp(restartedStore);
			check(restartedApp.preferences().visual.palette == PaletteId::Light &&
				restartedApp.preferences().audio.masterLevel == 0.68f,
				"controller preferences survive settings-file reload in a fresh app instance");
		}
		DeleteFileW(restartPath.c_str());
	}

	void testAppearanceApplyIsTransactionalAndTransportNeutral()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.dispatch(AppAction(AppActionKind::BuildTrace)).accepted, "appearance transaction fixture starts trace generation");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.4f));
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		const sortinglogic::RenderSnapshot before = app.snapshot();
		UiAppearancePreferences draft = app.preferences().appearance;
		draft.activePreset = UiPresetId::FlatVgaDos;
		draft.themes[UiPresetId::FlatVgaDos].palette = PaletteId::Amber;
		std::string validationError;
		check(setUiTokenValue(UiPresetId::FlatVgaDos, draft.themes[UiPresetId::FlatVgaDos],
			"scrollbar.show_arrows", UiTokenValue::fromBoolean(false), &validationError),
			"appearance apply fixture has a valid VGA draft");
		AppAction apply(AppActionKind::ApplyAppearance);
		apply.appearance.reset(new UiAppearancePreferences(draft));
		persistence.failWrites = true;
		const sortinglogic::CommandResult failed = app.dispatch(apply);
		check(!failed.accepted && app.preferences().appearance.activePreset == UiPresetId::Current &&
			app.snapshot().hasTrace == before.hasTrace && app.snapshot().cursor == before.cursor && app.snapshot().playing == before.playing,
			"appearance save failure leaves saved preferences and live transport unchanged");
		persistence.failWrites = false;
		const sortinglogic::CommandResult applied = app.dispatch(apply);
		const sortinglogic::RenderSnapshot after = app.snapshot();
		check(applied.accepted && app.preferences().appearance.activePreset == UiPresetId::FlatVgaDos &&
			app.preferences().appearance.themes.at(UiPresetId::Current).palette == PaletteId::Cyan &&
			app.preferences().appearance.themes.at(UiPresetId::FlatVgaDos).palette == PaletteId::Amber,
			"successful appearance apply commits the selected preset while keeping the other base customization");
		check(after.hasTrace == before.hasTrace && after.cursor == before.cursor && after.playing == before.playing,
			"applying appearance preserves trace, cursor, and playback state");
	}

	void testRestoreReconstructedDefaultsScope()
	{
		MemorySettings persistence;
		AppController app(persistence);
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		app.dispatch(AppAction(AppActionKind::IncreaseCountFine));
		app.dispatch(AppAction(AppActionKind::SetDirection, 0.0f, 1));
		app.dispatch(AppAction(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::MinimumFrequencyHz, 140.0f));
		app.dispatch(AppAction(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -12.0f,
			static_cast<int>(AlgorithmAudioChannel::Comparison)));
		const int writesBeforeRestore = persistence.writes;
		const sortinglogic::CommandResult restored = app.dispatch(AppAction(AppActionKind::RestoreReconstructedDefaults));
		const SoundEngine::ReconstructedParameters defaults;
		check(restored.accepted && persistence.writes == writesBeforeRestore + 1,
			"restoring reconstructed defaults persists the changed audio profile once");
		check(app.preferences().reconstructedAudio.minimumFrequencyHz == defaults.minimumFrequencyHz &&
			app.preferences().variantAudioOffsets.empty() && app.preferences().familyAudioDefaults.empty(),
			"restoring reconstructed defaults clears timbre edits and algorithm gain overrides");
		check(app.preferences().visual.palette == PaletteId::Amber && app.snapshot().itemCount == 81 &&
			app.snapshot().direction == sortinglogic::SortDirection::Descending,
			"restoring reconstructed defaults preserves appearance and simulation configuration");
	}

	void testMailboxIdlePollingAndConcurrentPublication()
	{
		LatestAudioStateMailbox mailbox;
		AudioPresentationState received;
		for (unsigned i = 0; i < 1000; ++i)
			check(!mailbox.consumeLatest(received), "empty polling does not acquire a buffer slot");
		const std::uint64_t count = 100000;
		std::atomic<bool> done(false);
		std::thread producer([&] {
			for (std::uint64_t revision = 1; revision <= count; ++revision)
			{
				AudioPresentationState state;
				state.revision = revision;
				state.transport.revision = revision;
				state.transport.timelineFrame = revision * 147;
				state.parameters.bandTrimDb.fill(static_cast<float>(revision));
				mailbox.publish(state);
				if (revision % 17 == 0) std::this_thread::yield();
			}
			done.store(true, std::memory_order_release);
		});
		std::uint64_t last = 0;
		bool coherent = true;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		for (;;)
		{
			if (std::chrono::steady_clock::now() >= deadline) break;
			if (mailbox.consumeLatest(received))
			{
				coherent = coherent && received.revision > last && received.transport.revision == received.revision &&
					received.transport.timelineFrame == received.revision * 147;
				for (float trim : received.parameters.bandTrimDb)
					coherent = coherent && trim == static_cast<float>(received.revision);
				last = received.revision;
			}
			else if (done.load(std::memory_order_acquire))
			{
				// Acquire the final publication after observing producer completion.
				if (last == count) break;
			}
		}
		producer.join();
		check(coherent && last == count, "idle callback polls preserve exclusive slots and coherent newest snapshots under concurrent publication");
	}

	void testDeviceRecoveryAndSeekAcknowledgement()
	{
		sortinglogic::Simulation simulation(20, 0x713u);
		sortinglogic::TraceBuildRequest request;
		simulation.beginTraceBuild(request);
		sortinglogic::TraceBuildResult result;
		result.succeeded = true;
		result.trace = sortinglogic::SortTraceBuilder::build(request);
		simulation.completeTraceBuild(std::move(result));
		AudioPresentationBridge bridge;
		LatestAudioStateMailbox mailbox;
		SoundEngine::Parameters parameters;
		SoundEngine::ReconstructedParameters reconstructed;
		const auto mode = SoundEngine::PlaybackTimingMode::FixedDuration;
		bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, true);
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		simulation.seekTo(simulation.snapshot().eventCount / 2);
		bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, true);
		check(!bridge.playheadReady(AudioTimelineAcknowledgement(bridge.timelineGeneration(), ~std::uint64_t(0))),
			"a preparing seek rejects playhead feedback even with an older callback acknowledgement");
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		AudioPresentationState prepared;
		check(mailbox.consumeLatest(prepared) && prepared.seekState && prepared.seekRevision > 0,
			"mid-trace inspection produces one prepared seek snapshot");
		check(!bridge.playheadReady(AudioTimelineAcknowledgement(prepared.timelineGeneration, prepared.seekRevision - 1)) &&
			bridge.playheadReady(AudioTimelineAcknowledgement(prepared.timelineGeneration, prepared.seekRevision)),
			"temporary stopped feedback stays fenced until the callback acknowledges the installed seek");
		const auto revision = bridge.transportRevision();
		for (unsigned frame = 0; frame < 100; ++frame)
			bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, true);
		check(!bridge.timelinePreparing() && bridge.transportRevision() == revision,
			"finishing preparation is not mistaken for device recovery and does not start another seek");
		bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, false);
		bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, true);
		check(bridge.timelinePreparing() && bridge.transportRevision() == revision + 1,
			"a real device recovery reconstructs the inspected position once");
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		for (unsigned frame = 0; frame < 100; ++frame)
			bridge.update(simulation, simulation.playbackState(), parameters, mailbox, reconstructed, mode, true);
		check(!bridge.timelinePreparing() && bridge.transportRevision() == revision + 1,
			"device recovery settles instead of recursively preparing audio");
	}

	void testAudioPresentationBridgeAndMailbox()
	{
		LatestAudioStateMailbox mailbox;
		AudioPresentationState latest;
		for (std::uint64_t revision = 1; revision <= 100; ++revision)
		{
			AudioPresentationState state;
			state.revision = revision;
			state.parameters.masterLevel = static_cast<float>(revision) / 100.0f;
			mailbox.publish(state);
		}
		check(mailbox.consumeLatest(latest) && latest.revision == 100 && latest.parameters.masterLevel == 1.0f,
			"latest-state mailbox coalesces pending updates to the newest complete snapshot");
		check(!mailbox.consumeLatest(latest), "mailbox does not replay stale snapshots after consumption");

		sortinglogic::Simulation simulation(5, 0x1234u);
		simulation.prepareInstaSort();
		sortinglogic::TraceBuildRequest request;
		check(simulation.beginTraceBuild(request).accepted, "test simulation starts a trace build");
		sortinglogic::TraceBuildResult built;
		built.succeeded = true;
		built.trace = sortinglogic::SortTraceBuilder::build(request);
		simulation.completeTraceBuild(std::move(built));
		AudioPresentationBridge bridge;
		LatestAudioStateMailbox traceMailbox;
		SoundEngine::Parameters parameters;
		sortinglogic::RenderSnapshot snapshot = simulation.snapshot();
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		waitForAudioTimeline(bridge, simulation, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"loaded trace publishes the continuous bed with no initial comparison focus");
		check(latest.timeline && latest.timeline->ready() && latest.timeline->sourceEventCount() == snapshot.eventCount &&
			latest.timeline->eligibleEventCount() == simulation.currentTrace()->comparisonEventIndices.size() &&
			latest.timeline->mode() == SoundEngine::PlaybackTimingMode::FixedDuration,
			"trace publication includes an immutable sound timeline with all source events and eligible comparisons");
		check(latest.transport.generation == latest.timelineGeneration && !latest.transport.playing &&
			!latest.transport.holdSelection,
			"initial audio transport installs paused at cursor zero without starting comparison births");
		const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation.currentTrace();
		std::size_t firstCompareCursor = 0;
		for (std::size_t i = 0; i < trace->events.size(); ++i)
			if (trace->events[i].type == sortinglogic::SemanticEventType::Compare) { firstCompareCursor = i + 1; break; }
		check(firstCompareCursor > 0, "Insertion Sort trace contains comparisons for audio mapping");
		simulation.seekTo(firstCompareCursor);
		snapshot = simulation.snapshot();
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		waitForAudioTimeline(bridge, simulation, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second &&
			latest.comparison.secondRole == SoundEngine::ComparisonRole::HeldKey,
			"comparison event maps both items and preserves the held-key participant");
		check(latest.seekState && latest.seekState->valid &&
			latest.seekState->targetCursor == firstCompareCursor &&
			latest.seekState->timelineGeneration == latest.timelineGeneration,
			"paused cursor selection publishes a generation-tagged prepared seek snapshot for callback installation");
		SoundEngine::AudioTimelineEvent timelineComparison;
		check(latest.timeline->comparisonAtCursor(firstCompareCursor, timelineComparison) &&
			timelineComparison.sourceCursor == firstCompareCursor && latest.transport.holdSelection &&
			latest.transport.sourceCursor == firstCompareCursor,
			"paused event stepping keeps an exact cursor-selected comparison in the prepared audio timeline");
		check(latest.burstComparison.firstPosition == latest.comparison.firstPosition &&
			latest.burstComparison.secondPosition == latest.comparison.secondPosition &&
			latest.burstComparison.secondRole == static_cast<std::uint8_t>(SoundEngine::ComparisonRole::HeldKey),
			"the reconstructed audio bridge preserves normalized values and semantic roles");
		const sortinglogic::SemanticEvent& firstCompare = trace->events[firstCompareCursor - 1];
		float firstValue = 0.0f, secondValue = 0.0f, minimum = trace->input[0].value, maximum = minimum;
		for (std::size_t i = 0; i < trace->input.size(); ++i)
		{
			minimum = minimum < trace->input[i].value ? minimum : trace->input[i].value;
			maximum = maximum > trace->input[i].value ? maximum : trace->input[i].value;
			if (trace->input[i].stableId == firstCompare.firstItemId) firstValue = trace->input[i].value;
			if (trace->input[i].stableId == firstCompare.secondItemId) secondValue = trace->input[i].value;
		}
		check(std::fabs(latest.comparison.firstPosition - SoundEngine::normalizeValue(firstValue, minimum, maximum)) < 1e-6f &&
			std::fabs(latest.comparison.secondPosition - SoundEngine::normalizeValue(secondValue, minimum, maximum)) < 1e-6f,
			"comparison values normalize against the prepared input range");
		sortinglogic::Simulation gnome(8, 0x6A04u);
		check(gnome.selectAlgorithm(sortinglogic::SortAlgorithm::Gnome).accepted, "Gnome Sort selects for its reconstructed operand-order check");
		gnome.prepareInstaSort();
		sortinglogic::TraceBuildRequest gnomeRequest;
		check(gnome.beginTraceBuild(gnomeRequest).accepted, "Gnome trace starts for audio operand-order mapping");
		sortinglogic::TraceBuildResult gnomeBuilt;
		gnomeBuilt.succeeded = true;
		gnomeBuilt.trace = sortinglogic::SortTraceBuilder::build(gnomeRequest);
		gnome.completeTraceBuild(std::move(gnomeBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> gnomeTrace = gnome.currentTrace();
		std::size_t gnomeCompareCursor = 0;
		while (gnomeCompareCursor < gnomeTrace->events.size() &&
			gnomeTrace->events[gnomeCompareCursor].type != sortinglogic::SemanticEventType::Compare) ++gnomeCompareCursor;
		if (gnomeCompareCursor < gnomeTrace->events.size())
		{
			gnome.seekTo(gnomeCompareCursor + 1);
			AudioPresentationBridge gnomeBridge;
			LatestAudioStateMailbox gnomeMailbox;
			gnomeBridge.update(gnome, gnome.playbackState(), parameters, gnomeMailbox);
			waitForAudioTimeline(gnomeBridge, gnome, parameters, gnomeMailbox);
			AudioPresentationState gnomeState;
			const bool gnomePublished = gnomeMailbox.consumeLatest(gnomeState);
			check(gnomePublished &&
				gnomeState.burstComparison.firstPosition == gnomeState.comparison.secondPosition &&
				gnomeState.burstComparison.secondPosition == gnomeState.comparison.firstPosition,
				"Gnome current audio operands reverse according to catalog metadata");
			const bool gnomeTimelineLookup = gnomeState.timeline &&
				gnomeState.timeline->comparisonAtCursor(gnomeCompareCursor + 1, timelineComparison);
			check(gnomeTimelineLookup && timelineComparison.sourceCursor == gnomeCompareCursor + 1,
				"Gnome prepared timeline retains the eligible comparison at its source cursor");
			check(gnomeTimelineLookup &&
				std::fabs(timelineComparison.firstPosition - gnomeState.burstComparison.firstPosition) < 1.0e-6 &&
				std::fabs(timelineComparison.secondPosition - gnomeState.burstComparison.secondPosition) < 1.0e-6,
				"Gnome timeline operands use the catalog's audio order");
		}
		SoundEngine::ReconstructedParameters releasePreferences;
		releasePreferences.idlePolicy = SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox, releasePreferences);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison && latest.releaseSelection && !latest.transportPlaying,
			"paused inspection publishes the configured special-release policy to the audio callback");
		const SoundEngine::PreparedAudioTimeline* stableTimeline = latest.timeline;
		parameters.heldKeyBoostDb = 0.0f;
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 0.0f &&
			latest.timeline == stableTimeline && std::fabs(latest.timelineGains.heldKeyGain - 1.0) < 1.0e-9 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio bridge applies live held-key gain changes without rebuilding the immutable timeline");
		parameters.heldKeyBoostDb = 18.0f;
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 18.0f &&
			std::fabs(latest.timelineGains.heldKeyGain - std::pow(10.0, 18.0 / 20.0)) < 1.0e-6 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio callback mailbox receives the full held-key offset range for a held comparison");

		const std::size_t laterCursor = trace->events.size() / 2;
		simulation.seekTo(laterCursor);
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] > 0,
			"coalesced frame summary includes movement events crossed during playback");
		simulation.seekTo(firstCompareCursor);
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] == 0 &&
			std::fabs(latest.comparison.firstPosition - SoundEngine::normalizeValue(firstValue, minimum, maximum)) < 1e-6f,
			"rewinding restores the comparison selected by the destination cursor");
		simulation.seekTo(trace->events.size());
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"completed trace retains the bed and clears comparison focus");
		simulation.reset();
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"reset retains the loaded bed and clears the comparison");

		sortinglogic::Simulation counting(32, 0xC017u, sortinglogic::InputProfile::IntegerKeys);
		check(counting.selectAlgorithm(sortinglogic::SortAlgorithm::Counting).accepted,
			"Counting Sort accepts an integer-key test input for pitch normalization");
		sortinglogic::TraceBuildRequest countingRequest;
		check(counting.beginTraceBuild(countingRequest).accepted, "Counting trace starts for workspace pitch mapping");
		sortinglogic::TraceBuildResult countingBuilt;
		countingBuilt.succeeded = true;
		countingBuilt.trace = sortinglogic::SortTraceBuilder::build(countingRequest);
		counting.completeTraceBuild(std::move(countingBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> countingTrace = counting.currentTrace();
		std::size_t firstCountingProbe = countingTrace->events.size();
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::Probe) { firstCountingProbe = i; break; }
		if (firstCountingProbe < countingTrace->events.size() && countingTrace->comparisonEventIndices.empty())
		{
			counting.seekTo(firstCountingProbe + 1);
			AudioPresentationBridge probeBridge;
			LatestAudioStateMailbox probeMailbox;
			probeBridge.update(counting, counting.playbackState(), parameters, probeMailbox);
			AudioPresentationState probeState;
			check(probeMailbox.consumeLatest(probeState) && probeState.hasComparison && probeState.burstComparison.probeOnly &&
				probeState.burstComparison.firstRole == static_cast<std::uint8_t>(SoundEngine::ComparisonRole::ProbedItem),
				"comparison-free distribution traces sonify real item probes as single-participant notes");
		}
		std::size_t counterWorkspaceSize = 0;
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::ResizeCounterWorkspace)
				counterWorkspaceSize = countingTrace->events[i].afterSize;
		std::size_t upperBucketWrite = countingTrace->events.size();
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::CounterWrite && counterWorkspaceSize > 1 &&
				countingTrace->events[i].firstIndex == counterWorkspaceSize - 1)
			{ upperBucketWrite = i; break; }
		check(counterWorkspaceSize > 1 && upperBucketWrite < countingTrace->events.size(),
			"Counting trace exposes a write at the end of its counter workspace");
		if (upperBucketWrite < countingTrace->events.size())
		{
			counting.seekTo(upperBucketWrite + 1);
			AudioPresentationBridge countingBridge;
			LatestAudioStateMailbox countingMailbox;
			countingBridge.update(counting, counting.playbackState(), parameters, countingMailbox);
			check(countingMailbox.consumeLatest(latest) &&
				std::fabs(latest.eventAccents.representativePositions[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] - 1.0f) < 1e-6f,
				"counter accents normalize against the counter workspace range rather than the primary array");
		}
		simulation.prepareShuffle(0x4321u);
		bridge.update(simulation, simulation.playbackState(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && !latest.traceLoaded && !latest.hasComparison,
			"preparing a replacement input fades the loaded trace bed out");

		sortinglogic::Simulation dense(800, 0x87654321u);
		check(dense.setDuration(1.0f).accepted, "800-item integration playback uses the minimum run duration");
		dense.prepareInstaInvert();
		sortinglogic::TraceBuildRequest denseRequest;
		check(dense.beginTraceBuild(denseRequest).accepted, "800-item reverse trace starts for audio-path stress coverage");
		sortinglogic::TraceBuildResult denseBuilt;
		denseBuilt.succeeded = true;
		denseBuilt.trace = sortinglogic::SortTraceBuilder::build(denseRequest);
		dense.completeTraceBuild(std::move(denseBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> denseTrace = dense.currentTrace();
		std::size_t denseComparisonEvents = 0;
		if (denseTrace)
			for (std::size_t i = 0; i < denseTrace->events.size(); ++i)
				if (denseTrace->events[i].type == sortinglogic::SemanticEventType::Compare) ++denseComparisonEvents;
		const std::string denseTraceMessage = std::string("800-item reverse trace contains dense ordered comparison events (actual ") +
			(denseTrace ? std::to_string(denseTrace->events.size()) + ", comparisons " + std::to_string(denseComparisonEvents) +
				", complete " + (denseTrace->complete ? "true" : "false") + ", reason " + denseTrace->completionReason : "no trace") + ")";
		const bool denseTraceRespectsBudget = denseTrace && !denseTrace->complete &&
			denseTrace->completionReason.find("128 MiB") != std::string::npos;
		check(denseTrace && denseTrace->events.size() > 100000 && denseComparisonEvents > 50000 && denseTraceRespectsBudget,
			"800-item reverse trace remains dense and returns a playable prefix at the accounted memory cap");
		AudioPresentationBridge denseBridge;
		LatestAudioStateMailbox denseMailbox;
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		denseMailbox.consumeLatest(latest);
		const std::size_t middleCursor = denseTrace->events.size() / 2;
		dense.seekTo(middleCursor);
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"dense 800-item seek publishes the current comparison without waiting for skipped events");
		const std::uint64_t middleRevision = latest.revision;
		dense.seekTo(denseTrace->events.size() * 3 / 4);
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.revision > middleRevision && latest.hasComparison,
			"dense forward playback replaces the prior mailbox state with the latest comparison");
		dense.seekTo(denseTrace->events.size());
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"dense completion leaves only the bed in the audio state");
		dense.reset();
		dense.togglePlayback();
		dense.advance(0.5f);
		const sortinglogic::RenderSnapshot halfway = dense.snapshot();
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		check(halfway.playing && halfway.cursor == denseTrace->events.size() / 2 &&
			denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"800-item playback reaches the halfway comparison without an audio-event backlog");
		dense.advance(0.5f);
		denseBridge.update(dense, dense.playbackState(), parameters, denseMailbox);
		check(dense.snapshot().complete && denseMailbox.consumeLatest(latest) &&
			latest.traceLoaded && !latest.hasComparison,
			"800-item playback completes on schedule and leaves the harmonic bed running");
	}

	void testAccentAudioActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const std::size_t distribution = static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution);
		const sortinglogic::CommandResult changed = app.dispatch(AppAction(AppActionKind::SetAudioParameter,
			0.27f, 0, AudioParameterId::AccentGain, static_cast<int>(distribution)));
		check(changed.accepted && persistence.writes == 1 &&
			std::fabs(persistence.value.audio.accentGain[distribution] - 0.27f) < 1e-6f,
			"indexed semantic-event gain action updates and persists its category");
		const sortinglogic::CommandResult muted = app.dispatch(AppAction(AppActionKind::SetAudioParameter,
			0.0f, 0, AudioParameterId::AccentRouting, static_cast<int>(distribution)));
		check(muted.accepted && persistence.writes == 2 && !persistence.value.audio.accentRouting[distribution],
			"indexed semantic-event routing action persists the category toggle");
	}

	void testVariantAudioLevels()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const SoundEngine::Parameters inherited = app.effectiveAudio();
		check(std::fabs(inherited.comparisonBoostDb) < 1e-6f,
			"a new variant inherits the reconstructed model's neutral family comparison level");
		const AppAction offset(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -12.0f,
			static_cast<int>(AlgorithmAudioChannel::Comparison));
		check(app.dispatch(offset).accepted && persistence.writes == 1 &&
			std::fabs(app.effectiveAudio().comparisonBoostDb - (inherited.comparisonBoostDb - 12.0f)) < 1e-5f,
			"variant operation levels apply a live dB offset and persist by stable IDs");
		const AppAction muteHeld(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -60.0f,
			static_cast<int>(AlgorithmAudioChannel::HeldKeyRole));
		check(app.dispatch(muteHeld).accepted && app.effectiveAudio().heldKeyBoostDb <= -53.9f,
			"the -60 dB variant offset effectively mutes held-key emphasis while preserving inheritance");
		check(app.dispatch(AppAction(AppActionKind::ResetVariantAudioLevels, "insertion-methods", "insertion")).accepted &&
			persistence.writes == 3 && std::fabs(app.effectiveAudio().comparisonBoostDb - inherited.comparisonBoostDb) < 1e-6f,
			"reset removes variant overrides and restores family defaults");
	}

	void testShowcaseActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const std::uint32_t seed = app.snapshot().inputSeed;
		check(app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::IntegerKeys))).accepted,
			"integer profile can be selected from the app action layer");
		check(persistence.writes == 1 && app.snapshot().inputSeed == seed &&
			app.snapshot().profile == sortinglogic::InputProfile::IntegerKeys,
			"profile change persists and regenerates from the existing seed");
		sortinglogic::Simulation expected(80, seed, sortinglogic::InputProfile::IntegerKeys);
		bool generatedMatches = expected.preparedInput().size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; generatedMatches && i < expected.preparedInput().size(); ++i)
			generatedMatches = expected.preparedInput()[i].stableId == app.simulation().preparedInput()[i].stableId &&
				expected.preparedInput()[i].integerKey == app.simulation().preparedInput()[i].integerKey;
		check(generatedMatches, "profile generation matches a fresh simulation using the saved seed");

		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "distribution-sorts", "counting")).accepted,
			"Counting Sort is selectable for integer keys");
		check(app.preferences().simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Counting) &&
			app.preferences().simulation.familyId == "distribution-sorts" && app.preferences().simulation.variantId == "counting",
			"selected algorithm persists with stable family and variant IDs");
		check(app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::NormalizedFloat))).accepted,
			"profile can change while setup is idle");
		check(app.snapshot().algorithm == sortinglogic::SortAlgorithm::Counting && !app.snapshot().runCompatible,
			"incompatible selection is retained after a profile change");
		const sortinglogic::CommandResult rejectedRun = app.dispatch(AppAction(AppActionKind::Run));
		check(!rejectedRun.accepted && rejectedRun.message.find("Normalized floats") != std::string::npos,
			"incompatible Run command explains the required profile");
		sortinglogic::Simulation expectedFloat(80, seed, sortinglogic::InputProfile::NormalizedFloat);
		bool sameFloatInput = expectedFloat.preparedInput().size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; sameFloatInput && i < expectedFloat.preparedInput().size(); ++i)
			sameFloatInput = expectedFloat.preparedInput()[i].stableId == app.simulation().preparedInput()[i].stableId &&
				expectedFloat.preparedInput()[i].value == app.simulation().preparedInput()[i].value &&
				!app.simulation().preparedInput()[i].hasIntegerKey;
		check(sameFloatInput, "profile regeneration is deterministic for the retained seed");
		const std::vector<sortinglogic::SortItem> floatInput = app.simulation().preparedInput();
		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "merge-sort", "merge")).accepted,
			"a compatible comparison algorithm can replace an incompatible selection");
		bool preservedAlgorithmInput = floatInput.size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; preservedAlgorithmInput && i < floatInput.size(); ++i)
			preservedAlgorithmInput = floatInput[i].stableId == app.simulation().preparedInput()[i].stableId &&
				floatInput[i].value == app.simulation().preparedInput()[i].value;
		check(preservedAlgorithmInput, "changing algorithms keeps the prepared data untouched");
		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "insertion-methods", "shell")).accepted,
			"Shell Sort can be selected in the normalized-float profile");
		check(!app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "unregistered", "missing")).accepted,
			"unregistered family and variant IDs are rejected safely");
		check(app.dispatch(AppAction(AppActionKind::SetShellGapSequence, 0.0f,
			static_cast<int>(sortinglogic::ShellGapSequence::Ciura))).accepted &&
			algorithmOptionValue(app.preferences(), *sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell), "gap_sequence") ==
			static_cast<int>(sortinglogic::ShellGapSequence::Ciura),
			"Shell sequence selection dispatches and persists");
		const sortinglogic::AlgorithmDescriptor* shell = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
		check(shell && app.dispatch(AppAction(AppActionKind::SetAlgorithmOption, shell->familyId, shell->variantId,
			"gap_sequence", "tokuda", static_cast<int>(sortinglogic::ShellGapSequence::Tokuda))).accepted &&
			algorithmOptionValue(app.preferences(), *shell, "gap_sequence") == static_cast<int>(sortinglogic::ShellGapSequence::Tokuda) &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(shell->familyId, shell->variantId, "gap_sequence"))->second == "tokuda",
			"catalog option dispatch applies and persists a stable Shell gap option ID");
		const sortinglogic::CommandResult run = app.dispatch(AppAction(AppActionKind::Run));
		check(run.accepted, "compatible Shell Sort run begins");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "Shell trace completes through the existing single-job service");
		const std::shared_ptr<const sortinglogic::SortTrace> shellTrace = app.simulation().currentTrace();
		check(shellTrace && shellTrace->shellGapSequence == sortinglogic::ShellGapSequence::Tokuda &&
			shellTrace->configuration.shellGapSequence == sortinglogic::ShellGapSequence::Tokuda,
			"stable Shell option selection is resolved into the built trace");
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted, "showcase trace starts playback");
		const int writesBeforeBusyProfile = persistence.writes;
		check(!app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::IntegerKeys))).accepted && persistence.writes == writesBeforeBusyProfile,
			"profile changes are blocked during playback and do not persist");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
	}

	void testDistributionAlgorithmsSelectableFromDefaultInput()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.snapshot().domain == sortinglogic::InputDomain::NormalizedFloats,
			"distribution selection fixture starts in the default normalized-float domain");
		const sortinglogic::CommandResult counting = app.dispatch(AppAction(AppActionKind::SelectAlgorithm,
			"distribution-sorts", "counting"));
		check(counting.accepted && app.snapshot().algorithm == sortinglogic::SortAlgorithm::Counting &&
			app.snapshot().domain == sortinglogic::InputDomain::NonnegativeIntegerKeys &&
			app.snapshot().pattern == sortinglogic::InputPattern::Random &&
			app.preferences().simulation.variantId == "counting",
			"selecting Counting from the algorithm browser switches to its compatible integer domain and persists it");
		const sortinglogic::CommandResult radix = app.dispatch(AppAction(AppActionKind::SelectAlgorithm,
			"distribution-sorts", "radix"));
		check(radix.accepted && app.snapshot().algorithm == sortinglogic::SortAlgorithm::Radix &&
			app.snapshot().domain == sortinglogic::InputDomain::NonnegativeIntegerKeys &&
			app.preferences().simulation.variantId == "radix",
			"selecting Radix from the algorithm browser changes the active algorithm without losing its compatible domain");
	}

	void testMetadataAlgorithmOptions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const sortinglogic::AlgorithmDescriptor* merge = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		check(merge && app.dispatch(AppAction(AppActionKind::SelectAlgorithm, merge->familyId, merge->variantId)).accepted,
			"Merge can be selected before applying catalog options");
		const int writesBeforeUnchanged = persistence.writes;
		const AppAction unchangedBottomUp(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "bottom_up", static_cast<int>(sortinglogic::MergeExecutionMode::BottomUp));
		check(app.dispatch(unchangedBottomUp).accepted && persistence.writes == writesBeforeUnchanged,
			"choosing the current catalog option does not cause an unnecessary settings write");
		const int topDownValue = static_cast<int>(sortinglogic::MergeExecutionMode::TopDown);
		const AppAction topDown(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "top_down", topDownValue);
		check(app.dispatch(topDown).accepted && app.snapshot().mergeExecutionMode == sortinglogic::MergeExecutionMode::TopDown &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(merge->familyId, merge->variantId, "execution_mode"))->second == "top_down",
			"Merge execution order dispatches and persists through stable option metadata");
		const int animatedValue = static_cast<int>(sortinglogic::MergeWritebackMode::AnimatedWrites);
		const AppAction animated(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"writeback_mode", "animated_writes", animatedValue);
		check(app.dispatch(animated).accepted && app.snapshot().mergeWritebackMode == sortinglogic::MergeWritebackMode::AnimatedWrites &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(merge->familyId, merge->variantId, "writeback_mode"))->second == "animated_writes",
			"Merge writeback remains independent and uses its own stable option group");
		const int writesBeforeInvalid = persistence.writes;
		const AppAction invalid(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "missing_option", topDownValue);
		check(!app.dispatch(invalid).accepted && persistence.writes == writesBeforeInvalid,
			"unknown stable option IDs are rejected without persisting a change");
		check(app.dispatch(AppAction(AppActionKind::BuildTrace)).accepted,
			"Merge builds from its stable option selections");
		const std::chrono::steady_clock::time_point mergeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < mergeDeadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		const std::shared_ptr<const sortinglogic::SortTrace> mergeTrace = app.snapshot().hasTrace
			? app.simulation().currentTrace() : std::shared_ptr<const sortinglogic::SortTrace>();
		check(mergeTrace && mergeTrace->mergeExecutionMode == sortinglogic::MergeExecutionMode::TopDown &&
			mergeTrace->mergeWritebackMode == sortinglogic::MergeWritebackMode::AnimatedWrites &&
			mergeTrace->configuration.mergeExecutionMode == sortinglogic::MergeExecutionMode::TopDown &&
			mergeTrace->configuration.mergeWritebackMode == sortinglogic::MergeWritebackMode::AnimatedWrites,
			"stable Merge option selections resolve into the trace independently");

		const sortinglogic::AlgorithmDescriptor* baseN = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::ArrayvBaseNHeap);
		check(baseN && app.dispatch(AppAction(AppActionKind::SelectAlgorithm, baseN->familyId, baseN->variantId)).accepted,
			"Base-N Heap can be selected from its stable family and variant IDs");
		const AppAction baseEight(AppActionKind::SetAlgorithmOption, baseN->familyId, baseN->variantId,
			"heap_arity", "base_8", 8);
		check(app.dispatch(baseEight).accepted &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(baseN->familyId, baseN->variantId, "heap_arity"))->second == "base_8",
			"catalog-driven Base-N arity selection persists by stable option ID");
		check(app.dispatch(AppAction(AppActionKind::BuildTrace)).accepted,
			"Base-N trace starts after choosing a non-default arity");
		const std::chrono::steady_clock::time_point buildDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < buildDeadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		const std::shared_ptr<const sortinglogic::SortTrace> baseNTrace = app.snapshot().hasTrace
			? app.simulation().currentTrace() : std::shared_ptr<const sortinglogic::SortTrace>();
		check(baseNTrace && baseNTrace->bucketCount == 8 && baseNTrace->configuration.heapArity == 8 &&
			app.simulation().algorithmConfiguration().heapArity == 8,
			"AppController routes catalog-selected heap arity into the immutable trace request");

		const sortinglogic::AlgorithmDescriptor* rotateMerge = sortinglogic::algorithmDescriptor(
			sortinglogic::SortAlgorithm::ArrayvOptimizedRotateMerge);
		check(rotateMerge && algorithmOptionValue(app.preferences(), *rotateMerge, "rotate_buffer_capacity") == 64 &&
			app.dispatch(AppAction(AppActionKind::SelectAlgorithm, rotateMerge->familyId, rotateMerge->variantId)).accepted,
			"Optimized Rotate Merge exposes its pinned default 64-item temporary buffer");
		const AppAction rotateBuffer(AppActionKind::SetAlgorithmOption, rotateMerge->familyId, rotateMerge->variantId,
			"rotate_buffer_capacity", "items_16", 16);
		check(app.dispatch(rotateBuffer).accepted &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(rotateMerge->familyId, rotateMerge->variantId,
				"rotate_buffer_capacity"))->second == "items_16",
			"Optimized Rotate Merge buffer capacity dispatches and persists by stable option ID");
		check(app.dispatch(AppAction(AppActionKind::BuildTrace)).accepted,
			"Optimized Rotate Merge trace starts after selecting a configured buffer capacity");
		const std::chrono::steady_clock::time_point rotateDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < rotateDeadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		const std::shared_ptr<const sortinglogic::SortTrace> rotateTrace = app.snapshot().hasTrace
			? app.simulation().currentTrace() : std::shared_ptr<const sortinglogic::SortTrace>();
		check(rotateTrace && rotateTrace->rotateMergeBufferSize == 16 &&
			rotateTrace->configuration.rotateMergeBufferSize == 16,
			"AppController routes configured temporary-buffer capacity into the immutable trace request");

		const sortinglogic::AlgorithmDescriptor* grail = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::ArrayvGrail);
		check(grail && app.dispatch(AppAction(AppActionKind::SelectAlgorithm, grail->familyId, grail->variantId)).accepted &&
			app.dispatch(AppAction(AppActionKind::SetAlgorithmOption, grail->familyId, grail->variantId,
				"grail_buffer_mode", "static_buffer", 1)).accepted,
			"Grail's static buffer mode is selectable through the catalog-driven settings model");
		check(app.dispatch(AppAction(AppActionKind::BuildTrace)).accepted,
			"Grail trace starts after selecting a static merge buffer");
		const std::chrono::steady_clock::time_point grailDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < grailDeadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		const std::shared_ptr<const sortinglogic::SortTrace> grailTrace = app.snapshot().hasTrace
			? app.simulation().currentTrace() : std::shared_ptr<const sortinglogic::SortTrace>();
		check(grailTrace && grailTrace->bucketCount == 1 &&
			grailTrace->configuration.grailBufferMode == sortinglogic::GrailBufferMode::StaticBuffer,
			"AppController routes Grail's selected buffer mode into the immutable trace request");
	}

	void testPlaybackTimebase()
	{
		using namespace playbacktimebase;
		const std::vector<std::size_t> eligible = { 0, 2, 5, 8 };
		const double duration = durationForReferenceEvents(eligible.size());
		check(durationForReferenceEvents(0) == 0.0 && durationForReferenceEvents(300) == 1.0 && duration == 4.0 / 300.0,
			"reference cadence duration uses the shared 300-event timebase");
		check(elapsedForReferenceCursor(eligible, 0) == 0.0 &&
			std::fabs(elapsedForReferenceCursor(eligible, 3) - 2.0 / 300.0) < 1e-12 &&
			std::fabs(elapsedForReferenceCursor(eligible, 6) - 3.0 / 300.0) < 1e-12,
			"reference cursor elapsed time counts only eligible events before the cursor");
		check(cursorForReferenceElapsed(eligible, 10, 0.0, duration) == 1 &&
			cursorForReferenceElapsed(eligible, 10, 0.0034, duration) == 3 &&
			cursorForReferenceElapsed(eligible, 10, duration - 0.0001, duration) == 9 &&
			cursorForReferenceElapsed(eligible, 10, duration, duration) == 10,
			"reference elapsed time advances through eligible events and reaches the exact trace end");
		check(secondsFromReferenceFrames(ReferenceTimelineSampleRate) == 1.0 &&
			secondsFromReferenceFrames(ReferenceTimelineSampleRate / 2) == 0.5,
			"audio playhead frames share the 44.1 kHz reference timeline");
	}

	void testPlaybackCoordinator()
	{
		PlaybackCoordinator coordinator;
		check(coordinator.state(false, false, false, 0, 0) == PlaybackCoordinatorState::NoTrace,
			"playback coordinator starts without a trace");
		coordinator.traceBuildRequested(true);
		check(coordinator.state(true, false, false, 0, 0) == PlaybackCoordinatorState::Queued &&
			!coordinator.consumeQueuedPlayback(true, false),
			"queued playback waits while a trace is building");
		coordinator.traceBuildCompleted(true);
		coordinator.setAudioPreparationPending(true);
		check(coordinator.audioPreparationBlocksStart() &&
			!coordinator.consumeQueuedPlayback(false, true) && coordinator.playbackQueued(),
			"queued playback waits for the enabled audio preparation gate");
		coordinator.setAudioPreparationPending(false);
		check(coordinator.consumeQueuedPlayback(false, true) && !coordinator.playbackQueued(),
			"queued playback is consumed exactly once when its prerequisites are ready");
		check(coordinator.state(false, true, false, 0, 10) == PlaybackCoordinatorState::Ready &&
			coordinator.state(false, true, true, 4, 10) == PlaybackCoordinatorState::Playing &&
			coordinator.state(false, true, false, 4, 10) == PlaybackCoordinatorState::Paused &&
			coordinator.state(false, true, false, 10, 10) == PlaybackCoordinatorState::Completed,
			"playback coordinator reports ready, playing, paused, and completed states");
		coordinator.setAudioPreparationPending(true);
		check(coordinator.state(false, true, false, 0, 10) == PlaybackCoordinatorState::PreparingAudio,
			"playback coordinator exposes audio preparation as a distinct state");
		coordinator.setAudioPreparationPending(false);

		coordinator.traceBuildRequested(true);
		coordinator.traceBuildFailed();
		check(!coordinator.playbackQueued() && !coordinator.consumeQueuedPlayback(false, false),
			"failed trace generation discards queued playback");
		coordinator.traceBuildRequested(true);
		check(!coordinator.consumeQueuedPlayback(false, false) && !coordinator.playbackQueued(),
			"a finished build without a trace discards queued playback");

		check(!coordinator.observeAudioClockAvailability(false) &&
			!coordinator.observeAudioClockAvailability(true) &&
			coordinator.observeAudioClockAvailability(false),
			"playback coordinator detects the transition from audio clock to host fallback");
		coordinator.setReferenceElapsedSeconds(2.5);
		coordinator.advanceReferenceClock(-1.0);
		coordinator.advanceReferenceClock(0.25);
		check(std::fabs(coordinator.referenceElapsedSeconds() - 2.75) < 1e-12,
			"host fallback clock ignores negative deltas and advances by positive frame time");
		const AudioPlaybackRevision currentRevision(7, 11);
		check(coordinator.acceptsAudioPlayhead(true, 7, 11, 44100, currentRevision) &&
			!coordinator.acceptsAudioPlayhead(false, 7, 11, 44100, currentRevision) &&
			!coordinator.acceptsAudioPlayhead(true, 6, 11, 44100, currentRevision) &&
			!coordinator.acceptsAudioPlayhead(true, 7, 10, 44100, currentRevision) &&
			!coordinator.acceptsAudioPlayhead(true, 7, 11, 0, currentRevision),
			"audio playhead acceptance requires the active timeline and transport fence");
	}

	void testPlaybackTimingActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const float originalDuration = app.snapshot().durationSeconds;
		const AppAction reference(AppActionKind::SetPlaybackTimingMode, 0.0f,
			static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence));
		check(app.dispatch(reference).accepted &&
			app.preferences().simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence &&
			persistence.writes == 1,
			"reference cadence is a persisted simulation preference");
		check(!app.dispatch(AppAction(AppActionKind::IncreaseDuration)).accepted &&
			app.snapshot().durationSeconds == originalDuration && persistence.writes == 1,
			"manual duration changes are rejected without a settings write in reference cadence");
		check(!app.dispatch(AppAction(AppActionKind::SetPlaybackTimingMode, 0.0f, 99)).accepted &&
			persistence.writes == 1,
			"invalid timing modes are rejected without persistence");
	}

	void testAudioPreparationPlaybackGate()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.dispatch(AppAction(AppActionKind::Run)).accepted, "run creates a trace for the audio-preparation gate");
		check(app.playbackState() == PlaybackCoordinatorState::BuildingTrace,
			"app controller exposes trace generation as a playback preparation state");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "audio-preparation gate fixture receives its trace");
		check(app.playbackState() == PlaybackCoordinatorState::Ready,
			"completed trace is ready when no audio preparation gate is active");
		app.setAudioPreparationPending(true);
		check(app.playbackState() == PlaybackCoordinatorState::PreparingAudio,
			"app controller exposes pending timeline preparation");
		check(!app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && !app.snapshot().playing,
			"Play is rejected with an actionable status while its immutable audio timeline is preparing");
		app.setAudioPreparationPending(false);
		check(app.playbackState() == PlaybackCoordinatorState::Ready &&
			app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && app.snapshot().playing &&
			app.playbackState() == PlaybackCoordinatorState::Playing,
			"Play becomes available as soon as timeline preparation completes");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
	}

	void testStaleAudioPlayheadCannotCancelPlayback()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.dispatch(AppAction(AppActionKind::Run)).accepted,
			"playhead revision regression creates a trace");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "playhead revision regression receives its trace");
		app.setAudioPreparationPending(false);
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && app.snapshot().playing,
			"playhead revision regression starts playback");

		SoundEngine::AudioPlayheadFeedback stalePause;
		stalePause.generation = 7;
		stalePause.transportRevision = 10;
		stalePause.durationFrames = 44100 * 10;
		stalePause.playing = false;
		const AudioPlaybackRevision currentRevision(7, 11);
		app.applyAudioPlayhead(stalePause, currentRevision);
		check(app.snapshot().playing,
			"an older paused feedback snapshot cannot cancel a newer Play transport revision");

		SoundEngine::AudioPlayheadFeedback currentPlayhead;
		currentPlayhead.generation = 7;
		currentPlayhead.transportRevision = 11;
		currentPlayhead.durationFrames = 44100 * 10;
		currentPlayhead.timelineFrame = 4410;
		currentPlayhead.sourceCursor = 12;
		currentPlayhead.playing = true;
		app.applyAudioPlayhead(currentPlayhead, currentRevision);
		check(app.snapshot().playing && app.snapshot().cursor == 12,
			"feedback for the current transport revision advances the visible playhead");

		currentPlayhead.timelineFrame = currentPlayhead.durationFrames;
		currentPlayhead.sourceCursor = app.snapshot().eventCount;
		currentPlayhead.playing = false;
		app.applyAudioPlayhead(currentPlayhead, currentRevision);
		check(!app.snapshot().playing,
			"completion feedback for the current revision still pauses the simulation");
	}
}

int main(int argc, char* argv[])
{
	if (argc == 2 && std::string(argv[1]) == "--render-cost-profile")
	{
		try { runRenderCostProfile(); return 0; }
		catch (const std::exception& error)
		{
			std::cerr << "Render cost profile failed: " << error.what() << '\n';
			return 1;
		}
	}
	testActiveRoleLookup();
	testInlineEditModel();
	testSimulationPlaybackState();
	testSpacing();
	testPreferenceValueEquality();
	testBarCountSteps();
	testModifierStepSizes();
	testAppearanceAndPresetArchitecture();
	testWindowsFontCatalog();
	testTextBlockLayoutCache();
	testRenderPipelineContracts();
	testLayoutAndWidgets();
	testAppearanceEditorLayout();
	testAppearanceEditorViewComposition();
	testAppearanceEditorViewSynchronizesControls();
	testAppearanceEditorViewActions();
	testAlgorithmProfileView();
	testAppearanceEditorModel();
	testAppearanceEditorSession();
	testAlgorithmBrowserModel();
	testUiOverlayState();
	testMainMenuSelectionActions();
	testSettingsNavigationRouting();
	testAudioSettingsActionAdapter();
	testUiCatalogActionNormalization();
	testSdlUiEventAdapter();
	testAlgorithmBrowserInputAndLayout();
	testAlgorithmBrowserPaintComposition();
	testSettingsPanelPaintComposition();
	testSettingsPanelViewComposesControlPaintCommands();
	testSettingsPanelModel();
	testSettingsPanelViewOwnsNavigationAndScrolling();
	testSettingsPanelViewInitializesItsControls();
	testMainControlsViewInitializesTypedControls();
	testMainControlsViewSynchronizesTextPresentation();
	testMainControlsViewAppliesLayoutAndVisibility();
	testTimelineViewOwnsSeekInputAndPaintComposition();
	testMainControlsViewSynchronizesPresentationState();
	testMainControlsViewComposesControlPaintCommands();
	testSettingsPanelViewSynchronizesControlState();
	testSettingsPanelControlCatalog();
	testSettingsPanelLayout();
	testSettingsStore();
	testFractionalDurationStepPersists();
	testAppControllerDispatchAndPersistence();
	testAppearanceApplyIsTransactionalAndTransportNeutral();
	testRestoreReconstructedDefaultsScope();
	testShowcaseActions();
	testDistributionAlgorithmsSelectableFromDefaultInput();
	testMetadataAlgorithmOptions();
	testPlaybackTimebase();
	testPlaybackCoordinator();
	testPlaybackTimingActions();
	testAudioPreparationPlaybackGate();
	testStaleAudioPlayheadCannotCancelPlayback();
	testMailboxIdlePollingAndConcurrentPublication();
	testDeviceRecoveryAndSeekAcknowledgement();
	testAudioPresentationBridgeAndMailbox();
	testAccentAudioActions();
	testVariantAudioLevels();
	if (failures)
	{
		std::cerr << failures << " app framework checks failed.\n";
		return 1;
	}
	std::cout << "All app framework checks passed.\n";
	return 0;
}
