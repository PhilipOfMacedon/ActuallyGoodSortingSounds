#include "AlgorithmStrategies.h"
#include "SortingLogic.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace
	{
		using detail::Recorder;
		void classicThreeSmoothComb(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::vector<std::size_t> gaps;
			for (std::size_t gap = n; gap > 1; --gap)
			{
				std::size_t remaining = gap;
				while (remaining % 6 == 0) remaining /= 6;
				while (remaining % 3 == 0) remaining /= 3;
				while (remaining % 2 == 0) remaining /= 2;
				if (remaining == 1) gaps.push_back(gap);
			}
			if (n > 1) gaps.push_back(1);
			for (std::size_t g = 0; g < gaps.size(); ++g)
			{
				const std::size_t gap = gaps[g];
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				bool changed;
				do
				{
					changed = false;
					for (std::size_t i = gap; i < n; ++i)
					{
						const SortItem a = r.at(i - gap), b = r.at(i);
						r.compare(a, b, i - gap, i);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i - gap, i); changed = true; }
					}
				} while (changed);
				r.annotate("main", RegionKind::Strided, 0, n, gap, -1,
					"3-smooth gap " + std::to_string(gap), begin);
			}
		}

		void threeSmoothCombIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t largestPowerOfTwo = 1;
			while (largestPowerOfTwo <= (n - 1) / 2) largestPowerOfTwo *= 2;
			for (std::size_t powerOfTwo = largestPowerOfTwo;; powerOfTwo /= 2)
			{
				std::vector<std::size_t> gaps(1, powerOfTwo);
				while (gaps.back() <= n / 3) gaps.push_back(gaps.back() * 3);
				for (std::size_t gapIndex = gaps.size(); gapIndex > 0; --gapIndex)
				{
					const std::size_t gap = gaps[gapIndex - 1];
					if (gap >= n) continue;
					r.addMetric("gap_passes");
					r.setMetric("last_gap", gap);
					const std::string label = "Iterative 3-smooth comb gap " + std::to_string(gap);
					const std::size_t begin = r.trace.events.size();
					const int phase = r.beginPhase(label);
					for (std::size_t i = 0; i + gap < n; ++i)
					{
						const SortItem left = r.at(i), right = r.at(i + gap);
						r.compare(left, right, i, i + gap);
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
							r.swap(i, i + gap);
					}
					r.endPhase(phase, label);
					r.annotate("main", RegionKind::Strided, 0, n, gap, -1, label, begin);
				}
				if (powerOfTwo == 1) break;
			}
		}

		void recursiveThreeSmoothPower(Recorder& r, std::size_t position, std::size_t gap, std::size_t end, bool groupPhases)
		{
			if (position >= end || gap >= end - position) return;
			int group = -1;
			if (groupPhases) group = r.beginPhase("Parallel 3-Smooth residue subgroups");
			recursiveThreeSmoothPower(r, position, gap * 3, end, groupPhases);
			recursiveThreeSmoothPower(r, position + gap, gap * 3, end, groupPhases);
			recursiveThreeSmoothPower(r, position + gap * 2, gap * 3, end, groupPhases);
			if (groupPhases) r.endPhase(group, "Parallel 3-Smooth residue subgroups");
			r.addMetric("gap_passes");
			r.setMetric("last_gap", gap);
			for (std::size_t i = position; i + gap < end; i += gap)
			{
				const SortItem a = r.at(i), b = r.at(i + gap);
				r.compare(a, b, i, i + gap);
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(i, i + gap);
			}
		}

		void recursiveThreeSmoothComb(Recorder& r, std::size_t position, std::size_t gap, std::size_t end, bool parallel)
		{
			if (position >= end || gap >= end - position) return;
			int group = -1;
			if (parallel) group = r.beginPhase("Parallel 3-Smooth binary residue split");
			recursiveThreeSmoothComb(r, position, gap * 2, end, parallel);
			recursiveThreeSmoothComb(r, position + gap, gap * 2, end, parallel);
			if (parallel) r.endPhase(group, "Parallel 3-Smooth binary residue split");
			recursiveThreeSmoothPower(r, position, gap, end, parallel);
		}

		void recursiveThreeSmooth(Recorder& r, bool parallel)
		{
			recursiveThreeSmoothComb(r, 0, 1, r.state.slots.size(), parallel);
		}

		bool networkCompareExchange(Recorder& r, std::size_t first, std::size_t second, bool ascendingLayer)
		{
			if (first >= r.state.slots.size() || second >= r.state.slots.size() || first == second) return false;
			const SortItem a = r.at(first), b = r.at(second);
			r.compare(a, b, first, second);
			const ComparisonResult comparison = compareItems(a, b, r.trace.direction);
			if ((ascendingLayer && comparison == ComparisonResult::Greater) ||
				(!ascendingLayer && comparison == ComparisonResult::Less))
				r.swap(first, second);
			return true;
		}

		std::size_t greatestPowerOfTwoLessThan(std::size_t value)
		{
			if (value < 2) return 0;
			std::size_t power = 1;
			while (power <= (value - 1) / 2) power *= 2;
			return power;
		}

		void bitonicIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t width = 2; width < n * 2; width *= 2)
			{
				const bool shortFinalRun = (((n + width - 1) / width) % 2) != 0;
				for (std::size_t gap = width / 2; gap > 0; gap /= 2)
				{
					const std::string label = "Bitonic layer width " + std::to_string(width) + ", gap " + std::to_string(gap);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t i = 0; i < n; ++i)
					{
						const std::size_t partner = i ^ gap;
						if (partner <= i || partner >= n) continue;
						const bool ascendingLayer = (((i & width) == 0) == shortFinalRun);
						if (networkCompareExchange(r, i, partner, ascendingLayer)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
				if (width > n) break;
			}
		}

		void bitonicRecursiveMerge(Recorder& r, std::size_t first, std::size_t length, bool ascendingLayer)
		{
			if (length < 2) return;
			const std::size_t split = greatestPowerOfTwoLessThan(length);
			const std::string label = "Recursive bitonic merge, span " + std::to_string(length);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t offset = 0; offset < length - split; ++offset)
				if (networkCompareExchange(r, first + offset, first + offset + split, ascendingLayer)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			bitonicRecursiveMerge(r, first, split, ascendingLayer);
			bitonicRecursiveMerge(r, first + split, length - split, ascendingLayer);
		}

		void bitonicRecursiveSort(Recorder& r, std::size_t first, std::size_t length, bool ascendingLayer)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			bitonicRecursiveSort(r, first, leftLength, !ascendingLayer);
			bitonicRecursiveSort(r, first + leftLength, length - leftLength, ascendingLayer);
			bitonicRecursiveMerge(r, first, length, ascendingLayer);
		}

		void bitonicParallelMerge(Recorder& r, std::size_t first, std::size_t middle,
			std::size_t end, std::size_t gap, bool ascendingLayer)
		{
			if (end - first < 2) return;
			const std::size_t length = end - first;
			// ArrayV's threaded kernel uses a fixed half-width network and leaves
			// irregular tails in place. Preserve that schedule for its power-of-two
			// domain; use the generalized bitonic merge for other lengths so every
			// catalog input remains a valid permutation sort.
			if ((length & (length - 1)) != 0)
			{
				bitonicRecursiveMerge(r, first, length, ascendingLayer);
				return;
			}
			const std::string label = "Parallel bitonic compare-exchange gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t i = first; i < middle; ++i)
				if (networkCompareExchange(r, i, i + gap, ascendingLayer)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			const std::size_t nextGap = gap / 2;
			const std::size_t leftMiddle = first + (middle - first) / 2;
			const std::size_t rightMiddle = middle + (end - middle) / 2;
			bitonicParallelMerge(r, first, leftMiddle, middle, nextGap, ascendingLayer);
			bitonicParallelMerge(r, middle, rightMiddle, end, nextGap, ascendingLayer);
		}

		void bitonicParallelSort(Recorder& r, std::size_t first, std::size_t end, bool ascendingLayer)
		{
			const std::size_t length = end - first;
			if (length < 2) return;
			const std::size_t middle = first + length / 2;
			const std::string label = "Parallel bitonic recursive subnetworks";
			const int phase = r.beginPhase(label);
			bitonicParallelSort(r, first, middle, !ascendingLayer);
			bitonicParallelSort(r, middle, end, ascendingLayer);
			r.endPhase(phase, label);
			bitonicParallelMerge(r, first, middle, end, length / 2, ascendingLayer);
		}

		void bitonicParallel(Recorder& r)
		{
			bitonicParallelSort(r, 0, r.state.slots.size(), true);
		}

		void boseNelsonCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			networkCompareExchange(r, first, second, true);
			r.addMetric("network_layers");
			r.addMetric("network_comparators");
			r.endPhase(phase, label);
		}

		void boseNelsonMergeRecursive(Recorder& r, std::size_t firstLeft, std::size_t leftLength,
			std::size_t firstRight, std::size_t rightLength, bool parallelSchedule)
		{
			if (leftLength == 1 && rightLength == 1)
			{
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 1+1 comparator");
			}
			else if (leftLength == 1 && rightLength == 2)
			{
				boseNelsonCompare(r, firstLeft, firstRight + 1, "Bose-Nelson 1+2 outer comparator");
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 1+2 inner comparator");
			}
			else if (leftLength == 2 && rightLength == 1)
			{
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 2+1 first comparator");
				boseNelsonCompare(r, firstLeft + 1, firstRight, "Bose-Nelson 2+1 second comparator");
			}
			else
			{
				const std::size_t leftMiddle = leftLength / 2;
				const std::size_t rightMiddle = leftLength % 2 == 1 ? rightLength / 2 : (rightLength + 1) / 2;
				if (parallelSchedule)
				{
					const std::string label = "Parallel Bose-Nelson merge subnets";
					const int phase = r.beginPhase(label);
					boseNelsonMergeRecursive(r, firstLeft, leftMiddle, firstRight, rightMiddle, true);
					boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
						firstRight + rightMiddle, rightLength - rightMiddle, true);
					r.endPhase(phase, label);
				}
				else
				{
					boseNelsonMergeRecursive(r, firstLeft, leftMiddle, firstRight, rightMiddle, false);
					boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
						firstRight + rightMiddle, rightLength - rightMiddle, false);
				}
				boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
					firstRight, rightMiddle, parallelSchedule);
			}
		}

		void boseNelsonSortRecursive(Recorder& r, std::size_t first, std::size_t length, bool parallelSchedule)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			if (parallelSchedule)
			{
				const std::string label = "Parallel Bose-Nelson sorted halves";
				const int phase = r.beginPhase(label);
				boseNelsonSortRecursive(r, first, leftLength, true);
				boseNelsonSortRecursive(r, first + leftLength, length - leftLength, true);
				r.endPhase(phase, label);
			}
			else
			{
				boseNelsonSortRecursive(r, first, leftLength, false);
				boseNelsonSortRecursive(r, first + leftLength, length - leftLength, false);
			}
			boseNelsonMergeRecursive(r, first, leftLength, first + leftLength, length - leftLength, parallelSchedule);
		}

		void boseNelsonIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t width = 2; width <= paddedLength; width *= 2)
				for (std::size_t offset = 0; offset < width / 2; ++offset)
				{
					const std::string label = "Iterative Bose-Nelson layer width " + std::to_string(width) +
						", offset " + std::to_string(offset);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t start = 0; start + offset < n; start += width)
					{
						const std::size_t half = width / 2;
						const std::size_t leftStart = start + offset;
						for (std::size_t i = 0; i < half - offset; ++i)
							if ((i & ~offset) == i && leftStart + i < n && start + half + i < n &&
								networkCompareExchange(r, leftStart + i, start + half + i, true)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void oddEvenMergeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t width = 1; width < n; width *= 2)
				for (std::size_t stride = width; stride > 0; stride /= 2)
					for (std::size_t blockOffset = stride % width; blockOffset + stride < n; blockOffset += stride * 2)
					{
						const std::string label = "Iterative odd-even merge layer width " + std::to_string(width * 2) +
							", stride " + std::to_string(stride);
						const int phase = r.beginPhase(label);
						std::size_t comparators = 0;
						for (std::size_t offset = 0; offset < stride; ++offset)
						{
							const std::size_t first = blockOffset + offset;
						const std::size_t second = first + stride;
						if (first < n && second < n && first / (width * 2) == second / (width * 2) &&
							networkCompareExchange(r, first, second, true)) ++comparators;
						}
						if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void oddEvenMergeRecursive(Recorder& r, std::size_t first, std::size_t middle,
			std::size_t length, std::size_t gap, bool parallelSchedule)
		{
			const std::size_t stride = gap * 2;
			if (stride < length)
			{
				if ((length / gap) % 2 != 0)
				{
					oddEvenMergeRecursive(r, first, (middle + 1) / 2, length + gap, stride, parallelSchedule);
					oddEvenMergeRecursive(r, first + gap, middle / 2, length - gap, stride, parallelSchedule);
				}
				else
				{
					oddEvenMergeRecursive(r, first, (middle + 1) / 2, length, stride, parallelSchedule);
					oddEvenMergeRecursive(r, first + gap, middle / 2, length, stride, parallelSchedule);
				}
				const std::string label = parallelSchedule ? "Parallel odd-even merge compare layer" : "Recursive odd-even merge compare layer";
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				if (middle % 2 != 0)
				{
					for (std::size_t i = first; i + gap < first + length; i += stride)
						if (networkCompareExchange(r, i, i + gap, true)) ++comparators;
				}
				else
				{
					for (std::size_t i = first + gap; i + gap < first + length; i += stride)
						if (networkCompareExchange(r, i, i + gap, true)) ++comparators;
				}
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
			else if (length > gap)
				boseNelsonCompare(r, first, first + gap, parallelSchedule ? "Parallel odd-even merge base layer" : "Recursive odd-even merge base layer");
		}

		void oddEvenMergeSortRecursive(Recorder& r, std::size_t first, std::size_t length, bool parallelSchedule)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			if (parallelSchedule)
			{
				const std::string label = "Parallel odd-even merge sorted halves";
				const int phase = r.beginPhase(label);
				oddEvenMergeSortRecursive(r, first, leftLength, true);
				oddEvenMergeSortRecursive(r, first + leftLength, length - leftLength, true);
				r.endPhase(phase, label);
			}
			else
			{
				oddEvenMergeSortRecursive(r, first, leftLength, false);
				oddEvenMergeSortRecursive(r, first + leftLength, length - leftLength, false);
			}
			oddEvenMergeRecursive(r, first, leftLength, length, 1, parallelSchedule);
		}

		void oddEvenMergeRecursive(Recorder& r, bool parallelSchedule)
		{
			oddEvenMergeSortRecursive(r, 0, r.state.slots.size(), parallelSchedule);
		}

		void diamondCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			if (networkCompareExchange(r, first, second, true)) r.addMetric("network_comparators");
			r.addMetric("network_layers");
			r.endPhase(phase, label);
		}

		void diamondIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t width = 4; width <= paddedLength; width *= 2)
				for (std::size_t diagonal = 0; diagonal < width / 2; ++diagonal)
				{
					const std::size_t count = diagonal <= width / 4 ? diagonal : width / 2 - diagonal;
					const std::string label = "Iterative diamond layer width " + std::to_string(width) +
						", diagonal " + std::to_string(diagonal);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t block = 0; block < n; block += width)
					{
						if (block + count + 1 >= n) continue;
						const std::size_t limit = std::min(n, block + width - count);
						for (std::size_t i = block + count; i + 1 < limit; i += 2)
							if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
			const std::size_t finalWidth = std::max<std::size_t>(2, paddedLength);
			for (std::size_t diagonal = 0; diagonal <= finalWidth / 2; ++diagonal)
			{
				const std::string label = "Iterative diamond final layer " + std::to_string(diagonal);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				const std::size_t limit = std::min(n, finalWidth - diagonal);
				for (std::size_t i = diagonal; i + 1 < limit; i += 2)
					if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
		}

		void diamondRecursive(Recorder& r, std::size_t first, std::size_t end, bool merge)
		{
			const std::size_t length = end - first;
			if (length == 2)
			{
				diamondCompare(r, first, first + 1, "Recursive diamond pair");
			}
			else if (length >= 3)
			{
				const double quarter = static_cast<double>(length) / 4.0;
				const std::size_t middle = first + length / 2;
				const std::size_t innerFirst = first + static_cast<std::size_t>(quarter);
				const std::size_t innerEnd = first + static_cast<std::size_t>(quarter * 3.0);
				if (merge)
				{
					diamondRecursive(r, first, middle, true);
					diamondRecursive(r, middle, end, true);
				}
				diamondRecursive(r, innerFirst, innerEnd, false);
				diamondRecursive(r, first, middle, false);
				diamondRecursive(r, middle, end, false);
				diamondRecursive(r, innerFirst, innerEnd, false);
			}
		}

		void diamondRecursiveVariant(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n != 0 && (n & (n - 1)) != 0)
			{
				// The recursive quarter split is only closed over power-of-two
				// lengths. Preserve it on that domain and use the clipped Diamond
				// comparator schedule for the source's irregular tail cases.
				diamondIterative(r);
				return;
			}
			diamondRecursive(r, 0, n, true);
		}

		void pairwiseCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			if (networkCompareExchange(r, first, second, true)) r.addMetric("network_comparators");
			r.addMetric("network_layers");
			r.endPhase(phase, label);
		}

		void pairwiseIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t width = 1;
			while (width < n)
			{
				const std::string label = "Iterative pairwise merge width " + std::to_string(width);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				std::size_t b = width, cycle = 0;
				while (b < n)
				{
					if (networkCompareExchange(r, b - width, b, true)) ++comparators;
					cycle = (cycle + 1) % width;
					++b;
					if (cycle == 0) b += width;
				}
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
				width *= 2;
			}
			width /= 4;
			std::size_t b = 0, cycle = 0;
			std::size_t outerWidth = 1;
			while (width > 0)
			{
				std::size_t distance = outerWidth;
				while (distance > 0)
				{
					const std::string label = "Iterative pairwise merge width " + std::to_string(width) +
						", distance " + std::to_string(distance);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					b = (distance + 1) * width;
					cycle = 0;
					while (b < n)
					{
						if (networkCompareExchange(r, b - distance * width, b, true)) ++comparators;
						cycle = (cycle + 1) % width;
						++b;
						if (cycle == 0) b += width;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
					distance /= 2;
				}
				width /= 2;
				outerWidth = outerWidth * 2 + 1;
			}
		}

		void pairwiseRecursive(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (gap == 0 || end <= gap || first == end - gap) return;
			std::size_t b = first + gap;
			const std::string label = "Recursive pairwise adjacent layer, gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			while (b < end)
			{
				if (networkCompareExchange(r, b - gap, b, true)) ++comparators;
				if (gap > (static_cast<std::size_t>(-1) - b) / 2) break;
				b += 2 * gap;
			}
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);

			if (gap > static_cast<std::size_t>(-1) / 2) return;
			const std::size_t nextGap = gap * 2;
			if (((end - first) / gap) % 2 == 0)
			{
				pairwiseRecursive(r, first, end, nextGap);
				if (first <= static_cast<std::size_t>(-1) - gap && end <= static_cast<std::size_t>(-1) - gap)
					pairwiseRecursive(r, first + gap, end + gap, nextGap);
			}
			else
			{
				if (end <= static_cast<std::size_t>(-1) - gap)
					pairwiseRecursive(r, first, end + gap, nextGap);
				pairwiseRecursive(r, first + gap, end, nextGap);
			}

			std::size_t tree = 1;
			while (tree < (end - first) / gap)
			{
				if (tree > (static_cast<std::size_t>(-1) - 1) / 2) break;
				tree = tree * 2 + 1;
			}
			b = first + gap;
			while (b <= end && gap < end - b)
			{
				std::size_t distance = tree;
				while (distance > 1)
				{
					distance /= 2;
					if (distance <= (static_cast<std::size_t>(-1) - b) / gap && b + distance * gap < end)
						pairwiseCompare(r, b, b + distance * gap, "Recursive pairwise far comparator");
				}
				if (gap > (static_cast<std::size_t>(-1) - b) / 2) break;
				b += 2 * gap;
			}
		}

		void creaseSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t maximum = 1;
			while (maximum <= (static_cast<std::size_t>(-1) / 2) && maximum * 2 < n) maximum *= 2;
			for (std::size_t next = maximum; next > 0; next /= 2)
			{
				const std::string adjacentLabel = "Crease adjacent layer, span " + std::to_string(next);
				int phase = r.beginPhase(adjacentLabel);
				std::size_t comparators = 0;
				for (std::size_t i = 0; i + 1 < n; i += 2)
					if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, adjacentLabel);
				for (std::size_t distance = maximum; distance >= next && distance > 1; distance /= 2)
				{
					const std::string label = "Crease fold layer, distance " + std::to_string(distance);
					phase = r.beginPhase(label);
					comparators = 0;
					for (std::size_t i = 1; i + distance - 1 < n; i += 2)
						if (networkCompareExchange(r, i, i + distance - 1, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
			}
		}

		void foldSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 2;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t fold = paddedLength / 2; fold > 0; fold /= 2)
				for (std::size_t width = paddedLength; width >= fold; width /= 2)
				{
					for (std::size_t start = 0; start < n; start += width)
					{
						const std::string label = "Fold halver width " + std::to_string(width) +
							", fold " + std::to_string(fold) + ", block " + std::to_string(start);
						const int phase = r.beginPhase(label);
						std::size_t comparators = 0;
						std::size_t left = start, right = start + width - 1;
						while (left < right)
						{
							if (networkCompareExchange(r, left, right, true)) ++comparators;
							++left;
							--right;
						}
						if (comparators != 0) r.addMetric("network_comparators", comparators);
						r.addMetric("network_layers");
						r.endPhase(phase, label);
					}
					if (width == fold) break;
				}
		}

		void mergeExchangeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t largestPower = 1;
			while (largestPower <= (static_cast<std::size_t>(-1) / 2) && largestPower * 2 <= n - 1) largestPower *= 2;
			for (std::size_t p = largestPower; p > 0; p /= 2)
			{
				std::size_t q = largestPower, remainder = 0, distance = p;
				for (;;)
				{
					const std::string label = "Merge-exchange layer p=" + std::to_string(p) +
						", q=" + std::to_string(q) + ", distance=" + std::to_string(distance);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t i = 0; i + distance < n; ++i)
						if ((i & p) == remainder && networkCompareExchange(r, i, i + distance, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
					if (q == p) break;
					distance = q - p;
					q /= 2;
					remainder = p;
				}
			}
		}

		void pairwiseMergeNetwork(Recorder& r, std::size_t first, std::size_t end)
		{
			const std::size_t middle = (first + end) / 2;
			const std::size_t innerMiddle = (first + middle) / 2;
			const std::size_t gap = middle - innerMiddle;
			for (std::size_t offset = 0; innerMiddle + offset < middle; ++offset)
				for (std::size_t distance = gap, index = innerMiddle; distance > 0; distance /= 2)
				{
					const std::size_t left = index + offset;
					const std::size_t right = left + distance;
					pairwiseCompare(r, left, right, "Pairwise merge comparator distance " + std::to_string(distance));
					const std::size_t nextDistance = distance / 2;
					if (nextDistance != 0) index -= nextDistance - (offset & nextDistance);
				}
			if (end - first > 4) pairwiseMergeNetwork(r, middle, end);
		}

		void pairwiseMergeSortRecursive(Recorder& r, std::size_t first, std::size_t end)
		{
			if (end - first < 2) return;
			const std::size_t middle = (first + end) / 2;
			const std::string label = "Recursive pairwise merge halves";
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t left = first, right = middle; left < middle; ++left, ++right)
				if (networkCompareExchange(r, left, right, true)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			if (end - first > 2)
			{
				pairwiseMergeSortRecursive(r, first, middle);
				pairwiseMergeSortRecursive(r, middle, end);
				pairwiseMergeNetwork(r, first, end);
			}
		}

		void pairwiseMergeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t gap = paddedLength / 2; gap > 0; gap /= 2)
			{
				const std::string label = "Iterative pairwise merge block gap " + std::to_string(gap);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				const std::size_t block = gap * 2;
				for (std::size_t start = 0; start < n; start += block)
					for (std::size_t offset = 0; offset < gap; ++offset)
						if (networkCompareExchange(r, start + offset, start + gap + offset, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
			for (std::size_t width = 2; width < paddedLength; width *= 2)
				for (std::size_t gap = width / 2; gap > 0; gap /= 2)
				{
					const std::string label = "Iterative pairwise merge width " + std::to_string(width) +
						", distance " + std::to_string(gap);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					const std::size_t block = width * 2;
					for (std::size_t start = 0; start < n; start += block)
						for (std::size_t offset = gap; offset < (width - gap) * 2; offset += gap * 2)
							for (std::size_t i = 0; i < gap; ++i)
								if (networkCompareExchange(r, start + offset + i, start + offset + gap + i, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void weaveIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t i = 1; i < paddedLength; i *= 2)
				for (std::size_t j = 1; j <= i; j *= 2)
					for (std::size_t block = 0; block < paddedLength; block += paddedLength / j)
					{
						const std::size_t distance = paddedLength / i / 2;
						std::size_t moving = 0;
						for (std::size_t line = paddedLength / j - distance; line >= paddedLength / j / 2; line -= distance)
						{
							const std::string label = "Iterative weave layer i=" + std::to_string(i) +
								", j=" + std::to_string(j) + ", line=" + std::to_string(line);
							const int phase = r.beginPhase(label);
							std::size_t comparators = 0;
							for (std::size_t p = 0; p < distance; ++p, ++moving)
								if (networkCompareExchange(r, block + moving, block + line + p, true)) ++comparators;
							if (comparators != 0) r.addMetric("network_comparators", comparators);
							r.addMetric("network_layers");
							r.endPhase(phase, label);
							if (line < distance) break;
						}
					}
		}

		void weaveCircle(Recorder& r, std::size_t position, std::size_t length, std::size_t gap)
		{
			if (length < 2 || gap == 0) return;
			weaveCircle(r, position, length / 2, gap * 2);
			weaveCircle(r, position + gap, length / 2, gap * 2);
			const std::string label = "Recursive weave circle length " + std::to_string(length) +
				", gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t i = 0; 2 * i < (length - 1) * gap; i += gap)
				if (networkCompareExchange(r, position + i, position + (length - 1) * gap - i, true)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			weaveCircle(r, position, length / 2, gap);
			const std::size_t rightStart = position + length * gap / 2;
			if (rightStart < r.state.slots.size()) weaveCircle(r, rightStart, length / 2, gap);
		}

		void weaveRecursive(Recorder& r)
		{
			std::size_t paddedLength = 1;
			while (paddedLength < r.state.slots.size()) paddedLength *= 2;
			weaveCircle(r, 0, paddedLength, 1);
		}

		void weaveParallelCircle(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (first >= end || gap == 0 || end - first < gap) return;
			std::size_t left = first, right = end;
			const std::string label = "Parallel weave circle layer gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			while (left < right)
			{
				if (networkCompareExchange(r, left, right, true)) ++comparators;
				left += gap;
				if (right < gap) break;
				right -= gap;
			}
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			if (first < right || left < end)
			{
				const int group = r.beginPhase("Parallel weave independent circle branches");
				if (first < right) weaveParallelCircle(r, first, right, gap);
				if (left < end) weaveParallelCircle(r, left, end, gap);
				r.endPhase(group, "Parallel weave independent circle branches");
			}
		}

		void weaveParallelWrapper(Recorder& r, std::size_t first, std::size_t length, std::size_t gap)
		{
			if (gap >= length || gap == 0 || first >= r.state.slots.size()) return;
			const int group = r.beginPhase("Parallel weave independent residue sorts");
			weaveParallelWrapper(r, first, length, gap * 2);
			weaveParallelWrapper(r, first + gap, length, gap * 2);
			r.endPhase(group, "Parallel weave independent residue sorts");
			const std::size_t end = first + length - gap;
			weaveParallelCircle(r, first, end, gap);
		}

		void weaveParallel(Recorder& r)
		{
			std::size_t paddedLength = 1;
			while (paddedLength < r.state.slots.size()) paddedLength *= 2;
			weaveParallelWrapper(r, 0, paddedLength, 1);
		}

		struct MatrixShape
		{
			std::size_t width;
			bool unbalanced;
			bool insertLast;
		};

		MatrixShape matrixShape(std::size_t length, bool insertLast)
		{
			std::size_t dimension = static_cast<std::size_t>(std::sqrt(static_cast<double>(length)));
			if (dimension * dimension == length - 1) insertLast = true;
			while (dimension > 1 && length % dimension != 0) --dimension;
			const std::size_t height = length / dimension;
			const bool unbalanced = (dimension == 1) != (height == 1);
			return MatrixShape{ dimension, unbalanced, unbalanced || insertLast };
		}

		bool matrixInsertLast(Recorder& r, std::size_t first, std::size_t last, std::size_t gap, bool ascending)
		{
			if (last <= first || last >= r.state.slots.size()) return false;
			const SortItem key = r.at(last);
			r.hold(last);
			std::ptrdiff_t cursor = static_cast<std::ptrdiff_t>(last) - static_cast<std::ptrdiff_t>(gap);
			bool moved = false;
			while (cursor >= static_cast<std::ptrdiff_t>(first))
			{
				const std::size_t index = static_cast<std::size_t>(cursor);
				const SortItem previous = r.at(index);
				r.compare(key, previous, last, index, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				const ComparisonResult result = compareItems(key, previous, SortDirection::Ascending);
				if ((ascending && result != ComparisonResult::Less) || (!ascending && result != ComparisonResult::Greater)) break;
				r.shift(index, index + gap);
				moved = true;
				cursor -= static_cast<std::ptrdiff_t>(gap);
			}
			r.place(static_cast<std::size_t>(cursor + static_cast<std::ptrdiff_t>(gap)), key);
			return moved;
		}

		void matrixGapReverse(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (gap == 0) return;
			const std::string label = "Matrix row reversal, stride " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			for (std::size_t left = first, right = end; left < right; left += gap)
			{
				if (right < gap) break;
				const std::size_t other = right - gap;
				if (left >= r.state.slots.size() || other >= r.state.slots.size()) break;
				if (left >= other) break;
				r.swap(left, other);
				right = other;
			}
			r.endPhase(phase, label);
		}

		bool matrixSort(Recorder& r, std::size_t first, std::size_t end, std::size_t gap,
			bool ascending, std::size_t depth)
		{
			if (gap == 0 || end <= first || depth > r.state.slots.size() + 2) return false;
			const std::size_t length = (end - first) / gap;
			if (length < 2) return false;
			if (length <= 16)
			{
				bool changed = false;
				for (std::size_t index = first + gap; index < end; index += gap)
					changed = matrixInsertLast(r, first, index, gap, ascending) || changed;
				return changed;
			}

			const MatrixShape shape = matrixShape(length, false);
			const std::size_t rowSpan = shape.width * gap;
			if (shape.insertLast)
			{
				const bool sortedPrefix = matrixSort(r, first, end - gap, gap, ascending, depth + 1);
				const bool inserted = matrixInsertLast(r, first, end - gap, gap, ascending);
				return sortedPrefix || inserted;
			}

			for (std::size_t row = first + rowSpan; row < end; row += rowSpan * 2)
				matrixGapReverse(r, row, row + rowSpan, gap);

			bool changed = false;
			bool columnChanged = false;
			do
			{
				bool rowChanged = false;
				bool rowAscending = ascending;
				for (std::size_t row = first; row < end; row += rowSpan)
				{
					const bool current = matrixSort(r, row, row + rowSpan, gap, rowAscending, depth + 1);
					rowChanged = current || rowChanged;
					changed = changed || rowChanged;
					rowAscending = !rowAscending;
				}

				columnChanged = false;
				for (std::size_t column = 0; column < shape.width; ++column)
				{
					const std::size_t columnStart = first + column * gap;
					const bool current = matrixSort(r, columnStart, end + column * gap, rowSpan, ascending, depth + 1);
					columnChanged = current || columnChanged;
					changed = changed || columnChanged;
				}
				r.addMetric("matrix_interleave_passes");
			} while (columnChanged);

			for (std::size_t row = first + rowSpan; row < end; row += rowSpan * 2)
				matrixGapReverse(r, row, row + rowSpan, gap);
			return changed;
		}

		void matrixSort(Recorder& r)
		{
			matrixSort(r, 0, r.state.slots.size(), 1, r.trace.direction == SortDirection::Ascending, 0);
		}




	}

	namespace strategies
	{
		void classicThreeSmoothComb(detail::Recorder& recorder) { sortinglogic::classicThreeSmoothComb(recorder); }
		void threeSmoothCombIterative(detail::Recorder& recorder) { sortinglogic::threeSmoothCombIterative(recorder); }
		void recursiveThreeSmooth(detail::Recorder& recorder, bool parallel) { sortinglogic::recursiveThreeSmooth(recorder, parallel); }
		void bitonicIterative(detail::Recorder& recorder) { sortinglogic::bitonicIterative(recorder); }
		void bitonicRecursiveSort(detail::Recorder& recorder, std::size_t first, std::size_t length, bool ascendingLayer)
		{ sortinglogic::bitonicRecursiveSort(recorder, first, length, ascendingLayer); }
		void bitonicParallel(detail::Recorder& recorder) { sortinglogic::bitonicParallel(recorder); }
		void boseNelsonIterative(detail::Recorder& recorder) { sortinglogic::boseNelsonIterative(recorder); }
		void boseNelsonSortRecursive(detail::Recorder& recorder, std::size_t first, std::size_t length, bool parallelSchedule)
		{ sortinglogic::boseNelsonSortRecursive(recorder, first, length, parallelSchedule); }
		void oddEvenMergeIterative(detail::Recorder& recorder) { sortinglogic::oddEvenMergeIterative(recorder); }
		void oddEvenMergeRecursive(detail::Recorder& recorder, bool parallelSchedule)
		{ sortinglogic::oddEvenMergeRecursive(recorder, parallelSchedule); }
		void diamondIterative(detail::Recorder& recorder) { sortinglogic::diamondIterative(recorder); }
		void diamondRecursiveVariant(detail::Recorder& recorder) { sortinglogic::diamondRecursiveVariant(recorder); }
		void pairwiseIterative(detail::Recorder& recorder) { sortinglogic::pairwiseIterative(recorder); }
		void pairwiseRecursive(detail::Recorder& recorder, std::size_t first, std::size_t end, std::size_t gap)
		{ sortinglogic::pairwiseRecursive(recorder, first, end, gap); }
		void creaseSort(detail::Recorder& recorder) { sortinglogic::creaseSort(recorder); }
		void foldSort(detail::Recorder& recorder) { sortinglogic::foldSort(recorder); }
		void mergeExchangeIterative(detail::Recorder& recorder) { sortinglogic::mergeExchangeIterative(recorder); }
		void pairwiseMergeSort(detail::Recorder& recorder)
		{
			std::size_t paddedLength = 1;
			while (paddedLength < recorder.state.slots.size()) paddedLength *= 2;
			sortinglogic::pairwiseMergeSortRecursive(recorder, 0, paddedLength);
		}
		void pairwiseMergeIterative(detail::Recorder& recorder) { sortinglogic::pairwiseMergeIterative(recorder); }
		void weaveIterative(detail::Recorder& recorder) { sortinglogic::weaveIterative(recorder); }
		void weaveParallel(detail::Recorder& recorder) { sortinglogic::weaveParallel(recorder); }
		void weaveRecursive(detail::Recorder& recorder) { sortinglogic::weaveRecursive(recorder); }
		void matrixSort(detail::Recorder& recorder) { sortinglogic::matrixSort(recorder); }
	}
}