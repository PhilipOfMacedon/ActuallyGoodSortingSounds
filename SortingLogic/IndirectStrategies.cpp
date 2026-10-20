#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::require;

		namespace
		{
			void stablePermutationSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const std::string indexLane = "stable_permutation_indices";
				r.declareMetric("index_permutation_swaps");
				r.declareMetric("permutations_examined");
				r.resizeCounters(indexLane, count);
				for (std::size_t i = 0; i < count; ++i) r.writeCounter(indexLane, i, static_cast<int>(i));
				const auto isSorted = [&r, count]()
				{
					r.addMetric("permutations_examined");
					for (std::size_t i = 1; i < count; ++i)
					{
						const SortItem previous = r.at(i - 1), current = r.at(i);
						r.compare(previous, current, i - 1, i);
						if (compareItems(previous, current, r.trace.direction) == ComparisonResult::Greater) return false;
					}
					return true;
				};
				std::function<bool(std::size_t)> permute;
				permute = [&r, &permute, &isSorted, &indexLane](std::size_t length)
				{
					if (length < 2) return isSorted();
					for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(length) - 2; i >= 0; --i)
					{
						if (permute(length - 1)) return true;
						const std::size_t first = static_cast<std::size_t>(i);
						const std::size_t second = length - 1;
						const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, first));
						const std::size_t secondIndex = static_cast<std::size_t>(r.counter(indexLane, second));
						r.swap(firstIndex, secondIndex);
						const int firstValue = r.counter(indexLane, first);
						const int secondValue = r.counter(indexLane, second);
						r.writeCounter(indexLane, first, secondValue);
						r.writeCounter(indexLane, second, firstValue);
						r.addMetric("index_permutation_swaps");
					}
					if (permute(length - 1)) return true;
					const int tail = r.counter(indexLane, length - 1);
					for (std::size_t i = length - 1; i > 0; --i)
						r.writeCounter(indexLane, i, r.counter(indexLane, i - 1));
					r.writeCounter(indexLane, 0, tail);
					const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, 0));
					r.hold(firstIndex);
					for (std::size_t i = 1; i < length; ++i)
					{
						const std::size_t destination = static_cast<std::size_t>(r.counter(indexLane, i - 1));
						const std::size_t source = static_cast<std::size_t>(r.counter(indexLane, i));
						r.shift(source, destination);
					}
					const std::size_t finalIndex = static_cast<std::size_t>(r.counter(indexLane, length - 1));
					r.place(finalIndex, r.state.heldItem);
					r.addMetric("permutations_examined");
					return false;
				};
				permute(count);
				r.resizeCounters(indexLane, 0);
			}

			void tableSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const std::string tableLane = "table_indices";
				const char* metricKeys[] = { "partitions", "maximum_depth", "table_index_swaps", "permutation_cycles", "permutation_cycle_moves" };
				for (std::size_t metric = 0; metric < sizeof(metricKeys) / sizeof(metricKeys[0]); ++metric)
					r.declareMetric(metricKeys[metric]);
				r.resizeCounters(tableLane, count);
				for (std::size_t i = 0; i < count; ++i) r.writeCounter(tableLane, i, static_cast<int>(i));
				const auto stableGreater = [&r, &tableLane](std::size_t first, std::size_t second)
				{
					const std::size_t firstIndex = static_cast<std::size_t>(r.counter(tableLane, first));
					const std::size_t secondIndex = static_cast<std::size_t>(r.counter(tableLane, second));
					const SortItem a = r.at(firstIndex), b = r.at(secondIndex);
					r.compare(a, b, firstIndex, secondIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
					const ComparisonResult order = compareItems(a, b, r.trace.direction);
					return order == ComparisonResult::Greater ||
						(order == ComparisonResult::Equal && firstIndex > secondIndex);
				};
				const auto tableSwap = [&r, &tableLane](std::size_t first, std::size_t second)
				{
					if (first == second) return;
					const int firstValue = r.counter(tableLane, first), secondValue = r.counter(tableLane, second);
					r.writeCounter(tableLane, first, secondValue);
					r.writeCounter(tableLane, second, firstValue);
					r.addMetric("table_index_swaps");
				};
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange, &stableGreater, &tableSwap, &tableLane](std::size_t first, std::size_t end, std::size_t depth)
				{
					if (end - first < 2) return;
					r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
					if (end - first < 3)
					{
						if (stableGreater(first, first + 1)) tableSwap(first, first + 1);
						return;
					}
					r.addMetric("partitions");
					const std::size_t begin = r.trace.events.size();
					const std::size_t middle = first + (end - 1 - first) / 2;
					if (stableGreater(first, middle)) tableSwap(first, middle);
					bool medianIsReady = false;
					if (stableGreater(middle, end - 1))
					{
						tableSwap(middle, end - 1);
						medianIsReady = stableGreater(first, middle);
					}
					if (!medianIsReady) tableSwap(first, middle);
					const std::size_t pivot = first;
					std::size_t left = first + 1, right = end;
					for (;;)
					{
						while (left < right && !stableGreater(left, pivot)) ++left;
						if (right == 0) break;
						--right;
						while (right >= left && stableGreater(right, pivot))
						{
							if (right == 0) break;
							--right;
						}
						if (left < right) tableSwap(left++, right);
						else break;
					}
					tableSwap(first, right);
					const std::size_t split = right;
					r.annotate(tableLane, RegionKind::Contiguous, first, end, 1, -1, "Indirect table partition", begin);
					if (split > first) sortRange(first, split, depth + 1);
					if (split + 1 < end) sortRange(split + 1, end, depth + 1);
				};
				if (count > 1) sortRange(0, count, 1);
				std::set<int> tablePermutation;
				for (std::size_t i = 0; i < count; ++i)
				{
					const int mapped = r.counter(tableLane, i);
					if (mapped < 0 || static_cast<std::size_t>(mapped) >= count || !tablePermutation.insert(mapped).second)
						throw std::logic_error("Table Sort produced a non-permutation index table.");
				}
				for (std::size_t first = 0; first < count; ++first)
				{
					const int mapped = r.counter(tableLane, first);
					if (mapped == static_cast<int>(first)) continue;
					const SortItem held = r.at(first);
					r.hold(first);
					std::size_t current = first;
					std::size_t next = static_cast<std::size_t>(mapped);
					std::vector<std::size_t> cyclePositions(1, first);
					do
					{
						r.shift(next, current);
						current = next;
						cyclePositions.push_back(current);
						require(cyclePositions.size() <= count, "Table Sort encountered a non-closing permutation cycle.");
						next = static_cast<std::size_t>(r.counter(tableLane, current));
					} while (next != first);
					r.place(current, held);
					for (std::size_t move = 0; move < cyclePositions.size(); ++move)
						r.writeCounter(tableLane, cyclePositions[move], static_cast<int>(cyclePositions[move]));
					r.addMetric("permutation_cycles");
					r.addMetric("permutation_cycle_moves", cyclePositions.size() - 1);
				}
				r.resizeCounters(tableLane, 0);
			}
		}

		void stablePermutationSortStrategy(Recorder& r) { stablePermutationSort(r); }
		void tableSortStrategy(Recorder& r) { tableSort(r); }
	}
}
