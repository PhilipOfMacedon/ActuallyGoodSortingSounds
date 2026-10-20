#include "UiArchitecture.h"
#include "../SortingLogic/TracePresentationPolicy.h"
#include "PlaybackTimebase.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
	std::string formatDuration(float seconds)
	{
		std::ostringstream value;
		value << std::setprecision(6) << seconds << " sec";
		return value.str();
	}

	UiColor colorToken(UiPresetId preset, const UiThemeCustomization& theme, const std::string& tokenId, const UiColor& fallback)
	{
		(void)preset;
		const std::map<std::string, UiTokenValue>::const_iterator value = theme.overrides.find(tokenId);
		return value != theme.overrides.end() && value->second.type == UiTokenType::Color ? value->second.color : fallback;
	}

	float numberToken(UiPresetId preset, const UiThemeCustomization& theme, const std::string& tokenId, float fallback)
	{
		const UiTokenValue value = effectiveUiTokenValue(preset, theme, tokenId);
		return value.type == UiTokenType::Number ? static_cast<float>(value.number) : fallback;
	}

	UiRect boundsFor(const UiLayoutResult& layout, const std::string& id)
	{
		const std::map<std::string, UiRect>::const_iterator found = layout.controlBounds.find(id);
		return found == layout.controlBounds.end() ? UiRect() : found->second;
	}

	void placeRow(UiLayoutResult& result, const std::vector<std::string>& ids, float x, float y,
		float width, float height, float gap)
	{
		if (ids.empty()) return;
		const float usable = std::max(0.0f, width - gap * static_cast<float>(ids.size() - 1));
		const float cell = usable / static_cast<float>(ids.size());
		for (std::size_t i = 0; i < ids.size(); ++i)
		{
			const UiRect bounds(x + static_cast<float>(i) * (cell + gap), y, cell, std::max(0.0f, height));
			result.controlBounds[ids[i]] = bounds;
			result.focusOrder.push_back(ids[i]);
		}
	}

	std::vector<std::string> selectIds(const std::vector<std::string>& all, const char* const* ids, std::size_t count)
	{
		std::vector<std::string> result;
		for (std::size_t i = 0; i < count; ++i)
			if (std::find(all.begin(), all.end(), ids[i]) != all.end()) result.push_back(ids[i]);
		return result;
	}

	void addExisting(UiLayoutResult& result, const std::vector<std::string>& requested,
		const std::vector<std::string>& available, float x, float y, float w, float h, float gap)
	{
		std::vector<std::string> ids;
		for (std::size_t i = 0; i < requested.size(); ++i)
			if (std::find(available.begin(), available.end(), requested[i]) != available.end()) ids.push_back(requested[i]);
		placeRow(result, ids, x, y, w, h, gap);
	}

	void layoutTimelineParts(UiLayoutResult& result, float arrowSize, bool arrows)
	{
		result.timelineTrack = result.timeline;
		result.timelineLeftArrow = UiRect();
		result.timelineRightArrow = UiRect();
		if (!arrows || result.timeline.width <= 2.0f * arrowSize + 8.0f || result.timeline.height <= 0.0f) return;
		const float size = std::min(std::max(0.0f, arrowSize), result.timeline.height);
		result.timelineLeftArrow = UiRect(result.timeline.x, result.timeline.y, size, result.timeline.height);
		result.timelineRightArrow = UiRect(result.timeline.x + result.timeline.width - size,
			result.timeline.y, size, result.timeline.height);
		result.timelineTrack = UiRect(result.timeline.x + size, result.timeline.y,
			std::max(0.0f, result.timeline.width - size * 2.0f), result.timeline.height);
		result.interactiveSubregions["timeline"].push_back(result.timelineLeftArrow);
		result.interactiveSubregions["timeline"].push_back(result.timelineTrack);
		result.interactiveSubregions["timeline"].push_back(result.timelineRightArrow);
	}
}

UiOverlayState::UiOverlayState()
	: active_(UiOverlayId::None), appearanceReturnOverlay_(UiOverlayId::None)
{
}

bool UiOverlayState::settingsSurfaceOpen() const
{
	return active_ == UiOverlayId::Settings || active_ == UiOverlayId::AppearanceEditor;
}

bool UiOverlayState::openSettings()
{
	if (active_ != UiOverlayId::None && active_ != UiOverlayId::Settings) return false;
	active_ = UiOverlayId::Settings;
	return true;
}

bool UiOverlayState::closeSettings()
{
	if (active_ != UiOverlayId::Settings) return false;
	active_ = UiOverlayId::None;
	return true;
}

bool UiOverlayState::openAlgorithmBrowser()
{
	if (active_ != UiOverlayId::None && active_ != UiOverlayId::AlgorithmBrowser) return false;
	active_ = UiOverlayId::AlgorithmBrowser;
	return true;
}

bool UiOverlayState::closeAlgorithmBrowser()
{
	if (active_ != UiOverlayId::AlgorithmBrowser) return false;
	active_ = UiOverlayId::None;
	return true;
}

bool UiOverlayState::openAppearanceEditor()
{
	if (active_ == UiOverlayId::AppearanceEditor) return false;
	appearanceReturnOverlay_ = active_ == UiOverlayId::Settings ? UiOverlayId::Settings : UiOverlayId::None;
	active_ = UiOverlayId::AppearanceEditor;
	return true;
}

bool UiOverlayState::closeAppearanceEditor()
{
	if (active_ != UiOverlayId::AppearanceEditor) return false;
	active_ = appearanceReturnOverlay_;
	appearanceReturnOverlay_ = UiOverlayId::None;
	return true;
}

UiScreenModel::UiScreenModel()
	: preset(UiPresetId::Current), transport(UiTransportState::NoTrace), setupEnabled(true), runEnabled(false), playEnabled(false),
		countEditable(true), durationEditable(true), descending(false), hasTrace(false), buildingTrace(false), playing(false), completed(false),
		itemCount(0), cursor(0), eventCount(0), audioEligibleEventCount(0), seed(0), algorithm(), inputDomain(), inputPattern(),
		shellGapSequence(), direction("Ascending"), playCaption("Generate & Play"), durationCaption(), status(),
		algorithmCapabilities(), telemetry()
{
}

UiScreenModel buildUiScreenModel(const sortinglogic::RenderSnapshot& snapshot,
	const UserPreferences& preferences, bool playbackPending, const std::string& applicationStatus)
{
	UiScreenModel model;
	model.preset = preferences.appearance.activePreset;
	model.setupEnabled = !snapshot.buildingTrace && !snapshot.playing && !playbackPending;
	model.runEnabled = model.setupEnabled && snapshot.runCompatible;
	model.playEnabled = snapshot.playing || snapshot.buildingTrace || snapshot.hasTrace || snapshot.runCompatible;
	model.countEditable = model.setupEnabled;
	const bool referenceCadence = preferences.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence;
	model.durationEditable = model.setupEnabled && !referenceCadence;
	model.descending = snapshot.direction == sortinglogic::SortDirection::Descending;
	model.hasTrace = snapshot.hasTrace;
	model.buildingTrace = snapshot.buildingTrace;
	model.playing = snapshot.playing;
	model.completed = snapshot.complete;
	model.itemCount = snapshot.itemCount;
	model.cursor = snapshot.cursor;
	model.eventCount = snapshot.eventCount;
	model.audioEligibleEventCount = snapshot.audioEligibleEventCount;
	model.seed = snapshot.inputSeed;
	model.algorithm = sortinglogic::algorithmDisplayName(snapshot.algorithm);
	model.inputDomain = sortinglogic::inputDomainName(snapshot.domain);
	model.inputPattern = sortinglogic::inputPatternName(snapshot.pattern);
	model.direction = model.descending ? "Descending" : "Ascending";
	if (snapshot.algorithm == sortinglogic::SortAlgorithm::Shell)
		model.shellGapSequence = sortinglogic::shellGapSequenceName(snapshot.shellGapSequence);
	const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptor(snapshot.algorithm);
	if (algorithm)
	{
		UiAlgorithmCapabilities& capabilities = model.algorithmCapabilities;
		capabilities.available = true;
		capabilities.stable = algorithm->stable;
		capabilities.inPlace = algorithm->inPlace;
		capabilities.supportsNormalizedFloat = algorithm->supportsNormalizedFloat;
		capabilities.supportsIntegerKeys = algorithm->supportsIntegerKeys;
		capabilities.safeMaximumItemCount = algorithm->safeMaxItemCount;
		capabilities.timeComplexity = algorithm->timeComplexity;
		capabilities.spaceComplexity = algorithm->spaceComplexity;
		capabilities.behavior = algorithm->behavior;
		capabilities.compatibilityMessage = snapshot.compatibilityMessage;
		for (std::size_t i = 0; i < algorithm->metrics.size(); ++i)
		{
			const std::map<std::string, std::uint64_t>::const_iterator value = snapshot.metrics.algorithm.find(algorithm->metrics[i].key);
			if (value != snapshot.metrics.algorithm.end())
				capabilities.metrics.push_back(UiTelemetryValue(algorithm->metrics[i].key, algorithm->metrics[i].displayName,
					std::to_string(value->second)));
		}
	}
	if (referenceCadence)
	{
		std::ostringstream duration;
		duration << std::fixed << std::setprecision(2)
			<< playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount) << " sec";
		model.durationCaption = duration.str();
	}
	else model.durationCaption = formatDuration(snapshot.durationSeconds);
	if (snapshot.playing) { model.transport = UiTransportState::Playing; model.playCaption = "Pause"; }
	else if (playbackPending) { model.transport = UiTransportState::Queued; model.playCaption = "Cancel queued"; }
	else if (snapshot.buildingTrace) { model.transport = UiTransportState::Building; model.playCaption = "Play when ready"; }
	else if (!snapshot.hasTrace) { model.transport = UiTransportState::NoTrace; model.playCaption = "Generate & Play"; }
	else if (snapshot.complete) { model.transport = UiTransportState::Replay; model.playCaption = "Replay"; }
	else { model.transport = UiTransportState::Ready; model.playCaption = "Play"; }
	model.status = applicationStatus.empty() ? snapshot.status : applicationStatus;
	model.telemetry.push_back(UiTelemetryValue("cursor", "Event", std::to_string(snapshot.cursor) + "/" + std::to_string(snapshot.eventCount)));
	model.telemetry.push_back(UiTelemetryValue("seed", "Seed", std::to_string(snapshot.inputSeed)));
	if (snapshot.hasTrace)
	{
		model.telemetry.push_back(UiTelemetryValue("comparisons", "Cmp", std::to_string(snapshot.metrics.comparisons)));
		model.telemetry.push_back(UiTelemetryValue("swaps", "Swap", std::to_string(snapshot.metrics.swaps)));
		model.telemetry.push_back(UiTelemetryValue("main_writes", "Main", std::to_string(snapshot.metrics.primaryWrites)));
		model.telemetry.push_back(UiTelemetryValue("auxiliary", "Aux", std::to_string(snapshot.metrics.auxiliaryItemWrites) + "/" +
			std::to_string(snapshot.metrics.auxiliaryCounterUpdates)));
		model.telemetry.push_back(UiTelemetryValue("peak_auxiliary", "Peak", std::to_string(snapshot.metrics.peakAuxiliaryCapacity)));
	}
	return model;
}

std::vector<sortinglogic::ActiveRole> rolesVisibleOnMainArray(const sortinglogic::RenderSnapshot& snapshot)
{
	std::vector<sortinglogic::ActiveRole> roles = snapshot.activeRoles;
	for (std::size_t i = 0; i < roles.size(); ++i)
		roles[i].lane = sortinglogic::visibleRoleWorkspace(snapshot.algorithm,
			snapshot.mergeBufferVisualizationEnabled, roles[i].lane);
	return roles;
}

int traceProgressPercent(bool hasTrace, bool complete, std::size_t cursor, std::size_t eventCount)
{
	if (!hasTrace) return 0;
	if (complete) return 100;
	if (eventCount == 0) return 0;
	const std::size_t appliedEvents = std::min(cursor, eventCount);
	const double fraction = static_cast<double>(appliedEvents) / static_cast<double>(eventCount);
	return std::max(0, std::min(100, static_cast<int>(std::lround(fraction * 100.0))));
}

UiLayoutInput::UiLayoutInput()
	: width(0), height(0), expanded(true), headerHeight(42), menuHeight(0), margin(16), gap(6), controlHeight(30), railWidth(340),
	transportHeight(88), chartInset(0), contentGap(14), timelineArrowSize(14)
{
}

UiRect UiLayoutResult::control(const std::string& id) const { return boundsFor(*this, id); }

UiLayoutResult calculateUiLayout(UiPresetId preset, const UiLayoutInput& in, const std::vector<std::string>& controlIds)
{
	UiLayoutResult result;
	const float width = std::max(0.0f, in.width), height = std::max(0.0f, in.height);
	const float margin = std::max(0.0f, in.margin), gap = std::max(0.0f, in.gap);
	const float headerHeight = std::max(0.0f, in.headerHeight);
	const float menuHeight = preset == UiPresetId::Current ? 0.0f : std::max(0.0f, in.menuHeight);
	// VGA menus share the title strip. The native-caption Windows 95 layout has
	// no title strip, so its menu row supplies the top inset on its own.
	const float headerBottom = (std::max)(headerHeight, menuHeight);
	result.header = UiRect(0, 0, width, headerBottom);
	result.menuBar = menuHeight > 0.0f
		? UiRect(0, (headerBottom - menuHeight) * 0.5f, width, menuHeight) : UiRect();
	// Explicitly clear controls omitted by a collapsed or responsive composition.
	// This keeps old bounds from remaining clickable after a layout change.
	for (std::size_t i = 0; i < controlIds.size(); ++i)
		result.controlBounds[controlIds[i]] = UiRect();
	const bool expanded = in.expanded;
	const float headerButtonY = std::max(0.0f, (headerHeight - 28.0f) * 0.5f);
	if (std::find(controlIds.begin(), controlIds.end(), "panel_toggle") != controlIds.end())
	{
		result.controlBounds["panel_toggle"] = UiRect(12.0f, headerButtonY, 30.0f, 28.0f);
		result.focusOrder.push_back("panel_toggle");
	}
	if (std::find(controlIds.begin(), controlIds.end(), "settings") != controlIds.end())
	{
		result.controlBounds["settings"] = UiRect(std::max(0.0f, width - 42.0f), headerButtonY, 30.0f, 28.0f);
		result.focusOrder.push_back("settings");
	}
	if (menuHeight > 0.0f)
	{
		const char* menuIds[] = { "menu_file", "menu_view", "menu_tools" };
		std::vector<std::string> menus = selectIds(controlIds, menuIds, sizeof(menuIds) / sizeof(menuIds[0]));
		const bool titleStripExists = headerHeight > 0.0f;
		const float menuX = titleStripExists ? 50.0f : 10.0f;
		const float rightReserve = titleStripExists ? 52.0f : 10.0f;
		const float menuWidth = (std::max)(0.0f, (std::min)(228.0f, width - menuX - rightReserve));
		const float menuY = result.menuBar.y;
		const float menuHeightInHeader = (std::min)(menuHeight, result.menuBar.height);
		placeRow(result, menus, menuX, menuY, menuWidth, menuHeightInHeader, 2.0f);
	}
	const UiRect collapsedChartStart(std::max(0.0f, (width - std::min(280.0f, width - 24.0f)) * 0.5f),
		headerBottom + std::max(24.0f, (height - headerBottom - 42.0f) * 0.5f) + 42.0f,
		std::min(280.0f, std::max(0.0f, width - 24.0f)), 42.0f);
	if (std::find(controlIds.begin(), controlIds.end(), "start_overlay") != controlIds.end())
		result.controlBounds["start_overlay"] = collapsedChartStart;

	if (preset == UiPresetId::Current)
	{
		if (!expanded)
		{
			const float inset = std::max(0.0f, in.chartInset);
			const float chartTop = headerBottom + gap + 16.0f + std::max(0.0f, in.contentGap) + inset;
			result.chartViewport = UiRect(margin + inset, chartTop, std::max(0.0f, width - 2.0f * (margin + inset)),
				std::max(0.0f, height - chartTop - 18.0f - inset));
			return result;
		}
		const bool wide = width >= 900.0f;
		const float available = std::max(0.0f, width - 2.0f * margin);
		const float setupTop = std::max(headerBottom + gap, 48.0f);
		const char* topIds[] = { "shuffle", "insta_sort", "insta_invert", "algorithm", "direction", "run", "play", "reset" };
		const char* row0[] = { "shuffle", "insta_sort", "insta_invert", "algorithm" };
		const char* row1[] = { "direction", "run", "play", "reset" };
		const char* row2[] = { "previous", "next", "bars_down", "bars_value", "bars_up" };
		const char* row3[] = { "time_down", "time_value", "time_up" };
		if (wide)
		{
			std::vector<std::string> first, second;
			for (std::size_t i = 0; i < sizeof(topIds) / sizeof(topIds[0]); ++i)
				if (std::find(controlIds.begin(), controlIds.end(), topIds[i]) != controlIds.end()) first.push_back(topIds[i]);
			const char* bottom[] = { "previous", "next", "bars_down", "bars_value", "bars_up", "time_down", "time_value", "time_up" };
			for (std::size_t i = 0; i < sizeof(bottom) / sizeof(bottom[0]); ++i)
				if (std::find(controlIds.begin(), controlIds.end(), bottom[i]) != controlIds.end()) second.push_back(bottom[i]);
			placeRow(result, first, margin, setupTop, available, in.controlHeight, gap);
			const float rowStride = std::max(36.0f, in.controlHeight + gap);
			const float secondY = setupTop + rowStride;
			placeRow(result, second, margin, secondY, available, in.controlHeight, gap);
			const float timelineY = secondY + in.controlHeight + 12.0f;
			result.timeline = UiRect(margin, timelineY, available, 10.0f);
			layoutTimelineParts(result, 0.0f, false);
			const float inset = std::max(0.0f, in.chartInset);
			const float chartTop = timelineY + 44.0f + std::max(0.0f, in.contentGap) + inset;
			result.chartViewport = UiRect(margin + inset, chartTop, std::max(0.0f, available - inset * 2.0f),
				std::max(0.0f, height - chartTop - 18.0f - inset));
		}
		else
		{
			const std::vector<std::string> rows[] = {
				selectIds(controlIds, row0, sizeof(row0) / sizeof(row0[0])),
				selectIds(controlIds, row1, sizeof(row1) / sizeof(row1[0])),
				selectIds(controlIds, row2, sizeof(row2) / sizeof(row2[0])),
				selectIds(controlIds, row3, sizeof(row3) / sizeof(row3[0])) };
			const float rowStride = std::max(35.0f, in.controlHeight + std::max(0.0f, gap - 1.0f));
			for (int row = 0; row < 4; ++row)
				placeRow(result, rows[row], margin, setupTop + row * rowStride, available, in.controlHeight, gap);
			const float timelineY = setupTop + 3.0f * rowStride + in.controlHeight + 11.0f;
			result.timeline = UiRect(margin, timelineY, available, 10.0f);
			layoutTimelineParts(result, 0.0f, false);
			const float inset = std::max(0.0f, in.chartInset);
			const float chartTop = timelineY + 42.0f + std::max(0.0f, in.contentGap) + inset;
			result.chartViewport = UiRect(margin + inset, chartTop, std::max(0.0f, available - inset * 2.0f),
				std::max(0.0f, height - chartTop - 18.0f - inset));
		}
		return result;
	}

	if (preset == UiPresetId::FlatVgaDos || preset == UiPresetId::ClassicWindows95)
	{
		const bool windows95 = preset == UiPresetId::ClassicWindows95;
		const float inset = std::max(0.0f, in.chartInset);
		if (!expanded)
		{
			const float compactInset = std::min(margin, std::max(0.0f, width * 0.04f));
			result.chartViewport = UiRect(compactInset, headerBottom + gap,
				std::max(0.0f, width - compactInset * 2.0f),
				std::max(0.0f, height - headerBottom - gap * 2.0f));
			result.clips["chart"] = result.chartViewport;
			return result;
		}

		const float contentGap = std::max(0.0f, in.contentGap);
		const float layoutGap = std::min(gap, 8.0f);
		const float controlHeight = std::max(0.0f, in.controlHeight);
		const float bottomWidth = std::max(0.0f, width - margin * 2.0f);
		const bool wide = width >= 900.0f;
		const float requestedButtonHeight = wide ? (windows95 ? 66.0f : 58.0f) : (windows95 ? 48.0f : 42.0f);
		const float requestedTimelineHeight = wide ? 18.0f : (std::min)(10.0f, controlHeight * 0.25f);
		const float requiredTransportHeight = wide
			? (std::max)(requestedButtonHeight + 12.0f, 8.0f + requestedTimelineHeight + 4.0f + 30.0f + 4.0f)
			: 7.0f + requestedTimelineHeight + 3.0f + 30.0f + requestedButtonHeight + 8.0f;
		const float availableTransportHeight = (std::max)(0.0f, height - headerBottom - layoutGap - margin);
		const float transportHeight = (std::min)(availableTransportHeight,
			(std::max)(requiredTransportHeight, (std::max)(0.0f, in.transportHeight)));
		const float transportTop = (std::max)(headerBottom + layoutGap, height - transportHeight - margin);
		result.transportArea = UiRect(margin, transportTop, bottomWidth,
			(std::max)(0.0f, height - transportTop - margin));
		const char* classicTransport[] = { "play", "next", "reset" };
		const char* vgaTransport[] = { "previous", "next", "play", "reset" };
		const char* const* transport = windows95 && wide ? classicTransport : vgaTransport;
		const std::size_t transportCount = windows95 && wide
			? sizeof(classicTransport) / sizeof(classicTransport[0])
			: sizeof(vgaTransport) / sizeof(vgaTransport[0]);
		const std::vector<std::string> transportIds = selectIds(controlIds, transport, transportCount);
		const float timelineHeight = std::min(wide ? 18.0f : 10.0f,
			std::max(0.0f, std::min(std::max(4.0f, wide ? 18.0f : controlHeight * 0.25f), result.transportArea.height - 12.0f)));
		if (wide)
		{
			const float railWidth = std::min(std::max(160.0f, in.railWidth),
				std::max(160.0f, std::min(width * 0.38f, width - margin * 3.0f - 160.0f)));
			const float railX = margin;
			const float railY = headerBottom + layoutGap;
			const float railBottom = std::max(railY, transportTop - layoutGap);
			result.setupRail = UiRect(railX, railY, railWidth, std::max(0.0f, railBottom - railY));
			const float railInset = std::min(8.0f, std::min(result.setupRail.width * 0.5f, result.setupRail.height * 0.5f));
			const float innerX = result.setupRail.x + railInset;
			const float innerWidth = std::max(0.0f, result.setupRail.width - railInset * 2.0f);
			const float innerTop = result.setupRail.y + railInset;
			const float innerHeight = std::max(0.0f, result.setupRail.height - railInset * 2.0f);
			const float rowGap = std::min(layoutGap, 8.0f);
			const int rowCount = 7;
			const float fittingHeight = std::max(0.0f, (innerHeight - rowGap * (rowCount - 1)) / rowCount);
			const float rowHeight = std::min(controlHeight, fittingHeight);
			const char* row0[] = { "shuffle", "insta_sort", "insta_invert" };
			const char* row1[] = { "algorithm" };
			const char* row2[] = { "domain", "pattern" };
			const char* row3[] = { "direction" };
			const char* row4[] = { "bars_down", "bars_value", "bars_up" };
			const char* row5[] = { "time_down", "time_value", "time_up" };
			const char* row6[] = { "seed_readout" };
			const char* const* rowIds[] = { row0, row1, row2, row3, row4, row5, row6 };
			const std::size_t rowSizes[] = { 3, 1, 2, 1, 3, 3, 1 };
			for (int row = 0; row < rowCount; ++row)
				addExisting(result, std::vector<std::string>(rowIds[row], rowIds[row] + rowSizes[row]), controlIds,
					innerX, innerTop + row * (rowHeight + rowGap), innerWidth, rowHeight, rowGap);
			const UiRect direction = result.control("direction");
			if (direction.width > 0.0f)
			{
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x, direction.y, direction.width * 0.5f, direction.height));
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x + direction.width * 0.5f, direction.y,
					direction.width * 0.5f, direction.height));
			}

			const float chartX = railX + railWidth + contentGap;
			const float chartY = railY;
			const float chartBottom = std::max(chartY, transportTop - layoutGap);
			result.chartViewport = UiRect(chartX + inset, chartY + inset,
				std::max(0.0f, width - chartX - inset - margin),
				std::max(0.0f, chartBottom - chartY - inset));

			const float buttonNominal = std::max(72.0f, std::min(132.0f, controlHeight * (windows95 ? 3.6f : 3.2f)));
			const float desiredGroupWidth = buttonNominal * static_cast<float>(transportIds.size()) +
				layoutGap * static_cast<float>(transportIds.empty() ? 0 : transportIds.size() - 1);
			const float groupWidth = std::min(result.transportArea.width * 0.46f, desiredGroupWidth);
			const float buttonHeight = std::min(requestedButtonHeight,
				std::max(0.0f, result.transportArea.height - 12.0f));
			const float buttonY = result.transportArea.y + std::max(0.0f, (result.transportArea.height - buttonHeight) * 0.5f);
			placeRow(result, transportIds, result.transportArea.x, buttonY, groupWidth, buttonHeight, layoutGap);
			const float infoX = result.transportArea.x + groupWidth + layoutGap;
			const float infoWidth = std::max(0.0f, result.transportArea.x + result.transportArea.width - infoX);
			result.timeline = UiRect(infoX, result.transportArea.y + 8.0f, infoWidth, timelineHeight);
			layoutTimelineParts(result, std::min(in.timelineArrowSize, timelineHeight), true);
			const float telemetryY = result.timeline.y + timelineHeight + 4.0f;
			const float telemetryHeight = std::max(0.0f, std::min(30.0f,
				result.transportArea.y + result.transportArea.height - 4.0f - telemetryY));
			result.telemetry = UiRect(infoX, telemetryY, infoWidth, telemetryHeight);
		}
		else
		{
			const float setupTop = headerBottom + layoutGap;
			const float setupWidth = bottomWidth;
			const float rowGap = std::min(layoutGap, 6.0f);
			const float setupSpace = std::max(0.0f, transportTop - setupTop - contentGap - layoutGap);
			const float rowHeight = std::min(controlHeight,
				std::max(0.0f, (setupSpace - rowGap * 4.0f) / 5.0f));
			const char* row0[] = { "shuffle", "insta_sort", "insta_invert" };
			const char* row1[] = { "algorithm" };
			const char* row2[] = { "direction" };
			const char* row3[] = { "bars_down", "bars_value", "bars_up" };
			const char* row4[] = { "time_down", "time_value", "time_up" };
			const char* const* rowIds[] = { row0, row1, row2, row3, row4 };
			const std::size_t rowSizes[] = { 3, 1, 1, 3, 3 };
			for (int row = 0; row < 5; ++row)
				addExisting(result, std::vector<std::string>(rowIds[row], rowIds[row] + rowSizes[row]), controlIds,
					margin, setupTop + row * (rowHeight + rowGap), setupWidth, rowHeight, rowGap);
			const UiRect direction = result.control("direction");
			if (direction.width > 0.0f)
			{
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x, direction.y, direction.width * 0.5f, direction.height));
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x + direction.width * 0.5f, direction.y,
					direction.width * 0.5f, direction.height));
			}
			const float chartY = setupTop + 5.0f * rowHeight + 4.0f * rowGap + contentGap;
			result.setupRail = UiRect(margin, setupTop, setupWidth, std::max(0.0f, chartY - setupTop - contentGap));
			result.chartViewport = UiRect(margin + inset, chartY,
				std::max(0.0f, setupWidth - inset * 2.0f),
				std::max(0.0f, transportTop - layoutGap - chartY));
			const float minimumButtonTop = 7.0f + timelineHeight + 3.0f + 30.0f;
			const float buttonHeight = std::min(requestedButtonHeight,
				std::max(0.0f, result.transportArea.height - minimumButtonTop - 8.0f));
			const float buttonY = result.transportArea.y + result.transportArea.height - buttonHeight - 8.0f;
			placeRow(result, transportIds, result.transportArea.x, buttonY, result.transportArea.width, buttonHeight, layoutGap);
			result.timeline = UiRect(result.transportArea.x, result.transportArea.y + 7.0f,
				result.transportArea.width, timelineHeight);
			layoutTimelineParts(result, 0.0f, false);
			const float telemetryY = result.timeline.y + timelineHeight + 3.0f;
			const float telemetryHeight = std::max(0.0f, std::min(30.0f,
				result.transportArea.y + result.transportArea.height - 4.0f - telemetryY));
			result.telemetry = UiRect(result.transportArea.x, telemetryY, result.transportArea.width, telemetryHeight);
		}
		result.clips["chart"] = result.chartViewport;
		result.clips["setup"] = result.setupRail;
		result.clips["transport"] = result.transportArea;
		return result;
	}

	if (!expanded)
	{
		const float inset = std::min(margin, std::max(0.0f, width * 0.04f));
		result.chartViewport = UiRect(inset, headerBottom + gap, std::max(0.0f, width - inset * 2),
			std::max(0.0f, height - headerBottom - gap * 2));
		return result;
	}
	return result;
}

UiControlPaintStyle::UiControlPaintStyle()
	: fill(), text(0.93f,0.96f,0.98f,1), border(0.25f,0.38f,0.43f,1), borderWidth(0), cornerRadius(0), drawArrow(false), squareIndicator(false)
{
}

UiControlPaintStyle resolveUiControlPaintStyle(UiPresetId preset, const UiThemeCustomization& theme,
	const std::string& role, bool enabled, bool hovered, bool pressed, bool selected,
	const std::map<std::string, UiColor>& resolvedPalette)
{
	UiControlPaintStyle style;
	const auto color = [&resolvedPalette, preset, &theme](const std::string& id, const UiColor& fallback)
	{
		const std::map<std::string, UiColor>::const_iterator base = resolvedPalette.find(id);
		return colorToken(preset, theme, id, base == resolvedPalette.end() ? fallback : base->second);
	};
	style.text = color("color.text", style.text);
	style.border = color("color.border", style.border);
	style.borderWidth = numberToken(preset, theme, "control.border_width", 0.0f);
	style.cornerRadius = 0.0f;
	style.drawArrow = role == "choice" || role == "dropdown";
	style.squareIndicator = preset == UiPresetId::FlatVgaDos || preset == UiPresetId::ClassicWindows95;
	if (!enabled) style.fill = color("color.disabled_button", UiColor(0.10f,0.16f,0.19f,1));
	else if (selected) style.fill = color("color.accent", UiColor(0.29f,0.72f,0.64f,1));
	else if (role == "run") style.fill = color("color.run", UiColor(0.48f,0.28f,0.12f,1));
	else if (role == "play-active") style.fill = color("color.play_active", UiColor(0.18f,0.42f,0.34f,1));
	else if (role == "play") style.fill = color("color.button", UiColor(0.13f,0.24f,0.31f,1));
	else if (role == "reset") style.fill = color("color.reset", UiColor(0.30f,0.21f,0.35f,1));
	else if (role == "value") style.fill = color("color.value_button", UiColor(0.10f,0.16f,0.20f,1));
	else style.fill = color("color.button", UiColor(0.13f,0.24f,0.31f,1));
	if (pressed)
	{
		style.fill.r *= 0.78f; style.fill.g *= 0.78f; style.fill.b *= 0.78f;
	}
	else if (hovered)
	{
		style.fill.r = std::min(1.0f, style.fill.r * 1.12f + 0.025f);
		style.fill.g = std::min(1.0f, style.fill.g * 1.12f + 0.025f);
		style.fill.b = std::min(1.0f, style.fill.b * 1.12f + 0.025f);
	}
	return style;
}
