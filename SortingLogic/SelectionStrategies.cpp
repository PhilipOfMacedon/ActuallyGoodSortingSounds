#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <map>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		void badSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("bad_sort_candidate_checks");
			r.declareMetric("bad_sort_comparisons");
			r.declareMetric("bad_sort_exchanges");
			for (std::size_t first = 0; first < count; ++first)
			{
				std::size_t selected = first;
				for (std::size_t candidate = first; candidate < count; ++candidate)
				{
					r.addMetric("bad_sort_candidate_checks");
					bool suffixMinimum = true;
					for (std::size_t later = candidate + 1; later < count; ++later)
					{
						const SortItem current = r.at(candidate), other = r.at(later);
						r.compare(current, other, candidate, later, "main", "main",
							SemanticRole::CurrentMinimum, SemanticRole::ProbedItem);
						r.addMetric("bad_sort_comparisons");
						if (compareItems(current, other, r.trace.direction) == ComparisonResult::Greater)
						{
							suffixMinimum = false;
							break;
						}
						r.chargeWork();
					}
					if (suffixMinimum)
					{
						selected = candidate;
						break;
					}
				}
				if (selected != first)
				{
					r.swap(first, selected, SemanticRole::CurrentMinimum);
					r.addMetric("bad_sort_exchanges");
				}
			}
		}

		void cycleSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t cycleStart = 0; cycleStart + 1 < n; ++cycleStart)
			{
				SortItem item = r.at(cycleStart);
				r.hold(cycleStart);
				for (;;)
				{
					std::size_t position = cycleStart;
					for (std::size_t i = cycleStart + 1; i < n; ++i)
					{
						const SortItem candidate = r.at(i);
						r.compare(candidate, item, i, cycleStart, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, item, r.trace.direction) == ComparisonResult::Less) ++position;
					}
					if (position == cycleStart)
					{
						r.place(cycleStart, item);
						break;
					}
					while (position < n)
					{
						const SortItem candidate = r.at(position);
						r.compare(candidate, item, position, cycleStart, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, item, r.trace.direction) != ComparisonResult::Equal) break;
						++position;
					}
					if (position >= n)
					{
						r.place(cycleStart, item);
						break;
					}
					r.carryWrite(position);
					item = r.state.heldItem;
				}
			}
		}

		void stableCycleSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("stable_cycle_cycles");
			r.declareMetric("stable_cycle_rank_probes");
			r.declareMetric("stable_cycle_item_moves");
			r.declareMetric("stable_cycle_fixed_positions");
			if (count < 2) return;

			const std::string referenceLane = "stable_cycle_reference";
			const std::string flagLane = "stable_cycle_fixed_bits";
			r.resizeAuxiliary(referenceLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeAuxiliary(referenceLane, i, r.at(i));
			r.resizeCounters(flagLane, (count + 7) / 8);
			std::map<std::uint64_t, std::size_t> originalPosition;
			for (std::size_t i = 0; i < count; ++i) originalPosition[r.trace.input[i].stableId] = i;

			const auto isFixed = [&r, &flagLane](std::size_t index)
			{
				return (r.counter(flagLane, index >> 3) & (1 << (index & 7))) != 0;
			};
			const auto markFixed = [&r, &flagLane](std::size_t index)
			{
				const std::size_t word = index >> 3;
				const int mask = 1 << (index & 7);
				if ((r.counter(flagLane, word) & mask) == 0)
				{
					r.writeCounter(flagLane, word, r.counter(flagLane, word) | mask);
					r.addMetric("stable_cycle_fixed_positions");
				}
			};
			const auto stableRank = [&r, &referenceLane, &originalPosition](const SortItem& item)
			{
				const std::size_t itemOrigin = originalPosition.find(item.stableId)->second;
				std::size_t destination = 0;
				for (std::size_t i = 0; i < r.trace.input.size(); ++i)
				{
					const SortItem candidate = r.auxiliaryAt(referenceLane, i);
					if (candidate.stableId == item.stableId) continue;
					r.compare(candidate, item, i, itemOrigin, referenceLane, "main",
						SemanticRole::ComparedItem, SemanticRole::HeldKey);
					r.addMetric("stable_cycle_rank_probes");
					const ComparisonResult order = compareItems(candidate, item, r.trace.direction);
					const std::size_t candidateOrigin = originalPosition.find(candidate.stableId)->second;
					if (order == ComparisonResult::Less || (order == ComparisonResult::Equal && candidateOrigin < itemOrigin)) ++destination;
				}
				return destination;
			};

			for (std::size_t cycleStart = 0; cycleStart < count; ++cycleStart)
			{
				if (isFixed(cycleStart)) continue;
				r.addMetric("stable_cycle_cycles");
				SortItem held = r.at(cycleStart);
				r.hold(cycleStart);
				for (;;)
				{
					const std::size_t destination = stableRank(held);
					if (destination >= count) throw std::logic_error("Stable Cycle Sort computed an out-of-range destination.");
					if (destination == cycleStart)
					{
						r.place(cycleStart, held);
						markFixed(cycleStart);
						break;
					}
					if (isFixed(destination)) throw std::logic_error("Stable Cycle Sort targeted a previously fixed destination.");
					r.carryWrite(destination);
					held = r.state.heldItem;
					markFixed(destination);
					r.addMetric("stable_cycle_item_moves");
				}
			}
			r.resizeAuxiliary(referenceLane, 0);
			r.resizeCounters(flagLane, 0);
		}
	}
}