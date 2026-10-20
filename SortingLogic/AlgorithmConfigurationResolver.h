#pragma once

#include "SortingLogic.h"

namespace sortinglogic
{
	namespace detail
	{
		// Resolves stable catalog option IDs over either an explicit configuration
		// or the legacy request fields. The returned configuration is canonical;
		// callers must not project it back into the compatibility fields.
		AlgorithmConfiguration resolveAlgorithmConfiguration(const TraceBuildRequest& request,
			const AlgorithmDescriptor& descriptor);
	}
}
