#pragma once

#include <cstddef>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		std::size_t traceMedianOfThree(Recorder& recorder, std::size_t first, std::size_t middle, std::size_t last);
		std::size_t traceHoarePartition(Recorder& recorder, std::size_t first, std::size_t end, std::size_t pivotIndex);
		void traceHeapSortRange(Recorder& recorder, std::size_t first, std::size_t end);
	}
}
