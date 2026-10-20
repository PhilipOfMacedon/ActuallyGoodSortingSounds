#include "SortingLogic.h"
#include "AlgorithmStrategies.h"
#include "SortingLogicInternal.h"
#include "TracePresentationPolicy.h"
#include "TraceRecorder.h"
#include "TraceRequestBuilderInternal.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace sortinglogic
{
	namespace detail
	{
		SortTrace buildAlgorithmTrace(const TraceBuildRequest& request, SortAlgorithm algorithm,
			AlgorithmStrategy strategy)
		{
			std::unique_ptr<Recorder> recorderStorage;
			try
			{
				recorderStorage.reset(new Recorder(request));
			}
			catch (const TraceBudgetReached& limit)
			{
				SortTrace failed;
				failed.algorithm = algorithm;
				const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
				if (descriptor) { failed.familyId = descriptor->familyId; failed.variantId = descriptor->variantId; }
				failed.direction = request.direction;
				failed.domain = effectiveDomain(request);
				failed.input = request.input;
				failed.initialState = makeInitialState(request.input);
				failed.snapshots.insert(std::make_pair(static_cast<std::size_t>(0), failed.initialState));
				failed.complete = false;
				failed.completionReason = limit.reason;
				return failed;
			}
			Recorder& recorder = *recorderStorage;
			try
			{
				strategy(recorder);
				recorder.finish();
			}
			catch (const OperationLimitReached&)
			{
				recorder.finishPartial("Global trace operation budget reached (" + std::to_string(request.maxOperations) + " operations).");
			}
			catch (const TraceBudgetReached& limit)
			{
				recorder.finishPartial(limit.reason);
			}
			sortinglogic::populateAudioEventIndices(recorder.trace);
			std::string reason;
			if (!validateState(recorder.state, request.input.size(), &reason)) throw std::logic_error(reason.c_str());
			return recorder.trace;
		}

	}

#define SORT_STRATEGY_REQUEST_BUILDER(name, id, strategy) \
		SortTrace name(const TraceBuildRequest& request) { return detail::buildAlgorithmTrace(request, SortAlgorithm::id, strategy); }
		SORT_STRATEGY_REQUEST_BUILDER(buildInsertionRequest, Insertion, &strategies::insertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildBubbleRequest, Bubble, &strategies::bubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildSelectionRequest, Selection, &strategies::selection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBingoRequest, ArrayvBingo, &strategies::bingo)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDoubleSelectionRequest, ArrayvDoubleSelection, &strategies::doubleSelection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableSelectionRequest, ArrayvStableSelection, &strategies::stableSelection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBadSortRequest, ArrayvBadSort, &strategies::badSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAsynchronousRequest, ArrayvAsynchronous, &strategies::asynchronousSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDMergeRequest, ArrayvPDMerge, &strategies::pdMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableCycleRequest, ArrayvStableCycle, &strategies::stableCycleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicTournamentRequest, ArrayvClassicTournament, &strategies::classicTournamentSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTournamentRequest, ArrayvTournament, &strategies::tournamentSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmoothSortRequest, ArrayvSmoothSort, &strategies::smoothSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinomialSmoothSortRequest, ArrayvBinomialSmoothSort, &strategies::binomialSmoothSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvImprovedBlockSelectionRequest, ArrayvImprovedBlockSelection, &strategies::improvedBlockSelectionSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessHybridQuickRequest, ArrayvStacklessHybridQuick, &strategies::stacklessHybridQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessDualPivotQuickRequest, ArrayvStacklessDualPivotQuick, &strategies::stacklessDualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvHanoiRequest, ArrayvHanoi, &strategies::hanoiSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLibraryRequest, ArrayvLibrary, &strategies::librarySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimplifiedLibraryRequest, ArrayvSimplifiedLibrary, &strategies::simplifiedLibrarySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveMergeRequest, ArrayvWeaveMerge, &strategies::weaveMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLaziestRequest, ArrayvLaziest, &strategies::laziestStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazierestRequest, ArrayvLazierest, &strategies::lazierestStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazyStableRequest, ArrayvLazyStable, &strategies::lazyStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedLazyStableRequest, ArrayvOptimizedLazyStable, &strategies::optimizedLazyStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFifthMergeRequest, ArrayvFifthMerge, &strategies::fifthMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvQuadMergeRequest, ArrayvQuadMerge, &strategies::quadMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBufferPartitionMergeRequest, ArrayvBufferPartitionMerge, &strategies::bufferPartitionMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvParallelBlockMergeRequest, ArrayvParallelBlockMerge, &strategies::parallelBlockMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedRotateMergeRequest, ArrayvOptimizedRotateMerge, &strategies::optimizedRotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGrailRequest, ArrayvGrail, &strategies::grailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvParallelGrailRequest, ArrayvParallelGrail, &strategies::parallelGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAdaptiveGrailRequest, ArrayvAdaptiveGrail, &strategies::adaptiveGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDQBranchedRequest, ArrayvPDQBranched, &strategies::pdqBranchedStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDQBranchlessRequest, ArrayvPDQBranchless, &strategies::pdqBranchlessStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSqrtRequest, ArrayvSqrt, &strategies::sqrtMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSynchronousSqrtRequest, ArrayvSynchronousSqrt, &strategies::synchronousSqrtMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvEctaRequest, ArrayvEcta, &strategies::ectaStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircularGrailRequest, ArrayvCircularGrail, &strategies::circularGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnstableGrailRequest, ArrayvUnstableGrail, &strategies::unstableGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDropMergeRequest, ArrayvDropMerge, &strategies::dropMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMedianMergeRequest, ArrayvMedianMerge, &strategies::medianMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRemiRequest, ArrayvRemi, &strategies::remiSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvChaliceRequest, ArrayvChalice, &strategies::chaliceSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFlanRequest, ArrayvFlan, &strategies::flanSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFluxRequest, ArrayvFlux, &strategies::fluxSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvKotaRequest, ArrayvKota, &strategies::kotaMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedWeaveMergeRequest, ArrayvOptimizedWeaveMerge, &strategies::optimizedWeaveMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWikiRequest, ArrayvWiki, &strategies::wikiMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvYujisBufferedMerge2Request, ArrayvYujisBufferedMerge2, &strategies::yujiBufferedMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAndreyRequest, ArrayvAndrey, &strategies::andreyMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvNewShuffleMergeRequest, ArrayvNewShuffleMerge, &strategies::newShuffleMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCycleRequest, ArrayvCycle, &strategies::cycleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildShellRequest, Shell, &strategies::shell)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryInsertionRequest, ArrayvBinaryInsertion, &strategies::binaryInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBlockInsertionRequest, ArrayvBlockInsertion, &strategies::blockInsertionSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDoubleInsertionRequest, ArrayvDoubleInsertion, &strategies::linearDoubleInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryDoubleInsertionRequest, ArrayvBinaryDoubleInsertion, &strategies::binaryDoubleInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroSortRequest, ArrayvIntroSort, &strategies::introSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPatienceRequest, ArrayvPatience, &strategies::patienceSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShellParallelRequest, ArrayvShellParallel, &strategies::shellSortParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMergeInsertionRequest, ArrayvMergeInsertion, &strategies::mergeInsertionSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicTreeRequest, ArrayvClassicTree, &strategies::classicTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTreeRequest, ArrayvTree, &strategies::treeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStrandRequest, ArrayvStrand, &strategies::strandSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAATreeRequest, ArrayvAATree, &strategies::aaTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAVLTreeRequest, ArrayvAVLTree, &strategies::avlTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSplayTreeRequest, ArrayvSplayTree, &strategies::splayTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRedBlackTreeRequest, ArrayvRedBlackTree, &strategies::redBlackTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRecursiveShellRequest, ArrayvRecursiveShell, &strategies::recursiveShell)
		SORT_STRATEGY_REQUEST_BUILDER(buildMergeRequest, Merge, &strategies::mergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildQuickRequest, Quick, &strategies::quick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLLQuickRequest, ArrayvLLQuick, &strategies::leftLeftQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLRQuickRequest, ArrayvLRQuick, &strategies::leftRightQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLRQuickParallelRequest, ArrayvLRQuickParallel, &strategies::leftRightQuickParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildHeapRequest, Heap, &strategies::heapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryHeapRequest, ArrayvTernaryHeap, &strategies::ternaryHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBaseNHeapRequest, ArrayvBaseNHeap, &strategies::baseNHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMinHeapRequest, ArrayvMinHeap, &strategies::minHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFlippedMinHeapRequest, ArrayvFlippedMinHeap, &strategies::flippedMinHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBottomUpHeapRequest, ArrayvBottomUpHeap, &strategies::bottomUpHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinomialHeapRequest, ArrayvBinomialHeap, &strategies::binomialHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazyHeapRequest, ArrayvLazyHeap, &strategies::lazyHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMinMaxHeapRequest, ArrayvMinMaxHeap, &strategies::minMaxHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOutOfPlaceHeapRequest, ArrayvOutOfPlaceHeap, &strategies::outOfPlaceHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTriangularHeapRequest, ArrayvTriangularHeap, &strategies::triangularHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeakHeapRequest, ArrayvWeakHeap, &strategies::weakHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPoplarHeapRequest, ArrayvPoplarHeap, &strategies::poplarHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildCountingRequest, Counting, &strategies::counting)
		SORT_STRATEGY_REQUEST_BUILDER(buildRadixRequest, Radix, &strategies::radix)
		SORT_STRATEGY_REQUEST_BUILDER(buildGnomeRequest, Gnome, &strategies::gnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildCocktailRequest, Cocktail, &strategies::cocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildCombRequest, Comb, &strategies::comb)
		SORT_STRATEGY_REQUEST_BUILDER(buildOddEvenRequest, OddEven, &strategies::oddEven)
		SORT_STRATEGY_REQUEST_BUILDER(buildPancakeRequest, Pancake, &strategies::pancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBurntPancakeRequest, ArrayvBurntPancake, &strategies::arrayvBurntPancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildCanonicalBurntPancakeRequest, CanonicalBurntPancake, &strategies::canonicalBurntPancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPancakeInsertionRequest, ArrayvPancakeInsertion, &strategies::arrayvPancakeInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildStoogeRequest, Stooge, &strategies::stooge)
		SORT_STRATEGY_REQUEST_BUILDER(buildBogoRequest, Bogo, &strategies::bogo)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBozoRequest, ArrayvBozo, &strategies::bozoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBubbleBogoRequest, ArrayvBubbleBogo, &strategies::bubbleBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvExchangeBogoRequest, ArrayvExchangeBogo, &strategies::exchangeBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDeterministicBogoRequest, ArrayvDeterministicBogo, &strategies::deterministicBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSelectionBogoRequest, ArrayvSelectionBogo, &strategies::selectionBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLessBogoRequest, ArrayvLessBogo, &strategies::lessBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCocktailBogoRequest, ArrayvCocktailBogo, &strategies::cocktailBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMedianQuickBogoRequest, ArrayvMedianQuickBogo, &strategies::medianQuickBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvQuickBogoRequest, ArrayvQuickBogo, &strategies::quickBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRandomGuessRequest, ArrayvRandomGuess, &strategies::randomGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedGuessRequest, ArrayvOptimizedGuess, &strategies::optimizedGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmartGuessRequest, ArrayvSmartGuess, &strategies::smartGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGuessRequest, ArrayvGuess, &strategies::guessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBogoBogoRequest, ArrayvBogoBogo, &strategies::bogoBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMergeBogoRequest, ArrayvMergeBogo, &strategies::mergeBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmartBogoBogoRequest, ArrayvSmartBogoBogo, &strategies::smartBogoBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDualPivotQuickRequest, ArrayvDualPivotQuick, &strategies::dualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedDualPivotQuickRequest, ArrayvOptimizedDualPivotQuick, &strategies::optimizedDualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableQuickRequest, ArrayvStableQuick, &strategies::stableQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableQuickParallelRequest, ArrayvStableQuickParallel, &strategies::stableQuickParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvForcedStableQuickRequest, ArrayvForcedStableQuick, &strategies::forcedStableQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryLLQuickRequest, ArrayvTernaryLLQuick, &strategies::ternaryLeftLeftQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryLRQuickRequest, ArrayvTernaryLRQuick, &strategies::ternaryLeftRightQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStablePermutationRequest, ArrayvStablePermutation, &strategies::stablePermutationSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTableRequest, ArrayvTable, &strategies::tableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryMergeRequest, ArrayvBinaryMerge, &strategies::binaryMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIterativeTopDownMergeRequest, ArrayvIterativeTopDownMerge, &strategies::iterativeTopDownMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvInPlaceMergeRequest, ArrayvInPlaceMerge, &strategies::inPlaceMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvParallelMergeRequest, ArrayvParallelMerge, &strategies::parallelMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMergeRequest, ArrayvRotateMerge, &strategies::rotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMergeParallelRequest, ArrayvRotateMergeParallel, &strategies::parallelRotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessRotateMergeRequest, ArrayvStacklessRotateMerge, &strategies::stacklessRotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBlockSwapMergeRequest, ArrayvBlockSwapMerge, &strategies::blockSwapMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeavedMergeRequest, ArrayvWeavedMerge, &strategies::weavedMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTwinMergeRequest, ArrayvTwinMerge, &strategies::twinMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTimRequest, ArrayvTim, &strategies::timMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCocktailMergeRequest, ArrayvCocktailMerge, &strategies::cocktailMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvImprovedInPlaceMergeRequest, ArrayvImprovedInPlaceMerge, &strategies::improvedInPlaceMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedBottomUpMergeRequest, ArrayvOptimizedBottomUpMerge, &strategies::optimizedBottomUpMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedBubbleRequest, ArrayvOptimizedBubble, &strategies::optimizedBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnoptimizedBubbleRequest, ArrayvUnoptimizedBubble, &strategies::unoptimizedBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSwaplessBubbleRequest, ArrayvSwaplessBubble, &strategies::swaplessBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryGnomeRequest, ArrayvBinaryGnome, &strategies::binaryGnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircleIterativeRequest, ArrayvCircleIterative, &strategies::circleSortIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircleRecursiveRequest, ArrayvCircleRecursive, &strategies::circleSortRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroCircleIterativeRequest, ArrayvIntroCircleIterative, &strategies::introCircleSortIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroCircleRecursiveRequest, ArrayvIntroCircleRecursive, &strategies::introCircleSortRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvHybridCombRequest, ArrayvHybridComb, &strategies::hybridComb)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicThreeSmoothCombRequest, ArrayvClassicThreeSmoothComb, &strategies::classicThreeSmoothComb)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombIterativeRequest, ArrayvThreeSmoothCombIterative, &strategies::threeSmoothCombIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombRecursiveRequest, ArrayvThreeSmoothCombRecursive, &strategies::recursiveThreeSmoothSequential)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombParallelRequest, ArrayvThreeSmoothCombParallel, &strategies::recursiveThreeSmoothParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicIterativeRequest, ArrayvBitonicIterative, &strategies::bitonicIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicRecursiveRequest, ArrayvBitonicRecursive, &strategies::bitonicRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicParallelRequest, ArrayvBitonicParallel, &strategies::bitonicParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonIterativeRequest, ArrayvBoseNelsonIterative, &strategies::boseNelsonIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonRecursiveRequest, ArrayvBoseNelsonRecursive, &strategies::boseNelsonRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonParallelRequest, ArrayvBoseNelsonParallel, &strategies::boseNelsonParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeIterativeRequest, ArrayvOddEvenMergeIterative, &strategies::oddEvenMergeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeRecursiveRequest, ArrayvOddEvenMergeRecursive, &strategies::oddEvenMergeRecursiveDefault)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeParallelRequest, ArrayvOddEvenMergeParallel, &strategies::oddEvenMergeParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDiamondIterativeRequest, ArrayvDiamondIterative, &strategies::diamondIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDiamondRecursiveRequest, ArrayvDiamondRecursive, &strategies::diamondRecursiveVariant)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseIterativeRequest, ArrayvPairwiseIterative, &strategies::pairwiseIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseRecursiveRequest, ArrayvPairwiseRecursive, &strategies::pairwiseRecursiveDefault)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCreaseRequest, ArrayvCrease, &strategies::creaseSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFoldRequest, ArrayvFold, &strategies::foldSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMergeExchangeIterativeRequest, ArrayvMergeExchangeIterative, &strategies::mergeExchangeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseMergeIterativeRequest, ArrayvPairwiseMergeIterative, &strategies::pairwiseMergeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseMergeRecursiveRequest, ArrayvPairwiseMergeRecursive, &strategies::pairwiseMergeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveIterativeRequest, ArrayvWeaveIterative, &strategies::weaveIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveParallelRequest, ArrayvWeaveParallel, &strategies::weaveParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveRecursiveRequest, ArrayvWeaveRecursive, &strategies::weaveRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMatrixRequest, ArrayvMatrix, &strategies::matrixSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedStoogeStudioRequest, ArrayvOptimizedStoogeStudio, &strategies::optimizedStoogeStudio)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvQuadStoogeRequest, ArrayvQuadStooge, &strategies::quadStooge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSlowRequest, ArrayvSlow, &strategies::slowSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircloidRequest, ArrayvCircloid, &strategies::circloidSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSillyRequest, ArrayvSilly, &strategies::sillySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSnuffleRequest, ArrayvSnuffle, &strategies::snuffleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCompleteGraphRequest, ArrayvCompleteGraph, &strategies::completeGraphSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedCocktailRequest, ArrayvOptimizedCocktail, &strategies::optimizedCocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnoptimizedCocktailRequest, ArrayvUnoptimizedCocktail, &strategies::unoptimizedCocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedGnomeRequest, ArrayvOptimizedGnome, &strategies::optimizedGnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShoveRequest, ArrayvShove, &strategies::shove)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSlopeRequest, ArrayvSlope, &strategies::slope)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAmericanFlagRequest, ArrayvAmericanFlag, &strategies::americanFlag)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryQuickIterativeRequest, ArrayvBinaryQuickIterative, &strategies::binaryQuickIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryQuickRecursiveRequest, ArrayvBinaryQuickRecursive, &strategies::binaryQuickRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicGravityRequest, ArrayvClassicGravity, &strategies::classicGravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFlashRequest, ArrayvFlash, &strategies::flash)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGravityRequest, ArrayvGravity, &strategies::gravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvInPlaceLsdRadixRequest, ArrayvInPlaceLsdRadix, &strategies::inPlaceLsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIndexRequest, ArrayvIndex, &strategies::indexSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLsdRadixRequest, ArrayvLsdRadix, &strategies::radix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMsdRadixRequest, ArrayvMsdRadix, &strategies::msdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPigeonholeRequest, ArrayvPigeonhole, &strategies::pigeonhole)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateLsdRadixRequest, ArrayvRotateLsdRadix, &strategies::rotateLsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMsdRadixRequest, ArrayvRotateMsdRadix, &strategies::rotateMsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShatterRequest, ArrayvShatter, &strategies::shatterSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimpleShatterRequest, ArrayvSimpleShatter, &strategies::simpleShatterSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimplisticGravityRequest, ArrayvSimplisticGravity, &strategies::simplisticGravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStaticRequest, ArrayvStatic, &strategies::staticSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTimeRequest, ArrayvTime, &strategies::timeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessAmericanFlagRequest, ArrayvStacklessAmericanFlag, &strategies::stacklessAmericanFlag)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessBinaryQuickRequest, ArrayvStacklessBinaryQuick, &strategies::stacklessBinaryRadix)
#undef SORT_STRATEGY_REQUEST_BUILDER
}
