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


		void binaryInsertionRange(Recorder& r, std::size_t first, std::size_t end)
		{
			for (std::size_t i = first + (first < end ? 1 : 0); i < end; ++i)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t low = first, high = i;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
				r.place(low, key);
			}
		}

		void binaryInsertion(Recorder& r)
		{
			binaryInsertionRange(r, 0, r.state.slots.size());
		}

	}
}
