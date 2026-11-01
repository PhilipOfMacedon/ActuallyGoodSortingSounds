#include "UiArchitecture.h"

#include <algorithm>
#include <cmath>

namespace
{
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
	: active_(UiOverlayId::None), appearanceReturnOverlay_(UiOverlayId::None), algorithmReturnOverlay_(UiOverlayId::None)
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
	if (active_ != UiOverlayId::None && active_ != UiOverlayId::DemoEditor && active_ != UiOverlayId::AlgorithmBrowser) return false;
	if (active_ != UiOverlayId::AlgorithmBrowser) algorithmReturnOverlay_ = active_;
	active_ = UiOverlayId::AlgorithmBrowser;
	return true;
}

bool UiOverlayState::closeAlgorithmBrowser()
{
	if (active_ != UiOverlayId::AlgorithmBrowser) return false;
	active_ = algorithmReturnOverlay_;
	algorithmReturnOverlay_ = UiOverlayId::None;
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

bool UiOverlayState::openDemoEditor()
{
	if (active_ != UiOverlayId::None && active_ != UiOverlayId::DemoEditor) return false;
	active_ = UiOverlayId::DemoEditor;
	return true;
}

bool UiOverlayState::closeDemoEditor()
{
	if (active_ != UiOverlayId::DemoEditor) return false;
	active_ = UiOverlayId::None;
	return true;
}

UiScreenModel::UiScreenModel()
	: preset(UiPresetId::Current), transport(UiTransportState::NoTrace), setupEnabled(true), runEnabled(false), playEnabled(false),
		countEditable(true), durationEditable(true), descending(false), hasTrace(false), buildingTrace(false), playing(false), completed(false),
		itemCount(0), cursor(0), eventCount(0), audioEligibleEventCount(0), seed(0), algorithm(), inputDomain(), inputPattern(),
	algorithmOptionsSummary(), direction("Ascending"), playCaption("Generate & Play"), durationCaption(), status(),
		algorithmCapabilities(), telemetry()
{
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
	if (preset == UiPresetId::Current && std::find(controlIds.begin(), controlIds.end(), "demo_editor") != controlIds.end())
	{
		result.controlBounds["demo_editor"] = UiRect(std::max(0.0f, width - 148.0f), headerButtonY, 100.0f, 28.0f);
		result.focusOrder.push_back("demo_editor");
	}
	if (menuHeight > 0.0f)
	{
		const char* menuIds[] = { "menu_file", "menu_view", "menu_tools" };
		std::vector<std::string> menus = selectIds(controlIds, menuIds, sizeof(menuIds) / sizeof(menuIds[0]));
		// Keep the hamburger on the left and Settings on the right for every
		// classic preset, including Windows 95 where the menu row is the header.
		const float menuX = 50.0f;
		const float rightReserve = 52.0f;
		const float menuWidth = (std::max)(0.0f, (std::min)(228.0f, width - menuX - rightReserve));
		const float menuY = result.menuBar.y;
		const float menuHeightInHeader = (std::min)(menuHeight, result.menuBar.height);
		placeRow(result, menus, menuX, menuY, menuWidth, menuHeightInHeader, 2.0f);
	}
	if (!expanded && std::find(controlIds.begin(), controlIds.end(), "demo_mode") != controlIds.end())
		result.controlBounds["demo_mode"] = UiRect(std::max(0.0f, width - 174.0f), headerButtonY, 124.0f, 28.0f);
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
		const char* topIds[] = { "shuffle", "insta_sort", "insta_invert", "algorithm", "direction", "run", "play", "reset", "demo_mode" };
		const char* row0[] = { "shuffle", "insta_sort", "insta_invert", "algorithm" };
		const char* row1[] = { "direction", "run", "play", "reset", "demo_mode" };
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
			const char* seedRow[] = { "seed_value", "seed_copy", "seed_save", "seed_bookmarks", "seed_previous", "seed_next", "seed_remove" };
			std::vector<std::string> third;
			for (std::size_t i = 0; i < sizeof(seedRow) / sizeof(seedRow[0]); ++i)
				if (std::find(controlIds.begin(), controlIds.end(), seedRow[i]) != controlIds.end()) third.push_back(seedRow[i]);
			const float thirdY = secondY + in.controlHeight + gap;
			placeRow(result, third, margin, thirdY, available, in.controlHeight, gap);
			const float timelineY = thirdY + in.controlHeight + 12.0f;
			result.timeline = UiRect(margin, timelineY, available, 10.0f);
			layoutTimelineParts(result, 0.0f, false);
			const float inset = std::max(0.0f, in.chartInset);
			const float chartTop = timelineY + 44.0f + std::max(0.0f, in.contentGap) + inset;
			result.chartViewport = UiRect(margin + inset, chartTop, std::max(0.0f, available - inset * 2.0f),
				std::max(0.0f, height - chartTop - 18.0f - inset));
		}
		else
		{
			const char* seedRow[] = { "seed_value", "seed_copy", "seed_save", "seed_bookmarks", "seed_previous", "seed_next", "seed_remove" };
			const std::vector<std::string> rows[] = {
				selectIds(controlIds, row0, sizeof(row0) / sizeof(row0[0])),
				selectIds(controlIds, row1, sizeof(row1) / sizeof(row1[0])),
				selectIds(controlIds, row2, sizeof(row2) / sizeof(row2[0])),
				selectIds(controlIds, row3, sizeof(row3) / sizeof(row3[0])),
				selectIds(controlIds, seedRow, sizeof(seedRow) / sizeof(seedRow[0])) };
			const float rowStride = std::max(35.0f, in.controlHeight + std::max(0.0f, gap - 1.0f));
			for (int row = 0; row < 4; ++row)
				placeRow(result, rows[row], margin, setupTop + row * rowStride, available, in.controlHeight, gap);
			const float seedY = setupTop + 4.0f * rowStride;
			placeRow(result, rows[4], margin, seedY, available, in.controlHeight, gap);
			const float timelineY = seedY + in.controlHeight + 11.0f;
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
		const char* classicTransport[] = { "previous", "next", "play", "reset" };
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
			const int rowCount = 9;
			const float fittingHeight = std::max(0.0f, (innerHeight - rowGap * (rowCount - 1)) / rowCount);
			const float rowHeight = std::min(controlHeight, fittingHeight);
			const char* row0[] = { "shuffle", "insta_sort", "insta_invert" };
			const char* row1[] = { "algorithm" };
			const char* row2[] = { "domain", "pattern" };
			const char* row3[] = { "direction" };
			const char* row4[] = { "bars_down", "bars_value", "bars_up" };
			const char* row5[] = { "time_down", "time_value", "time_up" };
			const char* row6[] = { "seed_value", "seed_copy", "seed_save" };
			const char* row7[] = { "seed_bookmarks", "seed_remove" };
			const char* row8[] = { "seed_previous", "seed_next" };
			const char* const* rowIds[] = { row0, row1, row2, row3, row4, row5, row6, row7, row8 };
			const std::size_t rowSizes[] = { 3, 1, 2, 1, 3, 3, 3, 2, 2 };
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
			const bool hasDemoMode = std::find(controlIds.begin(), controlIds.end(), "demo_mode") != controlIds.end();
			const float modeWidth = hasDemoMode ? 132.0f : 0.0f;
			if (hasDemoMode)
				result.controlBounds["demo_mode"] = UiRect(result.transportArea.x + result.transportArea.width - modeWidth,
					buttonY, modeWidth, buttonHeight);
			const float infoWidth = std::max(0.0f, result.transportArea.x + result.transportArea.width - infoX -
				(hasDemoMode ? modeWidth + layoutGap : 0.0f));
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
				std::max(0.0f, (setupSpace - rowGap * 5.0f - 120.0f) / 6.0f));
			const char* row0[] = { "shuffle", "insta_sort", "insta_invert" };
			const char* row1[] = { "algorithm" };
			const char* row2[] = { "direction" };
			const char* row3[] = { "bars_down", "bars_value", "bars_up" };
			const char* row4[] = { "time_down", "time_value", "time_up" };
			const char* row5[] = { "seed_value", "seed_copy", "seed_save", "seed_bookmarks", "seed_previous", "seed_next", "seed_remove" };
			const char* const* rowIds[] = { row0, row1, row2, row3, row4, row5 };
			const std::size_t rowSizes[] = { 3, 1, 1, 3, 3, 7 };
			for (int row = 0; row < 6; ++row)
				addExisting(result, std::vector<std::string>(rowIds[row], rowIds[row] + rowSizes[row]), controlIds,
					margin, setupTop + row * (rowHeight + rowGap), setupWidth, rowHeight, rowGap);
			const UiRect direction = result.control("direction");
			if (direction.width > 0.0f)
			{
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x, direction.y, direction.width * 0.5f, direction.height));
				result.interactiveSubregions["direction"].push_back(UiRect(direction.x + direction.width * 0.5f, direction.y,
					direction.width * 0.5f, direction.height));
			}
			const float chartY = setupTop + 6.0f * rowHeight + 5.0f * rowGap + contentGap;
			result.setupRail = UiRect(margin, setupTop, setupWidth, std::max(0.0f, chartY - setupTop - contentGap));
			result.chartViewport = UiRect(margin + inset, chartY,
				std::max(0.0f, setupWidth - inset * 2.0f),
				std::max(0.0f, transportTop - layoutGap - chartY));
			const float minimumButtonTop = 7.0f + timelineHeight + 3.0f + 30.0f;
			const float buttonHeight = std::min(requestedButtonHeight,
				std::max(0.0f, result.transportArea.height - minimumButtonTop - 8.0f));
			const float buttonY = result.transportArea.y + result.transportArea.height - buttonHeight - 8.0f;
			std::vector<std::string> narrowTransport = transportIds;
			if (std::find(controlIds.begin(), controlIds.end(), "demo_mode") != controlIds.end()) narrowTransport.push_back("demo_mode");
			placeRow(result, narrowTransport, result.transportArea.x, buttonY, result.transportArea.width, buttonHeight, layoutGap);
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
	: fill(), text(0.93f,0.96f,0.98f,1), border(0.25f,0.38f,0.43f,1), borderWidth(0), cornerRadius(0), paddingX(8), paddingY(4), shadowOffset(1), drawArrow(false), squareIndicator(false)
{
}

UiColor readableUiTextColor(const UiColor& preferred, const UiColor& surface, float minimumContrast)
{
	const auto luminance = [](const UiColor& color)
	{
		const auto linear = [](float channel) { return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f); };
		return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
	};
	const float background = luminance(surface);
	const float foreground = luminance(preferred);
	const float contrast = (std::max(background, foreground) + 0.05f) / (std::min(background, foreground) + 0.05f);
	if (contrast >= minimumContrast) return preferred;
	return (background + 0.05f) / 0.05f > 1.05f / (background + 0.05f)
		? UiColor(0, 0, 0, preferred.a) : UiColor(1, 1, 1, preferred.a);
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
	style.paddingX = numberToken(preset, theme, "control.padding_x", 8.0f);
	style.paddingY = numberToken(preset, theme, "control.padding_y", 4.0f);
	style.shadowOffset = numberToken(preset, theme, role == "panel" ? "panel.shadow_offset" : "control.shadow_offset", role == "panel" ? 5.0f : 1.0f);
	if (role == "panel") style.borderWidth = numberToken(preset, theme, "panel.border_width", 1.0f);
	style.drawArrow = role == "choice" || role == "dropdown";
	style.squareIndicator = preset == UiPresetId::FlatVgaDos || preset == UiPresetId::ClassicWindows95;
	if (!enabled) style.fill = color("color.disabled_button", UiColor(0.10f,0.16f,0.19f,1));
	else if (role == "panel") style.fill = color("color.panel", UiColor(0.10f,0.16f,0.20f,1));
	else if (selected) style.fill = color("color.accent", UiColor(0.29f,0.72f,0.64f,1));
	else if (role == "run") style.fill = color("color.run", UiColor(0.48f,0.28f,0.12f,1));
	else if (role == "play-active") style.fill = color("color.play_active", UiColor(0.18f,0.42f,0.34f,1));
	else if (role == "play") style.fill = color("color.button", UiColor(0.13f,0.24f,0.31f,1));
	else if (role == "reset") style.fill = preset == UiPresetId::ClassicWindows95
		? color("color.button", UiColor(0.75f,0.75f,0.75f,1))
		: color("color.reset", UiColor(0.30f,0.21f,0.35f,1));
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
	style.text = readableUiTextColor(style.text, style.fill, enabled ? 4.5f : 3.0f);
	return style;
}
