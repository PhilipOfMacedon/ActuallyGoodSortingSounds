#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

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

	}
}
