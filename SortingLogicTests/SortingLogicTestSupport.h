#pragma once

#include "../SortingLogic/SortingLogic.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sortinglogic_tests
{
	extern int failures;
	extern std::size_t checks;

	void check(bool condition, const std::string& message);
	bool sameItem(const sortinglogic::SortItem& a, const sortinglogic::SortItem& b);
	bool sameMetrics(const sortinglogic::SortMetrics& a, const sortinglogic::SortMetrics& b);
	bool sameSlots(const std::vector<sortinglogic::SortSlot>& a, const std::vector<sortinglogic::SortSlot>& b);
	bool sameState(const sortinglogic::SortState& a, const sortinglogic::SortState& b);
	std::vector<sortinglogic::SortItem> floats(const std::vector<float>& values);
	std::vector<sortinglogic::SortItem> integers(const std::vector<int>& values);
	bool isSorted(const sortinglogic::SortState& state, sortinglogic::SortDirection direction);
	void verifyAlgorithmCase(const std::string& label, const std::vector<sortinglogic::SortItem>& input,
		sortinglogic::InputProfile profile, sortinglogic::SortAlgorithm algorithm, sortinglogic::SortDirection direction,
		sortinglogic::InputDomain domain = sortinglogic::InputDomain::NormalizedFloats,
		sortinglogic::MergeWritebackMode mergeMode = sortinglogic::MergeWritebackMode::AtomicRangeWrite,
		sortinglogic::MergeExecutionMode mergeExecution = sortinglogic::MergeExecutionMode::BottomUp);

	void testMergeWritebackModes();
	void verifyProfilesAndAlgorithms();
	void testBurntPancakeOrientation();
	void testBinaryInsertionStrategy();
	void testPancakeStrategyFamily();
	void testSortingNetworkSchedules();
	void testDirectDistributionStrategies();
	void testMatrixSortShapes();
	void testIncompleteStoogeOutcome();
	void testStoogeFamilyVariants();
	void testRecursiveExchangeNoveltyVariants();
	void testFunSortVariants();
	void testDistributionAudioBaselines();
	void testStochasticNoveltyVariants();
	void testAdditionalBogoVariants();
	void testPointerQuicksortVariants();
	void testNestedBogoVariants();
	void testDualPivotQuickVariants();
	void testStableQuickVariants();
	void testMergeFamilyVariants();
	void testIterativeTopDownMergeSort();
	void testInPlaceMergeSort();
	void testParallelMergeSort();
	void testRotateMergeSort();
	void testParallelRotateMergeSort();
	void testStacklessRotateMergeSort();
	void testBlockSwapMergeSort();
	void testWeavedMergeSort();
	void testTwinSort();
	void testTimMergeVariants();
	void testImprovedInPlaceMergeSort();
	void testBlockInsertionSort();
	void testBufferedStoogeSort();
	void testBadSort();
	void testAsynchronousSort();
	void testPDMergeSort();
	void testStableCycleSort();
	void testTournamentSorts();
	void testSmoothSortVariants();
	void testImprovedBlockSelectionSort();
	void testStacklessHybridQuickSort();
	void testStacklessDualPivotQuickSort();
	void testHanoiSort();
	void testSimplifiedLibrarySort();
	void testLibrarySort();
	void testWeaveMergeSort();
	void testLaziestStableSort();
	void testLazierestStableSort();
	void testLazyStableMergeVariants();
	void testFifthAndQuadMergeSorts();
	void testBufferPartitionMergeSort();
	void testParallelBlockMergeSort();
	void testOptimizedRotateMergeSort();
	void testGrailSortModes();
	void testAdaptiveGrailSort();
	void testPDQVariants(bool includeStressPatterns);
	void testSqrtMergeVariants();
	void testEctaSort();
	void testCircularAndUnstableGrailSorts();
	void testDropMergeSort();
	void testParallelGrailSort();
	void testMedianMergeSort();
	void testRemiSort();
	void testRemainingMergeRosterBatch(std::size_t firstAlgorithm);
	void testTernaryQuickVariants();
	void testIndirectAndPermutationVariants();
	void testHybridCombAndIntroCircleVariants();
	void testHeapStrategyFamily();
	void testCircleStrategyFamily();
	void testElementaryTailStrategies();
	void testDoubleInsertionVariants();
	void testIntroSort();
	void testPatienceSort();
	void testParallelShellSort();
	void testGapInsertionStrategyCoverage();
	void testMergeInsertionSort();
	void testStalinVariants();
	void testUnbalancedTreeVariants();
	void testStrandSort();
	void testBalancedTreeSorts();
	void testSplayTreeSort();
}
