#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"
#include "../SortingLogic/TracePresentationPolicy.h"
#include "SortingLogicTestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sortinglogic_tests
{
	int failures = 0;
	std::size_t checks = 0;

	void check(bool condition, const std::string& message)
	{
		++checks;
		if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
	}

	std::vector<std::string> parseCsvRecord(const std::string& line)
	{
		std::vector<std::string> fields;
		std::string field;
		bool quoted = false;
		for (std::size_t i = 0; i < line.size(); ++i)
		{
			const char character = line[i];
			if (quoted)
			{
				if (character == '"' && i + 1 < line.size() && line[i + 1] == '"')
				{
					field += '"';
					++i;
				}
				else if (character == '"') quoted = false;
				else field += character;
			}
			else if (character == '"') quoted = true;
			else if (character == ',')
			{
				fields.push_back(field);
				field.clear();
			}
			else field += character;
		}
		fields.push_back(field);
		return fields;
	}

	void testPinnedCatalogCoverage()
	{
		using namespace sortinglogic;
		std::ifstream manifest("arrayv-catalog-coverage.csv");
		if (!manifest) manifest.open("agent-memory/arrayv-catalog-coverage.csv");
		if (!manifest) throw std::runtime_error("could not open arrayv-catalog-coverage.csv from the test output or repository root");

		std::string line;
		if (!std::getline(manifest, line)) throw std::runtime_error("ArrayV coverage manifest has no header");
		const std::vector<std::string> header = parseCsvRecord(line);
		const auto column = [&header](const std::string& name) -> std::size_t
		{
			for (std::size_t i = 0; i < header.size(); ++i) if (header[i] == name) return i;
			throw std::runtime_error("ArrayV coverage manifest is missing column " + name);
		};
		const std::size_t sourceColumn = column("source_class");
		const std::size_t familyColumn = column("family_id");
		const std::size_t variantColumn = column("variant_id");
		const std::size_t behaviorColumn = column("behavior_contract");
		const std::size_t optionsColumn = column("required_options");
		const std::size_t verificationColumn = column("verification_reference");
		const std::size_t statusColumn = column("status");

		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		std::set<std::string> catalogStableIds;
		std::set<std::string> catalogVariantIds;
		for (std::size_t i = 0; i < catalog.size(); ++i)
		{
			check(catalogStableIds.insert(catalog[i].stableId ? catalog[i].stableId : "").second,
				"catalog stable IDs are unique");
			check(catalogVariantIds.insert(catalog[i].familyId + "\n" + catalog[i].variantId).second,
				"catalog family and variant pairs are unique");
			std::map<std::string, std::size_t> defaultCountByGroup;
			std::map<std::string, AlgorithmOptionTarget> targetByGroup;
			std::set<std::string> optionIds;
			for (std::size_t option = 0; option < catalog[i].supportedOptions.size(); ++option)
			{
				const AlgorithmOptionDescriptor& descriptor = catalog[i].supportedOptions[option];
				const std::string group(descriptor.groupId);
				check(descriptor.target != AlgorithmOptionTarget::Unbound,
					"catalog options declare a trace-configuration target: " + catalog[i].variantId + "/" + group);
				check(optionIds.insert(group + "\n" + descriptor.stableId).second,
					"stable option IDs are unique within each catalog group");
				const std::pair<std::map<std::string, AlgorithmOptionTarget>::iterator, bool> targetInsertion =
					targetByGroup.insert(std::make_pair(group, descriptor.target));
				if (!targetInsertion.second)
					check(targetInsertion.first->second == descriptor.target,
						"every choice in a catalog option group uses the same configuration target");
				if (descriptor.isDefault) ++defaultCountByGroup[group];
				else defaultCountByGroup.insert(std::make_pair(group, 0));
			}
			for (std::map<std::string, std::size_t>::const_iterator group = defaultCountByGroup.begin();
				group != defaultCountByGroup.end(); ++group)
				check(group->second == 1, "each catalog option group declares exactly one default: " + catalog[i].variantId + "/" + group->first);
		}

		std::set<std::string> sourceClasses;
		std::set<std::string> coveredVariants;
		std::size_t rows = 0;
		std::size_t documentedAliases = 0;
		while (std::getline(manifest, line))
		{
			if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
			const std::vector<std::string> fields = parseCsvRecord(line);
			if (fields.size() != header.size())
			{
				check(false, "ArrayV coverage row has the expected number of columns");
				continue;
			}
			++rows;
			const std::string& source = fields[sourceColumn];
			const std::string& family = fields[familyColumn];
			const std::string& variant = fields[variantColumn];
			check(!source.empty() && sourceClasses.insert(source).second,
				"each pinned ArrayV source class appears exactly once in the manifest");
			check(fields[statusColumn] == "implemented_and_tested",
				"each pinned ArrayV source row is implemented and tested");
			check(!fields[behaviorColumn].empty() &&
				fields[behaviorColumn].find("Not yet reviewed") == std::string::npos,
				"each pinned source row has an audited behavior contract: " + source);
			check(!fields[optionsColumn].empty() &&
				fields[optionsColumn].find("To be audited") == std::string::npos,
				"each pinned source row declares its supported options: " + source);
			check(!fields[verificationColumn].empty(),
				"each pinned source row names its verification evidence: " + source);
			const AlgorithmDescriptor* descriptor = algorithmDescriptorById(family, variant);
			check(descriptor != nullptr,
				"manifest source " + source + " resolves to a registered family/variant ID");
			if (source == "GrailSort")
			{
				const bool optionsMatch = descriptor && descriptor->supportedOptions.size() == 3 &&
					descriptor->supportedOptions[0].value == 0 && std::string(descriptor->supportedOptions[0].stableId) == "in_place" &&
					descriptor->supportedOptions[1].value == 1 && std::string(descriptor->supportedOptions[1].stableId) == "static_buffer" &&
					descriptor->supportedOptions[2].value == 2 && std::string(descriptor->supportedOptions[2].stableId) == "dynamic_buffer";
				check(optionsMatch && fields[optionsColumn].find("grail_buffer_mode=") == 0,
					"Grail manifest options match the registered stable buffer-mode IDs");
			}
			else if (source == "OptimizedRotateMergeSort")
			{
				const int expectedCapacities[] = { 16, 32, 64, 128, 256 };
				bool optionsMatch = descriptor && descriptor->supportedOptions.size() == 5;
				for (std::size_t option = 0; optionsMatch && option < 5; ++option)
					optionsMatch = descriptor->supportedOptions[option].value == expectedCapacities[option];
				check(optionsMatch && fields[optionsColumn].find("rotate_buffer_capacity=") == 0 &&
					fields[optionsColumn].find("64 (default)") != std::string::npos,
					"Optimized Rotate Merge manifest capacities match the registered buffer options and default");
			}
			const bool firstMapping = coveredVariants.insert(family + "\n" + variant).second;
			if (!firstMapping)
			{
				const bool documentedAlias = fields[behaviorColumn].find("alias") != std::string::npos ||
					fields[optionsColumn].find("Alias") != std::string::npos;
				check(documentedAlias && !fields[verificationColumn].empty(),
					"a shared family/variant mapping must document its option-level alias and verification");
				if (documentedAlias) ++documentedAliases;
			}
		}
		check(rows == 208, "the pinned ArrayV coverage manifest contains all 208 selectable source rows");
		check(coveredVariants.size() + documentedAliases == 208,
			"all 208 manifest rows resolve to registered variants or documented option-level aliases");
		check(documentedAliases == 1, "the Merge Sort source wrapper is the only documented option-level alias");
		check(catalog.size() >= 211, "the catalog contains every upstream mapping plus existing and canonical companions");
		check(algorithmDescriptorById("partition-sorts", "quick") != nullptr &&
			algorithmDescriptorById("distribution-sorts", "radix") != nullptr &&
			algorithmDescriptorById("novelty-sorts", "stalin-discard") != nullptr &&
			algorithmDescriptorById("pancake-sorts", "burnt-canonical") != nullptr,
			"the existing Quick/Radix entries and canonical Stalin/Burnt Pancake companions remain registered");
	}

	bool sameItem(const sortinglogic::SortItem& a, const sortinglogic::SortItem& b)
	{
		return a.stableId == b.stableId && a.value == b.value && a.integerKey == b.integerKey && a.hasIntegerKey == b.hasIntegerKey;
	}

	bool sameMetrics(const sortinglogic::SortMetrics& a, const sortinglogic::SortMetrics& b)
	{
		return a.comparisons == b.comparisons && a.swaps == b.swaps && a.primaryWrites == b.primaryWrites &&
			a.auxiliaryItemWrites == b.auxiliaryItemWrites && a.auxiliaryCounterUpdates == b.auxiliaryCounterUpdates &&
			a.discards == b.discards && a.peakAuxiliaryCapacity == b.peakAuxiliaryCapacity && a.algorithm == b.algorithm;
	}

	bool sameSlots(const std::vector<sortinglogic::SortSlot>& a, const std::vector<sortinglogic::SortSlot>& b)
	{
		if (a.size() != b.size()) return false;
		for (std::size_t i = 0; i < a.size(); ++i)
			if (a[i].occupied != b[i].occupied || (a[i].occupied && !sameItem(a[i].item, b[i].item))) return false;
		return true;
	}

	bool sameState(const sortinglogic::SortState& a, const sortinglogic::SortState& b)
	{
		if (!sameSlots(a.slots, b.slots) || a.discardedItems.size() != b.discardedItems.size() ||
			a.hasHeldItem != b.hasHeldItem || a.gapIndex != b.gapIndex || !sameMetrics(a.metrics, b.metrics)) return false;
		for (std::size_t i = 0; i < a.discardedItems.size(); ++i)
			if (!sameItem(a.discardedItems[i], b.discardedItems[i])) return false;
		if (a.hasHeldItem && !sameItem(a.heldItem, b.heldItem)) return false;
		if (a.auxiliaryItems.size() != b.auxiliaryItems.size() || a.integerWorkspaces != b.integerWorkspaces) return false;
		for (std::map<std::string, std::vector<sortinglogic::SortSlot> >::const_iterator lane = a.auxiliaryItems.begin(); lane != a.auxiliaryItems.end(); ++lane)
		{
			std::map<std::string, std::vector<sortinglogic::SortSlot> >::const_iterator other = b.auxiliaryItems.find(lane->first);
			if (other == b.auxiliaryItems.end() || !sameSlots(lane->second, other->second)) return false;
		}
		return true;
	}

	std::vector<sortinglogic::SortItem> floats(const std::vector<float>& values)
	{
		std::vector<sortinglogic::SortItem> result;
		for (std::size_t i = 0; i < values.size(); ++i) result.push_back(sortinglogic::SortItem(100 + i, values[i]));
		return result;
	}

	std::vector<sortinglogic::SortItem> integers(const std::vector<int>& values)
	{
		std::vector<sortinglogic::SortItem> result;
		const float denominator = values.empty() ? 1.0f : static_cast<float>(values.size());
		for (std::size_t i = 0; i < values.size(); ++i)
			result.push_back(sortinglogic::SortItem(200 + i, values[i], static_cast<float>(values[i] + 1) / denominator));
		return result;
	}

	bool isSorted(const sortinglogic::SortState& state, sortinglogic::SortDirection direction)
	{
		if (state.hasHeldItem) return false;
		for (std::size_t i = 0; i < state.slots.size(); ++i)
			if (!state.slots[i].occupied) return false;
		for (std::size_t i = 1; i < state.slots.size(); ++i)
			if (sortinglogic::compareItems(state.slots[i - 1].item, state.slots[i].item, direction) == sortinglogic::ComparisonResult::Greater) return false;
		return true;
	}

	void verifyAlgorithmCase(const std::string& label, const std::vector<sortinglogic::SortItem>& input,
		sortinglogic::InputProfile profile, sortinglogic::SortAlgorithm algorithm, sortinglogic::SortDirection direction,
		sortinglogic::InputDomain domain,
		sortinglogic::MergeWritebackMode mergeMode,
		sortinglogic::MergeExecutionMode mergeExecution)
	{
		using namespace sortinglogic;
		if (!algorithmAcceptsItemCount(algorithm, input.size()))
		{
			check(true, label + " is skipped above the catalogued runtime ceiling");
			return;
		}
		TraceBuildRequest request;
		request.input = input; request.profile = profile; request.domain = domain; request.algorithm = algorithm; request.direction = direction;
		request.inputSeed = 0xA551u; request.shellGapSequence = ShellGapSequence::Tokuda; request.mergeWritebackMode = mergeMode;
		request.mergeExecutionMode = mergeExecution;
		SortTrace trace;
		try { trace = SortTraceBuilder::build(request); }
		catch (const std::exception& e) { check(false, label + " failed to build: " + e.what()); return; }
		check(trace.algorithm == algorithm && trace.direction == direction, label + " stores algorithm and direction metadata");
		check(trace.initialState.slots.size() == input.size(), label + " stores a replay baseline");
		check(trace.snapshots.find(0) != trace.snapshots.end(), label + " contains cursor-zero snapshot");
		std::map<std::uint64_t, SortItem> byId;
		std::map<std::uint64_t, std::size_t> originalPosition;
		for (std::size_t i = 0; i < input.size(); ++i) { byId[input[i].stableId] = input[i]; originalPosition[input[i].stableId] = i; }
		const bool exhaustiveCursorChecks = input.size() <= 64 && trace.events.size() <= 8192;
		const std::size_t cursorSampleStride = exhaustiveCursorChecks ? 1 :
			std::max<std::size_t>(1, (trace.events.size() + 63) / 64);
		std::map<std::size_t, SortState> cursorCheckpoints;
		SortState state = trace.initialState;
		cursorCheckpoints.insert(std::make_pair(0, state));
		std::uint64_t observedComparisons = 0;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const SemanticEvent& event = trace.events[i];
			check(event.sequence == i, label + " preserves ordered event sequence");
			if (event.type == SemanticEventType::Compare)
			{
				++observedComparisons;
				const std::map<std::uint64_t, SortItem>::const_iterator a = byId.find(event.firstItemId), b = byId.find(event.secondItemId);
				check(a != byId.end() && b != byId.end(), label + " comparisons reference known logical items");
				if (a != byId.end() && b != byId.end())
				{
					SortItem first = a->second, second = b->second;
					if (trace.outcome == AlgorithmOutcome::SortedValueRewrite)
						for (std::size_t item = 0; item < state.slots.size(); ++item)
							if (state.slots[item].occupied)
							{
								if (state.slots[item].item.stableId == event.firstItemId) first = state.slots[item].item;
								if (state.slots[item].item.stableId == event.secondItemId) second = state.slots[item].item;
							}
					check(event.comparison == compareItems(first, second, direction, event.comparisonBasis),
						label + " stores exact comparator outcome for its declared comparison basis");
				}
			}
			try { applyEventForward(state, event); }
			catch (const std::exception& e) { check(false, label + " replay failed at " + std::to_string(i) + ": " + e.what()); return; }
			const std::size_t cursor = i + 1;
			const bool sampleCursor = exhaustiveCursorChecks || cursor == trace.events.size() ||
				cursor % cursorSampleStride == 0 || cursor % 4096 == 0;
			if (sampleCursor)
			{
				std::string reason;
				const bool stateValid = validateState(state, input.size(), &reason);
				check(stateValid, label + " preserves unique primary identity: " + reason);
				if (!stateValid)
				{
					std::cerr << "First invalid cursor for " << label << ": event " << i << ", type " << static_cast<int>(event.type)
						<< ", workspace '" << event.workspaceName << "', indices " << event.firstIndex << '/' << event.secondIndex
						<< ", item IDs " << event.firstItemId << '/' << event.secondItemId << ", reason: " << reason << "\n";
					return;
				}
				cursorCheckpoints[cursor] = state;
			}
			check(state.metrics.comparisons == observedComparisons, label + " cursor metrics track comparison events");
		}
		if (exhaustiveCursorChecks)
			check(cursorCheckpoints.size() == trace.events.size() + 1, label + " retains every small-trace cursor");
		else
			check(cursorCheckpoints.size() <= trace.snapshots.size() + 66, label + " bounds checkpoints for larger traces");
		if (trace.outcome == AlgorithmOutcome::IncompleteDemonstration)
			check(true, label + " retains its declared incomplete-demonstration outcome");
		else
			check(isSorted(state, direction), label + " ends in the selected order");
		std::map<std::uint64_t, bool> seen;
		for (std::size_t i = 0; i < state.slots.size(); ++i) seen[state.slots[i].item.stableId] = true;
		if (trace.outcome == AlgorithmOutcome::SortedWithDiscards)
			for (std::size_t i = 0; i < state.discardedItems.size(); ++i)
				seen[state.discardedItems[i].stableId] = true;
		check(seen.size() == input.size(), label + " preserves every primary item exactly once");
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (descriptor && input.size() >= 2)
			for (std::size_t metric = 0; metric < descriptor->metrics.size(); ++metric)
				check(state.metrics.algorithm.find(descriptor->metrics[metric].key) != state.metrics.algorithm.end(),
					label + " records catalog metric " + descriptor->metrics[metric].displayName);
		if (descriptor && descriptor->stable)
			for (std::size_t i = 1; i < state.slots.size(); ++i)
				if (compareItems(state.slots[i - 1].item, state.slots[i].item, direction) == ComparisonResult::Equal)
					check(originalPosition[state.slots[i - 1].item.stableId] < originalPosition[state.slots[i].item.stableId],
						label + " preserves stability at output IDs " + std::to_string(state.slots[i - 1].item.stableId) + "/" +
						std::to_string(state.slots[i].item.stableId));
		for (std::map<std::size_t, SortState>::const_iterator snapshot = trace.snapshots.begin(); snapshot != trace.snapshots.end(); ++snapshot)
		{
			const std::map<std::size_t, SortState>::const_iterator checkpoint = cursorCheckpoints.find(snapshot->first);
			check(checkpoint != cursorCheckpoints.end(), label + " validates every stored snapshot cursor");
			if (checkpoint != cursorCheckpoints.end())
				check(sameState(snapshot->second, checkpoint->second), label + " stored snapshot matches its cursor");
		}
		SortState reverse = state;
		for (std::size_t i = trace.events.size(); i > 0; --i)
		{
			try { applyEventBackward(reverse, trace.events[i - 1]); }
			catch (const std::exception& e) { check(false, label + " inverse replay failed: " + e.what()); return; }
			const std::map<std::size_t, SortState>::const_iterator checkpoint = cursorCheckpoints.find(i - 1);
			if (checkpoint != cursorCheckpoints.end())
				check(sameState(reverse, checkpoint->second), label + " inverse mutation reconstructs cursor " + std::to_string(i - 1));
		}
		check(sameState(reverse, trace.initialState), label + " rewinds to exact initial workspace");
	}

	void testSimulationAndProfiles()
	{
		using namespace sortinglogic;
		Simulation integerA(40, 0x12345678u, InputProfile::IntegerKeys), integerB(40, 0x12345678u, InputProfile::IntegerKeys);
		check(integerA.preparedInput().size() == integerB.preparedInput().size(), "integer profile uses requested item count");
		bool sameSeed = true;
		std::set<int> generatedKeys;
		bool hasDuplicate = false;
		for (std::size_t i = 0; i < integerA.preparedInput().size(); ++i)
		{
			const SortItem& a = integerA.preparedInput()[i]; const SortItem& b = integerB.preparedInput()[i];
			sameSeed = sameSeed && a.stableId == b.stableId && a.integerKey == b.integerKey;
			check(a.hasIntegerKey && a.integerKey >= 0 && a.integerKey < 40, "generated integer key lies in [0,N-1]");
			if (!generatedKeys.insert(a.integerKey).second) hasDuplicate = true;
		}
		check(sameSeed, "integer-key input generation is deterministic for a seed");
		check(hasDuplicate, "integer profile samples with replacement and can generate duplicates");
		Simulation shuffledA(20, 1), shuffledB(20, 2);
		shuffledA.prepareShuffle(0x55AAu); shuffledB.prepareShuffle(0x55AAu);
		bool sameShuffle = true;
		for (std::size_t i = 0; i < shuffledA.preparedInput().size(); ++i)
			sameShuffle = sameShuffle && shuffledA.preparedInput()[i].stableId == shuffledB.preparedInput()[i].stableId;
		check(sameShuffle, "explicit shuffle seeds reproduce the same input permutation");
		integerA.prepareInstaSort();
		bool ascendingPreset = true;
		for (std::size_t i = 1; i < integerA.preparedInput().size(); ++i)
			ascendingPreset = ascendingPreset && integerA.preparedInput()[i - 1].integerKey <= integerA.preparedInput()[i].integerKey;
		check(ascendingPreset, "Insta-sort creates an ascending integer-key baseline");
		integerA.prepareInstaInvert();
		bool descendingPreset = true;
		for (std::size_t i = 1; i < integerA.preparedInput().size(); ++i)
			descendingPreset = descendingPreset && integerA.preparedInput()[i - 1].integerKey >= integerA.preparedInput()[i].integerKey;
		check(descendingPreset, "Insta-invert creates a descending integer-key baseline");
		check(integerA.selectAlgorithm(SortAlgorithm::Counting).accepted, "Counting Sort can be selected for integer keys");
		const std::vector<SortItem> prepared = integerA.preparedInput();
		check(integerA.selectAlgorithm(SortAlgorithm::Radix).accepted, "algorithm selection changes without regenerating input");
		bool preservedInput = prepared.size() == integerA.preparedInput().size();
		for (std::size_t i = 0; preservedInput && i < prepared.size(); ++i)
			preservedInput = sameItem(prepared[i], integerA.preparedInput()[i]);
		check(preservedInput, "switching algorithms preserves the prepared data and seed");
		check(integerA.setInputProfile(InputProfile::NormalizedFloat).accepted, "profile change is accepted when idle");
		check(integerA.algorithm() == SortAlgorithm::Radix && !integerA.snapshot().runCompatible,
			"profile change preserves an incompatible algorithm selection and disables Run");
		TraceBuildRequest request;
		check(!integerA.beginTraceBuild(request).accepted, "incompatible Run is rejected with visible status");
		check(integerA.snapshot().status.find("does not support Normalized floats") != std::string::npos, "incompatibility explanation is visible");
		check(integerA.selectAlgorithm(SortAlgorithm::Insertion).accepted, "a compatible algorithm restores Run");
		check(integerA.snapshot().runCompatible, "compatible algorithm reports Run enabled");
		Simulation novelty(80, 0xA001u);
		check(novelty.selectAlgorithm(SortAlgorithm::Bogo).accepted && !novelty.snapshot().runCompatible,
			"stochastic novelty algorithm stays selectable while its item ceiling disables Run");
		TraceBuildRequest noveltyRequest;
		check(!novelty.beginTraceBuild(noveltyRequest).accepted && novelty.snapshot().status.find("safe limit of 8") != std::string::npos,
			"over-limit Run explains the stochastic algorithm safety ceiling");
		check(novelty.setItemCount(8).accepted && novelty.snapshot().runCompatible,
			"reducing the input to the catalogued ceiling re-enables the novelty algorithm");
		Simulation configured(12, 0xC0A1u);
		check(configured.selectAlgorithm(SortAlgorithm::Shell).accepted &&
			configured.setShellGapSequence(ShellGapSequence::Ciura).accepted,
			"simulation accepts a Shell option through its configuration owner");
		TraceBuildRequest shellRequest;
		check(configured.beginTraceBuild(shellRequest).accepted, "configured Shell run starts from simulation state");
		TraceBuildResult shellResult;
		try { shellResult.trace = SortTraceBuilder::build(shellRequest); shellResult.succeeded = true; }
		catch (const std::exception& e) { check(false, std::string("configured Shell trace builds: ") + e.what()); }
		configured.completeTraceBuild(shellResult);
		const std::shared_ptr<const SortTrace> configuredShellTrace = configured.currentTrace();
		check(configuredShellTrace && configuredShellTrace->configuration.shellGapSequence == ShellGapSequence::Ciura,
			"simulation's named algorithm configuration reaches both typed trace state and its compatibility field");
		check(configured.selectAlgorithm(SortAlgorithm::ArrayvGrail).accepted &&
			configured.setAlgorithmOption("grail_buffer_mode", "static_buffer").accepted,
			"simulation applies a stable catalog option through its owned typed configuration");
		TraceBuildRequest grailRequest;
		check(configured.beginTraceBuild(grailRequest).accepted && grailRequest.hasExplicitConfiguration,
			"simulation publishes its typed configuration as part of the immutable trace request");
		TraceBuildResult grailResult;
		try { grailResult.trace = SortTraceBuilder::build(grailRequest); grailResult.succeeded = true; }
		catch (const std::exception& e) { check(false, std::string("configured Grail trace builds from Simulation request: ") + e.what()); }
		configured.completeTraceBuild(grailResult);
		const std::shared_ptr<const SortTrace> configuredGrailTrace = configured.currentTrace();
		check(configuredGrailTrace && configuredGrailTrace->configuration.grailBufferMode == GrailBufferMode::StaticBuffer &&
			configuredGrailTrace->configuration.grailBufferMode == GrailBufferMode::StaticBuffer,
			"Simulation request preserves typed Grail mode and its legacy trace projection without controller patch-up");
		if (configuredGrailTrace)
		{
			SortState grailReplay = configuredGrailTrace->initialState;
			for (std::size_t i = 0; i < configuredGrailTrace->events.size(); ++i)
				applyEventForward(grailReplay, configuredGrailTrace->events[i]);
			check(configuredGrailTrace->complete && isSorted(grailReplay, SortDirection::Ascending),
				"Simulation-produced non-default Grail configuration retains its normal sorted result");
		}
		check(configured.selectAlgorithm(SortAlgorithm::Merge).accepted &&
			configured.setMergeExecutionMode(MergeExecutionMode::TopDown).accepted &&
			configured.setMergeWritebackMode(MergeWritebackMode::AnimatedWrites).accepted,
			"simulation accepts independent Merge choices through the same configuration owner");
		TraceBuildRequest mergeRequest;
		check(configured.beginTraceBuild(mergeRequest).accepted, "configured Merge run starts from simulation state");
		TraceBuildResult mergeResult;
		try { mergeResult.trace = SortTraceBuilder::build(mergeRequest); mergeResult.succeeded = true; }
		catch (const std::exception& e) { check(false, std::string("configured Merge trace builds: ") + e.what()); }
		configured.completeTraceBuild(mergeResult);
		const std::shared_ptr<const SortTrace> configuredMergeTrace = configured.currentTrace();
		check(configuredMergeTrace && configuredMergeTrace->configuration.mergeExecutionMode == MergeExecutionMode::TopDown &&
			configuredMergeTrace->configuration.mergeWritebackMode == MergeWritebackMode::AnimatedWrites,
			"simulation configuration preserves independent typed Merge choices in the built trace");
		for (ShellGapSequence sequence = ShellGapSequence::Halving; sequence <= ShellGapSequence::Tokuda;
			sequence = static_cast<ShellGapSequence>(static_cast<int>(sequence) + 1))
		{
			const std::vector<std::size_t> gaps = shellGaps(800, sequence);
			check(!gaps.empty() && gaps.back() == 1, std::string(shellGapSequenceName(sequence)) + " gaps terminate at one");
			for (std::size_t i = 1; i < gaps.size(); ++i) check(gaps[i - 1] > gaps[i], "Shell gaps are strictly descending");
		}
	}

	void testSnapshotAnnotations()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::Shell, SortAlgorithm::Merge, SortAlgorithm::Counting, SortAlgorithm::Radix };
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			const InputProfile profile = algorithms[a] == SortAlgorithm::Counting || algorithms[a] == SortAlgorithm::Radix
				? InputProfile::IntegerKeys : InputProfile::NormalizedFloat;
			Simulation simulation(8, static_cast<std::uint32_t>(0x1234u + a), profile);
			check(simulation.selectAlgorithm(algorithms[a]).accepted, "annotation test selects algorithm");
			TraceBuildRequest request;
			check(simulation.beginTraceBuild(request).accepted, "annotation test starts trace build");
			TraceBuildResult result;
			try { result.trace = SortTraceBuilder::build(request); result.succeeded = true; }
			catch (const std::exception& e) { check(false, std::string("annotation trace builds: ") + e.what()); continue; }
			simulation.completeTraceBuild(result);
			const std::shared_ptr<const SortTrace> trace = simulation.currentTrace();
			check(trace && !trace->annotations.empty(), "workspace algorithm produces visible region annotations");
			if (!trace) continue;
			for (std::size_t region = 0; region < trace->annotations.size(); ++region)
			{
				const RegionAnnotation& annotation = trace->annotations[region];
				check(annotation.beginCursor < annotation.endCursor && annotation.endCursor <= trace->events.size(),
					"region annotation is attached to a valid event interval");
				check(!annotation.label.empty() && !annotation.lane.empty(), "region annotation has a lane and label");
			}
			SortState expected = trace->initialState;
			for (std::size_t cursor = 0; cursor <= trace->events.size(); ++cursor)
			{
				if (cursor > 0) applyEventForward(expected, trace->events[cursor - 1]);
				check(simulation.seekTo(cursor).accepted, "annotation test seeks to every event cursor");
				const RenderSnapshot snapshot = simulation.snapshot();
				check(sameState(snapshot.state, expected), "workspace snapshot matches its exact event cursor");
				std::size_t activeCount = 0;
				for (std::size_t region = 0; region < trace->annotations.size(); ++region)
					if (trace->annotations[region].beginCursor <= cursor && cursor < trace->annotations[region].endCursor)
						++activeCount;
				check(snapshot.activeRegions.size() == activeCount, "snapshot exposes exactly the active region annotations");
				}
		}
	}

	void testTraceServiceAndPlayback()
	{
		using namespace sortinglogic;
		Simulation simulation(18, 991);
		check(!simulation.hasTrace() && simulation.cursor() == 0,
			"narrow trace and cursor queries report the prepared no-trace state");
		TraceBuildService service;
		TraceBuildRequest request;
		check(simulation.beginTraceBuild(request).accepted, "simulation starts trace build");
		check(!simulation.prepareShuffle(22).accepted, "input changes are rejected while building");
		std::string error;
		check(service.submit(request, &error), "worker accepts immutable trace request");
		check(!service.submit(request, &error), "worker rejects duplicate active job");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (simulation.isBuildingTrace() && std::chrono::steady_clock::now() < deadline)
		{
			TraceBuildResult result;
			if (service.poll(result)) simulation.completeTraceBuild(result);
			std::this_thread::yield();
		}
		check(!simulation.isBuildingTrace() && simulation.hasTrace() && simulation.cursor() == 0,
			"worker publishes successful trace to narrow simulation queries");
		check(simulation.snapshot().hasTrace, "render snapshot still reports the published trace");
		const std::shared_ptr<const SortTrace> trace = simulation.currentTrace();
		if (trace)
		{
			std::vector<SortState> states(1, trace->initialState);
			for (std::size_t i = 0; i < trace->events.size(); ++i)
			{
				SortState next = states.back();
				applyEventForward(next, trace->events[i]);
				states.push_back(next);
			}
			for (std::size_t cursor = 0; cursor < states.size(); ++cursor)
			{
				simulation.seekTo(cursor);
				check(simulation.cursor() == cursor && sameState(simulation.snapshot().state, states[cursor]),
					"simulation seek reproduces workspace cursor " + std::to_string(cursor));
			}
			check(simulation.reset().accepted && simulation.cursor() == 0 &&
				sameState(simulation.snapshot().state, trace->initialState), "Reset restores exact prepared input");
			check(simulation.togglePlayback().accepted, "trace playback starts");
			check(!simulation.prepareInstaSort().accepted, "input preparation is rejected while playing");
			simulation.advance(5.0f);
			check(simulation.snapshot().cursor == trace->events.size() / 2, "duration maps linearly to the trace cursor");
			simulation.advance(5.0f);
			check(simulation.snapshot().complete, "playback applies the entire trace");
		}
		TraceBuildRequest invalid;
		invalid.algorithm = static_cast<SortAlgorithm>(9999);
		check(service.submit(invalid, &error), "worker receives unknown algorithm request for error delivery");
		bool delivered = false;
		while (!delivered && std::chrono::steady_clock::now() < deadline)
		{
			TraceBuildResult result;
			delivered = service.poll(result);
			if (delivered) check(!result.succeeded && !result.error.empty(), "builder failure is published as an error result");
			if (!delivered) std::this_thread::yield();
		}
		check(delivered, "worker delivers its failure result");
		TraceBuildRequest capped;
		capped.input = floats(std::vector<float>{ 0.9f, 0.1f, 0.5f });
		capped.maxOperations = 2;
		check(service.submit(capped, &error), "worker accepts a trace request with a local operation budget");
		delivered = false;
		const auto partialDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!delivered && std::chrono::steady_clock::now() < partialDeadline)
		{
			TraceBuildResult result;
			delivered = service.poll(result);
			if (delivered) check(result.succeeded && result.status == TraceBuildStatus::Partial && !result.trace.complete,
				"worker returns a typed playable partial result when the budget is reached");
			if (!delivered) std::this_thread::yield();
		}
		check(delivered, "worker delivers its bounded partial trace");
	}

	void testStressAndValidation()
	{
		using namespace sortinglogic;
		std::vector<SortItem> input;
		for (std::size_t i = 0; i < 800; ++i) input.push_back(SortItem(i, static_cast<float>((i * 137) % 800 + 1) / 800.0f));
		TraceBuildRequest request; request.input = input; request.algorithm = SortAlgorithm::Merge;
		SortTrace trace = SortTraceBuilder::build(request);
		check(trace.events.size() > SnapshotInterval && trace.snapshots.size() > 2, "800-item Merge trace stores periodic snapshots");
		SortState state = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(state, trace.events[i]);
		check(isSorted(state, SortDirection::Ascending), "800-item Merge Sort completes in order");
		bool duplicateIdRejected = false;
		try { SortTraceBuilder::build(std::vector<SortItem>{ SortItem(1, 0.2f), SortItem(1, 0.4f) }, SortDirection::Ascending); }
		catch (const std::invalid_argument&) { duplicateIdRejected = true; }
		check(duplicateIdRejected, "duplicate logical IDs are rejected");
		bool nonFiniteRejected = false;
		try { SortTraceBuilder::build(std::vector<SortItem>{ SortItem(1, std::numeric_limits<float>::infinity()) }, SortDirection::Ascending); }
		catch (const std::invalid_argument&) { nonFiniteRejected = true; }
		check(nonFiniteRejected, "non-finite input values are rejected");
	}

	void testPartialTraceBudget()
	{
		using namespace sortinglogic;
		TraceBuildRequest request;
		request.input = floats(std::vector<float>{ 0.9f, 0.1f, 0.5f, 0.2f });
		request.algorithm = SortAlgorithm::Insertion;
		request.maxOperations = 3;
		const SortTrace trace = SortTraceBuilder::build(request);
		check(!trace.complete && trace.operationCount == 3 && trace.events.size() == 3,
			"operation budget returns a bounded partial trace");
		check(!trace.completionReason.empty(), "partial trace exposes a clear stopping reason");
		check(trace.events.empty() || trace.events.back().type != SemanticEventType::Complete,
			"partial trace does not emit a false completion event");
		std::string reason;
		check(validateState(trace.snapshots.find(trace.events.size())->second, request.input.size(), &reason),
			"partial cursor snapshot preserves the replayable workspace: " + reason);
		SortState replay = trace.initialState;
		std::vector<SortState> cursors(1, replay);
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			applyEventForward(replay, trace.events[i]);
			cursors.push_back(replay);
		}
		check(sameState(replay, trace.snapshots.find(trace.events.size())->second),
			"partial cursor snapshot matches forward replay");
		for (std::size_t i = trace.events.size(); i > 0; --i)
		{
			applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, cursors[i - 1]), "partial trace rewinds exactly at cursor " + std::to_string(i - 1));
		}
		check(sameState(replay, trace.initialState), "partial trace rewinds to its original input");
	}

	void testDomainsAndPatterns()
	{
		using namespace sortinglogic;
		Simulation signedA(48, 0xA11CEu, InputDomain::SignedIntegerKeys);
		Simulation signedB(48, 0xA11CEu, InputDomain::SignedIntegerKeys);
		bool signedDeterministic = signedA.preparedInput().size() == signedB.preparedInput().size();
		for (std::size_t i = 0; i < signedA.preparedInput().size(); ++i)
		{
			const SortItem& item = signedA.preparedInput()[i];
			signedDeterministic = signedDeterministic && item.stableId == signedB.preparedInput()[i].stableId &&
				item.integerKey == signedB.preparedInput()[i].integerKey;
			check(item.integerKey >= -47 && item.integerKey <= 47 && item.value > 0.0f && item.value < 1.0f,
				"signed integer domain stays in its declared key and chart ranges");
		}
		check(signedDeterministic, "signed integer generation is deterministic from the run seed");
		std::vector<SortItem> signedDuplicateKeys = integers(std::vector<int>{ -4, 1, -2, 4, 0, -4, 3, 1, -1 });
		for (std::size_t i = 0; i < signedDuplicateKeys.size(); ++i)
			signedDuplicateKeys[i].value = static_cast<float>(signedDuplicateKeys[i].integerKey + 9) / 18.0f;
		for (SortAlgorithm algorithm : { SortAlgorithm::Counting, SortAlgorithm::Radix })
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " signed keys",
					signedDuplicateKeys, InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::SignedIntegerKeys);

		Simulation permutation(32, 0x51A7u, InputDomain::IntegerPermutation);
		std::set<int> keys;
		for (std::size_t i = 0; i < permutation.preparedInput().size(); ++i)
			keys.insert(permutation.preparedInput()[i].integerKey);
		check(keys.size() == 32 && *keys.begin() == 0 && *keys.rbegin() == 31,
			"integer permutation domain generates each key in [0,N-1] exactly once");
		check(permutation.setInputPattern(InputPattern::FewUnique).accepted == false &&
			permutation.setInputPattern(InputPattern::OrganPipe).accepted == false,
			"permutation domain rejects patterns that require duplicate keys");

		for (InputDomain domain : { InputDomain::NormalizedFloats, InputDomain::NonnegativeIntegerKeys,
			InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation })
		{
			Simulation sorted(20, 0xA55Au, domain, InputPattern::Sorted);
			Simulation reversed(20, 0xA55Au, domain, InputPattern::Reversed);
			Simulation nearlyA(20, 0xA55Au, domain, InputPattern::NearlySorted);
			Simulation nearlyB(20, 0xA55Au, domain, InputPattern::NearlySorted);
			bool ascending = true, descending = true, nearlyDeterministic = true;
			for (std::size_t i = 1; i < 20; ++i)
			{
				ascending = ascending && compareItems(sorted.preparedInput()[i - 1], sorted.preparedInput()[i], SortDirection::Ascending) != ComparisonResult::Greater;
				descending = descending && compareItems(reversed.preparedInput()[i - 1], reversed.preparedInput()[i], SortDirection::Descending) != ComparisonResult::Greater;
			}
			for (std::size_t i = 0; i < 20; ++i)
				nearlyDeterministic = nearlyDeterministic && sameItem(nearlyA.preparedInput()[i], nearlyB.preparedInput()[i]);
			check(ascending && descending, std::string(inputDomainName(domain)) + " supports sorted and reversed patterns");
			check(nearlyDeterministic, std::string(inputDomainName(domain)) + " nearly-sorted pattern is seed reproducible");
		}
		TraceBuildRequest radixAudioRequest;
		std::vector<int> radixValues;
		for (int key = 405; key >= 0; --key) radixValues.push_back(key);
		radixAudioRequest.input = integers(radixValues);
		radixAudioRequest.algorithm = SortAlgorithm::Radix;
		radixAudioRequest.familyId = algorithmDescriptor(SortAlgorithm::Radix)->familyId;
		radixAudioRequest.variantId = algorithmDescriptor(SortAlgorithm::Radix)->variantId;
		radixAudioRequest.profile = InputProfile::IntegerKeys;
		radixAudioRequest.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace radixAudioTrace = SortTraceBuilder::build(radixAudioRequest);
		bool validRadixProbes = radixAudioTrace.probeEventIndices.size() >= radixAudioRequest.input.size();
		for (std::size_t i = 0; i < radixAudioTrace.probeEventIndices.size(); ++i)
		{
			const std::size_t eventIndex = radixAudioTrace.probeEventIndices[i];
			validRadixProbes = validRadixProbes && eventIndex < radixAudioTrace.events.size() &&
				radixAudioTrace.events[eventIndex].type == SemanticEventType::Probe &&
				radixAudioTrace.events[eventIndex].firstRole == SemanticRole::ProcessedItem;
		}
		check(validRadixProbes, "Radix Sort records its primary-array item reads as reversible semantic probes");
		Simulation fewA(80, 0xF3A1u, InputDomain::NonnegativeIntegerKeys, InputPattern::FewUnique);
		Simulation fewB(80, 0xF3A1u, InputDomain::NonnegativeIntegerKeys, InputPattern::FewUnique);
		std::set<int> fewKeys;
		bool fewDeterministic = true;
		for (std::size_t i = 0; i < fewA.preparedInput().size(); ++i)
		{
			fewKeys.insert(fewA.preparedInput()[i].integerKey);
			fewDeterministic = fewDeterministic && fewA.preparedInput()[i].integerKey == fewB.preparedInput()[i].integerKey;
		}
		check(fewKeys.size() <= 5 && fewDeterministic, "few-unique profile uses at most five reproducible keys");
		Simulation organPipe(12, 23, InputDomain::NormalizedFloats, InputPattern::OrganPipe);
		bool organShape = true;
		for (std::size_t i = 1; i <= 6; ++i) organShape = organShape && organPipe.preparedInput()[i - 1].value <= organPipe.preparedInput()[i].value;
		for (std::size_t i = 7; i < 12; ++i) organShape = organShape && organPipe.preparedInput()[i - 1].value >= organPipe.preparedInput()[i].value;
		check(organShape, "organ-pipe pattern rises to a peak then falls");

		static const int expectedAndrutRanks[100] = {
			78,89,75,81,31,39,93,6,86,27,56,20,42,98,96,95,60,62,77,15,
			2,49,37,46,63,64,82,45,32,12,28,76,94,3,57,59,30,50,90,72,
			38,70,36,53,34,44,40,43,4,84,29,7,25,33,8,41,66,14,67,48,
			35,61,79,88,85,0,92,87,97,19,18,26,58,1,91,10,23,54,55,69,
			73,22,24,5,13,74,71,99,9,16,65,83,51,80,11,52,68,47,21,17
		};
		Simulation andrut(80, 0xA11CEu, InputDomain::NonnegativeIntegerKeys);
		const CommandResult loadedAndrut = andrut.setInputPattern(InputPattern::AndrutReference);
		bool exactAndrutOrder = loadedAndrut.accepted && andrut.itemCount() == 100 &&
			andrut.domain() == InputDomain::NormalizedFloats && andrut.preparedInput().size() == 100;
		std::set<int> referenceRanks;
		for (std::size_t i = 0; i < andrut.preparedInput().size(); ++i)
		{
			const int rank = static_cast<int>(std::lround(andrut.preparedInput()[i].value * 99.0f));
			referenceRanks.insert(rank);
			exactAndrutOrder = exactAndrutOrder && rank == expectedAndrutRanks[i] &&
				andrut.preparedInput()[i].stableId == i;
		}
		check(exactAndrutOrder && referenceRanks.size() == 100 && *referenceRanks.begin() == 0 && *referenceRanks.rbegin() == 99,
			"Andrut reference arrangement restores the extracted 100-rank permutation and selects 100 normalized bars");
		check(andrut.setItemCount(80).accepted && andrut.itemCount() == 80 && andrut.snapshot().pattern == InputPattern::Random,
			"leaving the fixed Andrut arrangement returns to a scalable random pattern");

		TraceBuildRequest invalidPermutation;
		invalidPermutation.input = integers(std::vector<int>{ 0, 0, 2 });
		invalidPermutation.profile = InputProfile::IntegerKeys;
		invalidPermutation.domain = InputDomain::IntegerPermutation;
		bool duplicatePermutationRejected = false;
		try { SortTraceBuilder::build(invalidPermutation); }
		catch (const std::invalid_argument&) { duplicatePermutationRejected = true; }
		check(duplicatePermutationRejected, "trace builder rejects duplicated keys in permutation domain");
	}

	void testSemanticProbeDiscardAndPhases()
	{
		using namespace sortinglogic;
		SortState state = makeInitialState(floats(std::vector<float>{ 0.2f, 0.8f }));
		const SortState initial = state;
		SemanticEvent probe;
		probe.type = SemanticEventType::Probe;
		probe.firstIndex = 1;
		probe.firstItemId = state.slots[1].item.stableId;
		probe.item = state.slots[1].item;
		applyEventForward(state, probe);
		applyEventBackward(state, probe);
		check(sameState(state, initial), "meaningful probes are reversible no-op observations");
		SemanticEvent phase;
		phase.type = SemanticEventType::PhaseBegin;
		phase.groupId = 7;
		phase.workspaceName = "network layer 7";
		applyEventForward(state, phase);
		applyEventBackward(state, phase);
		check(sameState(state, initial) && phase.groupId == 7, "grouped phase markers preserve exact state and their stable group ID");
		SemanticEvent discard;
		discard.type = SemanticEventType::DiscardItem;
		discard.firstIndex = 0;
		discard.item = state.slots[0].item;
		discard.firstItemId = discard.item.stableId;
		applyEventForward(state, discard);
		std::string reason;
		check(state.discardedItems.size() == 1 && !state.slots[0].occupied && state.metrics.discards == 1 &&
			validateState(state, 2, &reason), "discard mutation visibly conserves the logical item in its discard lane");
		applyEventBackward(state, discard);
		check(sameState(state, initial), "discard mutation rewinds to the exact active array");
	}

	void testTraceAudioPresentationPolicy()
	{
		using namespace sortinglogic;
		SortTrace counting;
		counting.algorithm = SortAlgorithm::Counting;
		SemanticEvent probe;
		probe.type = SemanticEventType::Probe;
		probe.firstItemId = 23;
		SemanticEvent auxiliaryWrite;
		auxiliaryWrite.type = SemanticEventType::AuxiliaryItemWrite;
		auxiliaryWrite.workspaceName = "output";
		SemanticEvent counterUpdate;
		counterUpdate.type = SemanticEventType::CounterWrite;
		SemanticEvent discard;
		discard.type = SemanticEventType::DiscardItem;
		counting.events.push_back(probe);
		counting.events.push_back(auxiliaryWrite);
		counting.events.push_back(counterUpdate);
		counting.events.push_back(discard);

		const TraceAudioPresentationPolicy countingPolicy(counting);
		check(countingPolicy.classify(counting.events[0]) == TraceAudioEventKind::ProbeAsComparison,
			"distribution-only traces route meaningful probes through the comparison voice");
		check(countingPolicy.classify(counting.events[1]) == TraceAudioEventKind::AuxiliaryItemWrite &&
			countingPolicy.classify(counting.events[2]) == TraceAudioEventKind::DistributionUpdate,
			"auxiliary item writes and counter bookkeeping have distinct shared presentation routes");
		check(countingPolicy.classify(counting.events[3]) == TraceAudioEventKind::DiscardAccent,
			"discard events receive the discard presentation route");
		TraceAudioOperandProjection probeOperands;
		check(countingPolicy.projectOperands(counting.events[0], probeOperands) && !probeOperands.hasSecondOperand &&
			probeOperands.firstItemId == 23 && probeOperands.firstRole == SemanticRole::ProbedItem,
			"single-item probes share the same stable-ID and semantic-role projection");
		const std::vector<std::size_t> countingIndices = collectAudioEventIndices(counting);
		check(countingIndices.size() == 2 && countingIndices[0] == 0 && countingIndices[1] == 1,
			"reference cadence includes audible probes and auxiliary writes but excludes bookkeeping and discards");
		check(supportsAudioControlCategory(*algorithmDescriptor(SortAlgorithm::Counting), AudioControlCategory::Probe) &&
			supportsAudioControlCategory(*algorithmDescriptor(SortAlgorithm::Counting), AudioControlCategory::Auxiliary),
			"shared presentation policy exposes Counting's supported operation gain categories");

		SortTrace comparisonTrace;
		comparisonTrace.algorithm = SortAlgorithm::Insertion;
		SemanticEvent compare;
		compare.type = SemanticEventType::Compare;
		SemanticEvent movement;
		movement.type = SemanticEventType::ShiftItem;
		comparisonTrace.events.push_back(compare);
		comparisonTrace.events.push_back(movement);
		comparisonTrace.comparisonEventIndices.push_back(0);
		const TraceAudioPresentationPolicy comparisonPolicy(comparisonTrace);
		check(comparisonPolicy.hasAlgorithmComparisons() &&
			comparisonPolicy.classify(comparisonTrace.events[0]) == TraceAudioEventKind::Comparison &&
			comparisonPolicy.classify(comparisonTrace.events[1]) == TraceAudioEventKind::MainChangeAccent,
			"comparison methods share comparison and main-change semantic classification");
		check(collectAudioEventIndices(comparisonTrace).size() == 1,
			"main-array accents do not alter reference-cadence event counts");
		TraceAudioOperandProjection comparisonOperands;
		check(comparisonPolicy.projectOperands(comparisonTrace.events[0], comparisonOperands) &&
			comparisonOperands.hasSecondOperand && comparisonOperands.firstItemId == comparisonTrace.events[0].firstItemId &&
			comparisonOperands.secondItemId == comparisonTrace.events[0].secondItemId &&
			comparisonOperands.firstRole == comparisonTrace.events[0].firstRole,
			"paired comparison projections preserve event operand order and roles");

		SortTrace hiddenMerge;
		hiddenMerge.algorithm = SortAlgorithm::Merge;
		hiddenMerge.mergeBufferVisualizationEnabled = false;
		SemanticEvent mergeWrite = auxiliaryWrite;
		mergeWrite.workspaceName = "merge";
		SemanticEvent mergeMutation;
		mergeMutation.type = SemanticEventType::WriteRange;
		SemanticEvent completionCompare = compare;
		completionCompare.completionScan = true;
		hiddenMerge.events.push_back(mergeWrite);
		hiddenMerge.events.push_back(mergeMutation);
		hiddenMerge.events.push_back(completionCompare);
		hiddenMerge.comparisonEventIndices.push_back(2);
		const TraceAudioPresentationPolicy hiddenMergePolicy(hiddenMerge);
		check(hiddenMergePolicy.classify(mergeWrite) == TraceAudioEventKind::Silent &&
			!hiddenMergePolicy.contributesToReferenceCadence(mergeWrite) &&
			!hiddenMergePolicy.frameAccentVisible(mergeMutation),
			"canonical Merge keeps auxiliary writes and non-comparison accents out of the reference score");
		check(hiddenMergePolicy.classify(completionCompare) == TraceAudioEventKind::Comparison &&
			hiddenMergePolicy.contributesToReferenceCadence(completionCompare) &&
			hiddenMergePolicy.frameAccentVisible(completionCompare) && hiddenMergePolicy.usesComparisonCadence(),
			"completion scans stay audible on the canonical Merge comparison schedule");
		SortTrace visibleMerge = hiddenMerge;
		visibleMerge.mergeBufferVisualizationEnabled = true;
		const TraceAudioPresentationPolicy visibleMergePolicy(visibleMerge);
		check(visibleMergePolicy.classify(mergeWrite) == TraceAudioEventKind::Silent &&
			!visibleMergePolicy.contributesToReferenceCadence(mergeWrite) &&
			collectAudioEventIndices(visibleMerge).size() == collectAudioEventIndices(hiddenMerge).size(),
			"canonical Merge audio event eligibility is invariant under auxiliary-lane visibility");
		check(!isWorkspaceVisible(SortAlgorithm::Merge, false, "merge") &&
			isWorkspaceVisible(SortAlgorithm::Merge, false, "counts") &&
			isWorkspaceVisible(SortAlgorithm::Merge, true, "merge") &&
			visibleRoleWorkspace(SortAlgorithm::Merge, false, "merge") == "main",
			"audio and chart consumers share Merge lane visibility and main-array role projection");
		RenderSnapshot mergeSnapshot;
		mergeSnapshot.algorithm = SortAlgorithm::Merge;
		mergeSnapshot.mergeBufferVisualizationEnabled = false;
		mergeSnapshot.activeRoles.push_back(ActiveRole(SemanticRole::ComparedItem, 11, 0, "merge"));
		mergeSnapshot.activeRoles.push_back(ActiveRole(SemanticRole::WriteTarget, 12, 1, "main"));
		const std::vector<ActiveRole> hiddenMergeRoles = visibleRolesOnMainArray(mergeSnapshot);
		check(hiddenMergeRoles.size() == 2 && hiddenMergeRoles[0].lane == "main" && hiddenMergeRoles[1].lane == "main",
			"hidden Merge buffer projects its semantic roles onto the visible primary array");
		mergeSnapshot.mergeBufferVisualizationEnabled = true;
		check(visibleRolesOnMainArray(mergeSnapshot)[0].lane == "merge",
			"visible Merge buffer preserves the role's auxiliary lane identity");
		check(supportsAudioControlCategory(*algorithmDescriptor(SortAlgorithm::CanonicalStalinFilter), AudioControlCategory::Discard),
			"catalog behavior labels declare discard audio controls");
	}

	std::size_t dynamicStringStorageBytes(const std::string& value)
	{
		return value.capacity() > 15 ? value.capacity() + 1 : 0;
	}

	std::size_t stateRetainedPayloadLowerBound(const sortinglogic::SortState& state)
	{
		using namespace sortinglogic;
		std::size_t bytes = state.slots.capacity() * sizeof(SortSlot) +
			state.discardedItems.capacity() * sizeof(SortItem);
		for (std::map<std::string, std::vector<SortSlot> >::const_iterator i = state.auxiliaryItems.begin();
			i != state.auxiliaryItems.end(); ++i)
			bytes += sizeof(std::pair<const std::string, std::vector<SortSlot> >) +
				dynamicStringStorageBytes(i->first) + i->second.capacity() * sizeof(SortSlot);
		for (std::map<std::string, std::vector<int> >::const_iterator i = state.integerWorkspaces.begin();
			i != state.integerWorkspaces.end(); ++i)
			bytes += sizeof(std::pair<const std::string, std::vector<int> >) +
				dynamicStringStorageBytes(i->first) + i->second.capacity() * sizeof(int);
		for (std::map<std::string, std::uint64_t>::const_iterator i = state.metrics.algorithm.begin();
			i != state.metrics.algorithm.end(); ++i)
			bytes += sizeof(std::pair<const std::string, std::uint64_t>) + dynamicStringStorageBytes(i->first);
		return bytes;
	}

	std::size_t traceRetainedPayloadLowerBound(const sortinglogic::SortTrace& trace)
	{
		using namespace sortinglogic;
		std::size_t bytes = sizeof(SortTrace) + trace.input.capacity() * sizeof(SortItem) +
			trace.events.capacity() * sizeof(SemanticEvent) +
			(trace.comparisonEventIndices.capacity() + trace.probeEventIndices.capacity() +
			trace.audioEventIndices.capacity()) * sizeof(std::size_t) +
			trace.annotations.capacity() * sizeof(RegionAnnotation) +
			stateRetainedPayloadLowerBound(trace.initialState) + dynamicStringStorageBytes(trace.familyId) +
			dynamicStringStorageBytes(trace.variantId) + dynamicStringStorageBytes(trace.completionReason);
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const SemanticEvent& event = trace.events[i];
			bytes += dynamicStringStorageBytes(event.firstLane) + dynamicStringStorageBytes(event.secondLane) +
				dynamicStringStorageBytes(event.workspaceName) + dynamicStringStorageBytes(event.metricName) +
				(event.beforeSlots.capacity() + event.afterSlots.capacity()) * sizeof(SortSlot) +
				event.beforeCounters.capacity() * sizeof(int);
		}
		for (std::size_t i = 0; i < trace.annotations.size(); ++i)
			bytes += dynamicStringStorageBytes(trace.annotations[i].lane) + dynamicStringStorageBytes(trace.annotations[i].label);
		for (std::map<std::size_t, SortState>::const_iterator i = trace.snapshots.begin(); i != trace.snapshots.end(); ++i)
			bytes += sizeof(std::pair<const std::size_t, SortState>) + stateRetainedPayloadLowerBound(i->second);
		return bytes;
	}

	void printTraceMemoryProfileRow(const char* name, const sortinglogic::SortTrace& trace, double buildMilliseconds)
	{
		const std::size_t lowerBound = traceRetainedPayloadLowerBound(trace);
		std::cout << name << " items=" << trace.input.size() << " events=" << trace.events.size()
			<< " event_capacity=" << trace.events.capacity() << " snapshots=" << trace.snapshots.size()
			<< " accounted_KiB=" << trace.accountedBytes / 1024
			<< " retained_payload_lower_bound_KiB=" << lowerBound / 1024
			<< " build_ms=" << buildMilliseconds
			<< " complete=" << (trace.complete ? "yes" : "no") << '\n';
	}

	void buildAndPrintTraceMemoryProfileRow(const char* name, const sortinglogic::TraceBuildRequest& request)
	{
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		const sortinglogic::SortTrace trace = sortinglogic::SortTraceBuilder::build(request);
		const double buildMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - started).count();
		printTraceMemoryProfileRow(name, trace, buildMilliseconds);
	}

	void runTraceMemoryProfile()
	{
		using namespace sortinglogic;
		std::cout << "SemanticEvent object bytes=" << sizeof(SemanticEvent)
			<< " (lower-bound estimate excludes allocator and tree-node overhead)\n";

		std::vector<float> floatValues;
		for (std::size_t i = 0; i < 256; ++i) floatValues.push_back(static_cast<float>((i * 47u) % 251u) / 250.0f);
		TraceBuildRequest insertionRequest;
		insertionRequest.algorithm = SortAlgorithm::Insertion;
		insertionRequest.input = floats(floatValues);
		buildAndPrintTraceMemoryProfileRow("Insertion-256", insertionRequest);

		floatValues.clear();
		for (std::size_t i = 0; i < 1024; ++i) floatValues.push_back(static_cast<float>((i * 479u) % 1021u) / 1020.0f);
		TraceBuildRequest budgetedInsertionRequest;
		budgetedInsertionRequest.algorithm = SortAlgorithm::Insertion;
		budgetedInsertionRequest.input = floats(floatValues);
		buildAndPrintTraceMemoryProfileRow("Insertion-1024-budgeted", budgetedInsertionRequest);

		floatValues.clear();
		for (std::size_t i = 0; i < 800; ++i) floatValues.push_back(static_cast<float>((i * 47u) % 797u) / 796.0f);
		TraceBuildRequest mergeRequest;
		mergeRequest.algorithm = SortAlgorithm::Merge;
		mergeRequest.input = floats(floatValues);
		buildAndPrintTraceMemoryProfileRow("Merge-800", mergeRequest);

		std::vector<int> integerValues;
		for (std::size_t i = 0; i < 800; ++i) integerValues.push_back(static_cast<int>((i * 61u) % 800u));
		TraceBuildRequest countingRequest;
		countingRequest.algorithm = SortAlgorithm::Counting;
		countingRequest.profile = InputProfile::IntegerKeys;
		countingRequest.domain = InputDomain::NonnegativeIntegerKeys;
		countingRequest.input = integers(integerValues);
		buildAndPrintTraceMemoryProfileRow("Counting-800", countingRequest);
	}
	enum class TestCommandKind { Focused, CatalogCoverage, TraceMemoryProfile };
	struct RegisteredTest { const char* id; const char* stage; void (*run)(); bool inFullSuite; };
	struct TestCommand { const char* argument; const char* summary; TestCommandKind kind; const char* testIds; };
	const char* activeStage = "test registry validation";

	void runPDQVariants() { testPDQVariants(true); }
	void runPDQVariantsSmoke() { testPDQVariants(false); }
	void runRemainingMergeRosterBatch() { testRemainingMergeRosterBatch(0); }
	void runNewShuffleMerge() { testRemainingMergeRosterBatch(8); }

	void runCoreContracts()
	{
		using namespace sortinglogic;
		activeStage = "Insertion strategy extraction";
		const std::vector<SortItem> insertionInput = floats({ 0.7f, 0.2f, 0.7f, 0.1f, 0.2f });
		verifyAlgorithmCase("Extracted Insertion strategy ascending", insertionInput, InputProfile::NormalizedFloat,
			SortAlgorithm::Insertion, SortDirection::Ascending);
		verifyAlgorithmCase("Extracted Insertion strategy descending", insertionInput, InputProfile::NormalizedFloat,
			SortAlgorithm::Insertion, SortDirection::Descending);
		verifyAlgorithmCase("Intro Sort small-range Insertion fallback", insertionInput, InputProfile::NormalizedFloat,
			SortAlgorithm::ArrayvIntroSort, SortDirection::Ascending);
		const SortAlgorithm elementaryAlgorithms[] = {
			SortAlgorithm::Bubble, SortAlgorithm::Selection, SortAlgorithm::ArrayvBingo,
			SortAlgorithm::ArrayvDoubleSelection, SortAlgorithm::ArrayvStableSelection,
			SortAlgorithm::Gnome, SortAlgorithm::Cocktail, SortAlgorithm::Comb, SortAlgorithm::OddEven,
			SortAlgorithm::ArrayvOptimizedBubble, SortAlgorithm::ArrayvUnoptimizedBubble,
			SortAlgorithm::ArrayvSwaplessBubble, SortAlgorithm::ArrayvOptimizedCocktail,
			SortAlgorithm::ArrayvUnoptimizedCocktail
		};
		const InputDomain candidateDomains[] = { InputDomain::NormalizedFloats, InputDomain::NonnegativeIntegerKeys,
			InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation };
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(elementaryAlgorithms) / sizeof(elementaryAlgorithms[0]); ++algorithmIndex)
		{
			const SortAlgorithm algorithm = elementaryAlgorithms[algorithmIndex];
			InputDomain domain = InputDomain::NormalizedFloats;
			for (std::size_t domainIndex = 0; domainIndex < sizeof(candidateDomains) / sizeof(candidateDomains[0]); ++domainIndex)
				if (algorithmSupports(algorithm, candidateDomains[domainIndex]))
				{
					domain = candidateDomains[domainIndex];
					break;
				}
			std::vector<SortItem> input;
			if (domain == InputDomain::NormalizedFloats) input = floats({ 0.6f, 0.2f, 0.6f, 0.1f, 0.2f });
			else
			{
				const int keys[] = { 3, 1, 3, 0, 1 };
				for (std::size_t itemIndex = 0; itemIndex < 5; ++itemIndex)
				{
					const int key = domain == InputDomain::SignedIntegerKeys ? keys[itemIndex] - 2 : keys[itemIndex];
					const float value = domain == InputDomain::SignedIntegerKeys
						? static_cast<float>(key + 5) / 10.0f : static_cast<float>(key + 1) / 5.0f;
					input.push_back(SortItem(itemIndex, key, value));
				}
				if (domain == InputDomain::IntegerPermutation)
				{
					const int permutationKeys[] = { 3, 1, 4, 0, 2 };
					for (std::size_t itemIndex = 0; itemIndex < 5; ++itemIndex)
						input[itemIndex] = SortItem(itemIndex, permutationKeys[itemIndex], static_cast<float>(permutationKeys[itemIndex] + 1) / 5.0f);
				}
			}
			const InputProfile profile = inputProfileForDomain(domain);
			const std::string label = algorithmDisplayName(algorithm);
			verifyAlgorithmCase("Elementary " + label + " ascending", input, profile, algorithm, SortDirection::Ascending, domain);
			verifyAlgorithmCase("Elementary " + label + " descending", input, profile, algorithm, SortDirection::Descending, domain);
		}
		const SortAlgorithm remainingNetworkStrategies[] = {
			SortAlgorithm::ArrayvClassicThreeSmoothComb, SortAlgorithm::ArrayvThreeSmoothCombIterative,
			SortAlgorithm::ArrayvThreeSmoothCombRecursive, SortAlgorithm::ArrayvThreeSmoothCombParallel,
			SortAlgorithm::ArrayvMatrix
		};
		const std::vector<SortItem> networkInput = integers(std::vector<int>{ 9, 1, 7, 3, 8, 2, 5, 0, 6, 4, 4 });
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(remainingNetworkStrategies) / sizeof(remainingNetworkStrategies[0]); ++algorithmIndex)
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				const SortAlgorithm algorithm = remainingNetworkStrategies[algorithmIndex];
				const SortDirection direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
				verifyAlgorithmCase(std::string("Extracted ") + algorithmDisplayName(algorithm), networkInput, InputProfile::IntegerKeys,
					algorithm, direction, InputDomain::NonnegativeIntegerKeys);
			}
		activeStage = "Direct distribution strategy extraction";
		testDirectDistributionStrategies();
		activeStage = "testDistributionAudioBaselines";
		testDistributionAudioBaselines();
		activeStage = "testBurntPancakeOrientation";
		testBurntPancakeOrientation();
		activeStage = "testSortingNetworkSchedules";
		testSortingNetworkSchedules();
		activeStage = "testMergeWritebackModes";
		testMergeWritebackModes();
		activeStage = "testSimulationAndProfiles";
		testSimulationAndProfiles();
		activeStage = "testSnapshotAnnotations";
		testSnapshotAnnotations();
		activeStage = "testTraceServiceAndPlayback";
		testTraceServiceAndPlayback();
		activeStage = "testStressAndValidation";
		testStressAndValidation();
		activeStage = "testPartialTraceBudget";
		testPartialTraceBudget();
		activeStage = "testDomainsAndPatterns";
		testDomainsAndPatterns();
		activeStage = "testSemanticProbeDiscardAndPhases";
		testSemanticProbeDiscardAndPhases();
		activeStage = "testTraceAudioPresentationPolicy";
		testTraceAudioPresentationPolicy();
		std::cout << "SortingLogic core contracts: " << checks << " checks, " << failures << " failures.\n";
	}

	const RegisteredTest registeredTests[] = {
		{"testCoreContracts", "SortingLogic core contracts", runCoreContracts, false},
		{"catalog-coverage", "testPinnedCatalogCoverage", testPinnedCatalogCoverage, false},
		{"testDistributionAudioBaselines", "testDistributionAudioBaselines", testDistributionAudioBaselines, true},
		{"testStochasticNoveltyVariants", "testStochasticNoveltyVariants", testStochasticNoveltyVariants, true},
		{"testAdditionalBogoVariants", "testAdditionalBogoVariants", testAdditionalBogoVariants, true},
		{"testPointerQuicksortVariants", "testPointerQuicksortVariants", testPointerQuicksortVariants, true},
		{"testNestedBogoVariants", "testNestedBogoVariants", testNestedBogoVariants, true},
		{"testDualPivotQuickVariants", "testDualPivotQuickVariants", testDualPivotQuickVariants, true},
		{"testStableQuickVariants", "testStableQuickVariants", testStableQuickVariants, true},
		{"testMergeFamilyVariants", "testMergeFamilyVariants", testMergeFamilyVariants, true},
		{"testIterativeTopDownMergeSort", "testIterativeTopDownMergeSort", testIterativeTopDownMergeSort, true},
		{"testInPlaceMergeSort", "testInPlaceMergeSort", testInPlaceMergeSort, true},
		{"testParallelMergeSort", "testParallelMergeSort", testParallelMergeSort, true},
		{"testRotateMergeSort", "testRotateMergeSort", testRotateMergeSort, true},
		{"testParallelRotateMergeSort", "testParallelRotateMergeSort", testParallelRotateMergeSort, true},
		{"testStacklessRotateMergeSort", "testStacklessRotateMergeSort", testStacklessRotateMergeSort, true},
		{"testBlockSwapMergeSort", "testBlockSwapMergeSort", testBlockSwapMergeSort, true},
		{"testWeavedMergeSort", "testWeavedMergeSort", testWeavedMergeSort, true},
		{"testTwinSort", "testTwinSort", testTwinSort, true},
		{"testTimMergeVariants", "testTimMergeVariants", testTimMergeVariants, true},
		{"testImprovedInPlaceMergeSort", "testImprovedInPlaceMergeSort", testImprovedInPlaceMergeSort, true},
		{"testBlockInsertionSort", "testBlockInsertionSort", testBlockInsertionSort, true},
		{"testBufferedStoogeSort", "testBufferedStoogeSort", testBufferedStoogeSort, true},
		{"testBadSort", "testBadSort", testBadSort, true},
		{"testAsynchronousSort", "testAsynchronousSort", testAsynchronousSort, true},
		{"testPDMergeSort", "testPDMergeSort", testPDMergeSort, true},
		{"testStableCycleSort", "testStableCycleSort", testStableCycleSort, true},
		{"testTournamentSorts", "testTournamentSorts", testTournamentSorts, true},
		{"testSmoothSortVariants", "testSmoothSortVariants", testSmoothSortVariants, true},
		{"testImprovedBlockSelectionSort", "testImprovedBlockSelectionSort", testImprovedBlockSelectionSort, true},
		{"testStacklessHybridQuickSort", "testStacklessHybridQuickSort", testStacklessHybridQuickSort, true},
		{"testStacklessDualPivotQuickSort", "testStacklessDualPivotQuickSort", testStacklessDualPivotQuickSort, true},
		{"testHanoiSort", "testHanoiSort", testHanoiSort, true},
		{"testSimplifiedLibrarySort", "testSimplifiedLibrarySort", testSimplifiedLibrarySort, true},
		{"testLibrarySort", "testLibrarySort", testLibrarySort, true},
		{"testWeaveMergeSort", "testWeaveMergeSort", testWeaveMergeSort, true},
		{"testLaziestStableSort", "testLaziestStableSort", testLaziestStableSort, true},
		{"testLazierestStableSort", "testLazierestStableSort", testLazierestStableSort, true},
		{"testLazyStableMergeVariants", "testLazyStableMergeVariants", testLazyStableMergeVariants, true},
		{"testFifthAndQuadMergeSorts", "testFifthAndQuadMergeSorts", testFifthAndQuadMergeSorts, true},
		{"testBufferPartitionMergeSort", "testBufferPartitionMergeSort", testBufferPartitionMergeSort, true},
		{"testParallelBlockMergeSort", "testParallelBlockMergeSort", testParallelBlockMergeSort, true},
		{"testOptimizedRotateMergeSort", "testOptimizedRotateMergeSort", testOptimizedRotateMergeSort, true},
		{"testGrailSortModes", "testGrailSortModes", testGrailSortModes, true},
		{"testAdaptiveGrailSort", "testAdaptiveGrailSort", testAdaptiveGrailSort, true},
		{"testPDQVariants", "testPDQVariants", runPDQVariants, true},
		{"testSqrtMergeVariants", "testSqrtMergeVariants", testSqrtMergeVariants, true},
		{"testEctaSort", "testEctaSort", testEctaSort, true},
		{"testCircularAndUnstableGrailSorts", "testCircularAndUnstableGrailSorts", testCircularAndUnstableGrailSorts, true},
		{"testDropMergeSort", "testDropMergeSort", testDropMergeSort, true},
		{"testParallelGrailSort", "testParallelGrailSort", testParallelGrailSort, true},
		{"testMedianMergeSort", "testMedianMergeSort", testMedianMergeSort, true},
		{"testRemiSort", "testRemiSort", testRemiSort, true},
		{"testRemainingMergeRosterBatch", "testRemainingMergeRosterBatch", runRemainingMergeRosterBatch, true},
		{"testTernaryQuickVariants", "testTernaryQuickVariants", testTernaryQuickVariants, true},
		{"testIndirectAndPermutationVariants", "testIndirectAndPermutationVariants", testIndirectAndPermutationVariants, true},
		{"testHybridCombAndIntroCircleVariants", "testHybridCombAndIntroCircleVariants", testHybridCombAndIntroCircleVariants, true},
		{"testDoubleInsertionVariants", "testDoubleInsertionVariants", testDoubleInsertionVariants, true},
		{"testIntroSort", "testIntroSort", testIntroSort, true},
		{"testPatienceSort", "testPatienceSort", testPatienceSort, true},
		{"testParallelShellSort", "testParallelShellSort", testParallelShellSort, true},
		{"testMergeInsertionSort", "testMergeInsertionSort", testMergeInsertionSort, true},
		{"testStalinVariants", "testStalinVariants", testStalinVariants, true},
		{"testUnbalancedTreeVariants", "testUnbalancedTreeVariants", testUnbalancedTreeVariants, true},
		{"testStrandSort", "testStrandSort", testStrandSort, true},
		{"testBalancedTreeSorts", "testBalancedTreeSorts", testBalancedTreeSorts, true},
		{"testSplayTreeSort", "testSplayTreeSort", testSplayTreeSort, true},
		{"verifyProfilesAndAlgorithms", "verifyProfilesAndAlgorithms", verifyProfilesAndAlgorithms, true},
		{"testBurntPancakeOrientation", "testBurntPancakeOrientation", testBurntPancakeOrientation, true},
		{"testSortingNetworkSchedules", "testSortingNetworkSchedules", testSortingNetworkSchedules, true},
		{"testMatrixSortShapes", "testMatrixSortShapes", testMatrixSortShapes, true},
		{"testIncompleteStoogeOutcome", "testIncompleteStoogeOutcome", testIncompleteStoogeOutcome, true},
		{"testStoogeFamilyVariants", "testStoogeFamilyVariants", testStoogeFamilyVariants, true},
		{"testRecursiveExchangeNoveltyVariants", "testRecursiveExchangeNoveltyVariants", testRecursiveExchangeNoveltyVariants, true},
		{"testFunSortVariants", "testFunSortVariants", testFunSortVariants, true},
		{"testMergeWritebackModes", "testMergeWritebackModes", testMergeWritebackModes, true},
		{"testSimulationAndProfiles", "testSimulationAndProfiles", testSimulationAndProfiles, true},
		{"testSnapshotAnnotations", "testSnapshotAnnotations", testSnapshotAnnotations, true},
		{"testTraceServiceAndPlayback", "testTraceServiceAndPlayback", testTraceServiceAndPlayback, true},
		{"testStressAndValidation", "testStressAndValidation", testStressAndValidation, true},
		{"testPartialTraceBudget", "testPartialTraceBudget", testPartialTraceBudget, true},
		{"testDomainsAndPatterns", "testDomainsAndPatterns", testDomainsAndPatterns, true},
		{"testSemanticProbeDiscardAndPhases", "testSemanticProbeDiscardAndPhases", testSemanticProbeDiscardAndPhases, true},
		{"testTraceAudioPresentationPolicy", "testTraceAudioPresentationPolicy", testTraceAudioPresentationPolicy, true},
		{"testBinaryInsertionStrategy", "testBinaryInsertionStrategy", testBinaryInsertionStrategy, false},
		{"testPancakeStrategyFamily", "testPancakeStrategyFamily", testPancakeStrategyFamily, false},
		{"testHeapStrategyFamily", "testHeapStrategyFamily", testHeapStrategyFamily, false},
		{"testCircleStrategyFamily", "testCircleStrategyFamily", testCircleStrategyFamily, false},
		{"testElementaryTailStrategies", "testElementaryTailStrategies", testElementaryTailStrategies, false},
		{"testPDQVariantsSmoke", "PDQ boundary and duplicate cases", runPDQVariantsSmoke, false},
		{"testNewShuffleMerge", "testNewShuffleMerge", runNewShuffleMerge, false},
		{"testGapInsertionStrategyCoverage", "testGapInsertionStrategyCoverage", testGapInsertionStrategyCoverage, false},
	};

	const TestCommand testCommands[] = {
		{"--catalog-coverage-only", "Pinned ArrayV catalog coverage: ", TestCommandKind::CatalogCoverage, "" },
		{"--trace-memory-profile", "", TestCommandKind::TraceMemoryProfile, "" },
		{"--binary-insertion-only", "Focused Binary Insertion tests: ", TestCommandKind::Focused, "testBinaryInsertionStrategy" },
		{"--pancake-family-only", "Focused Pancake family tests: ", TestCommandKind::Focused, "testPancakeStrategyFamily" },
		{"--heap-family-only", "Focused Heap family tests: ", TestCommandKind::Focused, "testHeapStrategyFamily" },
		{"--circle-family-only", "Focused Circle family tests: ", TestCommandKind::Focused, "testCircleStrategyFamily,testHybridCombAndIntroCircleVariants" },
		{"--elementary-tail-only", "Focused elementary exchange tests: ", TestCommandKind::Focused, "testElementaryTailStrategies,testHybridCombAndIntroCircleVariants" },
		{"--core-contracts-only", "", TestCommandKind::Focused, "testCoreContracts" },
		{"--bogo-only", "Focused stochastic novelty tests: ", TestCommandKind::Focused, "testStochasticNoveltyVariants,testAdditionalBogoVariants,testNestedBogoVariants" },
			{"--recursive-novelty-only", "Focused recursive novelty tests: ", TestCommandKind::Focused, "testIncompleteStoogeOutcome,testStoogeFamilyVariants,testRecursiveExchangeNoveltyVariants,testFunSortVariants,testBufferedStoogeSort,testSortingNetworkSchedules" },
		{"--dual-pivot-only", "Focused dual-pivot tests: ", TestCommandKind::Focused, "testDualPivotQuickVariants" },
		{"--stable-quick-only", "Focused stable Quick tests: ", TestCommandKind::Focused, "testStableQuickVariants" },
		{"--merge-variants-only", "Focused merge-family tests: ", TestCommandKind::Focused, "testMergeFamilyVariants" },
		{"--iterative-top-down-merge-only", "Focused Iterative Top-down Merge tests: ", TestCommandKind::Focused, "testIterativeTopDownMergeSort" },
		{"--in-place-merge-only", "Focused In-Place Merge tests: ", TestCommandKind::Focused, "testInPlaceMergeSort" },
		{"--parallel-merge-only", "Focused Parallel Merge tests: ", TestCommandKind::Focused, "testParallelMergeSort" },
		{"--rotate-merge-only", "Focused Rotate Merge tests: ", TestCommandKind::Focused, "testRotateMergeSort" },
		{"--parallel-rotate-merge-only", "Focused Parallel Rotate Merge tests: ", TestCommandKind::Focused, "testParallelRotateMergeSort" },
		{"--stackless-rotate-merge-only", "Focused Stackless Rotate Merge tests: ", TestCommandKind::Focused, "testStacklessRotateMergeSort" },
		{"--block-swap-merge-only", "Focused Block-Swap Merge tests: ", TestCommandKind::Focused, "testBlockSwapMergeSort" },
		{"--weaved-merge-only", "Focused Weaved Merge tests: ", TestCommandKind::Focused, "testWeavedMergeSort" },
		{"--twin-sort-only", "Focused Twin Sort tests: ", TestCommandKind::Focused, "testTwinSort" },
		{"--tim-merge-only", "Focused Tim merge tests: ", TestCommandKind::Focused, "testTimMergeVariants" },
		{"--improved-inplace-merge-only", "Focused Improved In-Place Merge tests: ", TestCommandKind::Focused, "testImprovedInPlaceMergeSort" },
		{"--block-insertion-only", "Focused Block Insertion tests: ", TestCommandKind::Focused, "testBlockInsertionSort" },
		{"--buffered-stooge-only", "Focused Buffered Stooge tests: ", TestCommandKind::Focused, "testBufferedStoogeSort" },
		{"--bad-sort-only", "Focused Bad Sort tests: ", TestCommandKind::Focused, "testBadSort" },
		{"--asynchronous-sort-only", "Focused Asynchronous Sort tests: ", TestCommandKind::Focused, "testAsynchronousSort" },
		{"--pd-merge-only", "Focused Pattern-Defeating Merge tests: ", TestCommandKind::Focused, "testPDMergeSort" },
		{"--stable-cycle-only", "Focused Stable Cycle tests: ", TestCommandKind::Focused, "testStableCycleSort" },
		{"--tournament-only", "Focused tournament tests: ", TestCommandKind::Focused, "testTournamentSorts" },
		{"--smooth-sort-only", "Focused Smooth Sort tests: ", TestCommandKind::Focused, "testSmoothSortVariants" },
		{"--block-selection-only", "Focused Improved Block Selection tests: ", TestCommandKind::Focused, "testImprovedBlockSelectionSort" },
		{"--stackless-hybrid-only", "Focused Stackless Hybrid Quick tests: ", TestCommandKind::Focused, "testStacklessHybridQuickSort" },
		{"--stackless-dual-pivot-only", "Focused Stackless Dual-Pivot Quick tests: ", TestCommandKind::Focused, "testStacklessDualPivotQuickSort" },
		{"--hanoi-only", "Focused Hanoi Sort tests: ", TestCommandKind::Focused, "testHanoiSort" },
		{"--simplified-library-only", "Focused Simplified Library tests: ", TestCommandKind::Focused, "testSimplifiedLibrarySort" },
		{"--library-only", "Focused Library Sort tests: ", TestCommandKind::Focused, "testLibrarySort" },
		{"--weave-merge-only", "Focused Weave Merge tests: ", TestCommandKind::Focused, "testWeaveMergeSort" },
		{"--laziest-only", "Focused Laziest Stable tests: ", TestCommandKind::Focused, "testLaziestStableSort" },
		{"--lazierest-only", "Focused Lazierest Stable tests: ", TestCommandKind::Focused, "testLazierestStableSort" },
		{"--lazy-grail-only", "Focused Lazy Stable merge tests: ", TestCommandKind::Focused, "testLazyStableMergeVariants" },
		{"--multiway-merge-only", "Focused Fifth/Quad merge tests: ", TestCommandKind::Focused, "testFifthAndQuadMergeSorts" },
		{"--buffer-partition-merge-only", "Focused Buffer Partition Merge tests: ", TestCommandKind::Focused, "testBufferPartitionMergeSort" },
		{"--parallel-block-merge-only", "Focused Parallel Block Merge tests: ", TestCommandKind::Focused, "testParallelBlockMergeSort" },
		{"--optimized-rotate-merge-only", "Focused Optimized Rotate Merge tests: ", TestCommandKind::Focused, "testOptimizedRotateMergeSort" },
		{"--grail-only", "Focused Grail family tests: ", TestCommandKind::Focused, "testGrailSortModes,testAdaptiveGrailSort" },
		{"--pdq-smoke-only", "Focused PDQ boundary tests: ", TestCommandKind::Focused, "testPDQVariantsSmoke" },
		{"--pdq-only", "Focused PDQ tests: ", TestCommandKind::Focused, "testPDQVariants" },
		{"--sqrt-only", "Focused Sqrt Sort tests: ", TestCommandKind::Focused, "testSqrtMergeVariants" },
		{"--ecta-only", "Focused Ecta tests: ", TestCommandKind::Focused, "testEctaSort" },
		{"--grail-variants-only", "Focused Circular/Unstable Grail tests: ", TestCommandKind::Focused, "testCircularAndUnstableGrailSorts" },
		{"--drop-merge-only", "Focused Drop Merge tests: ", TestCommandKind::Focused, "testDropMergeSort" },
		{"--parallel-grail-only", "Focused Parallel Grail tests: ", TestCommandKind::Focused, "testParallelGrailSort" },
		{"--median-merge-only", "Focused Median Merge tests: ", TestCommandKind::Focused, "testMedianMergeSort" },
		{"--remi-only", "Focused Remi Sort tests: ", TestCommandKind::Focused, "testRemiSort" },
		{"--merge-roster-tail-only", "Focused merge roster tail tests: ", TestCommandKind::Focused, "testRemainingMergeRosterBatch" },
		{"--new-shuffle-only", "Focused New Shuffle Merge tests: ", TestCommandKind::Focused, "testNewShuffleMerge" },
		{"--stackless-partition-only", "Focused stackless partition tests: ", TestCommandKind::Focused, "testStacklessHybridQuickSort,testStacklessDualPivotQuickSort" },
		{"--partition-quick-only", "Focused partition Quick tests: ", TestCommandKind::Focused, "testPointerQuicksortVariants,testDualPivotQuickVariants,testStableQuickVariants,testTernaryQuickVariants" },
		{"--ternary-quick-only", "Focused ternary Quick tests: ", TestCommandKind::Focused, "testTernaryQuickVariants" },
		{"--indirect-only", "Focused indirect and permutation tests: ", TestCommandKind::Focused, "testIndirectAndPermutationVariants" },
		{"--hybrid-exchange-only", "Focused hybrid exchange tests: ", TestCommandKind::Focused, "testHybridCombAndIntroCircleVariants" },
		{"--double-insertion-only", "Focused double insertion tests: ", TestCommandKind::Focused, "testDoubleInsertionVariants" },
		{"--intro-sort-only", "Focused IntroSort tests: ", TestCommandKind::Focused, "testIntroSort" },
		{"--patience-only", "Focused Patience Sort tests: ", TestCommandKind::Focused, "testPatienceSort" },
		{"--gap-insertion-only", "Focused insertion/gap strategy tests: ", TestCommandKind::Focused, "testGapInsertionStrategyCoverage,testDoubleInsertionVariants,testParallelShellSort" },
		{"--parallel-shell-only", "Focused parallel Shell tests: ", TestCommandKind::Focused, "testParallelShellSort" },
		{"--merge-insertion-only", "Focused Merge-Insertion tests: ", TestCommandKind::Focused, "testMergeInsertionSort" },
		{"--stalin-only", "Focused Stalin Sort tests: ", TestCommandKind::Focused, "testStalinVariants" },
		{"--tree-family-only", "Focused tree family tests: ", TestCommandKind::Focused, "testUnbalancedTreeVariants,testBalancedTreeSorts,testSplayTreeSort,testStrandSort" },
		{"--tree-only", "Focused unbalanced tree tests: ", TestCommandKind::Focused, "testUnbalancedTreeVariants" },
		{"--strand-only", "Focused Strand Sort tests: ", TestCommandKind::Focused, "testStrandSort" },
		{"--balanced-tree-only", "Focused balanced tree tests: ", TestCommandKind::Focused, "testBalancedTreeSorts" },
		{"--splay-only", "Focused Splay Tree tests: ", TestCommandKind::Focused, "testSplayTreeSort" },
	};

	const RegisteredTest* findRegisteredTest(const std::string& id)
	{
		for (std::size_t i = 0; i < sizeof(registeredTests) / sizeof(registeredTests[0]); ++i)
			if (id == registeredTests[i].id) return &registeredTests[i];
		return nullptr;
	}

	const TestCommand* findTestCommand(const char* argument)
	{
		if (!argument) return nullptr;
		for (std::size_t i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); ++i)
			if (argument == std::string(testCommands[i].argument)) return &testCommands[i];
		return nullptr;
	}

	void validateTestRegistry()
	{
		std::set<std::string> testIds;
		for (std::size_t i = 0; i < sizeof(registeredTests) / sizeof(registeredTests[0]); ++i)
			if (!testIds.insert(registeredTests[i].id).second)
				throw std::logic_error(std::string("Duplicate test registration: ") + registeredTests[i].id);
		std::set<std::string> commandIds;
		for (std::size_t i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); ++i)
		{
			if (!commandIds.insert(testCommands[i].argument).second)
				throw std::logic_error(std::string("Duplicate test command: ") + testCommands[i].argument);
			if (testCommands[i].kind != TestCommandKind::Focused) continue;
			const std::string ids = testCommands[i].testIds;
			std::size_t start = 0;
			while (start < ids.size())
			{
				const std::size_t comma = ids.find(',', start);
				const std::string id = ids.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				if (!findRegisteredTest(id)) throw std::logic_error(std::string("Unknown focused test: ") + id);
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
		}
	}

	void runRegisteredTest(const RegisteredTest& test)
	{
		activeStage = test.stage;
		test.run();
	}

	void runFocusedCommand(const TestCommand& command)
	{
		const std::string ids = command.testIds;
		std::size_t start = 0;
		while (start < ids.size())
		{
			const std::size_t comma = ids.find(',', start);
			const std::string id = ids.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			const RegisteredTest* test = findRegisteredTest(id);
			if (!test) throw std::logic_error(std::string("Unknown focused test: ") + id);
			runRegisteredTest(*test);
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
	}

}

using namespace sortinglogic_tests;

int main(int argc, char* argv[])
{
	try
	{
		validateTestRegistry();
		const RegisteredTest* coverage = findRegisteredTest("catalog-coverage");
		if (!coverage) throw std::logic_error("Catalog coverage test is not registered.");
		runRegisteredTest(*coverage);
		const TestCommand* command = argc > 1 ? findTestCommand(argv[1]) : nullptr;
		if (command && command->kind == TestCommandKind::CatalogCoverage)
		{
			std::cout << command->summary << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (command && command->kind == TestCommandKind::TraceMemoryProfile)
		{
			runTraceMemoryProfile();
			return EXIT_SUCCESS;
		}

		if (command && command->kind == TestCommandKind::Focused)
		{
			runFocusedCommand(*command);
			if (command->summary[0] != '\0')
				std::cout << command->summary << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		for (std::size_t i = 0; i < sizeof(registeredTests) / sizeof(registeredTests[0]); ++i)
			if (registeredTests[i].inFullSuite) runRegisteredTest(registeredTests[i]);
	}
	catch (const std::exception& error)
	{
		std::cerr << "SortingLogicTests aborted during " << activeStage << " after " << checks
			<< " checks: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	catch (...)
	{
		std::cerr << "SortingLogicTests aborted during " << activeStage << " by an unknown exception after " << checks << " checks.\n";
		return EXIT_FAILURE;
	}
	std::cout << "SortingLogicTests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
