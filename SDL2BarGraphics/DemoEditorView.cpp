#include "DemoEditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#include "AppActions.h"
#include "UiAppearance.h"
#include "../SortingLogic/SortingLogic.h"

namespace
{
	UiColor mix(const UiColor& a, const UiColor& b, float amount)
	{
		const float t = std::max(0.0f, std::min(1.0f, amount));
		return UiColor(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
	}

	std::map<std::string, UiColor> paletteMap(const DemoEditorPalette& palette)
	{
		std::map<std::string, UiColor> values;
		values["color.background"] = palette.background;
		values["color.panel"] = palette.panel;
		values["color.button"] = palette.button;
		values["color.value_button"] = palette.valueButton;
		values["color.disabled_button"] = palette.disabledButton;
		values["color.accent"] = palette.accent;
		values["color.text"] = palette.text;
		values["color.muted_text"] = palette.mutedText;
		return values;
	}

	void submitRectangle(UiPaintContext& context, const UiRect& bounds, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer, const UiRect& clip = UiRect(), bool clipped = false)
	{
		context.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, bounds, color, std::string(), std::string(),
			material, layer, clip, clipped, 0.0f));
	}

	void submitText(UiPaintContext& context, const std::string& id, const std::string& text,
		const UiRect& bounds, float height, const UiColor& color, UiPaintLayer layer,
		const UiRect& clip = UiRect(), bool clipped = false, UiTextAlignment alignment = UiTextAlignment::Left)
	{
		UiPaintCommand command(UiPaintPrimitive::Text, bounds, color, text, id,
			UiPaintMaterial::Solid, layer, clip, clipped, height);
		command.textAlignment = alignment;
		context.submit(command);
	}

	void submitStyledBox(UiPaintContext& context, const UiRect& bounds, const UiControlPaintStyle& style,
		UiPaintLayer layer, const UiRect& clip)
	{
		if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
		if (style.shadowOffset > 0.0f)
			submitRectangle(context, UiRect(bounds.x + style.shadowOffset, bounds.y + style.shadowOffset,
				bounds.width, bounds.height), UiColor(0.0f, 0.0f, 0.0f, 0.28f), UiPaintMaterial::Solid, layer, clip, true);
		submitRectangle(context, bounds, style.fill, UiPaintMaterial::Button, layer, clip, true);
		const float border = std::max(1.0f, style.borderWidth);
		submitRectangle(context, UiRect(bounds.x, bounds.y, bounds.width, border), style.border, UiPaintMaterial::Solid, layer, clip, true);
		submitRectangle(context, UiRect(bounds.x, bounds.y + bounds.height - border, bounds.width, border),
			style.border, UiPaintMaterial::Solid, layer, clip, true);
		submitRectangle(context, UiRect(bounds.x, bounds.y, border, bounds.height), style.border, UiPaintMaterial::Solid, layer, clip, true);
		submitRectangle(context, UiRect(bounds.x + bounds.width - border, bounds.y, border, bounds.height),
			style.border, UiPaintMaterial::Solid, layer, clip, true);
	}

	std::string shortDuration(float seconds)
	{
		std::ostringstream output;
		output.imbue(std::locale::classic());
		output << std::fixed << std::setprecision(seconds == std::floor(seconds) ? 0 : 1) << seconds;
		return output.str();
	}

	std::string className(sortinglogic::DemoComplexityClass value)
	{
		return sortinglogic::demoComplexityClassName(value);
	}

	const char* domainName(sortinglogic::InputDomain value)
	{
		switch (value)
		{
		case sortinglogic::InputDomain::NonnegativeIntegerKeys: return "Nonnegative integers";
		case sortinglogic::InputDomain::SignedIntegerKeys: return "Signed integers";
		case sortinglogic::InputDomain::IntegerPermutation: return "Integer permutation";
		default: return "Normalized floats";
		}
	}

	const char* patternName(sortinglogic::InputPattern value)
	{
		switch (value)
		{
		case sortinglogic::InputPattern::Sorted: return "Sorted";
		case sortinglogic::InputPattern::Reversed: return "Reverse";
		case sortinglogic::InputPattern::NearlySorted: return "Nearly sorted";
		case sortinglogic::InputPattern::FewUnique: return "Few unique";
		case sortinglogic::InputPattern::OrganPipe: return "Organ pipe";
		case sortinglogic::InputPattern::AndrutReference: return "Andrut reference";
		default: return "Random";
		}
	}

	AppAction demoAction(AppActionKind kind, const DemoActionPayload& payload)
	{
		AppAction action(kind);
		action.demo.reset(new DemoActionPayload(payload));
		return action;
	}

	void place(UIRoot& root, const std::string& id, float x, float y, float w, float h)
	{
		UiWidget* widget = root.find(id);
		if (widget) widget->setBounds(UiRect(x, y, std::max(0.0f, w), std::max(0.0f, h)));
	}

	void placeField(UIRoot& root, const std::string& id, float x, float y, float w, float h)
	{
		UiEditableField* field = root.findEditableField(id);
		if (field) field->setBounds(UiRect(x, y, std::max(0.0f, w), std::max(0.0f, h)));
	}

	void disableWidget(UIRoot& root, const std::string& id, bool enabled)
	{
		UiWidget* widget = root.find(id);
		if (widget) widget->setEnabled(enabled);
	}

	void setFieldActive(UIRoot& root, const std::string& id, bool active)
	{
		if (root.findEditableField(id)) root.setWidgetVisible(id, active);
	}

	int domainSelection(sortinglogic::InputDomain value)
	{
		switch (value)
		{
		case sortinglogic::InputDomain::NonnegativeIntegerKeys: return 1;
		case sortinglogic::InputDomain::SignedIntegerKeys: return 2;
		case sortinglogic::InputDomain::IntegerPermutation: return 3;
		default: return 0;
		}
	}
}

DemoEditorView::DemoEditorView()
	: controls_(), open_(false), setupEnabled_(true), demoActive_(false), draftDirty_(false), activeTab_(DemoEditorTab::Step),
	pickerIntent_(DemoAlgorithmPickerIntent::None), selectedStep_(0), selectedComplexityIndex_(0),
	synchronizedStepIndex_(static_cast<std::size_t>(-1)), synchronizedStepComplexity_(-1), selectedPresetId_(),
	pendingPresetSwitchId_(), draft_(), runnerState_(DemoRunnerState::Inactive), runnerStepIndex_(0), runnerStepCount_(0),
	runnerStatus_(), feedback_(), presetIds_(), complexityClasses_(demoComplexityClassOrder()), complexityLabels_(),
	optionGroupIds_(), optionGroupLabels_(), optionIds_(), optionLabels_(), panelBounds_(), headerBounds_(), bodyBounds_(),
	queueBounds_(), inspectorBounds_(), footerBounds_(), transportBounds_(), listClip_(), bodyViewport_(), logicalWidth_(0),
	logicalHeight_(0), laidOutWidth_(-1), laidOutHeight_(-1), laidOutStepCount_(static_cast<std::size_t>(-1)),
	dpiScale_(1.0f), laidOutDpiScale_(0.0f), laidOutScroll_(-1.0f),
	laidOutTab_(DemoEditorTab::Session), laidOutPreset_(UiPresetId::ClassicWindows95), confirmPresetSwitch_(false)
{
	createControls();
}

void DemoEditorView::createControls()
{
	controls_.addComboBox("demo_preset", std::vector<std::string>(1, "Select preset"), 0,
		AppAction(AppActionKind::SelectDemoPreset));
	controls_.addButton("demo_save", "Save a copy", AppAction(AppActionKind::SaveDemoPreset));
	controls_.addButton("demo_delete", "Delete", AppAction(AppActionKind::DeleteDemoPreset));
	controls_.addButton("demo_close", "Close", AppAction(AppActionKind::CloseDemoEditor));
	controls_.addButton("demo_start", "Start Demo", AppAction(AppActionKind::StartDemo));
	controls_.addButton("demo_mode", "Demo mode", AppAction(AppActionKind::ToggleDemoPlayback));
	controls_.addButton("demo_previous", "Previous", AppAction(AppActionKind::PreviousDemoStep));
	controls_.addButton("demo_next", "Next", AppAction(AppActionKind::NextDemoStep));
	controls_.addButton("demo_stop", "Stop", AppAction(AppActionKind::StopDemo));
	controls_.addSelectionList("demo_queue", AppAction(AppActionKind::SelectDemoStep));
	controls_.addButton("demo_up", "Move up", AppAction(AppActionKind::DemoMoveStepUp));
	controls_.addButton("demo_down", "Move down", AppAction(AppActionKind::DemoMoveStepDown));
	controls_.addButton("demo_remove", "Remove", AppAction(AppActionKind::DemoRemoveStep));
	controls_.addButton("demo_add", "Add algorithm…");
	controls_.addTabStrip("demo_tabs", std::vector<std::string>{ "Step", "Class inputs", "Session" }, 0);
	controls_.addButton("demo_replace", "Change algorithm…");
	controls_.addComboBox("demo_direction", std::vector<std::string>{ "Ascending", "Descending" }, 0);
	controls_.addComboBox("demo_timing", std::vector<std::string>{ "Fixed duration", "Reference cadence" }, 0);
	controls_.addEditableField("demo_duration_value", "Duration, seconds", UiEditableFieldKind::FiniteDecimal,
		"10", 1.0, 120.0, 8, AppAction());
	controls_.addButton("demo_completion_scan", "Completion scan: on");
	controls_.addComboBox("demo_option_group", std::vector<std::string>(1, "No options"), 0);
	controls_.addComboBox("demo_option", std::vector<std::string>(1, "No options"), 0);
	controls_.addButton("demo_override", "Use step override");
	controls_.addComboBox("demo_class", std::vector<std::string>(1, "Quadratic"), 0);
	controls_.addComboBox("demo_domain", std::vector<std::string>{ "Normalized floats", "Nonnegative integers", "Signed integers", "Integer permutation" }, 0);
	controls_.addComboBox("demo_pattern", std::vector<std::string>{ "Random", "Sorted", "Reverse", "Nearly sorted", "Few unique", "Organ pipe", "Andrut reference" }, 0);
	controls_.addEditableField("demo_count_value", "Item count", UiEditableFieldKind::UnsignedInteger,
		"80", sortinglogic::MinItemCount, sortinglogic::MaxItemCount, 5, AppAction());
	controls_.addEditableField("demo_seed_value", "Seed", UiEditableFieldKind::UnsignedInteger,
		"5489", 0.0, (std::numeric_limits<std::uint32_t>::max)(), 10, AppAction());
	controls_.addEditableField("demo_preset_name", "Preset name", UiEditableFieldKind::BoundedText,
		"Classic Five", 0.0, 0.0, 64, AppAction());
	controls_.addEditableField("demo_intermission_value", "Intermission, seconds", UiEditableFieldKind::FiniteDecimal,
		"1", 0.0, 120.0, 8, AppAction());
	controls_.addButton("demo_loop", "Loop: off");
	controls_.addButton("demo_switch_save", "Save");
	controls_.addButton("demo_switch_discard", "Discard");
	controls_.addButton("demo_switch_cancel", "Cancel");
	controls_.setFocusOrder({ "demo_preset", "demo_queue", "demo_up", "demo_down", "demo_remove", "demo_add", "demo_tabs",
		"demo_replace", "demo_direction", "demo_timing", "demo_duration_value", "demo_completion_scan", "demo_option_group",
		"demo_option", "demo_override", "demo_class", "demo_domain", "demo_pattern", "demo_count_value", "demo_seed_value",
		"demo_preset_name", "demo_intermission_value", "demo_loop", "demo_save", "demo_delete", "demo_start", "demo_mode",
		"demo_previous", "demo_next", "demo_stop", "demo_close" });
}

void DemoEditorView::synchronizeCatalogs(const DemoPresetStore& presets)
{
	presetIds_.clear();
	std::vector<std::string> names;
	const std::vector<DemoPreset>& prefabs = demoPrefabPresets();
	for (std::size_t i = 0; i < prefabs.size(); ++i) { presetIds_.push_back(prefabs[i].id); names.push_back(prefabs[i].name); }
	for (std::size_t i = 0; i < presets.userPresets().size(); ++i)
	{
		presetIds_.push_back(presets.userPresets()[i].id);
		names.push_back(presets.userPresets()[i].name);
	}
	UiWidget* preset = controls_.find("demo_preset");
	if (preset)
	{
		preset->setOptions(names.empty() ? std::vector<std::string>(1, "No presets") : names);
		int selected = 0;
		for (std::size_t i = 0; i < presetIds_.size(); ++i) if (presetIds_[i] == selectedPresetId_) selected = static_cast<int>(i);
		preset->setSelectedIndex(selected);
	}
	std::set<sortinglogic::DemoComplexityClass> usedClasses;
	for (std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator recipe = draft_.classInputs.begin();
		recipe != draft_.classInputs.end(); ++recipe) usedClasses.insert(recipe->first);
	for (std::size_t i = 0; i < draft_.steps.size(); ++i)
	{
		const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptorById(
			draft_.steps[i].familyId, draft_.steps[i].variantId);
		if (algorithm) usedClasses.insert(algorithm->demoComplexity);
	}
	complexityClasses_.clear();
	const std::vector<sortinglogic::DemoComplexityClass>& classOrder = demoComplexityClassOrder();
	for (std::size_t i = 0; i < classOrder.size(); ++i)
		if (usedClasses.find(classOrder[i]) != usedClasses.end()) complexityClasses_.push_back(classOrder[i]);
	if (complexityClasses_.empty()) complexityClasses_.push_back(sortinglogic::DemoComplexityClass::Quadratic);
	complexityLabels_.clear();
	for (std::size_t i = 0; i < complexityClasses_.size(); ++i) complexityLabels_.push_back(className(complexityClasses_[i]));
	if (UiWidget* classes = controls_.find("demo_class")) classes->setOptions(complexityLabels_);
}

DemoStep* DemoEditorView::currentStep()
{
	return selectedStep_ < draft_.steps.size() ? &draft_.steps[selectedStep_] : nullptr;
}

const DemoStep* DemoEditorView::currentStep() const
{
	return selectedStep_ < draft_.steps.size() ? &draft_.steps[selectedStep_] : nullptr;
}

DemoInputRecipe DemoEditorView::currentSharedRecipe() const
{
	if (selectedComplexityIndex_ >= complexityClasses_.size()) return DemoInputRecipe();
	const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator found =
		draft_.classInputs.find(complexityClasses_[selectedComplexityIndex_]);
	return found == draft_.classInputs.end() ? DemoInputRecipe() : found->second;
}

DemoInputRecipe DemoEditorView::currentEffectiveRecipe() const
{
	const DemoStep* step = currentStep();
	if (step && step->hasInputOverride) return step->inputOverride;
	if (step)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step->familyId, step->variantId);
		if (descriptor)
		{
			const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator found = draft_.classInputs.find(descriptor->demoComplexity);
			if (found != draft_.classInputs.end()) return found->second;
		}
	}
	return currentSharedRecipe();
}

DemoActionPayload DemoEditorView::payloadForRecipe(const DemoInputRecipe& recipe) const
{
	DemoActionPayload payload;
	payload.presetId = selectedPresetId_;
	payload.stepIndex = selectedStep_;
	payload.complexityClass = selectedComplexityIndex_ < complexityClasses_.size()
		? static_cast<int>(complexityClasses_[selectedComplexityIndex_])
		: static_cast<int>(sortinglogic::DemoComplexityClass::DemonstrationUnknown);
	payload.seed = recipe.seed;
	payload.itemCount = recipe.itemCount;
	payload.inputDomain = static_cast<int>(recipe.domain);
	payload.inputPattern = static_cast<int>(recipe.pattern);
	return payload;
}

void DemoEditorView::synchronizeOptions()
{
	optionGroupIds_.clear(); optionGroupLabels_.clear(); optionIds_.clear(); optionLabels_.clear();
	const DemoStep* step = currentStep();
	const sortinglogic::AlgorithmDescriptor* descriptor = step
		? sortinglogic::algorithmDescriptorById(step->familyId, step->variantId) : nullptr;
	std::set<std::string> seen;
	if (descriptor)
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			if (seen.insert(descriptor->supportedOptions[i].groupId).second)
			{
				optionGroupIds_.push_back(descriptor->supportedOptions[i].groupId);
				optionGroupLabels_.push_back(descriptor->supportedOptions[i].groupId);
			}
	UiWidget* group = controls_.find("demo_option_group");
	UiWidget* option = controls_.find("demo_option");
	if (!group || !option) return;
	int groupIndex = group->selectedIndex();
	group->setOptions(optionGroupLabels_.empty() ? std::vector<std::string>(1, "No algorithm options") : optionGroupLabels_);
	if (groupIndex >= static_cast<int>(optionGroupIds_.size())) groupIndex = 0;
	group->setSelectedIndex(groupIndex);
	if (descriptor && groupIndex >= 0 && static_cast<std::size_t>(groupIndex) < optionGroupIds_.size())
	{
		const std::string& groupId = optionGroupIds_[static_cast<std::size_t>(groupIndex)];
		std::string selectedStableId;
		std::vector<std::string> optionIds, optionNames;
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			if (groupId == descriptor->supportedOptions[i].groupId)
			{
				optionIds.push_back(descriptor->supportedOptions[i].stableId);
				optionNames.push_back(descriptor->supportedOptions[i].displayName);
			}
		const std::map<std::string, std::string>::const_iterator current = step->optionSelections.find(groupId);
		if (current != step->optionSelections.end()) selectedStableId = current->second;
		optionIds_ = optionIds; optionLabels_ = optionNames;
		option->setOptions(optionNames.empty() ? std::vector<std::string>(1, "No options") : optionNames);
		int selected = 0;
		for (std::size_t i = 0; i < optionIds.size(); ++i) if (optionIds[i] == selectedStableId) selected = static_cast<int>(i);
		option->setSelectedIndex(selected);
	}
	else
	{
		optionIds_.clear(); optionLabels_.clear();
		option->setOptions(std::vector<std::string>(1, "No options"));
		option->setSelectedIndex(0);
	}
}

void DemoEditorView::synchronizeQueue()
{
	UiSelectionList* list = controls_.findSelectionList("demo_queue");
	if (!list) return;
	std::vector<std::string> labels;
	for (std::size_t i = 0; i < draft_.steps.size(); ++i)
	{
		const DemoStep& step = draft_.steps[i];
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
		std::string label = std::to_string(i + 1) + "\t" + (descriptor ? descriptor->displayName : "Unavailable algorithm");
		if (descriptor) label += "\t" + className(descriptor->demoComplexity);
		if (step.hasInputOverride) label += "\t[override]";
		labels.push_back(label);
	}
	list->setItems(labels);
	list->setSelectedIndex(draft_.steps.empty() ? -1 : static_cast<int>(selectedStep_));
}

void DemoEditorView::applyPageVisibility()
{
	const bool stepPage = activeTab_ == DemoEditorTab::Step;
	const bool classPage = activeTab_ == DemoEditorTab::ClassInputs;
	const bool sessionPage = activeTab_ == DemoEditorTab::Session;
	const char* stepWidgets[] = { "demo_replace", "demo_direction", "demo_timing", "demo_completion_scan", "demo_option_group", "demo_option", "demo_override" };
	for (std::size_t i = 0; i < sizeof(stepWidgets) / sizeof(stepWidgets[0]); ++i) controls_.setWidgetVisible(stepWidgets[i], stepPage);
	const char* classWidgets[] = { "demo_class", "demo_domain", "demo_pattern" };
	for (std::size_t i = 0; i < sizeof(classWidgets) / sizeof(classWidgets[0]); ++i) controls_.setWidgetVisible(classWidgets[i], classPage);
	const char* fields[] = { "demo_duration_value", "demo_count_value", "demo_seed_value", "demo_preset_name", "demo_intermission_value" };
	for (std::size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
	{
		const bool visible = (fields[i] == std::string("demo_duration_value") && stepPage) ||
			((fields[i] == std::string("demo_count_value") || fields[i] == std::string("demo_seed_value")) && classPage) ||
			((fields[i] == std::string("demo_preset_name") || fields[i] == std::string("demo_intermission_value")) && sessionPage);
		setFieldActive(controls_, fields[i], visible);
	}
	controls_.setWidgetVisible("demo_loop", sessionPage);
	controls_.setWidgetVisible("demo_up", true);
	controls_.setWidgetVisible("demo_down", true);
	controls_.setWidgetVisible("demo_remove", true);
	controls_.setWidgetVisible("demo_add", true);
	controls_.setWidgetVisible("demo_tabs", inspectorBounds_.width > 0.0f);
	controls_.setWidgetVisible("demo_switch_save", confirmPresetSwitch_);
	controls_.setWidgetVisible("demo_switch_discard", confirmPresetSwitch_);
	controls_.setWidgetVisible("demo_switch_cancel", confirmPresetSwitch_);
}

void DemoEditorView::synchronize(const DemoPresetStore& presets, const DemoPreset& draft,
	const std::string& selectedPresetId, std::size_t selectedStep, const DemoRunner& runner,
	bool setupEnabled, int logicalWidth, int logicalHeight, bool draftDirty,
	const std::string& pendingPresetSwitchId, UiPresetId appearancePreset, float dpiScale)
{
	draft_ = draft;
	selectedPresetId_ = selectedPresetId;
	selectedStep_ = draft_.steps.empty() ? 0 : std::min(selectedStep, draft_.steps.size() - 1);
	setupEnabled_ = setupEnabled;
	demoActive_ = runner.active();
	draftDirty_ = draftDirty;
	pendingPresetSwitchId_ = pendingPresetSwitchId;
	confirmPresetSwitch_ = !pendingPresetSwitchId_.empty();
	runnerState_ = runner.state();
	runnerStepIndex_ = runner.stepIndex();
	runnerStepCount_ = runner.preset().steps.size();
	runnerStatus_ = runner.message();
	dpiScale_ = std::max(0.5f, std::min(4.0f, dpiScale));
	if (runner.active())
	{
		const DemoStep* active = runner.currentStep();
		const sortinglogic::AlgorithmDescriptor* descriptor = active
			? sortinglogic::algorithmDescriptorById(active->familyId, active->variantId) : nullptr;
		runnerStatus_ = "Running " + std::to_string(runnerStepIndex_ + 1) + "/" + std::to_string(runnerStepCount_) +
			" · " + (descriptor ? descriptor->displayName : "Unavailable algorithm");
		if (runner.hasEffectiveInput()) runnerStatus_ += " · " + std::to_string(runner.effectiveInput().itemCount) + " items";
		if (!runner.adjustments().empty()) runnerStatus_ += " · " + runner.adjustments().front();
	}
	synchronizeCatalogs(presets);
	if (selectedComplexityIndex_ >= complexityClasses_.size()) selectedComplexityIndex_ = 0;
	int stepComplexity = -1;
	if (selectedStep_ < draft_.steps.size())
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(
			draft_.steps[selectedStep_].familyId, draft_.steps[selectedStep_].variantId);
		if (descriptor) stepComplexity = static_cast<int>(descriptor->demoComplexity);
	}
	if (selectedStep_ != synchronizedStepIndex_ || stepComplexity != synchronizedStepComplexity_)
	{
		if (stepComplexity >= 0)
			for (std::size_t i = 0; i < complexityClasses_.size(); ++i)
				if (static_cast<int>(complexityClasses_[i]) == stepComplexity) selectedComplexityIndex_ = i;
		synchronizedStepIndex_ = selectedStep_;
		synchronizedStepComplexity_ = stepComplexity;
	}
	synchronizeQueue();
	synchronizeOptions();
	const DemoStep* step = currentStep();
	const DemoInputRecipe shared = currentSharedRecipe();
	const DemoInputRecipe effective = currentEffectiveRecipe();
	if (UiWidget* classes = controls_.find("demo_class")) classes->setSelectedIndex(static_cast<int>(selectedComplexityIndex_));
	if (UiWidget* domain = controls_.find("demo_domain")) domain->setSelectedIndex(domainSelection(shared.domain));
	if (UiWidget* pattern = controls_.find("demo_pattern")) pattern->setSelectedIndex(shared.pattern == sortinglogic::InputPattern::AndrutReference ? 6 : static_cast<int>(shared.pattern));
	if (UiWidget* direction = controls_.find("demo_direction")) direction->setSelectedIndex(step && step->direction == sortinglogic::SortDirection::Descending ? 1 : 0);
	if (UiWidget* timing = controls_.find("demo_timing")) timing->setSelectedIndex(step && step->timingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence ? 1 : 0);
	if (UiWidget* completion = controls_.find("demo_completion_scan"))
	{
		completion->setChecked(step && step->completionScanEnabled);
		completion->setLabel(std::string("Completion scan: ") + (step && step->completionScanEnabled ? "on" : "off"));
	}
	if (UiWidget* overrideButton = controls_.find("demo_override"))
		overrideButton->setLabel(step && step->hasInputOverride ? "Clear step override" : "Use step override");
	if (UiEditableField* field = controls_.findEditableField("demo_duration_value")) field->setValue(step ? shortDuration(step->fixedDurationSeconds) : "10");
	if (UiEditableField* field = controls_.findEditableField("demo_count_value")) field->setValue(std::to_string(shared.itemCount));
	if (UiEditableField* field = controls_.findEditableField("demo_seed_value")) field->setValue(std::to_string(shared.seed));
	if (UiEditableField* field = controls_.findEditableField("demo_preset_name")) field->setValue(draft_.name);
	if (UiEditableField* field = controls_.findEditableField("demo_intermission_value")) field->setValue(shortDuration(draft_.intermissionSeconds));
	if (UiWidget* loop = controls_.find("demo_loop")) loop->setLabel(std::string("Loop: ") + (draft_.loop ? "on" : "off"));
	if (UiWidget* save = controls_.find("demo_save")) save->setLabel(draft_.prefab ? "Save a copy" : "Save");
	const bool editable = setupEnabled_ && !demoActive_;
	controls_.setWidgetEnabled("demo_preset", editable);
	controls_.setWidgetEnabled("demo_delete", !draft_.prefab && editable);
	controls_.setWidgetEnabled("demo_queue", true);
	controls_.setWidgetEnabled("demo_up", editable && selectedStep_ > 0);
	controls_.setWidgetEnabled("demo_down", editable && selectedStep_ + 1 < draft_.steps.size());
	controls_.setWidgetEnabled("demo_remove", editable && step);
	controls_.setWidgetEnabled("demo_add", editable && draft_.steps.size() < DemoPresetStore::MaximumStepsPerPreset);
	controls_.setWidgetEnabled("demo_replace", editable && step != nullptr);
	controls_.setWidgetEnabled("demo_direction", editable && step);
	controls_.setWidgetEnabled("demo_timing", editable && step);
	controls_.setWidgetEnabled("demo_completion_scan", editable && step);
	controls_.setWidgetEnabled("demo_option_group", editable && step && !optionGroupIds_.empty());
	controls_.setWidgetEnabled("demo_option", editable && step && !optionIds_.empty());
	controls_.setWidgetEnabled("demo_override", editable && step);
	controls_.setWidgetEnabled("demo_class", editable);
	controls_.setWidgetEnabled("demo_domain", editable);
	controls_.setWidgetEnabled("demo_pattern", editable);
	controls_.setWidgetEnabled("demo_loop", editable);
	controls_.setWidgetEnabled("demo_save", editable && !draft_.steps.empty() && !draft_.name.empty());
	controls_.setWidgetEnabled("demo_start", demoActive_ || !draft_.steps.empty());
	controls_.find("demo_start")->setLabel(demoActive_ ? (runnerState_ == DemoRunnerState::Playing ? "Pause" : "Resume") : "Start Demo");
	controls_.find("demo_start")->setAction(AppAction(demoActive_ ? AppActionKind::ToggleDemoPlayback : AppActionKind::StartDemo));
	controls_.find("demo_mode")->setLabel(demoActive_ ? "Demo mode · on" : "Demo mode · off");
	controls_.setWidgetEnabled("demo_previous", demoActive_ && runnerStepIndex_ > 0);
	controls_.setWidgetEnabled("demo_next", demoActive_ && runnerStepIndex_ + 1 < runnerStepCount_);
	controls_.setWidgetEnabled("demo_stop", demoActive_);
	controls_.setWidgetEnabled("demo_switch_save", confirmPresetSwitch_);
	controls_.setWidgetEnabled("demo_switch_discard", confirmPresetSwitch_);
	controls_.setWidgetEnabled("demo_switch_cancel", confirmPresetSwitch_);
	if (step && step->timingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
		controls_.setWidgetEnabled("demo_duration_value", false);
	else controls_.setWidgetEnabled("demo_duration_value", editable && step);
	controls_.setWidgetEnabled("demo_count_value", editable);
	controls_.setWidgetEnabled("demo_seed_value", editable);
	controls_.setWidgetEnabled("demo_preset_name", editable);
	controls_.setWidgetEnabled("demo_intermission_value", editable);
	applyPageVisibility();
	layout(logicalWidth, logicalHeight, appearancePreset);
}

void DemoEditorView::layout(int logicalWidth, int logicalHeight, UiPresetId appearancePreset)
{
	logicalWidth_ = logicalWidth; logicalHeight_ = logicalHeight;
	const bool invalidate = logicalWidth != laidOutWidth_ || logicalHeight != laidOutHeight_ ||
		activeTab_ != laidOutTab_ || draft_.steps.size() != laidOutStepCount_ || appearancePreset != laidOutPreset_ ||
		dpiScale_ != laidOutDpiScale_ || bodyViewport_.offset() != laidOutScroll_;
	if (!invalidate) return;
	laidOutWidth_ = logicalWidth; laidOutHeight_ = logicalHeight; laidOutTab_ = activeTab_;
	laidOutStepCount_ = draft_.steps.size(); laidOutPreset_ = appearancePreset;
	laidOutDpiScale_ = dpiScale_; laidOutScroll_ = bodyViewport_.offset();
	const float screenW = static_cast<float>(std::max(1, logicalWidth));
	const float screenH = static_cast<float>(std::max(1, logicalHeight));
	const float panelW = std::max(0.0f, std::min(1080.0f, screenW - 16.0f));
	const float panelH = std::max(0.0f, std::min(840.0f, screenH - 16.0f));
	panelBounds_ = UiRect((screenW - panelW) * 0.5f, (screenH - panelH) * 0.5f, panelW, panelH);
	headerBounds_ = UiRect(panelBounds_.x + 20.0f, panelBounds_.y + 12.0f, panelW - 40.0f, 48.0f);
	const float pad = 18.0f;
	const float left = panelBounds_.x + pad;
	const float innerW = panelW - pad * 2.0f;
	const float presetLabelY = panelBounds_.y + 66.0f;
	const float presetY = panelBounds_.y + 86.0f;
	const float controlH = 34.0f;
	place(controls_, "demo_preset", left, presetY, innerW * 0.59f, controlH);
	place(controls_, "demo_save", left + innerW * 0.60f, presetY, innerW * 0.16f, controlH);
	place(controls_, "demo_delete", left + innerW * 0.77f, presetY, innerW * 0.10f, controlH);
	place(controls_, "demo_close", left + innerW * 0.88f, presetY, innerW * 0.12f, controlH);
	const float bodyTop = presetY + controlH + 16.0f;
	const float footerH = 86.0f;
	const float transportH = 48.0f;
	footerBounds_ = UiRect(panelBounds_.x, panelBounds_.y + panelH - footerH - transportH,
		panelW, footerH);
	transportBounds_ = UiRect(panelBounds_.x, footerBounds_.y + footerBounds_.height, panelW, transportH);
	bodyBounds_ = UiRect(left, bodyTop, innerW, std::max(100.0f, footerBounds_.y - bodyTop - 8.0f));
	const float gap = 16.0f;
	const bool twoColumns = innerW >= 840.0f;
	const float contentQueueW = twoColumns ? std::min(360.0f, innerW * 0.38f) : innerW;
	const float contentQueueH = twoColumns ? bodyBounds_.height : std::min(236.0f, std::max(160.0f, bodyBounds_.height * 0.46f));
	const float contentInspectorW = twoColumns ? innerW - contentQueueW - gap : innerW;
	const float inspectorFullH = activeTab_ == DemoEditorTab::Step ? 430.0f :
		activeTab_ == DemoEditorTab::ClassInputs ? 360.0f : 340.0f;
	const float totalContentH = twoColumns ? bodyBounds_.height : contentQueueH + gap + inspectorFullH;
	bodyViewport_.configure(bodyBounds_, totalContentH, 30.0f, bodyViewport_.offset());
	const float scroll = bodyViewport_.offset();
	queueBounds_ = UiRect(left, bodyTop - scroll, contentQueueW, contentQueueH);
	inspectorBounds_ = twoColumns
		? UiRect(left + contentQueueW + gap, bodyTop, contentInspectorW, bodyBounds_.height)
		: UiRect(left, queueBounds_.y + contentQueueH + gap, contentInspectorW, inspectorFullH);
	UiSelectionList* queue = controls_.findSelectionList("demo_queue");
	queueBounds_ = UiRect(queueBounds_.x, queueBounds_.y, queueBounds_.width, queueBounds_.height);
	const float listTop = queueBounds_.y + 34.0f;
	const float toolbarY = queueBounds_.y + queueBounds_.height - 72.0f;
	listClip_ = UiRect(queueBounds_.x + 2.0f, listTop, queueBounds_.width - 4.0f,
		std::max(42.0f, toolbarY - listTop - 8.0f));
	if (queue) { queue->setBounds(listClip_); queue->setRowHeight(54.0f); }
	const float queueGap = 8.0f;
	place(controls_, "demo_up", queueBounds_.x, toolbarY, (queueBounds_.width - queueGap * 2.0f) / 3.0f, 32.0f);
	place(controls_, "demo_down", queueBounds_.x + (queueBounds_.width - queueGap * 2.0f) / 3.0f + queueGap,
		toolbarY, (queueBounds_.width - queueGap * 2.0f) / 3.0f, 32.0f);
	place(controls_, "demo_remove", queueBounds_.x + 2.0f * ((queueBounds_.width - queueGap * 2.0f) / 3.0f + queueGap),
		toolbarY, (queueBounds_.width - queueGap * 2.0f) / 3.0f, 32.0f);
	place(controls_, "demo_add", queueBounds_.x, toolbarY + 40.0f, queueBounds_.width, 32.0f);
	const float ix = inspectorBounds_.x;
	const float iy = inspectorBounds_.y;
	const float iw = inspectorBounds_.width;
	if (UiTabStrip* tabs = controls_.findTabStrip("demo_tabs")) tabs->setBounds(UiRect(ix, iy, iw, 38.0f));
	const float rowH = 34.0f;
	if (activeTab_ == DemoEditorTab::Step)
	{
		place(controls_, "demo_replace", ix + iw * 0.60f, iy + 48.0f, iw * 0.40f, rowH);
		const float halfGap = 10.0f;
		place(controls_, "demo_direction", ix, iy + 100.0f, (iw - halfGap) * 0.5f, rowH);
		place(controls_, "demo_timing", ix + (iw + halfGap) * 0.5f, iy + 100.0f, (iw - halfGap) * 0.5f, rowH);
		placeField(controls_, "demo_duration_value", ix, iy + 172.0f, (iw - halfGap) * 0.48f, rowH);
		place(controls_, "demo_completion_scan", ix + iw * 0.52f, iy + 172.0f, iw * 0.48f, rowH);
		place(controls_, "demo_option_group", ix, iy + 246.0f, (iw - halfGap) * 0.42f, rowH);
		place(controls_, "demo_option", ix + (iw + halfGap) * 0.42f, iy + 246.0f, (iw - halfGap) * 0.58f, rowH);
		place(controls_, "demo_override", ix, iy + 370.0f, std::min(iw, 250.0f), rowH);
	}
	else if (activeTab_ == DemoEditorTab::ClassInputs)
	{
		place(controls_, "demo_class", ix, iy + 58.0f, iw, rowH);
		const float halfGap = 10.0f;
		place(controls_, "demo_domain", ix, iy + 126.0f, (iw - halfGap) * 0.5f, rowH);
		place(controls_, "demo_pattern", ix + (iw + halfGap) * 0.5f, iy + 126.0f, (iw - halfGap) * 0.5f, rowH);
		placeField(controls_, "demo_count_value", ix, iy + 202.0f, (iw - halfGap) * 0.5f, rowH);
		placeField(controls_, "demo_seed_value", ix + (iw + halfGap) * 0.5f, iy + 202.0f, (iw - halfGap) * 0.5f, rowH);
	}
	else
	{
		placeField(controls_, "demo_preset_name", ix, iy + 58.0f, iw, rowH);
		placeField(controls_, "demo_intermission_value", ix, iy + 132.0f, std::min(iw * 0.5f, 250.0f), rowH);
		place(controls_, "demo_loop", ix, iy + 202.0f, std::min(iw * 0.5f, 250.0f), rowH);
	}
	const float footerY = footerBounds_.y;
	place(controls_, "demo_start", panelBounds_.x + panelW - 194.0f, footerY + 22.0f, 168.0f, 38.0f);
	const float transY = transportBounds_.y + 8.0f;
	place(controls_, "demo_mode", panelBounds_.x + 18.0f, transY, 150.0f, 32.0f);
	place(controls_, "demo_previous", panelBounds_.x + panelW - 388.0f, transY, 114.0f, 32.0f);
	place(controls_, "demo_next", panelBounds_.x + panelW - 266.0f, transY, 104.0f, 32.0f);
	place(controls_, "demo_stop", panelBounds_.x + panelW - 154.0f, transY, 136.0f, 32.0f);
	const float dialogW = std::min(520.0f, panelW - 32.0f);
	const float dialogX = panelBounds_.x + (panelW - dialogW) * 0.5f;
	const float dialogY = panelBounds_.y + panelH * 0.34f;
	place(controls_, "demo_switch_save", dialogX, dialogY + 72.0f, dialogW / 3.0f - 6.0f, 36.0f);
	place(controls_, "demo_switch_discard", dialogX + dialogW / 3.0f, dialogY + 72.0f, dialogW / 3.0f - 6.0f, 36.0f);
	place(controls_, "demo_switch_cancel", dialogX + dialogW * 2.0f / 3.0f, dialogY + 72.0f, dialogW / 3.0f - 6.0f, 36.0f);
	controls_.setModalBounds(confirmPresetSwitch_ ? UiRect(dialogX - 10.0f, dialogY - 10.0f, dialogW + 20.0f, 126.0f) : panelBounds_,
		confirmPresetSwitch_ ? UiAction::ViewCommand::None : UiAction::ViewCommand::CloseDemoEditor);
	controls_.setInputCaptured(open_);
	applyPageVisibility();
	// Controls in a scrolled stack are fully hidden outside the content viewport,
	// so a clipped button cannot accidentally consume a click from the footer.
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		UiWidget* widget = controls_.widgets()[i].get();
		const std::string& id = widget->id();
		const bool contentControl = id != "demo_preset" && id != "demo_save" && id != "demo_delete" && id != "demo_close" &&
			id != "demo_start" && id != "demo_mode" && id != "demo_previous" && id != "demo_next" && id != "demo_stop" &&
			id.find("demo_switch_") != 0;
		if (contentControl && widget->visible())
		{
			const UiRect& b = widget->bounds();
			const bool fullyInside = b.y >= bodyBounds_.y && b.y + b.height <= bodyBounds_.y + bodyBounds_.height;
			if (!fullyInside) controls_.setWidgetVisible(id, false);
		}
	}
	for (std::size_t i = 0; i < controls_.editableFields().size(); ++i)
	{
		UiEditableField* field = controls_.editableFields()[i].get();
		if (!field->visible()) continue;
		const UiRect& b = field->bounds();
		const bool fullyInside = b.y >= bodyBounds_.y && b.y + b.height <= bodyBounds_.y + bodyBounds_.height;
		if (!fullyInside) controls_.setWidgetVisible(field->id(), false);
	}
}

void DemoEditorView::setOpen(bool value)
{
	open_ = value;
	controls_.setInputCaptured(value);
	if (!value) pickerIntent_ = DemoAlgorithmPickerIntent::None;
}

bool DemoEditorView::editingName() const
{
	const UiEditableField* field = controls_.findEditableField("demo_preset_name");
	return field && field->editing();
}

bool DemoEditorView::editingNumeric() const
{
	const char* ids[] = { "demo_duration_value", "demo_count_value", "demo_seed_value", "demo_intermission_value" };
	for (std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
	{
		const UiEditableField* field = controls_.findEditableField(ids[i]);
		if (field && field->editing()) return true;
	}
	return false;
}

DemoAlgorithmPickerIntent DemoEditorView::takeAlgorithmPickerIntent()
{
	const DemoAlgorithmPickerIntent result = pickerIntent_;
	pickerIntent_ = DemoAlgorithmPickerIntent::None;
	return result;
}

bool DemoEditorView::handleInput(const UiInputEvent& input, AppAction& action)
{
	if (!open_) return false;
	if (input.type == UiInputType::Wheel && bodyBounds_.contains(input.x, input.y) &&
		!listClip_.contains(input.x, input.y))
	{
		if (bodyViewport_.scrollWheel(input.x, input.y, input.wheelDelta, 1.0f))
		{
			laidOutScroll_ = -1.0f;
			layout(logicalWidth_, logicalHeight_, laidOutPreset_);
			return true;
		}
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
	if (emitted.event.controlId == "demo_tabs")
	{
		activeTab_ = emitted.event.selection == 1 ? DemoEditorTab::ClassInputs :
			emitted.event.selection == 2 ? DemoEditorTab::Session : DemoEditorTab::Step;
		bodyViewport_.setOffset(0.0f);
		applyPageVisibility();
		laidOutWidth_ = -1;
		layout(logicalWidth_, logicalHeight_, laidOutPreset_);
		return true;
	}
	if (emitted.event.controlId == "demo_queue")
	{
		selectedStep_ = static_cast<std::size_t>(std::max(0, emitted.event.selection));
		const DemoStep* step = currentStep();
		if (step)
		{
			const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step->familyId, step->variantId);
			if (descriptor)
				for (std::size_t i = 0; i < complexityClasses_.size(); ++i)
					if (complexityClasses_[i] == descriptor->demoComplexity) selectedComplexityIndex_ = i;
		}
		DemoActionPayload payload; payload.stepIndex = selectedStep_;
		action = demoAction(AppActionKind::SelectDemoStep, payload);
		return true;
	}
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
	if (id == "demo_switch_save" || id == "demo_switch_discard" || id == "demo_switch_cancel")
	{
		DemoActionPayload payload; payload.presetId = pendingPresetSwitchId_;
		payload.direction = id == "demo_switch_save" ? 0 : id == "demo_switch_discard" ? 1 : 2;
		AppAction action = demoAction(AppActionKind::ResolveDemoPresetSwitch, payload);
		action.selection = payload.direction;
		return action;
	}
	if (id == "demo_tabs") return AppAction();
	if (id == "demo_add") { pickerIntent_ = DemoAlgorithmPickerIntent::Add; return AppAction(); }
	if (id == "demo_replace") { pickerIntent_ = DemoAlgorithmPickerIntent::Replace; return AppAction(); }
	if (id == "demo_up" || id == "demo_down" || id == "demo_remove")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_;
		return demoAction(id == "demo_up" ? AppActionKind::DemoMoveStepUp : id == "demo_down"
			? AppActionKind::DemoMoveStepDown : AppActionKind::DemoRemoveStep, payload);
	}
	if (id == "demo_direction")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.direction = selection;
		return demoAction(AppActionKind::DemoSetStepDirection, payload);
	}
	if (id == "demo_timing")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.timingMode = selection == 1
			? static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence)
			: static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration);
		payload.value = currentStep() ? currentStep()->fixedDurationSeconds : 10.0f;
		return demoAction(AppActionKind::DemoSetStepTiming, payload);
	}
	if (id == "demo_completion_scan")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.flag = !(currentStep() && currentStep()->completionScanEnabled);
		return demoAction(AppActionKind::DemoSetStepCompletionScan, payload);
	}
	if (id == "demo_option_group") { synchronizeOptions(); return AppAction(); }
	if (id == "demo_option" && selection >= 0 && static_cast<std::size_t>(selection) < optionIds_.size() && currentStep())
	{
		const int groupIndex = controls_.find("demo_option_group")->selectedIndex();
		if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) >= optionGroupIds_.size()) return AppAction();
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.optionGroupId = optionGroupIds_[static_cast<std::size_t>(groupIndex)];
		payload.optionStableId = optionIds_[static_cast<std::size_t>(selection)];
		return demoAction(AppActionKind::DemoSetStepOption, payload);
	}
	if (id == "demo_override" && currentStep())
	{
		DemoActionPayload payload = payloadForRecipe(currentSharedRecipe());
		return demoAction(currentStep()->hasInputOverride ? AppActionKind::DemoClearStepOverride
			: AppActionKind::DemoSetStepOverride, payload);
	}
	if (id == "demo_class" && selection >= 0)
	{
		selectedComplexityIndex_ = std::min(static_cast<std::size_t>(selection), complexityClasses_.size() - 1);
		const DemoInputRecipe recipe = currentSharedRecipe();
		if (UiWidget* domain = controls_.find("demo_domain")) domain->setSelectedIndex(domainSelection(recipe.domain));
		if (UiWidget* pattern = controls_.find("demo_pattern")) pattern->setSelectedIndex(recipe.pattern == sortinglogic::InputPattern::AndrutReference ? 6 : static_cast<int>(recipe.pattern));
		return AppAction();
	}
	if (id == "demo_domain" || id == "demo_pattern")
	{
		DemoInputRecipe recipe = currentSharedRecipe();
		if (id == "demo_domain")
			recipe.domain = selection == 1 ? sortinglogic::InputDomain::NonnegativeIntegerKeys : selection == 2
				? sortinglogic::InputDomain::SignedIntegerKeys : selection == 3 ? sortinglogic::InputDomain::IntegerPermutation
				: sortinglogic::InputDomain::NormalizedFloats;
		else recipe.pattern = selection == 6 ? sortinglogic::InputPattern::AndrutReference : static_cast<sortinglogic::InputPattern>(selection);
		return demoAction(AppActionKind::DemoSetClassRecipe, payloadForRecipe(recipe));
	}
	if (id == "demo_count_value" || id == "demo_seed_value")
	{
		DemoInputRecipe recipe = currentSharedRecipe();
		if (id == "demo_seed_value") recipe.seed = static_cast<std::uint32_t>(std::strtoul(emitted.event.text.c_str(), nullptr, 10));
		else recipe.itemCount = static_cast<std::size_t>(std::strtoul(emitted.event.text.c_str(), nullptr, 10));
		return demoAction(AppActionKind::DemoSetClassRecipe, payloadForRecipe(recipe));
	}
	if (id == "demo_duration_value")
	{
		DemoActionPayload payload; payload.stepIndex = selectedStep_; payload.timingMode = currentStep()
			? static_cast<int>(currentStep()->timingMode) : static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration);
		payload.value = static_cast<float>(std::strtod(emitted.event.text.c_str(), nullptr));
		return demoAction(AppActionKind::DemoSetStepTiming, payload);
	}
	if (id == "demo_intermission_value")
	{
		DemoActionPayload payload; payload.value = static_cast<float>(std::strtod(emitted.event.text.c_str(), nullptr));
		return demoAction(AppActionKind::DemoSetIntermission, payload);
	}
	if (id == "demo_preset_name")
	{
		DemoActionPayload payload; payload.presetName = emitted.event.text;
		return demoAction(AppActionKind::DemoSetDraftName, payload);
	}
	if (id == "demo_loop")
	{
		DemoActionPayload payload; payload.flag = !draft_.loop;
		return demoAction(AppActionKind::DemoSetLoop, payload);
	}
	if (id == "demo_save") return demoAction(AppActionKind::SaveDemoPreset, DemoActionPayload());
	if (id == "demo_delete") return demoAction(AppActionKind::DeleteDemoPreset, DemoActionPayload());
	if (id == "demo_start" || id == "demo_mode" || id == "demo_previous" || id == "demo_next" || id == "demo_stop" || id == "demo_close")
		return emitted.appAction;
	return emitted.appAction;
}

void DemoEditorView::compose(const DemoEditorPalette& palette, int logicalWidth, int logicalHeight,
	UiPaintContext& paintContext) const
{
	if (!open_) return;
	const UiRect screen(0.0f, 0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight));
	submitRectangle(paintContext, screen, UiColor(0.0f, 0.0f, 0.0f, 0.64f), UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop);
	submitRectangle(paintContext, panelBounds_, palette.panel, UiPaintMaterial::Panel, UiPaintLayer::Panel);
	const float border = 1.0f;
	submitRectangle(paintContext, UiRect(panelBounds_.x, panelBounds_.y, panelBounds_.width, border), palette.accent,
		UiPaintMaterial::Accent, UiPaintLayer::Accent);
	submitRectangle(paintContext, UiRect(panelBounds_.x, panelBounds_.y + panelBounds_.height - border, panelBounds_.width, border),
		mix(palette.mutedText, palette.panel, 0.32f), UiPaintMaterial::Solid, UiPaintLayer::Accent);
	submitRectangle(paintContext, UiRect(panelBounds_.x, panelBounds_.y, border, panelBounds_.height),
		mix(palette.mutedText, palette.panel, 0.32f), UiPaintMaterial::Solid, UiPaintLayer::Accent);
	submitRectangle(paintContext, UiRect(panelBounds_.x + panelBounds_.width - border, panelBounds_.y, border, panelBounds_.height),
		mix(palette.mutedText, palette.panel, 0.32f), UiPaintMaterial::Solid, UiPaintLayer::Accent);
	composeText(palette, paintContext);
	composeSelectionList(palette, paintContext);
	composeTabs(palette, paintContext);
	composeFields(palette, paintContext);
	composeWidgetControls(palette, paintContext);
	composePresetSwitchConfirmation(palette, paintContext);
}

void DemoEditorView::composeText(const DemoEditorPalette& palette, UiPaintContext& context) const
{
	submitText(context, "#demo_title", "DEMO SESSION EDITOR", UiRect(headerBounds_.x, headerBounds_.y, headerBounds_.width, 24.0f),
		18.0f, palette.text, UiPaintLayer::SettingsText);
	submitText(context, "#demo_subtitle", "Actually Good Sorting Sounds · each algorithm receives its own input recipe",
		UiRect(headerBounds_.x, headerBounds_.y + 24.0f, headerBounds_.width, 18.0f), 12.0f, palette.mutedText, UiPaintLayer::SettingsText);
	submitText(context, "#demo_preset_caption", "Preset", UiRect(panelBounds_.x + 18.0f, panelBounds_.y + 64.0f, panelBounds_.width - 36.0f, 18.0f),
		12.0f, palette.mutedText, UiPaintLayer::SettingsText);
	submitText(context, "#demo_queue_title", "RUNNING ORDER  (" + std::to_string(draft_.steps.size()) + ")",
		UiRect(queueBounds_.x, queueBounds_.y, queueBounds_.width, 26.0f), 13.0f, palette.text, UiPaintLayer::SettingsText);
	if (draft_.steps.empty())
		submitText(context, "#demo_queue_empty", "No steps yet. Add an algorithm to begin the sequence.", listClip_, 13.0f, palette.mutedText,
			UiPaintLayer::SettingsText, listClip_, true);
	if (activeTab_ == DemoEditorTab::Step)
	{
		const DemoStep* step = currentStep();
		const sortinglogic::AlgorithmDescriptor* descriptor = step
			? sortinglogic::algorithmDescriptorById(step->familyId, step->variantId) : nullptr;
		submitText(context, "#demo_step_heading", step && descriptor
			? "STEP " + std::to_string(selectedStep_ + 1) + " · " + descriptor->displayName
			: "STEP · select an algorithm from the queue",
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 46.0f, inspectorBounds_.width * 0.58f, 25.0f), 14.0f, palette.text, UiPaintLayer::SettingsText);
		submitText(context, "#demo_step_direction_label", "Direction", UiRect(inspectorBounds_.x, inspectorBounds_.y + 80.0f,
			inspectorBounds_.width * 0.48f, 16.0f), 12.0f, palette.mutedText, UiPaintLayer::SettingsText);
		submitText(context, "#demo_step_timing_label", "Sound timing", UiRect(inspectorBounds_.x + inspectorBounds_.width * 0.52f,
			inspectorBounds_.y + 80.0f, inspectorBounds_.width * 0.48f, 16.0f), 12.0f, palette.mutedText, UiPaintLayer::SettingsText);
		submitText(context, "#demo_step_recipe", "INPUT RECIPE / " + (descriptor ? className(descriptor->demoComplexity) : "unavailable"),
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 306.0f, inspectorBounds_.width, 20.0f), 13.0f, palette.text, UiPaintLayer::SettingsText);
		const DemoInputRecipe recipe = currentEffectiveRecipe();
		const std::string recipeLine = std::to_string(recipe.itemCount) + " items · seed " + std::to_string(recipe.seed) + " · " +
			domainName(recipe.domain) + " · " + patternName(recipe.pattern) + (step && step->hasInputOverride ? " · step override" : " · shared class recipe");
		submitText(context, "#demo_step_recipe_detail", recipeLine,
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 330.0f, inspectorBounds_.width, 32.0f), 12.0f, palette.mutedText,
			UiPaintLayer::SettingsText);
	}
	else if (activeTab_ == DemoEditorTab::ClassInputs)
	{
		submitText(context, "#demo_class_heading", "SHARED INPUT RECIPE · " + (selectedComplexityIndex_ < complexityClasses_.size()
			? className(complexityClasses_[selectedComplexityIndex_]) : std::string("Unknown")),
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 42.0f, inspectorBounds_.width, 22.0f), 14.0f, palette.text, UiPaintLayer::SettingsText);
		submitText(context, "#demo_class_hint", "Edits here apply to this complexity class, even when the inspected step has an override.",
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 94.0f, inspectorBounds_.width, 28.0f), 12.0f, palette.mutedText, UiPaintLayer::SettingsText);
		std::string usedBy = "Used by: ";
		bool any = false;
		for (std::size_t i = 0; i < draft_.steps.size(); ++i)
		{
			const DemoStep& step = draft_.steps[i];
			if (step.hasInputOverride) continue;
			const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
			if (!descriptor || selectedComplexityIndex_ >= complexityClasses_.size() || descriptor->demoComplexity != complexityClasses_[selectedComplexityIndex_]) continue;
			if (any) usedBy += ", ";
			usedBy += "#" + std::to_string(i + 1) + " " + descriptor->displayName;
			any = true;
		}
		if (!any) usedBy += "no non-overridden steps";
		submitText(context, "#demo_class_used_by", usedBy,
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 272.0f, inspectorBounds_.width, 48.0f), 12.0f, palette.mutedText,
			UiPaintLayer::SettingsText);
	}
	else
	{
		submitText(context, "#demo_session_heading", "SESSION SETTINGS",
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 42.0f, inspectorBounds_.width, 22.0f), 14.0f, palette.text, UiPaintLayer::SettingsText);
		submitText(context, "#demo_session_restore", "Starting or stopping a demo restores the manual session paused at its saved cursor.",
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 254.0f, inspectorBounds_.width, 42.0f), 12.0f, palette.mutedText,
			UiPaintLayer::SettingsText);
		submitText(context, "#demo_session_summary", std::to_string(draft_.steps.size()) + " steps · " +
			shortDuration(draft_.intermissionSeconds) + " sec intermission · loop " + (draft_.loop ? "on" : "off"),
			UiRect(inspectorBounds_.x, inspectorBounds_.y + 218.0f, inspectorBounds_.width, 24.0f), 13.0f, palette.text, UiPaintLayer::SettingsText);
	}
	if (!runnerStatus_.empty())
		submitText(context, "#demo_runtime", runnerStatus_,
			UiRect(footerBounds_.x + 20.0f, footerBounds_.y + 15.0f, footerBounds_.width - 230.0f, 18.0f),
			12.0f, demoActive_ ? palette.accent : palette.mutedText, UiPaintLayer::SettingsText, footerBounds_, true);
	if (!feedback_.empty())
		submitText(context, "#demo_feedback", feedback_,
			UiRect(footerBounds_.x + 20.0f, footerBounds_.y + 38.0f, footerBounds_.width - 230.0f, 32.0f),
			12.0f, palette.mutedText, UiPaintLayer::SettingsText, footerBounds_, true);
	else if (!draftDirty_ && !demoActive_)
		submitText(context, "#demo_feedback", "Draft ready. Manual session preserved.",
			UiRect(footerBounds_.x + 20.0f, footerBounds_.y + 38.0f, footerBounds_.width - 230.0f, 24.0f),
			12.0f, palette.mutedText, UiPaintLayer::SettingsText, footerBounds_, true);
	else if (draftDirty_)
		submitText(context, "#demo_feedback", "Unsaved draft changes.",
			UiRect(footerBounds_.x + 20.0f, footerBounds_.y + 38.0f, footerBounds_.width - 230.0f, 24.0f),
			12.0f, palette.accent, UiPaintLayer::SettingsText, footerBounds_, true);
	if (!demoActive_)
		submitText(context, "#demo_transport_hint", "Select Demo mode to begin this retained draft.",
			UiRect(transportBounds_.x + 180.0f, transportBounds_.y + 13.0f, 360.0f, 20.0f), 11.0f,
			palette.mutedText, UiPaintLayer::SettingsText, transportBounds_, true);
}

void DemoEditorView::composeSelectionList(const DemoEditorPalette& palette, UiPaintContext& context) const
{
	const UiSelectionList* list = controls_.findSelectionList("demo_queue");
	if (!list || !list->visible()) return;
	const UiRect clip = list->bounds();
	submitRectangle(context, clip, palette.background, UiPaintMaterial::Panel, UiPaintLayer::Content, clip, true);
	const std::size_t first = list->viewport().firstVisibleItem(list->viewport().rowExtent());
	const std::size_t count = list->viewport().visibleItemCount(list->items().size(), list->viewport().rowExtent());
	for (std::size_t row = 0; row < count; ++row)
	{
		const std::size_t index = first + row;
		if (index >= list->items().size()) break;
		const UiRect bounds = list->viewport().itemBounds(index, list->viewport().rowExtent());
		const bool selected = static_cast<int>(index) == list->selectedIndex();
		const bool enabled = list->enabled();
		const UiControlPaintStyle style = resolveUiControlPaintStyle(palette.preset, palette.theme, "list-item", enabled,
			false, false, selected, paletteMap(palette));
		submitStyledBox(context, UiRect(bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, bounds.height - 4.0f), style,
			UiPaintLayer::Content, clip);
		std::string label = list->items()[index];
		const std::size_t tab = label.find('\t');
		const std::string position = tab == std::string::npos ? label : label.substr(0, tab);
		const std::size_t second = tab == std::string::npos ? std::string::npos : label.find('\t', tab + 1);
		const std::string name = tab == std::string::npos ? label : label.substr(tab + 1,
			second == std::string::npos ? std::string::npos : second - tab - 1);
		const std::string detail = second == std::string::npos ? std::string() : label.substr(second + 1);
		submitText(context, "#demo_queue_row_" + std::to_string(index) + "_title", position + "   " + name,
			UiRect(bounds.x + 12.0f, bounds.y + 4.0f, bounds.width - 24.0f, 22.0f), 14.0f, style.text,
			UiPaintLayer::Text, clip, true);
		if (!detail.empty())
			submitText(context, "#demo_queue_row_" + std::to_string(index) + "_detail", detail,
				UiRect(bounds.x + 38.0f, bounds.y + 27.0f, bounds.width - 52.0f, 18.0f), 12.0f,
				palette.mutedText, UiPaintLayer::Text, clip, true);
	}
}

void DemoEditorView::composeTabs(const DemoEditorPalette& palette, UiPaintContext& context) const
{
	const UiTabStrip* tabs = controls_.findTabStrip("demo_tabs");
	if (!tabs || !tabs->visible()) return;
	const UiRect bounds = tabs->bounds();
	for (std::size_t i = 0; i < tabs->tabs().size(); ++i)
	{
		const UiRect tabBounds = tabs->tabBounds(i);
		const bool selected = static_cast<int>(i) == tabs->selectedIndex();
		const UiControlPaintStyle style = resolveUiControlPaintStyle(palette.preset, palette.theme, "tab", tabs->enabled(),
			false, false, selected, paletteMap(palette));
		submitStyledBox(context, tabBounds, style, UiPaintLayer::SettingsControls, bounds);
		if (selected)
			submitRectangle(context, UiRect(tabBounds.x, tabBounds.y, tabBounds.width, 3.0f), palette.accent,
				UiPaintMaterial::Accent, UiPaintLayer::SettingsControlsOverlay, bounds, true);
		submitText(context, "#demo_tab_" + std::to_string(i), tabs->tabs()[i], tabBounds, 14.0f, style.text,
			UiPaintLayer::SettingsText, bounds, true, UiTextAlignment::Center);
	}
}

void DemoEditorView::composeFields(const DemoEditorPalette& palette, UiPaintContext& context) const
{
	for (std::size_t i = 0; i < controls_.editableFields().size(); ++i)
	{
		const UiEditableField& field = *controls_.editableFields()[i];
		if (!field.visible()) continue;
		const UiRect bounds = field.bounds();
		const UiControlPaintStyle style = resolveUiControlPaintStyle(palette.preset, palette.theme, "value", field.enabled(),
			false, field.focused(), field.focused(), paletteMap(palette));
		submitText(context, "#" + field.id() + "_caption", field.label(),
			UiRect(bounds.x, bounds.y - 18.0f, bounds.width, 16.0f), 12.0f, palette.mutedText, UiPaintLayer::SettingsText);
		UiControlPaintStyle fieldStyle = style;
		if (field.invalid()) fieldStyle.border = palette.accent;
		submitStyledBox(context, bounds, fieldStyle, UiPaintLayer::SettingsControls, bodyBounds_);
		submitText(context, "#" + field.id() + "_value", field.value() + (field.editing() ? "_" : ""),
			UiRect(bounds.x + 10.0f, bounds.y, bounds.width - 20.0f, bounds.height), 14.0f,
			field.enabled() ? palette.text : palette.mutedText, UiPaintLayer::SettingsText, bodyBounds_, true);
		if (field.invalid())
			submitText(context, "#" + field.id() + "_error", "Enter a valid value in range.",
				UiRect(bounds.x, bounds.y + bounds.height + 1.0f, bounds.width, 16.0f), 11.0f, palette.accent,
				UiPaintLayer::SettingsText, bodyBounds_, true);
	}
}

void DemoEditorView::composeWidgetControls(const DemoEditorPalette&, UiPaintContext& context) const
{
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const UiWidget& widget = *controls_.widgets()[i];
		if (!widget.visible() || widget.bounds().width <= 0.0f || widget.bounds().height <= 0.0f) continue;
		const bool dialog = widget.id().find("demo_switch_") == 0;
		const UiPaintLayer layer = dialog ? UiPaintLayer::SettingsControlsOverlay : UiPaintLayer::SettingsControls;
		context.submit(UiPaintCommand(UiPaintPrimitive::Control, widget.bounds(), UiColor(), std::string(), widget.id(),
			widget.kind() == UiWidgetKind::ComboBox ? UiPaintMaterial::Popup : UiPaintMaterial::Button,
			layer, dialog ? panelBounds_ : (widget.id() == "demo_preset" || widget.id() == "demo_save" || widget.id() == "demo_delete" || widget.id() == "demo_close" ? panelBounds_ : bodyBounds_),
			true, 14.0f));
		if (widget.expanded())
			context.submit(UiPaintCommand(UiPaintPrimitive::Control, widget.popupBounds(), UiColor(), std::string(), widget.id(),
				UiPaintMaterial::Popup, UiPaintLayer::Content, panelBounds_, true, 14.0f, true));
	}
}

void DemoEditorView::composePresetSwitchConfirmation(const DemoEditorPalette& palette, UiPaintContext& context) const
{
	if (!confirmPresetSwitch_) return;
	const float width = std::min(520.0f, panelBounds_.width - 32.0f);
	const UiRect dialog(panelBounds_.x + (panelBounds_.width - width) * 0.5f,
		panelBounds_.y + panelBounds_.height * 0.34f, width, 128.0f);
	submitRectangle(context, UiRect(0, 0, static_cast<float>(logicalWidth_), static_cast<float>(logicalHeight_)),
		UiColor(0.0f, 0.0f, 0.0f, 0.38f), UiPaintMaterial::ModalDim, UiPaintLayer::SettingsControlsOverlay);
	submitRectangle(context, dialog, palette.panel, UiPaintMaterial::Panel, UiPaintLayer::SettingsControlsOverlay, panelBounds_, true);
	submitRectangle(context, UiRect(dialog.x, dialog.y, dialog.width, 3.0f), palette.accent,
		UiPaintMaterial::Accent, UiPaintLayer::SettingsControlsOverlay, panelBounds_, true);
	submitText(context, "#demo_switch_title", "Unsaved demo draft",
		UiRect(dialog.x + 16.0f, dialog.y + 14.0f, dialog.width - 32.0f, 22.0f), 15.0f, palette.text,
		UiPaintLayer::SettingsText, dialog, true);
	submitText(context, "#demo_switch_message", "Save changes before switching presets?",
		UiRect(dialog.x + 16.0f, dialog.y + 42.0f, dialog.width - 32.0f, 20.0f), 13.0f, palette.mutedText,
		UiPaintLayer::SettingsText, dialog, true);
}

void DemoEditorView::layoutComboPopups(const std::function<float(const std::string&)>& measureOptionWidth)
{
	controls_.layoutComboPopups(UiRect(panelBounds_.x + 8.0f, panelBounds_.y + 8.0f,
		std::max(0.0f, panelBounds_.width - 16.0f), std::max(0.0f, panelBounds_.height - 16.0f)), measureOptionWidth, 12.0f);
}
