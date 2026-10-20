#pragma once

#include <cstddef>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
	}

	namespace strategies
	{
		void badSort(detail::Recorder& recorder);
		void bingo(detail::Recorder& recorder);
		void bubble(detail::Recorder& recorder);
		void comb(detail::Recorder& recorder);
		void cocktail(detail::Recorder& recorder);
		void cycleSort(detail::Recorder& recorder);
		void doubleSelection(detail::Recorder& recorder);
		void gnome(detail::Recorder& recorder);
		void insertion(detail::Recorder& recorder);
		void binaryInsertion(detail::Recorder& recorder);
		void binaryInsertionRange(detail::Recorder& recorder, std::size_t first, std::size_t end);
		void oddEven(detail::Recorder& recorder);
		void binaryGnome(detail::Recorder& recorder);
		void optimizedGnome(detail::Recorder& recorder);
		void shove(detail::Recorder& recorder);
		void slope(detail::Recorder& recorder);
		void hybridComb(detail::Recorder& recorder);
		void circleSortIterative(detail::Recorder& recorder);
		void circleSortRecursive(detail::Recorder& recorder);
		void introCircleSortIterative(detail::Recorder& recorder);
		void introCircleSortRecursive(detail::Recorder& recorder);
		void heapSort(detail::Recorder& recorder);
		void ternaryHeapSort(detail::Recorder& recorder);
		void baseNHeapSort(detail::Recorder& recorder);
		void minHeapSort(detail::Recorder& recorder);
		void flippedMinHeapSort(detail::Recorder& recorder);
		void bottomUpHeapSort(detail::Recorder& recorder);
		void binomialHeapSort(detail::Recorder& recorder);
		void lazyHeapSort(detail::Recorder& recorder);
		void minMaxHeapSort(detail::Recorder& recorder);
		void outOfPlaceHeapSort(detail::Recorder& recorder);
		void triangularHeapSort(detail::Recorder& recorder);
		void weakHeapSort(detail::Recorder& recorder);
		void poplarHeapSort(detail::Recorder& recorder);
		void optimizedBubble(detail::Recorder& recorder);
		void optimizedCocktail(detail::Recorder& recorder);
		void selection(detail::Recorder& recorder);
		void stableSelection(detail::Recorder& recorder);
		void stableCycleSort(detail::Recorder& recorder);
		void improvedBlockSelectionSort(detail::Recorder& recorder);
		void pancake(detail::Recorder& recorder);
		void arrayvBurntPancake(detail::Recorder& recorder);
		void canonicalBurntPancake(detail::Recorder& recorder);
		void arrayvPancakeInsertion(detail::Recorder& recorder);
		void swaplessBubble(detail::Recorder& recorder);
		void unoptimizedBubble(detail::Recorder& recorder);
		void unoptimizedCocktail(detail::Recorder& recorder);

		void classicThreeSmoothComb(detail::Recorder& recorder);
		void threeSmoothCombIterative(detail::Recorder& recorder);
		void recursiveThreeSmooth(detail::Recorder& recorder, bool parallel);
		void recursiveThreeSmoothSequential(detail::Recorder& recorder);
		void recursiveThreeSmoothParallel(detail::Recorder& recorder);
		void bitonicIterative(detail::Recorder& recorder);
		void bitonicRecursiveSort(detail::Recorder& recorder, std::size_t first, std::size_t length, bool ascendingLayer);
		void bitonicRecursive(detail::Recorder& recorder);
		void bitonicParallel(detail::Recorder& recorder);
		void boseNelsonIterative(detail::Recorder& recorder);
		void boseNelsonSortRecursive(detail::Recorder& recorder, std::size_t first, std::size_t length, bool parallelSchedule);
		void boseNelsonRecursive(detail::Recorder& recorder);
		void boseNelsonParallel(detail::Recorder& recorder);
		void oddEvenMergeIterative(detail::Recorder& recorder);
		void oddEvenMergeRecursive(detail::Recorder& recorder, bool parallelSchedule);
		void oddEvenMergeRecursiveDefault(detail::Recorder& recorder);
		void oddEvenMergeParallel(detail::Recorder& recorder);
		void diamondIterative(detail::Recorder& recorder);
		void diamondRecursiveVariant(detail::Recorder& recorder);
		void pairwiseIterative(detail::Recorder& recorder);
		void pairwiseRecursive(detail::Recorder& recorder, std::size_t first, std::size_t end, std::size_t gap);
		void pairwiseRecursiveDefault(detail::Recorder& recorder);
		void creaseSort(detail::Recorder& recorder);
		void foldSort(detail::Recorder& recorder);
		void mergeExchangeIterative(detail::Recorder& recorder);
		void pairwiseMergeSort(detail::Recorder& recorder);
		void pairwiseMergeIterative(detail::Recorder& recorder);
		void weaveIterative(detail::Recorder& recorder);
		void weaveParallel(detail::Recorder& recorder);
		void weaveRecursive(detail::Recorder& recorder);
		void matrixSort(detail::Recorder& recorder);

		void counting(detail::Recorder& recorder);
		void radix(detail::Recorder& recorder);
		void americanFlag(detail::Recorder& recorder);
		void binaryQuickIterative(detail::Recorder& recorder);
		void binaryQuickRecursive(detail::Recorder& recorder);
		void stacklessBinaryRadix(detail::Recorder& recorder);
		void stacklessAmericanFlag(detail::Recorder& recorder);
		void flash(detail::Recorder& recorder);
		void pigeonhole(detail::Recorder& recorder);
		void indexSort(detail::Recorder& recorder);
		void inPlaceLsdRadix(detail::Recorder& recorder);
		void rotateLsdRadix(detail::Recorder& recorder);
		void rotateMsdRadix(detail::Recorder& recorder);
		void msdRadix(detail::Recorder& recorder);
		void gravity(detail::Recorder& recorder);
		void classicGravity(detail::Recorder& recorder);
		void simplisticGravity(detail::Recorder& recorder);
		void shatterSort(detail::Recorder& recorder);
		void simpleShatterSort(detail::Recorder& recorder);
		void staticSort(detail::Recorder& recorder);
		void timeSort(detail::Recorder& recorder);
	}
}
