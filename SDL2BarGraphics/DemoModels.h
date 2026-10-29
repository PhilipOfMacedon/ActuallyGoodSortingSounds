#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../SortingLogic/SortingLogic.h"
#include "../SoundEngine/AudioTimeline.h"

struct DemoInputRecipe
{
	std::uint32_t seed;
	std::size_t itemCount;
	sortinglogic::InputDomain domain;
	sortinglogic::InputPattern pattern;

	DemoInputRecipe(std::uint32_t seedValue = 5489u, std::size_t count = 80,
		sortinglogic::InputDomain inputDomain = sortinglogic::InputDomain::NormalizedFloats,
		sortinglogic::InputPattern inputPattern = sortinglogic::InputPattern::Random)
		: seed(seedValue), itemCount(count), domain(inputDomain), pattern(inputPattern) {}
};

struct DemoStep
{
	std::string familyId;
	std::string variantId;
	std::map<std::string, std::string> optionSelections;
	sortinglogic::SortDirection direction;
	SoundEngine::PlaybackTimingMode timingMode;
	float fixedDurationSeconds;
	bool hasInputOverride;
	DemoInputRecipe inputOverride;

	DemoStep() : familyId(), variantId(), optionSelections(), direction(sortinglogic::SortDirection::Ascending),
		timingMode(SoundEngine::PlaybackTimingMode::FixedDuration), fixedDurationSeconds(10.0f),
		hasInputOverride(false), inputOverride() {}
};

struct DemoPreset
{
	std::string id;
	std::string name;
	bool prefab;
	std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe> classInputs;
	std::vector<DemoStep> steps;
	float intermissionSeconds;
	bool loop;

	DemoPreset() : id(), name(), prefab(false), classInputs(), steps(), intermissionSeconds(1.0f), loop(false) {}
};

struct DemoInputAdjustment
{
	sortinglogic::DemoComplexityClass complexity;
	DemoInputRecipe recipe;
	std::vector<std::string> messages;
	std::string error;
	DemoInputAdjustment() : complexity(sortinglogic::DemoComplexityClass::DemonstrationUnknown), recipe(), messages(), error() {}
};

struct DemoPresetIssue
{
	std::size_t stepIndex;
	std::string message;
	DemoPresetIssue(std::size_t step, const std::string& detail) : stepIndex(step), message(detail) {}
};

const std::vector<sortinglogic::DemoComplexityClass>& demoComplexityClassOrder();
const std::vector<DemoPreset>& demoPrefabPresets();
const DemoPreset* demoPrefabPreset(const std::string& id);
DemoInputRecipe demoRecipeForStep(const DemoPreset& preset, const DemoStep& step, std::string* error = nullptr);
DemoInputAdjustment adaptDemoInput(const DemoPreset& preset, const DemoStep& step, const DemoInputRecipe& recipe);
std::vector<DemoPresetIssue> validateDemoPreset(const DemoPreset& preset);

class DemoPresetStore
{
public:
	static const int CurrentVersion = 1;
	static const std::size_t MaximumPresets = 128;
	static const std::size_t MaximumStepsPerPreset = 256;

	DemoPresetStore();
	const std::vector<DemoPreset>& userPresets() const { return userPresets_; }
	const DemoPreset* findUserPreset(const std::string& id) const;
	bool saveUserPreset(const DemoPreset& preset, std::string* error = nullptr);
	bool renameUserPreset(const std::string& id, const std::string& name, std::string* error = nullptr);
	bool deleteUserPreset(const std::string& id);
	DemoPreset clonePrefabAsUser(const std::string& prefabId, const std::string& id, const std::string& name) const;

	static std::string encode(const std::vector<DemoPreset>& presets);
	static bool decode(const std::string& content, std::vector<DemoPreset>& presets,
		std::vector<std::string>* recoveredIssues = nullptr, std::string* error = nullptr);

private:
	std::vector<DemoPreset> userPresets_;
};
