#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AppearanceEditorView.h"
#include "MainControlsView.h"
#include "InlineEditView.h"
#include "AlgorithmBrowserView.h"
#include "AlgorithmBrowserModel.h"
#include "RenderingTypes.h"
#include "SettingsPanelView.h"
#include "AlgorithmProfileView.h"
#include "TimelineView.h"
#include "TextLabel.h"
#include "ThemePalette.h"
#include "UiLayer.h"
#include "UiArchitecture.h"
#include "UserSettings.h"
#include "LayoutMetrics.h"
#include "SeedLibrary.h"
#include "DemoEditorView.h"

union SDL_Event;
class Renderer2D;
class Window;

namespace sortinglogic
{
	struct AlgorithmDescriptor;
	struct CommandResult;
	struct RenderSnapshot;
}

class ControlPanel
{
public:
	ControlPanel(Window& window, Renderer2D& renderer, const UserPreferences& settings);
	~ControlPanel();

	ControlPanel(const ControlPanel&) = delete;
	ControlPanel& operator=(const ControlPanel&) = delete;

	bool isReady() const;
	bool handleEvent(const SDL_Event& event, AppAction& action);
	// Resolve presentation state and geometry before chart rendering. Painting
	// consumes this prepared frame so the chart and controls share one viewport.
	void prepareFrame(const sortinglogic::RenderSnapshot& snapshot, const std::string& persistenceStatus,
		bool playbackPending);
	void setSeedLibraryState(const std::vector<SeedBookmark>& bookmarks, std::size_t selectedIndex,
		bool canPrevious, bool canNext);
	void setDemoEditorState(const DemoPresetStore& presets, const DemoPreset& draft, const std::string& selectedPresetId,
		std::size_t selectedStep, const DemoRunner& runner, bool editorOpen);
	void completeActionResult(const AppAction& action, const sortinglogic::CommandResult& result);
	// Paint the frame prepared before chart rendering without recalculating layout.
	void render(const sortinglogic::RenderSnapshot& snapshot,
		const std::string& audioStatus = "Audio device is not initialized.");
	const UiRect& chartViewport() const { return chartViewport_; }
	bool settingsOpen() const { return overlayState_.settingsSurfaceOpen(); }
	bool textEntryActive() const
	{
		return inlineEditView_.active() || appearanceEditorView_.searchFocused() ||
			(overlayState_.is(UiOverlayId::AlgorithmBrowser) && algorithmBrowser_.searchFocused) ||
			demoEditorView_.editingName() || demoEditorView_.editingNumeric();
	}
	bool modalUiOpen() const { return overlayState_.modalOpen(); }

private:
	Window& window_;
	Renderer2D& renderer_;
	const UserPreferences& settings_;
	InlineEditView inlineEditView_;
	MainControlsView mainControlsView_;
	TimelineView timelineView_;
	AppearanceEditorView appearanceEditorView_;
	UiOverlayState overlayState_;
	AlgorithmBrowserModel algorithmBrowser_;
	AlgorithmBrowserView algorithmBrowserView_;
	SettingsPanelView settingsPanelView_;
	AlgorithmProfileView algorithmProfileView_;
	DemoEditorView demoEditorView_;
	UiScreenModel screenModel_;
	UiLayoutResult layoutResult_;
	UserPreferences renderPreferences_;
	std::map<std::string, std::unique_ptr<TextLabel> > labels_;
	std::unique_ptr<TextLabel> paletteCaption_;
	std::unique_ptr<TextLabel> spacingCaption_;
	std::unique_ptr<TextLabel> title_;
	std::unique_ptr<TextLabel> detail_;
	std::unique_ptr<TextLabel> status_;
	std::unique_ptr<TextLabel> collapsedSummary_;
	std::unique_ptr<TextLabel> settingsTitle_;
	std::unique_ptr<TextLabel> settingsSaveStatus_;
	std::unique_ptr<TextLabel> audioDeviceStatus_;
	std::string displayedSaveStatus_;
	std::vector<std::string> seedBookmarkLabels_;
	std::size_t selectedSeedBookmark_;
	bool canPreviousSeed_;
	bool canNextSeed_;
	std::uint32_t currentSeed_;
	const DemoPresetStore* demoPresets_;
	const DemoPreset* demoDraft_;
	const DemoRunner* demoRunner_;
	std::string selectedDemoPresetId_;
	std::size_t selectedDemoStep_;
	bool demoEditorOpen_;
	UiRect headerBounds_;
	UiRect gearBounds_;
	bool drawingAppearanceEditor_;
	UiRect chartViewport_;
	int width_;
	int height_;
	LayoutCacheKey layoutKey_;
	bool layoutCacheValid_;
	UiPresetId laidOutPreset_;
	bool fontFallbackActive_;

	void createWidgets();
	void updateLayout();
	void layoutInteractiveViews();
	void updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot, bool playbackPending);
	void syncLabels(const sortinglogic::RenderSnapshot& snapshot);
	void drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette);
	void drawMainControlPaintCommand(const UiPaintCommand& command, const ThemePalette& palette);
	void drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus);
	void drawSettingsPaintCommand(const UiPaintCommand& command, const ThemePalette& palette);
	void drawAppearanceEditor();
	bool handleDemoEditorEvent(const SDL_Event& event, AppAction& action);
	void drawDemoEditor(const ThemePalette& palette);
	void layoutSettingsControls();
	void drawSettingsControl(const UiWidget& widget, const ThemePalette& palette);
	void drawSliderControl(const UiWidget& widget, const ThemePalette& palette, const ClipBounds& clip, RenderLayer layer);
	void drawSurface(const UiRect& bounds, const UiControlPaintStyle& style, UiPresetId preset, RenderLayer layer,
		const ClipBounds& clip, bool pressed = false);
	void drawWidget(const UiWidget& widget, const ThemePalette& palette, RenderLayer layer, bool popup = false);
	ResolvedTextStyle resolveTextStyle(float targetHeight) const;
	void drawText(TextLabel& label, const UiRect& bounds, float targetHeight, const glm::vec4& tint, RenderLayer layer,
		const ClipBounds& clip = ClipBounds(), TextOverflowPolicy overflow = TextOverflowPolicy::Fit,
		TextHorizontalAlignment alignment = TextHorizontalAlignment::Center);
	void drawText(TextLabel& label, const UiRect& bounds, const ResolvedTextStyle& style, const glm::vec4& tint,
		RenderLayer layer, const ClipBounds& clip = ClipBounds(), TextOverflowPolicy overflow = TextOverflowPolicy::Fit,
		TextHorizontalAlignment alignment = TextHorizontalAlignment::Center);
	void submitBox(const UiRect& bounds, const glm::vec4& color, MaterialId material, RenderLayer layer, const ClipBounds& clip = ClipBounds());
	void processAction(const UiAction& action, AppAction& output);
	bool appearanceEditorOpen() const { return overlayState_.is(UiOverlayId::AppearanceEditor); }
	bool algorithmBrowserOpen() const { return overlayState_.is(UiOverlayId::AlgorithmBrowser); }
	void updateOverlayInputCapture();
	void closeSettings();
	void layoutAlgorithmMenu();
	void drawUiPaintCommand(const UiPaintCommand& command);
	void drawAlgorithmMenu(const ThemePalette& palette);
	bool handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action);
	void openAlgorithmMenu();
	void closeAlgorithmMenu();
	void beginInlineEdit(const std::string& widgetId);
	bool commitInlineEdit(AppAction& action);
	void cancelInlineEdit();
	void openAppearanceEditor();
	void closeAppearanceEditor(bool restore);
	void synchronizeAppearanceEditorControls();
	void layoutAppearanceEditor();
	bool handleAppearanceEditorEvent(const SDL_Event& event, AppAction& action);
	void processAppearanceEditorAction(const UiAction& action, AppAction& output);
	void chooseAppearanceColor();
	void importAppearanceProfile();
	void exportAppearanceProfile();
};
