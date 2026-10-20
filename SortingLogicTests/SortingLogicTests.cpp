#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"

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

namespace
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
		sortinglogic::InputDomain domain = sortinglogic::InputDomain::NormalizedFloats,
		sortinglogic::MergeWritebackMode mergeMode = sortinglogic::MergeWritebackMode::AtomicRangeWrite,
		sortinglogic::MergeExecutionMode mergeExecution = sortinglogic::MergeExecutionMode::BottomUp)
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
		std::vector<SortState> cursors;
		SortState state = trace.initialState;
		cursors.push_back(state);
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
					check(event.comparison == compareItems(first, second, direction), label + " stores exact comparator outcome");
				}
			}
			try { applyEventForward(state, event); }
			catch (const std::exception& e) { check(false, label + " replay failed at " + std::to_string(i) + ": " + e.what()); return; }
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
			check(state.metrics.comparisons == observedComparisons, label + " cursor metrics track comparison events");
			cursors.push_back(state);
		}
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
			if (snapshot->first < cursors.size()) check(sameState(snapshot->second, cursors[snapshot->first]), label + " stored snapshot matches its cursor");
		SortState reverse = state;
		for (std::size_t i = trace.events.size(); i > 0; --i)
		{
			try { applyEventBackward(reverse, trace.events[i - 1]); }
			catch (const std::exception& e) { check(false, label + " inverse replay failed: " + e.what()); return; }
			check(sameState(reverse, cursors[i - 1]), label + " inverse mutation reconstructs cursor " + std::to_string(i - 1));
		}
		check(sameState(reverse, trace.initialState), label + " rewinds to exact initial workspace");
	}

	void testMergeWritebackModes()
	{
		using namespace sortinglogic;
		const std::vector<SortItem> input = floats({ 0.8f, 0.1f, 0.6f, 0.2f, 0.4f, 0.3f });
		TraceBuildRequest atomicRequest;
		atomicRequest.input = input;
		atomicRequest.algorithm = SortAlgorithm::Merge;
		atomicRequest.mergeWritebackMode = MergeWritebackMode::AtomicRangeWrite;
		const SortTrace atomic = SortTraceBuilder::build(atomicRequest);
		check(atomic.familyId == "merge-sort" && atomic.variantId == "merge",
			"trace identifies its algorithm by stable family and variant strings");
		TraceBuildRequest stableIdRequest = atomicRequest;
		stableIdRequest.familyId = "merge-sort";
		stableIdRequest.variantId = "merge";
		check(SortTraceBuilder::build(stableIdRequest).familyId == stableIdRequest.familyId,
			"trace builder accepts matching stable family and variant identifiers");
		stableIdRequest.variantId = "unregistered";
		bool rejectedUnknownVariant = false;
		try { SortTraceBuilder::build(stableIdRequest); }
		catch (const std::invalid_argument&) { rejectedUnknownVariant = true; }
		check(rejectedUnknownVariant, "trace builder rejects unregistered stable variant identifiers");
		TraceBuildRequest animatedRequest = atomicRequest;
		animatedRequest.mergeWritebackMode = MergeWritebackMode::AnimatedWrites;
		const SortTrace animated = SortTraceBuilder::build(animatedRequest);
		TraceBuildRequest topDownAtomicRequest = atomicRequest;
		topDownAtomicRequest.mergeExecutionMode = MergeExecutionMode::TopDown;
		const SortTrace topDownAtomic = SortTraceBuilder::build(topDownAtomicRequest);
		TraceBuildRequest topDownAnimatedRequest = animatedRequest;
		topDownAnimatedRequest.mergeExecutionMode = MergeExecutionMode::TopDown;
		const SortTrace topDownAnimated = SortTraceBuilder::build(topDownAnimatedRequest);
		std::size_t atomicRangeWrites = 0, atomicItemWrites = 0, animatedItemWrites = 0, animatedRangeWrites = 0;
		for (std::size_t i = 0; i < atomic.events.size(); ++i)
		{
			if (atomic.events[i].type == SemanticEventType::WriteRange) ++atomicRangeWrites;
			if (atomic.events[i].type == SemanticEventType::WriteItem) ++atomicItemWrites;
		}
		for (std::size_t i = 0; i < animated.events.size(); ++i)
		{
			if (animated.events[i].type == SemanticEventType::WriteRange) ++animatedRangeWrites;
			if (animated.events[i].type == SemanticEventType::WriteItem) ++animatedItemWrites;
		}
		check(atomicRangeWrites > 0 && atomicItemWrites == 0 && animatedItemWrites > 0 && animatedRangeWrites == 0,
			"Merge writeback configuration switches between atomic ranges and item-by-item writes");
		SortState atomicEnd = atomic.initialState, animatedEnd = animated.initialState;
		for (std::size_t i = 0; i < atomic.events.size(); ++i) applyEventForward(atomicEnd, atomic.events[i]);
		for (std::size_t i = 0; i < animated.events.size(); ++i) applyEventForward(animatedEnd, animated.events[i]);
		check(isSorted(atomicEnd, SortDirection::Ascending) && isSorted(animatedEnd, SortDirection::Ascending) &&
			sameSlots(atomicEnd.slots, animatedEnd.slots) && atomicEnd.metrics.primaryWrites == animatedEnd.metrics.primaryWrites,
			"both Merge writeback modes produce the same final array and logical write count");
		SortState topDownAtomicEnd = topDownAtomic.initialState, topDownAnimatedEnd = topDownAnimated.initialState;
		for (std::size_t i = 0; i < topDownAtomic.events.size(); ++i) applyEventForward(topDownAtomicEnd, topDownAtomic.events[i]);
		for (std::size_t i = 0; i < topDownAnimated.events.size(); ++i) applyEventForward(topDownAnimatedEnd, topDownAnimated.events[i]);
		check(topDownAtomic.mergeExecutionMode == MergeExecutionMode::TopDown &&
			isSorted(topDownAtomicEnd, SortDirection::Ascending) && isSorted(topDownAnimatedEnd, SortDirection::Ascending) &&
			sameSlots(topDownAtomicEnd.slots, topDownAnimatedEnd.slots),
			"top-down Merge supports both independent writeback styles and reaches the same result");
		verifyAlgorithmCase("Merge atomic writeback", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Descending, InputDomain::NormalizedFloats, MergeWritebackMode::AtomicRangeWrite);
		verifyAlgorithmCase("Merge animated writeback", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Descending, InputDomain::NormalizedFloats, MergeWritebackMode::AnimatedWrites);
		verifyAlgorithmCase("Merge top-down atomic ascending", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Ascending, InputDomain::NormalizedFloats, MergeWritebackMode::AtomicRangeWrite, MergeExecutionMode::TopDown);
		verifyAlgorithmCase("Merge top-down atomic descending", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Descending, InputDomain::NormalizedFloats, MergeWritebackMode::AtomicRangeWrite, MergeExecutionMode::TopDown);
		verifyAlgorithmCase("Merge top-down animated ascending", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Ascending, InputDomain::NormalizedFloats, MergeWritebackMode::AnimatedWrites, MergeExecutionMode::TopDown);
		verifyAlgorithmCase("Merge top-down animated descending", input, InputProfile::NormalizedFloat, SortAlgorithm::Merge,
			SortDirection::Descending, InputDomain::NormalizedFloats, MergeWritebackMode::AnimatedWrites, MergeExecutionMode::TopDown);
		const AlgorithmDescriptor* mergeDescriptor = algorithmDescriptor(SortAlgorithm::Merge);
		bool hasExecutionGroup = false, hasWritebackGroup = false;
		for (std::size_t i = 0; mergeDescriptor && i < mergeDescriptor->supportedOptions.size(); ++i)
		{
			hasExecutionGroup = hasExecutionGroup || std::string(mergeDescriptor->supportedOptions[i].groupId) == "execution_mode";
			hasWritebackGroup = hasWritebackGroup || std::string(mergeDescriptor->supportedOptions[i].groupId) == "writeback_mode";
		}
		check(hasExecutionGroup && hasWritebackGroup, "Merge options remain separately grouped by stable metadata IDs");
	}

	void verifyProfilesAndAlgorithms()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > floatCases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f, 0.4f }, { 0.3f, 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }, { 0.5f, 0.4f, 0.3f, 0.2f, 0.1f }
		};
		const std::vector<std::vector<int> > integerCases = {
			{}, { 0 }, { 2, 0, 2, 0, 2 }, { 1, 1, 1, 1 }, { 0, 1, 2, 3, 4 }, { 4, 3, 2, 1, 0 }
		};
		std::vector<SortItem> randomFloat;
		std::vector<SortItem> randomInteger;
		std::mt19937 random(0xC0FFEEu);
		for (std::size_t i = 0; i < 22; ++i)
		{
			randomFloat.push_back(SortItem(500 + i, static_cast<float>(random() % 7) / 6.0f));
			randomInteger.push_back(SortItem(700 + i, static_cast<int>(random() % 22), static_cast<float>(random() % 22) / 22.0f));
		}
		std::shuffle(randomFloat.begin(), randomFloat.end(), random);
		std::shuffle(randomInteger.begin(), randomInteger.end(), random);
		std::vector<std::vector<SortItem> > floatInputs, integerInputs;
		for (std::size_t i = 0; i < floatCases.size(); ++i) floatInputs.push_back(floats(floatCases[i]));
		for (std::size_t i = 0; i < integerCases.size(); ++i) integerInputs.push_back(integers(integerCases[i]));
		floatInputs.push_back(randomFloat); integerInputs.push_back(randomInteger);
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		check(catalog.size() >= 36, "static catalog retains the original showcase and includes the first pinned distribution batch");
		const AlgorithmDescriptor* gnomeAudio = algorithmDescriptor(SortAlgorithm::Gnome);
		const AlgorithmDescriptor* insertionAudio = algorithmDescriptor(SortAlgorithm::Insertion);
		check(gnomeAudio && gnomeAudio->reverseAudioOperandOrder && insertionAudio && !insertionAudio->reverseAudioOperandOrder,
			"audio operand ordering is explicit in the catalog, including the reconstructed Gnome reversal");
		const AlgorithmDescriptor* bubble = algorithmDescriptor(SortAlgorithm::Bubble);
		const AlgorithmDescriptor* cocktail = algorithmDescriptor(SortAlgorithm::Cocktail);
		const AlgorithmDescriptor* insertion = algorithmDescriptor(SortAlgorithm::Insertion);
		const AlgorithmDescriptor* shell = algorithmDescriptor(SortAlgorithm::Shell);
		const AlgorithmDescriptor* counting = algorithmDescriptor(SortAlgorithm::Counting);
		const AlgorithmDescriptor* radix = algorithmDescriptor(SortAlgorithm::Radix);
		check(bubble && cocktail && bubble->familyId == cocktail->familyId && bubble->familyId == "exchange-sorts" &&
			bubble->variantId != cocktail->variantId, "related exchange sorts share a family but retain distinct variant IDs");
		check(insertion && shell && insertion->familyId == shell->familyId && insertion->familyId == "insertion-methods" &&
			insertion->variantId != shell->variantId, "Insertion and Shell methods share a family with distinct variants");
		check(counting && radix && counting->familyId == radix->familyId && counting->familyId == "distribution-sorts" &&
			counting->variantId != radix->variantId, "distribution sorts share a family with distinct variants");
		const AlgorithmDescriptor* shellFacts = algorithmDescriptor(SortAlgorithm::Shell);
		check(shellFacts && shellFacts->supportedOptions.size() == 4, "Shell catalog declares all four supported gap sequences");
		const AlgorithmDescriptor* baseNHeap = algorithmDescriptor(SortAlgorithm::ArrayvBaseNHeap);
		check(baseNHeap && baseNHeap->supportedOptions.size() == 5 &&
			std::string(baseNHeap->supportedOptions.front().groupId) == "heap_arity",
			"Base-N Heap declares bounded stable arity options");
		for (std::size_t a = 0; a < catalog.size(); ++a)
		{
			check(catalog[a].stableId && catalog[a].stableId[0] != '\0', "catalog algorithm has stable ID");
			check(!catalog[a].familyId.empty() && !catalog[a].variantId.empty(), "catalog algorithm has stable family and variant IDs");
			check(catalog[a].familyDisplayName && catalog[a].familyDisplayName[0] != '\0', "catalog algorithm has a family display name");
			check(catalog[a].safeMaxItemCount > 0 && catalog[a].safeMaxItemCount <= MaxItemCount && catalog[a].behavior,
				"catalog algorithm declares safe bounds and behavior class");
			check(catalog[a].timeComplexity && catalog[a].spaceComplexity && catalog[a].buildRequest != nullptr,
				"catalog provides algorithm facts and a request-specific strategy");
			for (std::size_t option = 0; option < catalog[a].supportedOptions.size(); ++option)
				check(catalog[a].supportedOptions[option].stableId && catalog[a].supportedOptions[option].displayName,
					"catalog option has stable ID and display name");
			for (std::size_t metric = 0; metric < catalog[a].metrics.size(); ++metric)
				check(catalog[a].metrics[metric].key && catalog[a].metrics[metric].displayName,
					"catalog metric has a key and description");
			for (std::size_t c = 0; c < floatInputs.size(); ++c)
			{
				if (!catalog[a].supportsNormalizedFloat) continue;
				for (int d = 0; d < 2; ++d)
					verifyAlgorithmCase(std::string(catalog[a].displayName) + " float case " + std::to_string(c), floatInputs[c],
						InputProfile::NormalizedFloat, catalog[a].id, d == 0 ? SortDirection::Ascending : SortDirection::Descending);
			}
			if (catalog[a].supportsIntegerKeys)
			{
				std::vector<InputDomain> domains;
				if (catalog[a].supportedDomains.empty()) domains.push_back(InputDomain::NonnegativeIntegerKeys);
				else domains = catalog[a].supportedDomains;
				for (std::size_t dIndex = 0; dIndex < domains.size(); ++dIndex)
				{
					const InputDomain domain = domains[dIndex];
					for (std::size_t c = 0; c < integerInputs.size(); ++c)
					{
						std::vector<SortItem> input = integerInputs[c];
						if (domain == InputDomain::SignedIntegerKeys)
							for (std::size_t i = 0; i < input.size(); ++i)
							{
								const int key = input[i].integerKey - static_cast<int>(input.size() / 2);
								input[i] = SortItem(input[i].stableId, key, input[i].value);
							}
						else if (domain == InputDomain::IntegerPermutation)
						{
							std::vector<int> keys(input.size());
							for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = static_cast<int>(i);
							std::shuffle(keys.begin(), keys.end(), random);
							for (std::size_t i = 0; i < input.size(); ++i)
								input[i] = SortItem(input[i].stableId, keys[i], input.empty() ? 0.0f : static_cast<float>(keys[i]) / static_cast<float>(input.size()));
						}
						for (int d = 0; d < 2; ++d)
							verifyAlgorithmCase(std::string(catalog[a].displayName) + " integer case " + std::to_string(c) + " domain " + inputDomainName(domain), input,
								InputProfile::IntegerKeys, catalog[a].id, d == 0 ? SortDirection::Ascending : SortDirection::Descending, domain);
					}
				}
			}
		}

		// Exercise every offered heap arity in both directions. The broad catalog
		// pass above covers the default base only; this verifies the user-selectable
		// arities actually reach the trace builder and remain exactly reversible.
		for (int arityIndex = 0; arityIndex < 5; ++arityIndex)
		{
			const int arities[] = { 2, 3, 4, 8, 16 };
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				TraceBuildRequest request;
				request.algorithm = SortAlgorithm::ArrayvBaseNHeap;
				request.familyId = "heap-sorts";
				request.variantId = "base-n";
				request.input = randomInteger;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.bucketCount = arities[arityIndex];
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t event = 0; event < trace.events.size(); ++event)
					applyEventForward(replay, trace.events[event]);
				std::string reason;
				check(trace.complete && trace.bucketCount == arities[arityIndex] &&
					isSorted(replay, request.direction) && validateState(replay, request.input.size(), &reason),
					"Base-N Heap arity " + std::to_string(arities[arityIndex]) + " sorts and preserves identities");
				for (std::size_t event = trace.events.size(); event > 0; --event)
					applyEventBackward(replay, trace.events[event - 1]);
				check(sameState(replay, trace.initialState),
					"Base-N Heap arity " + std::to_string(arities[arityIndex]) + " rewinds exactly");
			}
		}
	}

	void testBurntPancakeOrientation()
	{
		using namespace sortinglogic;
		const std::vector<SortItem> input = integers({ 5, 1, 4, 0, 3, 2, 2 });
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvBurntPancake, SortAlgorithm::CanonicalBurntPancake };
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[a];
				request.input = input;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				std::size_t orientationChanges = 0;
				for (std::size_t event = 0; event < trace.events.size(); ++event)
				{
					orientationChanges += trace.events[event].type == SemanticEventType::SetOrientation ? 1u : 0u;
					applyEventForward(replay, trace.events[event]);
				}
				bool allUpright = true;
				for (std::size_t i = 0; i < replay.slots.size(); ++i)
					allUpright = allUpright && replay.slots[i].occupied && !replay.slots[i].item.orientationReversed;
				std::string reason;
				check(trace.complete && isSorted(replay, request.direction) && allUpright &&
					validateState(replay, input.size(), &reason),
					std::string(algorithmDisplayName(algorithms[a])) + " sorts values and finishes every pancake upright");
				check(orientationChanges > 0, std::string(algorithmDisplayName(algorithms[a])) + " records visible orientation changes");
				for (std::size_t event = trace.events.size(); event > 0; --event)
					applyEventBackward(replay, trace.events[event - 1]);
				check(sameState(replay, trace.initialState),
					std::string(algorithmDisplayName(algorithms[a])) + " rewinds orientation and item order exactly");
			}
	}

	void testSortingNetworkSchedules()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvBitonicIterative, SortAlgorithm::ArrayvBitonicRecursive,
			SortAlgorithm::ArrayvBitonicParallel, SortAlgorithm::ArrayvBoseNelsonIterative,
			SortAlgorithm::ArrayvBoseNelsonRecursive, SortAlgorithm::ArrayvBoseNelsonParallel,
			SortAlgorithm::ArrayvOddEvenMergeIterative, SortAlgorithm::ArrayvOddEvenMergeRecursive,
			SortAlgorithm::ArrayvOddEvenMergeParallel, SortAlgorithm::ArrayvDiamondIterative,
			SortAlgorithm::ArrayvDiamondRecursive, SortAlgorithm::ArrayvPairwiseIterative,
			SortAlgorithm::ArrayvPairwiseRecursive, SortAlgorithm::ArrayvCrease,
			SortAlgorithm::ArrayvFold, SortAlgorithm::ArrayvMergeExchangeIterative,
			SortAlgorithm::ArrayvPairwiseMergeIterative, SortAlgorithm::ArrayvPairwiseMergeRecursive,
			SortAlgorithm::ArrayvWeaveIterative, SortAlgorithm::ArrayvWeaveParallel,
			SortAlgorithm::ArrayvWeaveRecursive, SortAlgorithm::ArrayvCompleteGraph
		};
		const std::vector<SortItem> input = integers(std::vector<int>{ 9, 1, 7, 3, 8, 2, 5, 0, 6, 4, 4 });
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithmIndex)
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[algorithmIndex];
				request.input = input;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				std::set<int> openPhases;
				std::size_t comparisonCount = 0;
				for (std::size_t eventIndex = 0; eventIndex < trace.events.size(); ++eventIndex)
				{
					const SemanticEvent& event = trace.events[eventIndex];
					if (event.type == SemanticEventType::PhaseBegin)
						check(openPhases.insert(event.groupId).second, "sorting network phase IDs are unique");
					else if (event.type == SemanticEventType::PhaseEnd)
						check(openPhases.erase(event.groupId) == 1, "sorting network phase ends match a live phase");
					if (event.type == SemanticEventType::Compare) ++comparisonCount;
					applyEventForward(replay, event);
				}
				const std::map<std::string, std::uint64_t>::const_iterator layers = replay.metrics.algorithm.find("network_layers");
				const std::map<std::string, std::uint64_t>::const_iterator comparators = replay.metrics.algorithm.find("network_comparators");
				std::string reason;
				check(trace.complete && isSorted(replay, request.direction) && validateState(replay, input.size(), &reason),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " completes the irregular duplicate-key network case");
				check(comparisonCount > 0 && layers != replay.metrics.algorithm.end() && layers->second > 0 &&
					comparators != replay.metrics.algorithm.end() && comparators->second > 0,
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " exposes comparator-layer and comparator metrics");
				check(openPhases.empty(), std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " closes all nested phase markers");
				for (std::size_t eventIndex = trace.events.size(); eventIndex > 0; --eventIndex)
					applyEventBackward(replay, trace.events[eventIndex - 1]);
				check(sameState(replay, trace.initialState),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " rewinds its full network trace exactly");
			}
	}

	void testMatrixSortShapes()
	{
		using namespace sortinglogic;
		const std::size_t lengths[] = { 17, 25, 31, 36, 49, 65 };
		std::mt19937 random(0x4D415452u);
		for (std::size_t lengthIndex = 0; lengthIndex < sizeof(lengths) / sizeof(lengths[0]); ++lengthIndex)
		{
			std::vector<int> values(lengths[lengthIndex]);
			for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<int>(random() % 17);
			verifyAlgorithmCase("Matrix Sort shape " + std::to_string(values.size()), integers(values),
				InputProfile::IntegerKeys, SortAlgorithm::ArrayvMatrix, SortDirection::Ascending,
				InputDomain::NonnegativeIntegerKeys);
			verifyAlgorithmCase("Matrix Sort reverse shape " + std::to_string(values.size()), integers(values),
				InputProfile::IntegerKeys, SortAlgorithm::ArrayvMatrix, SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);
		}
	}

	void testIncompleteStoogeOutcome()
	{
		using namespace sortinglogic;
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvOptimizedStooge;
		request.profile = InputProfile::IntegerKeys;
		request.domain = InputDomain::NonnegativeIntegerKeys;
		request.input = integers(std::vector<int>{ 4, 3, 2, 1, 0 });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState state = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(state, trace.events[i]);
		std::string reason;
		check(trace.complete && trace.outcome == AlgorithmOutcome::IncompleteDemonstration &&
			!isSorted(state, request.direction) && validateState(state, request.input.size(), &reason),
			"the pinned optimized Stooge demonstration reports its unsorted result without calling it a completed sort");
		for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(state, trace.events[i - 1]);
		check(sameState(state, trace.initialState), "the incomplete Stooge trace remains exactly reversible");
	}

	void testStoogeFamilyVariants()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x57006Eu);
		std::vector<int> values(11);
		for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<int>(random() % 7);
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvOptimizedStoogeStudio, SortAlgorithm::ArrayvQuadStooge, SortAlgorithm::ArrayvSlow
		};
		for (std::size_t i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); ++i)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[i])) + " randomized duplicate keys",
					integers(values), InputProfile::IntegerKeys, algorithms[i], direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
	}

	void testRecursiveExchangeNoveltyVariants()
	{
		using namespace sortinglogic;
		std::mt19937 random(0xC1AC10D);
		std::vector<int> values(11);
		for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<int>(random() % 7);
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvSilly, SortAlgorithm::ArrayvSnuffle };
		for (std::size_t i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); ++i)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[i])) + " randomized duplicate keys",
					integers(values), InputProfile::IntegerKeys, algorithms[i], direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
	}

	void testFunSortBoundedOutcome()
	{
		using namespace sortinglogic;
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvFun;
		request.input = floats({ 0.4f, 0.2f, 0.4f, 0.2f, 0.4f });
		request.profile = InputProfile::NormalizedFloat;
		request.direction = SortDirection::Ascending;
		const SortTrace trace = SortTraceBuilder::build(request);
		check(trace.complete && trace.outcome == AlgorithmOutcome::IncompleteDemonstration,
			"Fun Sort is explicitly presented as a completed trace of an incomplete method");
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvFun, 64) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvFun, 65),
			"Fun Sort enforces its conservative catalog item limit");
		SortState replay = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
		std::string reason;
		check(validateState(replay, request.input.size(), &reason), "Fun Sort retains every item despite its incomplete ordering contract");
		check(!isSorted(replay, request.direction), "Fun Sort fixture captures the pinned method's incomplete duplicate-key ordering");
		for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
		check(sameState(replay, trace.initialState), "Fun Sort trace rewinds exactly");
	}

	std::uint64_t distributionFixtureHash(const sortinglogic::SortTrace& trace, bool includeAudio)
	{
		std::uint64_t hash = 1469598103934665603ull;
		const auto mix = [&hash](std::uint64_t value)
		{
			for (int byte = 0; byte < 8; ++byte)
			{
				hash ^= (value >> (byte * 8)) & 0xFFu;
				hash *= 1099511628211ull;
			}
		};
		const auto mixString = [&mix](const std::string& value)
		{
			mix(value.size());
			for (std::size_t i = 0; i < value.size(); ++i) mix(static_cast<unsigned char>(value[i]));
		};
		const auto mixIndex = [&mix](std::size_t value)
		{
			mix(value == sortinglogic::NoGapIndex ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(value));
		};
		mix(trace.events.size());
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const sortinglogic::SemanticEvent& event = trace.events[i];
			mix(static_cast<unsigned int>(event.type)); mixIndex(event.firstIndex); mixIndex(event.secondIndex);
			mix(event.firstItemId); mix(event.secondItemId); mix(static_cast<int>(event.comparison));
			mix(static_cast<unsigned int>(event.firstRole)); mix(static_cast<unsigned int>(event.secondRole));
			mixString(event.firstLane); mixString(event.secondLane); mixString(event.workspaceName);
			mix(event.item.stableId); mix(static_cast<std::uint32_t>(event.item.integerKey));
			mix(static_cast<std::uint32_t>(event.beforeCounter)); mix(static_cast<std::uint32_t>(event.afterCounter));
			mix(event.beforeOccupied); mix(event.afterSize); mix(event.metricDelta); mix(static_cast<std::uint32_t>(event.groupId));
		}
		if (includeAudio)
		{
			mix(trace.audioEventIndices.size());
			for (std::size_t i = 0; i < trace.audioEventIndices.size(); ++i)
			{
				const std::size_t index = trace.audioEventIndices[i];
				const sortinglogic::SemanticEvent& event = trace.events[index];
				mix(index); mix(static_cast<unsigned int>(event.type)); mix(event.firstItemId); mix(event.secondItemId);
				mix(static_cast<unsigned int>(event.firstRole)); mix(static_cast<unsigned int>(event.secondRole));
			}
		}
		return hash;
	}

	void testDistributionAudioBaselines()
	{
		using namespace sortinglogic;
		const int keys[] = { 4, 0, 3, 3, 1, 5, 2, 0 };
		const std::uint64_t eventHashes[2][2] = {
			{ 3974441366071336120ull, 1076346355219668063ull },
			{ 10088327461051149393ull, 10565943430501191950ull }
		};
		const std::uint64_t audioHashes[2][2] = {
			{ 7999207750143543800ull, 15660322577698318975ull },
			{ 1736781132210867113ull, 14224815798361274518ull }
		};
		const std::size_t eventCounts[2] = { 48, 79 };
		const std::size_t audioCounts[2] = { 16, 32 };
		for (int algorithm = 0; algorithm < 2; ++algorithm)
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.algorithm = algorithm == 0 ? SortAlgorithm::Counting : SortAlgorithm::Radix;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.inputSeed = 0x13579BDFu;
				for (std::size_t i = 0; i < 8; ++i)
					request.input.push_back(SortItem(100 + i, keys[i], static_cast<float>(keys[i] + 1) / 8.0f));
				const SortTrace trace = SortTraceBuilder::build(request);
				check(trace.events.size() == eventCounts[algorithm] && trace.audioEventIndices.size() == audioCounts[algorithm],
					"Counting/Radix baseline event and audio-eligibility counts stay unchanged");
				const std::uint64_t actualEventHash = distributionFixtureHash(trace, false);
				const std::uint64_t actualAudioHash = distributionFixtureHash(trace, true);
				check(actualEventHash == eventHashes[algorithm][direction] && actualAudioHash == audioHashes[algorithm][direction],
					"Counting/Radix deterministic event order and audio operands match the frozen fixture");
			}
	}

	void testStochasticNoveltyVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvBozo, SortAlgorithm::ArrayvBubbleBogo, SortAlgorithm::ArrayvExchangeBogo };
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithmIndex)
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[algorithmIndex];
				request.input = integers(directionIndex == 0 ? std::vector<int>{ 4, 3, 2, 1, 0 } : std::vector<int>{ 0, 1, 2, 3, 4 });
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.inputSeed = 0x6B6F676Fu;
				const SortTrace first = SortTraceBuilder::build(request);
				const SortTrace second = SortTraceBuilder::build(request);
				check(first.complete && (first.outcome == AlgorithmOutcome::SortedPermutation ||
					first.outcome == AlgorithmOutcome::SortedStablePermutation),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " completes its seeded small permutation run");
				check(distributionFixtureHash(first, true) == distributionFixtureHash(second, true),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " reproduces the same event trace for a fixed seed");
				SortState replay = first.initialState;
				for (std::size_t event = 0; event < first.events.size(); ++event) applyEventForward(replay, first.events[event]);
				const std::map<std::string, std::uint64_t>::const_iterator attempts = replay.metrics.algorithm.find("random_attempts");
				std::string reason;
				check(isSorted(replay, request.direction) && validateState(replay, request.input.size(), &reason) &&
					attempts != replay.metrics.algorithm.end() && attempts->second > 0,
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " sorts, preserves identity, and reports random attempts");
				for (std::size_t event = first.events.size(); event > 0; --event) applyEventBackward(replay, first.events[event - 1]);
				check(sameState(replay, first.initialState),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " seeded trace rewinds exactly");
				check(algorithmAcceptsItemCount(algorithms[algorithmIndex], 8) && !algorithmAcceptsItemCount(algorithms[algorithmIndex], 9),
					std::string(algorithmDisplayName(algorithms[algorithmIndex])) + " enforces the 8-item stochastic limit");
			}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvBozo;
		partialRequest.input = integers({ 7, 6, 5, 4, 3, 2, 1, 0 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.inputSeed = 0x12345u;
		partialRequest.maxOperations = 18;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		check(!partial.complete && partial.completionReason.find("operation budget") != std::string::npos,
			"Bozo Sort returns a visible playable partial trace when its operation budget is exhausted");
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		std::string partialReason;
		check(validateState(partialState, partialRequest.input.size(), &partialReason),
			"budget-limited stochastic trace retains a valid item permutation");
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(sameState(partialState, partial.initialState), "budget-limited stochastic trace rewinds exactly");
	}

	void testAdditionalBogoVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvDeterministicBogo,
			SortAlgorithm::ArrayvSelectionBogo,
			SortAlgorithm::ArrayvLessBogo,
			SortAlgorithm::ArrayvCocktailBogo,
			SortAlgorithm::ArrayvMedianQuickBogo,
			SortAlgorithm::ArrayvQuickBogo,
			SortAlgorithm::ArrayvRandomGuess,
			SortAlgorithm::ArrayvOptimizedGuess,
			SortAlgorithm::ArrayvSmartGuess,
			SortAlgorithm::ArrayvGuess
		};
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			for (int d = 0; d < 2; ++d)
			{
				const SortDirection direction = d == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const std::vector<SortItem> input = integers({ 3, 1, 0, 2 });
				TraceBuildRequest request;
				request.algorithm = algorithms[a];
				request.input = input;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction;
				request.inputSeed = 0x5EED1234u;
				const std::uint32_t seed = request.inputSeed;
				const SortTrace first = SortTraceBuilder::build(request);
				request.inputSeed = algorithms[a] == SortAlgorithm::ArrayvDeterministicBogo ? seed ^ 0xFFFFFFFFu : seed;
				const SortTrace alternateSeed = SortTraceBuilder::build(request);
				check(first.complete && (first.outcome == AlgorithmOutcome::SortedPermutation ||
					first.outcome == AlgorithmOutcome::SortedStablePermutation),
					std::string(algorithmDisplayName(algorithms[a])) + " completes its " + std::to_string(input.size()) + "-item " +
					(d == 0 ? "ascending" : "descending") + " run" + (first.complete ? std::string() : ": " + first.completionReason +
					" (" + std::to_string(first.operationCount) + " operations)"));
				check(distributionFixtureHash(first, false) == distributionFixtureHash(alternateSeed, false),
					std::string(algorithmDisplayName(algorithms[a])) + (algorithms[a] == SortAlgorithm::ArrayvDeterministicBogo
						? " ignores the seed and preserves its deterministic permutation schedule"
						: " reproduces the same seeded trace"));
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " focused " + (d == 0 ? "ascending" : "descending"),
					input, InputProfile::IntegerKeys, algorithms[a], direction, InputDomain::NonnegativeIntegerKeys);
				const char* metricKey = algorithms[a] == SortAlgorithm::ArrayvDeterministicBogo ? "permutation_checks" :
					algorithms[a] == SortAlgorithm::ArrayvSelectionBogo ? "random_attempts" :
					algorithms[a] == SortAlgorithm::ArrayvQuickBogo ? "partitions" :
					algorithms[a] == SortAlgorithm::ArrayvRandomGuess ? "random_guesses" :
					(algorithms[a] == SortAlgorithm::ArrayvOptimizedGuess || algorithms[a] == SortAlgorithm::ArrayvSmartGuess ||
						algorithms[a] == SortAlgorithm::ArrayvGuess) ? "candidate_tuples" : "random_shuffles";
				const std::map<std::string, std::uint64_t>::const_iterator counter = first.snapshots.rbegin()->second.metrics.algorithm.find(metricKey);
				check(counter != first.snapshots.rbegin()->second.metrics.algorithm.end() && counter->second > 0,
					std::string(algorithmDisplayName(algorithms[a])) + " exposes its algorithm-specific effort metric");
			}
		}

		for (std::size_t a = 1; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			TraceBuildRequest limited;
			limited.algorithm = algorithms[a];
			limited.input = algorithms[a] == SortAlgorithm::ArrayvGuess ? integers({ 3, 2, 1, 0 }) : integers({ 4, 3, 2, 1, 0 });
			limited.profile = InputProfile::IntegerKeys;
			limited.domain = InputDomain::NonnegativeIntegerKeys;
			limited.inputSeed = 0x41414141u;
			limited.maxOperations = 1;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState state = partial.initialState;
			for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(state, partial.events[event]);
			std::string reason;
			check(!partial.complete && validateState(state, limited.input.size(), &reason),
				std::string(algorithmDisplayName(algorithms[a])) + " stops at the operation leash with a valid playable prefix");
			for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(state, partial.events[event - 1]);
			check(sameState(state, partial.initialState),
				std::string(algorithmDisplayName(algorithms[a])) + " operation-limited trace rewinds exactly");
		}

		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvDeterministicBogo, 5) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvDeterministicBogo, 6),
			"Deterministic Bogo Sort declares the five-item factorial demonstration ceiling");
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvSelectionBogo, 8) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvSelectionBogo, 9) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvLessBogo, 8) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvLessBogo, 9) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvCocktailBogo, 8) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvCocktailBogo, 9) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvMedianQuickBogo, 8) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvMedianQuickBogo, 9) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvQuickBogo, 5) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvQuickBogo, 6),
			"randomized novelty variants declare conservative item ceilings");
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvRandomGuess, 5) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvRandomGuess, 6) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvOptimizedGuess, 5) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvOptimizedGuess, 6) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvSmartGuess, 6) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvSmartGuess, 7) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvGuess, 4) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvGuess, 5),
			"guess-family strategies declare their per-variant exhaustive-search ceilings");
	}

	void testPointerQuicksortVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvLLQuick, SortAlgorithm::ArrayvLRQuick, SortAlgorithm::ArrayvLRQuickParallel };
		const std::vector<SortItem> duplicates = integers({ 5, 1, 4, 1, 3, 0, 2, 5, 2, 0, 4, 3 });
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
			for (int d = 0; d < 2; ++d)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " duplicate-pointer case " + (d == 0 ? "ascending" : "descending"),
					duplicates, InputProfile::IntegerKeys, algorithms[a], d == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest parallelRequest;
		parallelRequest.algorithm = SortAlgorithm::ArrayvLRQuickParallel;
		parallelRequest.input = duplicates;
		parallelRequest.profile = InputProfile::IntegerKeys;
		parallelRequest.domain = InputDomain::NonnegativeIntegerKeys;
		parallelRequest.inputSeed = 0x51554943u;
		const SortTrace parallel = SortTraceBuilder::build(parallelRequest);
		std::set<int> openGroups;
		bool balanced = true;
		for (std::size_t i = 0; i < parallel.events.size(); ++i)
		{
			const SemanticEvent& event = parallel.events[i];
			if (event.type == SemanticEventType::PhaseBegin) balanced = balanced && openGroups.insert(event.groupId).second;
			else if (event.type == SemanticEventType::PhaseEnd) balanced = balanced && openGroups.erase(event.groupId) == 1;
		}
		check(parallel.complete && balanced && openGroups.empty() && !parallel.annotations.empty(),
			"parallel pointer Quick Sort retains balanced recursive phase groups and partition annotations");
		const SortMetrics& metrics = parallel.snapshots.rbegin()->second.metrics;
		check(metrics.algorithm.find("partitions") != metrics.algorithm.end() && metrics.algorithm.find("partitions")->second > 0 &&
			metrics.algorithm.find("maximum_depth") != metrics.algorithm.end() && metrics.algorithm.find("maximum_depth")->second > 0,
			"pointer Quick Sort variants expose partition and recursion-depth metrics");
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvLLQuick, 128) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvLLQuick, 129),
			"left-left Quick Sort applies its recursion safety limit");
	}

	void testNestedBogoVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvBogoBogo, SortAlgorithm::ArrayvMergeBogo, SortAlgorithm::ArrayvSmartBogoBogo };
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
			for (int d = 0; d < 2; ++d)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[a];
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = d == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.inputSeed = 0xB060B060u + static_cast<std::uint32_t>(a * 17 + d);
				request.input = algorithms[a] == SortAlgorithm::ArrayvMergeBogo
					? integers({ 4, 1, 3, 0, 2 }) : integers({ 2, 0, 1 });
				const SortTrace first = SortTraceBuilder::build(request);
				const SortTrace repeat = SortTraceBuilder::build(request);
				check(first.complete && (first.outcome == AlgorithmOutcome::SortedPermutation ||
					first.outcome == AlgorithmOutcome::SortedStablePermutation),
					std::string(algorithmDisplayName(algorithms[a])) + " completes its bounded " +
					(d == 0 ? "ascending" : "descending") + " example" +
					(first.complete ? std::string() : ": " + first.completionReason));
				check(distributionFixtureHash(first, true) == distributionFixtureHash(repeat, true),
					std::string(algorithmDisplayName(algorithms[a])) + " reproduces its seeded trace");
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " nested novelty replay",
					request.input, request.profile, algorithms[a], request.direction, request.domain);

				TraceBuildRequest limited = request;
				limited.maxOperations = 1;
				const SortTrace partial = SortTraceBuilder::build(limited);
				SortState state = partial.initialState;
				for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
				std::string reason;
				check(!partial.complete && validateState(state, limited.input.size(), &reason),
					std::string(algorithmDisplayName(algorithms[a])) + " exposes a valid prefix at the operation leash");
				for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
				check(sameState(state, partial.initialState),
					std::string(algorithmDisplayName(algorithms[a])) + " partially generated trace rewinds exactly");
			}
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvBogoBogo, 3) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvBogoBogo, 4) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvSmartBogoBogo, 3) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvSmartBogoBogo, 4) &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvMergeBogo, 8) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvMergeBogo, 9),
			"nested Bogo methods and randomized Merge Bogo declare conservative item ceilings");
	}

	void testDualPivotQuickVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvDualPivotQuick, SortAlgorithm::ArrayvOptimizedDualPivotQuick };
		std::vector<int> duplicateValues;
		for (int i = 0; i < 72; ++i) duplicateValues.push_back(i % 2);
		duplicateValues[24] = 0;
		duplicateValues[47] = 1;
		const std::vector<SortItem> duplicates = integers(duplicateValues);
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
			for (int d = 0; d < 2; ++d)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " duplicate-heavy partition replay",
					duplicates, InputProfile::IntegerKeys, algorithms[a], d == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest optimized;
		optimized.algorithm = SortAlgorithm::ArrayvOptimizedDualPivotQuick;
		optimized.input = duplicates;
		optimized.profile = InputProfile::IntegerKeys;
		optimized.domain = InputDomain::NonnegativeIntegerKeys;
		optimized.inputSeed = 0xD0A1B170u;
		const SortTrace trace = SortTraceBuilder::build(optimized);
		const SortTrace repeat = SortTraceBuilder::build(optimized);
		const SortMetrics& metrics = trace.snapshots.rbegin()->second.metrics;
		check(trace.complete && !trace.annotations.empty() && distributionFixtureHash(trace, true) == distributionFixtureHash(repeat, true),
			"optimized Dual-Pivot Quick Sort deterministically records its partition regions");
		check(metrics.algorithm.find("partitions") != metrics.algorithm.end() && metrics.algorithm.find("partitions")->second > 0 &&
			metrics.algorithm.find("maximum_depth") != metrics.algorithm.end(),
			"Dual-Pivot Quick Sort variants expose partition depth statistics");
		check(metrics.algorithm.find("equal_key_moves") != metrics.algorithm.end() && metrics.algorithm.find("equal_key_moves")->second > 0,
			"optimized dual-pivot run exercises and counts duplicate-compaction moves");

		TraceBuildRequest limited = optimized;
		limited.maxOperations = 1;
		const SortTrace partial = SortTraceBuilder::build(limited);
		SortState state = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
		check(!partial.complete && sameState(state, partial.initialState), "dual-pivot operation-limited prefixes rewind exactly");
	}

	void testStableQuickVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvStableQuick,
			SortAlgorithm::ArrayvStableQuickParallel,
			SortAlgorithm::ArrayvForcedStableQuick
		};
		std::vector<int> values;
		for (int i = 0; i < 80; ++i) values.push_back((i * 19 + i / 5) % 9);
		std::mt19937 random(0x57AB1E00u);
		std::shuffle(values.begin(), values.end(), random);
		const std::vector<SortItem> input = integers(values);
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
			for (int d = 0; d < 2; ++d)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " stable duplicate-heavy replay",
					input, InputProfile::IntegerKeys, algorithms[a], d == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest parallel;
		parallel.algorithm = SortAlgorithm::ArrayvStableQuickParallel;
		parallel.input = input;
		parallel.profile = InputProfile::IntegerKeys;
		parallel.domain = InputDomain::NonnegativeIntegerKeys;
		parallel.inputSeed = 0x57AB1E00u;
		const SortTrace parallelTrace = SortTraceBuilder::build(parallel);
		const SortTrace parallelRepeat = SortTraceBuilder::build(parallel);
		std::set<int> openGroups;
		bool balanced = true;
		for (std::size_t i = 0; i < parallelTrace.events.size(); ++i)
		{
			const SemanticEvent& event = parallelTrace.events[i];
			if (event.type == SemanticEventType::PhaseBegin) balanced = balanced && openGroups.insert(event.groupId).second;
			else if (event.type == SemanticEventType::PhaseEnd) balanced = balanced && openGroups.erase(event.groupId) == 1;
		}
		check(parallelTrace.complete && balanced && openGroups.empty() && !parallelTrace.annotations.empty() &&
			distributionFixtureHash(parallelTrace, true) == distributionFixtureHash(parallelRepeat, true),
			"stable parallel Quick Sort uses deterministic pivot choices and balanced recursive phase groups");

		TraceBuildRequest forced = parallel;
		forced.algorithm = SortAlgorithm::ArrayvForcedStableQuick;
		const SortTrace forcedTrace = SortTraceBuilder::build(forced);
		const SortMetrics& forcedMetrics = forcedTrace.snapshots.rbegin()->second.metrics;
		check(forcedMetrics.algorithm.find("stable_tie_breaks") != forcedMetrics.algorithm.end() && forcedMetrics.algorithm.find("stable_tie_breaks")->second > 0,
			"Forced-Stable Quick Sort exercises its original-position tie key");
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			TraceBuildRequest limited = parallel;
			limited.algorithm = algorithms[a];
			limited.maxOperations = 1;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState state = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
			check(!partial.complete && sameState(state, partial.initialState),
				std::string(algorithmDisplayName(algorithms[a])) + " operation-limited prefix rewinds exactly");
		}
	}

	void testMergeFamilyVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvBinaryMerge,
			SortAlgorithm::ArrayvOptimizedBottomUpMerge
		};
		std::vector<int> values;
		for (int i = 0; i < 127; ++i) values.push_back((i * 37 + i / 3) % 17);
		std::mt19937 random(0xB1A4E123u);
		std::shuffle(values.begin(), values.end(), random);
		const std::vector<SortItem> input = integers(values);
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			for (int d = 0; d < 2; ++d)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " duplicate-heavy replay",
					input, InputProfile::IntegerKeys, algorithms[a], d == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[a]);
			check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable && !descriptor->inPlace,
				std::string(algorithmDisplayName(algorithms[a])) + " publishes stable merge-family metadata");
		}
		TraceBuildRequest binary;
		binary.algorithm = SortAlgorithm::ArrayvBinaryMerge;
		binary.input = input;
		binary.profile = InputProfile::IntegerKeys;
		binary.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace binaryTrace = SortTraceBuilder::build(binary);
		const SortMetrics& binaryMetrics = binaryTrace.snapshots.rbegin()->second.metrics;
		check(binaryMetrics.algorithm.find("binary_insertion_runs") != binaryMetrics.algorithm.end() &&
			binaryMetrics.algorithm.find("merged_ranges") != binaryMetrics.algorithm.end(),
			"Binary Merge Sort records insertion runs and merged ranges");
		TraceBuildRequest optimized = binary;
		optimized.algorithm = SortAlgorithm::ArrayvOptimizedBottomUpMerge;
		const SortTrace optimizedTrace = SortTraceBuilder::build(optimized);
		const SortMetrics& optimizedMetrics = optimizedTrace.snapshots.rbegin()->second.metrics;
		check(optimizedMetrics.algorithm.find("binary_insertion_runs") != optimizedMetrics.algorithm.end() &&
			optimizedMetrics.algorithm.find("merge_passes") != optimizedMetrics.algorithm.end() &&
			optimizedMetrics.algorithm.find("merge_passes")->second >= 2,
			"Optimized Bottom-Up Merge records 16-item run formation and paired buffer passes");
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			TraceBuildRequest limited = binary;
			limited.algorithm = algorithms[a];
			limited.maxOperations = 1;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState state = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
			check(!partial.complete && sameState(state, partial.initialState),
				std::string(algorithmDisplayName(algorithms[a])) + " partial operation budget rewinds exactly");
		}
	}

	void testIterativeTopDownMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x17D09A31u);
		for (std::size_t size = 0; size <= 65; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 13) / 13.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Iterative Top-down Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvIterativeTopDownMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvIterativeTopDownMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "iterative-top-down" &&
			descriptor->stable && !descriptor->inPlace,
			"Iterative Top-down Merge publishes its distinct stable workspace-backed catalog identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvIterativeTopDownMerge;
		request.input = floats({ 0.91f, 0.04f, 0.67f, 0.13f, 0.41f, 0.75f, 0.22f, 0.58f, 0.31f,
			0.82f, 0.09f, 0.49f, 0.36f, 0.63f, 0.18f, 0.95f, 0.27f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator passes = finalState.metrics.algorithm.find("merge_passes");
		check(trace.complete && passes != finalState.metrics.algorithm.end() && passes->second == 5 &&
			phaseBegins == 5 && phaseEnds == 5,
			"Iterative Top-down Merge emits five balanced layers for 17 items and closes each phase");

		request.maxOperations = 8;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Iterative Top-down Merge partial layer rewinds exactly");
	}

	void testInPlaceMergeSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.5f });
		cases.push_back(std::vector<float>(9, 0.4f));
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f });
		cases.push_back(std::vector<float>{ 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		std::mt19937 random(0x1A91CE55u);
		std::vector<float> randomized;
		for (int i = 0; i < 65; ++i) randomized.push_back(static_cast<float>(random() % 11) / 11.0f);
		cases.push_back(randomized);
		for (std::size_t input = 0; input < cases.size(); ++input)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("In-Place Merge input " + std::to_string(input), floats(cases[input]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvInPlaceMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvInPlaceMerge);
		check(descriptor && !descriptor->stable && descriptor->inPlace && descriptor->safeMaxItemCount == 512 &&
			algorithmAcceptsItemCount(SortAlgorithm::ArrayvInPlaceMerge, 512) &&
			!algorithmAcceptsItemCount(SortAlgorithm::ArrayvInPlaceMerge, 513),
			"In-Place Merge declares its unstable equal-key behavior, constant item storage, and a safe operation ceiling");

		TraceBuildRequest witnessRequest;
		witnessRequest.algorithm = SortAlgorithm::ArrayvInPlaceMerge;
		witnessRequest.input = floats({ 0.5f, 0.5f, 0.1f, 0.5f });
		const SortTrace witnessTrace = SortTraceBuilder::build(witnessRequest);
		SortState witnessState = witnessTrace.initialState;
		for (std::size_t event = 0; event < witnessTrace.events.size(); ++event)
			applyEventForward(witnessState, witnessTrace.events[event]);
		check(isSorted(witnessState, SortDirection::Ascending) &&
			witnessState.slots[1].item.stableId > witnessState.slots[2].item.stableId,
			"In-Place Merge exposes the pinned adjacent-swap method's equal-key reordering");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvInPlaceMerge;
		request.input = floats(randomized);
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(finalState, trace.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator swaps = finalState.metrics.algorithm.find("in_place_merge_swaps");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			swaps != finalState.metrics.algorithm.end() && swaps->second > 0,
			"In-Place Merge finishes using adjacent in-array movement without an auxiliary item lane");

		request.maxOperations = 96;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"In-Place Merge operation-limited recursion rewinds exactly");
	}

	void testParallelMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x50415241u);
		for (std::size_t size = 0; size <= 33; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 9) / 9.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Parallel Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvParallelMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvParallelMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "parallel" &&
			descriptor->stable && !descriptor->inPlace,
			"Parallel Merge publishes stable task-grouped merge metadata");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvParallelMerge;
		request.input = floats({ 0.93f, 0.11f, 0.66f, 0.32f, 0.74f, 0.18f, 0.85f, 0.04f, 0.52f, 0.25f,
			0.61f, 0.39f, 0.99f, 0.07f, 0.46f, 0.80f, 0.14f, 0.57f, 0.28f, 0.69f, 0.02f, 0.42f, 0.89f,
			0.21f, 0.75f, 0.34f, 0.96f, 0.09f, 0.50f, 0.17f, 0.63f });
		const SortTrace trace = SortTraceBuilder::build(request);
		const SortTrace replayTrace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		bool deterministicEvents = trace.events.size() == replayTrace.events.size();
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
			if (deterministicEvents)
				deterministicEvents = trace.events[event].type == replayTrace.events[event].type &&
					trace.events[event].sequence == replayTrace.events[event].sequence &&
					trace.events[event].groupId == replayTrace.events[event].groupId &&
					trace.events[event].firstIndex == replayTrace.events[event].firstIndex &&
					trace.events[event].secondIndex == replayTrace.events[event].secondIndex;
		}
		const std::map<std::string, std::uint64_t>::const_iterator tasks = finalState.metrics.algorithm.find("parallel_merge_tasks");
		check(trace.complete && tasks != finalState.metrics.algorithm.end() && tasks->second == 30 &&
			phaseBegins == 30 && phaseEnds == 30,
			"Parallel Merge groups all 30 recursive sibling tasks and closes their phase markers");
		check(deterministicEvents, "Parallel Merge replaces thread races with a repeatable deterministic trace schedule");

		request.maxOperations = 64;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Parallel Merge partial task-group trace rewinds exactly");
	}

	void testRotateMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x524F5441u);
		for (std::size_t size = 0; size <= 65; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 11) / 11.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Rotate Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvRotateMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvRotateMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "rotate" &&
			descriptor->stable && descriptor->inPlace,
			"Rotate Merge publishes its stable in-place family identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvRotateMerge;
		request.input = floats({ 0.6f, 0.2f, 0.5f, 0.2f, 0.1f, 0.9f, 0.5f, 0.4f, 0.1f, 0.8f,
			0.3f, 0.7f, 0.2f, 0.6f, 0.0f, 0.9f, 0.5f, 0.3f, 0.8f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator rotations = finalState.metrics.algorithm.find("rotate_block_swaps");
		const std::map<std::string, std::uint64_t>::const_iterator binary = finalState.metrics.algorithm.find("binary_search_comparisons");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			phaseBegins == 5 && phaseEnds == 5 && rotations != finalState.metrics.algorithm.end() && rotations->second > 0 &&
			binary != finalState.metrics.algorithm.end() && binary->second > 0,
			"Rotate Merge sorts duplicate-rich odd-sized input with five bottom-up phases, binary searches, and no item workspace");

		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Rotate Merge partial phase rewinds all block swaps and annotations exactly");
	}

	void testParallelRotateMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x50524F54u);
		for (std::size_t size = 0; size <= 35; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 9) / 9.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Parallel Rotate Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvRotateMergeParallel,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvRotateMergeParallel);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "parallel-rotate" &&
			descriptor->stable && descriptor->inPlace,
			"Parallel Rotate Merge keeps a stable top-down in-place catalog identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvRotateMergeParallel;
		request.input = floats({ 0.61f, 0.24f, 0.57f, 0.03f, 0.82f, 0.44f, 0.11f, 0.71f, 0.32f, 0.95f,
			0.18f, 0.66f, 0.39f, 0.07f, 0.53f, 0.27f, 0.88f, 0.14f, 0.75f, 0.48f, 0.01f, 0.93f, 0.36f,
			0.69f, 0.21f, 0.84f, 0.42f, 0.09f, 0.58f, 0.30f, 0.99f });
		const SortTrace trace = SortTraceBuilder::build(request);
		const SortTrace repeated = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		bool deterministic = trace.events.size() == repeated.events.size();
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
			if (deterministic)
				deterministic = trace.events[event].type == repeated.events[event].type &&
					trace.events[event].sequence == repeated.events[event].sequence &&
					trace.events[event].groupId == repeated.events[event].groupId &&
					trace.events[event].firstIndex == repeated.events[event].firstIndex &&
					trace.events[event].secondIndex == repeated.events[event].secondIndex;
		}
		const std::map<std::string, std::uint64_t>::const_iterator sortTasks = finalState.metrics.algorithm.find("parallel_sort_tasks");
		const std::map<std::string, std::uint64_t>::const_iterator mergeTasks = finalState.metrics.algorithm.find("parallel_merge_tasks");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			sortTasks != finalState.metrics.algorithm.end() && sortTasks->second == 30 &&
			mergeTasks != finalState.metrics.algorithm.end() && mergeTasks->second > 0 &&
			phaseBegins == phaseEnds && phaseBegins > 30,
			"Parallel Rotate Merge groups recursive sort/merge work and finishes with balanced phase markers");
		check(deterministic, "Parallel Rotate Merge preserves its deterministic serialized phase schedule");

		request.maxOperations = 64;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Parallel Rotate Merge partial nested phase groups rewind exactly");
	}

	void testStacklessRotateMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x53524D47u);
		for (std::size_t size = 0; size <= 57; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 10) / 10.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Stackless Rotate Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvStacklessRotateMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvStacklessRotateMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "stackless-rotate" &&
			descriptor->stable && descriptor->inPlace,
			"Stackless Rotate Merge publishes its stable in-place catalog identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvStacklessRotateMerge;
		request.input = floats({ 0.8f, 0.3f, 0.6f, 0.1f, 0.9f, 0.4f, 0.7f, 0.2f, 0.5f, 0.0f,
			0.8f, 0.3f, 0.6f, 0.1f, 0.9f, 0.4f, 0.7f, 0.2f, 0.5f, 0.0f, 0.55f, 0.15f, 0.85f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator passes = finalState.metrics.algorithm.find("merge_passes");
		const std::map<std::string, std::uint64_t>::const_iterator partitions = finalState.metrics.algorithm.find("partition_merges");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			passes != finalState.metrics.algorithm.end() && passes->second > 0 &&
			partitions != finalState.metrics.algorithm.end() && partitions->second > 0 &&
			phaseBegins == phaseEnds && phaseBegins == passes->second + 1,
			"Stackless Rotate Merge completes its staged partition passes without an auxiliary lane");

		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Stackless Rotate Merge operation-limited phase rewinds exactly");
	}

	void testBlockSwapMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x42534D47u);
		for (std::size_t size = 0; size <= 65; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 8) / 8.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Block-Swap Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvBlockSwapMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvBlockSwapMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "block-swap" &&
			descriptor->stable && descriptor->inPlace,
			"Block-Swap Merge publishes its stable in-place catalog identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvBlockSwapMerge;
		request.input = floats({ 0.8f, 0.2f, 0.7f, 0.1f, 0.6f, 0.0f, 0.5f, 0.3f, 0.9f, 0.4f,
			0.8f, 0.2f, 0.7f, 0.1f, 0.6f, 0.0f, 0.5f, 0.3f, 0.9f, 0.4f, 0.15f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator blockSwaps = finalState.metrics.algorithm.find("block_swaps");
		const std::map<std::string, std::uint64_t>::const_iterator comparisons = finalState.metrics.algorithm.find("block_merge_comparisons");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			phaseBegins == 5 && phaseEnds == 5 && blockSwaps != finalState.metrics.algorithm.end() && blockSwaps->second > 0 &&
			comparisons != finalState.metrics.algorithm.end() && comparisons->second > 0,
			"Block-Swap Merge produces stable merge passes using block movement and symmetric split searches");

		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Block-Swap Merge partial pass rewinds exactly");
	}

	void testWeavedMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x57454156u);
		for (std::size_t size = 0; size <= 65; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 9) / 9.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Weaved Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvWeavedMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvWeavedMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "weaved" &&
			!descriptor->stable && !descriptor->inPlace,
			"Weaved Merge publishes its auxiliary-workspace identity and source-observed equal-key instability");
		std::mt19937 witnessRandom(0x57454156u);
		for (std::size_t priorSize = 0; priorSize < 5; ++priorSize)
			for (std::size_t i = 0; i < priorSize; ++i) (void)witnessRandom();
		std::vector<float> witnessValues;
		for (std::size_t i = 0; i < 5; ++i) witnessValues.push_back(static_cast<float>(witnessRandom() % 9) / 9.0f);
		TraceBuildRequest witnessRequest;
		witnessRequest.algorithm = SortAlgorithm::ArrayvWeavedMerge;
		witnessRequest.input = floats(witnessValues);
		const SortTrace witnessTrace = SortTraceBuilder::build(witnessRequest);
		SortState witnessState = witnessTrace.initialState;
		for (std::size_t event = 0; event < witnessTrace.events.size(); ++event) applyEventForward(witnessState, witnessTrace.events[event]);
		bool reorderedEqualItems = false;
		for (std::size_t i = 1; i < witnessState.slots.size(); ++i)
			if (witnessState.slots[i - 1].item.value == witnessState.slots[i].item.value &&
				witnessState.slots[i - 1].item.stableId > witnessState.slots[i].item.stableId)
				reorderedEqualItems = true;
		check(isSorted(witnessState, SortDirection::Ascending) && reorderedEqualItems,
			"Weaved Merge preserves order by current index rather than original identity, producing an unstable tie witness");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvWeavedMerge;
		request.input = floats({ 0.61f, 0.04f, 0.72f, 0.19f, 0.55f, 0.31f, 0.88f, 0.11f, 0.47f, 0.95f,
			0.22f, 0.68f, 0.37f, 0.83f, 0.14f, 0.50f, 0.99f, 0.26f, 0.63f, 0.08f, 0.76f, 0.42f, 0.17f,
			0.91f, 0.34f, 0.59f, 0.02f, 0.79f, 0.46f, 0.13f, 0.86f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phaseBegins = 0, phaseEnds = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phaseBegins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++phaseEnds;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator ranges = finalState.metrics.algorithm.find("weave_merge_ranges");
		const std::map<std::string, std::uint64_t>::const_iterator comparisons = finalState.metrics.algorithm.find("weave_comparisons");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) &&
			finalState.auxiliaryItems.find("weave") != finalState.auxiliaryItems.end() &&
			finalState.auxiliaryItems.find("weave")->second.size() == request.input.size() &&
			ranges != finalState.metrics.algorithm.end() && phaseBegins == ranges->second && phaseBegins == phaseEnds &&
			comparisons != finalState.metrics.algorithm.end() && comparisons->second > 0,
			"Weaved Merge orders irregular input through strided auxiliary passes with balanced phase groups");

		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Weaved Merge partial nested strides restore both primary and auxiliary lanes exactly");
	}

	void testTwinSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x5457494Eu);
		for (std::size_t size = 0; size <= 80; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i)
				values.push_back(static_cast<float>(random() % 11) / 11.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Twin Sort duplicate-rich size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvTwinMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvTwinMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "twin" &&
			descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Twin Sort publishes its stable half-buffer merge identity");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvTwinMerge;
		request.input = floats({ 0.95f, 0.72f, 0.49f, 0.83f, 0.11f, 0.64f, 0.31f, 0.88f,
			0.05f, 0.57f, 0.27f, 0.99f, 0.42f, 0.76f, 0.18f, 0.68f, 0.36f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t phases = 0, annotations = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++phases;
			applyEventForward(finalState, trace.events[event]);
		}
		for (std::size_t annotation = 0; annotation < trace.annotations.size(); ++annotation)
			if (trace.annotations[annotation].group >= 0) ++annotations;
		const std::map<std::string, std::uint64_t>::const_iterator reversals = finalState.metrics.algorithm.find("twin_run_reversals");
		const std::map<std::string, std::uint64_t>::const_iterator passes = finalState.metrics.algorithm.find("twin_merge_passes");
		const std::map<std::string, std::uint64_t>::const_iterator bufferWrites = finalState.metrics.algorithm.find("twin_buffer_writes");
		const std::map<std::string, std::vector<SortSlot> >::const_iterator buffer = finalState.auxiliaryItems.find("twin_buffer");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) &&
			reversals != finalState.metrics.algorithm.end() && reversals->second > 0 &&
			passes != finalState.metrics.algorithm.end() && passes->second > 0 &&
			bufferWrites != finalState.metrics.algorithm.end() && bufferWrites->second > 0 &&
			buffer != finalState.auxiliaryItems.end() && buffer->second.size() == request.input.size() / 2 &&
			phases > 1 && annotations > 0,
			"Twin Sort records alternating-run reversal and grouped tail merges through a half-size buffer");

		// The pinned twin-pair scan recognizes a fully reversed input and exits
		// after reversing it, so it should not allocate the tail-merge workspace.
		request.input = floats({ 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f });
		const SortTrace reversedTrace = SortTraceBuilder::build(request);
		SortState reversedState = reversedTrace.initialState;
		for (std::size_t event = 0; event < reversedTrace.events.size(); ++event) applyEventForward(reversedState, reversedTrace.events[event]);
		check(reversedTrace.complete && isSorted(reversedState, SortDirection::Ascending) &&
			reversedState.auxiliaryItems.find("twin_buffer") == reversedState.auxiliaryItems.end(),
			"Twin Sort's full-reversal fast path avoids allocating a merge buffer");

		request.input = floats({ 0.95f, 0.72f, 0.49f, 0.83f, 0.11f, 0.64f, 0.31f, 0.88f,
			0.05f, 0.57f, 0.27f, 0.99f, 0.42f, 0.76f, 0.18f, 0.68f, 0.36f });
		request.maxOperations = 64;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Twin Sort operation-limited run reverses all primary and buffer mutations exactly");
	}

	void testTimMergeVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvTim, SortAlgorithm::ArrayvCocktailMerge };
		std::mt19937 random(0x54494D53u);
		for (std::size_t size = 0; size <= 72; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 13) / 13.0f);
			for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " duplicate-rich size " + std::to_string(size),
						floats(values), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}

		std::vector<float> stackedRuns;
		for (int i = 129; i > 0; --i) stackedRuns.push_back(static_cast<float>((i * 37) % 23) / 23.0f);
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " multi-run stack",
					floats(stackedRuns), InputProfile::NormalizedFloat, algorithms[algorithm],
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* tim = algorithmDescriptor(SortAlgorithm::ArrayvTim);
		const AlgorithmDescriptor* cocktail = algorithmDescriptor(SortAlgorithm::ArrayvCocktailMerge);
		check(tim && tim->familyId == "merge-hybrid" && tim->variantId == "tim" && tim->stable && !tim->inPlace &&
			cocktail && cocktail->familyId == "merge-hybrid" && cocktail->variantId == "cocktail-merge" && cocktail->stable,
			"Tim and Cocktail Merge retain separate stable hybrid catalog identities");

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvTim;
		request.input = floats(stackedRuns);
		const SortTrace timTrace = SortTraceBuilder::build(request);
		SortState timFinal = timTrace.initialState;
		for (std::size_t event = 0; event < timTrace.events.size(); ++event) applyEventForward(timFinal, timTrace.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator natural = timFinal.metrics.algorithm.find("tim_natural_runs");
		const std::map<std::string, std::uint64_t>::const_iterator reversals = timFinal.metrics.algorithm.find("tim_run_reversals");
		check(timTrace.complete && isSorted(timFinal, SortDirection::Ascending) &&
			natural != timFinal.metrics.algorithm.end() && natural->second > 1 &&
			reversals != timFinal.metrics.algorithm.end() && reversals->second > 0 &&
			timFinal.auxiliaryItems.find("tim_buffer") != timFinal.auxiliaryItems.end(),
			"Tim Sort identifies and reverses descending natural runs before merging its run stack");

		request.algorithm = SortAlgorithm::ArrayvCocktailMerge;
		request.input = floats(stackedRuns);
		const SortTrace cocktailTrace = SortTraceBuilder::build(request);
		SortState cocktailFinal = cocktailTrace.initialState;
		std::size_t cocktailPhases = 0;
		for (std::size_t event = 0; event < cocktailTrace.events.size(); ++event)
		{
			if (cocktailTrace.events[event].type == SemanticEventType::PhaseBegin) ++cocktailPhases;
			applyEventForward(cocktailFinal, cocktailTrace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator cocktailRuns = cocktailFinal.metrics.algorithm.find("tim_cocktail_runs");
		check(cocktailTrace.complete && isSorted(cocktailFinal, SortDirection::Ascending) &&
			cocktailRuns != cocktailFinal.metrics.algorithm.end() && cocktailRuns->second > 1 &&
			cocktailPhases > cocktailRuns->second,
			"Cocktail Merge forms fixed min-runs with bidirectional passes before Tim-style run merges");

		request.maxOperations = 80;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Tim family partial run restores run and buffer state exactly");
	}

	void testImprovedInPlaceMergeSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0x494E504Cu);
		for (std::size_t size = 0; size <= 52; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 10) / 10.0f);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Improved In-Place Merge size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvImprovedInPlaceMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
		}
		std::vector<float> larger;
		for (int i = 0; i < 127; ++i) larger.push_back(static_cast<float>((i * 41 + i / 5) % 17) / 17.0f);
		std::shuffle(larger.begin(), larger.end(), random);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Improved In-Place Merge duplicate-rich larger case", floats(larger),
				InputProfile::NormalizedFloat, SortAlgorithm::ArrayvImprovedInPlaceMerge,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvImprovedInPlaceMerge);
		check(descriptor && descriptor->familyId == "merge-sort" && descriptor->variantId == "improved-in-place" &&
			descriptor->stable && descriptor->inPlace,
			"Improved In-Place Merge advertises stable constant-item-workspace behavior");
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvImprovedInPlaceMerge;
		request.input = floats(larger);
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t begins = 0, ends = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++begins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++ends;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator rotations = finalState.metrics.algorithm.find("inplace_merge_rotations");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			rotations != finalState.metrics.algorithm.end() && rotations->second > 0 && begins == ends,
			"Improved In-Place Merge uses grouped stable rotations without allocating an item lane");

		request.maxOperations = 72;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Improved In-Place Merge partial symmetric rotations rewind exactly");
	}

	void testBlockInsertionSort()
	{
		using namespace sortinglogic;
		std::mt19937 random(0xB10C1A5u);
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.4f });
		cases.push_back(std::vector<float>(24, 0.5f));
		cases.push_back(std::vector<float>{ 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f });
		cases.push_back(std::vector<float>{ 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f });
		cases.push_back(std::vector<float>{ 0.1f, 0.3f, 0.2f, 0.4f, 0.6f, 0.5f, 0.8f, 0.7f,
			0.9f, 0.0f, 0.2f, 0.1f, 0.7f, 0.6f, 0.4f, 0.3f });
		for (std::size_t size = 0; size <= 64; ++size)
		{
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 9) / 9.0f);
			cases.push_back(values);
		}
		for (std::size_t c = 0; c < cases.size(); ++c)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Block Insertion shape " + std::to_string(c), floats(cases[c]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvBlockInsertion,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvBlockInsertion);
		check(descriptor && descriptor->familyId == "insertion-methods" && descriptor->variantId == "block-insertion" &&
			descriptor->stable && descriptor->inPlace,
			"Block Insertion is catalogued as a stable in-place insertion family method");
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvBlockInsertion;
		request.input = floats(std::vector<float>{ 0.8f, 0.9f, 0.3f, 0.5f, 0.6f, 0.2f, 0.4f, 0.1f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t mergeBegins = 0, mergeEnds = 0;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			if (trace.events[i].type == SemanticEventType::PhaseBegin && trace.events[i].workspaceName.find("Block insertion merge") == 0) ++mergeBegins;
			if (trace.events[i].type == SemanticEventType::PhaseEnd && trace.events[i].workspaceName.find("Block insertion merge") == 0) ++mergeEnds;
			applyEventForward(finalState, trace.events[i]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator merges = finalState.metrics.algorithm.find("block_insertion_merges");
		const std::map<std::string, std::uint64_t>::const_iterator insertions = finalState.metrics.algorithm.find("block_insertion_short_insertions");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			merges != finalState.metrics.algorithm.end() && merges->second > 0 &&
			insertions != finalState.metrics.algorithm.end() && insertions->second > 0 &&
			mergeBegins == mergeEnds,
			"Block Insertion merges longer runs and uses specialized insertion for short runs");

		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Block Insertion operation-budget prefix rewinds through run merges exactly");
	}

	void testBufferedStoogeSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.4f });
		cases.push_back(std::vector<float>(8, 0.5f));
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f });
		cases.push_back(std::vector<float>{ 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		cases.push_back(std::vector<float>{ 0.5f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.4f, 0.0f });
		for (std::size_t size = 0; size <= 8; ++size)
			for (std::size_t seed = 0; seed < 4; ++seed)
			{
				std::vector<float> values;
				std::mt19937 random(static_cast<std::uint32_t>(0xB00F0000u + size * 13 + seed));
				for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 5) / 5.0f);
				cases.push_back(values);
			}
		for (std::size_t c = 0; c < cases.size(); ++c)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Buffered Stooge shape " + std::to_string(c), floats(cases[c]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvBufferedStooge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvBufferedStooge);
		check(descriptor && descriptor->familyId == "recursive-novelty" && descriptor->variantId == "buffered-stooge" &&
			descriptor->safeMaxItemCount == 8 && !descriptor->stable,
			"Buffered Stooge has a separate novelty catalog entry with a conservative eight-item ceiling");
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvBufferedStooge;
		request.input = floats(cases[5]);
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t begins = 0, ends = 0;
		for (std::size_t event = 0; event < trace.events.size(); ++event)
		{
			if (trace.events[event].type == SemanticEventType::PhaseBegin) ++begins;
			if (trace.events[event].type == SemanticEventType::PhaseEnd) ++ends;
			applyEventForward(finalState, trace.events[event]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator calls = finalState.metrics.algorithm.find("buffered_stooge_calls");
		const std::map<std::string, std::uint64_t>::const_iterator bufferSwaps = finalState.metrics.algorithm.find("buffered_stooge_buffer_swaps");
		const std::map<std::string, std::uint64_t>::const_iterator cleanupSwaps = finalState.metrics.algorithm.find("buffered_stooge_cleanup_swaps");
		check(trace.complete && calls != finalState.metrics.algorithm.end() && calls->second > 1 &&
			bufferSwaps != finalState.metrics.algorithm.end() && cleanupSwaps != finalState.metrics.algorithm.end() &&
			begins == ends && (trace.outcome == AlgorithmOutcome::IncompleteDemonstration || isSorted(finalState, SortDirection::Ascending)),
			"Buffered Stooge exposes its recursive front-buffer/cleanup phases and reports unsorted output honestly");

		request.maxOperations = 64;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Buffered Stooge operation-budget prefix rewinds all buffer and cleanup swaps");
	}

	void testBadSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.4f });
		cases.push_back(std::vector<float>(18, 0.5f));
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f });
		cases.push_back(std::vector<float>{ 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		cases.push_back(std::vector<float>{ 0.4f, 0.5f, 0.6f, 0.1f, 0.3f, 0.2f, 0.7f, 0.0f });
		for (std::size_t size = 0; size <= 36; ++size)
		{
			std::vector<float> values;
			std::mt19937 random(static_cast<std::uint32_t>(0xBAD50000u + size));
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 11) / 11.0f);
			cases.push_back(values);
		}
		for (std::size_t c = 0; c < cases.size(); ++c)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Bad Sort shape " + std::to_string(c), floats(cases[c]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvBadSort,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvBadSort);
		check(descriptor && descriptor->familyId == "selection-sorts" && descriptor->variantId == "bad-cubic-selection" &&
			descriptor->safeMaxItemCount == 128 && !descriptor->stable && descriptor->inPlace,
			"Bad Sort has a cubic selection-family identity and a 128-item safety ceiling");
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvBadSort;
		request.input = floats(std::vector<float>{ 0.2f, 0.4f, 0.1f, 0.3f, 0.5f, 0.0f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
		const std::map<std::string, std::uint64_t>::const_iterator candidates = finalState.metrics.algorithm.find("bad_sort_candidate_checks");
		const std::map<std::string, std::uint64_t>::const_iterator comparisons = finalState.metrics.algorithm.find("bad_sort_comparisons");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && finalState.auxiliaryItems.empty() &&
			candidates != finalState.metrics.algorithm.end() && candidates->second > request.input.size() &&
			comparisons != finalState.metrics.algorithm.end() && comparisons->second > 0,
			"Bad Sort repeatedly probes suffix minima before swapping each selected item");
		check(!algorithmAcceptsItemCount(SortAlgorithm::ArrayvBadSort, 129),
			"Bad Sort refuses inputs above its safe cubic-trace limit");
		request.maxOperations = 48;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Bad Sort partial cubic trace rewinds exactly");
	}

	void testAsynchronousSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvAsynchronous;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "selection-sorts" && descriptor->variantId == "asynchronous-value-scan" &&
			descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == 384,
			"Asynchronous Sort is a stable, bounded integer value-scan variant");
		check(!algorithmSupports(algorithm, InputDomain::NormalizedFloats) &&
			algorithmSupports(algorithm, InputDomain::NonnegativeIntegerKeys) &&
			algorithmSupports(algorithm, InputDomain::SignedIntegerKeys) &&
			algorithmSupports(algorithm, InputDomain::IntegerPermutation),
			"Asynchronous Sort accepts integer domains and rejects normalized floats");
		check(algorithmAcceptsItemCount(algorithm, 384) && !algorithmAcceptsItemCount(algorithm, 385),
			"Asynchronous Sort exposes its 384-item safe ceiling");

		std::vector<std::vector<int> > cases[3];
		cases[0] = { {}, { 0 }, { 4, 1, 4, 0, 2, 1, 3, 0 }, { 0, 1, 2, 3, 4, 5, 6 }, { 6, 5, 4, 3, 2, 1, 0 } };
		cases[1] = { {}, { 0 }, { -4, 2, 0, -4, 3, 2, -1, 1 }, { -5, -3, -1, 0, 2, 4, 5 }, { 5, 4, 2, 0, -1, -3, -5 } };
		cases[2] = { {}, { 0 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 7, 6, 5, 4, 3, 2, 1, 0 } };
		std::mt19937 random(0xA5A50117u);
		std::vector<int> nonnegativeRandom, signedRandom, permutationRandom;
		for (int i = 0; i < 22; ++i)
		{
			nonnegativeRandom.push_back(static_cast<int>(random() % 22));
			signedRandom.push_back(static_cast<int>(random() % 43) - 21);
			permutationRandom.push_back(i);
		}
		std::shuffle(permutationRandom.begin(), permutationRandom.end(), random);
		cases[0].push_back(nonnegativeRandom);
		cases[1].push_back(signedRandom);
		cases[2].push_back(permutationRandom);
		const InputDomain domains[] = { InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation };
		for (int domain = 0; domain < 3; ++domain)
			for (std::size_t shape = 0; shape < cases[domain].size(); ++shape)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string("Asynchronous Sort ") + inputDomainName(domains[domain]) + " shape " + std::to_string(shape),
						integers(cases[domain][shape]), InputProfile::IntegerKeys, algorithm,
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending, domains[domain]);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		request.profile = InputProfile::IntegerKeys;
		request.domain = InputDomain::SignedIntegerKeys;
		request.input = integers({ -3, 2, 0, -3, 2, 1, 0, -1 });
		request.direction = SortDirection::Ascending;
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t probes = 0;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			if (trace.events[i].type == SemanticEventType::Probe)
			{
				++probes;
				check(trace.events[i].firstLane == "asynchronous_scan" &&
					trace.events[i].firstRole == SemanticRole::ProbedItem,
					"Asynchronous Sort tags probes with their auxiliary lane and role");
			}
			applyEventForward(finalState, trace.events[i]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator passes = finalState.metrics.algorithm.find("asynchronous_key_passes");
		const std::map<std::string, std::uint64_t>::const_iterator probeMetric = finalState.metrics.algorithm.find("asynchronous_source_probes");
		const std::map<std::string, std::uint64_t>::const_iterator emitted = finalState.metrics.algorithm.find("asynchronous_emitted_items");
		check(trace.complete && passes != finalState.metrics.algorithm.end() && passes->second == 6 &&
			probeMetric != finalState.metrics.algorithm.end() && probeMetric->second == probes && probes == 48 &&
			emitted != finalState.metrics.algorithm.end() && emitted->second == request.input.size() &&
			finalState.auxiliaryItems.empty() && finalState.integerWorkspaces.empty(),
			"Asynchronous Sort counts passes and probes, emits every item once, and clears temporary lanes");
		const SortTrace repeated = SortTraceBuilder::build(request);
		bool identical = trace.events.size() == repeated.events.size();
		for (std::size_t i = 0; identical && i < trace.events.size(); ++i)
			identical = trace.events[i].type == repeated.events[i].type &&
				trace.events[i].firstIndex == repeated.events[i].firstIndex &&
				trace.events[i].secondIndex == repeated.events[i].secondIndex &&
				trace.events[i].firstItemId == repeated.events[i].firstItemId &&
				trace.events[i].metricName == repeated.events[i].metricName;
		check(identical, "Asynchronous Sort trace is deterministic for repeated requests");

		TraceBuildRequest floatRequest;
		floatRequest.algorithm = algorithm;
		floatRequest.input = floats({ 0.3f, 0.1f, 0.2f });
		bool rejectedFloats = false;
		try { SortTraceBuilder::build(floatRequest); }
		catch (const std::invalid_argument&) { rejectedFloats = true; }
		check(rejectedFloats, "Asynchronous Sort refuses normalized-float input");
		std::vector<int> oversizedValues;
		for (int i = 0; i < 385; ++i) oversizedValues.push_back(i);
		TraceBuildRequest oversized = request;
		oversized.input = integers(oversizedValues);
		oversized.domain = InputDomain::IntegerPermutation;
		bool rejectedOversized = false;
		try { SortTraceBuilder::build(oversized); }
		catch (const std::invalid_argument&) { rejectedOversized = true; }
		check(rejectedOversized, "Asynchronous Sort refuses inputs above its safe trace limit");

		TraceBuildRequest partialRequest = request;
		partialRequest.maxOperations = 28;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Asynchronous Sort operation-limited trace rewinds its scan lane and consumed markers");
		TraceBuildRequest workLimited = request;
		workLimited.maxOperations = 10000;
		workLimited.maxWorkSteps = 12;
		const SortTrace workPartial = SortTraceBuilder::build(workLimited);
		SortState workState = workPartial.initialState;
		for (std::size_t i = 0; i < workPartial.events.size(); ++i) applyEventForward(workState, workPartial.events[i]);
		for (std::size_t i = workPartial.events.size(); i > 0; --i) applyEventBackward(workState, workPartial.events[i - 1]);
		check(!workPartial.complete && workPartial.completionReason.find("work budget") != std::string::npos &&
			sameState(workState, workPartial.initialState),
			"Asynchronous Sort charges the range scan to its work budget and rewinds partial state");
	}

	void testPDMergeSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases = {
			{}, { 0.4f }, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }, { 0.5f, 0.4f, 0.3f, 0.2f, 0.1f },
			{ 0.4f, 0.1f, 0.4f, 0.2f, 0.1f, 0.3f, 0.4f },
			{ 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 0.1f, 0.0f },
			{ 0.0f, 0.1f, 0.2f, 0.9f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f }
		};
		for (std::size_t size = 2; size <= 40; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0x50444D00u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 13) / 13.0f);
			std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Pattern-Defeating Merge shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvPDMerge,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvPDMerge);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "pattern-defeating" &&
			descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Pattern-Defeating Merge has stable merge-hybrid catalog facts");

		TraceBuildRequest backwardRequest;
		backwardRequest.algorithm = SortAlgorithm::ArrayvPDMerge;
		backwardRequest.input = floats({ 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 0.1f, 0.0f });
		const SortTrace backwardTrace = SortTraceBuilder::build(backwardRequest);
		bool usedSmallerRightBuffer = false;
		for (std::size_t i = 0; i < backwardTrace.events.size(); ++i)
			if (backwardTrace.events[i].type == SemanticEventType::Compare &&
				backwardTrace.events[i].firstLane == "main" && backwardTrace.events[i].secondLane == "pdmerge_buffer")
				usedSmallerRightBuffer = true;
		check(usedSmallerRightBuffer, "Pattern-Defeating Merge copies the smaller right run and merges backward");

		TraceBuildRequest forwardRequest = backwardRequest;
		forwardRequest.input = floats({ 0.0f, 0.1f, 0.2f, 0.9f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f });
		const SortTrace forwardTrace = SortTraceBuilder::build(forwardRequest);
		bool usedSmallerLeftBuffer = false;
		for (std::size_t i = 0; i < forwardTrace.events.size(); ++i)
			if (forwardTrace.events[i].type == SemanticEventType::Compare &&
				forwardTrace.events[i].firstLane == "pdmerge_buffer" && forwardTrace.events[i].secondLane == "main")
				usedSmallerLeftBuffer = true;
		check(usedSmallerLeftBuffer, "Pattern-Defeating Merge copies the smaller left run and merges forward");

		TraceBuildRequest partialRequest = forwardRequest;
		partialRequest.maxOperations = 42;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState state = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
		check(!partial.complete && sameState(state, partial.initialState),
			"Pattern-Defeating Merge operation-limited run detection and merge passes rewind exactly");
	}

	void testStableCycleSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > floatCases = {
			{}, { 0.4f }, { 0.3f, 0.1f, 0.3f, 0.2f, 0.1f, 0.3f, 0.0f },
			{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f }
		};
		for (std::size_t size = 2; size <= 32; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0x57AB1E00u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 9) / 9.0f);
			std::shuffle(values.begin(), values.end(), random);
			floatCases.push_back(values);
		}
		for (std::size_t shape = 0; shape < floatCases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Stable Cycle Sort float shape " + std::to_string(shape), floats(floatCases[shape]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvStableCycle,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Stable Cycle Sort integer duplicate keys", integers({ 3, 0, 2, 1, 2, 0, 3, 1, 2, 0 }),
				InputProfile::IntegerKeys, SortAlgorithm::ArrayvStableCycle,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvStableCycle);
		check(descriptor && descriptor->familyId == "selection-sorts" && descriptor->variantId == "stable-cycle" &&
			descriptor->stable && !descriptor->inPlace,
			"Stable Cycle Sort is catalogued as a stable selection-family cycle strategy");
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvStableCycle;
		request.input = integers({ 4, 1, 3, 1, 0, 4, 2, 1 });
		request.profile = InputProfile::IntegerKeys;
		request.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		std::size_t carryEvents = 0;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			if (trace.events[i].type == SemanticEventType::CarryWrite) ++carryEvents;
			applyEventForward(finalState, trace.events[i]);
		}
		const std::map<std::string, std::uint64_t>::const_iterator fixed = finalState.metrics.algorithm.find("stable_cycle_fixed_positions");
		const std::map<std::string, std::uint64_t>::const_iterator moves = finalState.metrics.algorithm.find("stable_cycle_item_moves");
		check(trace.complete && carryEvents > 0 && fixed != finalState.metrics.algorithm.end() && fixed->second == request.input.size() &&
			moves != finalState.metrics.algorithm.end() && moves->second == carryEvents &&
			finalState.auxiliaryItems.empty() && finalState.integerWorkspaces.empty(),
			"Stable Cycle Sort exposes reversible carry movement and clears its reference and bit lanes");

		TraceBuildRequest partialRequest = request;
		partialRequest.maxOperations = 35;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState state = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
		check(!partial.complete && sameState(state, partial.initialState),
			"Stable Cycle Sort partial traces rewind held items, packed flags, and reference copies exactly");
	}

	void testTournamentSorts()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvClassicTournament, SortAlgorithm::ArrayvTournament };
		std::vector<std::vector<float> > cases = {
			{}, { 0.4f }, { 0.5f, 0.1f, 0.4f, 0.2f, 0.4f, 0.0f, 0.2f },
			{ 0.3f, 0.3f, 0.3f, 0.3f, 0.3f }, { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f }
		};
		for (std::size_t size = 2; size <= 28; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0x70A00000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 10) / 10.0f);
			std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithm]);
			check(descriptor && descriptor->familyId == "selection-sorts" && descriptor->stable && !descriptor->inPlace,
				"Tournament variant has stable selection-family tree metadata");
			for (std::size_t shape = 0; shape < cases.size(); ++shape)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " float shape " + std::to_string(shape),
						floats(cases[shape]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " integer duplicates",
					integers({ 4, 1, 3, 1, 0, 4, 2, 1, 0 }), InputProfile::IntegerKeys, algorithms[algorithm],
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

			TraceBuildRequest request;
			request.algorithm = algorithms[algorithm];
			request.input = floats({ 0.6f, 0.1f, 0.5f, 0.1f, 0.3f, 0.2f, 0.4f });
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState finalState = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
			const char* matchMetric = algorithms[algorithm] == SortAlgorithm::ArrayvClassicTournament
				? "classic_tournament_tree_comparisons" : "tournament_matches";
			const char* outputMetric = algorithms[algorithm] == SortAlgorithm::ArrayvClassicTournament
				? "classic_tournament_extractions" : "tournament_outputs";
			const std::map<std::string, std::uint64_t>::const_iterator matches = finalState.metrics.algorithm.find(matchMetric);
			const std::map<std::string, std::uint64_t>::const_iterator outputs = finalState.metrics.algorithm.find(outputMetric);
			check(trace.complete && isSorted(finalState, SortDirection::Ascending) && matches != finalState.metrics.algorithm.end() &&
				matches->second > 0 && outputs != finalState.metrics.algorithm.end() && outputs->second == request.input.size() &&
				finalState.auxiliaryItems.empty() && finalState.integerWorkspaces.empty(),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " replays its tree and clears all workspaces");

			TraceBuildRequest partialRequest = request;
			partialRequest.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f });
			partialRequest.maxOperations = 48;
			const SortTrace partial = SortTraceBuilder::build(partialRequest);
			SortState partialState = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
			check(!partial.complete && sameState(partialState, partial.initialState),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " operation-limited tree construction rewinds exactly");
		}
	}

	void testSmoothSortVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvSmoothSort, SortAlgorithm::ArrayvBinomialSmoothSort };
		std::vector<std::vector<float> > cases = {
			{}, { 0.4f }, { 0.4f, 0.1f }, { 0.3f, 0.1f, 0.2f },
			{ 0.5f, 0.1f, 0.4f, 0.2f, 0.4f, 0.0f, 0.2f },
			{ 0.3f, 0.3f, 0.3f, 0.3f, 0.3f }, { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f }
		};
		for (std::size_t size = 2; size <= 36; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0x5A007000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 13) / 13.0f);
			if (size % 2 == 0) std::sort(values.begin(), values.end());
			else std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithm]);
			check(descriptor && descriptor->familyId == "selection-sorts" && !descriptor->stable && descriptor->inPlace,
				"Smooth Sort variant advertises its unstable, in-place selection-family contract");
			for (std::size_t shape = 0; shape < cases.size(); ++shape)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " float shape " + std::to_string(shape),
						floats(cases[shape]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " duplicate integer keys",
					integers({ 4, 1, 3, 1, 0, 4, 2, 1, 0 }), InputProfile::IntegerKeys, algorithms[algorithm],
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

			TraceBuildRequest request;
			request.algorithm = algorithms[algorithm];
			request.input = floats({ 0.7f, 0.2f, 0.5f, 0.1f, 0.4f, 0.3f, 0.6f });
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState finalState = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
			const char* metricKey = algorithms[algorithm] == SortAlgorithm::ArrayvSmoothSort
				? "smooth_heap_comparisons" : "binomial_smooth_comparisons";
			const std::map<std::string, std::uint64_t>::const_iterator metric = finalState.metrics.algorithm.find(metricKey);
			check(trace.complete && isSorted(finalState, SortDirection::Ascending) && metric != finalState.metrics.algorithm.end() &&
				metric->second > 0 && finalState.auxiliaryItems.empty() && finalState.integerWorkspaces.empty(),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " completes its native heap schedule without auxiliary storage");

			TraceBuildRequest partialRequest = request;
			partialRequest.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f });
			partialRequest.maxOperations = 56;
			const SortTrace partial = SortTraceBuilder::build(partialRequest);
			SortState partialState = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
			check(!partial.complete && sameState(partialState, partial.initialState),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " operation-limited heap construction rewinds exactly");
			for (int direction = 0; direction < 2; ++direction)
			{
				std::mt19937 stressRandom(static_cast<std::uint32_t>(0x8005A000u + algorithm * 7 + direction));
				std::vector<float> stressValues;
				for (std::size_t i = 0; i < 800; ++i) stressValues.push_back(static_cast<float>(stressRandom() % 257) / 257.0f);
				TraceBuildRequest stressRequest;
				stressRequest.algorithm = algorithms[algorithm];
				stressRequest.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				stressRequest.input = floats(stressValues);
				const SortTrace stressTrace = SortTraceBuilder::build(stressRequest);
				SortState stressState = stressTrace.initialState;
				for (std::size_t i = 0; i < stressTrace.events.size(); ++i) applyEventForward(stressState, stressTrace.events[i]);
				check(stressTrace.complete && isSorted(stressState, stressRequest.direction) && stressTrace.snapshots.size() > 2,
					std::string(algorithmDisplayName(algorithms[algorithm])) + " sorts 800 items across snapshot boundaries in both directions");
			}
		}
	}

	void testImprovedBlockSelectionSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvImprovedBlockSelection;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && !descriptor->stable && descriptor->inPlace,
			"Improved Block Selection advertises an in-place merge-hybrid contract");
		std::vector<std::vector<float> > cases = { {}, { 0.5f }, { 0.2f, 0.1f }, { 0.4f, 0.1f, 0.3f, 0.2f },
			{ 0.2f, 0.1f, 0.2f, 0.1f, 0.2f, 0.1f }, { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f } };
		for (std::size_t size = 2; size <= 80; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0xB10C0000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 17) / 17.0f);
			if (size % 3 == 0) std::sort(values.begin(), values.end());
			else if (size % 3 == 1) std::sort(values.rbegin(), values.rend());
			else std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Improved Block Selection float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Improved Block Selection duplicate integer keys",
				integers({ 7, 1, 5, 1, 3, 7, 0, 4, 2, 1, 6, 3, 0, 5, 2 }), InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		request.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
		const std::map<std::string, std::uint64_t>::const_iterator merges = finalState.metrics.algorithm.find("block_selection_forward_merges");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && merges != finalState.metrics.algorithm.end() && merges->second > 0,
			"Improved Block Selection completes its block-selection and forward merge stages");

		TraceBuildRequest partialRequest = request;
		partialRequest.input = floats({ 0.9f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f, 0.6f, 0.4f, 0.5f, 0.0f });
		partialRequest.maxOperations = 72;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Improved Block Selection partial block-selection trace rewinds exactly");
		for (int direction = 0; direction < 2; ++direction)
		{
			std::mt19937 stressRandom(static_cast<std::uint32_t>(0x800B10C0u + direction));
			std::vector<float> stressValues;
			for (std::size_t i = 0; i < 800; ++i) stressValues.push_back(static_cast<float>(stressRandom() % 193) / 193.0f);
			TraceBuildRequest stressRequest = request;
			stressRequest.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			stressRequest.input = floats(stressValues);
			const SortTrace stressTrace = SortTraceBuilder::build(stressRequest);
			SortState stressState = stressTrace.initialState;
			for (std::size_t i = 0; i < stressTrace.events.size(); ++i) applyEventForward(stressState, stressTrace.events[i]);
			check(stressTrace.complete && isSorted(stressState, stressRequest.direction) && stressTrace.snapshots.size() > 2,
				"Improved Block Selection sorts 800 items across trace snapshots in both directions");
		}
	}

	void testStacklessHybridQuickSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvStacklessHybridQuick;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "partition-sorts" && !descriptor->stable && descriptor->inPlace,
			"Stackless Hybrid Quick advertises its in-place partition-family contract");
		std::vector<std::vector<float> > cases = { { 0.3f, 0.1f, 0.2f } };
		for (std::size_t size = 2; size <= 72; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0x510C0000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 13) / 13.0f);
			if (size % 3 == 0) std::sort(values.begin(), values.end());
			else if (size % 3 == 1) std::sort(values.rbegin(), values.rend());
			else std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Stackless Hybrid Quick float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Stackless Hybrid Quick duplicate integer keys",
				integers({ 7, 1, 5, 1, 3, 7, 0, 4, 2, 1, 6, 3, 0, 5, 2 }), InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		request.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f,
			0.3f, 0.8f, 0.1f, 0.6f, 0.4f, 0.9f, 0.2f, 0.5f, 0.7f, 0.0f, 0.3f, 0.1f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
		const std::map<std::string, std::uint64_t>::const_iterator partitions = finalState.metrics.algorithm.find("stackless_hybrid_partitions");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && partitions != finalState.metrics.algorithm.end() && partitions->second > 0,
			"Stackless Hybrid Quick exercises its iterative partition loop");

		TraceBuildRequest partialRequest = request;
		partialRequest.maxOperations = 68;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Stackless Hybrid Quick operation-limited partition trace rewinds exactly");
		for (int direction = 0; direction < 2; ++direction)
		{
			std::mt19937 stressRandom(static_cast<std::uint32_t>(0x80051C00u + direction));
			std::vector<float> stressValues;
			for (std::size_t i = 0; i < 800; ++i) stressValues.push_back(static_cast<float>(stressRandom() % 257) / 257.0f);
			TraceBuildRequest stressRequest = request;
			stressRequest.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			stressRequest.input = floats(stressValues);
			const SortTrace stressTrace = SortTraceBuilder::build(stressRequest);
			SortState stressState = stressTrace.initialState;
			for (std::size_t i = 0; i < stressTrace.events.size(); ++i) applyEventForward(stressState, stressTrace.events[i]);
			check(stressTrace.complete && isSorted(stressState, stressRequest.direction) && stressTrace.snapshots.size() > 2,
				"Stackless Hybrid Quick sorts 800 items across trace snapshots in both directions");
		}
	}

	void testStacklessDualPivotQuickSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvStacklessDualPivotQuick;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "partition-sorts" && !descriptor->stable && descriptor->inPlace,
			"Stackless Dual-Pivot Quick advertises its in-place partition-family contract");
		std::vector<std::vector<float> > cases = { {}, { 0.5f }, { 0.2f, 0.1f }, { 0.3f, 0.1f, 0.2f },
			{ 0.4f, 0.1f, 0.4f, 0.2f, 0.1f, 0.4f, 0.3f }, { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f },
			{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f } };
		for (std::size_t size = 2; size <= 72; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0xD0A10000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 19) / 19.0f);
			if (size % 3 == 0) std::sort(values.begin(), values.end());
			else if (size % 3 == 1) std::sort(values.rbegin(), values.rend());
			else std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Stackless Dual-Pivot Quick float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Stackless Dual-Pivot Quick duplicate integer keys",
				integers({ 7, 1, 5, 1, 3, 7, 0, 4, 2, 1, 6, 3, 0, 5, 2 }), InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		request.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f,
			0.3f, 0.8f, 0.1f, 0.6f, 0.4f, 0.9f, 0.2f, 0.5f, 0.7f, 0.0f, 0.3f, 0.1f,
			0.8f, 0.2f, 0.5f, 0.7f, 0.1f, 0.4f, 0.9f, 0.3f, 0.6f, 0.0f, 0.2f, 0.8f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(finalState, trace.events[i]);
		const std::map<std::string, std::uint64_t>::const_iterator partitions = finalState.metrics.algorithm.find("stackless_dual_partitions");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) && partitions != finalState.metrics.algorithm.end() && partitions->second > 0,
			"Stackless Dual-Pivot Quick exercises its sampled two-pivot partition loop");

		TraceBuildRequest partialRequest = request;
		partialRequest.maxOperations = 72;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Stackless Dual-Pivot Quick partial partition trace rewinds exactly");
		for (int direction = 0; direction < 2; ++direction)
		{
			std::mt19937 stressRandom(static_cast<std::uint32_t>(0x800D0A10u + direction));
			std::vector<float> stressValues;
			for (std::size_t i = 0; i < 800; ++i) stressValues.push_back(static_cast<float>(stressRandom() % 257) / 257.0f);
			TraceBuildRequest stressRequest = request;
			stressRequest.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			stressRequest.input = floats(stressValues);
			const SortTrace stressTrace = SortTraceBuilder::build(stressRequest);
			SortState stressState = stressTrace.initialState;
			for (std::size_t i = 0; i < stressTrace.events.size(); ++i) applyEventForward(stressState, stressTrace.events[i]);
			check(stressTrace.complete && isSorted(stressState, stressRequest.direction) && stressTrace.snapshots.size() > 2,
				"Stackless Dual-Pivot Quick sorts 800 items across trace snapshots in both directions");
		}
	}

	void testHanoiSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvHanoi;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "insertion-methods" && descriptor->safeMaxItemCount == 16 && !descriptor->inPlace,
			"Hanoi Sort advertises its bounded two-peg workspace contract");
		check(!algorithmAcceptsItemCount(algorithm, 17), "Hanoi Sort disables runs above its trace-safe size ceiling");
		std::vector<std::vector<float> > cases = { {}, { 0.5f }, { 0.2f, 0.1f }, { 0.3f, 0.1f, 0.2f },
			{ 0.4f, 0.1f, 0.4f, 0.2f, 0.1f, 0.4f, 0.3f }, { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f },
			{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }, { 0.4f, 0.3f, 0.2f, 0.1f, 0.0f } };
		for (std::size_t size = 2; size <= 16; ++size)
		{
			std::mt19937 random(static_cast<std::uint32_t>(0xA4010000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 7) / 7.0f);
			if (size % 3 == 0) std::sort(values.begin(), values.end());
			else if (size % 3 == 1) std::sort(values.rbegin(), values.rend());
			else std::shuffle(values.begin(), values.end(), random);
			cases.push_back(values);
		}
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Hanoi Sort float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Hanoi Sort duplicate integer keys", integers({ 6, 2, 6, 1, 4, 2, 0, 5, 1, 3, 6, 0 }),
				InputProfile::IntegerKeys, algorithm, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);
		std::mt19937 catalogRandom(0xC0FFEEu);
		std::vector<SortItem> catalogFloat, catalogInteger;
		for (std::size_t i = 0; i < 22; ++i)
		{
			catalogFloat.push_back(SortItem(500 + i, static_cast<float>(catalogRandom() % 7) / 6.0f));
			catalogInteger.push_back(SortItem(700 + i, static_cast<int>(catalogRandom() % 22),
				static_cast<float>(catalogRandom() % 22) / 22.0f));
		}
		std::shuffle(catalogFloat.begin(), catalogFloat.end(), catalogRandom);
		std::shuffle(catalogInteger.begin(), catalogInteger.end(), catalogRandom);
		catalogInteger.resize(16);
		for (std::size_t i = 0; i < catalogInteger.size(); ++i)
			catalogInteger[i] = SortItem(catalogInteger[i].stableId, catalogInteger[i].integerKey % 16, catalogInteger[i].value);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase(std::string("Hanoi Sort randomized integer at safe limit direction ") + std::to_string(direction),
				catalogInteger, InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		request.input = floats({ 0.8f, 0.1f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f, 0.4f, 0.0f, 0.9f, 0.2f, 0.7f });
		const SortTrace trace = SortTraceBuilder::build(request);
		const SortState& finalState = trace.snapshots.rbegin()->second;
		const std::map<std::string, std::uint64_t>::const_iterator transfers = finalState.metrics.algorithm.find("hanoi_item_transfers");
		const std::map<std::string, std::uint64_t>::const_iterator moves = finalState.metrics.algorithm.find("hanoi_tower_moves");
		check(trace.complete && isSorted(finalState, SortDirection::Ascending) &&
			finalState.auxiliaryItems.find("hanoi-peg-2") == finalState.auxiliaryItems.end() &&
			finalState.auxiliaryItems.find("hanoi-peg-3") == finalState.auxiliaryItems.end() &&
			transfers != finalState.metrics.algorithm.end() && transfers->second > 0 &&
			moves != finalState.metrics.algorithm.end() && moves->second > 0,
			"Hanoi Sort returns both peg towers and records its source-specific move schedule");

		TraceBuildRequest partialRequest = request;
		partialRequest.maxOperations = 96;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Hanoi Sort operation-limited peg movements rewind to the exact initial state");
	}

	void testSimplifiedLibrarySort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvSimplifiedLibrary;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "insertion-methods" && descriptor->stable && !descriptor->inPlace &&
			descriptor->safeMaxItemCount == MaxItemCount,
			"Simplified Library Sort advertises stable rank-rebalance behavior and linear workspace");
		const std::size_t sizes[] = { 0, 1, 2, 17, 31, 32, 33, 63, 64, 65, 127, 128, 257 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			const std::size_t size = sizes[sizeIndex];
			std::mt19937 random(static_cast<std::uint32_t>(0x51B10000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 11) / 11.0f);
			if (sizeIndex % 4 == 0) std::sort(values.begin(), values.end());
			else if (sizeIndex % 4 == 1) std::sort(values.rbegin(), values.rend());
			else if (sizeIndex % 4 == 2 && size > 0) std::fill(values.begin(), values.end(), 0.5f);
			else std::shuffle(values.begin(), values.end(), random);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Simplified Library float size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		}
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Simplified Library duplicate integer keys",
				integers({ 6, 2, 6, 1, 4, 2, 0, 5, 1, 3, 6, 0, 4, 2, 1, 5 }), InputProfile::IntegerKeys,
				algorithm, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest request;
		request.algorithm = algorithm;
		std::mt19937 random(0x0800B11u);
		for (std::size_t i = 0; i < 800; ++i)
			request.input.push_back(SortItem(4000 + i, static_cast<float>(random() % 89) / 89.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			check(trace.complete && isSorted(replay, request.direction) && trace.snapshots.size() > 2 &&
				replay.auxiliaryItems.find("simplified-library-items") == replay.auxiliaryItems.end() &&
				replay.integerWorkspaces.find("simplified-library-counts") == replay.integerWorkspaces.end() &&
				replay.integerWorkspaces.find("simplified-library-locations") == replay.integerWorkspaces.end(),
				"Simplified Library Sort rebalances 800 items across snapshots and releases temporary workspaces");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState),
				"Simplified Library 800-item trace rewinds exactly across its rebalances");
		}

		TraceBuildRequest partialRequest = request;
		partialRequest.input.resize(40);
		partialRequest.maxOperations = 80;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Simplified Library operation-limited rebalance trace rewinds exactly");
	}

	void testLibrarySort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvLibrary;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "insertion-methods" && descriptor->variantId == "library" &&
			!descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Library Sort advertises randomized equal-key shelf placement and linear auxiliary storage");

		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 51, 52, 63, 64, 65 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			const std::size_t size = sizes[sizeIndex];
			const std::size_t patternIndex = sizeIndex;
			std::mt19937 random(static_cast<std::uint32_t>(0x11B20000u + size));
			std::vector<float> values;
			for (std::size_t i = 0; i < size; ++i) values.push_back(static_cast<float>(random() % 13) / 13.0f);
			if (patternIndex % 5 == 0) std::sort(values.begin(), values.end());
			else if (patternIndex % 5 == 1) std::sort(values.rbegin(), values.rend());
			else if (patternIndex % 5 == 2 && size != 0) std::fill(values.begin(), values.end(), 0.5f);
			else std::shuffle(values.begin(), values.end(), random);
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Library Sort float size " + std::to_string(size), floats(values),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		}
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Library Sort duplicate integer keys",
				integers({ 6, 2, 6, 1, 4, 2, 0, 5, 1, 3, 6, 0, 4, 2, 1, 5, 3, 6, 0, 2, 4, 1, 5, 3 }),
				InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest deterministic;
		deterministic.algorithm = algorithm;
		deterministic.input = floats(std::vector<float>(65, 0.5f));
		deterministic.inputSeed = 0x1B2D3F4u;
		const SortTrace first = SortTraceBuilder::build(deterministic);
		const SortTrace second = SortTraceBuilder::build(deterministic);
		bool sameEvents = first.events.size() == second.events.size();
		for (std::size_t i = 0; sameEvents && i < first.events.size(); ++i)
		{
			const SemanticEvent& a = first.events[i];
			const SemanticEvent& b = second.events[i];
			sameEvents = a.type == b.type && a.sequence == b.sequence && a.firstIndex == b.firstIndex &&
				a.secondIndex == b.secondIndex && a.firstItemId == b.firstItemId && a.secondItemId == b.secondItemId &&
				a.comparison == b.comparison && a.firstLane == b.firstLane && a.secondLane == b.secondLane;
		}
		const SortMetrics& metrics = first.snapshots.rbegin()->second.metrics;
		check(first.complete && second.complete && sameEvents &&
			metrics.algorithm.find("library_tie_choices") != metrics.algorithm.end() &&
			metrics.algorithm.find("library_tie_choices")->second > 0,
			"Library Sort uses its run seed for reproducible equal-key shelf choices");

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		stress.inputSeed = 0x80011B2u;
		std::mt19937 stressRandom(0x80011B2u);
		for (std::size_t i = 0; i < 800; ++i)
			stress.input.push_back(SortItem(8000 + i, static_cast<float>(stressRandom() % 113) / 113.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
				replay.auxiliaryItems.find("library-shelves") == replay.auxiliaryItems.end(),
				"Library Sort sorts 800 items across snapshots and releases its shelf workspace");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Library Sort 800-item trace rewinds exactly across rebalances");
		}

		TraceBuildRequest partialRequest = deterministic;
		partialRequest.input = floats(std::vector<float>(48, 0.5f));
		partialRequest.maxOperations = 96;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Library Sort operation-limited shelf trace rewinds exactly");
	}

	void testWeaveMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvWeaveMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "weave-merge" &&
			!descriptor->stable && descriptor->inPlace && descriptor->safeMaxItemCount == 128,
			"Weave Merge advertises its unstable in-place weave schedule and bounded quadratic ceiling");
		check(!algorithmAcceptsItemCount(algorithm, 129), "Weave Merge disables runs above its safe operation ceiling");
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.2f, 0.1f }, { 0.3f, 0.1f, 0.2f },
			{ 0.4f, 0.1f, 0.3f, 0.2f, 0.5f }, { 0.2f, 0.2f, 0.2f, 0.2f },
			{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f },
			{ 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f }
		};
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Weave Merge float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Weave Merge duplicate integer keys",
				integers({ 7, 2, 5, 2, 0, 6, 3, 1, 4, 2, 7, 0 }), InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest sourceShape;
		sourceShape.algorithm = algorithm;
		sourceShape.input = integers({ 2, 1, 1 });
		sourceShape.profile = InputProfile::IntegerKeys;
		sourceShape.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace sourceTrace = SortTraceBuilder::build(sourceShape);
		SortState sourceState = sourceTrace.initialState;
		for (std::size_t i = 0; i < sourceTrace.events.size(); ++i) applyEventForward(sourceState, sourceTrace.events[i]);
		check(sourceTrace.complete && isSorted(sourceState, SortDirection::Ascending) &&
			sourceState.slots[0].item.stableId == sourceShape.input[2].stableId &&
			sourceState.slots[1].item.stableId == sourceShape.input[1].stableId,
			"Weave Merge retains the pinned equal-key swaps instead of claiming stability");
		const SortMetrics& sourceMetrics = sourceState.metrics;
		check(sourceMetrics.algorithm.find("weave_recursive_merges") != sourceMetrics.algorithm.end() &&
			sourceMetrics.algorithm.find("weave_interleave_swaps") != sourceMetrics.algorithm.end() &&
			sourceMetrics.algorithm.find("weave_insertion_comparisons") != sourceMetrics.algorithm.end() &&
			sourceMetrics.algorithm.find("weave_insertion_swaps") != sourceMetrics.algorithm.end(),
			"Weave Merge records recursion, interleave, and cleanup-pass behavior");

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		std::mt19937 random(0xA11CE128u);
		for (std::size_t i = 0; i < 128; ++i)
			stress.input.push_back(SortItem(12000 + i, static_cast<float>(random() % 31) / 31.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 1,
				"Weave Merge sorts its 128-item ceiling across trace snapshots");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Weave Merge 128-item trace rewinds exactly");
		}

		TraceBuildRequest partialRequest = sourceShape;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0 });
		partialRequest.maxOperations = 72;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Weave Merge operation-limited interleave rewinds exactly");
	}

	void testLaziestStableSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvLaziest;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "laziest" &&
			descriptor->stable && descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Laziest Stable Sort advertises stable in-place run folding with no size cap");
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.2f, 0.1f }, { 0.4f, 0.1f, 0.3f, 0.2f },
			{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f },
			{ 0.5f, 0.4f, 0.3f, 0.2f, 0.1f },
			{ 0.7f, 0.1f, 0.5f, 0.1f, 0.3f, 0.7f, 0.2f, 0.6f, 0.1f, 0.4f, 0.5f, 0.0f, 0.3f, 0.2f, 0.6f, 0.4f, 0.1f }
		};
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Laziest Stable Sort float shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Laziest Stable Sort duplicate integer keys",
				integers({ 7, 2, 5, 2, 0, 6, 3, 1, 4, 2, 7, 0, 3, 6, 1, 5, 4 }), InputProfile::IntegerKeys,
				algorithm, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		std::mt19937 random(0x1A2E57u);
		for (std::size_t i = 0; i < 800; ++i)
			stress.input.push_back(SortItem(16000 + i, static_cast<float>(random() % 127) / 127.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			const SortMetrics& metrics = replay.metrics;
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
				metrics.algorithm.find("laziest_merges") != metrics.algorithm.end() && metrics.algorithm.find("laziest_merges")->second > 0 &&
				metrics.algorithm.find("laziest_exponential_probes") != metrics.algorithm.end() &&
				metrics.algorithm.find("laziest_exponential_probes")->second > 0,
				"Laziest Stable Sort folds 800 items through exponential-search merges and snapshots");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Laziest 800-item trace rewinds exactly across its in-place rotations");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 8;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Laziest operation-limited run formation rewinds exactly");
	}

	void testLazierestStableSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvLazierest;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "lazierest" &&
			descriptor->stable && descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Lazierest Stable Sort advertises stable in-place cube-root runs and fragmented folds");
		// Reproduce the reported application input: normalized values are created
		// in order, then shuffled with the user's seed before trace generation.
		std::vector<SortItem> reportedInput;
		for (std::size_t i = 0; i < 400; ++i)
			reportedInput.push_back(SortItem(i, static_cast<float>(i + 1) / 400.0f));
		std::mt19937 reportedSeed(3879711141u);
		std::shuffle(reportedInput.begin(), reportedInput.end(), reportedSeed);
		verifyAlgorithmCase("Lazierest Stable Sort reported 400-item seed 3879711141",
			reportedInput, InputProfile::NormalizedFloat, algorithm, SortDirection::Ascending);

		std::mt19937 random(0x1A2E57A3u);
		const std::size_t sizes[] = { 0, 1, 2, 3, 15, 16, 17, 18, 25, 26, 27, 28, 35, 36, 37, 63, 64, 65, 80, 127, 128 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i) values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? sizes[sizeIndex] : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Lazierest Stable Sort boundary size " + std::to_string(sizes[sizeIndex]),
					integers(values), InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Lazierest Stable Sort equal-key stability",
				integers({ 4, 1, 4, 2, 4, 1, 3, 2, 0, 3, 0, 4, 1, 3, 2, 0, 4, 2, 1, 3, 0, 2, 4, 1, 3, 0, 2 }),
				InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		for (std::size_t i = 0; i < 800; ++i)
			stress.input.push_back(SortItem(18100 + i, static_cast<float>(random() % 97) / 97.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			const SortMetrics& metrics = replay.metrics;
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
				metrics.algorithm.find("lazierest_fragmented_merges") != metrics.algorithm.end() &&
				metrics.algorithm.find("lazierest_fragmented_merges")->second > 0,
				"Lazierest Stable Sort performs its fragmented square-run folds on 800 items");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Lazierest 800-item trace rewinds exactly across snapshots");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7, 3, 6, 4, 5, 0 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 5;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Lazierest operation-limited run formation rewinds exactly");
	}

	void testLazyStableMergeVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvLazyStable, SortAlgorithm::ArrayvOptimizedLazyStable };
		const std::size_t sizes[] = { 0, 1, 2, 3, 15, 16, 17, 18, 31, 32, 33, 47, 63, 64, 65, 127, 128 };
		std::mt19937 random(0x6A2E5701u);
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithmIndex)
		{
			const SortAlgorithm algorithm = algorithms[algorithmIndex];
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
			check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable && descriptor->inPlace &&
				descriptor->safeMaxItemCount == MaxItemCount,
				std::string(algorithmDisplayName(algorithm)) + " advertises stable no-buffer merge capabilities");
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i) values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? sizes[sizeIndex] : 1)));
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " boundary size " + std::to_string(sizes[sizeIndex]),
						integers(values), InputProfile::IntegerKeys, algorithm,
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);
			}
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " duplicate stability",
					integers({ 4, 1, 4, 2, 4, 1, 3, 2, 0, 3, 0, 4, 1, 3, 2, 0, 4, 2, 1, 3, 0, 2, 4, 1, 3, 0, 2 }),
					InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

			TraceBuildRequest stress;
			stress.algorithm = algorithm;
			for (std::size_t i = 0; i < 800; ++i)
				stress.input.push_back(SortItem(19000 + i, static_cast<float>(random() % 97) / 97.0f));
			for (int direction = 0; direction < 2; ++direction)
			{
				stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const SortTrace trace = SortTraceBuilder::build(stress);
				SortState replay = trace.initialState;
				for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
				check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2,
					std::string(algorithmDisplayName(algorithm)) + " sorts 800 items across trace snapshots");
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
				check(sameState(replay, trace.initialState),
					std::string(algorithmDisplayName(algorithm)) + " 800-item trace rewinds exactly");
			}
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvOptimizedLazyStable;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7, 3, 6, 4, 5, 0, 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 40;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Optimized Lazy Stable operation-limited run rewinds exactly");
	}

	void testFifthAndQuadMergeSorts()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvFifthMerge, SortAlgorithm::ArrayvQuadMerge };
		const std::size_t sizes[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127 };
		std::mt19937 random(0x51F7A11u);
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithmIndex)
		{
			const SortAlgorithm algorithm = algorithms[algorithmIndex];
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
			check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable && !descriptor->inPlace &&
				descriptor->safeMaxItemCount == MaxItemCount,
				std::string(algorithmDisplayName(algorithm)) + " advertises stable multiway workspace merging");
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i) values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? sizes[sizeIndex] : 1)));
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " boundary size " + std::to_string(sizes[sizeIndex]),
						integers(values), InputProfile::IntegerKeys, algorithm,
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);
			}
			for (int direction = 0; direction < 2; ++direction)
			{
				const SortDirection sortDirection = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " all-equal values",
					integers(std::vector<int>(33, 4)), InputProfile::IntegerKeys, algorithm, sortDirection,
					InputDomain::NonnegativeIntegerKeys);
				verifyAlgorithmCase(std::string(algorithmDisplayName(algorithm)) + " reverse pattern",
					integers({ 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }),
					InputProfile::IntegerKeys, algorithm, sortDirection, InputDomain::NonnegativeIntegerKeys);
			}

			TraceBuildRequest stress;
			stress.algorithm = algorithm;
			for (std::size_t i = 0; i < 800; ++i)
				stress.input.push_back(SortItem(20000 + i, static_cast<float>(random() % 113) / 113.0f));
			for (int direction = 0; direction < 2; ++direction)
			{
				stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const SortTrace trace = SortTraceBuilder::build(stress);
				SortState replay = trace.initialState;
				for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
				check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
					replay.metrics.peakAuxiliaryCapacity >= 800,
					std::string(algorithmDisplayName(algorithm)) + " sorts 800 items through its reusable buffer");
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
				check(sameState(replay, trace.initialState), std::string(algorithmDisplayName(algorithm)) + " 800-item trace rewinds exactly");
			}
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvQuadMerge;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7, 3, 6, 4, 5, 0, 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 40;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState), "Quad Sort operation-limited trace rewinds exactly");
	}

	void testBufferPartitionMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvBufferPartitionMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "buffer-partition" &&
			!descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Buffer Partition Merge advertises median-selected unstable partitions and an auxiliary merge lane");
		std::mt19937 random(0xB0FF3A71u);
		const std::size_t sizes[] = { 0, 1, 2, 3, 15, 16, 17, 18, 31, 32, 33, 63, 64, 65, 127, 128 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i) values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? sizes[sizeIndex] : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Buffer Partition Merge boundary size " + std::to_string(sizes[sizeIndex]),
					integers(values), InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}
		for (int direction = 0; direction < 2; ++direction)
		{
			const SortDirection sortDirection = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			verifyAlgorithmCase("Buffer Partition Merge all-equal keys", integers(std::vector<int>(65, 7)),
				InputProfile::IntegerKeys, algorithm, sortDirection, InputDomain::NonnegativeIntegerKeys);
			verifyAlgorithmCase("Buffer Partition Merge reverse order", integers({ 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
				15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }),
				InputProfile::IntegerKeys, algorithm, sortDirection, InputDomain::NonnegativeIntegerKeys);
		}

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		for (std::size_t i = 0; i < 800; ++i)
			stress.input.push_back(SortItem(21000 + i, static_cast<float>(random() % 131) / 131.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			const SortMetrics& metrics = replay.metrics;
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
				metrics.algorithm.find("buffer_partition_selections") != metrics.algorithm.end() &&
				metrics.algorithm.find("buffer_partition_selections")->second > 0,
				"Buffer Partition Merge sorts 800 items with median-rank splits across snapshots");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Buffer Partition Merge 800-item trace rewinds exactly");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.input = integers({ 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7, 3, 6, 4, 5, 0, 8, 1, 7, 2, 6, 3, 5, 4, 0, 9, 1, 8, 2, 7 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 24;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState), "Buffer Partition Merge partial selection trace rewinds exactly");
	}

	void testParallelBlockMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvParallelBlockMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "parallel-block-merge" &&
			descriptor->stable && !descriptor->inPlace && descriptor->safeMaxItemCount == MaxItemCount,
			"Parallel Block Merge advertises stable disjoint block phases and one reusable workspace");
		std::mt19937 random(0xA112E115u);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i) values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? sizes[sizeIndex] : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Parallel Block Merge boundary size " + std::to_string(sizes[sizeIndex]),
					integers(values), InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}
		for (int direction = 0; direction < 2; ++direction)
			verifyAlgorithmCase("Parallel Block Merge duplicate stability",
				integers({ 4, 1, 4, 2, 4, 1, 3, 2, 0, 3, 0, 4, 1, 3, 2, 0, 4, 2, 1, 3, 0, 2, 4, 1, 3, 0, 2 }),
				InputProfile::IntegerKeys, algorithm,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);

		TraceBuildRequest stress;
		stress.algorithm = algorithm;
		for (std::size_t i = 0; i < 800; ++i)
			stress.input.push_back(SortItem(22000 + i, static_cast<float>(random() % 127) / 127.0f));
		for (int direction = 0; direction < 2; ++direction)
		{
			stress.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(stress);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			const SortMetrics& metrics = replay.metrics;
			check(trace.complete && isSorted(replay, stress.direction) && trace.snapshots.size() > 2 &&
				metrics.algorithm.find("parallel_block_tasks") != metrics.algorithm.end() &&
				metrics.algorithm.find("parallel_block_tasks")->second > 0 &&
				metrics.algorithm.find("parallel_block_layers") != metrics.algorithm.end() &&
				metrics.algorithm.find("parallel_block_layers")->second > 0,
				"Parallel Block Merge sorts 800 items in grouped independent merge layers");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Parallel Block Merge 800-item trace rewinds exactly");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.input = integers({ 16, 1, 15, 2, 14, 3, 13, 4, 12, 5, 11, 6, 10, 7, 9, 8, 0, 17, 3, 16, 4, 15, 5, 14, 6, 13, 7, 12, 8, 11, 9, 10, 1 });
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.maxOperations = 35;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialState, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialState, partial.events[i - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState), "Parallel Block Merge partial phase trace rewinds exactly");
	}

	void testOptimizedRotateMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvOptimizedRotateMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "optimized-rotate" &&
			descriptor->stable && !descriptor->inPlace && descriptor->supportedOptions.size() == 5,
			"Optimized Rotate Merge advertises stable buffered merging and five temporary-buffer choices");
		for (std::size_t option = 0; option < descriptor->supportedOptions.size(); ++option)
		{
			const AlgorithmOptionDescriptor& choice = descriptor->supportedOptions[option];
			check(std::string(choice.groupId) == "rotate_buffer_capacity" && choice.value == (16 << option) &&
				choice.value > 0 && (choice.value & (choice.value - 1)) == 0,
				"Optimized Rotate Merge buffer options are stable-ID powers of two");
		}

		std::mt19937 random(0x0A71A1EDu);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 31, 32, 33, 63, 64, 65, 127, 255, 257 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
				values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 3 + 1) : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Optimized Rotate Merge boundary/duplicate size " + std::to_string(sizes[sizeIndex]),
					integers(values), InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}

		std::vector<int> values;
		for (std::size_t i = 0; i < 800; ++i) values.push_back(static_cast<int>(random() % 83));
		std::uint64_t previousBufferMerges = 0, previousFallbackMerges = 0;
		for (std::size_t choice = 0; choice < descriptor->supportedOptions.size(); ++choice)
		{
			TraceBuildRequest request;
			request.algorithm = algorithm;
			request.input = integers(values);
			request.profile = InputProfile::IntegerKeys;
			request.domain = InputDomain::NonnegativeIntegerKeys;
			request.rotateMergeBufferSize = descriptor->supportedOptions[choice].value;
			for (int direction = 0; direction < 2; ++direction)
			{
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(replay, trace.events[event]);
				const std::uint64_t bufferMerges = replay.metrics.algorithm.find("optimized_rotate_buffer_merges")->second;
				const std::uint64_t fallbackMerges = replay.metrics.algorithm.find("optimized_rotate_in_place_merges")->second;
				const bool hasExpectedBufferedPath = request.rotateMergeBufferSize == 16 || bufferMerges > 0;
				if (!trace.complete || trace.rotateMergeBufferSize != request.rotateMergeBufferSize || !isSorted(replay, request.direction) ||
					!hasExpectedBufferedPath || fallbackMerges == 0)
					std::cerr << "Optimized Rotate diagnostic: capacity=" << request.rotateMergeBufferSize << " direction=" << direction
						<< " complete=" << trace.complete << " traceCapacity=" << trace.rotateMergeBufferSize
						<< " sorted=" << isSorted(replay, request.direction) << " bufferMerges=" << bufferMerges
						<< " fallbackMerges=" << fallbackMerges << " events=" << trace.events.size() << '\n';
				check(trace.complete && trace.rotateMergeBufferSize == request.rotateMergeBufferSize && isSorted(replay, request.direction) &&
					hasExpectedBufferedPath && fallbackMerges > 0,
					"Optimized Rotate Merge sorts 800 duplicate-heavy items with both configured merge paths");
				if (direction == 0)
				{
					if (choice > 0)
						check(bufferMerges >= previousBufferMerges && fallbackMerges <= previousFallbackMerges,
							"Larger temporary buffers shift work from rotation fallback to buffered merges");
					previousBufferMerges = bufferMerges;
					previousFallbackMerges = fallbackMerges;
				}
				for (std::size_t event = trace.events.size(); event > 0; --event) applyEventBackward(replay, trace.events[event - 1]);
				check(sameState(replay, trace.initialState), "Optimized Rotate Merge 800-item trace rewinds exactly");
			}
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.rotateMergeBufferSize = 16;
		partialRequest.maxOperations = 37;
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.input = integers({ 16, 1, 15, 2, 14, 3, 13, 4, 12, 5, 11, 6, 10, 7, 9, 8, 0, 17, 3, 16, 4, 15, 5, 14, 6, 13, 7, 12, 8, 11, 9, 10, 1 });
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState), "Optimized Rotate Merge operation-limited trace rewinds exactly");
	}

	void testGrailSortModes()
	{
		using namespace sortinglogic;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvGrail);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "grail" &&
			descriptor->stable && descriptor->supportedOptions.size() == 3,
			"Grail Sort exposes in-place, static, and dynamic buffer modes");
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127 };
		std::mt19937 random(0x6A41A11u);
		for (std::size_t mode = 0; mode < 3; ++mode)
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
					values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 4 + 1) : 1)));
				for (int direction = 0; direction < 2; ++direction)
				{
					TraceBuildRequest request;
					request.algorithm = SortAlgorithm::ArrayvGrail;
					request.bucketCount = static_cast<int>(mode);
					request.profile = InputProfile::IntegerKeys;
					request.domain = InputDomain::NonnegativeIntegerKeys;
					request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
					request.input = integers(values);
					const SortTrace trace = SortTraceBuilder::build(request);
					check(trace.bucketCount == static_cast<int>(mode), "Grail Sort retains its selected buffer mode in the trace");
					verifyAlgorithmCase("Grail Sort option mode boundary case", integers(values), InputProfile::IntegerKeys,
						SortAlgorithm::ArrayvGrail, request.direction, InputDomain::NonnegativeIntegerKeys);
				}
			}
		std::vector<int> values;
		for (std::size_t i = 0; i < 800; ++i) values.push_back(static_cast<int>(random() % 73));
		for (int mode = 0; mode < 3; ++mode)
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.algorithm = SortAlgorithm::ArrayvGrail;
				request.bucketCount = mode;
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.input = integers(values);
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
				const std::uint64_t buffered = replay.metrics.algorithm.find("grail_buffered_merges")->second;
				const std::uint64_t rotations = replay.metrics.algorithm.find("grail_rotation_merges")->second;
				check(trace.complete && isSorted(replay, request.direction) &&
					(mode == 0 ? (buffered == 0 && rotations > 0) : buffered > 0),
					"Grail Sort 800-item run follows its selected buffer strategy");
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
				check(sameState(replay, trace.initialState), "Grail Sort 800-item trace rewinds exactly");
			}
	}

	void testAdaptiveGrailSort()
	{
		using namespace sortinglogic;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvAdaptiveGrail);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "adaptive-grail" &&
			descriptor->stable && descriptor->inPlace,
			"Adaptive Grail advertises stable natural-run adaptation without item workspace");
		std::mt19937 random(0xADA971E5u);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 31, 32, 33, 63, 64, 65, 127, 257 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
				values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 3 + 1) : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Adaptive Grail boundary and duplicate case", integers(values), InputProfile::IntegerKeys,
					SortAlgorithm::ArrayvAdaptiveGrail,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}
		for (int direction = 0; direction < 2; ++direction)
		{
			std::vector<int> mostlySorted;
			for (int i = 0; i < 800; ++i) mostlySorted.push_back(i / 4);
			for (int i = 0; i < 32; ++i) std::swap(mostlySorted[(i * 23) % 800], mostlySorted[(i * 71 + 17) % 800]);
			verifyAlgorithmCase("Adaptive Grail 800-item partially ordered input", integers(mostlySorted), InputProfile::IntegerKeys,
				SortAlgorithm::ArrayvAdaptiveGrail,
				direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
				InputDomain::NonnegativeIntegerKeys);
		}
	}

	void testPDQVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvPDQBranched, SortAlgorithm::ArrayvPDQBranchless };
		std::mt19937 random(0x0D0A51E5u);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127 };
		for (std::size_t algorithmIndex = 0; algorithmIndex < 2; ++algorithmIndex)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithmIndex]);
			check(descriptor && descriptor->familyId == "quick" && descriptor->safeMaxItemCount == MaxItemCount && !descriptor->stable,
				"PDQ partition variant is registered as an unstable quicksort with the full safe item limit");
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
					values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 4 + 1) : 1)));
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase("PDQ boundary and duplicate case", integers(values), InputProfile::IntegerKeys,
						algorithms[algorithmIndex], direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);
			}
			for (int direction = 0; direction < 2; ++direction)
			{
				const SortDirection sortDirection = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				verifyAlgorithmCase("PDQ sorted equal-key pattern", integers(std::vector<int>(800, 5)), InputProfile::IntegerKeys,
					algorithms[algorithmIndex], sortDirection, InputDomain::NonnegativeIntegerKeys);
				std::vector<int> organPipe;
				for (int i = 0; i < 400; ++i) organPipe.push_back(i);
				for (int i = 399; i >= 0; --i) organPipe.push_back(i);
				verifyAlgorithmCase("PDQ organ-pipe pattern", integers(organPipe), InputProfile::IntegerKeys,
					algorithms[algorithmIndex], sortDirection, InputDomain::NonnegativeIntegerKeys);
			}
		}
	}

	void testSqrtMergeVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvSqrt, SortAlgorithm::ArrayvSynchronousSqrt };
		std::mt19937 random(0x5A271D00u);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 257 };
		for (std::size_t algorithmIndex = 0; algorithmIndex < 2; ++algorithmIndex)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithmIndex]);
			check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable && !descriptor->inPlace,
				"Square-root merge variant advertises stable buffered workspace behavior");
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
					values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 3 + 1) : 1)));
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase("Square-root merge boundary and duplicate case", integers(values), InputProfile::IntegerKeys,
						algorithms[algorithmIndex], direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);
			}
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[algorithmIndex];
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				std::vector<int> stressValues;
				for (std::size_t i = 0; i < 800; ++i) stressValues.push_back(static_cast<int>(random() % 91));
				request.input = integers(stressValues);
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
				const std::map<std::string, std::uint64_t>::const_iterator passes = replay.metrics.algorithm.find("sqrt_merge_passes");
				check(trace.complete && isSorted(replay, request.direction) && passes != replay.metrics.algorithm.end() && passes->second > 0,
					"Square-root merge sorts 800 items with its configured √n runs");
				if (algorithmIndex == 1)
				{
					const std::map<std::string, std::uint64_t>::const_iterator layers = replay.metrics.algorithm.find("sqrt_synchronous_layers");
					check(layers != replay.metrics.algorithm.end() && layers->second == passes->second,
						"Synchronous Sqrt groups each merge pass into a phase layer");
				}
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
				check(sameState(replay, trace.initialState), "Square-root merge 800-item trace rewinds exactly");
			}
		}
	}

	void testEctaSort()
	{
		using namespace sortinglogic;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(SortAlgorithm::ArrayvEcta);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->variantId == "ecta" && descriptor->stable,
			"Ecta is cataloged as a stable adaptive-run hybrid merge strategy");
		std::mt19937 random(0xEC7A50A7u);
		const std::size_t sizes[] = { 0, 1, 2, 15, 16, 31, 32, 33, 63, 64, 65, 127, 257 };
		for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
		{
			std::vector<int> values;
			for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
				values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 3 + 1) : 1)));
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Ecta run and merge boundary case", integers(values), InputProfile::IntegerKeys,
					SortAlgorithm::ArrayvEcta, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);
		}
		std::vector<int> values;
		for (std::size_t i = 0; i < 800; ++i) values.push_back(static_cast<int>(random() % 89));
		for (int direction = 0; direction < 2; ++direction)
		{
			TraceBuildRequest request;
			request.algorithm = SortAlgorithm::ArrayvEcta;
			request.profile = InputProfile::IntegerKeys;
			request.domain = InputDomain::NonnegativeIntegerKeys;
			request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			request.input = integers(values);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState replay = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
			const std::uint64_t fourWay = replay.metrics.algorithm.find("ecta_four_way_groups")->second;
			const std::uint64_t binary = replay.metrics.algorithm.find("ecta_binary_merges")->second;
			check(trace.complete && isSorted(replay, request.direction) && fourWay > 0 && binary > 0,
				"Ecta 800-item trace exercises four-way ping-pong groups and the binary merge tail");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "Ecta 800-item trace rewinds exactly");
		}
		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvEcta;
		partialRequest.maxOperations = 48;
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.input = integers({ 16, 1, 15, 2, 14, 3, 13, 4, 12, 5, 11, 6, 10, 7, 9, 8, 0, 17, 3, 16, 4, 15, 5, 14, 6, 13, 7, 12, 8, 11, 9, 10, 1 });
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialReplay = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialReplay, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialReplay, partial.events[i - 1]);
		check(!partial.complete && sameState(partialReplay, partial.initialState), "Ecta partial ping-pong trace rewinds exactly");
	}

	void testCircularAndUnstableGrailSorts()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvCircularGrail, SortAlgorithm::ArrayvUnstableGrail };
		std::mt19937 random(0xC1AC01A2u);
		const std::size_t sizes[] = { 0, 1, 2, 7, 8, 15, 16, 17, 31, 32, 63, 64, 65, 127 };
		for (std::size_t algorithmIndex = 0; algorithmIndex < 2; ++algorithmIndex)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithmIndex]);
			check(descriptor && descriptor->familyId == "merge-hybrid" &&
				descriptor->stable == (algorithmIndex == 0),
				"Circular and unstable Grail strategies expose their distinct stability contracts");
			for (std::size_t sizeIndex = 0; sizeIndex < sizeof(sizes) / sizeof(sizes[0]); ++sizeIndex)
			{
				std::vector<int> values;
				for (std::size_t i = 0; i < sizes[sizeIndex]; ++i)
					values.push_back(static_cast<int>(random() % (sizes[sizeIndex] ? (sizes[sizeIndex] / 4 + 1) : 1)));
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase("Grail variant boundary and duplicate case", integers(values), InputProfile::IntegerKeys,
						algorithms[algorithmIndex], direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);
			}
			std::vector<int> values;
			for (std::size_t i = 0; i < 800; ++i) values.push_back(static_cast<int>(random() % 61));
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.algorithm = algorithms[algorithmIndex];
				request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				request.input = integers(values);
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(replay, trace.events[i]);
				check(trace.complete && isSorted(replay, request.direction), "Grail variant sorts an 800-item duplicate-heavy input");
				if (algorithmIndex == 0)
					check(replay.metrics.algorithm.find("circular_grail_wraps")->second > 0,
						"Circular Grail exercises wrapped ring-buffer regions");
				else
					check(replay.metrics.algorithm.find("unstable_grail_equal_right_choices")->second > 0,
						"Unstable Grail exposes right-biased equal-key merge choices");
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
				check(sameState(replay, trace.initialState), "Grail variant 800-item trace rewinds exactly");
			}
		}
	}

	void testDropMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvDropMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && !descriptor->stable,
			"Drop Merge advertises its family and unstable suffix-sort contract");
		const std::vector<std::vector<int> > shapes = {
			{}, { 0 }, { 3, 1, 2, 1, 3, 0 }, { 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }
		};
		for (std::size_t shape = 0; shape < shapes.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Drop Merge boundary shape " + std::to_string(shape), integers(shapes[shape]),
					InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		std::vector<int> mergePath;
		for (int value = 0; value < 20; ++value) mergePath.push_back(value);
		for (int value = 0; value < 20; ++value) mergePath.push_back(value);
		for (int value = 40; value < 80; ++value) mergePath.push_back(value);
		TraceBuildRequest mergeRequest;
		mergeRequest.algorithm = algorithm;
		mergeRequest.profile = InputProfile::IntegerKeys;
		mergeRequest.domain = InputDomain::NonnegativeIntegerKeys;
		mergeRequest.input = integers(mergePath);
		for (int direction = 0; direction < 2; ++direction)
		{
			mergeRequest.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			std::vector<int> directionalMergePath = mergePath;
			if (direction != 0)
				for (std::size_t i = 0; i < directionalMergePath.size(); ++i) directionalMergePath[i] = 79 - directionalMergePath[i];
			mergeRequest.input = integers(directionalMergePath);
			const SortTrace trace = SortTraceBuilder::build(mergeRequest);
			SortState end = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(end, trace.events[i]);
			const std::map<std::string, std::uint64_t>& metrics = end.metrics.algorithm;
			check(trace.complete && isSorted(end, mergeRequest.direction) && metrics.find("drop_merge_final_merges")->second == 1 &&
				metrics.find("drop_merge_suffix_heap_sorts")->second == 1 && metrics.find("drop_merge_early_fallbacks")->second == 0,
				"Drop Merge sorts the buffered suffix and merges it for both directions");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(end, trace.events[i - 1]);
			check(sameState(end, trace.initialState), "Drop Merge suffix and merge trace rewinds exactly");
		}

		std::vector<int> reverseValues;
		for (int value = 79; value >= 0; --value) reverseValues.push_back(value);
		TraceBuildRequest fallbackRequest = mergeRequest;
		fallbackRequest.direction = SortDirection::Ascending;
		fallbackRequest.input = integers(reverseValues);
		const SortTrace fallback = SortTraceBuilder::build(fallbackRequest);
		SortState fallbackEnd = fallback.initialState;
		for (std::size_t i = 0; i < fallback.events.size(); ++i) applyEventForward(fallbackEnd, fallback.events[i]);
		check(fallback.complete && isSorted(fallbackEnd, SortDirection::Ascending) &&
			fallbackEnd.metrics.algorithm.find("drop_merge_early_fallbacks")->second == 1 &&
			fallbackEnd.metrics.algorithm.find("drop_merge_final_merges")->second == 0,
			"Drop Merge abandons a highly disordered monotone subsequence and heap-sorts the untouched input");
		for (std::size_t i = fallback.events.size(); i > 0; --i) applyEventBackward(fallbackEnd, fallback.events[i - 1]);
		check(sameState(fallbackEnd, fallback.initialState), "Drop Merge fallback trace rewinds exactly");

		std::vector<int> stress;
		for (int value = 0; value < 200; ++value) stress.push_back(value);
		for (int value = 0; value < 200; ++value) stress.push_back(value);
		for (int value = 400; value < 800; ++value) stress.push_back(value);
		TraceBuildRequest stressRequest = mergeRequest;
		stressRequest.input = integers(stress);
		stressRequest.direction = SortDirection::Ascending;
		const SortTrace stressTrace = SortTraceBuilder::build(stressRequest);
		SortState stressEnd = stressTrace.initialState;
		for (std::size_t i = 0; i < stressTrace.events.size(); ++i) applyEventForward(stressEnd, stressTrace.events[i]);
		check(stressTrace.complete && isSorted(stressEnd, SortDirection::Ascending) &&
			stressEnd.metrics.algorithm.find("drop_merge_final_merges")->second == 1,
			"Drop Merge exercises its buffered merge path at the 800-item product maximum");
		for (std::size_t i = stressTrace.events.size(); i > 0; --i) applyEventBackward(stressEnd, stressTrace.events[i - 1]);
		check(sameState(stressEnd, stressTrace.initialState), "Drop Merge 800-item trace rewinds exactly");

		TraceBuildRequest partialRequest = mergeRequest;
		partialRequest.input = integers(mergePath);
		partialRequest.maxOperations = 32;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialEnd = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialEnd, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialEnd, partial.events[i - 1]);
		check(!partial.complete && sameState(partialEnd, partial.initialState), "Drop Merge operation-limited prefix rewinds exactly");
	}

	void testParallelGrailSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvParallelGrail;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable,
			"Parallel Grail advertises its family and stable merge contract");
		const std::vector<std::vector<int> > shapes = {
			{}, { 0 }, { 2, 0, 2, 0, 1, 1 }, { 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }
		};
		for (std::size_t shape = 0; shape < shapes.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Parallel Grail boundary shape " + std::to_string(shape), integers(shapes[shape]),
					InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		std::mt19937 random(0x6A11F00Du);
		std::vector<int> stress;
		for (std::size_t i = 0; i < 800; ++i) stress.push_back(static_cast<int>(random() % 61));
		for (int direction = 0; direction < 2; ++direction)
		{
			TraceBuildRequest request;
			request.algorithm = algorithm;
			request.profile = InputProfile::IntegerKeys;
			request.domain = InputDomain::NonnegativeIntegerKeys;
			request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			request.input = integers(stress);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState end = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(end, trace.events[i]);
			const std::map<std::string, std::uint64_t>& metrics = end.metrics.algorithm;
			check(trace.complete && isSorted(end, request.direction) && metrics.find("parallel_grail_layers")->second > 0 &&
				metrics.find("parallel_grail_tasks")->second > 0 && metrics.find("parallel_grail_buffered_merges")->second > 0 &&
				metrics.find("parallel_grail_rotation_merges")->second > 0,
				"Parallel Grail sorts 800 duplicate-rich items and exercises buffered and rotation merges");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(end, trace.events[i - 1]);
			check(sameState(end, trace.initialState), "Parallel Grail 800-item phase trace rewinds exactly");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.input = integers(std::vector<int>{ 8, 1, 7, 2, 6, 3, 5, 4, 9, 0, 8, 2, 7, 3, 6, 4, 5, 1, 9, 0, 8, 3, 7, 4, 6, 5, 2, 1, 9, 0, 4, 3, 2, 1, 0 });
		partialRequest.maxOperations = 20;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialEnd = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialEnd, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialEnd, partial.events[i - 1]);
		check(!partial.complete && sameState(partialEnd, partial.initialState), "Parallel Grail operation-limited phase prefix rewinds exactly");
	}

	void testMedianMergeSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvMedianMerge;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && !descriptor->stable,
			"Median Merge advertises its hybrid family and partition-instability contract");
		const std::vector<std::vector<int> > shapes = {
			{}, { 0 }, { 2, 0, 2, 0, 1, 1 }, { 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }
		};
		for (std::size_t shape = 0; shape < shapes.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Median Merge boundary shape " + std::to_string(shape), integers(shapes[shape]),
					InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		std::vector<int> organPipe;
		for (int i = 0; i < 800; ++i) organPipe.push_back((std::min)(i, 800 - i));
		for (int direction = 0; direction < 2; ++direction)
		{
			TraceBuildRequest request;
			request.algorithm = algorithm;
			request.profile = InputProfile::IntegerKeys;
			request.domain = InputDomain::NonnegativeIntegerKeys;
			request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			request.input = integers(organPipe);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState end = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(end, trace.events[i]);
			const std::map<std::string, std::uint64_t>& metrics = end.metrics.algorithm;
			check(trace.complete && isSorted(end, request.direction) && metrics.find("median_merge_partitions")->second > 0 &&
				metrics.find("median_merge_bad_partitions")->second > 0 &&
				metrics.find("median_merge_median_of_medians_pivots")->second > 0 &&
				metrics.find("median_merge_merge_ranges")->second > 0,
				"Median Merge sorts an 800-item organ-pipe input and exercises bad-split pivot and merge fallbacks");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(end, trace.events[i - 1]);
			check(sameState(end, trace.initialState), "Median Merge 800-item partition and merge trace rewinds exactly");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.input = integers(std::vector<int>{ 8, 1, 7, 2, 6, 3, 5, 4, 9, 0, 8, 2, 7, 3, 6, 4, 5, 1, 9, 0, 8, 3, 7, 4, 6, 5, 2, 1, 9, 0, 4, 3, 2, 1, 0 });
		partialRequest.maxOperations = 32;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialEnd = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialEnd, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialEnd, partial.events[i - 1]);
		check(!partial.complete && sameState(partialEnd, partial.initialState), "Median Merge operation-limited prefix rewinds exactly");
	}

	void testRemiSort()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithm = SortAlgorithm::ArrayvRemi;
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		check(descriptor && descriptor->familyId == "merge-hybrid" && descriptor->stable,
			"Remi advertises its merge family and stable ordering contract");
		const std::vector<std::vector<int> > shapes = {
			{}, { 0 }, { 2, 0, 2, 0, 1, 1 }, { 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }
		};
		for (std::size_t shape = 0; shape < shapes.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Remi boundary shape " + std::to_string(shape), integers(shapes[shape]),
					InputProfile::IntegerKeys, algorithm,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NonnegativeIntegerKeys);

		std::mt19937 random(0xAE11D00Du);
		std::vector<int> stress;
		for (std::size_t i = 0; i < 800; ++i) stress.push_back(static_cast<int>(random() % 61));
		for (int direction = 0; direction < 2; ++direction)
		{
			TraceBuildRequest request;
			request.algorithm = algorithm;
			request.profile = InputProfile::IntegerKeys;
			request.domain = InputDomain::NonnegativeIntegerKeys;
			request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			request.input = integers(stress);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState end = trace.initialState;
			for (std::size_t i = 0; i < trace.events.size(); ++i) applyEventForward(end, trace.events[i]);
			const std::map<std::string, std::uint64_t>& metrics = end.metrics.algorithm;
			const std::map<std::string, std::vector<int> >::const_iterator cursorLane = end.integerWorkspaces.find("remi_run_cursors");
			check(trace.complete && isSorted(end, request.direction) && metrics.find("remi_branching_factor")->second >= 2 &&
				metrics.find("remi_initial_runs")->second > 1 && metrics.find("remi_merges")->second > 0 &&
				cursorLane == end.integerWorkspaces.end(),
				"Remi merges an 800-item duplicate-rich input through cbrt-way run groups and drains its cursor lane");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(end, trace.events[i - 1]);
			check(sameState(end, trace.initialState), "Remi 800-item cursor and workspace trace rewinds exactly");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = algorithm;
		partialRequest.profile = InputProfile::IntegerKeys;
		partialRequest.domain = InputDomain::NonnegativeIntegerKeys;
		partialRequest.input = integers(std::vector<int>{ 8, 1, 7, 2, 6, 3, 5, 4, 9, 0, 8, 2, 7, 3, 6, 4, 5, 1, 9, 0, 8, 3, 7, 4, 6, 5, 2, 1, 9, 0, 4, 3, 2, 1, 0 });
		partialRequest.maxOperations = 32;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialEnd = partial.initialState;
		for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialEnd, partial.events[i]);
		for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialEnd, partial.events[i - 1]);
		check(!partial.complete && sameState(partialEnd, partial.initialState), "Remi operation-limited run prefix rewinds exactly");
	}

	void testRemainingMergeRosterBatch(std::size_t firstAlgorithm = 0)
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvChalice, SortAlgorithm::ArrayvFlan, SortAlgorithm::ArrayvFlux,
			SortAlgorithm::ArrayvKota, SortAlgorithm::ArrayvOptimizedWeaveMerge, SortAlgorithm::ArrayvWiki,
			SortAlgorithm::ArrayvYujisBufferedMerge2, SortAlgorithm::ArrayvAndrey, SortAlgorithm::ArrayvNewShuffleMerge
		};
		const char* metrics[] = { "chalice_distinct_keys", "flan_partitions", "flux_analyzed_transitions", "kota_initial_blocks",
			"optimized_weave_blocks", "wiki_natural_runs", "yuji_runs", "andrey_runs", "new_shuffle_runs" };
		check(!algorithmAcceptsItemCount(SortAlgorithm::ArrayvNewShuffleMerge, 513),
			"New Shuffle Merge rejects counts above its trace-memory safety limit");
		const std::vector<std::vector<int> > shapes = {
			{}, { 0 }, { 2, 0, 2, 0, 1, 1 }, { 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }
		};
		for (std::size_t algorithm = firstAlgorithm; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[algorithm]);
			check(descriptor && descriptor->variantId.size() > 0, "tail roster variant is registered with a stable selection ID");
			for (std::size_t shape = 0; shape < shapes.size(); ++shape)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " boundary shape " + std::to_string(shape),
						integers(shapes[shape]), InputProfile::IntegerKeys, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NonnegativeIntegerKeys);

			std::mt19937 random(static_cast<std::uint32_t>(0xC0FFEEu + algorithm * 0x9E3779B9u));
			std::vector<int> values;
			const std::size_t stressCount = (std::min)(std::size_t(800), descriptor ? descriptor->safeMaxItemCount : std::size_t(800));
			for (std::size_t i = 0; i < stressCount; ++i) values.push_back(static_cast<int>(random() % 43));
			for (int direction = 0; direction < 2; ++direction)
			{
				TraceBuildRequest request;
				request.input = integers(values); request.algorithm = algorithms[algorithm]; request.profile = InputProfile::IntegerKeys;
				request.domain = InputDomain::NonnegativeIntegerKeys;
				request.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
				SortTrace trace = SortTraceBuilder::build(request);
				SortState end = trace.initialState;
				std::size_t snapshotsVerified = 0;
				for (std::size_t i = 0; i < trace.events.size(); ++i)
				{
					applyEventForward(end, trace.events[i]);
					const std::map<std::size_t, SortState>::const_iterator snapshot = trace.snapshots.find(i + 1);
					if (snapshot != trace.snapshots.end())
					{
						check(sameState(end, snapshot->second), std::string(algorithmDisplayName(algorithms[algorithm])) + " snapshot matches its large-run cursor");
						++snapshotsVerified;
					}
				}
				const std::map<std::string, std::uint64_t>::const_iterator metric = end.metrics.algorithm.find(metrics[algorithm]);
				check(trace.complete && isSorted(end, request.direction) && metric != end.metrics.algorithm.end() && metric->second > 0,
					std::string(algorithmDisplayName(algorithms[algorithm])) + " records its characteristic path at its tested trace-safe item count");
				check(snapshotsVerified > 0, std::string(algorithmDisplayName(algorithms[algorithm])) + " crosses and validates a 4096-event snapshot");
				for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(end, trace.events[i - 1]);
				check(sameState(end, trace.initialState), std::string(algorithmDisplayName(algorithms[algorithm])) + " large trace rewinds exactly");
			}

			TraceBuildRequest partialRequest;
			partialRequest.input = integers(std::vector<int>{ 8, 1, 7, 2, 6, 3, 5, 4, 9, 0, 8, 2, 7, 3, 6, 4, 5, 1, 9, 0, 8, 3, 7, 4, 6, 5, 2, 1, 9, 0, 4, 3, 2, 1, 0 });
			partialRequest.algorithm = algorithms[algorithm]; partialRequest.profile = InputProfile::IntegerKeys;
			partialRequest.domain = InputDomain::NonnegativeIntegerKeys; partialRequest.maxOperations = 32;
			const SortTrace partial = SortTraceBuilder::build(partialRequest);
			SortState partialEnd = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(partialEnd, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(partialEnd, partial.events[i - 1]);
			check(!partial.complete && sameState(partialEnd, partial.initialState),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " operation-limited prefix rewinds exactly");
		}
	}

	void testTernaryQuickVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvTernaryLLQuick,
			SortAlgorithm::ArrayvTernaryLRQuick
		};
		std::vector<int> values;
		for (int i = 0; i < 73; ++i) values.push_back((i * 29 + i / 4) % 11);
		std::mt19937 random(0x7E2A4A11u);
		std::shuffle(values.begin(), values.end(), random);
		std::vector<std::vector<int> > cases;
		cases.push_back(values);
		cases.push_back(std::vector<int>(19, 5));
		cases.push_back(std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 });
		cases.push_back(std::vector<int>{ 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
		for (std::size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
		{
			for (std::size_t c = 0; c < cases.size(); ++c)
				for (int d = 0; d < 2; ++d)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[a])) + " shape " + std::to_string(c),
						integers(cases[c]), InputProfile::IntegerKeys, algorithms[a],
						d == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithms[a]);
			check(descriptor && descriptor->familyId == "partition-sorts" && descriptor->safeMaxItemCount == 128,
				std::string(algorithmDisplayName(algorithms[a])) + " advertises its family and recursion safety limit");
		}
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvTernaryLRQuick;
		request.input = integers(values);
		request.profile = InputProfile::IntegerKeys;
		request.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace trace = SortTraceBuilder::build(request);
		const SortMetrics& metrics = trace.snapshots.rbegin()->second.metrics;
		check(metrics.algorithm.find("equal_key_moves") != metrics.algorithm.end() && metrics.algorithm.find("equal_key_moves")->second > 0,
			"Ternary LR Quick exercises bidirectional equal-key gathering");
		request.algorithm = SortAlgorithm::ArrayvTernaryLLQuick;
		TraceBuildRequest limitedRequest = request;
		limitedRequest.maxOperations = 1;
		const SortTrace limited = SortTraceBuilder::build(limitedRequest);
		SortState state = limited.initialState;
		for (std::size_t i = 0; i < limited.events.size(); ++i) applyEventForward(state, limited.events[i]);
		for (std::size_t i = limited.events.size(); i > 0; --i) applyEventBackward(state, limited.events[i - 1]);
		check(!limited.complete && sameState(state, limited.initialState), "ternary partition prefix rewinds exactly");
	}

	void testIndirectAndPermutationVariants()
	{
		using namespace sortinglogic;
		std::vector<int> factorialValues{ 4, 1, 3, 0, 2 };
		const std::vector<SortItem> factorialInput = integers(factorialValues);
		for (int d = 0; d < 2; ++d)
			verifyAlgorithmCase("Stable Permutation Sort factorial search",
				factorialInput, InputProfile::IntegerKeys, SortAlgorithm::ArrayvStablePermutation,
				d == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);
		const AlgorithmDescriptor* permutationDescriptor = algorithmDescriptor(SortAlgorithm::ArrayvStablePermutation);
		check(permutationDescriptor && permutationDescriptor->safeMaxItemCount == 8 && !permutationDescriptor->stable,
			"factorial permutation search publishes its safe limit and non-stable contract");
		TraceBuildRequest factorial;
		factorial.algorithm = SortAlgorithm::ArrayvStablePermutation;
		factorial.input = factorialInput;
		factorial.profile = InputProfile::IntegerKeys;
		factorial.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace factorialTrace = SortTraceBuilder::build(factorial);
		const SortMetrics& factorialMetrics = factorialTrace.snapshots.rbegin()->second.metrics;
		check(factorialMetrics.algorithm.find("permutations_examined") != factorialMetrics.algorithm.end() &&
			factorialMetrics.algorithm.find("permutations_examined")->second > 0,
			"Stable Permutation Sort counts tested candidate permutations");

		std::vector<int> values;
		for (int i = 0; i < 73; ++i) values.push_back((i * 13 + i / 6) % 12);
		std::mt19937 random(0x7AB1E5EDu);
		std::shuffle(values.begin(), values.end(), random);
		const std::vector<SortItem> tableInput = integers(values);
		for (int d = 0; d < 2; ++d)
			verifyAlgorithmCase("Table Sort stable index-table permutation",
				tableInput, InputProfile::IntegerKeys, SortAlgorithm::ArrayvTable,
				d == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);
		TraceBuildRequest table;
		table.algorithm = SortAlgorithm::ArrayvTable;
		table.input = tableInput;
		table.profile = InputProfile::IntegerKeys;
		table.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace tableTrace = SortTraceBuilder::build(table);
		const SortMetrics& tableMetrics = tableTrace.snapshots.rbegin()->second.metrics;
		check(tableMetrics.algorithm.find("partitions") != tableMetrics.algorithm.end() &&
			tableMetrics.algorithm.find("permutation_cycles") != tableMetrics.algorithm.end() &&
			tableMetrics.algorithm.find("permutation_cycle_moves") != tableMetrics.algorithm.end() &&
			tableMetrics.algorithm.find("table_index_swaps") != tableMetrics.algorithm.end(),
			"Table Sort records indirect partition and cycle application metrics");
		for (std::size_t algorithmIndex = 0; algorithmIndex < 2; ++algorithmIndex)
		{
			TraceBuildRequest limited = algorithmIndex == 0 ? factorial : table;
			limited.maxOperations = 1;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState state = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
			check(!partial.complete && sameState(state, partial.initialState),
				algorithmIndex == 0 ? "factorial partial trace rewinds the index workspace" : "table-sort partial trace rewinds the index workspace");
		}
	}

	void testHybridCombAndIntroCircleVariants()
	{
		using namespace sortinglogic;
		std::vector<int> values;
		for (int i = 0; i < 73; ++i) values.push_back((i * 31 + i / 5) % 13);
		std::mt19937 random(0xC1AC1E11u);
		std::shuffle(values.begin(), values.end(), random);
		const std::vector<SortItem> duplicateInput = integers(values);
		for (int d = 0; d < 2; ++d)
			verifyAlgorithmCase("Hybrid Comb Sort duplicate-heavy fallback",
				duplicateInput, InputProfile::IntegerKeys, SortAlgorithm::ArrayvHybridComb,
				d == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);
		TraceBuildRequest comb;
		comb.algorithm = SortAlgorithm::ArrayvHybridComb;
		comb.input = duplicateInput;
		comb.profile = InputProfile::IntegerKeys;
		comb.domain = InputDomain::NonnegativeIntegerKeys;
		const SortTrace combTrace = SortTraceBuilder::build(comb);
		const SortMetrics& combMetrics = combTrace.snapshots.rbegin()->second.metrics;
		check(combMetrics.algorithm.find("gap_passes") != combMetrics.algorithm.end() &&
			combMetrics.algorithm.find("insertion_fallbacks") != combMetrics.algorithm.end() &&
			combMetrics.algorithm.find("insertion_fallbacks")->second == 1,
			"Hybrid Comb records its shrink passes and insertion fallback");

		const SortAlgorithm introAlgorithms[] = {
			SortAlgorithm::ArrayvIntroCircleIterative,
			SortAlgorithm::ArrayvIntroCircleRecursive
		};
		for (std::size_t a = 0; a < sizeof(introAlgorithms) / sizeof(introAlgorithms[0]); ++a)
		{
			for (int d = 0; d < 2; ++d)
				verifyAlgorithmCase(std::string(algorithmDisplayName(introAlgorithms[a])) + " duplicate-heavy fallback",
					duplicateInput, InputProfile::IntegerKeys, introAlgorithms[a],
					d == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NonnegativeIntegerKeys);
			TraceBuildRequest intro;
			intro.algorithm = introAlgorithms[a];
			intro.input = duplicateInput;
			intro.profile = InputProfile::IntegerKeys;
			intro.domain = InputDomain::NonnegativeIntegerKeys;
			const SortTrace introTrace = SortTraceBuilder::build(intro);
			const SortMetrics& introMetrics = introTrace.snapshots.rbegin()->second.metrics;
			check(introMetrics.algorithm.find("circle_passes") != introMetrics.algorithm.end() &&
				introMetrics.algorithm.find("binary_fallbacks") != introMetrics.algorithm.end() &&
				introMetrics.algorithm.find("binary_fallbacks")->second > 0,
				std::string(algorithmDisplayName(introAlgorithms[a])) + " records circle passes and the introspective fallback");
		}
		for (std::size_t algorithmIndex = 0; algorithmIndex < 3; ++algorithmIndex)
		{
			TraceBuildRequest limited = comb;
			limited.algorithm = algorithmIndex == 0 ? SortAlgorithm::ArrayvHybridComb : introAlgorithms[algorithmIndex - 1];
			limited.maxOperations = 1;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState state = partial.initialState;
			for (std::size_t i = 0; i < partial.events.size(); ++i) applyEventForward(state, partial.events[i]);
			for (std::size_t i = partial.events.size(); i > 0; --i) applyEventBackward(state, partial.events[i - 1]);
			check(!partial.complete && sameState(state, partial.initialState), "hybrid comb / intro circle limited trace rewinds exactly");
		}
	}

	void testDoubleInsertionVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvDoubleInsertion, SortAlgorithm::ArrayvBinaryDoubleInsertion };
		std::vector<std::vector<int> > cases = {
			std::vector<int>(), std::vector<int>{ 7 }, std::vector<int>{ 5, 5 },
			std::vector<int>{ 1, 2, 3, 4, 5, 6 }, std::vector<int>{ 6, 5, 4, 3, 2, 1 },
			std::vector<int>{ 4, 1, 4, 2, 4, 1, 2, 4, 3 }, std::vector<int>{ 9, 0, 8, 1, 7, 2, 6, 3, 5, 4, 4 }
		};
		std::mt19937 random(0xD0B1E1A5u);
		std::vector<int> randomized;
		for (int i = 0; i < 73; ++i) randomized.push_back(static_cast<int>(random() % 17));
		cases.push_back(randomized);
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
				for (std::size_t inputIndex = 0; inputIndex < cases.size(); ++inputIndex)
				{
					std::vector<float> inputValues;
					for (std::size_t item = 0; item < cases[inputIndex].size(); ++item)
						inputValues.push_back(static_cast<float>(cases[inputIndex][item]));
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " source-shaped input " + std::to_string(inputIndex),
						floats(inputValues), InputProfile::NormalizedFloat, algorithms[algorithm],
						directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NormalizedFloats);
				}

		std::vector<int> ascending;
		for (int i = 0; i < 128; ++i) ascending.push_back(i);
		TraceBuildRequest linear;
		linear.algorithm = SortAlgorithm::ArrayvDoubleInsertion;
		std::vector<float> ascendingValues;
		for (std::size_t i = 0; i < ascending.size(); ++i) ascendingValues.push_back(static_cast<float>(ascending[i]));
		linear.input = floats(ascendingValues);
		linear.profile = InputProfile::NormalizedFloat;
		linear.domain = InputDomain::NormalizedFloats;
		TraceBuildRequest binary = linear;
		binary.algorithm = SortAlgorithm::ArrayvBinaryDoubleInsertion;
		const SortTrace linearTrace = SortTraceBuilder::build(linear);
		const SortTrace binaryTrace = SortTraceBuilder::build(binary);
		check(binaryTrace.snapshots.rbegin()->second.metrics.comparisons < linearTrace.snapshots.rbegin()->second.metrics.comparisons,
			"Binary Double Insertion uses fewer search comparisons than its linear-search companion on ordered data");

		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			TraceBuildRequest limited = linear;
			limited.algorithm = algorithms[algorithm];
			limited.maxOperations = 3;
			const SortTrace partial = SortTraceBuilder::build(limited);
			SortState replay = partial.initialState;
			for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(replay, partial.events[event]);
			for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(replay, partial.events[event - 1]);
			check(!partial.complete && sameState(replay, partial.initialState),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " partial trace rewinds a held key and gap exactly");
		}
	}

	void testIntroSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.4f });
		cases.push_back(std::vector<float>{ 0.8f, 0.2f });
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f });
		cases.push_back(std::vector<float>{ 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		cases.push_back(std::vector<float>{ 0.5f, 0.1f, 0.5f, 0.2f, 0.5f, 0.3f, 0.5f, 0.4f, 0.5f, 0.6f, 0.1f, 0.7f, 0.2f, 0.8f, 0.3f, 0.9f, 0.4f });
		std::mt19937 random(0x1A7A0502u);
		std::vector<float> randomized;
		for (int i = 0; i < 127; ++i) randomized.push_back(static_cast<float>(random() % 29) / 29.0f);
		cases.push_back(randomized);
		std::vector<float> ascending;
		for (int i = 0; i < 127; ++i) ascending.push_back(static_cast<float>(i) / 127.0f);
		cases.push_back(ascending);
		std::reverse(ascending.begin(), ascending.end());
		cases.push_back(ascending);
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t input = 0; input < cases.size(); ++input)
				verifyAlgorithmCase("IntroSort input " + std::to_string(input), floats(cases[input]), InputProfile::NormalizedFloat,
					SortAlgorithm::ArrayvIntroSort, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NormalizedFloats);

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvIntroSort;
		request.input = floats(randomized);
		request.profile = InputProfile::NormalizedFloat;
		request.domain = InputDomain::NormalizedFloats;
		const SortTrace trace = SortTraceBuilder::build(request);
		const std::map<std::string, std::uint64_t>& metrics = trace.snapshots.rbegin()->second.metrics.algorithm;
		check(trace.complete && metrics.find("partitions") != metrics.end() && metrics.find("partitions")->second > 0 &&
			metrics.find("maximum_depth") != metrics.end() && metrics.find("insertion_passes") != metrics.end() &&
			metrics.find("heap_fallbacks") != metrics.end(),
			"IntroSort exposes its partition depth, heap fallback, and final insertion-pass measures");

		request.maxOperations = 5;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState state = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(state, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(state, partial.events[event - 1]);
		check(!partial.complete && sameState(state, partial.initialState), "IntroSort partial trace rewinds exactly");
	}

	void testPatienceSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.25f });
		cases.push_back(std::vector<float>{ 0.4f, 0.4f });
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f });
		cases.push_back(std::vector<float>{ 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		cases.push_back(std::vector<float>{ 0.3f, 0.1f, 0.3f, 0.2f, 0.1f, 0.4f, 0.2f, 0.3f, 0.1f, 0.4f });
		std::mt19937 random(0xFA71EACEu);
		std::vector<float> randomized;
		for (int i = 0; i < 79; ++i) randomized.push_back(static_cast<float>(random() % 13) / 13.0f);
		cases.push_back(randomized);
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t input = 0; input < cases.size(); ++input)
				verifyAlgorithmCase("Patience Sort input " + std::to_string(input), floats(cases[input]), InputProfile::NormalizedFloat,
					SortAlgorithm::ArrayvPatience, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NormalizedFloats);

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvPatience;
		request.input = floats(randomized);
		request.profile = InputProfile::NormalizedFloat;
		request.domain = InputDomain::NormalizedFloats;
		const SortTrace trace = SortTraceBuilder::build(request);
		const SortState& finalState = trace.snapshots.rbegin()->second;
		const std::map<std::string, std::uint64_t>& metrics = finalState.metrics.algorithm;
		check(trace.complete && metrics.find("pile_count") != metrics.end() && metrics.find("pile_count")->second > 0 &&
			metrics.find("heap_pushes") != metrics.end() && metrics.find("heap_pushes")->second == metrics.find("pile_count")->second &&
			metrics.find("heap_pops") != metrics.end() && metrics.find("heap_pops")->second == randomized.size() &&
			finalState.metrics.peakAuxiliaryCapacity >= randomized.size(),
			"Patience Sort records pile and priority-queue work in reversible workspaces");
		for (std::map<std::string, std::vector<SortSlot> >::const_iterator lane = finalState.auxiliaryItems.begin(); lane != finalState.auxiliaryItems.end(); ++lane)
			check(lane->second.empty(), "Patience Sort drains every auxiliary pile by completion");

		request.maxOperations = 7;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState replay = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(replay, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(replay, partial.events[event - 1]);
		check(!partial.complete && sameState(replay, partial.initialState), "Patience Sort partial pile and heap trace rewinds exactly");
	}

	void testParallelShellSort()
	{
		using namespace sortinglogic;
		std::vector<std::vector<float> > cases;
		cases.push_back(std::vector<float>());
		cases.push_back(std::vector<float>{ 0.5f });
		cases.push_back(std::vector<float>{ 0.9f, 0.1f });
		cases.push_back(std::vector<float>{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f });
		cases.push_back(std::vector<float>{ 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f });
		cases.push_back(std::vector<float>{ 0.7f, 0.1f, 0.7f, 0.2f, 0.5f, 0.3f, 0.1f, 0.8f, 0.4f, 0.2f, 0.9f, 0.3f, 0.6f });
		std::mt19937 random(0x5E1150A7u);
		std::vector<float> randomized;
		for (int i = 0; i < 127; ++i) randomized.push_back(static_cast<float>(random() % 19) / 19.0f);
		cases.push_back(randomized);
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t input = 0; input < cases.size(); ++input)
				verifyAlgorithmCase("Parallel Shell Sort input " + std::to_string(input), floats(cases[input]), InputProfile::NormalizedFloat,
					SortAlgorithm::ArrayvShellParallel, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					InputDomain::NormalizedFloats);

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvShellParallel;
		request.input = floats(randomized);
		request.profile = InputProfile::NormalizedFloat;
		request.domain = InputDomain::NormalizedFloats;
		const SortTrace trace = SortTraceBuilder::build(request);
		std::set<int> openPhases;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			if (trace.events[i].type == SemanticEventType::PhaseBegin)
				check(openPhases.insert(trace.events[i].groupId).second, "Parallel Shell starts each gap phase exactly once");
			else if (trace.events[i].type == SemanticEventType::PhaseEnd)
				check(openPhases.erase(trace.events[i].groupId) == 1, "Parallel Shell closes its matching gap phase");
		}
		const std::map<std::string, std::uint64_t>& metrics = trace.snapshots.rbegin()->second.metrics.algorithm;
		check(trace.complete && openPhases.empty() && metrics.find("gap_passes") != metrics.end() &&
			metrics.find("gap_passes")->second == 5 && metrics.find("parallel_tasks") != metrics.end() &&
			metrics.find("parallel_tasks")->second > 0,
			"Parallel Shell records deterministic extended-Ciura phase and residue-task metrics");

		request.maxOperations = 5;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState state = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(state, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(state, partial.events[event - 1]);
		check(!partial.complete && sameState(state, partial.initialState), "Parallel Shell partial phase rewinds exactly");
	}

	void testMergeInsertionSort()
	{
		using namespace sortinglogic;
		const std::size_t lengths[] = { 0, 1, 2, 3, 5, 8, 13, 16, 17, 31, 32, 33, 63, 64, 65, 127 };
		std::mt19937 random(0xF04D1023u);
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t lengthIndex = 0; lengthIndex < sizeof(lengths) / sizeof(lengths[0]); ++lengthIndex)
			{
				std::vector<float> values;
				for (std::size_t i = 0; i < lengths[lengthIndex]; ++i)
					values.push_back(static_cast<float>(random() % 11) / 11.0f);
				verifyAlgorithmCase("Merge-Insertion Sort length " + std::to_string(lengths[lengthIndex]), floats(values),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvMergeInsertion,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
			}

		std::vector<float> ordered;
		for (int i = 0; i < 127; ++i) ordered.push_back(static_cast<float>(i) / 127.0f);
		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvMergeInsertion;
		request.input = floats(ordered);
		request.profile = InputProfile::NormalizedFloat;
		request.domain = InputDomain::NormalizedFloats;
		const SortTrace trace = SortTraceBuilder::build(request);
		const std::map<std::string, std::uint64_t>& metrics = trace.snapshots.rbegin()->second.metrics.algorithm;
		check(trace.complete && metrics.find("block_levels") != metrics.end() && metrics.find("block_levels")->second > 0 &&
			metrics.find("block_searches") != metrics.end() && metrics.find("block_searches")->second > 0 &&
			metrics.find("block_swaps") != metrics.end(),
			"Merge-Insertion Sort exposes its block-level search and movement measures");

		request.maxOperations = 7;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState replay = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(replay, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(replay, partial.events[event - 1]);
		check(!partial.complete && sameState(replay, partial.initialState), "Merge-Insertion partial block trace rewinds exactly");
	}

	void testStalinVariants()
	{
		using namespace sortinglogic;
		const std::vector<std::vector<float> > cases = {
			{}, { 0.4f }, { 0.1f, 0.2f, 0.3f, 0.4f }, { 0.9f, 0.7f, 0.5f, 0.3f, 0.1f },
			{ 0.3f, 0.1f, 0.4f, 0.2f, 0.5f }, { 0.5f, 0.2f, 0.8f, 0.2f, 0.7f, 0.1f },
			{ 0.6f, 0.6f, 0.6f, 0.6f }, { 0.8f, 0.2f, 0.8f, 0.1f, 0.9f, 0.3f }
		};
		const SortAlgorithm variants[] = { SortAlgorithm::ArrayvStalinOverwrite, SortAlgorithm::CanonicalStalinFilter };
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t algorithm = 0; algorithm < sizeof(variants) / sizeof(variants[0]); ++algorithm)
				for (std::size_t input = 0; input < cases.size(); ++input)
					verifyAlgorithmCase(std::string(algorithmDisplayName(variants[algorithm])) + " input " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, variants[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		const std::vector<SortItem> mixed = floats({ 0.7f, 0.2f, 0.9f, 0.1f, 0.8f });
		TraceBuildRequest overwriteRequest;
		overwriteRequest.algorithm = SortAlgorithm::ArrayvStalinOverwrite;
		overwriteRequest.input = mixed;
		const SortTrace overwrite = SortTraceBuilder::build(overwriteRequest);
		SortState overwriteEnd = overwrite.initialState;
		for (std::size_t event = 0; event < overwrite.events.size(); ++event)
			applyEventForward(overwriteEnd, overwrite.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator rewrites = overwriteEnd.metrics.algorithm.find("value_rewrites");
		check(overwrite.complete && overwrite.outcome == AlgorithmOutcome::SortedValueRewrite &&
			rewrites != overwriteEnd.metrics.algorithm.end() && rewrites->second == 3,
			"pinned Stalin records its overwrite outcome and exact rewrite count");
		bool identityPreserved = overwriteEnd.slots.size() == mixed.size();
		for (std::size_t i = 0; i < mixed.size() && identityPreserved; ++i)
			identityPreserved = overwriteEnd.slots[i].item.stableId == mixed[i].stableId;
		check(identityPreserved && overwriteEnd.slots[1].item.value == mixed[0].value &&
			overwriteEnd.slots[3].item.value == mixed[2].value,
			"pinned Stalin overwrites scalar keys while retaining each position's stable identity");

		TraceBuildRequest filterRequest = overwriteRequest;
		filterRequest.algorithm = SortAlgorithm::CanonicalStalinFilter;
		const SortTrace filter = SortTraceBuilder::build(filterRequest);
		SortState filterEnd = filter.initialState;
		for (std::size_t event = 0; event < filter.events.size(); ++event)
			applyEventForward(filterEnd, filter.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator discards = filterEnd.metrics.algorithm.find("discarded_items");
		const std::map<std::string, std::uint64_t>::const_iterator kept = filterEnd.metrics.algorithm.find("kept_items");
		check(filter.complete && filter.outcome == AlgorithmOutcome::SortedWithDiscards &&
			discards != filterEnd.metrics.algorithm.end() && discards->second == 3 &&
			kept != filterEnd.metrics.algorithm.end() && kept->second == filterEnd.slots.size() &&
			filterEnd.discardedItems.size() == 3 && filterEnd.slots.size() + filterEnd.discardedItems.size() == mixed.size(),
			"canonical Stalin reports retained and discarded items without losing provenance");

		TraceBuildRequest partialRequest = filterRequest;
		partialRequest.maxOperations = 2;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"canonical Stalin partial traces rewind across discard and compaction mutations");
	}

	void testUnbalancedTreeVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvClassicTree, SortAlgorithm::ArrayvTree };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }, { 0.5f, 0.4f, 0.3f, 0.2f, 0.1f },
			{ 0.3f, 0.1f, 0.4f, 0.2f, 0.5f }, { 0.5f, 0.2f, 0.5f, 0.1f, 0.2f, 0.5f, 0.1f },
			{ 0.7f, 0.7f, 0.7f, 0.7f }
		};
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
				for (std::size_t input = 0; input < cases.size(); ++input)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " input " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		std::vector<float> orderedValues;
		for (int i = 0; i < 48; ++i) orderedValues.push_back(static_cast<float>(i) / 48.0f);
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			TraceBuildRequest request;
			request.algorithm = algorithms[algorithm];
			request.input = floats(orderedValues);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState finalState = trace.initialState;
			for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(finalState, trace.events[event]);
			const std::map<std::string, std::uint64_t>::const_iterator height = finalState.metrics.algorithm.find("tree_height");
			check(trace.complete && height != finalState.metrics.algorithm.end() && height->second == orderedValues.size(),
				std::string(algorithmDisplayName(algorithms[algorithm])) + " exposes the degenerate ordered-input tree height");
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvTree;
		partialRequest.input = floats({ 0.6f, 0.2f, 0.9f, 0.1f, 0.8f, 0.3f });
		partialRequest.maxOperations = 3;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"tree-node copy, child links, and partial traversal mutations rewind exactly");
	}

	void testStrandSort()
	{
		using namespace sortinglogic;
		const std::vector<std::vector<float> > cases = {
			{}, { 0.4f }, { 0.1f, 0.2f, 0.3f, 0.4f }, { 0.9f, 0.7f, 0.5f, 0.3f, 0.1f },
			{ 0.3f, 0.1f, 0.4f, 0.2f, 0.5f }, { 0.5f, 0.2f, 0.8f, 0.2f, 0.7f, 0.1f },
			{ 0.6f, 0.6f, 0.6f, 0.6f }
		};
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t input = 0; input < cases.size(); ++input)
				verifyAlgorithmCase("Strand Sort input " + std::to_string(input), floats(cases[input]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvStrand,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvStrand;
		request.input = floats({ 0.8f, 0.2f, 0.6f, 0.3f, 0.9f, 0.1f, 0.7f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(finalState, trace.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator strands = finalState.metrics.algorithm.find("strands");
		const std::map<std::string, std::uint64_t>::const_iterator merges = finalState.metrics.algorithm.find("merge_comparisons");
		check(trace.complete && strands != finalState.metrics.algorithm.end() && strands->second > 1 &&
			merges != finalState.metrics.algorithm.end() && merges->second > 0 &&
			finalState.auxiliaryItems.find("strand_sorted") != finalState.auxiliaryItems.end(),
			"Strand Sort exposes repeated subsequence extraction and auxiliary merge work");

		request.maxOperations = 4;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Strand Sort partial auxiliary extraction rewinds exactly");
	}

	void testBalancedTreeSorts()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::ArrayvAATree, SortAlgorithm::ArrayvAVLTree, SortAlgorithm::ArrayvRedBlackTree
		};
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }, { 0.5f, 0.4f, 0.3f, 0.2f, 0.1f },
			{ 0.3f, 0.1f, 0.4f, 0.2f, 0.5f }, { 0.5f, 0.2f, 0.5f, 0.1f, 0.2f, 0.5f, 0.1f },
			{ 0.7f, 0.7f, 0.7f, 0.7f }
		};
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
				for (std::size_t input = 0; input < cases.size(); ++input)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " input " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);

		std::vector<float> ordered;
		for (int i = 0; i < 64; ++i) ordered.push_back(static_cast<float>(i) / 64.0f);
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
		{
			TraceBuildRequest request;
			request.algorithm = algorithms[algorithm];
			request.input = floats(ordered);
			const SortTrace trace = SortTraceBuilder::build(request);
			SortState finalState = trace.initialState;
			for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(finalState, trace.events[event]);
			const std::map<std::string, std::uint64_t>::const_iterator height = finalState.metrics.algorithm.find("tree_height");
			const std::map<std::string, std::uint64_t>::const_iterator rotations = finalState.metrics.algorithm.find("tree_rotations");
			check(trace.complete && height != finalState.metrics.algorithm.end() && height->second <= 14 &&
				rotations != finalState.metrics.algorithm.end() && rotations->second > 0,
				std::string(algorithmDisplayName(algorithms[algorithm])) + " stays logarithmically shallow and rotates sorted input");
			if (algorithms[algorithm] == SortAlgorithm::ArrayvRedBlackTree)
			{
				const std::map<std::string, std::uint64_t>::const_iterator recolors = finalState.metrics.algorithm.find("tree_recolors");
				const std::vector<int>& rootWorkspace = finalState.integerWorkspaces.at("tree_root");
				const std::vector<int>& colorWorkspace = finalState.integerWorkspaces.at("tree_color");
				const std::size_t rootIndex = static_cast<std::size_t>(rootWorkspace[0] - 1);
				check(recolors != finalState.metrics.algorithm.end() && recolors->second > 0,
					"Red-Black Tree exposes its color-split and rotation recoloring work");
				check(rootIndex < colorWorkspace.size() && colorWorkspace[rootIndex] == 0,
					"Red-Black Tree leaves a black root after every insertion sequence");
			}
		}

		TraceBuildRequest partialRequest;
		partialRequest.algorithm = SortAlgorithm::ArrayvRedBlackTree;
		partialRequest.input = floats({ 0.8f, 0.2f, 0.6f, 0.1f, 0.7f, 0.3f, 0.5f });
		partialRequest.maxOperations = 8;
		const SortTrace partial = SortTraceBuilder::build(partialRequest);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState),
			"Red-Black Tree partial node, color, link, and rotation mutations rewind exactly");
	}

	void testSplayTreeSort()
	{
		using namespace sortinglogic;
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }, { 0.5f, 0.4f, 0.3f, 0.2f, 0.1f },
			{ 0.3f, 0.1f, 0.4f, 0.2f, 0.5f }, { 0.5f, 0.2f, 0.5f, 0.1f, 0.2f, 0.5f, 0.1f },
			{ 0.7f, 0.7f, 0.7f, 0.7f }
		};
		for (int direction = 0; direction < 2; ++direction)
			for (std::size_t input = 0; input < cases.size(); ++input)
			{
				verifyAlgorithmCase("Splay Tree Sort input " + std::to_string(input), floats(cases[input]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvSplayTree,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending, InputDomain::NormalizedFloats);
			}

		TraceBuildRequest request;
		request.algorithm = SortAlgorithm::ArrayvSplayTree;
		request.input = floats({ 0.8f, 0.2f, 0.6f, 0.1f, 0.7f, 0.3f, 0.5f });
		const SortTrace trace = SortTraceBuilder::build(request);
		SortState finalState = trace.initialState;
		for (std::size_t event = 0; event < trace.events.size(); ++event) applyEventForward(finalState, trace.events[event]);
		const std::map<std::string, std::uint64_t>::const_iterator rotations = finalState.metrics.algorithm.find("tree_rotations");
		const std::map<std::string, std::uint64_t>::const_iterator height = finalState.metrics.algorithm.find("tree_height");
		check(trace.complete && rotations != finalState.metrics.algorithm.end() && rotations->second > 0 &&
			height != finalState.metrics.algorithm.end() && height->second > 0,
			"Splay Tree records rotations and the resulting tree height");

		request.maxOperations = 8;
		const SortTrace partial = SortTraceBuilder::build(request);
		SortState partialState = partial.initialState;
		for (std::size_t event = 0; event < partial.events.size(); ++event) applyEventForward(partialState, partial.events[event]);
		for (std::size_t event = partial.events.size(); event > 0; --event) applyEventBackward(partialState, partial.events[event - 1]);
		check(!partial.complete && sameState(partialState, partial.initialState), "Splay Tree partial rotations rewind exactly");
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
		check(!simulation.isBuildingTrace() && simulation.snapshot().hasTrace, "worker publishes successful trace to simulation");
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
				check(sameState(simulation.snapshot().state, states[cursor]), "simulation seek reproduces workspace cursor " + std::to_string(cursor));
			}
			check(simulation.reset().accepted && sameState(simulation.snapshot().state, trace->initialState), "Reset restores exact prepared input");
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
}

int main(int argc, char* argv[])
{
	const char* stage = "verifyProfilesAndAlgorithms";
	try
	{
		stage = "testPinnedCatalogCoverage";
		testPinnedCatalogCoverage();
		if (argc > 1 && std::string(argv[1]) == "--catalog-coverage-only")
		{
			std::cout << "Pinned ArrayV catalog coverage: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--core-contracts-only")
		{
			using namespace sortinglogic;
			stage = "Insertion strategy extraction";
			const std::vector<SortItem> insertionInput = floats({ 0.7f, 0.2f, 0.7f, 0.1f, 0.2f });
			verifyAlgorithmCase("Extracted Insertion strategy ascending", insertionInput, InputProfile::NormalizedFloat,
				SortAlgorithm::Insertion, SortDirection::Ascending);
			verifyAlgorithmCase("Extracted Insertion strategy descending", insertionInput, InputProfile::NormalizedFloat,
				SortAlgorithm::Insertion, SortDirection::Descending);
			verifyAlgorithmCase("Intro Sort small-range Insertion fallback", insertionInput, InputProfile::NormalizedFloat,
				SortAlgorithm::ArrayvIntroSort, SortDirection::Ascending);
			stage = "testDistributionAudioBaselines";
			testDistributionAudioBaselines();
			stage = "testBurntPancakeOrientation";
			testBurntPancakeOrientation();
			stage = "testSortingNetworkSchedules";
			testSortingNetworkSchedules();
			stage = "testMergeWritebackModes";
			testMergeWritebackModes();
			stage = "testSimulationAndProfiles";
			testSimulationAndProfiles();
			stage = "testSnapshotAnnotations";
			testSnapshotAnnotations();
			stage = "testTraceServiceAndPlayback";
			testTraceServiceAndPlayback();
			stage = "testStressAndValidation";
			testStressAndValidation();
			stage = "testPartialTraceBudget";
			testPartialTraceBudget();
			stage = "testDomainsAndPatterns";
			testDomainsAndPatterns();
			stage = "testSemanticProbeDiscardAndPhases";
			testSemanticProbeDiscardAndPhases();
			std::cout << "SortingLogic core contracts: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--bogo-only")
		{
			stage = "testAdditionalBogoVariants";
			testAdditionalBogoVariants();
			stage = "testNestedBogoVariants";
			testNestedBogoVariants();
			std::cout << "Focused stochastic novelty tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--dual-pivot-only")
		{
			stage = "testDualPivotQuickVariants";
			testDualPivotQuickVariants();
			std::cout << "Focused dual-pivot tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stable-quick-only")
		{
			stage = "testStableQuickVariants";
			testStableQuickVariants();
			std::cout << "Focused stable Quick tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--merge-variants-only")
		{
			stage = "testMergeFamilyVariants";
			testMergeFamilyVariants();
			std::cout << "Focused merge-family tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--iterative-top-down-merge-only")
		{
			stage = "testIterativeTopDownMergeSort";
			testIterativeTopDownMergeSort();
			std::cout << "Focused Iterative Top-down Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--in-place-merge-only")
		{
			stage = "testInPlaceMergeSort";
			testInPlaceMergeSort();
			std::cout << "Focused In-Place Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--parallel-merge-only")
		{
			stage = "testParallelMergeSort";
			testParallelMergeSort();
			std::cout << "Focused Parallel Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--rotate-merge-only")
		{
			stage = "testRotateMergeSort";
			testRotateMergeSort();
			std::cout << "Focused Rotate Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--parallel-rotate-merge-only")
		{
			stage = "testParallelRotateMergeSort";
			testParallelRotateMergeSort();
			std::cout << "Focused Parallel Rotate Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stackless-rotate-merge-only")
		{
			stage = "testStacklessRotateMergeSort";
			testStacklessRotateMergeSort();
			std::cout << "Focused Stackless Rotate Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--block-swap-merge-only")
		{
			stage = "testBlockSwapMergeSort";
			testBlockSwapMergeSort();
			std::cout << "Focused Block-Swap Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--weaved-merge-only")
		{
			stage = "testWeavedMergeSort";
			testWeavedMergeSort();
			std::cout << "Focused Weaved Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--twin-sort-only")
		{
			stage = "testTwinSort";
			testTwinSort();
			std::cout << "Focused Twin Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--tim-merge-only")
		{
			stage = "testTimMergeVariants";
			testTimMergeVariants();
			std::cout << "Focused Tim merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--improved-inplace-merge-only")
		{
			stage = "testImprovedInPlaceMergeSort";
			testImprovedInPlaceMergeSort();
			std::cout << "Focused Improved In-Place Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--block-insertion-only")
		{
			stage = "testBlockInsertionSort";
			testBlockInsertionSort();
			std::cout << "Focused Block Insertion tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--buffered-stooge-only")
		{
			stage = "testBufferedStoogeSort";
			testBufferedStoogeSort();
			std::cout << "Focused Buffered Stooge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--bad-sort-only")
		{
			stage = "testBadSort";
			testBadSort();
			std::cout << "Focused Bad Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--asynchronous-sort-only")
		{
			stage = "testAsynchronousSort";
			testAsynchronousSort();
			std::cout << "Focused Asynchronous Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--pd-merge-only")
		{
			stage = "testPDMergeSort";
			testPDMergeSort();
			std::cout << "Focused Pattern-Defeating Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stable-cycle-only")
		{
			stage = "testStableCycleSort";
			testStableCycleSort();
			std::cout << "Focused Stable Cycle tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--tournament-only")
		{
			stage = "testTournamentSorts";
			testTournamentSorts();
			std::cout << "Focused tournament tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--smooth-sort-only")
		{
			stage = "testSmoothSortVariants";
			testSmoothSortVariants();
			std::cout << "Focused Smooth Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--block-selection-only")
		{
			stage = "testImprovedBlockSelectionSort";
			testImprovedBlockSelectionSort();
			std::cout << "Focused Improved Block Selection tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stackless-hybrid-only")
		{
			stage = "testStacklessHybridQuickSort";
			testStacklessHybridQuickSort();
			std::cout << "Focused Stackless Hybrid Quick tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stackless-dual-pivot-only")
		{
			stage = "testStacklessDualPivotQuickSort";
			testStacklessDualPivotQuickSort();
			std::cout << "Focused Stackless Dual-Pivot Quick tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--hanoi-only")
		{
			stage = "testHanoiSort";
			testHanoiSort();
			std::cout << "Focused Hanoi Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--simplified-library-only")
		{
			stage = "testSimplifiedLibrarySort";
			testSimplifiedLibrarySort();
			std::cout << "Focused Simplified Library tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--library-only")
		{
			stage = "testLibrarySort";
			testLibrarySort();
			std::cout << "Focused Library Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--weave-merge-only")
		{
			stage = "testWeaveMergeSort";
			testWeaveMergeSort();
			std::cout << "Focused Weave Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--laziest-only")
		{
			stage = "testLaziestStableSort";
			testLaziestStableSort();
			std::cout << "Focused Laziest Stable tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--lazierest-only")
		{
			stage = "testLazierestStableSort";
			testLazierestStableSort();
			std::cout << "Focused Lazierest Stable tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--lazy-grail-only")
		{
			stage = "testLazyStableMergeVariants";
			testLazyStableMergeVariants();
			std::cout << "Focused Lazy Stable merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--multiway-merge-only")
		{
			stage = "testFifthAndQuadMergeSorts";
			testFifthAndQuadMergeSorts();
			std::cout << "Focused Fifth/Quad merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--buffer-partition-merge-only")
		{
			stage = "testBufferPartitionMergeSort";
			testBufferPartitionMergeSort();
			std::cout << "Focused Buffer Partition Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--parallel-block-merge-only")
		{
			stage = "testParallelBlockMergeSort";
			testParallelBlockMergeSort();
			std::cout << "Focused Parallel Block Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--optimized-rotate-merge-only")
		{
			stage = "testOptimizedRotateMergeSort";
			testOptimizedRotateMergeSort();
			std::cout << "Focused Optimized Rotate Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--grail-only")
		{
			stage = "testGrailSortModes";
			testGrailSortModes();
			stage = "testAdaptiveGrailSort";
			testAdaptiveGrailSort();
			std::cout << "Focused Grail family tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--pdq-only")
		{
			stage = "testPDQVariants";
			testPDQVariants();
			std::cout << "Focused PDQ tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--sqrt-only")
		{
			stage = "testSqrtMergeVariants";
			testSqrtMergeVariants();
			std::cout << "Focused Sqrt Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--ecta-only")
		{
			stage = "testEctaSort";
			testEctaSort();
			std::cout << "Focused Ecta tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--grail-variants-only")
		{
			stage = "testCircularAndUnstableGrailSorts";
			testCircularAndUnstableGrailSorts();
			std::cout << "Focused Circular/Unstable Grail tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--drop-merge-only")
		{
			stage = "testDropMergeSort";
			testDropMergeSort();
			std::cout << "Focused Drop Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--parallel-grail-only")
		{
			stage = "testParallelGrailSort";
			testParallelGrailSort();
			std::cout << "Focused Parallel Grail tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--median-merge-only")
		{
			stage = "testMedianMergeSort";
			testMedianMergeSort();
			std::cout << "Focused Median Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--remi-only")
		{
			stage = "testRemiSort";
			testRemiSort();
			std::cout << "Focused Remi Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--merge-roster-tail-only")
		{
			stage = "testRemainingMergeRosterBatch";
			testRemainingMergeRosterBatch();
			std::cout << "Focused merge roster tail tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--new-shuffle-only")
		{
			stage = "testNewShuffleMerge";
			testRemainingMergeRosterBatch(8);
			std::cout << "Focused New Shuffle Merge tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--ternary-quick-only")
		{
			stage = "testTernaryQuickVariants";
			testTernaryQuickVariants();
			std::cout << "Focused ternary Quick tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--indirect-only")
		{
			stage = "testIndirectAndPermutationVariants";
			testIndirectAndPermutationVariants();
			std::cout << "Focused indirect and permutation tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--hybrid-exchange-only")
		{
			stage = "testHybridCombAndIntroCircleVariants";
			testHybridCombAndIntroCircleVariants();
			std::cout << "Focused hybrid exchange tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--double-insertion-only")
		{
			stage = "testDoubleInsertionVariants";
			testDoubleInsertionVariants();
			std::cout << "Focused double insertion tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--intro-sort-only")
		{
			stage = "testIntroSort";
			testIntroSort();
			std::cout << "Focused IntroSort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--patience-only")
		{
			stage = "testPatienceSort";
			testPatienceSort();
			std::cout << "Focused Patience Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--parallel-shell-only")
		{
			stage = "testParallelShellSort";
			testParallelShellSort();
			std::cout << "Focused parallel Shell tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--merge-insertion-only")
		{
			stage = "testMergeInsertionSort";
			testMergeInsertionSort();
			std::cout << "Focused Merge-Insertion tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--stalin-only")
		{
			stage = "testStalinVariants";
			testStalinVariants();
			std::cout << "Focused Stalin Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--tree-only")
		{
			stage = "testUnbalancedTreeVariants";
			testUnbalancedTreeVariants();
			std::cout << "Focused unbalanced tree tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--strand-only")
		{
			stage = "testStrandSort";
			testStrandSort();
			std::cout << "Focused Strand Sort tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--balanced-tree-only")
		{
			stage = "testBalancedTreeSorts";
			testBalancedTreeSorts();
			std::cout << "Focused balanced tree tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (argc > 1 && std::string(argv[1]) == "--splay-only")
		{
			stage = "testSplayTreeSort";
			testSplayTreeSort();
			std::cout << "Focused Splay Tree tests: " << checks << " checks, " << failures << " failures.\n";
			return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		testDistributionAudioBaselines();
		stage = "testStochasticNoveltyVariants";
		testStochasticNoveltyVariants();
		stage = "testAdditionalBogoVariants";
		testAdditionalBogoVariants();
		stage = "testPointerQuicksortVariants";
		testPointerQuicksortVariants();
		stage = "testNestedBogoVariants";
		testNestedBogoVariants();
		stage = "testDualPivotQuickVariants";
		testDualPivotQuickVariants();
		stage = "testStableQuickVariants";
		testStableQuickVariants();
		stage = "testMergeFamilyVariants";
		testMergeFamilyVariants();
		stage = "testIterativeTopDownMergeSort";
		testIterativeTopDownMergeSort();
		stage = "testInPlaceMergeSort";
		testInPlaceMergeSort();
		stage = "testParallelMergeSort";
		testParallelMergeSort();
		stage = "testRotateMergeSort";
		testRotateMergeSort();
		stage = "testParallelRotateMergeSort";
		testParallelRotateMergeSort();
		stage = "testStacklessRotateMergeSort";
		testStacklessRotateMergeSort();
		stage = "testBlockSwapMergeSort";
		testBlockSwapMergeSort();
		stage = "testWeavedMergeSort";
		testWeavedMergeSort();
		stage = "testTwinSort";
		testTwinSort();
		stage = "testTimMergeVariants";
		testTimMergeVariants();
		stage = "testImprovedInPlaceMergeSort";
		testImprovedInPlaceMergeSort();
		stage = "testBlockInsertionSort";
		testBlockInsertionSort();
		stage = "testBufferedStoogeSort";
		testBufferedStoogeSort();
		stage = "testBadSort";
		testBadSort();
		stage = "testAsynchronousSort";
		testAsynchronousSort();
		stage = "testPDMergeSort";
		testPDMergeSort();
		stage = "testStableCycleSort";
		testStableCycleSort();
		stage = "testTournamentSorts";
		testTournamentSorts();
		stage = "testSmoothSortVariants";
		testSmoothSortVariants();
		stage = "testImprovedBlockSelectionSort";
		testImprovedBlockSelectionSort();
		stage = "testStacklessHybridQuickSort";
		testStacklessHybridQuickSort();
		stage = "testStacklessDualPivotQuickSort";
		testStacklessDualPivotQuickSort();
		stage = "testHanoiSort";
		testHanoiSort();
		stage = "testSimplifiedLibrarySort";
		testSimplifiedLibrarySort();
		stage = "testLibrarySort";
		testLibrarySort();
		stage = "testWeaveMergeSort";
		testWeaveMergeSort();
		stage = "testLaziestStableSort";
		testLaziestStableSort();
		stage = "testLazierestStableSort";
		testLazierestStableSort();
		stage = "testLazyStableMergeVariants";
		testLazyStableMergeVariants();
		stage = "testFifthAndQuadMergeSorts";
		testFifthAndQuadMergeSorts();
		stage = "testBufferPartitionMergeSort";
		testBufferPartitionMergeSort();
		stage = "testParallelBlockMergeSort";
		testParallelBlockMergeSort();
		stage = "testOptimizedRotateMergeSort";
		testOptimizedRotateMergeSort();
		stage = "testGrailSortModes";
		testGrailSortModes();
		stage = "testAdaptiveGrailSort";
		testAdaptiveGrailSort();
		stage = "testPDQVariants";
		testPDQVariants();
		stage = "testSqrtMergeVariants";
		testSqrtMergeVariants();
		stage = "testEctaSort";
		testEctaSort();
		stage = "testCircularAndUnstableGrailSorts";
		testCircularAndUnstableGrailSorts();
		stage = "testDropMergeSort";
		testDropMergeSort();
		stage = "testParallelGrailSort";
		testParallelGrailSort();
		stage = "testMedianMergeSort";
		testMedianMergeSort();
		stage = "testRemiSort";
		testRemiSort();
		stage = "testRemainingMergeRosterBatch";
		testRemainingMergeRosterBatch();
		stage = "testTernaryQuickVariants";
		testTernaryQuickVariants();
		stage = "testIndirectAndPermutationVariants";
		testIndirectAndPermutationVariants();
		stage = "testHybridCombAndIntroCircleVariants";
		testHybridCombAndIntroCircleVariants();
		stage = "testDoubleInsertionVariants";
		testDoubleInsertionVariants();
		stage = "testIntroSort";
		testIntroSort();
		stage = "testPatienceSort";
		testPatienceSort();
		stage = "testParallelShellSort";
		testParallelShellSort();
		stage = "testMergeInsertionSort";
		testMergeInsertionSort();
		stage = "testStalinVariants";
		testStalinVariants();
		stage = "testUnbalancedTreeVariants";
		testUnbalancedTreeVariants();
		stage = "testStrandSort";
		testStrandSort();
		stage = "testBalancedTreeSorts";
		testBalancedTreeSorts();
		stage = "testSplayTreeSort";
		testSplayTreeSort();
		verifyProfilesAndAlgorithms();
		stage = "testBurntPancakeOrientation";
		testBurntPancakeOrientation();
		stage = "testSortingNetworkSchedules";
		testSortingNetworkSchedules();
		stage = "testMatrixSortShapes";
		testMatrixSortShapes();
		stage = "testIncompleteStoogeOutcome";
		testIncompleteStoogeOutcome();
		stage = "testStoogeFamilyVariants";
		testStoogeFamilyVariants();
		stage = "testRecursiveExchangeNoveltyVariants";
		testRecursiveExchangeNoveltyVariants();
		stage = "testFunSortBoundedOutcome";
		testFunSortBoundedOutcome();
		stage = "testMergeWritebackModes";
		testMergeWritebackModes();
		stage = "testSimulationAndProfiles";
		testSimulationAndProfiles();
		stage = "testSnapshotAnnotations";
		testSnapshotAnnotations();
		stage = "testTraceServiceAndPlayback";
		testTraceServiceAndPlayback();
		stage = "testStressAndValidation";
		testStressAndValidation();
		stage = "testPartialTraceBudget";
		testPartialTraceBudget();
		stage = "testDomainsAndPatterns";
		testDomainsAndPatterns();
		stage = "testSemanticProbeDiscardAndPhases";
		testSemanticProbeDiscardAndPhases();
	}
	catch (const std::exception& error)
	{
		std::cerr << "SortingLogicTests aborted during " << stage << " after " << checks
			<< " checks: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	catch (...)
	{
		std::cerr << "SortingLogicTests aborted during " << stage << " by an unknown exception after " << checks << " checks.\n";
		return EXIT_FAILURE;
	}
	std::cout << "SortingLogicTests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
