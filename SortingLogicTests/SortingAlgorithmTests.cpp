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
				AlgorithmConfiguration configuration;
				configuration.setValue("heap_arity", arities[arityIndex]);
				request.setConfiguration(configuration);
				const SortTrace trace = SortTraceBuilder::build(request);
				SortState replay = trace.initialState;
				for (std::size_t event = 0; event < trace.events.size(); ++event)
					applyEventForward(replay, trace.events[event]);
				std::string reason;
				check(trace.complete && trace.configuration.value("heap_arity") == arities[arityIndex] &&
					isSorted(replay, request.direction) && validateState(replay, request.input.size(), &reason),
					"Base-N Heap arity " + std::to_string(arities[arityIndex]) + " sorts and preserves identities");
				for (std::size_t event = trace.events.size(); event > 0; --event)
					applyEventBackward(replay, trace.events[event - 1]);
				check(sameState(replay, trace.initialState),
					"Base-N Heap arity " + std::to_string(arities[arityIndex]) + " rewinds exactly");
			}
		}
	}

	void testCompactReconstructionModes()
	{
		using namespace sortinglogic;
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t algorithmIndex = 0; algorithmIndex < catalog.size(); ++algorithmIndex)
		{
			const AlgorithmDescriptor& descriptor = catalog[algorithmIndex];
			if (!descriptor.reconstructionPresentationSupported) continue;
			InputDomain domain = InputDomain::NormalizedFloats;
			InputProfile profile = InputProfile::NormalizedFloat;
			std::vector<SortItem> input = floats({ 0.7f, 0.2f, 0.7f, 0.1f });
			if (!descriptor.supportsNormalizedFloat)
			{
				profile = InputProfile::IntegerKeys;
				domain = descriptor.supportedDomains.empty() ? InputDomain::NonnegativeIntegerKeys : descriptor.supportedDomains.front();
				input = integers({ 3, 1, 2, 0 });
				if (domain == InputDomain::SignedIntegerKeys)
					for (std::size_t i = 0; i < input.size(); ++i)
						input[i] = SortItem(input[i].stableId, input[i].integerKey - 2, input[i].value);
				else if (domain == InputDomain::IntegerPermutation)
				{
					const int permutation[] = { 3, 1, 2, 0 };
					for (std::size_t i = 0; i < input.size(); ++i)
						input[i] = SortItem(input[i].stableId, permutation[i], static_cast<float>(permutation[i]) / 4.0f);
				}
			}
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase(std::string(descriptor.displayName) + " compact/reference mode",
					input, profile, descriptor.id, direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
					domain, true);
		}
	}

	void testDetailedReconstructionCoverage()
	{
		using namespace sortinglogic;
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t algorithmIndex = 0; algorithmIndex < catalog.size(); ++algorithmIndex)
		{
			const AlgorithmDescriptor& descriptor = catalog[algorithmIndex];
			if (!descriptor.reconstructionPresentationSupported) continue;

			const std::size_t initialCount = std::min<std::size_t>(64, descriptor.safeMaxItemCount);
			if (initialCount < 2)
			{
				check(false, std::string(descriptor.stableId) + " reconstruction registration supports at least two items");
				continue;
			}

			InputDomain domain = InputDomain::NormalizedFloats;
			InputProfile profile = InputProfile::NormalizedFloat;
			if (!descriptor.supportsNormalizedFloat)
			{
				profile = InputProfile::IntegerKeys;
				domain = descriptor.supportedDomains.empty() ? InputDomain::NonnegativeIntegerKeys : descriptor.supportedDomains.front();
			}
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				SortTrace trace;
				std::size_t count = initialCount;
				while (count >= 2)
				{
					std::vector<SortItem> input;
					for (std::size_t i = 0; i < count; ++i)
					{
						const int key = static_cast<int>(i);
						const float value = static_cast<float>(key) / static_cast<float>(count - 1);
						input.push_back(profile == InputProfile::NormalizedFloat
							? SortItem(9000 + i, value)
							: SortItem(9000 + i, key, value));
					}
					std::mt19937 random(static_cast<unsigned int>(descriptor.id) ^ static_cast<unsigned int>(count) ^ 0x5EEDu);
					std::shuffle(input.begin(), input.end(), random);
					TraceBuildRequest request;
					request.algorithm = descriptor.id;
					request.familyId = descriptor.familyId;
					request.variantId = descriptor.variantId;
					request.input.swap(input);
					request.profile = profile;
					request.domain = domain;
					request.direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
					try { trace = SortTraceBuilder::build(request); }
					catch (const std::exception& error)
					{
						check(false, std::string(descriptor.stableId) + " detailed reconstruction build failed: " + error.what());
						break;
					}
					if (trace.complete && trace.outcome != AlgorithmOutcome::IncompleteDemonstration) break;
					count /= 2;
				}

				std::size_t reconstructiveWrites = 0;
				for (std::size_t eventIndex = 0; eventIndex < trace.events.size(); ++eventIndex)
				{
					const SemanticEvent& event = trace.events[eventIndex];
					if (!event.reconstructive) continue;
					if (event.type == SemanticEventType::WriteItem || event.type == SemanticEventType::Swap)
						++reconstructiveWrites;
				}
				check(trace.complete && trace.outcome != AlgorithmOutcome::IncompleteDemonstration,
					std::string(descriptor.stableId) + " completes its representative reconstruction input");
				check(trace.configuration.value("reconstruction_mode", 0) == 1,
					std::string(descriptor.stableId) + " defaults to detailed reconstruction");
				check(reconstructiveWrites > 0,
					std::string(descriptor.stableId) + " emits granular reconstruction operations for a scrambled input");
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

	void testBinaryInsertionStrategy()
	{
		using namespace sortinglogic;
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f }, { 0.3f, 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f }, { 0.6f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f }
		};
		for (std::size_t input = 0; input < cases.size(); ++input)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("Binary Insertion case " + std::to_string(input), floats(cases[input]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvBinaryInsertion,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
	}

	void testPancakeStrategyFamily()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::Pancake, SortAlgorithm::ArrayvBurntPancake,
			SortAlgorithm::CanonicalBurntPancake, SortAlgorithm::ArrayvPancakeInsertion };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f }, { 0.3f, 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f }, { 0.6f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f }
		};
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (std::size_t input = 0; input < cases.size(); ++input)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " pancake case " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
		testBurntPancakeOrientation();
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

	void testDirectDistributionStrategies()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = {
			SortAlgorithm::Counting, SortAlgorithm::Radix, SortAlgorithm::ArrayvAmericanFlag,
			SortAlgorithm::ArrayvBinaryQuickIterative, SortAlgorithm::ArrayvBinaryQuickRecursive,
			SortAlgorithm::ArrayvClassicGravity, SortAlgorithm::ArrayvFlash, SortAlgorithm::ArrayvGravity,
			SortAlgorithm::ArrayvInPlaceLsdRadix, SortAlgorithm::ArrayvIndex, SortAlgorithm::ArrayvLsdRadix,
			SortAlgorithm::ArrayvMsdRadix, SortAlgorithm::ArrayvPigeonhole, SortAlgorithm::ArrayvRotateLsdRadix,
			SortAlgorithm::ArrayvRotateMsdRadix, SortAlgorithm::ArrayvShatter, SortAlgorithm::ArrayvSimpleShatter,
			SortAlgorithm::ArrayvSimplisticGravity, SortAlgorithm::ArrayvStatic, SortAlgorithm::ArrayvTime,
			SortAlgorithm::ArrayvStacklessAmericanFlag, SortAlgorithm::ArrayvStacklessBinaryQuick
		};
		const InputDomain candidateDomains[] = { InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys,
			InputDomain::IntegerPermutation, InputDomain::NormalizedFloats };
		for (std::size_t algorithmIndex = 0; algorithmIndex < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithmIndex)
		{
			const SortAlgorithm algorithm = algorithms[algorithmIndex];
			InputDomain domain = InputDomain::NormalizedFloats;
			for (std::size_t domainIndex = 0; domainIndex < sizeof(candidateDomains) / sizeof(candidateDomains[0]); ++domainIndex)
				if (algorithmSupports(algorithm, candidateDomains[domainIndex]))
				{
					domain = candidateDomains[domainIndex];
					break;
				}
			std::vector<SortItem> input;
			if (domain == InputDomain::NormalizedFloats) input = floats({ 0.6f, 0.2f, 0.6f, 0.1f, 0.2f });
			else if (domain == InputDomain::IntegerPermutation)
			{
				const int keys[] = { 3, 1, 4, 0, 2 };
				for (std::size_t item = 0; item < 5; ++item)
					input.push_back(SortItem(item, keys[item], static_cast<float>(keys[item] + 1) / 5.0f));
			}
			else
			{
				const int keys[] = { 3, 1, 3, 0, 1 };
				for (std::size_t item = 0; item < 5; ++item)
				{
					const int key = domain == InputDomain::SignedIntegerKeys ? keys[item] - 2 : keys[item];
					const float value = domain == InputDomain::SignedIntegerKeys
						? static_cast<float>(key + 5) / 10.0f : static_cast<float>(key + 1) / 5.0f;
					input.push_back(SortItem(item, key, value));
				}
			}
			for (int directionIndex = 0; directionIndex < 2; ++directionIndex)
			{
				const SortDirection direction = directionIndex == 0 ? SortDirection::Ascending : SortDirection::Descending;
				verifyAlgorithmCase(std::string("Direct distribution ") + algorithmDisplayName(algorithm) +
					(directionIndex == 0 ? " ascending" : " descending"), input, inputProfileForDomain(domain),
					algorithm, direction, domain);
			}
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
			SortAlgorithm::Stooge, SortAlgorithm::ArrayvCircloid, SortAlgorithm::ArrayvOptimizedStoogeStudio,
			SortAlgorithm::ArrayvQuadStooge, SortAlgorithm::ArrayvSlow
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

	void testFunSortVariants()
	{
		using namespace sortinglogic;
		const std::uint32_t reportedSeed = 2688576985u;
		Simulation fixture(64, reportedSeed, InputDomain::NormalizedFloats, InputPattern::Random);
		TraceBuildRequest reported;
		reported.algorithm = SortAlgorithm::ArrayvFun;
		reported.input = fixture.preparedInput();
		reported.inputSeed = reportedSeed;
		reported.domain = InputDomain::NormalizedFloats;
		reported.pattern = InputPattern::Random;
		reported.profile = InputProfile::NormalizedFloat;
		for (int direction = 0; direction < 2; ++direction)
		{
			reported.direction = direction == 0 ? SortDirection::Ascending : SortDirection::Descending;
			const SortTrace trace = SortTraceBuilder::build(reported);
			SortState replay = trace.initialState;
			std::map<std::uint64_t, SortItem> itemById;
			for (std::size_t i = 0; i < reported.input.size(); ++i)
				itemById[reported.input[i].stableId] = reported.input[i];
			for (std::size_t i = 0; i < trace.events.size(); ++i)
			{
				const SemanticEvent& event = trace.events[i];
				if (event.type == SemanticEventType::Compare)
				{
					check(event.comparisonBasis == ComparisonBasis::ValueThenStableId,
						"corrected Fun records its total comparison basis");
					check(event.comparison == compareItems(itemById[event.firstItemId], itemById[event.secondItemId],
						reported.direction, event.comparisonBasis),
						"corrected Fun comparison outcome matches the recorded value-plus-ID order");
				}
				applyEventForward(replay, event);
			}
			std::string reason;
			check(trace.complete && trace.outcome == AlgorithmOutcome::SortedPermutation &&
				isSorted(replay, reported.direction) && validateState(replay, reported.input.size(), &reason),
				"reported 64-item seed is fully sorted and retains every item in both directions");
			for (std::size_t i = trace.events.size(); i > 0; --i) applyEventBackward(replay, trace.events[i - 1]);
			check(sameState(replay, trace.initialState), "corrected Fun trace rewinds exactly");
		}

		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f, 0.4f },
			{ 0.3f, 0.3f, 0.3f, 0.3f }, { 0.0f, 0.1f, 0.2f, 0.3f },
			{ 0.3f, 0.2f, 0.1f, 0.0f }, { 0.6f, 0.1f, 0.4f, 0.1f, 0.9f, 0.2f }
		};
		for (std::size_t shape = 0; shape < cases.size(); ++shape)
			for (int direction = 0; direction < 2; ++direction)
				verifyAlgorithmCase("corrected Fun shape " + std::to_string(shape), floats(cases[shape]),
					InputProfile::NormalizedFloat, SortAlgorithm::ArrayvFun,
					direction == 0 ? SortDirection::Ascending : SortDirection::Descending);

		TraceBuildRequest original;
		original.algorithm = SortAlgorithm::ArrayvFunOriginal;
		original.input = floats({ 0.4f, 0.2f, 0.4f, 0.2f, 0.4f });
		original.profile = InputProfile::NormalizedFloat;
		const SortTrace originalTrace = SortTraceBuilder::build(original);
		SortState originalState = originalTrace.initialState;
		for (std::size_t i = 0; i < originalTrace.events.size(); ++i)
			applyEventForward(originalState, originalTrace.events[i]);
		const AlgorithmDescriptor* correctedDescriptor = algorithmDescriptor(SortAlgorithm::ArrayvFun);
		const AlgorithmDescriptor* originalDescriptor = algorithmDescriptor(SortAlgorithm::ArrayvFunOriginal);
		check(correctedDescriptor && originalDescriptor && !correctedDescriptor->stable && !originalDescriptor->stable &&
			originalDescriptor->safeMaxItemCount == 64 && !isSorted(originalState, SortDirection::Ascending) &&
			originalTrace.outcome == AlgorithmOutcome::IncompleteDemonstration,
			"upstream original remains separately labeled with its duplicate-key limitation and 64-item cap");
		check(algorithmAcceptsItemCount(SortAlgorithm::ArrayvFun, 64) && !algorithmAcceptsItemCount(SortAlgorithm::ArrayvFun, 65),
			"corrected Fun keeps its conservative 64-item catalog limit");
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
				request.optionSelections["reconstruction_mode"] = "compact_reference";
				request.inputSeed = 0x13579BDFu;
				for (std::size_t i = 0; i < 8; ++i)
					request.input.push_back(SortItem(100 + i, keys[i], static_cast<float>(keys[i] + 1) / 8.0f));
				const SortTrace trace = SortTraceBuilder::build(request);
				check(trace.events.size() == eventCounts[algorithm] && trace.audioEventIndices.size() == audioCounts[algorithm],
					"Counting/Radix baseline event and audio-eligibility counts stay unchanged");
				const std::uint64_t actualEventHash = distributionFixtureHash(trace, false);
				const std::uint64_t actualAudioHash = distributionFixtureHash(trace, true);
				if (actualEventHash != eventHashes[algorithm][direction] || actualAudioHash != audioHashes[algorithm][direction])
					std::cerr << "Distribution fixture diagnostic: algorithm=" << algorithm << " direction=" << direction
						<< " events=" << trace.events.size() << " audio=" << trace.audioEventIndices.size()
						<< " eventHash=" << actualEventHash << " expectedEventHash=" << eventHashes[algorithm][direction]
						<< " audioHash=" << actualAudioHash << " expectedAudioHash=" << audioHashes[algorithm][direction] << "\n";
				check(actualEventHash == eventHashes[algorithm][direction] && actualAudioHash == audioHashes[algorithm][direction],
					"Counting/Radix deterministic event order and audio operands match the frozen fixture");
			}
	}

	void testStochasticNoveltyVariants()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::Bogo, SortAlgorithm::ArrayvBozo,
			SortAlgorithm::ArrayvBubbleBogo, SortAlgorithm::ArrayvExchangeBogo };
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
				const char* attemptsMetric = algorithms[algorithmIndex] == SortAlgorithm::Bogo ? "shuffle_attempts" : "random_attempts";
				const std::map<std::string, std::uint64_t>::const_iterator attempts = replay.metrics.algorithm.find(attemptsMetric);
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

	void testPDQVariants(bool includeStressPatterns = true)
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
			if (!includeStressPatterns) continue;
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

	void testHeapStrategyFamily()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::Heap, SortAlgorithm::ArrayvTernaryHeap,
			SortAlgorithm::ArrayvBaseNHeap, SortAlgorithm::ArrayvMinHeap, SortAlgorithm::ArrayvFlippedMinHeap,
			SortAlgorithm::ArrayvBottomUpHeap, SortAlgorithm::ArrayvBinomialHeap, SortAlgorithm::ArrayvLazyHeap,
			SortAlgorithm::ArrayvMinMaxHeap, SortAlgorithm::ArrayvOutOfPlaceHeap, SortAlgorithm::ArrayvTriangularHeap,
			SortAlgorithm::ArrayvWeakHeap, SortAlgorithm::ArrayvPoplarHeap };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f }, { 0.3f, 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f }, { 0.6f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f }
		};
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (std::size_t input = 0; input < cases.size(); ++input)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " heap case " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
	}

	void testCircleStrategyFamily()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvCircleIterative, SortAlgorithm::ArrayvCircleRecursive,
			SortAlgorithm::ArrayvIntroCircleIterative, SortAlgorithm::ArrayvIntroCircleRecursive };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f, 0.2f }, { 0.3f, 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f }, { 0.6f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f }
		};
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (std::size_t input = 0; input < cases.size(); ++input)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " circle case " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
	}

	void testElementaryTailStrategies()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::ArrayvBinaryGnome, SortAlgorithm::ArrayvOptimizedGnome,
			SortAlgorithm::ArrayvShove, SortAlgorithm::ArrayvSlope, SortAlgorithm::ArrayvHybridComb };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.4f, 0.2f, 0.4f }, { 0.3f, 0.3f, 0.3f },
			{ 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f }, { 0.6f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f }
		};
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (std::size_t input = 0; input < cases.size(); ++input)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " elementary case " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending);
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

	void testGapInsertionStrategyCoverage()
	{
		using namespace sortinglogic;
		const SortAlgorithm algorithms[] = { SortAlgorithm::Shell, SortAlgorithm::ArrayvRecursiveShell };
		const std::vector<std::vector<float> > cases = {
			{}, { 0.5f }, { 0.9f, 0.1f }, { 0.4f, 0.4f, 0.4f },
			{ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f },
			{ 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f },
			{ 0.7f, 0.1f, 0.7f, 0.2f, 0.5f, 0.3f, 0.1f, 0.8f, 0.4f, 0.2f, 0.9f, 0.3f, 0.6f }
		};
		for (std::size_t algorithm = 0; algorithm < sizeof(algorithms) / sizeof(algorithms[0]); ++algorithm)
			for (std::size_t input = 0; input < cases.size(); ++input)
				for (int direction = 0; direction < 2; ++direction)
					verifyAlgorithmCase(std::string(algorithmDisplayName(algorithms[algorithm])) + " focused shape " + std::to_string(input),
						floats(cases[input]), InputProfile::NormalizedFloat, algorithms[algorithm],
						direction == 0 ? SortDirection::Ascending : SortDirection::Descending,
						InputDomain::NormalizedFloats);
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
}
