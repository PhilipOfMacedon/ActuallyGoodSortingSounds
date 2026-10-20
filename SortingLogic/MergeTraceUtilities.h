#pragma once

#include <cstddef>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		void traceMergeRange(Recorder& recorder, std::size_t first, std::size_t middle, std::size_t last);
	}
}
