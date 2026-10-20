#pragma once

#include "SortingLogic.h"

namespace sortinglogic
{
	namespace detail
	{
		// Resolves stable catalog option IDs over either an explicit configuration
		// or the legacy request fields. Strategy builders consume only the result.
		AlgorithmConfiguration resolveAlgorithmConfiguration(const TraceBuildRequest& request,
			const AlgorithmDescriptor& descriptor);

		// Compatibility projection for callers and trace metadata that still expose
		// the pre-configuration request fields.
		void projectAlgorithmConfigurationToLegacyFields(TraceBuildRequest& request,
			const AlgorithmConfiguration& configuration, const AlgorithmDescriptor& descriptor);
	}
}
