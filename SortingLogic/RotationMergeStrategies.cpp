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


			void stacklessRotateMergeSortInternal(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				if (count < 2) return;
				r.declareMetric("merge_passes");
				r.declareMetric("partition_merges");
				r.declareMetric("partition_merge_comparisons");
				r.declareMetric("rotate_block_swaps");
				r.declareMetric("rotate_item_swaps");
				const auto readMain = [&r, count](std::size_t index, const std::string& source) -> SortItem
				{
					if (index >= count)
						throw std::logic_error(std::string("Stackless Rotate Merge ") + source + " read index " +
							std::to_string(index) + " outside primary size " + std::to_string(count) + ".");
					return r.at(index);
				};

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
					std::size_t leftLength = middle - first, rightLength = last - middle;
					while (leftLength != 0 && rightLength != 0)
					{
						r.chargeWork();
						if (rightLength < leftLength)
						{
							swapBlocks(middle - rightLength, middle, rightLength);
							last -= rightLength;
							middle -= rightLength;
							leftLength -= rightLength;
						}
						else
						{
							swapBlocks(first, middle, leftLength);
							first += leftLength;
							middle += leftLength;
							rightLength -= leftLength;
						}
					}
				};

				const auto partitionMerge = [&r, &rotate, &readMain](std::size_t first, std::size_t middle,
					std::size_t last, std::size_t selectedCount)
				{
					std::size_t leftLength = middle - first, rightLength = last - middle;
					if (leftLength == 0 || rightLength == 0 || selectedCount == 0) return;
					if (selectedCount > leftLength + rightLength)
						throw std::logic_error("Stackless Rotate Merge partition exceeds its input range.");
					const std::size_t annotationBegin = r.trace.events.size();
					r.addMetric("partition_merges");
					if (rightLength < leftLength)
					{
						selectedCount = leftLength + rightLength - selectedCount;
						std::size_t low = selectedCount > leftLength ? selectedCount - leftLength : 0;
						std::size_t high = (std::min)(selectedCount, rightLength);
						while (low < high)
						{
							r.chargeWork();
							const std::size_t probe = low + (high - low) / 2;
							const std::size_t leftIndex = middle - (selectedCount - probe);
							const std::size_t rightIndex = last - probe - 1;
							const std::string context = "left partition search [" + std::to_string(first) + ", " +
								std::to_string(middle) + ", " + std::to_string(last) + "; c=" +
								std::to_string(selectedCount) + "; probe=" + std::to_string(probe) + "]";
							const SortItem left = readMain(leftIndex, context), right = readMain(rightIndex, "right partition search");
							r.compare(left, right, leftIndex, rightIndex, "main", "main",
								SemanticRole::ComparedItem, SemanticRole::ProbedItem);
							r.addMetric("partition_merge_comparisons");
							if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) high = probe;
							else low = probe + 1;
						}
						rotate(middle - (selectedCount - low), middle, last - low);
					}
					else
					{
						std::size_t low = selectedCount > rightLength ? selectedCount - rightLength : 0;
						std::size_t high = (std::min)(selectedCount, leftLength);
						while (low < high)
						{
							r.chargeWork();
							const std::size_t probe = low + (high - low) / 2;
							const std::size_t leftIndex = first + probe;
							const std::size_t rightIndex = middle + (selectedCount - probe) - 1;
							const std::string context = "right partition search [" + std::to_string(first) + ", " +
								std::to_string(middle) + ", " + std::to_string(last) + "; c=" +
								std::to_string(selectedCount) + "; probe=" + std::to_string(probe) + "]";
							const SortItem left = readMain(leftIndex, context), right = readMain(rightIndex, "left partition search");
							r.compare(left, right, leftIndex, rightIndex, "main", "main",
								SemanticRole::ComparedItem, SemanticRole::ProbedItem);
							r.addMetric("partition_merge_comparisons");
							if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) high = probe;
							else low = probe + 1;
						}
						rotate(first + low, middle, middle + (selectedCount - low));
					}
					r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
						"Stackless partition merge", annotationBegin);
				};

				const int initialPairs = r.beginPhase("Stackless merge initial pairs");
				for (std::size_t i = 1; i < count; i += 2)
				{
					r.chargeWork();
					const SortItem left = readMain(i - 1, "initial pair"), right = readMain(i, "initial pair");
					r.compare(left, right, i - 1, i);
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(i - 1, i, SemanticRole::WriteTarget);
						r.addMetric("rotate_item_swaps");
					}
				}
				r.endPhase(initialPairs, "Stackless merge initial pairs");

			for (std::size_t width = 2; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Stackless partition merge width " + std::to_string(width));
				std::size_t lastCovered = 0;
				for (std::size_t first = 0; first + width < count; first += width * 2)
				{
					r.chargeWork();
					lastCovered = (std::min)(first + width * 2, count);
					partitionMerge(first, first + width, lastCovered, width);
				}
				for (std::size_t subWidth = width / 2; subWidth > 1; subWidth /= 2)
					for (std::size_t first = 0; first + subWidth < lastCovered; first += subWidth * 2)
					{
						r.chargeWork();
						const std::size_t last = (std::min)(first + subWidth * 2, count);
						std::size_t prefix = first + 1;
						while (prefix < last)
						{
							r.chargeWork();
							const SortItem before = readMain(prefix - 1, "sorted-prefix scan"), after = readMain(prefix, "sorted-prefix scan");
							r.compare(before, after, prefix - 1, prefix);
							if (compareItems(before, after, r.trace.direction) == ComparisonResult::Greater) break;
							++prefix;
						}
						if (prefix < last) partitionMerge(first, prefix, last, subWidth);
					}
				for (std::size_t i = 1; i < lastCovered; i += 2)
				{
					r.chargeWork();
					const SortItem left = readMain(i - 1, "pass pair"), right = readMain(i, "pass pair");
					r.compare(left, right, i - 1, i);
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(i - 1, i, SemanticRole::WriteTarget);
						r.addMetric("rotate_item_swaps");
					}
				}
				r.endPhase(phase, "Stackless partition merge width " + std::to_string(width));
				r.addMetric("merge_passes");
				if (width > count / 2) break;
			}
			}
		}

		void stacklessRotateMergeStrategy(Recorder& r) { stacklessRotateMergeSortInternal(r); }
		void rotateMergeStrategy(Recorder& r) { rotateMergeSortInternal(r, false); }
		void parallelRotateMergeStrategy(Recorder& r) { rotateMergeSortInternal(r, true); }
	}
}