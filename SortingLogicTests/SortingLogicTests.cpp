#include "../SortingLogic/SortingLogic.h"
#include "../SortingLogic/TraceBuildService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
					check(event.comparison == compareItems(a->second, b->second, direction), label + " stores exact comparator outcome");
			}
			try { applyEventForward(state, event); }
			catch (const std::exception& e) { check(false, label + " replay failed at " + std::to_string(i) + ": " + e.what()); return; }
			std::string reason;
			check(validateState(state, input.size(), &reason), label + " preserves unique primary identity: " + reason);
			check(state.metrics.comparisons == observedComparisons, label + " cursor metrics track comparison events");
			cursors.push_back(state);
		}
		check(isSorted(state, direction), label + " ends in the selected order");
		std::map<std::uint64_t, bool> seen;
		for (std::size_t i = 0; i < state.slots.size(); ++i) seen[state.slots[i].item.stableId] = true;
		check(seen.size() == input.size(), label + " preserves every primary item exactly once");
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (descriptor && input.size() >= 2)
			for (std::size_t metric = 0; metric < descriptor->metrics.size(); ++metric)
				check(state.metrics.algorithm.find(descriptor->metrics[metric].key) != state.metrics.algorithm.end(),
					label + " records catalog metric " + descriptor->metrics[metric].displayName);
		if (descriptor && descriptor->stable)
			for (std::size_t i = 1; i < state.slots.size(); ++i)
				if (compareItems(state.slots[i - 1].item, state.slots[i].item, direction) == ComparisonResult::Equal)
					check(originalPosition[state.slots[i - 1].item.stableId] < originalPosition[state.slots[i].item.stableId], label + " preserves stability");
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
		const std::vector<std::vector<float> > floatCases = {
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
		check(catalog.size() == 16, "static catalog exposes the original showcase and the first novelty-family expansion");
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
			for (std::size_t c = 0; c < integerInputs.size(); ++c)
				for (int d = 0; d < 2; ++d)
					verifyAlgorithmCase(std::string(catalog[a].displayName) + " integer case " + std::to_string(c), integerInputs[c],
						InputProfile::IntegerKeys, catalog[a].id, d == 0 ? SortDirection::Ascending : SortDirection::Descending);
		}
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

int main()
{
	const char* stage = "verifyProfilesAndAlgorithms";
	try
	{
		verifyProfilesAndAlgorithms();
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
