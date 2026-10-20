#include "MergeTraceUtilities.h"
#include "TraceRecorder.h"

#include <vector>

namespace sortinglogic
{
	namespace detail
	{
		void traceMergeRange(Recorder& r, std::size_t first, std::size_t middle, std::size_t last)
		{
			if (middle >= last) return;
			const std::size_t begin = r.trace.events.size();
			for (std::size_t i = first; i < last; ++i) r.writeAuxiliary("merge", i, r.at(i));
			std::vector<SortItem> merged;
			if (r.trace.configuration.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite)
				merged.reserve(last - first);
			std::size_t left = first, right = middle;
			for (std::size_t out = first; out < last; ++out)
			{
				SortItem chosen;
				if (left >= middle) chosen = r.auxiliaryAt("merge", right++);
				else if (right >= last) chosen = r.auxiliaryAt("merge", left++);
				else
				{
					const SortItem a = r.auxiliaryAt("merge", left), b = r.auxiliaryAt("merge", right);
					r.compare(a, b, left, right, "merge", "merge");
					if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
						chosen = r.auxiliaryAt("merge", left++);
					else chosen = r.auxiliaryAt("merge", right++);
				}
				if (r.trace.configuration.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite) merged.push_back(chosen);
				else r.writeMain(out, chosen);
			}
			if (r.trace.configuration.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite)
				r.writeMainRange(first, merged);
			r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Merge range", begin);
		}
	}
}
