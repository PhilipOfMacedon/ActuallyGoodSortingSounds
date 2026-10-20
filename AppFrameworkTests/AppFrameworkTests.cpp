#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <iostream>
#include <cmath>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

#include "../SDL2BarGraphics/LayoutMetrics.h"
#include "../SDL2BarGraphics/BarCountSteps.h"
#include "../SDL2BarGraphics/AlgorithmBrowserModel.h"
#include "../SDL2BarGraphics/AlgorithmBrowserView.h"
#include "../SDL2BarGraphics/AppController.h"
#include "../SDL2BarGraphics/AudioPresentationBridge.h"
#include "../SDL2BarGraphics/RenderPipeline.h"
#include "../SDL2BarGraphics/RenderingTypes.h"
#include "../SDL2BarGraphics/SettingsScroll.h"
#include "../SDL2BarGraphics/TextBlockLayout.h"
#include "../SDL2BarGraphics/UiLayer.h"
#include "../SDL2BarGraphics/UiArchitecture.h"
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

	void waitForAudioTimeline(AudioPresentationBridge& bridge, const sortinglogic::Simulation& simulation,
		const SoundEngine::Parameters& parameters, LatestAudioStateMailbox& mailbox,
		const SoundEngine::ReconstructedParameters& reconstructed = SoundEngine::ReconstructedParameters(),
		SoundEngine::PlaybackTimingMode mode = SoundEngine::PlaybackTimingMode::FixedDuration,
		bool audioDeviceAvailable = false)
	{
		for (unsigned int attempt = 0; attempt < 5000; ++attempt)
		{
			bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, audioDeviceAvailable);
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
		check(stepDeltaWithModifiers(10.0f, false, false) == 10.0f &&
			stepDeltaWithModifiers(10.0f, true, false) == 1.0f &&
			stepDeltaWithModifiers(10.0f, false, true) == 100.0f &&
			stepDeltaWithModifiers(10.0f, true, true) == 10.0f,
			"bar step uses 10 units by default, Ctrl divides by ten, and Shift multiplies by ten");
		check(stepDeltaWithModifiers(1.0f, false, false) == 1.0f &&
			std::fabs(stepDeltaWithModifiers(1.0f, true, false) - 0.1f) < 0.00001f &&
			stepDeltaWithModifiers(1.0f, false, true) == 10.0f &&
			stepDeltaWithModifiers(1.0f, true, true) == 1.0f,
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
		AlgorithmBrowserModel browser;
		const std::vector<AlgorithmFamilyEntry>& catalog = browser.families();
		check(!catalog.empty(), "algorithm browser model exposes the registered family catalog");
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		check(insertion != nullptr, "algorithm browser fixture resolves the registered insertion method");
		if (!insertion) return;

		browser.begin(insertion->familyId);
		check(browser.open && browser.searchFocused && browser.selectedFamily == insertion->familyId,
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
		const std::vector<const sortinglogic::AlgorithmDescriptor*> insertionVariants = browser.visibleVariants(insertionFamily);
		check(insertionFamily && !insertionVariants.empty() && insertionVariants[0] == insertion,
			"the selected family exposes its registered variants in catalog order");

		browser.search = "insertion sort";
		const std::vector<const AlgorithmFamilyEntry*> lowercaseFamilies = browser.visibleFamilies();
		const std::vector<const sortinglogic::AlgorithmDescriptor*> lowercaseVariants = browser.visibleVariants(insertionFamily);
		browser.search = "INSERTION SORT";
		const std::vector<const AlgorithmFamilyEntry*> filteredFamilies = browser.visibleFamilies();
		const AlgorithmFamilyEntry* filteredFamily = nullptr;
		for (std::size_t i = 0; i < filteredFamilies.size(); ++i)
			if (filteredFamilies[i]->id == insertion->familyId) filteredFamily = filteredFamilies[i];
		const std::vector<const sortinglogic::AlgorithmDescriptor*> filteredVariants = browser.visibleVariants(filteredFamily);
		bool insertionFound = false;
		bool variantsRemainInCatalogOrder = true;
		std::size_t previousCatalogIndex = 0;
		for (std::size_t i = 0; i < filteredVariants.size(); ++i)
		{
			if (filteredVariants[i] == insertion) insertionFound = true;
			std::size_t catalogIndex = 0;
			while (catalogIndex < insertionFamily->variants.size() && insertionFamily->variants[catalogIndex] != filteredVariants[i])
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
		browser.close();
		check(!browser.open && !browser.searchFocused && !browser.pendingSelection,
			"closing the browser releases its modal search and pending-selection state");
	}

	void testAlgorithmBrowserInputAndLayout()
	{
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		if (!insertion) return;

		AlgorithmBrowserModel browser;
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
		browser.handleInput(wheel, true, static_cast<int>(insertion->id));
		check(browser.familyScroll > 0 && browser.familyViewport.offset() > 0.0f,
			"browser wheel input updates the family viewport scroll position");
		const std::vector<AlgorithmFamilyEntry>& allFamilies = browser.families();
		const AlgorithmFamilyEntry* largestFamily = allFamilies.empty() ? nullptr : &allFamilies[0];
		for (std::size_t i = 1; i < allFamilies.size(); ++i)
			if (!largestFamily || allFamilies[i].variants.size() > largestFamily->variants.size())
				largestFamily = &allFamilies[i];
		browser.layout(UiRect(540.0f, 194.0f, 80.0f, 30.0f), 640, 240);
		browser.selectedFamily = largestFamily ? largestFamily->id : sortinglogic::FamilyId();
		browser.variantScroll = 0;
		browser.prepare();
		check(browser.variantViewport.maximumOffset() > 0.0f,
			"the largest registered family exposes a scrollable variant viewport");
		const UiInputEvent variantWheel(UiInputType::Wheel, browser.variantBounds.x + 4.0f,
			browser.variantBounds.y + 4.0f, -1);
		browser.handleInput(variantWheel, true, static_cast<int>(insertion->id));
		check(largestFamily && browser.variantScroll > 0 && browser.variantViewport.offset() > 0.0f,
			"browser wheel input updates the selected family variant viewport");

		AlgorithmBrowserModel keyboard;
		keyboard.begin(insertion->familyId);
		keyboard.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
			static_cast<int>(insertion->id));
		check(keyboard.familySelection == 1 && !keyboard.searchFocused,
			"down arrow leaves search and moves keyboard focus to the next family");
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Tab), true,
			static_cast<int>(insertion->id));
		check(keyboard.searchFocused,
			"tab returns keyboard focus to the browser search field");
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Tab), true,
			static_cast<int>(insertion->id));
		keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Right), true,
			static_cast<int>(insertion->id));
		check(keyboard.variantPane,
			"right arrow opens the selected family variant pane");
		const std::vector<const AlgorithmFamilyEntry*> keyboardFamilies = keyboard.visibleFamilies();
		const AlgorithmFamilyEntry* keyboardFamily = keyboardFamilies[static_cast<std::size_t>(keyboard.familySelection)];
		const std::vector<const sortinglogic::AlgorithmDescriptor*> keyboardVariants = keyboard.visibleVariants(keyboardFamily);
		if (keyboardVariants.size() > 1)
			keyboard.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
				static_cast<int>(insertion->id));
		const std::size_t selectedVariant = static_cast<std::size_t>(keyboard.variantSelection);
		const AlgorithmBrowserInputResult selected = keyboard.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Enter), true,
			static_cast<int>(insertion->id));
		check(selected.selectionRequested && keyboard.pendingSelection && selectedVariant < keyboardVariants.size() &&
			selected.familyId == keyboardVariants[selectedVariant]->familyId &&
			selected.variantId == keyboardVariants[selectedVariant]->variantId,
			"enter emits a typed selection for the keyboard-highlighted catalog variant");

		AlgorithmBrowserModel busy;
		busy.begin(insertion->familyId);
		busy.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Down), true,
			static_cast<int>(insertion->id));
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Right), true,
			static_cast<int>(insertion->id));
		const AlgorithmBrowserInputResult rejected = busy.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Enter), false,
			static_cast<int>(insertion->id));
		check(!rejected.selectionRequested && !busy.pendingSelection && !busy.status.empty(),
			"busy setup state rejects keyboard selection with visible feedback");
		busy.handleInput(UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Escape), true,
			static_cast<int>(insertion->id));
		check(!busy.variantPane,
			"escape backs out of the variant pane without closing the browser");
		const AlgorithmBrowserInputResult close = busy.handleInput(
			UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, UiKey::Escape), true,
			static_cast<int>(insertion->id));
		check(close.closeRequested,
			"escape from the family pane requests modal dismissal");

		AlgorithmBrowserModel pointer;
		pointer.begin(insertion->familyId);
		pointer.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		pointer.handleInput(UiInputEvent(UiInputType::TextInput, std::string("quick")), true,
			static_cast<int>(insertion->id));
		pointer.prepare();
		const std::vector<const AlgorithmFamilyEntry*> quickFamilies = pointer.visibleFamilies();
		check(!quickFamilies.empty(),
			"text input filters the browser catalog before pointer selection");
		if (!quickFamilies.empty())
		{
			pointer.handleInput(UiInputEvent(UiInputType::PointerDown, pointer.familyBounds.x + 4.0f,
				pointer.familyBounds.y + 4.0f), true, static_cast<int>(insertion->id));
			const std::vector<const sortinglogic::AlgorithmDescriptor*> quickVariants = pointer.visibleVariants(
				quickFamilies[0]);
			const UiRect firstVariant = pointer.variantViewport.itemBounds(0, 30.0f);
			const AlgorithmBrowserInputResult clicked = pointer.handleInput(UiInputEvent(UiInputType::PointerDown,
				firstVariant.x + 4.0f, firstVariant.y + 4.0f), true, static_cast<int>(insertion->id));
			check(clicked.selectionRequested && !quickVariants.empty() && clicked.familyId == quickVariants[0]->familyId &&
				clicked.variantId == quickVariants[0]->variantId,
				"pointer selection emits the typed variant action through the same browser model");
		}

		AlgorithmBrowserModel outside;
		outside.begin(insertion->familyId);
		outside.layout(UiRect(20.0f, 20.0f, 140.0f, 30.0f), 800, 600);
		const AlgorithmBrowserInputResult dismissed = outside.handleInput(
			UiInputEvent(UiInputType::PointerDown, 799.0f, 599.0f), true, static_cast<int>(insertion->id));
		check(dismissed.closeRequested,
			"outside pointer input requests modal dismissal");
	}

	void testAlgorithmBrowserPaintComposition()
	{
		const sortinglogic::AlgorithmDescriptor* insertion = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		check(insertion != nullptr, "browser paint fixture uses a registered algorithm");
		if (!insertion) return;
		AlgorithmBrowserModel browser;
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
		view.compose(browser, palette, true, static_cast<int>(insertion->id), 800, 600, recorder);
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
		view.compose(browser, palette, false, static_cast<int>(insertion->id), 800, 600, recorder);
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
		bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, true);
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		simulation.seekTo(simulation.snapshot().eventCount / 2);
		bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, true);
		check(!bridge.playheadReady(bridge.timelineGeneration(), ~std::uint64_t(0)),
			"a preparing seek rejects playhead feedback even with an older callback acknowledgement");
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		AudioPresentationState prepared;
		check(mailbox.consumeLatest(prepared) && prepared.seekState && prepared.seekRevision > 0,
			"mid-trace inspection produces one prepared seek snapshot");
		check(!bridge.playheadReady(prepared.timelineGeneration, prepared.seekRevision - 1) &&
			bridge.playheadReady(prepared.timelineGeneration, prepared.seekRevision),
			"temporary stopped feedback stays fenced until the callback acknowledges the installed seek");
		const auto revision = bridge.transportRevision();
		for (unsigned frame = 0; frame < 100; ++frame)
			bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, true);
		check(!bridge.timelinePreparing() && bridge.transportRevision() == revision,
			"finishing preparation is not mistaken for device recovery and does not start another seek");
		bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, false);
		bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, true);
		check(bridge.timelinePreparing() && bridge.transportRevision() == revision + 1,
			"a real device recovery reconstructs the inspected position once");
		waitForAudioTimeline(bridge, simulation, parameters, mailbox, reconstructed, mode, true);
		for (unsigned frame = 0; frame < 100; ++frame)
			bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode, true);
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
		bridge.update(simulation, snapshot, parameters, traceMailbox);
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
		bridge.update(simulation, snapshot, parameters, traceMailbox);
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
			gnomeBridge.update(gnome, gnome.snapshot(), parameters, gnomeMailbox);
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
		bridge.update(simulation, snapshot, parameters, traceMailbox, releasePreferences);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison && latest.releaseSelection && !latest.transportPlaying,
			"paused inspection publishes the configured special-release policy to the audio callback");
		const SoundEngine::PreparedAudioTimeline* stableTimeline = latest.timeline;
		parameters.heldKeyBoostDb = 0.0f;
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 0.0f &&
			latest.timeline == stableTimeline && std::fabs(latest.timelineGains.heldKeyGain - 1.0) < 1.0e-9 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio bridge applies live held-key gain changes without rebuilding the immutable timeline");
		parameters.heldKeyBoostDb = 18.0f;
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 18.0f &&
			std::fabs(latest.timelineGains.heldKeyGain - std::pow(10.0, 18.0 / 20.0)) < 1.0e-6 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio callback mailbox receives the full held-key offset range for a held comparison");

		const std::size_t laterCursor = trace->events.size() / 2;
		simulation.seekTo(laterCursor);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] > 0,
			"coalesced frame summary includes movement events crossed during playback");
		simulation.seekTo(firstCompareCursor);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] == 0 &&
			std::fabs(latest.comparison.firstPosition - SoundEngine::normalizeValue(firstValue, minimum, maximum)) < 1e-6f,
			"rewinding restores the comparison selected by the destination cursor");
		simulation.seekTo(trace->events.size());
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"completed trace retains the bed and clears comparison focus");
		simulation.reset();
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
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
			probeBridge.update(counting, counting.snapshot(), parameters, probeMailbox);
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
			countingBridge.update(counting, counting.snapshot(), parameters, countingMailbox);
			check(countingMailbox.consumeLatest(latest) &&
				std::fabs(latest.eventAccents.representativePositions[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] - 1.0f) < 1e-6f,
				"counter accents normalize against the counter workspace range rather than the primary array");
		}
		simulation.prepareShuffle(0x4321u);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
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
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		denseMailbox.consumeLatest(latest);
		const std::size_t middleCursor = denseTrace->events.size() / 2;
		dense.seekTo(middleCursor);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"dense 800-item seek publishes the current comparison without waiting for skipped events");
		const std::uint64_t middleRevision = latest.revision;
		dense.seekTo(denseTrace->events.size() * 3 / 4);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.revision > middleRevision && latest.hasComparison,
			"dense forward playback replaces the prior mailbox state with the latest comparison");
		dense.seekTo(denseTrace->events.size());
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"dense completion leaves only the bed in the audio state");
		dense.reset();
		dense.togglePlayback();
		dense.advance(0.5f);
		const sortinglogic::RenderSnapshot halfway = dense.snapshot();
		denseBridge.update(dense, halfway, parameters, denseMailbox);
		check(halfway.playing && halfway.cursor == denseTrace->events.size() / 2 &&
			denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"800-item playback reaches the halfway comparison without an audio-event backlog");
		dense.advance(0.5f);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
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
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "audio-preparation gate fixture receives its trace");
		app.setAudioPreparationPending(true);
		check(!app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && !app.snapshot().playing,
			"Play is rejected with an actionable status while its immutable audio timeline is preparing");
		app.setAudioPreparationPending(false);
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && app.snapshot().playing,
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
		app.applyAudioPlayhead(stalePause, 7, 11);
		check(app.snapshot().playing,
			"an older paused feedback snapshot cannot cancel a newer Play transport revision");

		SoundEngine::AudioPlayheadFeedback currentPlayhead;
		currentPlayhead.generation = 7;
		currentPlayhead.transportRevision = 11;
		currentPlayhead.durationFrames = 44100 * 10;
		currentPlayhead.timelineFrame = 4410;
		currentPlayhead.sourceCursor = 12;
		currentPlayhead.playing = true;
		app.applyAudioPlayhead(currentPlayhead, 7, 11);
		check(app.snapshot().playing && app.snapshot().cursor == 12,
			"feedback for the current transport revision advances the visible playhead");

		currentPlayhead.timelineFrame = currentPlayhead.durationFrames;
		currentPlayhead.sourceCursor = app.snapshot().eventCount;
		currentPlayhead.playing = false;
		app.applyAudioPlayhead(currentPlayhead, 7, 11);
		check(!app.snapshot().playing,
			"completion feedback for the current revision still pauses the simulation");
	}
}

int main()
{
	testSpacing();
	testBarCountSteps();
	testModifierStepSizes();
	testAppearanceAndPresetArchitecture();
	testWindowsFontCatalog();
	testTextBlockLayoutCache();
	testRenderPipelineContracts();
	testLayoutAndWidgets();
	testAlgorithmBrowserModel();
	testAlgorithmBrowserInputAndLayout();
	testAlgorithmBrowserPaintComposition();
	testSettingsStore();
	testFractionalDurationStepPersists();
	testAppControllerDispatchAndPersistence();
	testAppearanceApplyIsTransactionalAndTransportNeutral();
	testRestoreReconstructedDefaultsScope();
	testShowcaseActions();
	testDistributionAlgorithmsSelectableFromDefaultInput();
	testMetadataAlgorithmOptions();
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
