#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "Renderer2D.h"
#include "TextLabel.h"
#include "ThemePalette.h"
#include "UiLayer.h"
#include "UserSettings.h"
#include "Window.h"
#include "LayoutMetrics.h"
#include "../SortingLogic/SortingLogic.h"

class ControlPanel
{
public:
	ControlPanel(Window& window, Renderer2D& renderer, const UserPreferences& settings);
	~ControlPanel();

	ControlPanel(const ControlPanel&) = delete;
	ControlPanel& operator=(const ControlPanel&) = delete;

	bool isReady() const;
	bool handleEvent(const SDL_Event& event, AppAction& action);
	void render(const sortinglogic::RenderSnapshot& snapshot, const std::string& persistenceStatus,
		const std::string& audioStatus = "Audio device is not initialized.");
	float chartTop() const { return chartTop_; }
	bool settingsOpen() const { return settingsOpen_; }

private:
	Window& window_;
	Renderer2D& renderer_;
	const UserPreferences& settings_;
	UIRoot mainUi_;
	UIRoot settingsUi_;
	std::map<std::string, std::unique_ptr<TextLabel> > labels_;
	std::map<std::string, std::string> settingsCaptions_;
	std::unique_ptr<TextLabel> paletteCaption_;
	std::unique_ptr<TextLabel> spacingCaption_;
	std::unique_ptr<TextLabel> title_;
	std::unique_ptr<TextLabel> detail_;
	std::unique_ptr<TextLabel> status_;
	std::unique_ptr<TextLabel> settingsTitle_;
	std::unique_ptr<TextLabel> settingsSaveStatus_;
	std::unique_ptr<TextLabel> audioDeviceStatus_;
	std::string displayedDetail_;
	std::string displayedStatus_;
	std::string displayedSaveStatus_;
	Uint32 lastDetailUpdate_;
	UiRect timelineBounds_;
	UiRect headerBounds_;
	UiRect gearBounds_;
	UiRect settingsPanelBounds_;
	UiRect settingsContentBounds_;
	bool draggingTimeline_;
	bool settingsOpen_;
	bool algorithmMenuOpen_;
	bool algorithmMenuVariantPane_;
	bool algorithmSearchFocused_;
	std::string algorithmSearch_;
	sortinglogic::FamilyId algorithmMenuFamily_;
	int algorithmFamilySelection_;
	int algorithmVariantSelection_;
	int algorithmFamilyScroll_;
	int algorithmVariantScroll_;
	UiRect algorithmMenuBounds_;
	UiRect algorithmSearchBounds_;
	UiRect algorithmFamilyBounds_;
	UiRect algorithmVariantBounds_;
	int settingsTab_;
	bool audioAdvanced_;
	float settingsScrollOffset_;
	int selectedBand_;
	float chartTop_;
	int width_;
	int height_;
	LayoutCacheKey layoutKey_;
	bool layoutCacheValid_;
	std::string inlineEditWidgetId_;
	std::string inlineEditBuffer_;
	bool inlineEditReplaceOnNextDigit_;

	void createWidgets();
	void updateLayout();
	void updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot);
	void syncLabels(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette);
	void drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette);
	void drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus);
	void layoutSettingsControls();
	void updateAudioControls();
	void updateAlgorithmAudioControls(const sortinglogic::AlgorithmDescriptor& algorithm);
	void drawSettingsControl(const UiWidget& widget, const ThemePalette& palette);
	void drawWidget(const UiWidget& widget, const ThemePalette& palette, RenderLayer layer, bool popup = false);
	void drawText(TextLabel& label, const UiRect& bounds, float targetHeight, const glm::vec4& tint, RenderLayer layer, const ClipBounds& clip = ClipBounds());
	void submitBox(const UiRect& bounds, const glm::vec4& color, MaterialId material, RenderLayer layer, const ClipBounds& clip = ClipBounds());
	void processAction(const UiAction& action, AppAction& output);
	void closeSettings();
	void layoutAlgorithmMenu();
	void drawAlgorithmMenu(const ThemePalette& palette);
	bool handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action);
	void openAlgorithmMenu();
	void closeAlgorithmMenu();
	void beginInlineEdit(const std::string& widgetId);
	void updateInlineEditLabel();
	bool appendInlineEditText(const char* text);
	bool commitInlineEdit(AppAction& action);
	void cancelInlineEdit();
	void selectAlgorithmMenuVariant(const sortinglogic::AlgorithmDescriptor& algorithm, AppAction& action);
	float timelineFraction(float x) const;
};
