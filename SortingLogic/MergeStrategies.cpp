#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"
#include "MergeTraceUtilities.h"
#include "GrailTraceUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

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

			void parallelMergeSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				if (count < 2) return;
				r.declareMetric("parallel_merge_tasks");
				r.declareMetric("merge_passes");
				r.declareMetric("maximum_merge_depth");
				r.resizeAuxiliary("merge", count);
				std::function<void(std::size_t, std::size_t, std::size_t)> split;
				split = [&r, &split](std::size_t first, std::size_t last, std::size_t depth)
				{
					r.chargeWork();
					if (last - first < 2) return;
					const std::size_t middle = first + (last - first) / 2;
					const int siblingTasks = r.beginPhase("Parallel Merge sibling tasks");
					r.addMetric("parallel_merge_tasks");
					split(first, middle, depth + 1);
					split(middle, last, depth + 1);
					r.endPhase(siblingTasks, "Parallel Merge sibling tasks");
					detail::traceMergeRange(r, first, middle, last);
					r.addMetric("merge_passes");
					r.setMetric("maximum_merge_depth", (std::max)(
						r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
				};
				split(0, count, 1);
			}

			void optimizedBottomUpMergePass(Recorder& r, std::size_t width, bool sourceIsAuxiliary)
			{
				const std::size_t n = r.state.slots.size();
				const std::string lane = "optimized_bottom_up";
				r.addMetric("merge_passes");
				for (std::size_t first = 0; first < n; first += width * 2)
				{
					const std::size_t middle = std::min(first + width, n);
					const std::size_t end = std::min(first + width * 2, n);
					const std::size_t annotationBegin = r.trace.events.size();
					std::size_t left = first, right = middle;
					for (std::size_t out = first; out < end; ++out)
					{
						SortItem chosen;
						if (left >= middle)
							chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, right++) : r.at(right++);
						else if (right >= end)
							chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, left++) : r.at(left++);
						else
						{
							const SortItem a = sourceIsAuxiliary ? r.auxiliaryAt(lane, left) : r.at(left);
							const SortItem b = sourceIsAuxiliary ? r.auxiliaryAt(lane, right) : r.at(right);
							const std::string sourceLane = sourceIsAuxiliary ? lane : "main";
							r.compare(a, b, left, right, sourceLane, sourceLane);
							if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
								chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, left++) : r.at(left++);
							else
								chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, right++) : r.at(right++);
						}
						if (sourceIsAuxiliary) r.writeMain(out, chosen);
						else r.writeAuxiliary(lane, out, chosen);
					}
					r.annotate(sourceIsAuxiliary ? "main" : lane, RegionKind::Contiguous, first, end, 1, -1,
						"Optimized bottom-up merge width " + std::to_string(width), annotationBegin);
				}
			}

			void optimizedBottomUpMergeSort(Recorder& r)
			{
				const std::size_t n = r.state.slots.size();
				if (n < 2) return;
				const std::size_t runSize = 16;
				for (std::size_t first = 0; first < n; first += runSize)
				{
					strategies::binaryInsertionRange(r, first, std::min(first + runSize, n));
					r.addMetric("binary_insertion_runs");
				}
				if (n <= runSize) return;
				r.resizeAuxiliary("optimized_bottom_up", n);
				for (std::size_t width = runSize; width < n; width *= 4)
				{
					optimizedBottomUpMergePass(r, width, false);
					optimizedBottomUpMergePass(r, width * 2, true);
				}
			}


			void ectaSortInternal(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				for (const char* key : { "ecta_min_run", "ecta_insertion_runs", "ecta_four_way_groups", "ecta_four_way_merges",
					"ecta_four_way_comparisons", "ecta_four_way_auxiliary_writes", "ecta_four_way_passes",
					"ecta_binary_merges", "ecta_binary_passes" }) r.declareMetric(key);
				if (count < 2) return;
				std::size_t minRun = count;
				while (minRun >= 32) minRun = (minRun + 1) / 2;
				minRun = (std::max)(std::size_t(1), minRun);
				r.setMetric("ecta_min_run", static_cast<std::uint64_t>(minRun));
				r.resizeAuxiliary("ecta_buffer", count);
				for (std::size_t first = 0; first < count; first += minRun)
				{
					strategies::binaryInsertionRange(r, first, (std::min)(count, first + minRun));
					r.addMetric("ecta_insertion_runs");
				}

				std::size_t width = minRun;
				while (width <= count / 4)
				{
					const int phase = r.beginPhase("Ecta four-way ping-pong layer width " + std::to_string(width));
					for (std::size_t first = 0; first < count; first += width * 4)
					{
						std::vector<std::size_t> boundaries;
						boundaries.reserve(5);
						for (std::size_t part = 0; part <= 4; ++part)
							boundaries.push_back((std::min)(count, first + width * part));
						std::size_t active = 0;
						for (std::size_t part = 0; part < 4; ++part)
							if (boundaries[part] < boundaries[part + 1]) ++active;
						if (active > 1)
						{
							detail::traceStableKWayMerge(r, boundaries, "ecta_buffer", "ecta_four_way", "Ecta four-way block merge");
							r.addMetric("ecta_four_way_groups");
							r.addMetric("ecta_four_way_merges");
						}
					}
					r.addMetric("ecta_four_way_passes");
					r.endPhase(phase, "Ecta four-way ping-pong layer");
					if (width > count / 16)
					{
						width *= 4;
						break;
					}
					width *= 4;
				}

				r.resizeAuxiliary("merge", count);
				while (width < count)
				{
					const int phase = r.beginPhase("Ecta binary merge tail width " + std::to_string(width));
					for (std::size_t first = 0; first < count; first += width * 2)
					{
						const std::size_t middle = (std::min)(count, first + width);
						const std::size_t last = (std::min)(count, middle + width);
						if (middle < last)
						{
						detail::traceMergeRange(r, first, middle, last);
							r.addMetric("ecta_binary_merges");
						}
					}
					r.addMetric("ecta_binary_passes");
					r.endPhase(phase, "Ecta binary merge tail");
					if (width > count / 2) break;
					width *= 2;
				}
				r.resizeAuxiliary("merge", 0);
				r.resizeAuxiliary("ecta_buffer", 0);
			}
		static void laziestStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "laziest_block_runs", "laziest_merges", "laziest_merge_comparisons",
				"laziest_exponential_probes", "laziest_rotation_swaps" })
				r.declareMetric(key);
			if (count < 2) return;
			if (count <= 16)
			{
				r.addMetric("laziest_block_runs");
				strategies::binaryInsertionRange(r, 0, count);
				return;
			}
			const std::size_t blockLength = (std::max)(std::size_t(16), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			std::size_t runStart = 0;
			for (; runStart + 2 * blockLength < count; runStart += blockLength)
			{
				strategies::binaryInsertionRange(r, runStart, runStart + blockLength);
				r.addMetric("laziest_block_runs");
			}
			if (runStart < count)
			{
				strategies::binaryInsertionRange(r, runStart, count);
				r.addMetric("laziest_block_runs");
			}

			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("laziest_merge_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto leftExponentialSearch = [&r](std::size_t keyIndex, std::size_t first, std::size_t end, const SortItem& key)
			{
				std::size_t offset = 1;
				while (first + offset - 1 < end)
				{
					const std::size_t probe = first + offset - 1;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("laziest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater) break;
					if (offset > end - first) { offset = end - first + 1; break; }
					offset *= 2;
				}
				std::size_t low = first + offset / 2;
				std::size_t high = (std::min)(end, first + offset - 1);
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("laziest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				return low;
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last)
			{
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("laziest_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Laziest stable rotation", begin);
			};
			const auto merge = [&r, &compareAt, &leftExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const std::size_t begin = r.trace.events.size();
				const int phase = r.beginPhase("Laziest in-place stable merge");
				std::size_t left = first, right = middle;
				while (left < right && right < last)
				{
					r.chargeWork();
					if (compareAt(left, right) == ComparisonResult::Greater)
					{
						const SortItem held = r.at(left);
						const std::size_t destination = leftExponentialSearch(left, right + 1, last, held);
						rotate(left, right, destination);
						left += destination - right;
						right = destination;
					}
					else ++left;
				}
				r.addMetric("laziest_merges");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Laziest merged run", begin);
				r.endPhase(phase, "Laziest in-place stable merge");
			};

			while (runStart >= blockLength)
			{
				merge(runStart - blockLength, runStart, count);
				runStart -= blockLength;
			}
		}

		static void lazierestStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "lazierest_block_runs", "lazierest_merges", "lazierest_fragmented_merges",
				"lazierest_merge_comparisons", "lazierest_exponential_probes", "lazierest_rotation_swaps" })
				r.declareMetric(key);
			if (count < 2) return;
			if (count <= 16)
			{
				r.addMetric("lazierest_block_runs");
				strategies::binaryInsertionRange(r, 0, count);
				return;
			}

			// This source variant uses ceil(cuberoot(n)) sized insertion runs,
			// followed by square-sized merges and fragmented suffix folds.
			std::size_t lowRoot = 0, highRoot = (std::min)(std::size_t(1291), count);
			while (lowRoot < highRoot)
			{
				const std::size_t middle = lowRoot + (highRoot - lowRoot) / 2;
				if (middle * middle * middle >= count) highRoot = middle;
				else lowRoot = middle + 1;
			}
			const std::size_t blockLength = (std::max)(std::size_t(1), lowRoot);
			const std::size_t squareLength = blockLength * blockLength;
			for (std::size_t end = count % blockLength; end <= count; end += blockLength)
			{
				strategies::binaryInsertionRange(r, end >= blockLength ? end - blockLength : 0, end);
				r.addMetric("lazierest_block_runs");
			}

			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem left = r.at(first), right = r.at(second);
				r.compare(left, right, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("lazierest_merge_comparisons");
				return compareItems(left, right, r.trace.direction);
			};
			const auto leftExponentialSearch = [&r](std::size_t keyIndex, std::size_t first, std::size_t end, const SortItem& key)
			{
				std::size_t offset = 1;
				while (first + offset - 1 < end)
				{
					const std::size_t probe = first + offset - 1;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater) break;
					if (offset > end - first) { offset = end - first + 1; break; }
					offset *= 2;
				}
				std::size_t low = first + offset / 2;
				std::size_t high = (std::min)(end, first + offset - 1);
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				return low;
			};
			const auto rightExponentialSearch = [&r](std::size_t first, std::size_t end, std::size_t keyIndex, const SortItem& key)
			{
				if (first >= end) return first;
				std::size_t offset = 1;
				while (offset <= end - first)
				{
					const std::size_t probe = end - offset;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) >= ComparisonResult::Equal) break;
					offset *= 2;
				}
				// Match the source's right exponential search bounds. When the
				// stride runs past the beginning, the unprobed prefix still needs
				// a binary search; returning `first` here loses that information.
				const std::size_t range = end - first;
				const std::size_t low = offset > range
					? first
					: (std::max)(first, end - offset + 1);
				const std::size_t high = end - offset / 2;
				std::size_t left = low, right = (std::max)(low, (std::min)(end, high));
				while (left < right)
				{
					const std::size_t middle = left + (right - left) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) right = middle;
					else left = middle + 1;
				}
				return left;
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first == middle || middle == last) return;
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("lazierest_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Lazierest stable rotation", begin);
			};

			const auto mergeForward = [&r, &compareAt, &leftExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::size_t left = first, right = middle;
				while (left < right && right < last)
				{
					r.chargeWork();
					if (compareAt(left, right) == ComparisonResult::Greater)
					{
						const std::size_t destination = leftExponentialSearch(left, right + 1, last, r.at(left));
						rotate(left, right, destination);
						left += destination - right;
						right = destination;
					}
					else ++left;
				}
			};
			const auto mergeBackward = [&r, &rightExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::ptrdiff_t left = static_cast<std::ptrdiff_t>(middle) - 1;
				std::ptrdiff_t right = static_cast<std::ptrdiff_t>(last) - 1;
				while (right > left && left >= static_cast<std::ptrdiff_t>(first))
				{
					r.chargeWork();
					const SortItem leftItem = r.at(static_cast<std::size_t>(left));
					const SortItem rightItem = r.at(static_cast<std::size_t>(right));
					r.compare(leftItem, rightItem, static_cast<std::size_t>(left), static_cast<std::size_t>(right), "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					if (compareItems(leftItem, rightItem, r.trace.direction) == ComparisonResult::Greater)
					{
						const std::size_t bound = rightExponentialSearch(first, static_cast<std::size_t>(left), static_cast<std::size_t>(right), rightItem);
						rotate(bound, static_cast<std::size_t>(left + 1), static_cast<std::size_t>(right + 1));
						right -= left + 1 - static_cast<std::ptrdiff_t>(bound);
						left = static_cast<std::ptrdiff_t>(bound) - 1;
					}
					else --right;
				}
			};
			const auto merge = [&r, &mergeForward, &mergeBackward](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const std::size_t begin = r.trace.events.size();
				const int phase = r.beginPhase("Lazierest in-place merge");
				if (last - middle < middle - first) mergeBackward(first, middle, last);
				else mergeForward(first, middle, last);
				r.addMetric("lazierest_merges");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Lazierest merged run", begin);
				r.endPhase(phase, "Lazierest in-place merge");
			};

			std::size_t runEnd = count - blockLength;
			std::size_t suffixEnd = count;
			while (runEnd > 0)
			{
				r.chargeWork();
				if (suffixEnd - runEnd == squareLength)
				{
					suffixEnd -= squareLength;
					runEnd -= (std::min)(blockLength, runEnd);
				}
				if (runEnd == 0) break;
				mergeForward(runEnd >= blockLength ? runEnd - blockLength : 0, runEnd, suffixEnd);
				r.addMetric("lazierest_merges");
				runEnd = runEnd >= blockLength ? runEnd - blockLength : 0;
			}

			const auto fragmentedMerge = [&r, &leftExponentialSearch, &rotate, &merge](std::size_t first, std::size_t middle, std::size_t last, std::size_t block)
			{
				std::size_t index = first + (middle - first) % block;
				while (index < middle)
				{
					r.chargeWork();
					const std::size_t destination = leftExponentialSearch(index, middle, last, r.at(index));
					rotate(index, middle, destination);
					const std::size_t moved = destination - middle;
					const std::size_t mergeMiddle = index;
					index += moved;
					middle += moved;
					merge(first, mergeMiddle, index);
					first = index;
					index += block;
				}
				const std::size_t finalFirst = index > block ? (std::max)(first, index - block) : first;
				merge(finalFirst, index, last);
				r.addMetric("lazierest_fragmented_merges");
			};

			for (std::size_t start = count - squareLength; start > 0;)
			{
				const std::size_t middle = start;
				const std::size_t first = middle > squareLength ? middle - squareLength : 0;
				fragmentedMerge(first, middle, count, blockLength);
				if (start <= squareLength) break;
				start -= squareLength;
			}
		}





		static void lazyStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "lazy_pair_comparisons", "lazy_pair_swaps", "lazy_merge_comparisons", "lazy_rotation_swaps", "lazy_merge_passes" })
				r.declareMetric(key);
			for (std::size_t index = 1; index < count; index += 2)
			{
				const SortItem left = r.at(index - 1), right = r.at(index);
				r.compare(left, right, index - 1, index, "main", "main");
				r.addMetric("lazy_pair_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(index - 1, index);
					r.addMetric("lazy_pair_swaps");
				}
			}
			for (std::size_t part = 2; part < count;)
			{
				const int phase = r.beginPhase("Lazy stable merge pass");
				std::size_t left = 0;
				while (left + 2 * part <= count)
				{
					detail::grailMergeWithoutBuffer(r, left, part, part, "lazy");
					left += 2 * part;
				}
				const std::size_t remainder = count - left;
				if (remainder > part) detail::grailMergeWithoutBuffer(r, left, part, remainder - part, "lazy");
				r.addMetric("lazy_merge_passes");
				r.endPhase(phase, "Lazy stable merge pass");
				if (part > count / 2) break;
				part *= 2;
			}
		}

		static void optimizedLazyStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_lazy_runs", "optimized_lazy_run_reversals", "optimized_lazy_insertion_comparisons",
				"optimized_lazy_shifts", "optimized_lazy_merge_comparisons", "optimized_lazy_rotation_swaps", "optimized_lazy_merge_passes" })
				r.declareMetric(key);
			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem left = r.at(first), right = r.at(second);
				r.compare(left, right, first, second, "main", "main");
				r.addMetric("optimized_lazy_insertion_comparisons");
				return compareItems(left, right, r.trace.direction);
			};
			const auto insertionRun = [&r, &compareAt](std::size_t first, std::size_t last)
			{
				if (last - first < 2) return;
				std::size_t index = first + 1;
				if (compareAt(first, index) == ComparisonResult::Greater)
				{
					while (index < last && compareAt(index - 1, index) == ComparisonResult::Greater) ++index;
					for (std::size_t left = first, right = index; left < right && left < --right; ++left)
					{
						r.swap(left, right, SemanticRole::WriteTarget);
						r.addMetric("optimized_lazy_run_reversals");
					}
				}
				else
				{
					while (index < last && compareAt(index - 1, index) != ComparisonResult::Greater) ++index;
				}
				for (; index < last; ++index)
				{
					const SortItem key = r.at(index);
					r.hold(index);
					std::size_t position = index;
					while (position > first)
					{
						const SortItem previous = r.at(position - 1);
						r.compare(key, previous, index, position - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
						r.addMetric("optimized_lazy_insertion_comparisons");
						if (compareItems(key, previous, r.trace.direction) != ComparisonResult::Less) break;
						r.shift(position - 1, position);
						r.addMetric("optimized_lazy_shifts");
						--position;
					}
					r.place(position, key);
				}
				r.addMetric("optimized_lazy_runs");
			};

			for (std::size_t first = 0; first < count; first += 16)
			{
				const std::size_t last = (std::min)(count, first + 16);
				const std::size_t begin = r.trace.events.size();
				insertionRun(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Optimized lazy insertion run", begin);
			}
			for (std::size_t part = 16; part < count;)
			{
				const int phase = r.beginPhase("Optimized lazy merge pass");
				std::size_t left = 0;
				while (left + 2 * part <= count)
				{
					detail::grailMergeWithoutBuffer(r, left, part, part, "optimized_lazy");
					left += 2 * part;
				}
				const std::size_t remainder = count - left;
				if (remainder > part) detail::grailMergeWithoutBuffer(r, left, part, remainder - part, "optimized_lazy");
				r.addMetric("optimized_lazy_merge_passes");
				r.endPhase(phase, "Optimized lazy merge pass");
				if (part > count / 2) break;
				part *= 2;
			}
		}

		static void weaveMergeRange(Recorder& r, std::size_t first, std::size_t last)
		{
			if (last <= first) return;
			if (last - first == 1)
			{
				const SortItem left = r.at(first), right = r.at(last);
				r.compare(left, right, first, last, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric("weave_base_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(first, last);
					r.addMetric("weave_base_swaps");
				}
				return;
			}

			const std::size_t middle = first + (last - first) / 2;
			weaveMergeRange(r, first, middle);
			weaveMergeRange(r, middle + 1, last);
			r.addMetric("weave_recursive_merges");
			const std::size_t mergeBegin = r.trace.events.size();

			// Move each right-run item left into its alternating position. Adjacent
			// swaps preserve the relative order of the cells shifted by the weave.
			for (std::size_t offset = 1; offset <= middle - first; ++offset)
			{
				std::size_t source = middle + offset;
				const std::size_t target = first + offset * 2 - 1;
				while (source > target)
				{
					r.swap(source - 1, source, SemanticRole::WriteTarget);
					--source;
					r.addMetric("weave_interleave_swaps");
				}
			}

			// ArrayV's weave kernel finishes each interleaved range with a leftward
			// insertion pass rather than a conventional two-way merge.
			for (std::size_t index = first; index <= last; ++index)
			{
				std::size_t position = index;
				while (position > first)
				{
					const SortItem current = r.at(position), previous = r.at(position - 1);
					r.compare(current, previous, position, position - 1, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ComparedItem);
					r.addMetric("weave_insertion_comparisons");
					if (compareItems(current, previous, r.trace.direction) == ComparisonResult::Greater) break;
					r.swap(position - 1, position, SemanticRole::HeldKey);
					--position;
					r.addMetric("weave_insertion_swaps");
				}
			}
			r.annotate("main", RegionKind::Contiguous, first, middle + 1, 1, -1, "Weave left run", mergeBegin);
			r.annotate("main", RegionKind::Contiguous, middle + 1, last + 1, 1, -1, "Weave right run", mergeBegin);
		}

		static void weaveMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "weave_recursive_merges", "weave_interleave_swaps", "weave_insertion_comparisons",
				"weave_insertion_swaps", "weave_base_comparisons", "weave_base_swaps" }) r.declareMetric(key);
			if (count > 1) weaveMergeRange(r, 0, count - 1);
		}

		}

		void laziestStableSortStrategy(Recorder& r) { laziestStableSort(r); }
		void lazierestStableSortStrategy(Recorder& r) { lazierestStableSort(r); }
		void lazyStableSortStrategy(Recorder& r) { lazyStableSort(r); }
		void optimizedLazyStableSortStrategy(Recorder& r) { optimizedLazyStableSort(r); }
		void weaveMergeSortStrategy(Recorder& r) { weaveMergeSort(r); }
		void ectaStrategy(Recorder& r) { ectaSortInternal(r); }
		void mergeSortStrategy(Recorder& r) { mergeSort(r); }
		void binaryMergeStrategy(Recorder& r) { binaryMergeSort(r); }
		void iterativeTopDownMergeStrategy(Recorder& r) { iterativeTopDownMergeSort(r); }
		void inPlaceMergeStrategy(Recorder& r) { inPlaceMergeSort(r); }
		void parallelMergeStrategy(Recorder& r) { parallelMergeSort(r); }
		void optimizedBottomUpMergeStrategy(Recorder& r) { optimizedBottomUpMergeSort(r); }
		void pdMergeSortStrategy(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("pdmerge_run_count");
			r.declareMetric("pdmerge_run_reversals");
			r.declareMetric("pdmerge_merge_passes");
			r.declareMetric("pdmerge_merges");
			r.declareMetric("pdmerge_buffered_items");
			if (count < 2) return;

			std::vector<std::size_t> runs;
			runs.push_back(0);
			std::size_t start = 0;
			while (start + 1 < count)
			{
				const SortItem first = r.at(start), second = r.at(start + 1);
				r.compare(first, second, start, start + 1);
				const bool forwardRun = compareItems(first, second, r.trace.direction) != ComparisonResult::Greater;
				std::size_t end = start + 2;
				while (end < count)
				{
					const SortItem previous = r.at(end - 1), current = r.at(end);
					r.compare(previous, current, end - 1, end);
					const bool thisForward = compareItems(previous, current, r.trace.direction) != ComparisonResult::Greater;
					if (thisForward != forwardRun) break;
					++end;
				}
				if (!forwardRun)
				{
					const int group = r.beginPhase("Reverse strict descending run");
					for (std::size_t left = start, right = end - 1; left < right; ++left, --right)
						r.swap(left, right, SemanticRole::WriteTarget);
					r.endPhase(group, "Descending run reversed");
					r.addMetric("pdmerge_run_reversals");
				}
				runs.push_back(end);
				r.addMetric("pdmerge_run_count");
				start = end;
			}
			if (runs.back() != count) runs.push_back(count);

			const std::string runLane = "pdmerge_run_boundaries";
			r.resizeCounters(runLane, runs.size());
			for (std::size_t i = 0; i < runs.size(); ++i)
				r.writeCounter(runLane, i, static_cast<int>(runs[i]));
			const std::string bufferLane = "pdmerge_buffer";
			const auto mergePair = [&r, &bufferLane](std::size_t begin, std::size_t middle, std::size_t end)
			{
				const std::size_t leftCount = middle - begin;
				const std::size_t rightCount = end - middle;
				const int group = r.beginPhase("Pattern-defeating merge " + std::to_string(begin) + ".." + std::to_string(end));
				if (leftCount <= rightCount)
				{
					r.resizeAuxiliary(bufferLane, leftCount);
					for (std::size_t i = 0; i < leftCount; ++i) r.writeAuxiliary(bufferLane, i, r.at(begin + i));
					std::size_t left = 0, right = middle, output = begin;
					while (left < leftCount && right < end)
					{
						const SortItem leftItem = r.auxiliaryAt(bufferLane, left);
						const SortItem rightItem = r.at(right);
						r.compare(leftItem, rightItem, left, right, bufferLane, "main", SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(output++, leftItem, SemanticRole::WriteTarget), ++left;
						else
						r.writeMain(output++, rightItem, SemanticRole::WriteTarget), ++right;
					}
					while (left < leftCount) r.writeMain(output++, r.auxiliaryAt(bufferLane, left++), SemanticRole::WriteTarget);
					r.addMetric("pdmerge_buffered_items", leftCount);
				}
				else
				{
					r.resizeAuxiliary(bufferLane, rightCount);
					for (std::size_t i = 0; i < rightCount; ++i) r.writeAuxiliary(bufferLane, i, r.at(middle + i));
					std::size_t left = middle, right = rightCount, output = end;
					while (left > begin && right > 0)
					{
						const SortItem leftItem = r.at(left - 1);
						const SortItem rightItem = r.auxiliaryAt(bufferLane, right - 1);
						r.compare(leftItem, rightItem, left - 1, right - 1, "main", bufferLane, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(--output, rightItem, SemanticRole::WriteTarget), --right;
						else
						r.writeMain(--output, leftItem, SemanticRole::WriteTarget), --left;
					}
					while (right > 0) r.writeMain(--output, r.auxiliaryAt(bufferLane, --right), SemanticRole::WriteTarget);
					r.addMetric("pdmerge_buffered_items", rightCount);
				}
				r.resizeAuxiliary(bufferLane, 0);
				r.endPhase(group, "Merge pair complete");
				r.addMetric("pdmerge_merges");
			};

			std::uint64_t pass = 0;
			while (runs.size() > 2)
			{
				const int group = r.beginPhase("Pairwise natural-run merge pass " + std::to_string(pass + 1));
				std::vector<std::size_t> next;
				next.push_back(0);
				const std::size_t runCount = runs.size() - 1;
				for (std::size_t run = 0; run < runCount; run += 2)
				{
					if (run + 1 < runCount)
						mergePair(runs[run], runs[run + 1], runs[run + 2]);
					next.push_back(runs[(std::min)(run + 2, runCount)]);
				}
				r.endPhase(group, "Pairwise run pass complete");
				++pass;
				r.setMetric("pdmerge_merge_passes", pass);
				runs.swap(next);
				r.resizeCounters(runLane, runs.size());
				for (std::size_t i = 0; i < runs.size(); ++i)
					r.writeCounter(runLane, i, static_cast<int>(runs[i]));
			}
			r.resizeCounters(runLane, 0);
			r.resizeAuxiliary(bufferLane, 0);
		}

	}
}
