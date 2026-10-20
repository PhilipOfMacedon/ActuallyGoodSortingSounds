#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		void emitPermutationOrder(Recorder& recorder, const std::vector<std::size_t>& order,
			const std::string& auxiliaryLane, const std::string& phaseLabel);
	}
}
