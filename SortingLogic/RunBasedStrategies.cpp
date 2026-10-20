#include "AlgorithmStrategies.h"
#include "PermutationTraceUtilities.h"
#include "TraceRecorder.h"
#include "TraceRequestBuilderInternal.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void stalinOverwrite(Recorder& r)
		{
			r.declareMetric("value_rewrites");
			const std::size_t count = r.state.slots.size();
			for (std::size_t i = 1; i < count; ++i)
			{
				const SortItem previous = r.at(i - 1), current = r.at(i);
				r.compare(previous, current, i - 1, i);
				if (compareItems(previous, current, r.trace.direction) != ComparisonResult::Greater) continue;
				SortItem replacement = current;
				replacement.value = previous.value;
				replacement.integerKey = previous.integerKey;
				replacement.hasIntegerKey = previous.hasIntegerKey;
				r.replaceScalar(i, replacement);
				r.addMetric("value_rewrites");
			}
		}

		void canonicalStalinFilter(Recorder& r)
		{
			r.declareMetric("discarded_items");
			r.declareMetric("kept_items");
			std::size_t index = 1;
			while (index < r.state.slots.size())
			{
				const SortItem previous = r.at(index - 1), current = r.at(index);
				r.compare(previous, current, index - 1, index);
				if (compareItems(previous, current, r.trace.direction) == ComparisonResult::Greater)
				{
					r.discardMain(index);
					r.compactAfterDiscard(index);
					r.addMetric("discarded_items");
					continue;
				}
				++index;
			}
			r.setMetric("kept_items", r.state.slots.size());
			if (!r.state.slots.empty())
				r.annotate("main", RegionKind::Contiguous, 0, r.state.slots.size(), 1, -1,
					"Stalin Sort retained subsequence", 0);
		}

		void strandSort(Recorder& r)
		{
			r.declareMetric("strands");
			r.declareMetric("strand_items");
			r.declareMetric("merge_comparisons");
			r.declareMetric("last_strand_length");
			const std::size_t count = r.state.slots.size();
			std::vector<std::size_t> remaining(count), sorted;
			for (std::size_t i = 0; i < count; ++i) remaining[i] = i;
			while (!remaining.empty())
			{
				r.chargeWork();
				r.resizeAuxiliary("strand", 0);
				std::vector<std::size_t> strand(1, remaining[0]), nextRemaining, merged;
				r.writeAuxiliary("strand", 0, r.trace.input[strand[0]]);
				for (std::size_t i = 1; i < remaining.size(); ++i)
				{
					const std::size_t candidate = remaining[i], tail = strand.back();
					const SortItem candidateItem = r.trace.input[candidate], tailItem = r.trace.input[tail];
					r.compare(candidateItem, tailItem, candidate, strand.size() - 1, "main", "strand");
					if (compareItems(candidateItem, tailItem, r.trace.direction) != ComparisonResult::Less)
					{
						strand.push_back(candidate);
						r.writeAuxiliary("strand", strand.size() - 1, candidateItem);
						r.addMetric("strand_items");
					}
					else nextRemaining.push_back(candidate);
				}
				r.addMetric("strands");
				r.setMetric("last_strand_length", strand.size());
				std::size_t strandIndex = 0, sortedIndex = 0;
				merged.reserve(strand.size() + sorted.size());
				while (strandIndex < strand.size() && sortedIndex < sorted.size())
				{
					const SortItem fresh = r.trace.input[strand[strandIndex]];
					const SortItem old = r.trace.input[sorted[sortedIndex]];
					r.compare(fresh, old, strandIndex, sortedIndex, "strand", "strand_sorted");
					r.addMetric("merge_comparisons");
					if (compareItems(fresh, old, r.trace.direction) == ComparisonResult::Less)
						merged.push_back(strand[strandIndex++]);
					else merged.push_back(sorted[sortedIndex++]);
				}
				while (strandIndex < strand.size()) merged.push_back(strand[strandIndex++]);
				while (sortedIndex < sorted.size()) merged.push_back(sorted[sortedIndex++]);
				sorted.swap(merged);
				r.resizeAuxiliary("strand_sorted", 0);
				for (std::size_t i = 0; i < sorted.size(); ++i)
					r.writeAuxiliary("strand_sorted", i, r.trace.input[sorted[i]]);
				remaining.swap(nextRemaining);
			}
			detail::emitPermutationOrder(r, sorted, std::string(), "Strand merge output");
		}

		void patienceSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "pile_count", "pile_assignments", "maximum_pile_height", "heap_pushes", "heap_pops" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			std::vector<std::string> lanes;
			std::vector<std::size_t> pileSizes;
			std::vector<std::size_t> pileMaximumSizes;
			std::vector<std::size_t> pileBeginCursors;
			std::map<std::uint64_t, std::size_t> originalPositions;
			for (std::size_t i = 0; i < r.trace.input.size(); ++i) originalPositions[r.trace.input[i].stableId] = i;

			for (std::size_t inputIndex = 0; inputIndex < count; ++inputIndex)
			{
				const SortItem item = r.at(inputIndex);
				std::size_t low = 0, high = lanes.size();
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const std::size_t topIndex = pileSizes[middle] - 1;
					const SortItem top = r.auxiliaryAt(lanes[middle], topIndex);
					r.compare(item, top, inputIndex, topIndex, "main", lanes[middle], SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy);
					if (compareItems(item, top, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				if (low == lanes.size())
				{
					lanes.push_back("patience.pile." + std::to_string(lanes.size()));
					pileSizes.push_back(0);
					pileMaximumSizes.push_back(0);
					pileBeginCursors.push_back(r.trace.events.size());
				}
				const std::string& lane = lanes[low];
				const std::size_t insertionCursor = r.trace.events.size();
				r.writeAuxiliary(lane, pileSizes[low], item);
				++pileSizes[low];
				pileMaximumSizes[low] = std::max(pileMaximumSizes[low], pileSizes[low]);
				pileBeginCursors[low] = std::min(pileBeginCursors[low], insertionCursor);
				r.addMetric("pile_assignments");
				r.setMetric("pile_count", lanes.size());
				r.setMetric("maximum_pile_height", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_pile_height"], pileSizes[low]));
			}

			const std::string heapLane = "patience.priority_queue";
			r.resizeCounters(heapLane, 0);
			const auto pileHasPriority = [&r, &lanes, &pileSizes, &originalPositions](std::size_t firstPile, std::size_t secondPile)
			{
				const std::size_t firstIndex = pileSizes[firstPile] - 1;
				const std::size_t secondIndex = pileSizes[secondPile] - 1;
				const SortItem first = r.auxiliaryAt(lanes[firstPile], firstIndex);
				const SortItem second = r.auxiliaryAt(lanes[secondPile], secondIndex);
				r.compare(first, second, firstIndex, secondIndex, lanes[firstPile], lanes[secondPile],
					SemanticRole::AuxiliaryCopy, SemanticRole::AuxiliaryCopy);
				const ComparisonResult order = compareItems(first, second, r.trace.direction);
				if (order != ComparisonResult::Equal) return order == ComparisonResult::Less;
				return originalPositions[first.stableId] < originalPositions[second.stableId];
			};
			const auto exchangeHeapEntries = [&r, &heapLane](std::size_t first, std::size_t second)
			{
				const int firstPile = r.counter(heapLane, first), secondPile = r.counter(heapLane, second);
				r.writeCounter(heapLane, first, secondPile);
				r.writeCounter(heapLane, second, firstPile);
			};

			std::size_t heapSize = 0;
			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
			{
				r.resizeCounters(heapLane, heapSize + 1);
				r.writeCounter(heapLane, heapSize, static_cast<int>(pile));
				std::size_t child = heapSize++;
				while (child > 0)
				{
					const std::size_t parent = (child - 1) / 2;
					const std::size_t childPile = static_cast<std::size_t>(r.counter(heapLane, child));
					const std::size_t parentPile = static_cast<std::size_t>(r.counter(heapLane, parent));
					if (!pileHasPriority(childPile, parentPile)) break;
					exchangeHeapEntries(parent, child);
					child = parent;
				}
				r.addMetric("heap_pushes");
			}

			const auto siftHeapDown = [&r, &heapLane, &pileHasPriority, &exchangeHeapEntries](std::size_t root, std::size_t size)
			{
				for (;;)
				{
					const std::size_t leftChild = root * 2 + 1;
					if (leftChild >= size) return;
					std::size_t best = leftChild;
					const std::size_t rightChild = leftChild + 1;
					if (rightChild < size && pileHasPriority(
						static_cast<std::size_t>(r.counter(heapLane, rightChild)),
						static_cast<std::size_t>(r.counter(heapLane, leftChild)))) best = rightChild;
					if (!pileHasPriority(static_cast<std::size_t>(r.counter(heapLane, best)),
						static_cast<std::size_t>(r.counter(heapLane, root)))) return;
					exchangeHeapEntries(root, best);
					root = best;
				}
			};

			for (std::size_t output = 0; output < count; ++output)
			{
				const std::size_t chosenPile = static_cast<std::size_t>(r.counter(heapLane, 0));
				const std::size_t topIndex = pileSizes[chosenPile] - 1;
				const SortItem chosen = r.auxiliaryAt(lanes[chosenPile], topIndex);
				r.writeMain(output, chosen);
				r.resizeAuxiliary(lanes[chosenPile], topIndex);
				--pileSizes[chosenPile];
				r.addMetric("heap_pops");
				if (pileSizes[chosenPile] == 0)
				{
					if (heapSize == 1)
					{
						r.resizeCounters(heapLane, 0);
						heapSize = 0;
					}
					else
					{
						const int lastPile = r.counter(heapLane, heapSize - 1);
						r.writeCounter(heapLane, 0, lastPile);
						r.resizeCounters(heapLane, heapSize - 1);
						--heapSize;
						siftHeapDown(0, heapSize);
					}
				}
				else siftHeapDown(0, heapSize);
			}

			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
				r.annotate(lanes[pile], RegionKind::Group, 0, pileMaximumSizes[pile], 1, static_cast<int>(pile),
					"Patience pile " + std::to_string(pile + 1), pileBeginCursors[pile]);
		}
	}

	SortTrace buildArrayvStalinOverwriteRequest(const TraceBuildRequest& request)
	{
		SortTrace trace = detail::buildAlgorithmTrace(request, SortAlgorithm::ArrayvStalinOverwrite,
			&strategies::stalinOverwrite);
		trace.outcome = AlgorithmOutcome::SortedValueRewrite;
		return trace;
	}

	SortTrace buildCanonicalStalinFilterRequest(const TraceBuildRequest& request)
	{
		SortTrace trace = detail::buildAlgorithmTrace(request, SortAlgorithm::CanonicalStalinFilter,
			&strategies::canonicalStalinFilter);
		trace.outcome = AlgorithmOutcome::SortedWithDiscards;
		return trace;
	}
}
