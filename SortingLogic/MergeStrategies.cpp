#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"
#include "MergeTraceUtilities.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		namespace
		{
			void mergeSort(Recorder& r)
			{
				const std::size_t n = r.state.slots.size();
				if (n < 2) return;
				r.resizeAuxiliary("merge", n);
				if (r.trace.configuration.mergeExecutionMode == MergeExecutionMode::TopDown)
				{
					std::size_t maximumDepth = 0;
					for (std::size_t span = n; span > 1; span = (span + 1) / 2) ++maximumDepth;
					std::function<void(std::size_t, std::size_t, std::size_t)> split;
					split = [&r, &split, maximumDepth](std::size_t first, std::size_t last, std::size_t depth)
					{
						if (last - first < 2) return;
						const std::size_t middle = first + (last - first) / 2;
						split(first, middle, depth + 1);
						split(middle, last, depth + 1);
						detail::traceMergeRange(r, first, middle, last);
						const std::uint64_t pass = static_cast<std::uint64_t>(maximumDepth - depth);
						r.setMetric("merge_passes", std::max(r.state.metrics.algorithm["merge_passes"], pass));
					};
					split(0, n, 0);
					return;
				}
				for (std::size_t width = 1; width < n; width *= 2)
				{
					r.addMetric("merge_passes");
					for (std::size_t first = 0; first < n; first += width * 2)
					{
						const std::size_t middle = std::min(first + width, n);
						const std::size_t last = std::min(first + width * 2, n);
						detail::traceMergeRange(r, first, middle, last);
					}
					if (width > n / 2) break;
				}
			}
		}

		void mergeSortStrategy(Recorder& r) { mergeSort(r); }
	}
}
