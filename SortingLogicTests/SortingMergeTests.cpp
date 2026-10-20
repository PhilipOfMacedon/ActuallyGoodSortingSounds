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
		TraceBuildRequest stableOptionRequest = atomicRequest;
		stableOptionRequest.optionSelections["execution_mode"] = "top_down";
		stableOptionRequest.optionSelections["writeback_mode"] = "animated_writes";
		const SortTrace stableOptionTrace = SortTraceBuilder::build(stableOptionRequest);
		check(stableOptionTrace.configuration.mergeExecutionMode == MergeExecutionMode::TopDown &&
			stableOptionTrace.configuration.mergeWritebackMode == MergeWritebackMode::AnimatedWrites &&
			stableOptionTrace.events.size() == topDownAnimated.events.size(),
			"stable Merge option IDs override legacy request fields and resolve to the same configured trace");
		TraceBuildRequest explicitConfigurationRequest = atomicRequest;
		explicitConfigurationRequest.mergeExecutionMode = MergeExecutionMode::TopDown;
		AlgorithmConfiguration explicitConfiguration;
		explicitConfigurationRequest.setConfiguration(explicitConfiguration);
		const SortTrace explicitConfigurationTrace = SortTraceBuilder::build(explicitConfigurationRequest);
		check(explicitConfigurationTrace.configuration.mergeExecutionMode == MergeExecutionMode::BottomUp,
			"explicit typed configuration takes precedence over a conflicting legacy request field, including its default value");
		TraceBuildRequest typedHeapRequest;
		typedHeapRequest.input = floats({ 0.8f, 0.1f, 0.6f, 0.2f, 0.4f, 0.3f });
		typedHeapRequest.algorithm = SortAlgorithm::ArrayvBaseNHeap;
		typedHeapRequest.bucketCount = 2;
		AlgorithmConfiguration typedHeapConfiguration;
		typedHeapConfiguration.heapArity = 8;
		typedHeapRequest.setConfiguration(typedHeapConfiguration);
		const SortTrace typedHeapTrace = SortTraceBuilder::build(typedHeapRequest);
		SortState typedHeapEnd = typedHeapTrace.initialState;
		for (std::size_t i = 0; i < typedHeapTrace.events.size(); ++i) applyEventForward(typedHeapEnd, typedHeapTrace.events[i]);
		check(typedHeapTrace.configuration.heapArity == 8 &&
			isSorted(typedHeapEnd, SortDirection::Ascending),
			"typed Base-N heap arity drives the strategy without duplicating it in trace metadata");
		const SortAlgorithm legacyBucketRadixAlgorithms[] = {
			SortAlgorithm::ArrayvAmericanFlag, SortAlgorithm::ArrayvStacklessAmericanFlag
		};
		for (std::size_t i = 0; i < sizeof(legacyBucketRadixAlgorithms) / sizeof(legacyBucketRadixAlgorithms[0]); ++i)
		{
			TraceBuildRequest legacyBucketRequest;
			legacyBucketRequest.algorithm = legacyBucketRadixAlgorithms[i];
			legacyBucketRequest.domain = InputDomain::NonnegativeIntegerKeys;
			legacyBucketRequest.bucketCount = 4;
			legacyBucketRequest.input = integers({ 3, 0, 2, 1 });
			const SortTrace legacyBucketTrace = SortTraceBuilder::build(legacyBucketRequest);
			check(legacyBucketTrace.configuration.radixBase == 4,
				"legacy bucket count resolves to the typed radix base for " + std::string(algorithmDisplayName(legacyBucketRadixAlgorithms[i])));
		}
		TraceBuildRequest invalidTypedHeapRequest = typedHeapRequest;
		AlgorithmConfiguration invalidTypedHeapConfiguration;
		invalidTypedHeapConfiguration.heapArity = 1;
		invalidTypedHeapRequest.setConfiguration(invalidTypedHeapConfiguration);
		bool rejectedInvalidTypedHeap = false;
		try { SortTraceBuilder::build(invalidTypedHeapRequest); }
		catch (const std::invalid_argument&) { rejectedInvalidTypedHeap = true; }
		check(rejectedInvalidTypedHeap, "typed algorithm configuration rejects a choice absent from the catalog");
		TraceBuildRequest unknownOptionRequest = atomicRequest;
		unknownOptionRequest.optionSelections["execution_mode"] = "missing_mode";
		bool rejectedUnknownOption = false;
		try { SortTraceBuilder::build(unknownOptionRequest); }
		catch (const std::invalid_argument&) { rejectedUnknownOption = true; }
		check(rejectedUnknownOption, "trace builder rejects option IDs missing from the selected catalog variant");
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
		check(topDownAtomic.configuration.mergeExecutionMode == MergeExecutionMode::TopDown &&
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
				if (!trace.complete || trace.configuration.rotateMergeBufferSize != request.rotateMergeBufferSize || !isSorted(replay, request.direction) ||
					!hasExpectedBufferedPath || fallbackMerges == 0)
					std::cerr << "Optimized Rotate diagnostic: capacity=" << request.rotateMergeBufferSize << " direction=" << direction
						<< " complete=" << trace.complete << " traceCapacity=" << trace.configuration.rotateMergeBufferSize
						<< " sorted=" << isSorted(replay, request.direction) << " bufferMerges=" << bufferMerges
						<< " fallbackMerges=" << fallbackMerges << " events=" << trace.events.size() << '\n';
				check(trace.complete && trace.configuration.rotateMergeBufferSize == request.rotateMergeBufferSize && isSorted(replay, request.direction) &&
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
					check(trace.configuration.grailBufferMode == static_cast<GrailBufferMode>(mode), "Grail Sort retains its selected buffer mode in the trace");
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
}
