#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		namespace
		{
			void stacklessHybridQuickSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const char* metricKeys[] = { "stackless_hybrid_maximum_scans", "stackless_hybrid_maximum_items_parked",
					"stackless_hybrid_partitions", "stackless_hybrid_partition_comparisons", "stackless_hybrid_insertion_runs",
					"stackless_hybrid_duplicate_skips", "stackless_hybrid_max_active_range" };
				for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
				if (count < 2) return;
				const auto later = [&r](const SortItem& first, const SortItem& second)
					{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
				const auto less = [&r](const SortItem& first, const SortItem& second)
					{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Less; };
				std::size_t maximum = 0;
				for (std::size_t i = 1; i < count; ++i)
				{
					r.compare(r.at(i), r.at(maximum), i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					r.addMetric("stackless_hybrid_maximum_scans");
					if (later(r.at(i), r.at(maximum))) maximum = i;
				}
				const SortItem maximumItem = r.at(maximum);
				std::size_t end = count;
				for (std::size_t cursor = count; cursor > 0; --cursor)
				{
					const std::size_t index = cursor - 1;
					r.compare(r.at(index), maximumItem, index, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(r.at(index), maximumItem, r.trace.direction) == ComparisonResult::Equal)
					{
						--end;
						r.swap(index, end, SemanticRole::CurrentMaximum);
						r.addMetric("stackless_hybrid_maximum_items_parked");
					}
				}

				const auto medianOfThree = [&r, &later](std::size_t first, std::size_t rangeEnd)
				{
					const std::size_t middle = first + (rangeEnd - 1 - first) / 2;
					const std::size_t last = rangeEnd - 1;
					r.compare(r.at(first), r.at(middle), first, middle);
					if (later(r.at(first), r.at(middle))) r.swap(first, middle, SemanticRole::Pivot);
					r.compare(r.at(middle), r.at(last), middle, last);
					if (later(r.at(middle), r.at(last)))
					{
						r.swap(middle, last, SemanticRole::Pivot);
						r.compare(r.at(first), r.at(middle), first, middle);
						if (later(r.at(first), r.at(middle))) return;
					}
					r.swap(first, middle, SemanticRole::Pivot);
				};
				const auto partition = [&r, &less](std::size_t first, std::size_t rangeEnd)
				{
					std::ptrdiff_t scan = static_cast<std::ptrdiff_t>(first);
					std::ptrdiff_t tail = static_cast<std::ptrdiff_t>(rangeEnd);
					for (;;)
					{
						++scan;
						while (scan < tail)
						{
							r.compare(r.at(static_cast<std::size_t>(scan)), r.at(first), static_cast<std::size_t>(scan), first,
								"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							r.addMetric("stackless_hybrid_partition_comparisons");
							if (!less(r.at(static_cast<std::size_t>(scan)), r.at(first))) break;
							++scan;
						}
						--tail;
						while (tail >= scan)
						{
							r.compare(r.at(static_cast<std::size_t>(tail)), r.at(first), static_cast<std::size_t>(tail), first,
								"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							r.addMetric("stackless_hybrid_partition_comparisons");
							if (less(r.at(static_cast<std::size_t>(tail)), r.at(first))) break;
							--tail;
						}
						if (scan < tail) r.swap(static_cast<std::size_t>(scan), static_cast<std::size_t>(tail));
						else
						{
							r.swap(first, static_cast<std::size_t>(tail), SemanticRole::Pivot);
							return static_cast<std::size_t>(tail);
						}
					}
				};
				const auto insertionRange = [&r](std::size_t first, std::size_t rangeEnd)
				{
					for (std::size_t i = first + (first < rangeEnd ? 1u : 0u); i < rangeEnd; ++i)
					{
						const SortItem key = r.at(i);
						r.hold(i);
						std::size_t low = first, high = i;
						while (low < high)
						{
							const std::size_t middle = low + (high - low) / 2;
							const SortItem candidate = r.at(middle);
							r.compare(key, candidate, i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
							if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
							else low = middle + 1;
						}
						for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
						r.place(low, key);
					}
				};
				const auto lowerBound = [&r](std::size_t first, std::size_t rangeEnd, std::size_t pivotIndex)
				{
					const SortItem pivot = r.at(pivotIndex);
					std::size_t low = first, high = rangeEnd;
					while (low < high)
					{
						const std::size_t middle = low + (high - low) / 2;
						r.compare(pivot, r.at(middle), pivotIndex, middle, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
						if (compareItems(pivot, r.at(middle), r.trace.direction) != ComparisonResult::Greater) high = middle;
						else low = middle + 1;
					}
					return low;
				};

				std::size_t first = 0, partitionEnd = end;
				bool selectMedian = true;
				for (;;)
				{
					r.setMetric("stackless_hybrid_max_active_range", std::max<std::uint64_t>(r.state.metrics.algorithm["stackless_hybrid_max_active_range"], partitionEnd - first));
					while (partitionEnd - first > 16)
					{
						if (selectMedian) medianOfThree(first, partitionEnd);
						const int phase = r.beginPhase("Stackless Hoare partition");
						const std::size_t pivot = partition(first, partitionEnd);
						r.swap(pivot, end, SemanticRole::Pivot);
						partitionEnd = pivot;
						r.endPhase(phase, "Pivot parked beside the maximum suffix");
						r.addMetric("stackless_hybrid_partitions");
					}
					insertionRange(first, partitionEnd);
					r.addMetric("stackless_hybrid_insertion_runs");
					first = partitionEnd + 1;
					if (first >= end)
					{
						if (first - 1 < end) r.swap(first - 1, end, SemanticRole::Pivot);
						return;
					}
					partitionEnd = lowerBound(first, end, first - 1);
					r.swap(first - 1, end, SemanticRole::Pivot);
					selectMedian = true;
					while (first < partitionEnd)
					{
						r.compare(r.at(first - 1), r.at(first), first - 1, first, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
						if (compareItems(r.at(first - 1), r.at(first), r.trace.direction) != ComparisonResult::Equal) break;
						selectMedian = false;
						++first;
						r.addMetric("stackless_hybrid_duplicate_skips");
					}
					if (first == partitionEnd) selectMedian = true;
				}
			}

			void stacklessDualPivotQuickSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const char* metricKeys[] = { "stackless_dual_maximum_scans", "stackless_dual_maximum_items_parked",
					"stackless_dual_partitions", "stackless_dual_partition_comparisons", "stackless_dual_insertion_runs",
					"stackless_dual_duplicate_skips", "stackless_dual_max_active_range" };
				for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
				if (count < 2) return;
				const auto later = [&r](const SortItem& first, const SortItem& second)
					{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
				const auto less = [&r](const SortItem& first, const SortItem& second)
					{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Less; };
				std::size_t maximum = 0;
				for (std::size_t i = 1; i < count; ++i)
				{
					r.compare(r.at(i), r.at(maximum), i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					r.addMetric("stackless_dual_maximum_scans");
					if (later(r.at(i), r.at(maximum))) maximum = i;
				}
				const SortItem maximumItem = r.at(maximum);
				std::size_t boundary = count;
				for (std::size_t cursor = count; cursor > 0; --cursor)
				{
					const std::size_t index = cursor - 1;
					r.compare(r.at(index), maximumItem, index, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(r.at(index), maximumItem, r.trace.direction) == ComparisonResult::Equal)
					{
						--boundary;
						r.swap(index, boundary, SemanticRole::CurrentMaximum);
						r.addMetric("stackless_dual_maximum_items_parked");
					}
				}
				const auto insertionRange = [&r](std::size_t first, std::size_t end)
				{
					for (std::size_t i = first + (first < end ? 1u : 0u); i < end; ++i)
					{
						const SortItem key = r.at(i);
						r.hold(i);
						std::size_t low = first, high = i;
						while (low < high)
						{
							const std::size_t middle = low + (high - low) / 2;
							r.compare(key, r.at(middle), i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
							if (compareItems(key, r.at(middle), r.trace.direction) == ComparisonResult::Less) high = middle;
							else low = middle + 1;
						}
						for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
						r.place(low, key);
					}
				};
				const auto lowerBound = [&r](std::size_t first, std::size_t end, std::size_t pivotIndex)
				{
					const SortItem pivot = r.at(pivotIndex);
					std::size_t low = first, high = end;
					while (low < high)
					{
						const std::size_t middle = low + (high - low) / 2;
						r.compare(pivot, r.at(middle), pivotIndex, middle, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
						if (compareItems(pivot, r.at(middle), r.trace.direction) != ComparisonResult::Greater) high = middle;
						else low = middle + 1;
					}
					return low;
				};
				const auto partition = [&r, &less, &later](std::size_t first, std::size_t rangeEnd, std::size_t maximumBoundary)
				{
					std::size_t firstSample = first + (rangeEnd - first) / 3;
					std::size_t secondSample = first + 2 * (rangeEnd - first) / 3;
					std::size_t lastPivot = rangeEnd;
					r.compare(r.at(firstSample), r.at(secondSample), firstSample, secondSample);
					if (later(r.at(firstSample), r.at(secondSample)))
					{
						r.swap(firstSample, first, SemanticRole::Pivot);
						r.swap(secondSample, --lastPivot, SemanticRole::Pivot);
					}
					else
					{
						r.swap(secondSample, first, SemanticRole::Pivot);
						r.swap(firstSample, --lastPivot, SemanticRole::Pivot);
					}
					std::size_t left = first, right = lastPivot;
					for (std::size_t scan = left + 1; scan < right; ++scan)
					{
						r.compare(r.at(scan), r.at(lastPivot), scan, lastPivot, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("stackless_dual_partition_comparisons");
						if (less(r.at(scan), r.at(lastPivot)))
						{
							r.swap(scan, ++left);
							continue;
						}
						r.compare(r.at(scan), r.at(first), scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("stackless_dual_partition_comparisons");
						if (!less(r.at(scan), r.at(first)))
						{
							do
							{
								--right;
								if (right <= scan) break;
								r.compare(r.at(right), r.at(first), right, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
								r.addMetric("stackless_dual_partition_comparisons");
							}
							while (!less(r.at(right), r.at(first)));
							r.swap(scan, right);
							r.compare(r.at(scan), r.at(lastPivot), scan, lastPivot, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							r.addMetric("stackless_dual_partition_comparisons");
							if (less(r.at(scan), r.at(lastPivot))) r.swap(scan, ++left);
						}
					}
					r.swap(first, left, SemanticRole::Pivot);
					r.swap(maximumBoundary, lastPivot, SemanticRole::Pivot);
					r.swap(lastPivot, right);
					return left;
				};

				std::size_t first = 0, partitionEnd = boundary;
				bool medianMode = true;
				for (;;)
				{
					r.setMetric("stackless_dual_max_active_range", std::max<std::uint64_t>(r.state.metrics.algorithm["stackless_dual_max_active_range"], partitionEnd - first));
					while (partitionEnd - first > 24)
					{
						if (!medianMode) r.swap(first, first + (partitionEnd - first) / 3, SemanticRole::Pivot);
						const int phase = r.beginPhase("Stackless dual-pivot partition");
						const std::size_t pivot = partition(first, partitionEnd, boundary);
						partitionEnd = pivot;
						r.endPhase(phase, "Dual-pivot partition complete");
						r.addMetric("stackless_dual_partitions");
					}
					insertionRange(first, partitionEnd);
					r.addMetric("stackless_dual_insertion_runs");
					first = partitionEnd + 1;
					if (first >= boundary)
					{
						if (first - 1 < boundary) r.swap(first - 1, boundary, SemanticRole::Pivot);
						return;
					}
					partitionEnd = lowerBound(first, boundary, first - 1);
					r.swap(first - 1, boundary, SemanticRole::Pivot);
					medianMode = true;
					while (first < partitionEnd)
					{
						r.compare(r.at(first - 1), r.at(first), first - 1, first, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
						if (compareItems(r.at(first - 1), r.at(first), r.trace.direction) != ComparisonResult::Equal) break;
						medianMode = false;
						++first;
						r.addMetric("stackless_dual_duplicate_skips");
					}
					if (first == partitionEnd) medianMode = true;
				}
			}
		}

		void stacklessHybridQuick(Recorder& r) { stacklessHybridQuickSort(r); }
		void stacklessDualPivotQuick(Recorder& r) { stacklessDualPivotQuickSort(r); }
	}
}
