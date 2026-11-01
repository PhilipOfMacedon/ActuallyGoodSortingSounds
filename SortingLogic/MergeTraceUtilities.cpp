#include "MergeTraceUtilities.h"
#include "TraceRecorder.h"

#include <stdexcept>
#include <string>
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
			const bool detailed = r.detailedReconstructionEnabled();
			std::vector<SortItem> merged;
			const std::string outputLane = "merge_output";
			if (detailed) r.resizeAuxiliary(outputLane, last - first);
			else merged.reserve(last - first);
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
				if (detailed) r.writeAuxiliary(outputLane, out - first, chosen);
				else merged.push_back(chosen);
			}
			if (detailed)
				r.publishAuxiliaryToMain(outputLane, 0, first, last - first, "Merge range reconstruction");
			else r.writeMainRange(first, merged);
			r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
				detailed ? "Merged range" : "Merge range", detailed ? r.trace.events.size() : begin);
		}

		void traceStableKWayMerge(Recorder& r, const std::vector<std::size_t>& boundaries, const std::string& lane,
			const std::string& metricPrefix, const std::string& phaseLabel)
		{
			if (boundaries.size() < 3 || boundaries.front() == boundaries.back()) return;
			const std::size_t first = boundaries.front(), last = boundaries.back();
			const std::size_t runCount = boundaries.size() - 1;
			std::vector<std::size_t> cursors(boundaries.begin(), boundaries.end() - 1);
			const std::string cursorLane = metricPrefix + "_run_cursors";
			r.resizeCounters(cursorLane, runCount);
			for (std::size_t run = 0; run < runCount; ++run)
				r.writeCounter(cursorLane, run, static_cast<int>(cursors[run]));
			const std::size_t begin = r.trace.events.size();
			const int phase = r.beginPhase(phaseLabel);
			std::size_t output = 0;
			while (true)
			{
				std::size_t selected = runCount;
				for (std::size_t run = 0; run < runCount; ++run)
				{
					if (cursors[run] >= boundaries[run + 1]) continue;
					if (selected == runCount) { selected = run; continue; }
					const SortItem candidate = r.at(cursors[run]);
					const SortItem currentBest = r.at(cursors[selected]);
					r.compare(candidate, currentBest, cursors[run], cursors[selected], "main", "main",
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric(metricPrefix + "_comparisons");
					if (compareItems(candidate, currentBest, r.trace.direction) == ComparisonResult::Less) selected = run;
					r.chargeWork();
				}
				if (selected == runCount) break;
				r.writeAuxiliary(lane, output++, r.at(cursors[selected]));
				++cursors[selected];
				r.writeCounter(cursorLane, selected, static_cast<int>(cursors[selected]));
				r.addMetric(metricPrefix + "_auxiliary_writes");
			}
			if (output != last - first) throw std::logic_error("K-way merge did not consume every input item.");
			for (std::size_t index = first; index < last; ++index)
				r.writeMain(index, r.auxiliaryAt(lane, index - first), SemanticRole::WriteTarget);
			r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, phaseLabel, begin);
			r.addMetric(metricPrefix + "_merges");
			r.endPhase(phase, phaseLabel);
			r.resizeCounters(cursorLane, 0);
		}
	}
}
