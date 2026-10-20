#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <functional>
#include <string>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void insertion(Recorder& r)
		{
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t position = i;
				while (position > 0)
				{
					const SortItem previous = r.at(position - 1);
					r.compare(previous, key, position - 1, position, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
					if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
					r.shift(position - 1, position);
					--position;
				}
				r.place(position, key);
			}
		}

		void binaryInsertionRange(Recorder& r, std::size_t first, std::size_t end)
		{
			for (std::size_t i = first + (first < end ? 1 : 0); i < end; ++i)
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
		}

		void binaryInsertion(Recorder& r)
		{
			binaryInsertionRange(r, 0, r.state.slots.size());
		}

		void blockInsertionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("block_insertion_runs");
			r.declareMetric("block_insertion_run_reversals");
			r.declareMetric("block_insertion_short_insertions");
			r.declareMetric("block_insertion_merges");
			r.declareMetric("block_insertion_comparisons");
			r.declareMetric("block_insertion_rotations");
			r.declareMetric("block_insertion_rotation_swaps");
			if (count < 2) return;

			const auto compareAt = [&r](std::size_t left, std::size_t right) -> ComparisonResult
			{
				const SortItem a = r.at(left), b = r.at(right);
				r.compare(a, b, left, right);
				r.addMetric("block_insertion_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last, int phase)
			{
				if (first == middle || middle == last) return;
				const std::size_t eventBegin = r.trace.events.size();
				const auto reverse = [&r](std::size_t begin, std::size_t end)
				{
					while (begin < end)
					{
						r.chargeWork();
						r.swap(begin++, --end, SemanticRole::WriteTarget);
						r.addMetric("block_insertion_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.addMetric("block_insertion_rotations");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Block inserted into sorted prefix", eventBegin);
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> mergeRun;
			mergeRun = [&r, &compareAt, &rotate, &mergeRun, count](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Block Insertion merge exceeded its bounded recursion depth at [" +
					std::to_string(first) + "," + std::to_string(middle) + "," + std::to_string(last) + ").");
				if (compareAt(middle - 1, middle) != ComparisonResult::Greater) return;
				if (last - first == 2)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_merges");
					return;
				}

				const int phase = r.beginPhase("Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
				std::size_t firstCut = first, secondCut = middle;
				if (middle - first > last - middle)
				{
					firstCut = first + (middle - first) / 2;
					const SortItem pivot = r.at(firstCut);
					std::size_t low = middle, high = last;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, firstCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
						else high = probe;
					}
					secondCut = low;
				}
				else
				{
					secondCut = middle + (last - middle) / 2;
					const SortItem pivot = r.at(secondCut);
					std::size_t low = first, high = middle;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, secondCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
						else high = probe;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut, phase);
				r.addMetric("block_insertion_merges");
				mergeRun(first, firstCut, newMiddle, depth + 1);
				mergeRun(newMiddle, secondCut, last, depth + 1);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Merged monotone block", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
				r.endPhase(phase, "Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
			};

			const auto insertShortRun = [&r](std::size_t keyIndex)
			{
				const SortItem key = r.at(keyIndex);
				r.hold(keyIndex);
				std::size_t low = 0, high = keyIndex;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = keyIndex; position > low; --position) r.shift(position - 1, position);
				r.place(low, key);
				r.addMetric("block_insertion_short_insertions");
			};

			std::size_t runStart = 0;
			bool firstRun = true;
			while (runStart < count)
			{
				std::size_t runEnd = runStart + 1;
				bool descending = false;
				if (runEnd < count)
				{
					descending = compareAt(runStart, runEnd) == ComparisonResult::Greater;
					++runEnd;
					while (runEnd < count)
					{
						const ComparisonResult order = compareAt(runEnd - 1, runEnd);
						if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
						r.chargeWork();
						++runEnd;
					}
				}

				if (descending)
				{
					const std::size_t eventBegin = r.trace.events.size();
					for (std::size_t left = runStart, right = runEnd - 1; left < right; ++left, --right)
						r.swap(left, right, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_run_reversals");
					r.annotate("main", RegionKind::Contiguous, runStart, runEnd, 1, -1,
						"Reversed strict monotone run", eventBegin);
				}
				r.addMetric("block_insertion_runs");

				if (firstRun)
				{
					firstRun = false;
				}
				else if (runEnd - runStart < 3)
				{
					for (std::size_t index = runStart; index < runEnd; ++index)
						insertShortRun(index);
				}
				else
				{
					const int phase = r.beginPhase("Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
					mergeRun(0, runStart, runEnd, 0);
					r.annotate("main", RegionKind::Contiguous, 0, runEnd, 1, phase,
						"Sorted prefix grows by run merge", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
					r.endPhase(phase, "Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
				}
				runStart = runEnd;
			}
		}
	}
}
