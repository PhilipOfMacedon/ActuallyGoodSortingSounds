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

			void binaryMergeSort(Recorder& r)
			{
				const std::size_t n = r.state.slots.size();
				if (n < 2) return;
				std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
				{
					const std::size_t length = end - first;
					if (length < 2) return;
					if (length < 64)
					{
						strategies::binaryInsertionRange(r, first, end);
						r.addMetric("binary_insertion_runs");
						return;
					}
					const std::size_t middle = first + length / 2;
					sortRange(first, middle, depth + 1);
					sortRange(middle, end, depth + 1);
					detail::traceMergeRange(r, first, middle, end);
					r.addMetric("merged_ranges");
					r.setMetric("maximum_merge_depth", std::max<std::uint64_t>(
						r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
				};
				sortRange(0, n, 1);
			}

			void iterativeTopDownMergeSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				if (count < 2) return;
				r.declareMetric("merge_passes");
				r.resizeAuxiliary("merge", count);

				std::size_t subarrayCount = 1;
				while (subarrayCount < count) subarrayCount <<= 1;
				while (subarrayCount > 1)
				{
					const int phase = r.beginPhase("Iterative top-down merge layer");
					for (std::size_t i = 0; i < subarrayCount; i += 2)
					{
						r.chargeWork();
						const std::size_t first = count * i / subarrayCount;
						const std::size_t middle = count * (i + 1) / subarrayCount;
						const std::size_t last = count * (std::min)(i + 2, subarrayCount) / subarrayCount;
						if (first < middle && middle < last)
							detail::traceMergeRange(r, first, middle, last);
					}
					r.endPhase(phase, "Iterative top-down merge layer");
					r.addMetric("merge_passes");
					subarrayCount >>= 1;
				}
			}

			void inPlaceMergeSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				if (count < 2) return;
				r.declareMetric("in_place_merges");
				r.declareMetric("in_place_merge_swaps");
				r.declareMetric("merge_comparisons");

				std::function<void(std::size_t, std::size_t)> sortRange;
				sortRange = [&r, &sortRange](std::size_t first, std::size_t last)
				{
					r.chargeWork();
					const std::size_t length = last - first;
					if (length < 2) return;
					if (length == 2)
					{
						const SortItem a = r.at(first), b = r.at(first + 1);
						r.compare(a, b, first, first + 1);
						r.addMetric("merge_comparisons");
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
						{
							r.swap(first, first + 1, SemanticRole::WriteTarget);
							r.addMetric("in_place_merge_swaps");
						}
						return;
					}

					const std::size_t middle = first + length / 2;
					sortRange(first, middle);
					sortRange(middle, last);
					const std::size_t annotationBegin = r.trace.events.size();
					const int phase = r.beginPhase("In-place stable merge");
					for (std::size_t left = first; left < middle; ++left)
					{
						r.chargeWork();
						const SortItem a = r.at(left), b = r.at(middle);
						r.compare(a, b, left, middle, "main", "main", SemanticRole::ComparedItem, SemanticRole::WriteTarget);
						r.addMetric("merge_comparisons");
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
						{
							r.swap(left, middle, SemanticRole::WriteTarget);
							r.addMetric("in_place_merge_swaps");
							for (std::size_t right = middle; right + 1 < last; ++right)
							{
								r.chargeWork();
								const SortItem earlier = r.at(right), later = r.at(right + 1);
								r.compare(earlier, later, right, right + 1, "main", "main",
									SemanticRole::ComparedItem, SemanticRole::WriteTarget);
								r.addMetric("merge_comparisons");
								if (compareItems(earlier, later, r.trace.direction) != ComparisonResult::Greater) break;
								r.swap(right, right + 1, SemanticRole::WriteTarget);
								r.addMetric("in_place_merge_swaps");
							}
						}
					}
					r.addMetric("in_place_merges");
					r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "In-place merge range", annotationBegin);
					r.endPhase(phase, "In-place stable merge");
				};

				sortRange(0, count);
			}
		}

		void mergeSortStrategy(Recorder& r) { mergeSort(r); }
		void binaryMergeStrategy(Recorder& r) { binaryMergeSort(r); }
		void iterativeTopDownMergeStrategy(Recorder& r) { iterativeTopDownMergeSort(r); }
		void inPlaceMergeStrategy(Recorder& r) { inPlaceMergeSort(r); }
	}
}
