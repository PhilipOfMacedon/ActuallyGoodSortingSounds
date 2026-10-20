#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		void shell(Recorder& r)
		{
			const std::vector<std::size_t> gaps = shellGaps(r.state.slots.size(), r.trace.configuration.shellGapSequence);
			for (std::size_t g = 0; g < gaps.size(); ++g)
			{
				const std::size_t gap = gaps[g];
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				for (std::size_t i = gap; i < r.state.slots.size(); ++i)
				{
					const SortItem key = r.at(i);
					r.hold(i);
					std::size_t position = i;
					while (position >= gap)
					{
						const SortItem previous = r.at(position - gap);
						r.compare(previous, key, position - gap, position, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
						r.shift(position - gap, position);
						position -= gap;
					}
					r.place(position, key);
				}
				r.annotate("main", RegionKind::Strided, 0, r.state.slots.size(), gap, -1,
					"Shell gap " + std::to_string(gap), begin);
			}
		}

		static std::size_t doubleInsertionBound(Recorder& r, const SortItem& key, std::size_t first,
			std::size_t end, bool upperBound, bool binarySearch, std::size_t keyIndex)
		{
			if (binarySearch)
			{
				std::size_t low = first, high = end;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					const ComparisonResult order = compareItems(key, candidate, r.trace.direction);
					if (order == ComparisonResult::Less || (!upperBound && order == ComparisonResult::Equal)) high = middle;
					else low = middle + 1;
				}
				return low;
			}

			std::size_t position = first;
			while (position < end)
			{
				const SortItem candidate = r.at(position);
				r.compare(candidate, key, position, keyIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
				const ComparisonResult order = compareItems(candidate, key, r.trace.direction);
				if (upperBound ? order == ComparisonResult::Greater : order != ComparisonResult::Less) break;
				++position;
			}
			return position;
		}

		static void insertDoubleKeyFromLeft(Recorder& r, std::size_t source, std::size_t end,
			bool binarySearch)
		{
			const SortItem key = r.at(source);
			r.hold(source);
			const std::size_t targetBoundary = doubleInsertionBound(r, key, source + 1, end, false, binarySearch, source);
			const std::size_t target = targetBoundary - 1;
			for (std::size_t position = source; position < target; ++position)
				r.shift(position + 1, position);
			r.place(target, key);
		}

		static void insertDoubleKeyFromRight(Recorder& r, std::size_t source, std::size_t first,
			bool binarySearch)
		{
			const SortItem key = r.at(source);
			r.hold(source);
			const std::size_t target = doubleInsertionBound(r, key, first, source, true, binarySearch, source);
			for (std::size_t position = source; position > target; --position)
				r.shift(position - 1, position);
			r.place(target, key);
		}

		static void doubleInsertion(Recorder& r, bool binarySearch)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;

			std::size_t left = count / 2 - 1;
			std::size_t right = left + 1;
			const SortItem centerLeft = r.at(left), centerRight = r.at(right);
			r.compare(centerLeft, centerRight, left, right);
			if (compareItems(centerLeft, centerRight, r.trace.direction) == ComparisonResult::Greater)
				r.swap(left, right);

			while (left > 0 && right + 1 < count)
			{
				--left;
				++right;
				const SortItem leftItem = r.at(left), rightItem = r.at(right);
				r.compare(leftItem, rightItem, left, right);
				if (compareItems(leftItem, rightItem, r.trace.direction) == ComparisonResult::Greater)
				{
					insertDoubleKeyFromRight(r, right, left + 1, binarySearch);
					insertDoubleKeyFromLeft(r, left, right + 1, binarySearch);
				}
				else
				{
					insertDoubleKeyFromLeft(r, left, right, binarySearch);
					insertDoubleKeyFromRight(r, right, left + 1, binarySearch);
				}
			}

			if (right + 1 < count)
				insertDoubleKeyFromRight(r, right + 1, 0, binarySearch);
		}

		static void recursiveShellRange(Recorder& r, std::size_t start, std::size_t gap)
		{
			const std::size_t n = r.state.slots.size();
			if (start + gap > n) return;
			recursiveShellRange(r, start, gap * 3);
			recursiveShellRange(r, start + gap, gap * 3);
			recursiveShellRange(r, start + 2 * gap, gap * 3);
			const std::size_t begin = r.trace.events.size();
			r.addMetric("gap_passes");
			r.setMetric("last_gap", gap);
			for (std::size_t i = start + gap; i < n; i += gap)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t position = i;
				while (position >= start + gap)
				{
					const std::size_t previousIndex = position - gap;
					const SortItem previous = r.at(previousIndex);
					r.compare(previous, key, previousIndex, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
					if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
					r.shift(previousIndex, position);
					position -= gap;
				}
				r.place(position, key);
			}
			r.annotate("main", RegionKind::Strided, start, n, gap, -1,
				"Recursive Shell residue " + std::to_string(start) + ", gap " + std::to_string(gap), begin);
		}

		void recursiveShell(Recorder& r)
		{
			if (!r.state.slots.empty()) recursiveShellRange(r, 0, 1);
		}

		void shellSortParallel(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "gap_passes", "last_gap", "parallel_tasks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			const std::size_t extendedCiura[] = { 8861, 3938, 1750, 701, 301, 132, 57, 23, 10, 4, 1 };
			for (std::size_t g = 0; g < sizeof(extendedCiura) / sizeof(extendedCiura[0]); ++g)
			{
				const std::size_t gap = extendedCiura[g];
				if (gap >= count) continue;
				const std::size_t tasks = std::min(gap, count - gap);
				const int phase = r.beginPhase("Parallel Shell gap " + std::to_string(gap));
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				for (std::size_t residue = 0; residue < tasks; ++residue)
				{
					const std::size_t begin = r.trace.events.size();
					for (std::size_t i = residue + gap; i < count; i += gap)
					{
						const SortItem key = r.at(i), previous = r.at(i - gap);
						r.compare(previous, key, i - gap, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) continue;
						r.hold(i);
						std::size_t position = i;
						for (;;)
						{
						const std::size_t source = position - gap;
						r.shift(source, position);
						position = source;
						if (position < residue + gap) break;
						const std::size_t candidateIndex = position - gap;
						const SortItem candidate = r.at(candidateIndex);
						r.compare(candidate, key, candidateIndex, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, key, r.trace.direction) != ComparisonResult::Greater) break;
					}
					r.place(position, key);
					}
					r.addMetric("parallel_tasks");
					r.annotate("main", RegionKind::Strided, residue, count, gap, phase,
						"Parallel Shell residue " + std::to_string(residue), begin);
				}
				r.endPhase(phase, "Parallel Shell gap " + std::to_string(gap));
			}
		}

		void linearDoubleInsertion(Recorder& r) { doubleInsertion(r, false); }
		void binaryDoubleInsertion(Recorder& r) { doubleInsertion(r, true); }
	}
}