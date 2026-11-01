#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"
#include "MergeTraceUtilities.h"
#include "GrailTraceUtilities.h"
#include "PartitionTraceUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include <utility>

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
				if (r.trace.configuration.value("execution_mode", static_cast<int>(MergeExecutionMode::BottomUp)) == static_cast<int>(MergeExecutionMode::TopDown))
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
		static void stableBinaryMergeSortRange(Recorder& r, std::size_t first, std::size_t last, const std::string& metricPrefix)
		{
			for (std::size_t start = first; start < last; start += 8)
			{
				strategies::binaryInsertionRange(r, start, (std::min)(last, start + 8));
				r.addMetric(metricPrefix + "_insertion_runs");
			}
			for (std::size_t width = 8; width < last - first;)
			{
				const int phase = r.beginPhase(metricPrefix + " binary merge pass");
				for (std::size_t start = first; start < last;)
				{
					const std::size_t middle = (std::min)(last, start + width);
					const std::size_t end = (std::min)(last, middle + width);
					if (middle < end) detail::traceMergeRange(r, start, middle, end);
					start = end;
				}
				r.addMetric(metricPrefix + "_binary_merge_passes");
				r.endPhase(phase, metricPrefix + " binary merge pass");
				if (width > (last - first) / 2) break;
				width *= 2;
			}
		}

		static void recursiveRotationMerge(Recorder& r, std::size_t first, std::size_t middle, std::size_t last,
			const char* metricPrefix)
		{
			if (first >= middle || middle >= last) return;
			r.chargeWork();
			if (last - first == 2)
			{
				const SortItem left = r.at(first), right = r.at(middle);
				r.compare(left, right, first, middle, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric(std::string(metricPrefix) + "_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					r.addMetric(std::string(metricPrefix) + "_swaps");
				}
				return;
			}
			const std::size_t leftLength = middle - first, rightLength = last - middle;
			std::size_t firstCut = first, secondCut = middle;
			if (leftLength >= rightLength)
			{
				firstCut = first + leftLength / 2;
				secondCut = middle + detail::grailBinarySearch(r, middle, rightLength, firstCut, true, metricPrefix);
			}
			else
			{
				secondCut = middle + rightLength / 2;
				firstCut = first + detail::grailBinarySearch(r, first, leftLength, secondCut, false, metricPrefix);
			}
			const std::size_t newMiddle = firstCut + (secondCut - middle);
			if (firstCut != middle && secondCut != middle)
			{
				detail::grailRotate(r, firstCut, middle - firstCut, secondCut - middle, metricPrefix);
				r.addMetric(std::string(metricPrefix) + "_rotations");
			}
			recursiveRotationMerge(r, first, firstCut, newMiddle, metricPrefix);
			recursiveRotationMerge(r, newMiddle, secondCut, last, metricPrefix);
		}

		static void optimizedWeaveMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_weave_blocks", "optimized_weave_bit_reversals", "optimized_weave_insertion_passes",
				"optimized_weave_comparisons", "optimized_weave_swaps", "optimized_weave_merge_comparisons",
				"optimized_weave_rotation_swaps", "optimized_weave_rotations" }) r.declareMetric(key);
			if (count < 2) return;
			for (std::size_t first = 0; first < count; first += 2)
			{
				const std::size_t last = (std::min)(count, first + 2);
				const std::size_t length = last - first;
				if (length < 2) continue;
				const int phase = r.beginPhase("Optimized weave base block");
				const std::size_t begin = r.trace.events.size();
				std::size_t bits = 0;
				for (std::size_t padded = 1; padded < length; padded <<= 1) ++bits;
				for (std::size_t index = 1; index < length; ++index)
				{
					r.chargeWork();
					std::size_t reversed = 0, value = index;
					for (std::size_t bit = 0; bit < bits; ++bit) { reversed = (reversed << 1) | (value & 1u); value >>= 1; }
					if (reversed > index)
					{
						r.swap(first + index, first + reversed, SemanticRole::WriteTarget);
						r.addMetric("optimized_weave_bit_reversals");
					}
				}
				strategies::binaryInsertionRange(r, first, last);
				r.addMetric("optimized_weave_blocks");
				r.addMetric("optimized_weave_insertion_passes");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Bit-reversed weave block", begin);
				r.endPhase(phase, "Optimized weave block");
			}
			for (std::size_t width = 2; width < count;)
			{
				const int phase = r.beginPhase("Optimized weave merge level width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						recursiveRotationMerge(r, first, middle, last, "optimized_weave");
						r.addMetric("optimized_weave_blocks");
					}
				}
				r.endPhase(phase, "Optimized weave merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		static void wikiSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "wiki_natural_runs", "wiki_reversed_runs", "wiki_extensions", "wiki_merges",
				"wiki_buffered_merges", "wiki_rotation_merges", "wiki_buffer_writes", "wiki_comparisons", "wiki_merge_comparisons", "wiki_rotation_swaps" }) r.declareMetric(key);
			if (count < 2) return;
			struct Run { std::size_t first, last; Run(std::size_t a, std::size_t b) : first(a), last(b) {} };
			std::vector<Run> runs;
			const std::size_t minimumRun = 16;
			for (std::size_t first = 0; first < count;)
			{
				std::size_t last = first + 1;
				bool descending = false;
				if (last < count)
				{
					const SortItem a = r.at(first), b = r.at(last);
					r.compare(a, b, first, last, "main", "main");
					r.addMetric("wiki_comparisons");
					descending = compareItems(a, b, r.trace.direction) == ComparisonResult::Greater;
					++last;
					while (last < count)
					{
						const SortItem previous = r.at(last - 1), current = r.at(last);
						r.compare(previous, current, last - 1, last, "main", "main");
						r.addMetric("wiki_comparisons");
						const ComparisonResult relation = compareItems(previous, current, r.trace.direction);
						if (descending ? relation != ComparisonResult::Greater : relation == ComparisonResult::Greater) break;
						++last;
					}
				}
				if (descending)
				{
					std::size_t left = first, right = last - 1;
					while (left < right) { r.swap(left++, right--, SemanticRole::WriteTarget); }
					r.addMetric("wiki_reversed_runs");
				}
				else r.addMetric("wiki_natural_runs");
				const std::size_t target = (std::min)(count, (std::max)(last, first + minimumRun));
				if (target > last) { strategies::binaryInsertionRange(r, first, target); r.addMetric("wiki_extensions"); last = target; }
				runs.push_back(Run(first, last));
				first = last;
			}
			const std::size_t capacity = (std::max)(std::size_t(1), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			r.resizeAuxiliary("wiki_cache", capacity);
			while (runs.size() > 1)
			{
				std::vector<Run> next;
				const int phase = r.beginPhase("Wiki buffered block merge pass");
				for (std::size_t i = 0; i < runs.size(); i += 2)
				{
					if (i + 1 == runs.size()) { next.push_back(runs[i]); continue; }
					const Run left = runs[i], right = runs[i + 1];
					const std::size_t begin = r.trace.events.size();
					const std::uint64_t comparisons = r.state.metrics.comparisons;
					detail::grailBufferedMerge(r, left.first, left.last, right.last, capacity, "wiki_cache", "wiki");
					r.addMetric("wiki_merges");
					r.addMetric("wiki_comparisons", r.state.metrics.comparisons - comparisons);
					r.annotate("main", RegionKind::Contiguous, left.first, right.last, 1, phase, "Wiki block merge", begin);
					next.push_back(Run(left.first, right.last));
				}
				r.endPhase(phase, "Wiki merge pass");
				runs.swap(next);
			}
			r.resizeAuxiliary("wiki_cache", 0);
		}

		static void andreySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "andrey_runs", "andrey_merge_levels", "andrey_comparisons", "andrey_merge_comparisons",
				"andrey_rotation_swaps", "andrey_rotations", "andrey_swaps" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t block = (std::max)(std::size_t(2), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			for (std::size_t first = 0; first < count; first += block)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + block));
				r.addMetric("andrey_runs");
			}
			for (std::size_t width = block; width < count;)
			{
				const int phase = r.beginPhase("Andrey power-of-two block merge width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t mergeBegin = r.trace.events.size();
						recursiveRotationMerge(r, first, middle, last, "andrey");
						r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Andrey rotation merge", mergeBegin);
					}
				}
				r.addMetric("andrey_merge_levels");
				r.endPhase(phase, "Andrey block merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		static void newShuffleMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "new_shuffle_runs", "new_shuffle_merge_levels", "new_shuffle_comparisons", "new_shuffle_rotations" }) r.declareMetric(key);
			if (count < 2) return;
			for (std::size_t run = 0; run < count; run += 2)
			{
				if (run + 1 < count)
				{
					const SortItem a = r.at(run), b = r.at(run + 1);
					r.compare(a, b, run, run + 1, "main", "main");
					r.addMetric("new_shuffle_comparisons");
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(run, run + 1, SemanticRole::WriteTarget);
				}
				r.addMetric("new_shuffle_runs");
			}
			for (std::size_t width = 2; width < count;)
			{
				const int phase = r.beginPhase("New Shuffle stable merge width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					const std::size_t mergeBegin = r.trace.events.size();
					std::size_t output = first, right = middle;
					while (output < middle && right < last)
					{
						r.chargeWork();
						const SortItem leftItem = r.at(output), rightItem = r.at(right);
						r.compare(leftItem, rightItem, output, right, "main", "main");
						r.addMetric("new_shuffle_comparisons");
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater) ++output;
						else
					{
						for (std::size_t position = right; position > output; --position)
							{ r.swap(position - 1, position, SemanticRole::WriteTarget); r.addMetric("new_shuffle_rotations"); }
						++output; ++middle; ++right;
					}
					}
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Shuffle-merge run pair", mergeBegin);
				}
				r.addMetric("new_shuffle_merge_levels");
				r.endPhase(phase, "New Shuffle merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		static void yujiBufferedMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "yuji_runs", "yuji_merge_levels", "yuji_merges", "yuji_comparisons", "yuji_buffer_writes" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t runSize = (std::max)(std::size_t(2), count / 16);
			r.resizeAuxiliary("yuji_buffer", count / 2 + (count & 1u));
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("yuji_runs");
			}
			for (std::size_t width = runSize; width < count;)
			{
				const int phase = r.beginPhase("Yuji buffered merge redistribution width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					if (middle == last) continue;
					const std::size_t leftCount = middle - first, rightCount = last - middle;
					const std::size_t begin = r.trace.events.size();
					if (leftCount <= rightCount)
					{
						for (std::size_t i = 0; i < leftCount; ++i) { r.writeAuxiliary("yuji_buffer", i, r.at(first + i)); r.addMetric("yuji_buffer_writes"); }
						std::size_t left = 0, right = middle, output = first;
						while (left < leftCount && right < last)
						{
							const SortItem a = r.auxiliaryAt("yuji_buffer", left), b = r.at(right);
							r.compare(a, b, first + left, right, "yuji_buffer", "main"); r.addMetric("yuji_comparisons");
							if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater) r.writeMain(output++, r.auxiliaryAt("yuji_buffer", left++));
							else r.writeMain(output++, r.at(right++));
						}
						while (left < leftCount) r.writeMain(output++, r.auxiliaryAt("yuji_buffer", left++));
					}
					else
					{
						for (std::size_t i = 0; i < rightCount; ++i) { r.writeAuxiliary("yuji_buffer", i, r.at(middle + i)); r.addMetric("yuji_buffer_writes"); }
						std::size_t left = middle, right = rightCount, output = last;
						while (left > first && right > 0)
						{
							const SortItem a = r.at(left - 1), b = r.auxiliaryAt("yuji_buffer", right - 1);
							r.compare(a, b, left - 1, middle + right - 1, "main", "yuji_buffer"); r.addMetric("yuji_comparisons");
							if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.writeMain(--output, r.at(--left));
							else r.writeMain(--output, r.auxiliaryAt("yuji_buffer", --right));
						}
						while (right > 0) r.writeMain(--output, r.auxiliaryAt("yuji_buffer", --right));
					}
					r.addMetric("yuji_merges"); r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Yuji redistributed merge", begin);
				}
				r.addMetric("yuji_merge_levels"); r.endPhase(phase, "Yuji merge level");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("yuji_buffer", 0);
		}

		static void kotaSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "kota_block_length", "kota_initial_blocks", "kota_merge_levels", "kota_merges",
				"kota_comparisons", "kota_auxiliary_writes", "kota_buffer_writes", "kota_rotation_merges" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t block = 1;
			while (block * block < count) ++block;
			r.setMetric("kota_block_length", block);
			r.resizeAuxiliary("kota_block_buffer", count);
			for (std::size_t first = 0; first < count; first += block)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + block));
				r.addMetric("kota_initial_blocks");
			}
			std::size_t width = block;
			while (width < count)
			{
				const std::size_t fanout = 4;
				const std::size_t groupWidth = width * fanout;
				const int phase = r.beginPhase("Kota block merge level width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += groupWidth)
				{
					std::vector<std::size_t> boundaries(1, first);
					for (std::size_t boundary = first + width; boundary < (std::min)(count, first + groupWidth); boundary += width)
						boundaries.push_back(boundary);
					boundaries.push_back((std::min)(count, first + groupWidth));
					if (boundaries.size() > 2)
					{
						detail::traceStableKWayMerge(r, boundaries, "kota_block_buffer", "kota", "Kota four-block merge");
					}
				}
				r.addMetric("kota_merge_levels"); r.endPhase(phase, "Kota block merge level");
				if (width > count / fanout) break;
				width *= fanout;
			}
			r.resizeAuxiliary("kota_block_buffer", 0);
		}

		static void remiSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "remi_branching_factor", "remi_run_length", "remi_initial_runs", "remi_merge_passes",
				"remi_merges", "remi_comparisons", "remi_auxiliary_writes", "remi_insertion_runs", "remi_binary_merge_passes" }) r.declareMetric(key);
			if (count < 2) return;

			std::size_t branchingFactor = 1;
			while (branchingFactor * branchingFactor * branchingFactor < count) ++branchingFactor;
			const std::size_t runLength = branchingFactor * branchingFactor;
			r.setMetric("remi_branching_factor", static_cast<std::uint64_t>(branchingFactor));
			r.setMetric("remi_run_length", static_cast<std::uint64_t>(runLength));
			r.resizeAuxiliary("remi_buffer", count);
			std::size_t runCount = 0;
			for (std::size_t first = 0; first < count; first += runLength)
			{
				stableBinaryMergeSortRange(r, first, (std::min)(count, first + runLength), "remi");
				r.addMetric("remi_initial_runs");
				++runCount;
			}

			std::size_t width = runLength;
			while (width < count && runCount > 1)
			{
				const std::size_t groupSpan = width * branchingFactor;
				const int phase = r.beginPhase("Remi " + std::to_string(branchingFactor) + "-way merge level width " + std::to_string(width));
				std::size_t nextRunCount = 0;
				for (std::size_t first = 0; first < count; first += groupSpan)
				{
					std::vector<std::size_t> boundaries;
					boundaries.push_back(first);
					for (std::size_t boundary = first + width; boundary < (std::min)(count, first + groupSpan); boundary += width)
						boundaries.push_back(boundary);
					boundaries.push_back((std::min)(count, first + groupSpan));
					if (boundaries.size() > 2)
						detail::traceStableKWayMerge(r, boundaries, "remi_buffer", "remi", "Remi run-heap merge group");
					++nextRunCount;
				}
				r.endPhase(phase, "Remi multiway merge level");
				r.addMetric("remi_merge_passes");
				runCount = nextRunCount;
				if (width > count / branchingFactor) break;
				width *= branchingFactor;
			}
			r.resizeAuxiliary("remi_buffer", 0);
		}

		static void chaliceSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "chalice_distinct_keys", "chalice_blocks", "chalice_merge_levels", "chalice_merges",
				"chalice_comparisons", "chalice_key_probes", "chalice_auxiliary_writes", "chalice_run_cursors",
				"chalice_insertion_runs", "chalice_binary_merge_passes" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t root = (std::max)(std::size_t(2), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			r.resizeAuxiliary("chalice_keys", (std::min)(root, count));
			for (std::size_t i = 0; i < count && r.state.auxiliaryItems["chalice_keys"].size() > 0; ++i)
			{
				bool distinct = true;
				for (std::size_t k = 0; k < r.state.auxiliaryItems["chalice_keys"].size(); ++k)
				{
					if (r.state.auxiliaryItems["chalice_keys"][k].occupied)
					{
						const SortItem candidate = r.at(i), key = r.auxiliaryAt("chalice_keys", k);
						r.compare(candidate, key, i, k, "main", "chalice_keys", SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("chalice_comparisons"); r.addMetric("chalice_key_probes");
						if (compareItems(candidate, key, r.trace.direction) == ComparisonResult::Equal) { distinct = false; break; }
					}
				}
				if (!distinct) continue;
				for (std::size_t k = 0; k < r.state.auxiliaryItems["chalice_keys"].size(); ++k)
					if (!r.state.auxiliaryItems["chalice_keys"][k].occupied)
						{ r.writeAuxiliary("chalice_keys", k, r.at(i)); r.addMetric("chalice_distinct_keys"); r.addMetric("chalice_auxiliary_writes"); break; }
			}
			r.resizeAuxiliary("chalice_keys", 0);
			r.resizeAuxiliary("chalice_buffer", count);
			const std::size_t runLength = root * root;
			for (std::size_t first = 0; first < count; first += runLength)
			{
				stableBinaryMergeSortRange(r, first, (std::min)(count, first + runLength), "chalice");
				r.addMetric("chalice_blocks");
			}
			std::size_t width = runLength;
			while (width < count)
			{
				const int phase = r.beginPhase("Chalice key-block merge width " + std::to_string(width));
				std::vector<std::size_t> next(1, 0);
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					if (middle < last)
					{
						std::vector<std::size_t> pairBoundaries; pairBoundaries.push_back(first); pairBoundaries.push_back(middle); pairBoundaries.push_back(last);
						detail::traceStableKWayMerge(r, pairBoundaries, "chalice_buffer", "chalice", "Chalice block merge");
					}
					next.push_back(last);
				}
				r.addMetric("chalice_merge_levels"); r.endPhase(phase, "Chalice block merge level");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("chalice_buffer", 0);
		}

		static void fifthMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "fifth_partition_count", "fifth_insertion_runs", "fifth_binary_merge_passes",
				"fifth_merges", "fifth_comparisons", "fifth_auxiliary_writes" }) r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const std::size_t fifthLength = count / 5;
			const std::size_t firstLength = count - fifthLength * 4;
			std::vector<std::size_t> boundaries;
			boundaries.push_back(0);
			boundaries.push_back(firstLength);
			for (std::size_t part = 1; part <= 4; ++part) boundaries.push_back(firstLength + fifthLength * part);
			for (std::size_t part = 0; part + 1 < boundaries.size(); ++part)
			{
				stableBinaryMergeSortRange(r, boundaries[part], boundaries[part + 1], "fifth");
				r.addMetric("fifth_partition_count");
			}
			r.resizeAuxiliary("merge", 0);
			r.resizeAuxiliary("fifth_buffer", count);
			detail::traceStableKWayMerge(r, boundaries, "fifth_buffer", "fifth", "Five-part stable merge");
			r.resizeAuxiliary("fifth_buffer", 0);
		}

		static void quadMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "quad_merged_groups", "quad_leaf_runs", "quad_merge_layers", "quad_merges", "quad_comparisons", "quad_auxiliary_writes" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("quad_buffer", count);
			for (std::size_t first = 0; first < count; first += 8)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + 8));
				r.addMetric("quad_leaf_runs");
			}
			for (std::size_t runWidth = 8; runWidth < count;)
			{
				const int phase = r.beginPhase("Quad four-run merge layer");
				for (std::size_t first = 0; first < count; first += runWidth * 4)
				{
					std::vector<std::size_t> boundaries;
					boundaries.reserve(5);
					for (std::size_t part = 0; part <= 4; ++part)
						boundaries.push_back((std::min)(count, first + runWidth * part));
					std::size_t activeRuns = 0;
					for (std::size_t part = 0; part < 4; ++part)
						if (boundaries[part] < boundaries[part + 1]) ++activeRuns;
					if (activeRuns > 1)
					{
						detail::traceStableKWayMerge(r, boundaries, "quad_buffer", "quad", "Quad four-run merge");
						r.addMetric("quad_merged_groups");
					}
				}
				r.addMetric("quad_merge_layers");
				r.endPhase(phase, "Quad four-run merge layer");
				if (runWidth > count / 4) break;
				runWidth *= 4;
			}
			r.resizeAuxiliary("quad_buffer", 0);
		}

		static void blockSwapMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("merge_passes");
			r.declareMetric("block_merge_ranges");
			r.declareMetric("block_merge_comparisons");
			r.declareMetric("block_swaps");
			r.declareMetric("block_item_swaps");

			const auto binarySearchMid = [&r](std::size_t first, std::size_t middle, std::size_t last) -> std::size_t
			{
				std::size_t low = 0, high = (std::min)(middle - first, last - middle);
				while (low < high)
				{
					r.chargeWork();
					const std::size_t offset = low + (high - low) / 2;
					const std::size_t leftIndex = middle - offset - 1, rightIndex = middle + offset;
					const SortItem left = r.at(leftIndex), right = r.at(rightIndex);
					r.compare(left, right, leftIndex, rightIndex, "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("block_merge_comparisons");
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) low = offset + 1;
					else high = offset;
				}
				return low;
			};
			const auto swapBlocks = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t offset = 0; offset < length; ++offset)
				{
					r.chargeWork();
					r.swap(first + offset, second + offset, SemanticRole::WriteTarget);
					r.addMetric("block_item_swaps");
				}
				r.addMetric("block_swaps");
			};
			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &binarySearchMid, &swapBlocks, &merge, count](std::size_t first,
				std::size_t middle, std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Block-Swap Merge recursion exceeded its bounded range depth.");
				const std::size_t annotationBegin = r.trace.events.size();
				r.addMetric("block_merge_ranges");
				std::size_t blockLength = binarySearchMid(first, middle, last);
				while (blockLength != 0)
				{
					swapBlocks(middle - blockLength, middle, blockLength);
					const std::size_t rightMiddle = middle + blockLength;
					merge(middle, rightMiddle, last, depth + 1);
					last = middle;
					middle -= blockLength;
					blockLength = binarySearchMid(first, middle, last);
				}
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
					"Block-swap merge range", annotationBegin);
			};

			for (std::size_t width = 1; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Block-swap merge width " + std::to_string(width));
				std::size_t first = 0;
				for (; first + width * 2 <= count; first += width * 2)
				{
					r.chargeWork();
					merge(first, first + width, first + width * 2, 0);
				}
				if (first + width < count)
					merge(first, first + width, count, 0);
				r.endPhase(phase, "Block-swap merge width " + std::to_string(width));
				r.addMetric("merge_passes");
				if (width > count / 2) break;
			}
		}

		static void weavedMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("weave_merge_ranges");
			r.declareMetric("weave_depth");
			r.declareMetric("weave_comparisons");
			r.resizeAuxiliary("weave", count);

			std::function<void(std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &merge, count](std::size_t residue, std::size_t modulus, std::size_t depth)
			{
				r.chargeWork();
				if (residue + modulus >= count) return;
				const std::string label = "Weaved merge stride " + std::to_string(modulus);
				const std::size_t annotationBegin = r.trace.events.size();
				const int phase = r.beginPhase(label);
				r.addMetric("weave_merge_ranges");
				r.setMetric("weave_depth", (std::max)(r.state.metrics.algorithm["weave_depth"], static_cast<std::uint64_t>(depth)));
				merge(residue, modulus * 2, depth + 1);
				merge(residue + modulus, modulus * 2, depth + 1);

				std::size_t left = residue, right = residue + modulus, output = residue;
				const std::size_t doubleModulus = modulus * 2;
				while (left < count && right < count)
				{
					r.chargeWork();
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right, "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("weave_comparisons");
					const ComparisonResult relation = compareItems(a, b, r.trace.direction);
					if (relation != ComparisonResult::Greater && !(relation == ComparisonResult::Equal && left > right))
					{
						r.writeAuxiliary("weave", output, a);
						left += doubleModulus;
					}
					else
					{
						r.writeAuxiliary("weave", output, b);
						right += doubleModulus;
					}
					output += modulus;
				}
				while (left < count)
				{
					r.chargeWork();
					r.writeAuxiliary("weave", output, r.at(left));
					left += doubleModulus;
					output += modulus;
				}
				while (right < count)
				{
					r.chargeWork();
					r.writeAuxiliary("weave", output, r.at(right));
					right += doubleModulus;
					output += modulus;
				}

				for (std::size_t index = residue; index < count; index += modulus)
				{
					r.chargeWork();
					r.writeMain(index, r.auxiliaryAt("weave", index));
				}
				r.annotate("main", RegionKind::Strided, residue, count, modulus, phase, label, annotationBegin);
				r.annotate("weave", RegionKind::Strided, residue, count, modulus, phase, label, annotationBegin);
				r.endPhase(phase, label);
			};

			merge(0, 1, 0);
		}

		static void twinMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("twin_pair_comparisons");
			r.declareMetric("twin_run_reversals");
			r.declareMetric("twin_merge_passes");
			r.declareMetric("twin_merged_ranges");
			r.declareMetric("twin_buffer_writes");

			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				r.addMetric("twin_pair_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto reverseRange = [&r](std::size_t first, std::size_t last, int phase)
			{
				const std::size_t begin = r.trace.events.size();
				std::size_t left = first, right = last;
				while (left < right)
				{
					r.chargeWork();
					r.swap(left++, right--, SemanticRole::WriteTarget);
				}
				r.addMetric("twin_run_reversals");
				r.annotate("main", RegionKind::Contiguous, first, last + 1, 1, phase,
					"Twin Sort reversed run", begin);
			};

			// The source first examines alternating adjacent pairs. Out-of-order
			// pairs are used to identify descending runs; strict boundary checks
			// keep equal items from being reversed across runs.
			const int pairPhase = r.beginPhase("Twin pair-run normalization");
			std::size_t index = 0;
			std::size_t end = count - 2;
			while (index <= end)
			{
				r.chargeWork();
				if (compareAt(index, index + 1) != ComparisonResult::Greater)
				{
					index += 2;
					continue;
				}

				const std::size_t start = index;
				index += 2;
				while (true)
				{
					if (index > end)
					{
						if (start == 0)
						{
						bool wholeInputDescending = count % 2 == 0;
						if (!wholeInputDescending)
							wholeInputDescending = compareAt(index - 1, index) == ComparisonResult::Greater;
						if (wholeInputDescending)
						{
							reverseRange(0, count - 1, pairPhase);
							r.endPhase(pairPhase, "Twin pair-run normalization");
							return;
						}
						}
						break;
					}

					if (compareAt(index, index + 1) == ComparisonResult::Greater)
					{
						if (compareAt(index - 1, index) == ComparisonResult::Greater)
						{
							index += 2;
							continue;
						}
						r.swap(index, index + 1);
					}
					break;
				}

				reverseRange(start, index - 1, pairPhase);
				index += 2;
			}
			r.endPhase(pairPhase, "Twin pair-run normalization");

			// Merge adjacent runs from the tail. Copying only the right run keeps
			// the item buffer at the source's n/2 capacity and lets primary writes
			// remain reversible identity moves.
			const std::string lane = "twin_buffer";
			r.resizeAuxiliary(lane, count / 2);
			for (std::size_t width = 2; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Twin tail-merge width " + std::to_string(width));
				for (std::size_t first = 0; first + width < count; first += width * 2)
				{
					r.chargeWork();
					const std::size_t mergeBegin = r.trace.events.size();
					const std::size_t middle = first + width;
					const std::size_t last = (std::min)(count, first + width * 2);
					if (compareAt(middle - 1, middle) != ComparisonResult::Greater) continue;

					const std::size_t rightCount = last - middle;
					for (std::size_t offset = 0; offset < rightCount; ++offset)
					{
						r.chargeWork();
						r.writeAuxiliary(lane, offset, r.at(middle + offset));
						r.addMetric("twin_buffer_writes");
					}

					std::size_t left = middle;
					std::size_t right = rightCount;
					std::size_t destination = last;
					while (left > first && right > 0)
					{
						r.chargeWork();
						const SortItem a = r.at(left - 1);
						const SortItem b = r.auxiliaryAt(lane, right - 1);
						r.compare(a, b, left - 1, right - 1, "main", lane,
							SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("twin_pair_comparisons");
						--destination;
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							r.writeMain(destination, r.at(--left));
						else
						r.writeMain(destination, r.auxiliaryAt(lane, --right));
					}
					while (right > 0)
					{
						r.chargeWork();
						r.writeMain(--destination, r.auxiliaryAt(lane, --right));
					}
					r.addMetric("twin_merged_ranges");
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
						"Twin tail merge", mergeBegin);
					r.annotate(lane, RegionKind::Contiguous, 0, rightCount, 1, phase,
						"Copied right run", mergeBegin);
				}
				r.endPhase(phase, "Twin tail-merge width " + std::to_string(width));
				r.addMetric("twin_merge_passes");
				if (width > count / 2) break;
			}
		}

		static std::size_t timMinRunLength(std::size_t length)
		{
			std::size_t remainder = 0;
			while (length >= 32)
			{
				remainder |= length & 1u;
				length >>= 1u;
			}
			return length + remainder;
		}

		static void timFamilySort(Recorder& r, bool cocktailSeedRuns)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("tim_runs");
			r.declareMetric("tim_natural_runs");
			r.declareMetric("tim_cocktail_runs");
			r.declareMetric("tim_run_reversals");
			r.declareMetric("tim_binary_insertions");
			r.declareMetric("tim_merge_passes");
			r.declareMetric("tim_merged_runs");
			r.declareMetric("tim_merge_comparisons");
			if (count < 2) return;

			const std::size_t minRun = timMinRunLength(count);
			r.setMetric("tim_min_run", minRun);
			std::vector<std::size_t> runBases, runLengths;
			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				return compareItems(a, b, r.trace.direction);
			};
			const auto binaryInsertSuffix = [&r](std::size_t base, std::size_t sortedEnd, std::size_t end)
			{
				for (std::size_t itemIndex = sortedEnd; itemIndex < end; ++itemIndex)
				{
					const SortItem key = r.at(itemIndex);
					r.hold(itemIndex);
					std::size_t low = base, high = itemIndex;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t middle = low + (high - low) / 2;
						const SortItem candidate = r.at(middle);
						r.compare(key, candidate, itemIndex, middle, "main", "main",
							SemanticRole::HeldKey, SemanticRole::ComparedItem);
						if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
						else low = middle + 1;
					}
					for (std::size_t position = itemIndex; position > low; --position) r.shift(position - 1, position);
					r.place(low, key);
					r.addMetric("tim_binary_insertions");
				}
			};
			const auto cocktailRun = [&r](std::size_t first, std::size_t end)
			{
				if (end <= first + 1) return;
				std::size_t low = first, high = end - 1;
				bool changed = true;
				while (changed && low < high)
				{
					changed = false;
					for (std::size_t i = low; i < high; ++i)
					{
						const SortItem a = r.at(i), b = r.at(i + 1);
						r.compare(a, b, i, i + 1);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i, i + 1); changed = true; }
					}
					if (!changed) break;
					--high;
					changed = false;
					for (std::size_t i = high; i > low; --i)
					{
						const SortItem a = r.at(i - 1), b = r.at(i);
						r.compare(a, b, i - 1, i);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i - 1, i); changed = true; }
					}
					++low;
				}
			};

			std::function<void(std::size_t)> mergeAt;
			mergeAt = [&r, &runBases, &runLengths, &mergeAt](std::size_t runIndex)
			{
				const std::size_t first = runBases[runIndex];
				const std::size_t leftLength = runLengths[runIndex];
				const std::size_t middle = first + leftLength;
				const std::size_t rightLength = runLengths[runIndex + 1];
				const std::size_t last = middle + rightLength;
				const std::string lane = "tim_buffer";
				const int phase = r.beginPhase("Tim run merge " + std::to_string(first) + "–" + std::to_string(last));
				const std::size_t begin = r.trace.events.size();
				const SortItem leftEnd = r.at(middle - 1), rightStart = r.at(middle);
				r.compare(leftEnd, rightStart, middle - 1, middle);
				r.addMetric("tim_merge_comparisons");
				if (compareItems(leftEnd, rightStart, r.trace.direction) == ComparisonResult::Greater)
				{
					if (r.state.auxiliaryItems.find(lane) == r.state.auxiliaryItems.end())
						r.resizeAuxiliary(lane, r.state.slots.size());
					for (std::size_t offset = 0; offset < leftLength; ++offset)
					{
						r.chargeWork();
						r.writeAuxiliary(lane, offset, r.at(first + offset));
					}
					std::size_t left = 0, right = middle, destination = first;
					while (left < leftLength && right < last)
					{
						r.chargeWork();
						const SortItem a = r.auxiliaryAt(lane, left), b = r.at(right);
						r.compare(a, b, left, right, lane, "main",
							SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						r.addMetric("tim_merge_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(destination++, r.auxiliaryAt(lane, left++));
						else r.writeMain(destination++, r.at(right++));
					}
					while (left < leftLength)
					{
						r.chargeWork();
						r.writeMain(destination++, r.auxiliaryAt(lane, left++));
					}
				}
				r.addMetric("tim_merged_runs");
				r.addMetric("tim_merge_passes");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Tim merged run", begin);
				if (r.state.auxiliaryItems.find(lane) != r.state.auxiliaryItems.end())
					r.annotate(lane, RegionKind::Contiguous, 0, leftLength, 1, phase, "Buffered left run", begin);
				r.endPhase(phase, "Tim run merge " + std::to_string(first) + "–" + std::to_string(last));
				runLengths[runIndex] += runLengths[runIndex + 1];
				runBases.erase(runBases.begin() + runIndex + 1);
				runLengths.erase(runLengths.begin() + runIndex + 1);
			};
			const auto collapseRuns = [&runBases, &runLengths, &mergeAt]()
			{
				while (runLengths.size() > 1)
				{
					std::size_t index = runLengths.size() - 2;
					const bool firstInvariant = index > 0 && runLengths[index - 1] <= runLengths[index] + runLengths[index + 1];
					const bool secondInvariant = index > 1 && runLengths[index - 2] <= runLengths[index - 1] + runLengths[index];
					if (firstInvariant || secondInvariant)
					{
						if (index > 0 && runLengths[index - 1] < runLengths[index + 1]) --index;
						mergeAt(index);
					}
					else if (runLengths[index] <= runLengths[index + 1]) mergeAt(index);
					else break;
				}
			};

			if (cocktailSeedRuns)
			{
				for (std::size_t first = 0; first < count; )
				{
					const std::size_t last = (std::min)(count, first + minRun);
					const int phase = r.beginPhase("Cocktail-built Tim run " + std::to_string(first) + "–" + std::to_string(last));
					cocktailRun(first, last);
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Cocktail-built run", r.trace.events.size());
					r.endPhase(phase, "Cocktail-built Tim run " + std::to_string(first) + "–" + std::to_string(last));
					r.addMetric("tim_cocktail_runs");
					r.addMetric("tim_runs");
					runBases.push_back(first);
					runLengths.push_back(last - first);
					collapseRuns();
					first = last;
				}
			}
			else
			{
				std::size_t first = 0;
				while (first < count)
				{
					std::size_t runEnd = first + 1;
					if (runEnd < count)
					{
						const bool descending = compareAt(first, runEnd) == ComparisonResult::Greater;
						++runEnd;
						while (runEnd < count)
						{
							r.chargeWork();
							const ComparisonResult order = compareAt(runEnd - 1, runEnd);
							if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
							++runEnd;
						}
						if (descending)
						{
							const std::size_t begin = r.trace.events.size();
							std::size_t left = first, right = runEnd - 1;
							while (left < right) r.swap(left++, right--, SemanticRole::WriteTarget);
							r.annotate("main", RegionKind::Contiguous, first, runEnd, 1, -1, "Reversed natural run", begin);
							r.addMetric("tim_run_reversals");
						}
					}
					const std::size_t naturalEnd = runEnd;
					const std::size_t targetEnd = (std::min)(count, (std::max)(naturalEnd, first + minRun));
					binaryInsertSuffix(first, naturalEnd, targetEnd);
					r.addMetric("tim_natural_runs");
					r.addMetric("tim_runs");
					runBases.push_back(first);
					runLengths.push_back(targetEnd - first);
					collapseRuns();
					first = targetEnd;
				}
			}
			while (runLengths.size() > 1)
			{
				std::size_t index = runLengths.size() - 2;
				if (index > 0 && runLengths[index - 1] < runLengths[index + 1]) --index;
				mergeAt(index);
			}
		}

		static void improvedInPlaceMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("inplace_merge_ranges");
			r.declareMetric("inplace_merge_comparisons");
			r.declareMetric("inplace_merge_rotations");
			r.declareMetric("inplace_merge_rotation_swaps");
			r.declareMetric("inplace_merge_insertion_runs");
			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				r.addMetric("inplace_merge_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last, int group)
			{
				if (first == middle || middle == last) return;
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.chargeWork();
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("inplace_merge_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.addMetric("inplace_merge_rotations");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, group, "Stable block rotation", begin);
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &compareAt, &rotate, &merge, count](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Improved In-Place Merge recursion exceeded its bounded depth.");
				if (compareAt(middle - 1, middle) != ComparisonResult::Greater) return;

				const std::size_t total = last - first;
				if (total == 2)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					return;
				}

				const int phase = r.beginPhase("Symmetric in-place merge " + std::to_string(first) + "–" + std::to_string(last));
				r.addMetric("inplace_merge_ranges");
				std::size_t firstCut = first, secondCut = middle;
				if (middle - first > last - middle)
				{
					firstCut = first + (middle - first) / 2;
					const SortItem pivot = r.at(firstCut);
					std::size_t low = middle, high = last;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, firstCut);
						r.addMetric("inplace_merge_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
						else high = probe;
					}
					secondCut = low;
				}
				else
				{
					secondCut = middle + (last - middle) / 2;
					const SortItem pivot = r.at(secondCut);
					std::size_t low = first, high = middle;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, secondCut);
						r.addMetric("inplace_merge_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
						else high = probe;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut, phase);
				merge(first, firstCut, newMiddle, depth + 1);
				merge(newMiddle, secondCut, last, depth + 1);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Symmetric in-place merge range", r.trace.events.size() > 0 ? r.trace.events.size() - 1 : 0);
				r.endPhase(phase, "Symmetric in-place merge " + std::to_string(first) + "–" + std::to_string(last));
			};

			std::function<void(std::size_t, std::size_t, std::size_t)> sort;
			sort = [&r, &sort, &merge](std::size_t first, std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				const std::size_t length = last - first;
				if (length < 2) return;
				if (length <= 16)
				{
					strategies::binaryInsertionRange(r, first, last);
					r.addMetric("inplace_merge_insertion_runs");
					return;
				}
				const std::size_t middle = first + length / 2;
				sort(first, middle, depth + 1);
				sort(middle, last, depth + 1);
				merge(first, middle, last, depth);
			};
			sort(0, count, 0);
		}

		static void dropMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "drop_merge_kept", "drop_merge_dropped", "drop_merge_scan_comparisons",
				"drop_merge_early_fallbacks", "drop_merge_suffix_heap_sorts", "drop_merge_final_merges" })
				r.declareMetric(key);
			if (count < 2) return;

			r.resizeAuxiliary("drop_merge_kept", count);
			r.resizeAuxiliary("drop_merge_dropped", count);
			r.resizeAuxiliary("drop_merge_output", count);
			std::size_t kept = 0, dropped = 0;
			const std::size_t scanBegin = r.trace.events.size();
			bool fellBack = false;
			for (std::size_t index = 0; index < count; ++index)
			{
				const SortItem candidate = r.at(index);
				bool belongsToRun = kept == 0;
				if (kept != 0)
				{
					const SortItem tail = r.auxiliaryAt("drop_merge_kept", kept - 1);
					r.compare(tail, candidate, kept - 1, index, "drop_merge_kept", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("drop_merge_scan_comparisons");
					belongsToRun = compareItems(tail, candidate, r.trace.direction) != ComparisonResult::Greater;
				}
				if (belongsToRun)
				{
					r.writeAuxiliary("drop_merge_kept", kept++, candidate);
					r.addMetric("drop_merge_kept");
				}
				else
				{
					r.writeAuxiliary("drop_merge_dropped", dropped++, candidate);
					r.addMetric("drop_merge_dropped");
				}

				const std::size_t processed = index + 1;
				if (processed >= (count + 3) / 4 && dropped * 5 > processed * 3)
				{
					// The source algorithm abandons its monotone subsequence when the
					// drop buffer grows too quickly. The original main array is still
					// untouched here, so the deterministic introspective fallback can
					// take over without reconstructing a partially compacted range.
					r.addMetric("drop_merge_early_fallbacks");
					r.annotate("main", RegionKind::Contiguous, 0, count, 1, -1,
						"Drop Merge early fallback", scanBegin);
					detail::traceHeapSortRange(r, 0, count);
					fellBack = true;
					break;
				}
			}
			if (!fellBack)
			{
				r.annotate("drop_merge_kept", RegionKind::Contiguous, 0, kept, 1, -1,
					"Drop Merge monotone subsequence", scanBegin);
				r.annotate("drop_merge_dropped", RegionKind::Contiguous, 0, dropped, 1, -1,
					"Drop Merge buffered items", scanBegin);
				if (dropped > 1)
				{
					for (std::size_t i = 0; i < dropped; ++i)
						r.writeMain(i, r.auxiliaryAt("drop_merge_dropped", i));
					detail::traceHeapSortRange(r, 0, dropped);
					r.addMetric("drop_merge_suffix_heap_sorts");
				}
				std::size_t left = 0, right = 0;
				const std::size_t mergeBegin = r.trace.events.size();
				for (std::size_t output = 0; output < count; ++output)
				{
					SortItem chosen;
					if (left >= kept) chosen = r.at(right++);
					else if (right >= dropped) chosen = r.auxiliaryAt("drop_merge_kept", left++);
					else
					{
						const SortItem a = r.auxiliaryAt("drop_merge_kept", left);
						const SortItem b = r.at(right);
						r.compare(a, b, left, right, "drop_merge_kept", "main",
							SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						r.addMetric("drop_merge_scan_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							chosen = r.auxiliaryAt("drop_merge_kept", left++);
						else chosen = r.at(right++);
					}
					r.writeAuxiliary("drop_merge_output", output, chosen);
				}
				r.addMetric("drop_merge_final_merges");
				r.annotate("main", RegionKind::Contiguous, 0, count, 1, -1,
					"Drop Merge final merge", mergeBegin);
				for (std::size_t i = 0; i < count; ++i)
					r.writeMain(i, r.auxiliaryAt("drop_merge_output", i));
			}
			r.resizeAuxiliary("drop_merge_kept", 0);
			r.resizeAuxiliary("drop_merge_dropped", 0);
			r.resizeAuxiliary("drop_merge_output", 0);
		}

		std::size_t medianOfMediansIndex(Recorder& r, std::size_t first, std::size_t end)
		{
			while (end - first > 5)
			{
				std::size_t medianWrite = first;
				for (std::size_t group = first; group < end; group += 5)
				{
					const std::size_t groupEnd = (std::min)(end, group + 5);
					strategies::binaryInsertionRange(r, group, groupEnd);
					const std::size_t median = group + (groupEnd - group - 1) / 2;
					if (medianWrite != median) r.swap(medianWrite, median, SemanticRole::Pivot);
					++medianWrite;
				}
				end = medianWrite;
			}
			strategies::binaryInsertionRange(r, first, end);
			return first + (end - first) / 2;
		}

		static void medianMergeRange(Recorder& r, std::size_t first, std::size_t end)
		{
			if (end - first < 2) return;
			const std::size_t runSize = 16;
			for (std::size_t run = first; run < end; run += runSize)
			{
				strategies::binaryInsertionRange(r, run, (std::min)(end, run + runSize));
				r.addMetric("median_merge_initial_runs");
			}
			for (std::size_t width = runSize; width < end - first;)
			{
				const int phase = r.beginPhase("Median Merge partition merge width " + std::to_string(width));
				for (std::size_t left = first; left < end; left += width * 2)
				{
					const std::size_t middle = (std::min)(end, left + width);
					const std::size_t right = (std::min)(end, middle + width);
					if (middle < right)
					{
						const std::size_t begin = r.trace.events.size();
						detail::traceMergeRange(r, left, middle, right);
						r.annotate("main", RegionKind::Contiguous, left, right, 1, phase,
							"Median Merge partition range", begin);
						r.addMetric("median_merge_merge_ranges");
					}
				}
				r.endPhase(phase, "Median Merge partition merge width");
				r.addMetric("median_merge_merge_passes");
				if (width > (end - first) / 2) break;
				width *= 2;
			}
		}

		static void medianMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "median_merge_partitions", "median_merge_comparisons", "median_merge_bad_partitions",
				"median_merge_median_of_medians_pivots", "median_merge_initial_runs", "median_merge_merge_ranges",
				"median_merge_merge_passes", "median_merge_maximum_depth", "median_merge_partition_fallbacks" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);

			struct Range { std::size_t first, end, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), end(b), depth(d) {} };
			std::vector<Range> pending;
			pending.push_back(Range(0, count, 0));
			while (!pending.empty())
			{
				Range range = pending.back();
				pending.pop_back();
				std::size_t first = range.first, end = range.end, depth = range.depth;
				bool useMedianOfMedians = false;
				while (end - first > 16)
				{
					r.setMetric("median_merge_maximum_depth", (std::max)(r.state.metrics.algorithm["median_merge_maximum_depth"],
						static_cast<std::uint64_t>(depth)));
					const std::size_t middle = first + (end - first) / 2;
					const std::size_t pivotIndex = useMedianOfMedians
						? medianOfMediansIndex(r, first, end)
						: detail::traceMedianOfThree(r, first, middle, end - 1);
					if (useMedianOfMedians) r.addMetric("median_merge_median_of_medians_pivots");
					const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
					const std::size_t split = detail::traceHoarePartition(r, first, end, pivotIndex);
					r.addMetric("median_merge_partitions");
					r.addMetric("median_merge_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					if (split <= first || split >= end)
					{
						r.addMetric("median_merge_partition_fallbacks");
						medianMergeRange(r, first, end);
						break;
					}
					const std::size_t leftLength = split - first, rightLength = end - split;
					const bool badlyBalanced = (std::min)(leftLength, rightLength) * 16 < (std::max)(leftLength, rightLength);
					if (badlyBalanced)
						r.addMetric("median_merge_bad_partitions");
					useMedianOfMedians = badlyBalanced;
					++depth;
					if (leftLength <= rightLength)
					{
						medianMergeRange(r, first, split);
						first = split;
					}
					else
					{
						medianMergeRange(r, split, end);
						end = split;
					}
				}
				if (end > first && end - first <= 16)
					strategies::binaryInsertionRange(r, first, end);
			}
			r.resizeAuxiliary("merge", 0);
		}

		static void bufferPartitionMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "buffer_partition_selections", "buffer_partition_select_passes", "buffer_partition_fallbacks",
				"buffer_partition_merges", "buffer_partition_comparisons" }) r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const auto selectRank = [&r](std::size_t first, std::size_t end, std::size_t target) -> std::size_t
			{
				std::size_t low = first, high = end;
				std::size_t depthLimit = 0;
				for (std::size_t size = end - first; size > 1; size >>= 1) ++depthLimit;
				depthLimit = depthLimit * 2 + 1;
				while (high - low > 1)
				{
					r.chargeWork();
					if (depthLimit == 0 || high - low <= 3)
					{
						if (depthLimit == 0) r.addMetric("buffer_partition_fallbacks");
						strategies::binaryInsertionRange(r, low, high);
						return target;
					}
					--depthLimit;
					const std::size_t middle = low + (high - low) / 2;
					const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
					const std::size_t pivot = detail::traceMedianOfThree(r, low, middle, high - 1);
					const std::size_t split = detail::traceHoarePartition(r, low, high, pivot);
					r.addMetric("buffer_partition_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					r.addMetric("buffer_partition_select_passes");
					if (split == target) return target;
					if (split <= low || split >= high)
					{
						r.addMetric("buffer_partition_fallbacks");
						detail::traceHeapSortRange(r, low, high);
						return target;
					}
					if (split < target) low = split;
					else high = split;
				}
				return target;
			};

			std::function<void(std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &selectRank](std::size_t first, std::size_t end)
			{
				r.chargeWork();
				if (end - first <= 16)
				{
					strategies::binaryInsertionRange(r, first, end);
					return;
				}
				const std::size_t middle = first + (end - first) / 2;
				r.addMetric("buffer_partition_selections");
				const std::size_t split = selectRank(first, end, middle);
				const std::size_t annotationBegin = r.trace.events.size();
				sortRange(first, split);
				sortRange(split, end);
				const int phase = r.beginPhase("Buffer-partitioned merge");
				detail::traceMergeRange(r, first, split, end);
				r.endPhase(phase, "Buffer-partitioned merge");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Median-selected merge range", annotationBegin);
				r.addMetric("buffer_partition_merges");
			};
			sortRange(0, count);
			r.resizeAuxiliary("merge", 0);
		}

		static void parallelBlockMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "parallel_block_runs", "parallel_block_tasks", "parallel_block_layers", "parallel_block_comparisons" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("parallel_block_runs");
			}
			for (std::size_t width = runSize; width < count;)
			{
				const int phase = r.beginPhase("Parallel block merge layer width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t begin = r.trace.events.size();
						const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
						detail::traceMergeRange(r, first, middle, last);
						r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Independent merge block", begin);
						r.addMetric("parallel_block_tasks");
						r.addMetric("parallel_block_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					}
				}
				r.endPhase(phase, "Parallel block merge layer");
				r.addMetric("parallel_block_layers");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("merge", 0);
		}

		static void optimizedRotateMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_rotate_runs", "optimized_rotate_merges", "optimized_rotate_buffer_merges",
				"optimized_rotate_in_place_merges", "optimized_rotate_buffer_writes", "optimized_rotate_comparisons",
				"optimized_rotate_trimmed_items" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t bufferCapacity = static_cast<std::size_t>((std::max)(1, (std::min)(256,
				r.trace.configuration.value("rotate_buffer_capacity", 64))));
			r.resizeAuxiliary("rotate_merge_buffer", bufferCapacity);

			const auto bufferedMerge = [&r, bufferCapacity](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const SortItem firstRight = r.at(middle);
				const SortItem lastLeft = r.at(middle - 1);
				r.compare(lastLeft, firstRight, middle - 1, middle, "main", "main");
				r.addMetric("optimized_rotate_comparisons");
				if (compareItems(lastLeft, firstRight, r.trace.direction) != ComparisonResult::Greater) return;

				// Trim the already ordered prefix and suffix around the merge. Equal left
				// items stay before equal right items, preserving stability.
				std::size_t low = first, high = middle;
				while (low < high)
				{
					const std::size_t probe = low + (high - low) / 2;
					const SortItem candidate = r.at(probe);
					r.probeItem("main", probe, candidate, SemanticRole::ProbedItem);
					r.compare(candidate, firstRight, probe, middle, "main", "main");
					r.addMetric("optimized_rotate_comparisons");
					if (compareItems(candidate, firstRight, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
					else high = probe;
				}
				const std::size_t mergeFirst = low;
				low = middle; high = last;
				while (low < high)
				{
					const std::size_t probe = low + (high - low) / 2;
					const SortItem candidate = r.at(probe);
					r.probeItem("main", probe, candidate, SemanticRole::ProbedItem);
					r.compare(candidate, lastLeft, probe, middle - 1, "main", "main");
					r.addMetric("optimized_rotate_comparisons");
					if (compareItems(candidate, lastLeft, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
					else high = probe;
				}
				const std::size_t mergeLast = low;
				r.addMetric("optimized_rotate_trimmed_items", (mergeFirst - first) + (last - mergeLast));
				const std::size_t leftCount = middle - mergeFirst;
				const std::size_t rightCount = mergeLast - middle;
				if (leftCount == 0 || rightCount == 0) return;
				if ((std::min)(leftCount, rightCount) > bufferCapacity)
				{
					// The source falls back to recursive binary splitting and rotations
					// when neither run fits its configured temporary array.
					grailMergeWithoutBuffer(r, mergeFirst, leftCount, rightCount, "optimized_rotate");
					r.addMetric("optimized_rotate_in_place_merges");
					return;
				}

				if (leftCount <= rightCount)
				{
					for (std::size_t offset = 0; offset < leftCount; ++offset)
					{
						r.writeAuxiliary("rotate_merge_buffer", offset, r.at(mergeFirst + offset));
						r.addMetric("optimized_rotate_buffer_writes");
					}
					std::size_t left = 0, right = middle, output = mergeFirst;
					while (left < leftCount && right < mergeLast)
					{
						const SortItem a = r.auxiliaryAt("rotate_merge_buffer", left);
						const SortItem b = r.at(right);
						r.compare(a, b, left, right, "rotate_merge_buffer", "main", SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						r.addMetric("optimized_rotate_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(output++, r.auxiliaryAt("rotate_merge_buffer", left++));
						else r.writeMain(output++, r.at(right++));
					}
					while (left < leftCount) r.writeMain(output++, r.auxiliaryAt("rotate_merge_buffer", left++));
				}
				else
				{
					for (std::size_t offset = 0; offset < rightCount; ++offset)
					{
						r.writeAuxiliary("rotate_merge_buffer", offset, r.at(middle + offset));
						r.addMetric("optimized_rotate_buffer_writes");
					}
					std::size_t left = middle, right = rightCount, output = mergeLast;
					while (left > mergeFirst && right > 0)
					{
						const SortItem a = r.at(left - 1);
						const SortItem b = r.auxiliaryAt("rotate_merge_buffer", right - 1);
						r.compare(a, b, left - 1, right - 1, "main", "rotate_merge_buffer", SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("optimized_rotate_comparisons");
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							r.writeMain(--output, r.at(--left));
						else r.writeMain(--output, r.auxiliaryAt("rotate_merge_buffer", --right));
					}
					while (right > 0) r.writeMain(--output, r.auxiliaryAt("rotate_merge_buffer", --right));
				}
				r.addMetric("optimized_rotate_buffer_merges");
			};

			const std::size_t run = 32;
			for (std::size_t first = 0; first < count; first += run)
			{
				const std::size_t last = (std::min)(count, first + run);
				strategies::binaryInsertionRange(r, first, last);
				r.addMetric("optimized_rotate_runs");
			}
		for (std::size_t width = run; width < count;)
			{
				const int phase = r.beginPhase("Optimized rotate-merge pass");
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t begin = r.trace.events.size();
						bufferedMerge(first, middle, last);
						r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Buffered or rotated merge", begin);
						r.addMetric("optimized_rotate_merges");
					}
				}
				r.endPhase(phase, "Optimized rotate-merge pass");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("rotate_merge_buffer", 0);
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

			void mergeInsertionBlockSwap(Recorder& r, std::size_t firstEnd, std::size_t secondEnd, std::size_t blockSize)
			{
				for (std::size_t offset = 0; offset < blockSize; ++offset)
					r.swap(firstEnd - offset, secondEnd - offset);
				r.addMetric("block_swaps");
			}

			void mergeInsertionBlockInsert(Recorder& r, std::size_t sourceEnd, std::size_t destinationEnd, std::size_t blockSize)
			{
				while (sourceEnd >= blockSize && sourceEnd - blockSize >= destinationEnd)
				{
					mergeInsertionBlockSwap(r, sourceEnd - blockSize, sourceEnd, blockSize);
					sourceEnd -= blockSize;
					r.addMetric("block_insertions");
				}
			}

			void mergeInsertionBlockReverse(Recorder& r, std::size_t firstEnd, std::size_t end, std::size_t blockSize)
			{
				if (end < blockSize) return;
				std::size_t lastEnd = end - blockSize;
				while (lastEnd > firstEnd)
				{
					mergeInsertionBlockSwap(r, firstEnd, lastEnd, blockSize);
					firstEnd += blockSize;
					if (lastEnd < blockSize) break;
					lastEnd -= blockSize;
					r.addMetric("block_reversals");
				}
			}

			std::size_t mergeInsertionBlockSearch(Recorder& r, const SortItem& key, std::size_t first,
				std::size_t end, std::size_t blockSize, std::size_t keyIndex)
			{
				while (first < end)
				{
					const std::size_t middle = first + (((end - first) / blockSize) / 2) * blockSize;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) end = middle;
					else first = middle + blockSize;
				}
				return first;
			}

			void mergeInsertionOrderBlocks(Recorder& r, std::size_t firstEnd, std::size_t end,
				std::size_t blockSize)
			{
				for (std::size_t leftEnd = firstEnd, rightEnd = leftEnd + blockSize;
					rightEnd < end; leftEnd += blockSize, rightEnd += 2 * blockSize)
					mergeInsertionBlockInsert(r, rightEnd, leftEnd, blockSize);
				const std::size_t middleEnd = firstEnd + (((end - firstEnd) / blockSize) / 2) * blockSize;
				mergeInsertionBlockReverse(r, middleEnd, end, blockSize);
				r.addMetric("block_orderings");
			}

			void mergeInsertionSort(Recorder& r)
			{
				const std::size_t count = r.state.slots.size();
				const char* metricKeys[] = { "block_swaps", "block_insertions", "block_reversals", "block_orderings", "block_searches", "block_levels" };
				for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
				std::size_t blockSize = 1;
				while (blockSize <= count / 2)
				{
					for (std::size_t end = 2 * blockSize - 1; end < count; end += 2 * blockSize)
					{
						const SortItem left = r.at(end - blockSize), right = r.at(end);
						r.compare(left, right, end - blockSize, end);
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
							mergeInsertionBlockSwap(r, end - blockSize, end, blockSize);
					}
					blockSize *= 2;
				}

				while (blockSize > 0)
				{
					r.addMetric("block_levels");
					const std::size_t firstEnd = blockSize - 1;
					std::size_t runEnd = firstEnd + 2 * blockSize;
					std::size_t group = 2, jacobsthal = 4;
					while (runEnd <= count && 2 * blockSize * group - blockSize <= count - runEnd)
					{
						mergeInsertionOrderBlocks(r, runEnd, runEnd + 2 * blockSize * group - blockSize, blockSize);
						const std::size_t searchEnd = firstEnd + blockSize * (jacobsthal - 1);
						runEnd += blockSize * group - blockSize;
						for (std::size_t keyIndex = runEnd; keyIndex < runEnd + blockSize * group; keyIndex += blockSize)
						{
							if (keyIndex >= count) break;
							const SortItem key = r.at(keyIndex);
							const std::size_t destination = mergeInsertionBlockSearch(r, key, firstEnd, searchEnd, blockSize, keyIndex);
							r.addMetric("block_searches");
							mergeInsertionBlockInsert(r, keyIndex, destination, blockSize);
						}
						runEnd += blockSize * group + blockSize;
						group = jacobsthal - group;
						jacobsthal *= 2;
					}
					while (runEnd < count)
					{
						const SortItem key = r.at(runEnd);
						const std::size_t destination = mergeInsertionBlockSearch(r, key, firstEnd, runEnd, blockSize, runEnd);
						r.addMetric("block_searches");
						mergeInsertionBlockInsert(r, runEnd, destination, blockSize);
						runEnd += 2 * blockSize;
					}
					blockSize /= 2;
				}
			}
		}

		void optimizedWeaveMergeStrategy(Recorder& r) { optimizedWeaveMergeSort(r); }
		void wikiMergeStrategy(Recorder& r) { wikiSort(r); }
		void andreyMergeStrategy(Recorder& r) { andreySort(r); }
		void newShuffleMergeStrategy(Recorder& r) { newShuffleMergeSort(r); }
		void yujiBufferedMergeStrategy(Recorder& r) { yujiBufferedMergeSort(r); }
		void kotaMergeStrategy(Recorder& r) { kotaSort(r); }
		void quadMergeStrategy(Recorder& r) { quadMergeSort(r); }
		void blockSwapMergeStrategy(Recorder& r) { blockSwapMergeSort(r); }
		void weavedMergeStrategy(Recorder& r) { weavedMergeSort(r); }
		void twinMergeStrategy(Recorder& r) { twinMergeSort(r); }
		void timMergeStrategy(Recorder& r) { timFamilySort(r, false); }
		void cocktailMergeStrategy(Recorder& r) { timFamilySort(r, true); }
		void improvedInPlaceMergeStrategy(Recorder& r) { improvedInPlaceMergeSort(r); }
		void dropMergeStrategy(Recorder& r) { dropMergeSort(r); }
		void medianMergeStrategy(Recorder& r) { medianMergeSort(r); }
		void bufferPartitionMergeStrategy(Recorder& r) { bufferPartitionMergeSort(r); }
		void parallelBlockMergeStrategy(Recorder& r) { parallelBlockMergeSort(r); }
		void optimizedRotateMergeStrategy(Recorder& r) { optimizedRotateMergeSort(r); }
		void mergeInsertionSortStrategy(Recorder& r) { mergeInsertionSort(r); }
		void remiSortStrategy(Recorder& r) { remiSort(r); }
		void chaliceSortStrategy(Recorder& r) { chaliceSort(r); }
		void fifthMergeSortStrategy(Recorder& r) { fifthMergeSort(r); }
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
