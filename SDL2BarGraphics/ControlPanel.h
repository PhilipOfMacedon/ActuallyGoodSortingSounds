#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AppearanceEditorView.h"
#include "AlgorithmBrowserView.h"
#include "AlgorithmBrowserModel.h"
#include "RenderingTypes.h"
#include "SettingsPanelView.h"
#include "SettingsPanelModel.h"
#include "SettingsPanelControls.h"
#include "SettingsPanelLayout.h"
#include "AlgorithmProfileView.h"
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
		return !inlineEditWidgetId_.empty() || appearanceSearchFocused_ ||
			(overlayState_.is(UiOverlayId::AlgorithmBrowser) && algorithmBrowser_.searchFocused);
	}
	bool modalUiOpen() const { return overlayState_.modalOpen(); }

private:
	Window& window_;
	Renderer2D& renderer_;
	const UserPreferences& settings_;
	UIRoot mainUi_;
	UIRoot settingsUi_;
	AppearanceEditorView appearanceEditorView_;
	SettingsPanelModel settingsPanelModel_;
	UiOverlayState overlayState_;
	AlgorithmBrowserModel algorithmBrowser_;
	AlgorithmBrowserView algorithmBrowserView_;
	SettingsPanelView settingsPanelView_;
	AlgorithmProfileView algorithmProfileView_;
	UiScreenModel screenModel_;
	UiLayoutResult layoutResult_;
	UserPreferences renderPreferences_;
	std::map<std::string, std::unique_ptr<TextLabel> > labels_;
	std::map<std::string, std::string> settingsCaptions_;
	std::unique_ptr<TextLabel> paletteCaption_;
	std::unique_ptr<TextLabel> spacingCaption_;
	std::unique_ptr<TextLabel> title_;
	std::unique_ptr<TextLabel> detail_;
	std::unique_ptr<TextLabel> status_;
	std::unique_ptr<TextLabel> collapsedSummary_;
	std::unique_ptr<TextLabel> settingsTitle_;
	std::unique_ptr<TextLabel> settingsSaveStatus_;
	std::unique_ptr<TextLabel> audioDeviceStatus_;
	std::string displayedDetail_;
	std::string displayedStatus_;
	std::string displayedCollapsedSummary_;
	std::string displayedSaveStatus_;
	Uint32 lastDetailUpdate_;
	UiRect timelineBounds_;
	UiRect timelineControlBounds_;
	UiRect timelineLeftArrowBounds_;
	UiRect timelineRightArrowBounds_;
	UiRect headerBounds_;
	UiRect gearBounds_;
	UiRect settingsPanelBounds_;
	UiRect settingsContentBounds_;
	bool draggingTimeline_;
	bool appearanceSearchFocused_;
	bool drawingAppearanceEditor_;
	UiRect chartViewport_;
	int width_;
	int height_;
	LayoutCacheKey layoutKey_;
	bool layoutCacheValid_;
	UiPresetId laidOutPreset_;
	bool fontFallbackActive_;
	std::string inlineEditWidgetId_;
	std::string inlineEditBuffer_;
	bool inlineEditReplaceOnNextDigit_;
	bool inlineEditAppearanceValue_;

	void createWidgets();
	void updateLayout();
	void updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot, bool playbackPending);
	void syncLabels(const sortinglogic::RenderSnapshot& snapshot);
	void drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette);
	void drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus);
	void drawAppearanceEditor();
	void layoutSettingsControls();
	void updateAudioControls();
	void updateAlgorithmAudioControls(const sortinglogic::AlgorithmDescriptor& algorithm);
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
	void refreshAppearanceTokens();
	void updateAppearanceEditorControls();
	void layoutAppearanceEditor();
	bool handleAppearanceEditorEvent(const SDL_Event& event, AppAction& action);
	void processAppearanceEditorAction(const UiAction& action, AppAction& output);
	void resetAppearanceToken();
	void adjustAppearanceNumber(float direction);
	void chooseAppearanceColor();
	void importAppearanceProfile();
	void exportAppearanceProfile();
	float timelineFraction(float x) const;
};
