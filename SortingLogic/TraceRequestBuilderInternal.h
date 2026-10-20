#pragma once

#include "SortingLogic.h"

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
		typedef void (*AlgorithmStrategy)(Recorder&);

		SortTrace buildAlgorithmTrace(const TraceBuildRequest& request, SortAlgorithm algorithm,
			AlgorithmStrategy strategy);
	}
}
