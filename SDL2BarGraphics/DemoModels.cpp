#include "DemoModels.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace
{
	std::string escapeField(const std::string& value)
	{
		static const char Hex[] = "0123456789ABCDEF";
		std::string output;
		for (std::size_t i = 0; i < value.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(value[i]);
			if (c == '%' || c == '\t' || c == '\r' || c == '\n')
			{
				output += '%';
				output += Hex[c >> 4];
				output += Hex[c & 15];
			}
			else output += static_cast<char>(c);
		}
		return output;
	}

	int fromHex(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	}

	bool unescapeField(const std::string& encoded, std::string& output)
	{
		output.clear();
		for (std::size_t i = 0; i < encoded.size(); ++i)
		{
			if (encoded[i] != '%') output += encoded[i];
			else
			{
				if (i + 2 >= encoded.size()) return false;
				const int high = fromHex(encoded[i + 1]);
				const int low = fromHex(encoded[i + 2]);
				if (high < 0 || low < 0) return false;
				output += static_cast<char>((high << 4) | low);
				i += 2;
			}
		}
		return true;
	}

	std::vector<std::string> splitTabs(const std::string& line)
	{
		std::vector<std::string> fields;
		std::size_t begin = 0;
		for (;;)
		{
			const std::size_t next = line.find('\t', begin);
			fields.push_back(line.substr(begin, next == std::string::npos ? next : next - begin));
			if (next == std::string::npos) break;
			begin = next + 1;
		}
		return fields;
	}

	bool parseUnsigned(const std::string& text, std::uint64_t maximum, std::uint64_t& value)
	{
		if (text.empty()) return false;
		std::uint64_t parsed = 0;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if (c < '0' || c > '9') return false;
			const std::uint64_t digit = c - '0';
			if (digit > maximum || parsed > (maximum - digit) / 10) return false;
			parsed = parsed * 10 + digit;
		}
		value = parsed;
		return true;
	}

	bool parseFloat(const std::string& text, float& value)
	{
		char* end = nullptr;
		const float parsed = std::strtof(text.c_str(), &end);
		if (!end || end == text.c_str() || *end || !std::isfinite(parsed)) return false;
		value = parsed;
		return true;
	}

	bool parseIntEnum(const std::string& text, int minimum, int maximum, int& value)
	{
		std::uint64_t parsed = 0;
		if (!parseUnsigned(text, static_cast<std::uint64_t>(maximum), parsed) ||
			parsed < static_cast<std::uint64_t>(minimum)) return false;
		value = static_cast<int>(parsed);
		return true;
	}

	std::string encodeOptions(const std::map<std::string, std::string>& options)
	{
		std::ostringstream text;
		for (std::map<std::string, std::string>::const_iterator i = options.begin(); i != options.end(); ++i)
		{
			if (i != options.begin()) text << ';';
			text << escapeField(i->first) << '=' << escapeField(i->second);
		}
		return text.str();
	}

	bool decodeOptions(const std::string& text, std::map<std::string, std::string>& options)
	{
		options.clear();
		std::size_t begin = 0;
		while (begin < text.size())
		{
			const std::size_t end = text.find(';', begin);
			const std::string item = text.substr(begin, end == std::string::npos ? end : end - begin);
			const std::size_t equals = item.find('=');
			std::string group, stableId;
			if (equals == std::string::npos || !unescapeField(item.substr(0, equals), group) ||
				!unescapeField(item.substr(equals + 1), stableId) || group.empty() || stableId.empty()) return false;
			options[group] = stableId;
			if (end == std::string::npos) break;
			begin = end + 1;
		}
		return true;
	}

	DemoStep stepFor(sortinglogic::SortAlgorithm algorithm, SoundEngine::PlaybackTimingMode timing, float duration)
	{
		DemoStep step;
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(algorithm);
		if (descriptor)
		{
			step.familyId = descriptor->familyId;
			step.variantId = descriptor->variantId;
			std::set<std::string> groups;
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			{
				const sortinglogic::AlgorithmOptionDescriptor& option = descriptor->supportedOptions[i];
				if (option.isDefault && groups.insert(option.groupId).second)
					step.optionSelections[option.groupId] = option.stableId;
			}
		}
		step.timingMode = timing;
		step.fixedDurationSeconds = duration;
		return step;
	}

	DemoPreset makePrefab(const std::string& id, const std::string& name)
	{
		DemoPreset preset;
		preset.id = id;
		preset.name = name;
		preset.prefab = true;
		return preset;
	}

	bool validRecipe(const DemoInputRecipe& recipe)
	{
		return recipe.itemCount >= sortinglogic::MinItemCount && recipe.itemCount <= sortinglogic::MaxItemCount &&
			static_cast<int>(recipe.domain) >= static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) &&
			static_cast<int>(recipe.domain) <= static_cast<int>(sortinglogic::InputDomain::IntegerPermutation) &&
			static_cast<int>(recipe.pattern) >= static_cast<int>(sortinglogic::InputPattern::Random) &&
			static_cast<int>(recipe.pattern) <= static_cast<int>(sortinglogic::InputPattern::AndrutReference);
	}

	void issue(std::vector<DemoPresetIssue>& issues, std::size_t step, const std::string& message)
	{
		issues.push_back(DemoPresetIssue(step, message));
	}
}

const int DemoPresetStore::CurrentVersion;
const std::size_t DemoPresetStore::MaximumPresets;
const std::size_t DemoPresetStore::MaximumStepsPerPreset;

const std::vector<sortinglogic::DemoComplexityClass>& demoComplexityClassOrder()
{
	static const std::vector<sortinglogic::DemoComplexityClass> classes = {
		sortinglogic::DemoComplexityClass::Linear, sortinglogic::DemoComplexityClass::NLogN,
		sortinglogic::DemoComplexityClass::NLogSquaredN, sortinglogic::DemoComplexityClass::NSqrtN,
		sortinglogic::DemoComplexityClass::Quadratic, sortinglogic::DemoComplexityClass::N2LogN,
		sortinglogic::DemoComplexityClass::HigherPolynomial, sortinglogic::DemoComplexityClass::GapDependent,
		sortinglogic::DemoComplexityClass::KeyDependent, sortinglogic::DemoComplexityClass::Superpolynomial,
		sortinglogic::DemoComplexityClass::DemonstrationUnknown };
	return classes;
}

const std::vector<DemoPreset>& demoPrefabPresets()
{
	static const std::vector<DemoPreset> presets = []()
	{
		std::vector<DemoPreset> result;
		DemoPreset classic = makePrefab("prefab.classic-five", "Classic Five");
		classic.classInputs[sortinglogic::DemoComplexityClass::Quadratic] = DemoInputRecipe(5489u, 80);
		classic.classInputs[sortinglogic::DemoComplexityClass::NLogN] = DemoInputRecipe(5489u, 320);
		const SoundEngine::PlaybackTimingMode cadence = SoundEngine::PlaybackTimingMode::ReferenceCadence;
		classic.steps.push_back(stepFor(sortinglogic::SortAlgorithm::Insertion, cadence, 10.0f));
		classic.steps.push_back(stepFor(sortinglogic::SortAlgorithm::Bubble, cadence, 10.0f));
		classic.steps.push_back(stepFor(sortinglogic::SortAlgorithm::Selection, cadence, 10.0f));
		classic.steps.push_back(stepFor(sortinglogic::SortAlgorithm::Merge, cadence, 10.0f));
		classic.steps.push_back(stepFor(sortinglogic::SortAlgorithm::Gnome, cadence, 10.0f));
		result.push_back(classic);

		DemoPreset tour = makePrefab("prefab.ten-sort-tour", "Ten-sort Tour");
		tour.classInputs[sortinglogic::DemoComplexityClass::Quadratic] = DemoInputRecipe(5489u, 80);
		tour.classInputs[sortinglogic::DemoComplexityClass::NLogN] = DemoInputRecipe(5489u, 320);
		tour.classInputs[sortinglogic::DemoComplexityClass::GapDependent] = DemoInputRecipe(5489u, 160);
		tour.classInputs[sortinglogic::DemoComplexityClass::KeyDependent] = DemoInputRecipe(5489u, 512,
			sortinglogic::InputDomain::IntegerPermutation);
		const SoundEngine::PlaybackTimingMode fixed = SoundEngine::PlaybackTimingMode::FixedDuration;
		const sortinglogic::SortAlgorithm algorithms[] = { sortinglogic::SortAlgorithm::Insertion,
			sortinglogic::SortAlgorithm::Bubble, sortinglogic::SortAlgorithm::Selection, sortinglogic::SortAlgorithm::Shell,
			sortinglogic::SortAlgorithm::Merge, sortinglogic::SortAlgorithm::Quick, sortinglogic::SortAlgorithm::Heap,
			sortinglogic::SortAlgorithm::Counting, sortinglogic::SortAlgorithm::Radix, sortinglogic::SortAlgorithm::Gnome };
		for (std::size_t i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); ++i)
			tour.steps.push_back(stepFor(algorithms[i], fixed, 10.0f));
		result.push_back(tour);
		return result;
	}();
	return presets;
}

const DemoPreset* demoPrefabPreset(const std::string& id)
{
	const std::vector<DemoPreset>& presets = demoPrefabPresets();
	for (std::size_t i = 0; i < presets.size(); ++i) if (presets[i].id == id) return &presets[i];
	return nullptr;
}

DemoInputRecipe demoRecipeForStep(const DemoPreset& preset, const DemoStep& step, std::string* error)
{
	if (step.hasInputOverride)
	{
		if (error) error->clear();
		return step.inputOverride;
	}
	const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
	if (!algorithm)
	{
		if (error) *error = "The step references an unavailable algorithm: " + step.familyId + "/" + step.variantId;
		return DemoInputRecipe();
	}
	const std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator recipe =
		preset.classInputs.find(algorithm->demoComplexity);
	if (recipe == preset.classInputs.end())
	{
		if (error) *error = "No input recipe is defined for complexity class " +
			std::string(sortinglogic::demoComplexityClassName(algorithm->demoComplexity)) + ".";
		return DemoInputRecipe();
	}
	if (error) error->clear();
	return recipe->second;
}

DemoInputAdjustment adaptDemoInput(const DemoPreset& preset, const DemoStep& step, const DemoInputRecipe& recipe)
{
	DemoInputAdjustment result;
	result.recipe = recipe;
	const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
	if (!algorithm)
	{
		result.error = "The step references an unavailable algorithm: " + step.familyId + "/" + step.variantId;
		return result;
	}
	result.complexity = algorithm->demoComplexity;
	const std::size_t maxCount = std::min(sortinglogic::MaxItemCount, algorithm->safeMaxItemCount);
	const std::size_t boundedCount = std::max(sortinglogic::MinItemCount, std::min(recipe.itemCount, maxCount));
	if (boundedCount != recipe.itemCount)
	{
		result.recipe.itemCount = boundedCount;
		result.messages.push_back("Item count adjusted from " + std::to_string(recipe.itemCount) + " to " +
			std::to_string(boundedCount) + " for this algorithm's supported range.");
	}
	if (!sortinglogic::algorithmSupports(algorithm->id, result.recipe.domain))
	{
		result.recipe.domain = algorithm->preferredDemoDomain;
		result.messages.push_back("Input domain changed to " + std::string(sortinglogic::inputDomainName(result.recipe.domain)) +
			" because this algorithm cannot use the saved domain.");
	}
	if (!sortinglogic::inputPatternCompatible(result.recipe.domain, result.recipe.pattern))
	{
		result.recipe.pattern = sortinglogic::InputPattern::Random;
		result.messages.push_back("Input pattern changed to Random because it is incompatible with the selected domain.");
	}
	if (result.recipe.pattern == sortinglogic::InputPattern::AndrutReference && result.recipe.itemCount != 100)
	{
		result.recipe.pattern = sortinglogic::InputPattern::Random;
		result.messages.push_back("Input pattern changed to Random because the Andrut reference contains exactly 100 items.");
	}
	if (!validRecipe(result.recipe)) result.error = "The effective input recipe is invalid.";
	(void)preset;
	return result;
}

std::vector<DemoPresetIssue> validateDemoPreset(const DemoPreset& preset)
{
	std::vector<DemoPresetIssue> issues;
	if (preset.name.empty()) issue(issues, 0, "Preset name is empty.");
	if (preset.steps.empty()) issue(issues, 0, "Preset has no algorithm steps.");
	if (!std::isfinite(preset.intermissionSeconds) || preset.intermissionSeconds < 0.0f || preset.intermissionSeconds > 120.0f)
		issue(issues, 0, "Intermission must be between 0 and 120 seconds.");
	for (std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator i = preset.classInputs.begin();
		i != preset.classInputs.end(); ++i)
		if (!validRecipe(i->second)) issue(issues, 0, "An input recipe contains an invalid item count, domain, or pattern.");
	for (std::size_t i = 0; i < preset.steps.size(); ++i)
	{
		const DemoStep& step = preset.steps[i];
		const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptorById(step.familyId, step.variantId);
		if (!algorithm)
		{
			issue(issues, i, "Algorithm unavailable: " + step.familyId + "/" + step.variantId);
			continue;
		}
		if (!step.hasInputOverride && preset.classInputs.find(algorithm->demoComplexity) == preset.classInputs.end())
			issue(issues, i, "Missing input recipe for " + std::string(sortinglogic::demoComplexityClassName(algorithm->demoComplexity)) + ".");
		if (step.hasInputOverride && !validRecipe(step.inputOverride))
			issue(issues, i, "Step input override is invalid.");
		if (step.timingMode == SoundEngine::PlaybackTimingMode::FixedDuration &&
			(!std::isfinite(step.fixedDurationSeconds) || step.fixedDurationSeconds < 1.0f || step.fixedDurationSeconds > 120.0f))
			issue(issues, i, "Fixed step duration must be between 1 and 120 seconds.");
		for (std::map<std::string, std::string>::const_iterator option = step.optionSelections.begin();
			option != step.optionSelections.end(); ++option)
		{
			bool found = false;
			for (std::size_t k = 0; k < algorithm->supportedOptions.size(); ++k)
				if (algorithm->supportedOptions[k].groupId == option->first &&
					algorithm->supportedOptions[k].stableId == option->second) found = true;
			if (!found) issue(issues, i, "Unavailable option " + option->first + "/" + option->second + ".");
		}
	}
	return issues;
}

DemoPresetStore::DemoPresetStore() : userPresets_() {}

const DemoPreset* DemoPresetStore::findUserPreset(const std::string& id) const
{
	for (std::size_t i = 0; i < userPresets_.size(); ++i) if (userPresets_[i].id == id) return &userPresets_[i];
	return nullptr;
}

bool DemoPresetStore::saveUserPreset(const DemoPreset& preset, std::string* error)
{
	if (preset.prefab || preset.id.empty() || preset.name.empty() || preset.steps.size() > MaximumStepsPerPreset)
	{
		if (error) *error = "Prefab, unnamed, or oversized demo presets cannot be saved as user presets.";
		return false;
	}
	if (demoPrefabPreset(preset.id))
	{
		if (error) *error = "Prefab presets are immutable; save the edited copy with a user preset ID.";
		return false;
	}
	const std::vector<DemoPresetIssue> issues = validateDemoPreset(preset);
	for (std::size_t i = 0; i < issues.size(); ++i)
	{
		// Missing algorithms are retained intentionally. The editor can show the
		// unavailable stable ID and let the owner replace or remove that step.
		if (issues[i].message.find("Algorithm unavailable:") == 0) continue;
		if (error) *error = issues[i].message;
		return false;
	}
	for (std::size_t i = 0; i < userPresets_.size(); ++i)
		if (userPresets_[i].id == preset.id)
		{
			userPresets_[i] = preset;
			if (error) error->clear();
			return true;
		}
	if (userPresets_.size() >= MaximumPresets)
	{
		if (error) *error = "The user preset limit has been reached.";
		return false;
	}
	userPresets_.push_back(preset);
	if (error) error->clear();
	return true;
}

bool DemoPresetStore::renameUserPreset(const std::string& id, const std::string& name, std::string* error)
{
	if (name.empty() || demoPrefabPreset(id))
	{
		if (error) *error = "Prefab presets cannot be renamed and user names cannot be empty.";
		return false;
	}
	for (std::size_t i = 0; i < userPresets_.size(); ++i)
		if (userPresets_[i].id == id)
		{
			userPresets_[i].name = name;
			if (error) error->clear();
			return true;
		}
	if (error) *error = "The user preset was not found.";
	return false;
}

bool DemoPresetStore::deleteUserPreset(const std::string& id)
{
	if (demoPrefabPreset(id)) return false;
	for (std::size_t i = 0; i < userPresets_.size(); ++i)
		if (userPresets_[i].id == id)
		{
			userPresets_.erase(userPresets_.begin() + static_cast<std::ptrdiff_t>(i));
			return true;
		}
	return false;
}

DemoPreset DemoPresetStore::clonePrefabAsUser(const std::string& prefabId, const std::string& id, const std::string& name) const
{
	const DemoPreset* prefab = demoPrefabPreset(prefabId);
	if (!prefab) return DemoPreset();
	DemoPreset copy = *prefab;
	copy.id = id;
	copy.name = name;
	copy.prefab = false;
	return copy;
}

std::string DemoPresetStore::encode(const std::vector<DemoPreset>& presets)
{
	std::ostringstream output;
	output.imbue(std::locale::classic());
	output << "version=" << CurrentVersion << '\n';
	std::size_t saved = 0;
	for (std::size_t i = 0; i < presets.size() && saved < MaximumPresets; ++i)
	{
		const DemoPreset& preset = presets[i];
		if (preset.prefab || preset.id.empty() || preset.name.empty() || preset.steps.size() > MaximumStepsPerPreset) continue;
		output << "P\t" << escapeField(preset.id) << '\t' << escapeField(preset.name) << '\t'
			<< std::setprecision(9) << preset.intermissionSeconds << '\t' << (preset.loop ? 1 : 0) << '\n';
		for (std::map<sortinglogic::DemoComplexityClass, DemoInputRecipe>::const_iterator recipe = preset.classInputs.begin();
			recipe != preset.classInputs.end(); ++recipe)
			output << "R\t" << escapeField(preset.id) << '\t' << static_cast<int>(recipe->first) << '\t'
				<< recipe->second.seed << '\t' << recipe->second.itemCount << '\t' << static_cast<int>(recipe->second.domain)
				<< '\t' << static_cast<int>(recipe->second.pattern) << '\n';
		for (std::size_t stepIndex = 0; stepIndex < preset.steps.size(); ++stepIndex)
		{
			const DemoStep& step = preset.steps[stepIndex];
			const DemoInputRecipe& overrideRecipe = step.inputOverride;
			output << "S\t" << escapeField(preset.id) << '\t' << stepIndex << '\t' << escapeField(step.familyId) << '\t'
				<< escapeField(step.variantId) << '\t' << static_cast<int>(step.direction) << '\t'
				<< static_cast<int>(step.timingMode) << '\t' << std::setprecision(9) << step.fixedDurationSeconds << '\t'
				<< (step.hasInputOverride ? 1 : 0) << '\t' << overrideRecipe.seed << '\t' << overrideRecipe.itemCount << '\t'
				<< static_cast<int>(overrideRecipe.domain) << '\t' << static_cast<int>(overrideRecipe.pattern) << '\t'
				<< escapeField(encodeOptions(step.optionSelections)) << '\n';
		}
		++saved;
	}
	return output.str();
}

bool DemoPresetStore::decode(const std::string& content, std::vector<DemoPreset>& presets,
	std::vector<std::string>* recoveredIssues, std::string* error)
{
	presets.clear();
	if (recoveredIssues) recoveredIssues->clear();
	std::istringstream lines(content);
	std::string line;
	bool versionSeen = false;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.empty()) continue;
		if (line.compare(0, 8, "version=") == 0)
		{
			if (versionSeen || line != "version=1")
			{
				if (error) *error = "The demo preset file version is unsupported or repeated.";
				presets.clear();
				return false;
			}
			versionSeen = true;
			continue;
		}
		if (!versionSeen)
		{
			if (recoveredIssues) recoveredIssues->push_back("Ignored demo record before version header.");
			continue;
		}
		std::vector<std::string> fields = splitTabs(line);
		for (std::size_t i = 0; i < fields.size(); ++i)
		{
			std::string decoded;
			if (!unescapeField(fields[i], decoded)) { decoded.clear(); fields.clear(); break; }
			fields[i] = decoded;
		}
		if (fields.empty())
		{
			if (recoveredIssues) recoveredIssues->push_back("Skipped a malformed demo record.");
			continue;
		}
		if (fields[0] == "P" && fields.size() == 5)
		{
			float intermission = 0.0f;
			std::uint64_t loopValue = 0;
			if (fields[1].empty() || fields[2].empty() || !parseFloat(fields[3], intermission) || intermission < 0.0f ||
				intermission > 120.0f || !parseUnsigned(fields[4], 1, loopValue) || demoPrefabPreset(fields[1]))
			{
				if (recoveredIssues) recoveredIssues->push_back("Skipped an invalid or protected demo preset record.");
				continue;
			}
			bool duplicate = false;
			for (std::size_t i = 0; i < presets.size(); ++i) duplicate = duplicate || presets[i].id == fields[1];
			if (duplicate || presets.size() >= MaximumPresets)
			{
				if (recoveredIssues) recoveredIssues->push_back("Skipped a duplicate or excess user preset.");
				continue;
			}
			DemoPreset preset;
			preset.id = fields[1]; preset.name = fields[2]; preset.prefab = false;
			preset.intermissionSeconds = intermission; preset.loop = loopValue != 0;
			presets.push_back(preset);
			continue;
		}
		if (fields.size() > 1)
		{
			std::vector<DemoPreset>::iterator preset = std::find_if(presets.begin(), presets.end(), [&](const DemoPreset& value)
				{ return value.id == fields[1]; });
			if (preset == presets.end())
			{
				if (recoveredIssues) recoveredIssues->push_back("Skipped a demo child record whose preset could not be loaded.");
				continue;
			}
			if (fields[0] == "R" && fields.size() == 7)
			{
				int complexity = 0, domain = 0, pattern = 0;
				std::uint64_t seed = 0, count = 0;
				if (!parseIntEnum(fields[2], 0, static_cast<int>(sortinglogic::DemoComplexityClass::DemonstrationUnknown), complexity) ||
					!parseUnsigned(fields[3], (std::numeric_limits<std::uint32_t>::max)(), seed) ||
					!parseUnsigned(fields[4], sortinglogic::MaxItemCount, count) ||
					!parseIntEnum(fields[5], static_cast<int>(sortinglogic::InputDomain::NormalizedFloats),
						static_cast<int>(sortinglogic::InputDomain::IntegerPermutation), domain) ||
					!parseIntEnum(fields[6], static_cast<int>(sortinglogic::InputPattern::Random),
						static_cast<int>(sortinglogic::InputPattern::AndrutReference), pattern))
				{
					if (recoveredIssues) recoveredIssues->push_back("Skipped an invalid demo input-recipe record.");
					continue;
				}
				preset->classInputs[static_cast<sortinglogic::DemoComplexityClass>(complexity)] = DemoInputRecipe(
					static_cast<std::uint32_t>(seed), static_cast<std::size_t>(count),
					static_cast<sortinglogic::InputDomain>(domain), static_cast<sortinglogic::InputPattern>(pattern));
				continue;
			}
			if (fields[0] == "S" && fields.size() == 14)
			{
				std::uint64_t index = 0, overrideSeed = 0, overrideCount = 0, overrideFlag = 0;
				int direction = 0, timing = 0, domain = 0, pattern = 0;
				float duration = 0.0f;
				DemoStep step;
				if (!parseUnsigned(fields[2], MaximumStepsPerPreset - 1, index) || fields[3].empty() || fields[4].empty() ||
					!parseIntEnum(fields[5], 0, 1, direction) || !parseIntEnum(fields[6], 0, 1, timing) ||
					!parseFloat(fields[7], duration) || !parseUnsigned(fields[8], 1, overrideFlag) ||
					!parseUnsigned(fields[9], (std::numeric_limits<std::uint32_t>::max)(), overrideSeed) ||
					!parseUnsigned(fields[10], sortinglogic::MaxItemCount, overrideCount) ||
					!parseIntEnum(fields[11], static_cast<int>(sortinglogic::InputDomain::NormalizedFloats),
						static_cast<int>(sortinglogic::InputDomain::IntegerPermutation), domain) ||
					!parseIntEnum(fields[12], static_cast<int>(sortinglogic::InputPattern::Random),
						static_cast<int>(sortinglogic::InputPattern::AndrutReference), pattern) ||
					!decodeOptions(fields[13], step.optionSelections))
				{
					if (recoveredIssues) recoveredIssues->push_back("Skipped an invalid demo step record.");
					continue;
				}
				step.familyId = fields[3]; step.variantId = fields[4];
				step.direction = static_cast<sortinglogic::SortDirection>(direction);
				step.timingMode = static_cast<SoundEngine::PlaybackTimingMode>(timing);
				step.fixedDurationSeconds = duration;
				step.hasInputOverride = overrideFlag != 0;
				step.inputOverride = DemoInputRecipe(static_cast<std::uint32_t>(overrideSeed),
					static_cast<std::size_t>(overrideCount), static_cast<sortinglogic::InputDomain>(domain),
					static_cast<sortinglogic::InputPattern>(pattern));
				if (index > preset->steps.size())
				{
					if (recoveredIssues) recoveredIssues->push_back("Skipped a non-contiguous demo step index.");
					continue;
				}
				if (index == preset->steps.size()) preset->steps.push_back(step);
				else preset->steps[static_cast<std::size_t>(index)] = step;
				continue;
			}
		}
		if (recoveredIssues) recoveredIssues->push_back("Skipped an unrecognized demo record.");
	}
	if (!versionSeen)
	{
		if (error) *error = "The demo preset file has no supported version header.";
		return false;
	}
	if (error) error->clear();
	return true;
}
