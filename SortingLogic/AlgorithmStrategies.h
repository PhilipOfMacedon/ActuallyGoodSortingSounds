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
		void bingo(detail::Recorder& recorder);
		void bubble(detail::Recorder& recorder);
		void comb(detail::Recorder& recorder);
		void cocktail(detail::Recorder& recorder);
		void doubleSelection(detail::Recorder& recorder);
		void gnome(detail::Recorder& recorder);
		void insertion(detail::Recorder& recorder);
		void oddEven(detail::Recorder& recorder);
		void optimizedBubble(detail::Recorder& recorder);
		void optimizedCocktail(detail::Recorder& recorder);
		void selection(detail::Recorder& recorder);
		void stableSelection(detail::Recorder& recorder);
		void swaplessBubble(detail::Recorder& recorder);
		void unoptimizedBubble(detail::Recorder& recorder);
		void unoptimizedCocktail(detail::Recorder& recorder);

		void classicThreeSmoothComb(detail::Recorder& recorder);
		void threeSmoothCombIterative(detail::Recorder& recorder);
		void recursiveThreeSmooth(detail::Recorder& recorder, bool parallel);
		void bitonicIterative(detail::Recorder& recorder);
		void bitonicRecursiveSort(detail::Recorder& recorder, std::size_t first, std::size_t length, bool ascendingLayer);
		void bitonicParallel(detail::Recorder& recorder);
		void boseNelsonIterative(detail::Recorder& recorder);
		void boseNelsonSortRecursive(detail::Recorder& recorder, std::size_t first, std::size_t length, bool parallelSchedule);
		void oddEvenMergeIterative(detail::Recorder& recorder);
		void oddEvenMergeRecursive(detail::Recorder& recorder, bool parallelSchedule);
		void diamondIterative(detail::Recorder& recorder);
		void diamondRecursiveVariant(detail::Recorder& recorder);
		void pairwiseIterative(detail::Recorder& recorder);
		void pairwiseRecursive(detail::Recorder& recorder, std::size_t first, std::size_t end, std::size_t gap);
		void creaseSort(detail::Recorder& recorder);
		void foldSort(detail::Recorder& recorder);
		void mergeExchangeIterative(detail::Recorder& recorder);
		void pairwiseMergeSort(detail::Recorder& recorder);
		void pairwiseMergeIterative(detail::Recorder& recorder);
		void weaveIterative(detail::Recorder& recorder);
		void weaveParallel(detail::Recorder& recorder);
		void weaveRecursive(detail::Recorder& recorder);
		void matrixSort(detail::Recorder& recorder);
	}
}
