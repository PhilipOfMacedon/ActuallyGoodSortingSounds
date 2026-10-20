#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		namespace
		{
			void rotateMergeSortInternal(Recorder& r, bool parallelSchedule)
			{
				const std::size_t count = r.state.slots.size();
				if (count < 2) return;
				r.declareMetric("merge_passes");
				r.declareMetric("rotate_merge_ranges");
				r.declareMetric("rotate_block_swaps");
				r.declareMetric("rotate_item_swaps");
				r.declareMetric("binary_search_comparisons");
				if (parallelSchedule)
				{
					r.declareMetric("parallel_sort_tasks");
					r.declareMetric("parallel_merge_tasks");
					r.declareMetric("maximum_merge_depth");
				}

				const auto swapBlocks = [&r](std::size_t first, std::size_t second, std::size_t length)
				{
					for (std::size_t offset = 0; offset < length; ++offset)
					{
						r.chargeWork();
						r.swap(first + offset, second + offset, SemanticRole::WriteTarget);
						r.addMetric("rotate_item_swaps");
					}
					r.addMetric("rotate_block_swaps");
				};

				const auto rotate = [&r, &swapBlocks](std::size_t first, std::size_t middle, std::size_t last)
				{
					std::size_t leftLength = middle - first;
					std::size_t rightLength = last - middle;
					while (leftLength != 0 && rightLength != 0)
					{
						r.chargeWork();
						if (leftLength == rightLength)
						{
							swapBlocks(first, middle, leftLength);
							return;
						}
						if (leftLength < rightLength)
						{
							swapBlocks(first, last - leftLength, leftLength);
							last -= leftLength;
							rightLength -= leftLength;
						}
						else
						{
							swapBlocks(first, middle, rightLength);
							first += rightLength;
							leftLength -= rightLength;
						}
					}
				};

				std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> rotateMerge;
				rotateMerge = [&r, &rotate, &rotateMerge, parallelSchedule](std::size_t first, std::size_t middle,
					std::size_t last, std::size_t depth)
				{
					r.chargeWork();
					if (parallelSchedule)
						r.setMetric("maximum_merge_depth", (std::max)(r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
					if (first >= middle || middle >= last) return;
					if (depth > r.state.slots.size() * 2 + 2)
						throw std::logic_error("Rotate Merge recursion exceeded its bounded range depth.");
					if (last - first == 2)
					{
						const SortItem left = r.at(first), right = r.at(middle);
						r.compare(left, right, first, middle);
						r.addMetric("binary_search_comparisons");
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
						{
							r.swap(first, middle, SemanticRole::WriteTarget);
							r.addMetric("rotate_item_swaps");
						}
						return;
					}

					const std::size_t leftLength = middle - first;
					const std::size_t rightLength = last - middle;
					std::size_t firstCut, secondCut;
					if (leftLength >= rightLength)
					{
						firstCut = first + leftLength / 2;
						const SortItem pivot = r.at(firstCut);
						std::size_t low = middle, high = last;
						while (low < high)
						{
							r.chargeWork();
							const std::size_t probeIndex = low + (high - low) / 2;
							const SortItem probe = r.at(probeIndex);
							r.compare(pivot, probe, firstCut, probeIndex, "main", "main",
								SemanticRole::Pivot, SemanticRole::ComparedItem);
							r.addMetric("binary_search_comparisons");
							if (compareItems(pivot, probe, r.trace.direction) == ComparisonResult::Greater) low = probeIndex + 1;
							else high = probeIndex;
						}
						secondCut = low;
					}
					else
					{
						secondCut = middle + rightLength / 2;
						const SortItem pivot = r.at(secondCut);
						std::size_t low = first, high = middle;
						while (low < high)
						{
							r.chargeWork();
							const std::size_t probeIndex = low + (high - low) / 2;
							const SortItem probe = r.at(probeIndex);
							r.compare(probe, pivot, probeIndex, secondCut, "main", "main",
								SemanticRole::ComparedItem, SemanticRole::Pivot);
							r.addMetric("binary_search_comparisons");
							if (compareItems(probe, pivot, r.trace.direction) == ComparisonResult::Greater) high = probeIndex;
							else low = probeIndex + 1;
						}
						firstCut = low;
					}

					const std::size_t newMiddle = firstCut + (secondCut - middle);
					rotate(firstCut, middle, secondCut);
					if (parallelSchedule)
					{
						const int siblingMerges = r.beginPhase("Parallel rotate-merge sibling tasks");
						r.addMetric("parallel_merge_tasks");
						rotateMerge(first, firstCut, newMiddle, depth + 1);
						rotateMerge(newMiddle, secondCut, last, depth + 1);
						r.endPhase(siblingMerges, "Parallel rotate-merge sibling tasks");
					}
					else
					{
						rotateMerge(first, firstCut, newMiddle, depth + 1);
						rotateMerge(newMiddle, secondCut, last, depth + 1);
					}
					r.addMetric("rotate_merge_ranges");
				};

				if (parallelSchedule)
				{
					std::function<void(std::size_t, std::size_t, std::size_t)> split;
					split = [&r, &split, &rotateMerge](std::size_t first, std::size_t last, std::size_t depth)
					{
						r.chargeWork();
						if (last - first < 2) return;
						const std::size_t middle = first + (last - first) / 2;
						const int siblingSorts = r.beginPhase("Parallel rotate-merge sort tasks");
						r.addMetric("parallel_sort_tasks");
						split(first, middle, depth + 1);
						split(middle, last, depth + 1);
						r.endPhase(siblingSorts, "Parallel rotate-merge sort tasks");
						rotateMerge(first, middle, last, depth);
						r.addMetric("merge_passes");
					};
					split(0, count, 1);
					return;
				}

				for (std::size_t width = 1; width < count; width *= 2)
				{
					const int phase = r.beginPhase("Rotate merge width " + std::to_string(width));
					for (std::size_t first = 0; first < count; first += width * 2)
					{
						r.chargeWork();
						const std::size_t middle = (std::min)(first + width, count);
						const std::size_t last = (std::min)(first + width * 2, count);
						if (first < middle && middle < last)
						{
							const std::size_t annotationBegin = r.trace.events.size();
							rotateMerge(first, middle, last, 0);
							r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
								"In-place rotate merge range", annotationBegin);
						}
					}
					r.endPhase(phase, "Rotate merge width " + std::to_string(width));
					r.addMetric("merge_passes");
					if (width > count / 2) break;
				}
			}
		}

		void rotateMergeStrategy(Recorder& r) { rotateMergeSortInternal(r, false); }
		void parallelRotateMergeStrategy(Recorder& r) { rotateMergeSortInternal(r, true); }
	}
}