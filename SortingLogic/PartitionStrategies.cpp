#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		namespace
		{
			void quickSort(Recorder& r)
			{
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange](std::size_t low, std::size_t high, std::size_t depth)
				{
					if (low >= high) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(
						r.state.metrics.algorithm["maximum_depth"], static_cast<std::uint64_t>(depth)));
					const std::size_t begin = r.trace.events.size();
					const std::size_t pivotIndex = low + (high - low) / 2;
					const SortItem pivot = r.at(pivotIndex);
					r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
					std::ptrdiff_t i = static_cast<std::ptrdiff_t>(low) - 1;
					std::ptrdiff_t j = static_cast<std::ptrdiff_t>(high) + 1;
					while (true)
					{
						do { ++i; const SortItem item = r.at(static_cast<std::size_t>(i));
							r.compare(item, pivot, static_cast<std::size_t>(i), pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						} while (compareItems(r.at(static_cast<std::size_t>(i)), pivot, r.trace.direction) == ComparisonResult::Less);
						do { --j; const SortItem item = r.at(static_cast<std::size_t>(j));
							r.compare(item, pivot, static_cast<std::size_t>(j), pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						} while (compareItems(r.at(static_cast<std::size_t>(j)), pivot, r.trace.direction) == ComparisonResult::Greater);
						if (i >= j) break;
						r.swap(static_cast<std::size_t>(i), static_cast<std::size_t>(j), SemanticRole::Pivot);
					}
					const std::size_t split = static_cast<std::size_t>(j);
					r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1, "Quick partition", begin);
					if (split > low) sortRange(low, split, depth + 1);
					if (split + 1 < high) sortRange(split + 1, high, depth + 1);
				};
				if (!r.state.slots.empty()) sortRange(0, r.state.slots.size() - 1, 1);
			}

			void leftLeftQuickSort(Recorder& r)
			{
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange](std::size_t low, std::size_t high, std::size_t depth)
				{
					if (low >= high) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
					const std::size_t begin = r.trace.events.size();
					const SortItem pivot = r.at(high);
					std::size_t boundary = low;
					for (std::size_t scan = low; scan < high; ++scan)
					{
						const SortItem item = r.at(scan);
						r.compare(item, pivot, scan, high, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot, r.trace.direction) == ComparisonResult::Less)
							r.swap(boundary++, scan, SemanticRole::Pivot);
					}
					r.swap(boundary, high, SemanticRole::Pivot);
					r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1, "Left-left quick partition", begin);
					if (boundary > low) sortRange(low, boundary - 1, depth + 1);
					if (boundary < high) sortRange(boundary + 1, high, depth + 1);
				};
				if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size() - 1, 1);
			}

			void leftRightQuickSort(Recorder& r, bool parallelSchedule)
			{
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, parallelSchedule](std::size_t low, std::size_t high, std::size_t depth)
				{
					if (low >= high) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
					const std::size_t begin = r.trace.events.size();
					const std::size_t pivotIndex = low + (high - low + 1) / 2;
					const SortItem pivot = r.at(pivotIndex);
					std::ptrdiff_t left = static_cast<std::ptrdiff_t>(low);
					std::ptrdiff_t right = static_cast<std::ptrdiff_t>(high);
					while (left <= right)
					{
						while (left <= right)
						{
							const std::size_t index = static_cast<std::size_t>(left);
							const SortItem item = r.at(index);
							r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(item, pivot, r.trace.direction) != ComparisonResult::Less) break;
							++left;
						}
						while (left <= right)
						{
							const std::size_t index = static_cast<std::size_t>(right);
							const SortItem item = r.at(index);
							r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(item, pivot, r.trace.direction) != ComparisonResult::Greater) break;
							--right;
						}
						if (left <= right)
						{
							r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(right), SemanticRole::Pivot);
							++left;
							--right;
						}
					}
					r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1,
						parallelSchedule ? "Parallel left-right quick partition" : "Left-right quick partition", begin);
					int phase = -1;
					if (parallelSchedule) phase = r.beginPhase("Parallel left-right quick subranges");
					if (static_cast<std::ptrdiff_t>(low) < right) sortRange(low, static_cast<std::size_t>(right), depth + 1);
					if (left < static_cast<std::ptrdiff_t>(high)) sortRange(static_cast<std::size_t>(left), high, depth + 1);
					if (parallelSchedule) r.endPhase(phase, "Parallel left-right quick subranges");
				};
				if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size() - 1, 1);
			}

			void dualPivotQuickSort(Recorder& r, bool optimized)
			{
				const std::size_t insertionThreshold = optimized ? 27 : 4;
				std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, optimized, insertionThreshold](std::size_t first, std::size_t end,
					std::size_t divisor, std::size_t depth)
				{
					if (end - first < 2) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					const std::size_t length = end - first;
					if (length <= insertionThreshold)
					{
						r.addMetric("insertion_fallback_ranges");
						for (std::size_t i = first + 1; i < end; ++i)
							for (std::size_t j = i; j > first; --j)
							{
								const SortItem left = r.at(j - 1), right = r.at(j);
								r.compare(left, right, j - 1, j, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
								if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
								r.swap(j - 1, j, SemanticRole::ComparedItem);
							}
						return;
					}
					const std::size_t begin = r.trace.events.size();
					r.addMetric("partitions");
					const std::size_t third = length / divisor;
					std::size_t firstSample = first + third;
					std::size_t secondSample = end - 1 - third;
					if (firstSample <= first) firstSample = first + 1;
					if (secondSample >= end - 1) secondSample = end - 2;
					const SortItem firstSampleItem = r.at(firstSample), secondSampleItem = r.at(secondSample);
					r.compare(firstSampleItem, secondSampleItem, firstSample, secondSample, "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ComparedItem);
					if (compareItems(firstSampleItem, secondSampleItem, r.trace.direction) == ComparisonResult::Less)
					{
						r.swap(firstSample, first, SemanticRole::Pivot);
						r.swap(secondSample, end - 1, SemanticRole::Pivot);
					}
					else
					{
						r.swap(firstSample, end - 1, SemanticRole::Pivot);
						r.swap(secondSample, first, SemanticRole::Pivot);
					}
					const SortItem pivot1 = r.at(first), pivot2 = r.at(end - 1);
					std::size_t less = first + 1;
					std::size_t great = end - 2;
					for (std::size_t scan = less; scan <= great; ++scan)
					{
						SortItem item = r.at(scan);
						r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Less)
						{
							r.swap(scan, less++, SemanticRole::Pivot);
							continue;
						}
						r.compare(item, pivot2, scan, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot2, r.trace.direction) != ComparisonResult::Greater) continue;
						while (scan < great)
						{
							const SortItem right = r.at(great);
							r.compare(right, pivot2, great, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(right, pivot2, r.trace.direction) != ComparisonResult::Greater) break;
							--great;
						}
						r.swap(scan, great, SemanticRole::Pivot);
						if (great > 0) --great;
						item = r.at(scan);
						r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Less)
							r.swap(scan, less++, SemanticRole::Pivot);
						if (scan == std::numeric_limits<std::size_t>::max()) break;
					}
					const std::size_t distance = great >= less ? great - less : 0;
					if (distance < 13) ++divisor;
					r.swap(less - 1, first, SemanticRole::Pivot);
					r.swap(great + 1, end - 1, SemanticRole::Pivot);
					const std::size_t middleFirst = less;
					std::size_t middleEnd = great + 1;
					r.annotate("main", RegionKind::Contiguous, first, less - 1, 1, -1, "Below first pivot", begin);
					r.annotate("main", RegionKind::Contiguous, less, middleEnd, 1, -1, "Between pivots", begin);
					r.annotate("main", RegionKind::Contiguous, great + 2, end, 1, -1, "Above second pivot", begin);
					if (!optimized)
					{
						sortRange(first, less - 1, divisor, depth + 1);
						if (compareItems(pivot1, pivot2, r.trace.direction) == ComparisonResult::Less)
							sortRange(middleFirst, middleEnd, divisor, depth + 1);
						sortRange(great + 2, end, divisor, depth + 1);
						return;
					}
					sortRange(first, less - 1, divisor, depth + 1);
					sortRange(great + 2, end, divisor, depth + 1);
					if (distance > length - std::min<std::size_t>(length, 13) &&
						compareItems(pivot1, pivot2, r.trace.direction) != ComparisonResult::Equal)
					{
						for (std::size_t scan = less; scan < middleEnd; ++scan)
						{
							const SortItem item = r.at(scan);
							r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Equal)
							{
								r.swap(scan, less++, SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
								continue;
							}
							r.compare(item, pivot2, scan, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(item, pivot2, r.trace.direction) == ComparisonResult::Equal)
							{
								r.swap(scan, --middleEnd, SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
								const SortItem moved = r.at(scan);
								r.compare(moved, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
								if (compareItems(moved, pivot1, r.trace.direction) == ComparisonResult::Equal)
								{
									r.swap(scan, less++, SemanticRole::Pivot);
									r.addMetric("equal_key_moves");
								}
							}
						}
					}
					r.annotate("main", RegionKind::Contiguous, less, middleEnd, 1, -1, "Equal-key middle region", r.trace.events.size());
					if (compareItems(pivot1, pivot2, r.trace.direction) == ComparisonResult::Less)
						sortRange(less, middleEnd, divisor, depth + 1);
				};
				if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 3, 1);
			}

			void stableQuickSort(Recorder& r, bool parallelSchedule)
			{
				std::mt19937 random(r.trace.inputSeed ^ (parallelSchedule ? 0x57AB1E00u : 0x57AB1E01u));
				std::map<std::uint64_t, std::size_t> originalOrder;
				for (std::size_t i = 0; i < r.trace.input.size(); ++i) originalOrder[r.trace.input[i].stableId] = i;
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, &random, &originalOrder, parallelSchedule](std::size_t first, std::size_t end, std::size_t depth)
				{
					if (end - first < 2) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					r.addMetric("partitions");
					const std::size_t begin = r.trace.events.size();
					std::uniform_int_distribution<std::size_t> choose(first, end - 1);
					const std::size_t pivotIndex = parallelSchedule ? choose(random) : first;
					const SortItem pivot = r.at(pivotIndex);
					const std::string leftLane = "stable_quick_left";
					const std::string rightLane = "stable_quick_right";
					std::vector<SortItem> left, right, ordered;
					left.reserve(end - first);
					right.reserve(end - first);
					for (std::size_t index = first; index < end; ++index)
					{
						if (index == pivotIndex) continue;
						const SortItem item = r.at(index);
						r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						ComparisonResult order = compareItems(item, pivot, r.trace.direction);
						if (parallelSchedule && order == ComparisonResult::Equal)
						{
							r.addMetric("stable_tie_breaks");
							order = originalOrder[item.stableId] < originalOrder[pivot.stableId]
								? ComparisonResult::Less : ComparisonResult::Greater;
						}
						if (order == ComparisonResult::Less)
						{
							left.push_back(item);
							r.writeAuxiliary(leftLane, left.size() - 1, item);
						}
						else
						{
							right.push_back(item);
							r.writeAuxiliary(rightLane, right.size() - 1, item);
						}
					}
					ordered.reserve(end - first);
					ordered.insert(ordered.end(), left.begin(), left.end());
					ordered.push_back(pivot);
					ordered.insert(ordered.end(), right.begin(), right.end());
					r.writeMainRange(first, ordered);
					r.resizeAuxiliary(leftLane, 0);
					r.resizeAuxiliary(rightLane, 0);
					const std::size_t pivotPosition = first + left.size();
					r.annotate("main", RegionKind::Contiguous, first, pivotPosition, 1, -1, "Stable quick left partition", begin);
					r.annotate("main", RegionKind::Contiguous, pivotPosition, pivotPosition + 1, 1, -1, "Stable quick pivot", begin);
					r.annotate("main", RegionKind::Contiguous, pivotPosition + 1, end, 1, -1, "Stable quick right partition", begin);
					if (parallelSchedule)
					{
						const int phase = r.beginPhase("Stable quick recursive branches");
						sortRange(first, pivotPosition, depth + 1);
						sortRange(pivotPosition + 1, end, depth + 1);
						r.endPhase(phase, "Stable quick recursive branches complete");
					}
					else
					{
						sortRange(first, pivotPosition, depth + 1);
						sortRange(pivotPosition + 1, end, depth + 1);
					}
				};
				if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
			}

			void forcedStableQuickSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const std::string orderLane = "forced_stable_order";
				r.resizeCounters(orderLane, count);
				for (std::size_t i = 0; i < count; ++i) r.writeCounter(orderLane, i, static_cast<int>(i));
				const auto stableGreater = [&r, &orderLane](std::size_t first, std::size_t second)
				{
					const SortItem a = r.at(first), b = r.at(second);
					r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					const ComparisonResult valueOrder = compareItems(a, b, r.trace.direction);
					if (valueOrder != ComparisonResult::Equal) return valueOrder == ComparisonResult::Greater;
					r.addMetric("stable_tie_breaks");
					return r.counter(orderLane, first) > r.counter(orderLane, second);
				};
				const auto stableSwap = [&r, &orderLane](std::size_t first, std::size_t second)
				{
					if (first == second) return;
					const int firstOrder = r.counter(orderLane, first), secondOrder = r.counter(orderLane, second);
					r.swap(first, second, SemanticRole::Pivot);
					r.writeCounter(orderLane, first, secondOrder);
					r.writeCounter(orderLane, second, firstOrder);
				};
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, &stableGreater, &stableSwap](std::size_t first, std::size_t end, std::size_t depth)
				{
					if (end - first < 2) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					if (end - first < 3)
					{
						if (stableGreater(first, first + 1)) stableSwap(first, first + 1);
						return;
					}
					r.addMetric("partitions");
					const std::size_t middle = first + (end - 1 - first) / 2;
					if (stableGreater(first, middle)) stableSwap(first, middle);
					bool medianIsReady = false;
					if (stableGreater(middle, end - 1))
					{
						stableSwap(middle, end - 1);
						medianIsReady = stableGreater(first, middle);
					}
					if (!medianIsReady) stableSwap(first, middle);
					const std::size_t pivot = first;
					std::size_t left = first;
					std::size_t right = end;
					std::size_t split = first;
					while (true)
					{
						++left;
						while (left < right && !stableGreater(left, pivot)) ++left;
						--right;
						while (right >= left && stableGreater(right, pivot)) --right;
						if (left < right) stableSwap(left, right);
						else { split = right; break; }
					}
					stableSwap(first, split);
					r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Forced-stable quick partition", r.trace.events.size());
					if (split > first) sortRange(first, split, depth + 1);
					if (split + 1 < end) sortRange(split + 1, end, depth + 1);
				};
				if (count > 1) sortRange(0, count, 1);
				r.resizeCounters(orderLane, 0);
			}

			void ternaryQuickSort(Recorder& r, bool leftLeftPartition)
			{
				const auto compareIndices = [&r](std::size_t first, std::size_t second)
				{
					const SortItem a = r.at(first), b = r.at(second);
					r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					return compareItems(a, b, r.trace.direction);
				};
				const auto selectPivot = [&compareIndices](std::size_t first, std::size_t end)
				{
					const std::size_t middle = first + (end - first) / 2;
					const std::size_t last = end - 1;
					if (compareIndices(first, middle) == ComparisonResult::Equal) return first;
					if (compareIndices(first, last) == ComparisonResult::Equal ||
						compareIndices(middle, last) == ComparisonResult::Equal) return last;
					return compareIndices(first, middle) == ComparisonResult::Less
						? (compareIndices(middle, last) == ComparisonResult::Less ? middle :
							(compareIndices(first, last) == ComparisonResult::Less ? last : first))
						: (compareIndices(middle, last) == ComparisonResult::Greater ? middle :
							(compareIndices(first, last) == ComparisonResult::Less ? first : last));
				};
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, &compareIndices, &selectPivot, leftLeftPartition](std::size_t first, std::size_t end, std::size_t depth)
				{
					if (end - first < 2) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(
						r.state.metrics.algorithm["maximum_depth"], static_cast<std::uint64_t>(depth)));
					const std::size_t begin = r.trace.events.size();
					r.addMetric("partitions");
					const std::size_t pivotIndex = selectPivot(first, end);
					const SortItem pivot = r.at(pivotIndex);
					r.swap(pivotIndex, end - 1, SemanticRole::Pivot);
					std::size_t leftEnd = first, rightBegin = first;
					if (leftLeftPartition)
					{
						std::size_t left = first, equalStart = end - 1;
						std::size_t current = first;
						while (current < equalStart)
						{
							const SortItem item = r.at(current);
							r.compare(item, pivot, current, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
							if (order == ComparisonResult::Equal)
							{
								--equalStart;
								r.swap(equalStart, current, SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
							}
							else
						{
							if (order == ComparisonResult::Less) r.swap(left++, current, SemanticRole::Pivot);
							++current;
						}
						}
						leftEnd = left;
						const std::size_t equalCount = end - equalStart;
						for (std::size_t offset = 0; offset < equalCount; ++offset)
							r.swap(left + offset, end - 1 - offset, SemanticRole::Pivot);
						rightBegin = left + equalCount;
					}
					else
					{
						std::ptrdiff_t left = static_cast<std::ptrdiff_t>(first);
						std::ptrdiff_t right = static_cast<std::ptrdiff_t>(end) - 2;
						std::ptrdiff_t equalLeft = left, equalRight = right;
						const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(end) - 1;
						for (;;)
						{
							while (left <= right)
							{
								const std::size_t index = static_cast<std::size_t>(left);
								const SortItem item = r.at(index);
								r.compare(item, pivot, index, static_cast<std::size_t>(last), "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
								const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
								if (order == ComparisonResult::Greater) break;
								if (order == ComparisonResult::Equal)
								{
									r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(equalLeft++), SemanticRole::Pivot);
									r.addMetric("equal_key_moves");
								}
								++left;
							}
							while (left <= right)
							{
								const std::size_t index = static_cast<std::size_t>(right);
								const SortItem item = r.at(index);
								r.compare(item, pivot, index, static_cast<std::size_t>(last), "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
								const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
								if (order == ComparisonResult::Less) break;
								if (order == ComparisonResult::Equal)
								{
									r.swap(static_cast<std::size_t>(right), static_cast<std::size_t>(equalRight--), SemanticRole::Pivot);
									r.addMetric("equal_key_moves");
								}
								--right;
							}
							if (left > right) break;
							r.swap(static_cast<std::size_t>(left++), static_cast<std::size_t>(right--), SemanticRole::Pivot);
						}
						r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(last), SemanticRole::Pivot);
						const std::ptrdiff_t lessCount = left - equalLeft;
						const std::ptrdiff_t greaterCount = equalRight - right;
						std::ptrdiff_t leftCursor = left - 1, rightCursor = left + 1;
						const std::ptrdiff_t equalPrefixEnd = static_cast<std::ptrdiff_t>(first) +
							std::min(equalLeft - static_cast<std::ptrdiff_t>(first), lessCount);
						for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(first); k < equalPrefixEnd; ++k, --leftCursor)
							r.swap(static_cast<std::size_t>(k), static_cast<std::size_t>(leftCursor), SemanticRole::Pivot);
						const std::ptrdiff_t equalSuffixStart = last - 1 - std::min(last - 1 - equalRight, greaterCount - 1);
						for (std::ptrdiff_t k = last - 1; k > equalSuffixStart; --k, ++rightCursor)
							r.swap(static_cast<std::size_t>(rightCursor), static_cast<std::size_t>(k), SemanticRole::Pivot);
						leftEnd = first + static_cast<std::size_t>(lessCount);
						rightBegin = end - static_cast<std::size_t>(greaterCount);
					}
					r.annotate("main", RegionKind::Contiguous, first, leftEnd, 1, -1, "Ternary quick less-than partition", begin);
					r.annotate("main", RegionKind::Contiguous, leftEnd, rightBegin, 1, -1, "Ternary quick pivot-equal region", begin);
					r.annotate("main", RegionKind::Contiguous, rightBegin, end, 1, -1, "Ternary quick greater-than partition", begin);
					if (leftEnd > first) sortRange(first, leftEnd, depth + 1);
					if (end > rightBegin) sortRange(rightBegin, end, depth + 1);
				};
				if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
			}
		}

		void quick(Recorder& r) { quickSort(r); }
		void leftLeftQuick(Recorder& r) { leftLeftQuickSort(r); }
		void leftRightQuick(Recorder& r) { leftRightQuickSort(r, false); }
		void leftRightQuickParallel(Recorder& r) { leftRightQuickSort(r, true); }
		void dualPivotQuick(Recorder& r) { dualPivotQuickSort(r, false); }
		void optimizedDualPivotQuick(Recorder& r) { dualPivotQuickSort(r, true); }
		void stableQuick(Recorder& r) { stableQuickSort(r, false); }
		void stableQuickParallel(Recorder& r) { stableQuickSort(r, true); }
		void forcedStableQuick(Recorder& r) { forcedStableQuickSort(r); }
		void ternaryLeftLeftQuick(Recorder& r) { ternaryQuickSort(r, true); }
		void ternaryLeftRightQuick(Recorder& r) { ternaryQuickSort(r, false); }
	}
}
