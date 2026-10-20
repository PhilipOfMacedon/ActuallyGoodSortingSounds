#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AppearanceEditorView.h"
#include "MainControlsView.h"
#include "InlineEditModel.h"
#include "AlgorithmBrowserView.h"
#include "AlgorithmBrowserModel.h"
#include "RenderingTypes.h"
#include "SettingsPanelView.h"
#include "SettingsPanelControls.h"
#include "AlgorithmProfileView.h"
#include "TimelineView.h"
#include "TextLabel.h"
#include "ThemePalette.h"
#include "UiLayer.h"
#include "UiArchitecture.h"
#include "UserSettings.h"
#include "LayoutMetrics.h"

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
	void completeActionResult(const AppAction& action, const sortinglogic::CommandResult& result);
	// Paint the frame prepared before chart rendering without recalculating layout.
	void render(const sortinglogic::RenderSnapshot& snapshot,
		const std::string& audioStatus = "Audio device is not initialized.");
	const UiRect& chartViewport() const { return chartViewport_; }
	bool settingsOpen() const { return overlayState_.settingsSurfaceOpen(); }
	bool textEntryActive() const
	{
		return inlineEdit_.active() || appearanceSearchFocused_ ||
			(overlayState_.is(UiOverlayId::AlgorithmBrowser) && algorithmBrowser_.searchFocused);
	}
	bool modalUiOpen() const { return overlayState_.modalOpen(); }

private:
	Window& window_;
	Renderer2D& renderer_;
	const UserPreferences& settings_;
	MainControlsView mainControlsView_;
	UIRoot& mainUi_; // Temporary access alias while input, layout, and paint move behind the view boundary.
	TimelineView timelineView_;
	AppearanceEditorView appearanceEditorView_;
	UiOverlayState overlayState_;
	AlgorithmBrowserModel algorithmBrowser_;
	AlgorithmBrowserView algorithmBrowserView_;
	SettingsPanelView settingsPanelView_;
	AlgorithmProfileView algorithmProfileView_;
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
	UiRect headerBounds_;
	UiRect gearBounds_;
	bool appearanceSearchFocused_;
	bool drawingAppearanceEditor_;
	UiRect chartViewport_;
	int width_;
	int height_;
	LayoutCacheKey layoutKey_;
	bool layoutCacheValid_;
	UiPresetId laidOutPreset_;
	bool fontFallbackActive_;
	InlineEditModel inlineEdit_;

	void createWidgets();
	void updateLayout();
	void updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot, bool playbackPending);
	void syncLabels(const sortinglogic::RenderSnapshot& snapshot);
	void drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette);
	void drawMainControlPaintCommand(const UiPaintCommand& command, const ThemePalette& palette);
	void drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus);
	void drawSettingsPaintCommand(const UiPaintCommand& command, const ThemePalette& palette);
	void drawAppearanceEditor();
	void layoutSettingsControls();
	void drawSettingsControl(const UiWidget& widget, const ThemePalette& palette);
	void drawSliderControl(const UiWidget& widget, const ThemePalette& palette, const ClipBounds& clip, RenderLayer layer);
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
	void layoutMainComboPopups();
	void drawUiPaintCommand(const UiPaintCommand& command);
	void drawAlgorithmMenu(const ThemePalette& palette);
	bool handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action);
	void openAlgorithmMenu();
	void closeAlgorithmMenu();
	void beginInlineEdit(const std::string& widgetId);
	void updateInlineEditLabel();
	bool appendInlineEditText(const char* text);
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
