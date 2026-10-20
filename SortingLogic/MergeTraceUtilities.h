#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		void traceMergeRange(Recorder& recorder, std::size_t first, std::size_t middle, std::size_t last);
		void traceStableKWayMerge(Recorder& recorder, const std::vector<std::size_t>& boundaries,
			const std::string& lane, const std::string& metricPrefix, const std::string& phaseLabel);
	}
}
