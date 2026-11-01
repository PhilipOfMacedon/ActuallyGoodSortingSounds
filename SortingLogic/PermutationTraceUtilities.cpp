#include "PermutationTraceUtilities.h"
#include "TraceRecorder.h"

namespace sortinglogic
{
	namespace detail
	{
		void emitPermutationOrder(Recorder& r, const std::vector<std::size_t>& order,
			const std::string& auxiliaryLane, const std::string& phaseLabel)
		{
			const bool detailed = r.detailedReconstructionEnabled();
			const int phase = detailed ? r.beginReconstruction(phaseLabel, order.size()) : r.beginPhase(phaseLabel);
			std::vector<std::size_t> currentPosition(order.size());
			for (std::size_t i = 0; i < order.size(); ++i) currentPosition[i] = i;
			for (std::size_t target = 0; target < order.size(); ++target)
			{
				const std::size_t source = currentPosition[order[target]];
				if (!detailed || source == target)
				{
					if (!auxiliaryLane.empty()) r.probeAuxiliary(auxiliaryLane, order[target], SemanticRole::ProbedItem);
					else r.probeItem("main", source, r.at(source), SemanticRole::ProbedItem);
				}
				else if (!auxiliaryLane.empty())
					r.probeAuxiliary(auxiliaryLane, order[target], SemanticRole::ProbedItem);
				else
					r.probeItem("main", source, r.at(source), SemanticRole::ProbedItem);
				if (detailed) r.setReconstructionProgress(target + 1, order.size());
				if (source != target)
				{
					std::size_t displacedSource = 0;
					while (displacedSource < order.size() && currentPosition[displacedSource] != target) ++displacedSource;
					r.swap(target, source, SemanticRole::WriteTarget);
					currentPosition[order[target]] = target;
					if (displacedSource < order.size()) currentPosition[displacedSource] = source;
				}
				if (detailed) r.markReconstructionFrontier("main", 0, target + 1, "Committed permutation prefix");
			}
			if (detailed) r.endReconstruction(phase, phaseLabel);
			else r.endPhase(phase, phaseLabel);
		}

	}
}
