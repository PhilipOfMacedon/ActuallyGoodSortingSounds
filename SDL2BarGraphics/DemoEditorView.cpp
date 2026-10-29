#include "DemoEditorView.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>

#include "AppActions.h"
#include "../SortingLogic/SortingLogic.h"

namespace
{
	const char* domainName(sortinglogic::InputDomain value)
	{
		switch (value)
		{
		case sortinglogic::InputDomain::NonnegativeIntegerKeys: return "Nonnegative integer keys";
		case sortinglogic::InputDomain::SignedIntegerKeys: return "Signed integer keys";
		case sortinglogic::InputDomain::IntegerPermutation: return "Integer permutation";
		default: return "Normalized floats";
		}
	}
	const char* patternName(sortinglogic::InputPattern value)
	{
		switch (value)
		{
		case sortinglogic::InputPattern::Random: return "Random";
		case sortinglogic::InputPattern::Sorted: return "Sorted";
		case sortinglogic::InputPattern::Reversed: return "Reverse";
		case sortinglogic::InputPattern::NearlySorted: return "Nearly sorted";
		case sortinglogic::InputPattern::FewUnique: return "Few unique";
		case sortinglogic::InputPattern::OrganPipe: return "Organ pipe";
		case sortinglogic::InputPattern::AndrutReference: return "Andrut reference";
		}
		return "Random";
	}
	AppAction demoAction(AppActionKind kind, const DemoActionPayload& payload)
	{
		AppAction action(kind);
		action.demo.reset(new DemoActionPayload(payload));
		return action;
	}
	UiRect rowBounds(const UiRect& panel, float y, float height)
	{
		return UiRect(panel.x + 18.0f, panel.y + y, panel.width - 36.0f, height);
	}
	void submitRect(UiPaintContext& context, const UiRect& bounds, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer)
	{
		context.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, bounds, color, std::string(), std::string(),
			material, layer, UiRect(), false, 0.0f));
	}
	void submitText(UiPaintContext& context, const std::string& id, const std::string& text,
		const UiRect& bounds, float height, const UiColor& color, UiPaintLayer layer)
	{
		context.submit(UiPaintCommand(UiPaintPrimitive::Text, bounds, color, text, id,
			UiPaintMaterial::Solid, layer, UiRect(), false, height));
	}
	void placeRow(UIRoot& root, const UiRect& panel, float y, float h,
		const std::vector<std::pair<std::string, float> >& controls, float gap = 6.0f)
	{
		float totalWeight = 0.0f;
		for (std::size_t i = 0; i < controls.size(); ++i) totalWeight += controls[i].second;
		const float usable = panel.width - 36.0f - gap * static_cast<float>(controls.size() - 1);
		float x = panel.x + 18.0f;
		for (std::size_t i = 0; i < controls.size(); ++i)
		{
			const float w = usable * controls[i].second / totalWeight;
			UiWidget* widget = root.find(controls[i].first);
			if (widget) widget->setBounds(UiRect(x, panel.y + y, w, h));
			x += w + gap;
		}
	}
}

DemoEditorView::DemoEditorView()
	: controls_(), open_(false), setupEnabled_(true), demoActive_(false), selectedStep_(0), selectedComplexityIndex_(0),
	selectedPresetId_(), synchronizedPresetId_(), synchronizedStep_(static_cast<std::size_t>(-1)), draft_(), presetIds_(), complexityLabels_(), complexityClasses_(demoComplexityClassOrder()), algorithmIds_(), algorithmLabels_(),
	optionGroupIds_(), optionGroupLabels_(), optionIds_(), optionLabels_(), panelBounds_(), seedEditBounds_(), countEditBounds_(),
	editingName_(false), nameDraft_(), feedback_(), runnerStatus_(), editingNumericField_(0), numericDraft_()
{
	createControls();
}

void DemoEditorView::createControls()
{
	controls_.addComboBox("demo_preset", std::vector<std::string>(1, "Select preset"));
	controls_.addButton("demo_start", "Start", AppAction(AppActionKind::StartDemo));
	controls_.addButton("demo_stop", "Stop", AppAction(AppActionKind::StopDemo));
	controls_.addButton("demo_previous", "Previous", AppAction(AppActionKind::PreviousDemoStep));
	controls_.addButton("demo_next", "Next", AppAction(AppActionKind::NextDemoStep));
	controls_.addButton("demo_close", "Close", AppAction(AppActionKind::CloseDemoEditor));
	controls_.addComboBox("demo_step", std::vector<std::string>(1, "No steps"));
	controls_.addComboBox("demo_algorithm", std::vector<std::string>(1, "Choose algorithm"));
	controls_.addButton("demo_add", "Add step");
	controls_.addButton("demo_replace", "Replace");
	controls_.addButton("demo_remove", "Remove", AppAction(AppActionKind::DemoRemoveStep));
	controls_.addButton("demo_up", "Move up", AppAction(AppActionKind::DemoMoveStepUp));
	controls_.addButton("demo_down", "Move down", AppAction(AppActionKind::DemoMoveStepDown));
	controls_.addComboBox("demo_option_group", std::vector<std::string>(1, "No options"));
	controls_.addComboBox("demo_option", std::vector<std::string>(1, "No option"));
	controls_.addComboBox("demo_direction", std::vector<std::string>{ "Ascending", "Descending" });
	controls_.addComboBox("demo_timing", std::vector<std::string>{ "Fixed duration", "Reference cadence" });
	controls_.addButton("demo_duration_down", "Duration -");
	controls_.addButton("demo_duration_value", "10 sec");
	controls_.addButton("demo_duration_up", "Duration +");
	controls_.addComboBox("demo_class", std::vector<std::string>(1, "Linear"));
	controls_.addComboBox("demo_domain", std::vector<std::string>{ "Normalized floats", "Nonnegative integers", "Signed integers", "Integer permutation" });
	controls_.addComboBox("demo_pattern", std::vector<std::string>{ "Random", "Sorted", "Reverse", "Nearly sorted", "Few unique", "Organ pipe", "Andrut reference" });
	controls_.addButton("demo_count_down", "Count -10");
	controls_.addButton("demo_count_value", "80 items");
	controls_.addButton("demo_count_up", "Count +10");
	controls_.addButton("demo_seed_down", "Seed -1");
	controls_.addButton("demo_seed_value", "Seed 5489");
	controls_.addButton("demo_seed_up", "Seed +1");
	controls_.addButton("demo_override", "Use step override");
	controls_.addButton("demo_intermission_down", "Intermission -");
	controls_.addButton("demo_intermission_value", "1 sec");
	controls_.addButton("demo_intermission_up", "Intermission +");
	controls_.addButton("demo_loop", "Loop: off");
	controls_.addButton("demo_save", "Save preset", AppAction(AppActionKind::SaveDemoPreset));
	controls_.addButton("demo_rename", "Rename");
	controls_.addButton("demo_delete", "Delete", AppAction(AppActionKind::DeleteDemoPreset));
	controls_.setFocusOrder({ "demo_preset", "demo_start", "demo_stop", "demo_previous", "demo_next", "demo_close",
		"demo_step", "demo_algorithm", "demo_add", "demo_replace", "demo_remove", "demo_up", "demo_down",
		"demo_option_group", "demo_option", "demo_direction", "demo_timing", "demo_duration_down", "demo_duration_value",
		"demo_duration_up", "demo_class", "demo_domain", "demo_pattern", "demo_count_down", "demo_count_value",
		"demo_count_up", "demo_seed_down", "demo_seed_value", "demo_seed_up", "demo_override", "demo_intermission_down",
		"demo_intermission_value", "demo_intermission_up", "demo_loop", "demo_save", "demo_rename", "demo_delete" });
}

DemoStep* DemoEditorView::currentStep()
{
	return selectedStep_ < draft_.steps.size() ? &draft_.steps[selectedStep_] : nullptr;
}

const DemoStep* DemoEditorView::currentStep() const
{
	return selectedStep_ < draft_.steps.size() ? &draft_.steps[selectedStep_] : nullptr;
}

DemoInputRecipe DemoEditorView::currentRecipe() const
{
	const DemoStep* step = currentStep();
	if (step && step->hasInputOverride) return step->inputOverride;
	if (selectedComplexityIndex_ < complexityClasses_.size())
	{
		const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator recipe =
			draft_.classInputs.find(complexityClasses_[selectedComplexityIndex_]);
		if (recipe != draft_.classInputs.end()) return recipe->second;
	}
	return DemoInputRecipe();
}

DemoActionPayload DemoEditorView::payloadForRecipe(const DemoInputRecipe& recipe) const
{
	DemoActionPayload payload;
	payload.presetId = selectedPresetId_;
	payload.stepIndex = selectedStep_;
	payload.complexityClass = selectedComplexityIndex_ < complexityClasses_.size()
		? static_cast<int>(complexityClasses_[selectedComplexityIndex_])
		: static_cast<int>(sortinglogic::DemoComplexityClass::Linear);
	payload.seed = recipe.seed;
	payload.itemCount = recipe.itemCount;
	payload.inputDomain = static_cast<int>(recipe.domain);
	payload.inputPattern = static_cast<int>(recipe.pattern);
	return payload;
}

void DemoEditorView::synchronize(const DemoPresetStore& presets, const DemoPreset& draft,
	const std::string& selectedPresetId, std::size_t selectedStep,
	const DemoRunner& runner, bool setupEnabled, int logicalWidth, int logicalHeight)
{
	draft_ = draft;
	selectedPresetId_ = selectedPresetId;
	selectedStep_ = draft_.steps.empty() ? 0 : (std::min)(selectedStep, draft_.steps.size() - 1);
	setupEnabled_ = setupEnabled;
	demoActive_ = runner.active();
	runnerStatus_ = runner.message();
	if (runner.active())
	{
		const DemoStep* active = runner.currentStep();
		const sortinglogic::AlgorithmDescriptor* algorithm = active
			? sortinglogic::algorithmDescriptorById(active->familyId, active->variantId) : nullptr;
		if (active)
		{
			runnerStatus_ = "Active step " + std::to_string(runner.stepIndex() + 1) + "/" +
				std::to_string(runner.preset().steps.size()) + (algorithm ? " | " + std::string(algorithm->displayName) : " | unavailable algorithm");
			if (runner.hasEffectiveInput())
				runnerStatus_ += " | " + std::to_string(runner.effectiveInput().itemCount) + " items | seed " +
					std::to_string(runner.effectiveInput().seed);
		}
	}
	if (selectedPresetId_ != synchronizedPresetId_ || selectedStep_ != synchronizedStep_)
	{
		selectedComplexityIndex_ = 0;
		const DemoStep* activeStep = currentStep();
		const sortinglogic::AlgorithmDescriptor* activeAlgorithm = activeStep
			? sortinglogic::algorithmDescriptorById(activeStep->familyId, activeStep->variantId) : nullptr;
		if (activeAlgorithm)
			for (std::size_t i = 0; i < complexityClasses_.size(); ++i)
				if (complexityClasses_[i] == activeAlgorithm->demoComplexity) selectedComplexityIndex_ = i;
		synchronizedPresetId_ = selectedPresetId_;
		synchronizedStep_ = selectedStep_;
	}

	presetIds_.clear();
	std::vector<std::string> presetLabels;
	const std::vector<DemoPreset>& prefabs = demoPrefabPresets();
	for (std::size_t i = 0; i < prefabs.size(); ++i) { presetIds_.push_back(prefabs[i].id); presetLabels.push_back(prefabs[i].name); }
	for (std::size_t i = 0; i < presets.userPresets().size(); ++i)
	{
		presetIds_.push_back(presets.userPresets()[i].id);
		presetLabels.push_back(presets.userPresets()[i].name);
	}
	controls_.find("demo_preset")->setOptions(presetLabels.empty() ? std::vector<std::string>(1, "No presets") : presetLabels);
	int presetIndex = 0;
	for (std::size_t i = 0; i < presetIds_.size(); ++i) if (presetIds_[i] == selectedPresetId_) presetIndex = static_cast<int>(i);
	controls_.find("demo_preset")->setSelectedIndex(presetIndex);

	std::vector<std::string> stepLabels;
	for (std::size_t i = 0; i < draft_.steps.size(); ++i)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(draft_.steps[i].familyId, draft_.steps[i].variantId);
		stepLabels.push_back(std::to_string(i + 1) + ". " + (descriptor ? descriptor->displayName : "Unavailable algorithm") +
			(draft_.steps[i].hasInputOverride ? " [override]" : ""));
	}
	controls_.find("demo_step")->setOptions(stepLabels.empty() ? std::vector<std::string>(1, "No steps") : stepLabels);
	controls_.find("demo_step")->setSelectedIndex(static_cast<int>(selectedStep_));

	algorithmIds_.clear(); algorithmLabels_.clear();
	const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
	for (std::size_t i = 0; i < catalog.size(); ++i)
	{
		algorithmIds_.push_back(catalog[i].familyId + "\n" + catalog[i].variantId);
		algorithmLabels_.push_back(catalog[i].displayName);
	}
	controls_.find("demo_algorithm")->setOptions(algorithmLabels_.empty() ? std::vector<std::string>(1, "No algorithms") : algorithmLabels_);

	complexityClasses_ = demoComplexityClassOrder();
	complexityLabels_.clear();
	for (std::size_t i = 0; i < complexityClasses_.size(); ++i)
		complexityLabels_.push_back(sortinglogic::demoComplexityClassName(complexityClasses_[i]));
	controls_.find("demo_class")->setOptions(complexityLabels_);
	if (selectedComplexityIndex_ >= complexityClasses_.size()) selectedComplexityIndex_ = 0;
	const DemoStep* step = currentStep();
	controls_.find("demo_class")->setSelectedIndex(static_cast<int>(selectedComplexityIndex_));
	const DemoInputRecipe recipe = currentRecipe();
	controls_.find("demo_domain")->setSelectedIndex(recipe.domain == sortinglogic::InputDomain::NonnegativeIntegerKeys ? 1 :
		recipe.domain == sortinglogic::InputDomain::SignedIntegerKeys ? 2 : recipe.domain == sortinglogic::InputDomain::IntegerPermutation ? 3 : 0);
	controls_.find("demo_pattern")->setSelectedIndex(recipe.pattern == sortinglogic::InputPattern::AndrutReference ? 6 : static_cast<int>(recipe.pattern));
	controls_.find("demo_count_value")->setLabel(std::to_string(recipe.itemCount) + " items");
	controls_.find("demo_seed_value")->setLabel("Seed " + std::to_string(recipe.seed));
	controls_.find("demo_override")->setLabel(step && step->hasInputOverride ? "Clear step override" : "Use step override");
	controls_.find("demo_override")->setEnabled(step != nullptr && setupEnabled_ && !demoActive_);

	optionGroupIds_.clear(); optionGroupLabels_.clear(); optionIds_.clear(); optionLabels_.clear();
	std::map<std::string, std::string> selectedOptions;
	if (step)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step->familyId, step->variantId);
		std::set<std::string> seen;
		if (descriptor)
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
				if (seen.insert(descriptor->supportedOptions[i].groupId).second)
				{
					optionGroupIds_.push_back(descriptor->supportedOptions[i].groupId);
					optionGroupLabels_.push_back(descriptor->supportedOptions[i].groupId);
				}
	}
	controls_.find("demo_option_group")->setOptions(optionGroupLabels_.empty() ? std::vector<std::string>(1, "No options") : optionGroupLabels_);
	const int groupIndex = controls_.find("demo_option_group")->selectedIndex();
	if (step && groupIndex >= 0 && static_cast<std::size_t>(groupIndex) < optionGroupIds_.size())
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step->familyId, step->variantId);
		const std::string& group = optionGroupIds_[static_cast<std::size_t>(groupIndex)];
		if (descriptor)
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
				if (group == descriptor->supportedOptions[i].groupId)
				{
					optionIds_.push_back(descriptor->supportedOptions[i].stableId);
					optionLabels_.push_back(descriptor->supportedOptions[i].displayName);
				}
		const std::map<std::string, std::string>::const_iterator selected = step->optionSelections.find(group);
		if (selected != step->optionSelections.end()) selectedOptions[group] = selected->second;
	}
	controls_.find("demo_option")->setOptions(optionLabels_.empty() ? std::vector<std::string>(1, "No options") : optionLabels_);
	if (!optionIds_.empty() && step)
	{
		const std::string group = optionGroupIds_[static_cast<std::size_t>(groupIndex)];
		const std::map<std::string, std::string>::const_iterator selected = step->optionSelections.find(group);
		if (selected != step->optionSelections.end())
			for (std::size_t i = 0; i < optionIds_.size(); ++i) if (optionIds_[i] == selected->second) controls_.find("demo_option")->setSelectedIndex(static_cast<int>(i));
	}
	controls_.find("demo_option_group")->setEnabled(step && !optionGroupIds_.empty() && setupEnabled_ && !demoActive_);
	controls_.find("demo_option")->setEnabled(step && !optionIds_.empty() && setupEnabled_ && !demoActive_);
	controls_.find("demo_direction")->setSelectedIndex(step && step->direction == sortinglogic::SortDirection::Descending ? 1 : 0);
	controls_.find("demo_timing")->setSelectedIndex(step && step->timingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence ? 1 : 0);
	controls_.find("demo_duration_value")->setLabel(step ? std::to_string(static_cast<int>(step->fixedDurationSeconds)) + " sec" : "10 sec");
	controls_.find("demo_intermission_value")->setLabel(std::to_string(static_cast<int>(draft_.intermissionSeconds)) + " sec");
	controls_.find("demo_loop")->setLabel(draft_.loop ? "Loop: on" : "Loop: off");
	controls_.find("demo_delete")->setEnabled(!draft_.prefab && !selectedPresetId_.empty() && !demoActive_);

	const bool editingAllowed = setupEnabled_ && !demoActive_;
	const char* setupControls[] = { "demo_preset", "demo_step", "demo_algorithm", "demo_add", "demo_replace", "demo_remove", "demo_up", "demo_down",
		"demo_option_group", "demo_option", "demo_direction", "demo_timing", "demo_duration_down", "demo_duration_value", "demo_duration_up",
		"demo_class", "demo_domain", "demo_pattern", "demo_count_down", "demo_count_value", "demo_count_up", "demo_seed_down", "demo_seed_value",
		"demo_seed_up", "demo_intermission_down", "demo_intermission_value", "demo_intermission_up", "demo_loop", "demo_save", "demo_rename" };
	for (std::size_t i = 0; i < sizeof(setupControls) / sizeof(setupControls[0]); ++i)
		controls_.find(setupControls[i])->setEnabled(editingAllowed);
	controls_.find("demo_start")->setEnabled(!demoActive_ && !draft_.steps.empty());
	if (demoActive_)
	{
		controls_.find("demo_start")->setEnabled(true);
		controls_.find("demo_start")->setLabel(runner.state() == DemoRunnerState::Playing ? "Pause" : "Play");
		controls_.find("demo_start")->setAction(AppAction(AppActionKind::ToggleDemoPlayback));
	}
	else
	{
		controls_.find("demo_start")->setLabel("Start");
		controls_.find("demo_start")->setAction(AppAction(AppActionKind::StartDemo));
	}
	controls_.find("demo_stop")->setEnabled(demoActive_);
	controls_.find("demo_previous")->setEnabled(demoActive_ && runner.stepIndex() > 0);
	controls_.find("demo_next")->setEnabled(demoActive_ && runner.stepIndex() + 1 < runner.preset().steps.size());
	controls_.find("demo_remove")->setEnabled(editingAllowed && step);
	controls_.find("demo_replace")->setEnabled(editingAllowed && step && !algorithmIds_.empty());
	controls_.find("demo_up")->setEnabled(editingAllowed && selectedStep_ > 0);
	controls_.find("demo_down")->setEnabled(editingAllowed && selectedStep_ + 1 < draft_.steps.size());
	controls_.find("demo_count_down")->setEnabled(editingAllowed && recipe.itemCount > sortinglogic::MinItemCount);
	controls_.find("demo_count_up")->setEnabled(editingAllowed && recipe.itemCount < sortinglogic::MaxItemCount);
	controls_.find("demo_seed_down")->setEnabled(editingAllowed && recipe.seed > 0);
	controls_.find("demo_seed_up")->setEnabled(editingAllowed && recipe.seed < (std::numeric_limits<std::uint32_t>::max)());
	controls_.find("demo_save")->setEnabled(editingAllowed);
	layout(logicalWidth, logicalHeight);
}

void DemoEditorView::layout(int logicalWidth, int logicalHeight)
{
	const float width = (std::min)(920.0f, (std::max)(240.0f, static_cast<float>(logicalWidth) - 24.0f));
	const float height = (std::min)(500.0f, (std::max)(430.0f, static_cast<float>(logicalHeight) - 32.0f));
	panelBounds_ = UiRect((static_cast<float>(logicalWidth) - width) * 0.5f,
		(static_cast<float>(logicalHeight) - height) * 0.5f, width, height);
	controls_.setModalBounds(panelBounds_, UiAction::ViewCommand::CloseDemoEditor);
	controls_.setInputCaptured(open_);
	const float rowHeight = 30.0f;
	placeRow(controls_, panelBounds_, 46, rowHeight, {{"demo_preset", 2.5f}, {"demo_start", 1.0f}, {"demo_stop", 0.8f}, {"demo_previous", 1.0f}, {"demo_next", 0.8f}, {"demo_close", 0.7f}});
	placeRow(controls_, panelBounds_, 90, rowHeight, {{"demo_step", 2.0f}, {"demo_algorithm", 2.2f}, {"demo_add", 0.8f}, {"demo_replace", 0.8f}, {"demo_remove", 0.75f}, {"demo_up", 0.7f}, {"demo_down", 0.7f}});
	placeRow(controls_, panelBounds_, 134, rowHeight, {{"demo_option_group", 1.2f}, {"demo_option", 1.6f}, {"demo_direction", 1.0f}, {"demo_timing", 1.3f}, {"demo_duration_down", 0.9f}, {"demo_duration_value", 0.8f}, {"demo_duration_up", 0.9f}});
	placeRow(controls_, panelBounds_, 178, rowHeight, {{"demo_class", 1.4f}, {"demo_domain", 1.4f}, {"demo_pattern", 1.4f}, {"demo_count_down", 0.9f}, {"demo_count_value", 1.0f}, {"demo_count_up", 0.9f}});
	placeRow(controls_, panelBounds_, 222, rowHeight, {{"demo_seed_down", 0.8f}, {"demo_seed_value", 2.3f}, {"demo_seed_up", 0.8f}, {"demo_override", 1.5f}, {"demo_intermission_down", 1.0f}, {"demo_intermission_value", 0.8f}, {"demo_intermission_up", 1.0f}});
	placeRow(controls_, panelBounds_, 266, rowHeight, {{"demo_loop", 1.0f}, {"demo_save", 1.0f}, {"demo_rename", 1.0f}, {"demo_delete", 1.0f}});
	seedEditBounds_ = controls_.find("demo_seed_value")->bounds();
	countEditBounds_ = controls_.find("demo_count_value")->bounds();
}

void DemoEditorView::setOpen(bool value)
{
	open_ = value;
	if (!value) { editingName_ = false; editingNumericField_ = 0; controls_.closeCombo(); }
}

void DemoEditorView::appendName(const std::string& text)
{
	if (editingName_ && nameDraft_.size() < 64) nameDraft_ += text;
}

void DemoEditorView::eraseNameCharacter()
{
	if (editingName_ && !nameDraft_.empty()) nameDraft_.erase(nameDraft_.size() - 1);
}

void DemoEditorView::cancelNameEdit() { editingName_ = false; nameDraft_.clear(); }

bool DemoEditorView::commitNameEdit(AppAction& action)
{
	if (!editingName_) return false;
	if (nameDraft_.empty()) { cancelNameEdit(); return true; }
	DemoActionPayload payload; payload.presetId = selectedPresetId_; payload.presetName = nameDraft_;
	action = demoAction(AppActionKind::RenameDemoPreset, payload);
	editingName_ = false;
	return true;
}

void DemoEditorView::appendNumericText(const std::string& text)
{
	if (!editingNumeric()) return;
	for (std::size_t i = 0; i < text.size() && numericDraft_.size() < 20; ++i)
		if (text[i] >= '0' && text[i] <= '9') numericDraft_.push_back(text[i]);
}

void DemoEditorView::eraseNumericCharacter()
{
	if (!numericDraft_.empty()) numericDraft_.erase(numericDraft_.size() - 1);
}

void DemoEditorView::cancelNumericEdit() { editingNumericField_ = 0; numericDraft_.clear(); }

bool DemoEditorView::commitNumericEdit(AppAction& action)
{
	if (!editingNumeric()) return false;
	char* end = nullptr;
	const unsigned long long parsed = std::strtoull(numericDraft_.c_str(), &end, 10);
	if (numericDraft_.empty() || !end || *end != '\0') return false;
	const DemoInputRecipe recipe = currentRecipe();
	if (editingNumericField_ == 1 || editingNumericField_ == 2)
	{
		DemoInputRecipe edited = recipe;
		if (editingNumericField_ == 1)
		{
			if (parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
			edited.seed = static_cast<std::uint32_t>(parsed);
		}
		else
		{
			if (parsed < sortinglogic::MinItemCount || parsed > sortinglogic::MaxItemCount) return false;
			edited.itemCount = static_cast<std::size_t>(parsed);
		}
		DemoActionPayload payload = payloadForRecipe(edited);
		const DemoStep* step = currentStep();
		if (step && step->hasInputOverride)
		{
			payload.hasInputOverride = true;
			action = demoAction(AppActionKind::DemoSetStepOverride, payload);
		}
		else action = demoAction(AppActionKind::DemoSetClassRecipe, payload);
	}
	else if (editingNumericField_ == 3)
	{
		if (parsed < 1 || parsed > 120 || !currentStep()) return false;
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.value = static_cast<float>(parsed);
		payload.timingMode = static_cast<int>(currentStep()->timingMode);
		action = demoAction(AppActionKind::DemoSetStepTiming, payload);
	}
	else
	{
		if (parsed > 120) return false;
		DemoActionPayload payload; payload.value = static_cast<float>(parsed);
		action = demoAction(AppActionKind::DemoSetIntermission, payload);
	}
	cancelNumericEdit();
	return true;
}

bool DemoEditorView::handleInput(const UiInputEvent& input, AppAction& action)
{
	if (!open_) return false;
	if (editingNumeric())
	{
		if (input.type == UiInputType::TextInput) { appendNumericText(input.text); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Escape) { cancelNumericEdit(); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Backspace) { eraseNumericCharacter(); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Enter)
			return commitNumericEdit(action) || true;
		return true;
	}
	if (editingName_)
	{
		if (input.type == UiInputType::TextInput) { appendName(input.text); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Escape) { cancelNameEdit(); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Backspace) { eraseNameCharacter(); return true; }
		if (input.type == UiInputType::KeyDown && input.key == UiKey::Enter) return commitNameEdit(action) || true;
		return true;
	}
	UiAction emitted;
	const bool consumed = controls_.handleEvent(input, emitted);
	if (!consumed) return false;
	if (emitted.viewCommand == UiAction::ViewCommand::CloseDemoEditor)
	{
		action = AppAction(AppActionKind::CloseDemoEditor);
		return true;
	}
	if (emitted.event.kind == UiEventKind::None) return true;
	action = mapUiAction(emitted);
	return true;
}

AppAction DemoEditorView::mapUiAction(const UiAction& emitted)
{
	const std::string& id = emitted.event.controlId;
	const int selection = emitted.event.selection;
	if (id == "demo_preset" && selection >= 0 && static_cast<std::size_t>(selection) < presetIds_.size())
	{
		DemoActionPayload payload; payload.presetId = presetIds_[static_cast<std::size_t>(selection)];
		return demoAction(AppActionKind::SelectDemoPreset, payload);
	}
	if (id == "demo_step" && selection >= 0)
	{
		DemoActionPayload payload; payload.stepIndex = static_cast<std::size_t>(selection);
		return demoAction(AppActionKind::SelectDemoStep, payload);
	}
	if (id == "demo_class" && selection >= 0)
	{
		selectedComplexityIndex_ = (std::min)(static_cast<std::size_t>(selection), complexityClasses_.empty() ? 0 : complexityClasses_.size() - 1);
		return AppAction();
	}
	if (id == "demo_option_group") return AppAction();
	if (id == "demo_domain" || id == "demo_pattern")
	{
		DemoInputRecipe recipe = currentRecipe();
		if (id == "demo_domain")
			recipe.domain = selection == 1 ? sortinglogic::InputDomain::NonnegativeIntegerKeys : selection == 2
				? sortinglogic::InputDomain::SignedIntegerKeys : selection == 3 ? sortinglogic::InputDomain::IntegerPermutation
				: sortinglogic::InputDomain::NormalizedFloats;
		else recipe.pattern = selection == 6 ? sortinglogic::InputPattern::AndrutReference : static_cast<sortinglogic::InputPattern>(selection);
		DemoActionPayload payload = payloadForRecipe(recipe);
		const DemoStep* step = currentStep();
		if (step && step->hasInputOverride)
		{
			payload.hasInputOverride = true;
			return demoAction(AppActionKind::DemoSetStepOverride, payload);
		}
		return demoAction(AppActionKind::DemoSetClassRecipe, payload);
	}
	if (id == "demo_direction")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.direction = selection;
		return demoAction(AppActionKind::DemoSetStepDirection, payload);
	}
	if (id == "demo_timing")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_;
		payload.timingMode = selection == 1 ? static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence)
			: static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration);
		payload.value = currentStep() ? currentStep()->fixedDurationSeconds : 10.0f;
		return demoAction(AppActionKind::DemoSetStepTiming, payload);
	}
	if (id == "demo_option" && selection >= 0 && static_cast<std::size_t>(selection) < optionIds_.size() && currentStep())
	{
		const int group = controls_.find("demo_option_group")->selectedIndex();
		if (group < 0 || static_cast<std::size_t>(group) >= optionGroupIds_.size()) return AppAction();
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.optionGroupId = optionGroupIds_[static_cast<std::size_t>(group)];
		payload.optionStableId = optionIds_[static_cast<std::size_t>(selection)];
		return demoAction(AppActionKind::DemoSetStepOption, payload);
	}
	if (id == "demo_algorithm") return AppAction();
	if (id == "demo_add" || id == "demo_replace")
	{
		const int index = controls_.find("demo_algorithm")->selectedIndex();
		if (index < 0 || static_cast<std::size_t>(index) >= algorithmIds_.size()) return AppAction();
		const std::size_t split = algorithmIds_[static_cast<std::size_t>(index)].find('\n');
		DemoActionPayload payload; payload.familyId = algorithmIds_[static_cast<std::size_t>(index)].substr(0, split);
		payload.variantId = algorithmIds_[static_cast<std::size_t>(index)].substr(split + 1);
		payload.stepIndex = id == "demo_add" ? draft_.steps.size() : selectedStep_;
		return demoAction(id == "demo_add" ? AppActionKind::DemoAddStep : AppActionKind::DemoSetStepAlgorithm, payload);
	}
	if (id == "demo_remove" || id == "demo_up" || id == "demo_down")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_;
		return demoAction(id == "demo_remove" ? AppActionKind::DemoRemoveStep : id == "demo_up"
			? AppActionKind::DemoMoveStepUp : AppActionKind::DemoMoveStepDown, payload);
	}
	if (id == "demo_override" && currentStep())
	{
		DemoActionPayload payload = payloadForRecipe(currentRecipe());
		return demoAction(currentStep()->hasInputOverride ? AppActionKind::DemoClearStepOverride : AppActionKind::DemoSetStepOverride, payload);
	}
	if (id == "demo_loop")
	{
		DemoActionPayload payload; payload.flag = !draft_.loop;
		return demoAction(AppActionKind::DemoSetLoop, payload);
	}
	if (id == "demo_intermission_down" || id == "demo_intermission_up")
	{
		DemoActionPayload payload; payload.value = std::max(0.0f, std::min(120.0f, draft_.intermissionSeconds + (id == "demo_intermission_up" ? 0.5f : -0.5f)));
		return demoAction(AppActionKind::DemoSetIntermission, payload);
	}
	if (id == "demo_duration_down" || id == "demo_duration_up")
	{
		if (!currentStep()) return AppAction();
		DemoActionPayload payload; payload.stepIndex = selectedStep_;
		payload.timingMode = static_cast<int>(currentStep()->timingMode);
		payload.value = std::max(1.0f, std::min(120.0f, currentStep()->fixedDurationSeconds + (id == "demo_duration_up" ? 1.0f : -1.0f)));
		return demoAction(AppActionKind::DemoSetStepTiming, payload);
	}
	if (id == "demo_count_down" || id == "demo_count_up" || id == "demo_seed_down" || id == "demo_seed_up")
	{
		DemoInputRecipe recipe = currentRecipe();
		if (id == "demo_count_down") recipe.itemCount = (std::max)(sortinglogic::MinItemCount, recipe.itemCount > 10 ? recipe.itemCount - 10 : sortinglogic::MinItemCount);
		if (id == "demo_count_up") recipe.itemCount = (std::min)(sortinglogic::MaxItemCount, recipe.itemCount + 10);
		if (id == "demo_seed_down") --recipe.seed;
		if (id == "demo_seed_up") ++recipe.seed;
		DemoActionPayload payload = payloadForRecipe(recipe);
		const DemoStep* step = currentStep();
		if (step && step->hasInputOverride)
		{ payload.hasInputOverride = true; return demoAction(AppActionKind::DemoSetStepOverride, payload); }
		return demoAction(AppActionKind::DemoSetClassRecipe, payload);
	}
	if (id == "demo_seed_value" || id == "demo_count_value")
	{
		editingNumericField_ = id == "demo_seed_value" ? 1 : 2;
		const DemoInputRecipe recipe = currentRecipe();
		numericDraft_ = id == "demo_seed_value" ? std::to_string(recipe.seed) : std::to_string(recipe.itemCount);
		return AppAction();
	}
	if (id == "demo_duration_value" && currentStep())
	{
		editingNumericField_ = 3;
		numericDraft_ = std::to_string(static_cast<int>(currentStep()->fixedDurationSeconds));
		return AppAction();
	}
	if (id == "demo_intermission_value")
	{
		editingNumericField_ = 4;
		numericDraft_ = std::to_string(static_cast<int>(draft_.intermissionSeconds));
		return AppAction();
	}
	if (id == "demo_save")
	{
		DemoActionPayload payload;
		if (editingName_) payload.presetName = nameDraft_;
		return demoAction(AppActionKind::SaveDemoPreset, payload);
	}
	if (id == "demo_rename")
	{
		editingName_ = true;
		nameDraft_ = draft_.name;
		return AppAction();
	}
	if (id == "demo_delete") return demoAction(AppActionKind::DeleteDemoPreset, payloadForRecipe(currentRecipe()));
	if (id == "demo_start") return emitted.appAction;
	if (id == "demo_stop") return AppAction(AppActionKind::StopDemo);
	if (id == "demo_previous") return AppAction(AppActionKind::PreviousDemoStep);
	if (id == "demo_next") return AppAction(AppActionKind::NextDemoStep);
	if (id == "demo_close") return AppAction(AppActionKind::CloseDemoEditor);
	return emitted.appAction;
}

void DemoEditorView::compose(const DemoEditorPalette& palette, int logicalWidth, int logicalHeight,
	UiPaintContext& paintContext) const
{
	if (!open_) return;
	submitRect(paintContext, UiRect(0.0f, 0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight)),
		UiColor(0.0f, 0.0f, 0.0f, 0.64f), UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop);
	submitRect(paintContext, panelBounds_, palette.panel, UiPaintMaterial::Panel, UiPaintLayer::Panel);
	submitRect(paintContext, UiRect(panelBounds_.x, panelBounds_.y, panelBounds_.width, 3.0f), palette.accent,
		UiPaintMaterial::Accent, UiPaintLayer::Accent);
	submitText(paintContext, "#demo_title", "DEMO SESSION EDITOR", UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 10.0f, panelBounds_.width - 36.0f, 26.0f), 18.0f, palette.text, UiPaintLayer::SettingsText);
	submitText(paintContext, "#demo_subtitle", demoActive_ ? "Manual controls are paused and preserved until this session ends." :
		"Presets are recipes; each step rebuilds its own input from the stored seed.",
		UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 32.0f, panelBounds_.width - 36.0f, 17.0f), 11.0f, palette.mutedText, UiPaintLayer::SettingsText);
	const char* captions[] = { "Preset", "Sequence and algorithm", "Step options and timing", "Complexity-class input", "Recipe values", "Session settings and preset actions" };
	const float captionY[] = { 43, 87, 131, 175, 219, 263 };
	for (std::size_t row = 0; row < 6; ++row)
		submitText(paintContext, "#demo_caption_" + std::to_string(row), captions[row],
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + captionY[row] - 13.0f, panelBounds_.width - 36.0f, 12.0f), 9.0f, palette.mutedText, UiPaintLayer::SettingsText);
	if (editingName_)
		submitText(paintContext, "#demo_name", "Preset name: " + nameDraft_ + "_  (Enter saves name, Escape cancels)",
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 337.0f, panelBounds_.width - 36.0f, 20.0f), 12.0f, palette.text, UiPaintLayer::SettingsText);
	else
		submitText(paintContext, "#demo_name", "Preset name: " + draft_.name,
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 337.0f, panelBounds_.width - 36.0f, 20.0f), 12.0f, palette.text, UiPaintLayer::SettingsText);
	if (editingNumeric())
		submitText(paintContext, "#demo_numeric", "Enter number: " + numericDraft_ + "_  (Enter applies, Escape cancels)",
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 362.0f, panelBounds_.width - 36.0f, 20.0f), 12.0f, palette.accent, UiPaintLayer::SettingsText);
	if (!runnerStatus_.empty())
		submitText(paintContext, "#demo_runtime", runnerStatus_,
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 388.0f, panelBounds_.width - 36.0f, 18.0f), 11.0f, palette.text, UiPaintLayer::SettingsText);
	if (!feedback_.empty())
		submitText(paintContext, "#demo_feedback", feedback_,
			UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 408.0f, panelBounds_.width - 36.0f, 18.0f), 10.0f, palette.mutedText, UiPaintLayer::SettingsText);
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const UiWidget& widget = *controls_.widgets()[i];
		if (!widget.visible()) continue;
		const UiColor fill = !widget.enabled() ? palette.disabledButton :
		(widget.focused() ? palette.accent : widget.pressed() ? palette.accent : widget.hovered() ? palette.valueButton : palette.button);
		submitRect(paintContext, widget.bounds(), fill,
			widget.kind() == UiWidgetKind::ComboBox ? UiPaintMaterial::Popup : UiPaintMaterial::Button, UiPaintLayer::SettingsControls);
		submitText(paintContext, "#demo_widget_" + widget.id(), widget.label(), widget.bounds(), 11.0f,
			widget.enabled() ? palette.text : palette.mutedText, UiPaintLayer::SettingsText);
		if (widget.expanded())
			for (std::size_t option = 0; option < widget.options().size(); ++option)
			{
				const UiRect bounds = widget.optionBounds(option);
				if (bounds.width <= 0 || bounds.height <= 0) continue;
				submitRect(paintContext, bounds, static_cast<int>(option) == widget.selectedIndex() ? palette.accent : palette.button,
					UiPaintMaterial::Popup, UiPaintLayer::SettingsControls);
				submitText(paintContext, "#demo_option_" + widget.id() + "_" + std::to_string(option), widget.options()[option],
					bounds, 11.0f, palette.text, UiPaintLayer::SettingsText);
			}
	}
}
