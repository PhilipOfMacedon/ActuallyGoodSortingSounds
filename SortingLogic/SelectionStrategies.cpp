#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

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

		void improvedBlockSelectionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "block_selection_runs", "block_selection_blocks_selected", "block_selection_block_swaps",
				"block_selection_rotations", "block_selection_merge_comparisons", "block_selection_forward_merges",
				"block_selection_backward_merges", "block_selection_max_block_length" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				r.compare(r.at(first), r.at(second), first, second);
				r.addMetric("block_selection_merge_comparisons");
			};
			const auto ceilingRoot = [&r](std::size_t value)
			{
				std::size_t root = 1;
				while (root * root < value) { root *= 2; r.chargeWork(); }
				return root;
			};
			const auto multiSwap = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t i = 0; i < length; ++i)
				{
					r.swap(first + i, second + i);
					r.addMetric("block_selection_block_swaps");
				}
			};
			const auto rotate = [&r, &multiSwap](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::size_t left = middle - first, right = end - middle;
				while (left > 0 && right > 0)
				{
					r.chargeWork();
					if (right < left)
					{
						multiSwap(middle - right, middle, right);
						end -= right; middle -= right; left -= right;
					}
					else
					{
						multiSwap(first, middle, left);
						first += left; middle += left; right -= left;
					}
				}
				r.addMetric("block_selection_rotations");
			};
			const auto mergeForward = [&r, &compareAt, &later, &rotate](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::size_t left = first, right = middle;
				while (left < right && right < end)
				{
					compareAt(left, right);
					if (later(r.at(left), r.at(right)))
					{
						std::size_t runEnd = right + 1;
						while (runEnd < end)
						{
							compareAt(left, runEnd);
							if (!later(r.at(left), r.at(runEnd))) break;
						++runEnd;
						}
						const std::size_t moved = runEnd - right;
						rotate(left, right, runEnd);
						left += moved; right = runEnd;
					}
					else ++left;
				}
				r.addMetric("block_selection_forward_merges");
				return left;
			};
			const auto mergeBackward = [&r, &compareAt, &later, &rotate](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::ptrdiff_t left = static_cast<std::ptrdiff_t>(middle) - 1;
				std::ptrdiff_t right = static_cast<std::ptrdiff_t>(end) - 1;
				while (right > left && left >= static_cast<std::ptrdiff_t>(first))
				{
					compareAt(static_cast<std::size_t>(left), static_cast<std::size_t>(right));
					if (later(r.at(static_cast<std::size_t>(left)), r.at(static_cast<std::size_t>(right))))
					{
						std::ptrdiff_t runStart = left;
						while (runStart > static_cast<std::ptrdiff_t>(first))
						{
							compareAt(static_cast<std::size_t>(runStart - 1), static_cast<std::size_t>(right));
							if (!later(r.at(static_cast<std::size_t>(runStart - 1)), r.at(static_cast<std::size_t>(right)))) break;
							--runStart;
						}
						const std::size_t moved = static_cast<std::size_t>(left - runStart + 1);
						rotate(static_cast<std::size_t>(runStart), static_cast<std::size_t>(left + 1), static_cast<std::size_t>(right + 1));
						right -= static_cast<std::ptrdiff_t>(moved); left = runStart - 1;
					}
					else --right;
				}
				r.addMetric("block_selection_backward_merges");
			};
			const auto selectRange = [&r, &compareAt, &later](std::size_t first, std::size_t end, std::size_t blockLength)
			{
				std::size_t minimum = first;
				for (std::size_t candidate = first + blockLength; candidate < end; candidate += blockLength)
				{
					compareAt(candidate, minimum);
					if (later(r.at(minimum), r.at(candidate)) ||
						(compareItems(r.at(candidate), r.at(minimum), r.trace.direction) == ComparisonResult::Equal &&
						 later(r.at(minimum + blockLength - 1), r.at(candidate + blockLength - 1))))
					{
						if (compareItems(r.at(candidate), r.at(minimum), r.trace.direction) == ComparisonResult::Equal)
							compareAt(minimum + blockLength - 1, candidate + blockLength - 1);
						minimum = candidate;
					}
				}
				return minimum;
			};
			const auto blockSelect = [&r, &compareAt, &later, &multiSwap, &selectRange](std::size_t first, std::size_t middle,
				std::size_t end, std::size_t blockLength)
			{
				std::size_t target = first, selected = middle, candidate = middle;
				while (target < middle)
				{
					compareAt(target, middle);
					if (later(r.at(target), r.at(middle))) break;
					target += blockLength;
				}
				if (target == middle) return;
				multiSwap(target, selected, blockLength);
				target += blockLength; selected += blockLength;
				while (target < selected && selected < end)
				{
					compareAt(candidate, selected);
					if (!later(r.at(candidate), r.at(selected)))
					{
						if (target != candidate) multiSwap(target, candidate, blockLength);
						target += blockLength;
						candidate = selectRange(std::max(middle, target), selected, blockLength);
					}
					else
					{
						if (candidate == target) candidate = selected;
						if (target != selected) multiSwap(target, selected, blockLength);
						target += blockLength; selected += blockLength;
					}
				}
				while (target < selected)
				{
					candidate = selectRange(target, end, blockLength);
					if (target != candidate) multiSwap(target, candidate, blockLength);
					target += blockLength;
				}
				r.addMetric("block_selection_blocks_selected");
			};

			for (std::size_t run = 1; run < count; run *= 2)
			{
				r.chargeWork();
				const int phase = r.beginPhase("Block-selection merge run " + std::to_string(run));
				std::size_t blockLength = ceilingRoot(run), blockRun = run;
				const std::size_t boundary = count - count % blockLength;
				r.setMetric("block_selection_max_block_length", std::max<std::uint64_t>(r.state.metrics.algorithm["block_selection_max_block_length"], blockLength));
				while (blockRun > 16)
				{
					for (std::size_t base = 0; base + run < boundary; base += 2 * run)
						for (std::size_t block = base; block + blockRun < std::min(base + 2 * run, boundary); block += blockRun)
							blockSelect(block, block + blockRun, std::min(block + 2 * blockRun, boundary), blockLength);
					blockRun = blockLength;
					blockLength = ceilingRoot(blockLength);
					r.setMetric("block_selection_max_block_length", std::max<std::uint64_t>(r.state.metrics.algorithm["block_selection_max_block_length"], blockLength));
				}
				for (std::size_t base = 0; base + run < boundary; base += 2 * run)
				{
					std::size_t finish = base;
					for (std::size_t block = base; block + blockRun < std::min(base + 2 * run, boundary); block += blockRun)
						finish = mergeForward(finish, block + blockRun, std::min(block + 2 * blockRun, boundary));
				}
				const std::size_t tailStart = count - count % (2 * run);
				mergeBackward(tailStart, boundary, count);
				r.endPhase(phase, "Block-selection merge run complete");
				r.addMetric("block_selection_runs");
			}
		}
	}
}
