#include "PermutationTraceUtilities.h"
#include "TraceRecorder.h"

namespace sortinglogic
{
	namespace detail
	{
		void emitPermutationOrder(Recorder& r, const std::vector<std::size_t>& order,
			const std::string& auxiliaryLane, const std::string& phaseLabel)
		{
			const int phase = r.beginPhase(phaseLabel);
			for (std::size_t i = 0; i < order.size(); ++i)
			{
				if (!auxiliaryLane.empty()) r.probeAuxiliary(auxiliaryLane, order[i], SemanticRole::ProbedItem);
				else r.probeItem("main", order[i], r.trace.input[order[i]], SemanticRole::ProbedItem);
			}
			std::vector<std::size_t> currentPosition(order.size());
			for (std::size_t i = 0; i < order.size(); ++i) currentPosition[i] = i;
			for (std::size_t target = 0; target < order.size(); ++target)
			{
				const std::size_t source = currentPosition[order[target]];
				if (source != target)
				{
					std::size_t displacedSource = 0;
					while (displacedSource < order.size() && currentPosition[displacedSource] != target) ++displacedSource;
					r.swap(target, source, SemanticRole::WriteTarget);
					currentPosition[order[target]] = target;
					if (displacedSource < order.size()) currentPosition[displacedSource] = source;
				}
			}
			r.endPhase(phase, phaseLabel);
		}

	}
}
