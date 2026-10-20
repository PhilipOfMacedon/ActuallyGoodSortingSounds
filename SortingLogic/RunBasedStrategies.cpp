#include "AlgorithmStrategies.h"
#include "PermutationTraceUtilities.h"
#include "TraceRecorder.h"

#include <cstddef>
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
	}
}
