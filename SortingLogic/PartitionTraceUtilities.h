#pragma once

#include "SortingLogic.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		std::pair<SortItem, std::size_t> sampledMedian(Recorder& recorder,
			const std::vector<std::size_t>& positions, const char* metricPrefix);
		std::size_t traceMedianOfThree(Recorder& recorder, std::size_t first, std::size_t middle, std::size_t last);
		std::size_t traceHoarePartition(Recorder& recorder, std::size_t first, std::size_t end, std::size_t pivotIndex);
		void traceHeapSortRange(Recorder& recorder, std::size_t first, std::size_t end);
	}
}
