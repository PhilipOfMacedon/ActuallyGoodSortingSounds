#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "DemoModels.h"
#include "DemoRunner.h"
#include "UiArchitecture.h"

struct AppAction;
struct DemoActionPayload;

struct DemoEditorPalette
{
	UiColor background, panel, button, valueButton, disabledButton, accent, text, mutedText;
};

// Retained controls for editing and running demo presets. It owns only view
// state and emits typed AppActions; preset persistence and session ownership
// remain in AppController.
class DemoEditorView
{
public:
	DemoEditorView();
	void synchronize(const DemoPresetStore& presets, const DemoPreset& draft,
		const std::string& selectedPresetId, std::size_t selectedStep,
		const DemoRunner& runner, bool setupEnabled, int logicalWidth, int logicalHeight);
	bool handleInput(const UiInputEvent& input, AppAction& action);
	void compose(const DemoEditorPalette& palette, int logicalWidth, int logicalHeight,
		UiPaintContext& paintContext) const;
	bool open() const { return open_; }
	void setOpen(bool value);
	void setFeedback(const std::string& message) { feedback_ = message; }
	bool editingName() const { return editingName_; }
	bool editingNumeric() const { return editingNumericField_ != 0; }
	void appendName(const std::string& text);
	void eraseNameCharacter();
	void cancelNameEdit();
	bool commitNameEdit(AppAction& action);
	const std::string& nameDraft() const { return nameDraft_; }
	const UiWidget* control(const std::string& id) const { return controls_.find(id); }
	const UiRect& panelBounds() const { return panelBounds_; }
	void layoutComboPopups(const std::function<float(const std::string&)>& measureOptionWidth);
	void appendNumericText(const std::string& text);
	void eraseNumericCharacter();
	void cancelNumericEdit();
	bool commitNumericEdit(AppAction& action);

private:
	UIRoot controls_;
	bool open_;
	bool setupEnabled_;
	bool demoActive_;
	std::size_t selectedStep_;
	std::size_t selectedComplexityIndex_;
	std::string selectedPresetId_;
	std::string synchronizedPresetId_;
	std::size_t synchronizedStep_;
	DemoPreset draft_;
	std::vector<std::string> presetIds_;
	std::vector<std::string> complexityLabels_;
	std::vector<sortinglogic::DemoComplexityClass> complexityClasses_;
	std::vector<std::string> algorithmIds_;
	std::vector<std::string> algorithmLabels_;
	std::vector<std::string> optionGroupIds_;
	std::vector<std::string> optionGroupLabels_;
	std::vector<std::string> optionIds_;
	std::vector<std::string> optionLabels_;
	UiRect panelBounds_;
	UiRect seedEditBounds_;
	UiRect countEditBounds_;
	bool editingName_;
	std::string nameDraft_;
	std::string feedback_;
	std::string runnerStatus_;
	int editingNumericField_; // 1 seed, 2 count, 3 duration, 4 intermission
	std::string numericDraft_;

	void createControls();
	void layout(int logicalWidth, int logicalHeight);
	DemoInputRecipe currentRecipe() const;
	DemoStep* currentStep();
	const DemoStep* currentStep() const;
	DemoActionPayload payloadForRecipe(const DemoInputRecipe& recipe) const;
	AppAction mapUiAction(const UiAction& action);
};
