#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "DemoModels.h"
#include "DemoRunner.h"
#include "UiArchitecture.h"

struct AppAction;
struct DemoActionPayload;

enum class DemoEditorTab { Step = 0, ClassInputs = 1, Session = 2 };
enum class DemoAlgorithmPickerIntent { None, Add, Replace };

struct DemoEditorPalette
{
	UiColor background, panel, button, valueButton, disabledButton, accent, text, mutedText;
	UiPresetId preset;
	UiThemeCustomization theme;
	DemoEditorPalette() : background(), panel(), button(), valueButton(), disabledButton(), accent(), text(), mutedText(),
		preset(UiPresetId::Current), theme() {}
};

// Retained editor view. It owns navigation, component interaction and field
// drafts only; AppController remains authoritative for demo configuration.
class DemoEditorView
{
public:
	DemoEditorView();
	void synchronize(const DemoPresetStore& presets, const DemoPreset& draft,
		const std::string& selectedPresetId, std::size_t selectedStep,
		const DemoRunner& runner, bool setupEnabled, int logicalWidth, int logicalHeight,
		bool draftDirty = false, const std::string& pendingPresetSwitchId = std::string(),
		UiPresetId appearancePreset = UiPresetId::Current, float dpiScale = 1.0f);
	bool handleInput(const UiInputEvent& input, AppAction& action);
	void compose(const DemoEditorPalette& palette, int logicalWidth, int logicalHeight,
		UiPaintContext& paintContext) const;
	bool open() const { return open_; }
	void setOpen(bool value);
	void setFeedback(const std::string& message) { feedback_ = message; }
	bool editingName() const;
	bool editingNumeric() const;
	const UiWidget* control(const std::string& id) const { return controls_.find(id); }
	const UiTabStrip* tabStrip(const std::string& id) const { return controls_.findTabStrip(id); }
	const UiSelectionList* selectionList(const std::string& id) const { return controls_.findSelectionList(id); }
	const UiEditableField* editableField(const std::string& id) const { return controls_.findEditableField(id); }
	const UiRect& panelBounds() const { return panelBounds_; }
	DemoEditorTab activeTab() const { return activeTab_; }
	DemoAlgorithmPickerIntent takeAlgorithmPickerIntent();
	void layoutComboPopups(const std::function<float(const std::string&)>& measureOptionWidth);

private:
	UIRoot controls_;
	bool open_;
	bool setupEnabled_;
	bool demoActive_;
	bool draftDirty_;
	DemoEditorTab activeTab_;
	DemoAlgorithmPickerIntent pickerIntent_;
	std::size_t selectedStep_;
	std::size_t selectedComplexityIndex_;
	std::size_t synchronizedStepIndex_;
	int synchronizedStepComplexity_;
	std::string selectedPresetId_;
	std::string pendingPresetSwitchId_;
	DemoPreset draft_;
	DemoRunnerState runnerState_;
	std::size_t runnerStepIndex_;
	std::size_t runnerStepCount_;
	std::string runnerStatus_;
	std::string feedback_;
	std::vector<std::string> presetIds_;
	std::vector<sortinglogic::DemoComplexityClass> complexityClasses_;
	std::vector<std::string> complexityLabels_;
	std::vector<std::string> optionGroupIds_;
	std::vector<std::string> optionGroupLabels_;
	std::vector<std::string> optionIds_;
	std::vector<std::string> optionLabels_;
	UiRect panelBounds_;
	UiRect headerBounds_;
	UiRect bodyBounds_;
	UiRect queueBounds_;
	UiRect inspectorBounds_;
	UiRect footerBounds_;
	UiRect transportBounds_;
	UiRect listClip_;
	UiScrollViewport bodyViewport_;
	int logicalWidth_;
	int logicalHeight_;
	int laidOutWidth_;
	int laidOutHeight_;
	float dpiScale_;
	float laidOutDpiScale_;
	float laidOutScroll_;
	std::size_t laidOutStepCount_;
	DemoEditorTab laidOutTab_;
	UiPresetId laidOutPreset_;
	bool confirmPresetSwitch_;

	void createControls();
	void synchronizeCatalogs(const DemoPresetStore& presets);
	void synchronizeOptions();
	void synchronizeQueue();
	void applyPageVisibility();
	void layout(int logicalWidth, int logicalHeight, UiPresetId appearancePreset);
	DemoStep* currentStep();
	const DemoStep* currentStep() const;
	DemoInputRecipe currentSharedRecipe() const;
	DemoInputRecipe currentEffectiveRecipe() const;
	DemoActionPayload payloadForRecipe(const DemoInputRecipe& recipe) const;
	AppAction mapUiAction(const UiAction& action);
	void composeWidgetControls(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
	void composeSelectionList(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
	void composeTabs(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
	void composeFields(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
	void composeText(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
	void composePresetSwitchConfirmation(const DemoEditorPalette& palette, UiPaintContext& paintContext) const;
};
