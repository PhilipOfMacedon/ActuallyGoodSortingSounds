#include "SortingLogic.h"
#include "TracePresentationPolicy.h"
#include "AlgorithmStrategies.h"
#include "PermutationTraceUtilities.h"
#include "SortingLogicInternal.h"
#include "TraceRecorder.h"
#include "MergeTraceUtilities.h"
#include "PartitionTraceUtilities.h"
#include "GrailTraceUtilities.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sortinglogic
{
	using detail::OperationLimitReached;
	using detail::Recorder;
	using detail::TraceBudgetReached;
	using detail::effectiveDomain;
	using detail::require;
	using detail::grailBinarySearch;
	using detail::grailRotate;
	using detail::grailMergeWithoutBuffer;
	using detail::grailBufferedMerge;

	namespace
	{
		bool precedes(const SortItem& a, const SortItem& b, SortDirection direction)
		{
			return compareItems(a, b, direction) == ComparisonResult::Less;
		}









		void asynchronousSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("asynchronous_key_passes");
			r.declareMetric("asynchronous_source_probes");
			r.declareMetric("asynchronous_emitted_items");
			if (count == 0) return;

			const std::string lane = "asynchronous_scan";
			const std::string consumedLane = "asynchronous_consumed";
			r.resizeAuxiliary(lane, count);
			r.resizeCounters(consumedLane, count);
			const auto valueRank = [&r](const SortItem& item) -> std::int64_t
			{
				return r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(item.integerKey) : -static_cast<std::int64_t>(item.integerKey);
			};
			std::int64_t minimumRank = valueRank(r.at(0)), maximumRank = minimumRank;
			for (std::size_t i = 0; i < count; ++i)
			{
				r.chargeWork();
				const SortItem item = r.at(i);
				r.writeAuxiliary(lane, i, item);
				const std::int64_t rank = valueRank(item);
				minimumRank = (std::min)(minimumRank, rank);
				maximumRank = (std::max)(maximumRank, rank);
			}

			std::uint64_t completedPasses = 0, probes = 0;
			std::size_t output = 0;
			for (std::int64_t currentRank = minimumRank; currentRank <= maximumRank; ++currentRank)
			{
				const int phase = r.beginPhase("Asynchronous value pass " + std::to_string(currentRank));
				for (std::size_t source = 0; source < count; ++source)
				{
					r.probeAuxiliary(lane, source, SemanticRole::ProbedItem);
					const SortItem item = r.auxiliaryAt(lane, source);
					if (r.counter(consumedLane, source) == 0 && valueRank(item) <= currentRank)
					{
						r.writeMain(output++, item, SemanticRole::WriteTarget);
						r.writeCounter(consumedLane, source, 1);
					}
				}
				r.endPhase(phase, "Asynchronous value pass " + std::to_string(currentRank));
				++completedPasses;
				probes += count;
				r.setMetric("asynchronous_key_passes", completedPasses);
				r.setMetric("asynchronous_source_probes", probes);
				r.setMetric("asynchronous_emitted_items", output);
			}
			if (output != count) throw std::logic_error("Asynchronous Sort did not emit every input item.");
			r.resizeAuxiliary(lane, 0);
			r.resizeCounters(consumedLane, 0);
		}

		void flanSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "flan_partitions", "flan_pivot_comparisons", "flan_comparisons", "flan_equal_items",
				"flan_insertion_fallbacks", "flan_maximum_depth" }) r.declareMetric(key);
			if (count < 2) return;
			struct Range { std::size_t first, last, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), last(b), depth(d) {} };
			std::vector<Range> pending(1, Range(0, count, 0));
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				r.setMetric("flan_maximum_depth", (std::max)(r.state.metrics.algorithm["flan_maximum_depth"], static_cast<std::uint64_t>(range.depth)));
				if (range.last - range.first <= 24)
				{
					strategies::binaryInsertionRange(r, range.first, range.last); r.addMetric("flan_insertion_fallbacks"); continue;
				}
				const std::size_t n = range.last - range.first;
				std::vector<std::size_t> positions;
				for (std::size_t i = 0; i < 9; ++i) positions.push_back(range.first + ((n - 1) * i) / 8);
				const SortItem pivot = detail::sampledMedian(r, positions, "flan").first;
				std::size_t less = range.first, scan = range.first, greater = range.last;
				const std::size_t pivotIndex = positions[4];
				const std::size_t partitionBegin = r.trace.events.size();
				const int phase = r.beginPhase("Flan three-way ninther partition");
				while (scan < greater)
				{
					r.chargeWork();
					const SortItem item = r.at(scan);
					r.compare(item, pivot, scan, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("flan_comparisons");
					const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
					if (order == ComparisonResult::Less) { r.swap(less++, scan++, SemanticRole::WriteTarget); }
					else if (order == ComparisonResult::Greater) { r.swap(scan, --greater, SemanticRole::WriteTarget); }
					else { ++scan; r.addMetric("flan_equal_items"); }
				}
				r.addMetric("flan_partitions");
				r.annotate("main", RegionKind::Contiguous, range.first, less, 1, phase, "Less than pivot", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, less, greater, 1, phase, "Pivot equals", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, greater, range.last, 1, phase, "Greater than pivot", partitionBegin);
				r.endPhase(phase, "Flan partition complete");
				const Range left(range.first, less, range.depth + 1), right(greater, range.last, range.depth + 1);
				if (left.last - left.first > right.last - right.first) { pending.push_back(left); pending.push_back(right); }
				else { pending.push_back(right); pending.push_back(left); }
			}
		}

		void fluxSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "flux_analyzed_transitions", "flux_descending_transitions", "flux_sorted_fast_paths",
				"flux_reversed_fast_paths", "flux_near_ordered_fallbacks", "flux_partitions", "flux_pivot_comparisons",
				"flux_comparisons", "flux_equal_items", "flux_insertion_fallbacks", "flux_maximum_depth" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t descents = 0;
			for (std::size_t i = 1; i < count; ++i)
			{
				r.chargeWork();
				const SortItem a = r.at(i - 1), b = r.at(i);
				r.compare(a, b, i - 1, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("flux_analyzed_transitions");
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
				{
					++descents;
					r.addMetric("flux_descending_transitions");
				}
			}
			if (descents == 0) { r.addMetric("flux_sorted_fast_paths"); return; }
			if (descents == count - 1)
			{
				for (std::size_t left = 0, right = count - 1; left < right; ++left, --right) r.swap(left, right, SemanticRole::WriteTarget);
				r.addMetric("flux_reversed_fast_paths"); return;
			}
			if (descents <= count / 6 || descents >= (count * 5) / 6)
			{
				strategies::binaryInsertionRange(r, 0, count); r.addMetric("flux_near_ordered_fallbacks"); return;
			}
			struct Range { std::size_t first, last, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), last(b), depth(d) {} };
			std::vector<Range> pending(1, Range(0, count, 0));
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				r.setMetric("flux_maximum_depth", (std::max)(r.state.metrics.algorithm["flux_maximum_depth"], static_cast<std::uint64_t>(range.depth)));
				if (range.last - range.first <= 20)
				{
					strategies::binaryInsertionRange(r, range.first, range.last); r.addMetric("flux_insertion_fallbacks"); continue;
				}
				const std::size_t n = range.last - range.first;
				const std::vector<std::size_t> positions = { range.first, range.first + n / 4, range.first + n / 2,
					range.first + (3 * n) / 4, range.last - 1 };
				const SortItem pivot = detail::sampledMedian(r, positions, "flux").first;
				const std::size_t pivotIndex = positions[2];
				std::size_t less = range.first, scan = range.first, greater = range.last;
				const std::size_t partitionBegin = r.trace.events.size();
				const int phase = r.beginPhase("Flux median-of-five partition");
				while (scan < greater)
				{
					r.chargeWork();
					const SortItem item = r.at(scan);
					r.compare(item, pivot, scan, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("flux_comparisons");
					const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
					if (order == ComparisonResult::Less) r.swap(less++, scan++, SemanticRole::WriteTarget);
					else if (order == ComparisonResult::Greater) r.swap(scan, --greater, SemanticRole::WriteTarget);
					else { ++scan; r.addMetric("flux_equal_items"); }
				}
				r.addMetric("flux_partitions");
				r.annotate("main", RegionKind::Contiguous, range.first, less, 1, phase, "Flux lower partition", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, less, greater, 1, phase, "Flux pivot band", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, greater, range.last, 1, phase, "Flux upper partition", partitionBegin);
				r.endPhase(phase, "Flux partition complete");
				const Range left(range.first, less, range.depth + 1), right(greater, range.last, range.depth + 1);
				if (left.last - left.first > right.last - right.first) { pending.push_back(left); pending.push_back(right); }
				else { pending.push_back(right); pending.push_back(left); }
			}
		}

		void quadMergeSort(Recorder& r)
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

		void blockSwapMergeSort(Recorder& r)
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

		void weavedMergeSort(Recorder& r)
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

		void twinMergeSort(Recorder& r)
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

		std::size_t timMinRunLength(std::size_t length)
		{
			std::size_t remainder = 0;
			while (length >= 32)
			{
				remainder |= length & 1u;
				length >>= 1u;
			}
			return length + remainder;
		}

		void timFamilySort(Recorder& r, bool cocktailSeedRuns)
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

		void improvedInPlaceMergeSort(Recorder& r)
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

		void blockInsertionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("block_insertion_runs");
			r.declareMetric("block_insertion_run_reversals");
			r.declareMetric("block_insertion_short_insertions");
			r.declareMetric("block_insertion_merges");
			r.declareMetric("block_insertion_comparisons");
			r.declareMetric("block_insertion_rotations");
			r.declareMetric("block_insertion_rotation_swaps");
			if (count < 2) return;

			const auto compareAt = [&r](std::size_t left, std::size_t right) -> ComparisonResult
			{
				const SortItem a = r.at(left), b = r.at(right);
				r.compare(a, b, left, right);
				r.addMetric("block_insertion_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last, int phase)
			{
				if (first == middle || middle == last) return;
				const std::size_t eventBegin = r.trace.events.size();
				const auto reverse = [&r](std::size_t begin, std::size_t end)
				{
					while (begin < end)
					{
						r.chargeWork();
						r.swap(begin++, --end, SemanticRole::WriteTarget);
						r.addMetric("block_insertion_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.addMetric("block_insertion_rotations");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Block inserted into sorted prefix", eventBegin);
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> mergeRun;
			mergeRun = [&r, &compareAt, &rotate, &mergeRun, count](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Block Insertion merge exceeded its bounded recursion depth at [" +
					std::to_string(first) + "," + std::to_string(middle) + "," + std::to_string(last) + ").");
				if (compareAt(middle - 1, middle) != ComparisonResult::Greater) return;
				if (last - first == 2)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_merges");
					return;
				}

				const int phase = r.beginPhase("Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
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
						r.compare(candidate, pivot, probe, firstCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
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
						r.compare(candidate, pivot, probe, secondCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
						else high = probe;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut, phase);
				r.addMetric("block_insertion_merges");
				mergeRun(first, firstCut, newMiddle, depth + 1);
				mergeRun(newMiddle, secondCut, last, depth + 1);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Merged monotone block", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
				r.endPhase(phase, "Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
			};

			const auto insertShortRun = [&r](std::size_t keyIndex)
			{
				const SortItem key = r.at(keyIndex);
				r.hold(keyIndex);
				std::size_t low = 0, high = keyIndex;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = keyIndex; position > low; --position) r.shift(position - 1, position);
				r.place(low, key);
				r.addMetric("block_insertion_short_insertions");
			};

			std::size_t runStart = 0;
			bool firstRun = true;
			while (runStart < count)
			{
				std::size_t runEnd = runStart + 1;
				bool descending = false;
				if (runEnd < count)
				{
					descending = compareAt(runStart, runEnd) == ComparisonResult::Greater;
					++runEnd;
					while (runEnd < count)
					{
						const ComparisonResult order = compareAt(runEnd - 1, runEnd);
						if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
						r.chargeWork();
						++runEnd;
					}
				}

				if (descending)
				{
					const std::size_t eventBegin = r.trace.events.size();
					for (std::size_t left = runStart, right = runEnd - 1; left < right; ++left, --right)
						r.swap(left, right, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_run_reversals");
					r.annotate("main", RegionKind::Contiguous, runStart, runEnd, 1, -1,
						"Reversed strict monotone run", eventBegin);
				}
				r.addMetric("block_insertion_runs");

				if (firstRun)
				{
					firstRun = false;
				}
				else if (runEnd - runStart < 3)
				{
					for (std::size_t index = runStart; index < runEnd; ++index)
						insertShortRun(index);
				}
				else
				{
					const int phase = r.beginPhase("Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
					mergeRun(0, runStart, runEnd, 0);
					r.annotate("main", RegionKind::Contiguous, 0, runEnd, 1, phase,
						"Sorted prefix grows by run merge", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
					r.endPhase(phase, "Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
				}
				runStart = runEnd;
			}
		}

		void dropMergeSort(Recorder& r)
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

		void medianMergeRange(Recorder& r, std::size_t first, std::size_t end)
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

		void medianMergeSort(Recorder& r)
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

		void bufferPartitionMergeSort(Recorder& r)
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

		void parallelBlockMergeSort(Recorder& r)
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

		void optimizedRotateMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_rotate_runs", "optimized_rotate_merges", "optimized_rotate_buffer_merges",
				"optimized_rotate_in_place_merges", "optimized_rotate_buffer_writes", "optimized_rotate_comparisons",
				"optimized_rotate_trimmed_items" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t bufferCapacity = static_cast<std::size_t>((std::max)(1, (std::min)(256, r.trace.configuration.rotateMergeBufferSize)));
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

		void patienceSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "pile_count", "pile_assignments", "maximum_pile_height", "heap_pushes", "heap_pops" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			std::vector<std::string> lanes;
			std::vector<std::size_t> pileSizes;
			std::vector<std::size_t> pileMaximumSizes;
			std::vector<std::size_t> pileBeginCursors;
			std::map<std::uint64_t, std::size_t> originalPositions;
			for (std::size_t i = 0; i < r.trace.input.size(); ++i) originalPositions[r.trace.input[i].stableId] = i;

			for (std::size_t inputIndex = 0; inputIndex < count; ++inputIndex)
			{
				const SortItem item = r.at(inputIndex);
				std::size_t low = 0, high = lanes.size();
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const std::size_t topIndex = pileSizes[middle] - 1;
					const SortItem top = r.auxiliaryAt(lanes[middle], topIndex);
					r.compare(item, top, inputIndex, topIndex, "main", lanes[middle], SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy);
					if (compareItems(item, top, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				if (low == lanes.size())
				{
					lanes.push_back("patience.pile." + std::to_string(lanes.size()));
					pileSizes.push_back(0);
					pileMaximumSizes.push_back(0);
					pileBeginCursors.push_back(r.trace.events.size());
				}
				const std::string& lane = lanes[low];
				const std::size_t insertionCursor = r.trace.events.size();
				r.writeAuxiliary(lane, pileSizes[low], item);
				++pileSizes[low];
				pileMaximumSizes[low] = std::max(pileMaximumSizes[low], pileSizes[low]);
				pileBeginCursors[low] = std::min(pileBeginCursors[low], insertionCursor);
				r.addMetric("pile_assignments");
				r.setMetric("pile_count", lanes.size());
				r.setMetric("maximum_pile_height", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_pile_height"], pileSizes[low]));
			}

			const std::string heapLane = "patience.priority_queue";
			r.resizeCounters(heapLane, 0);
			const auto pileHasPriority = [&r, &lanes, &pileSizes, &originalPositions](std::size_t firstPile, std::size_t secondPile)
			{
				const std::size_t firstIndex = pileSizes[firstPile] - 1;
				const std::size_t secondIndex = pileSizes[secondPile] - 1;
				const SortItem first = r.auxiliaryAt(lanes[firstPile], firstIndex);
				const SortItem second = r.auxiliaryAt(lanes[secondPile], secondIndex);
				r.compare(first, second, firstIndex, secondIndex, lanes[firstPile], lanes[secondPile],
					SemanticRole::AuxiliaryCopy, SemanticRole::AuxiliaryCopy);
				const ComparisonResult order = compareItems(first, second, r.trace.direction);
				if (order != ComparisonResult::Equal) return order == ComparisonResult::Less;
				return originalPositions[first.stableId] < originalPositions[second.stableId];
			};
			const auto exchangeHeapEntries = [&r, &heapLane](std::size_t first, std::size_t second)
			{
				const int firstPile = r.counter(heapLane, first), secondPile = r.counter(heapLane, second);
				r.writeCounter(heapLane, first, secondPile);
				r.writeCounter(heapLane, second, firstPile);
			};

			std::size_t heapSize = 0;
			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
			{
				r.resizeCounters(heapLane, heapSize + 1);
				r.writeCounter(heapLane, heapSize, static_cast<int>(pile));
				std::size_t child = heapSize++;
				while (child > 0)
				{
					const std::size_t parent = (child - 1) / 2;
					const std::size_t childPile = static_cast<std::size_t>(r.counter(heapLane, child));
					const std::size_t parentPile = static_cast<std::size_t>(r.counter(heapLane, parent));
					if (!pileHasPriority(childPile, parentPile)) break;
					exchangeHeapEntries(parent, child);
					child = parent;
				}
				r.addMetric("heap_pushes");
			}

			const auto siftHeapDown = [&r, &heapLane, &pileHasPriority, &exchangeHeapEntries](std::size_t root, std::size_t size)
			{
				for (;;)
				{
					const std::size_t leftChild = root * 2 + 1;
					if (leftChild >= size) return;
					std::size_t best = leftChild;
					const std::size_t rightChild = leftChild + 1;
					if (rightChild < size && pileHasPriority(
						static_cast<std::size_t>(r.counter(heapLane, rightChild)),
						static_cast<std::size_t>(r.counter(heapLane, leftChild)))) best = rightChild;
					if (!pileHasPriority(static_cast<std::size_t>(r.counter(heapLane, best)),
						static_cast<std::size_t>(r.counter(heapLane, root)))) return;
					exchangeHeapEntries(root, best);
					root = best;
				}
			};

			for (std::size_t output = 0; output < count; ++output)
			{
				const std::size_t chosenPile = static_cast<std::size_t>(r.counter(heapLane, 0));
				const std::size_t topIndex = pileSizes[chosenPile] - 1;
				const SortItem chosen = r.auxiliaryAt(lanes[chosenPile], topIndex);
				r.writeMain(output, chosen);
				r.resizeAuxiliary(lanes[chosenPile], topIndex);
				--pileSizes[chosenPile];
				r.addMetric("heap_pops");
				if (pileSizes[chosenPile] == 0)
				{
					if (heapSize == 1)
					{
						r.resizeCounters(heapLane, 0);
						heapSize = 0;
					}
					else
					{
						const int lastPile = r.counter(heapLane, heapSize - 1);
						r.writeCounter(heapLane, 0, lastPile);
						r.resizeCounters(heapLane, heapSize - 1);
						--heapSize;
						siftHeapDown(0, heapSize);
					}
				}
				else siftHeapDown(0, heapSize);
			}

			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
				r.annotate(lanes[pile], RegionKind::Group, 0, pileMaximumSizes[pile], 1, static_cast<int>(pile),
					"Patience pile " + std::to_string(pile + 1), pileBeginCursors[pile]);
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

		void stablePermutationSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const std::string indexLane = "stable_permutation_indices";
			r.declareMetric("index_permutation_swaps");
			r.declareMetric("permutations_examined");
			r.resizeCounters(indexLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeCounter(indexLane, i, static_cast<int>(i));
			const auto isSorted = [&r, count]()
			{
				r.addMetric("permutations_examined");
				for (std::size_t i = 1; i < count; ++i)
				{
					const SortItem previous = r.at(i - 1), current = r.at(i);
					r.compare(previous, current, i - 1, i);
					if (compareItems(previous, current, r.trace.direction) == ComparisonResult::Greater) return false;
				}
				return true;
			};
			std::function<bool(std::size_t)> permute;
			permute = [&r, &permute, &isSorted, &indexLane](std::size_t length)
			{
				if (length < 2) return isSorted();
				for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(length) - 2; i >= 0; --i)
				{
					if (permute(length - 1)) return true;
					const std::size_t first = static_cast<std::size_t>(i);
					const std::size_t second = length - 1;
					const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, first));
					const std::size_t secondIndex = static_cast<std::size_t>(r.counter(indexLane, second));
					r.swap(firstIndex, secondIndex);
					const int firstValue = r.counter(indexLane, first);
					const int secondValue = r.counter(indexLane, second);
					r.writeCounter(indexLane, first, secondValue);
					r.writeCounter(indexLane, second, firstValue);
					r.addMetric("index_permutation_swaps");
				}
				if (permute(length - 1)) return true;
				const int tail = r.counter(indexLane, length - 1);
				for (std::size_t i = length - 1; i > 0; --i)
					r.writeCounter(indexLane, i, r.counter(indexLane, i - 1));
				r.writeCounter(indexLane, 0, tail);
				const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, 0));
				r.hold(firstIndex);
				for (std::size_t i = 1; i < length; ++i)
				{
					const std::size_t destination = static_cast<std::size_t>(r.counter(indexLane, i - 1));
					const std::size_t source = static_cast<std::size_t>(r.counter(indexLane, i));
					r.shift(source, destination);
				}
				const std::size_t finalIndex = static_cast<std::size_t>(r.counter(indexLane, length - 1));
				r.place(finalIndex, r.state.heldItem);
				r.addMetric("permutations_examined");
				return false;
			};
			permute(count);
			r.resizeCounters(indexLane, 0);
		}

		void tableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const std::string tableLane = "table_indices";
			const char* metricKeys[] = { "partitions", "maximum_depth", "table_index_swaps", "permutation_cycles", "permutation_cycle_moves" };
			for (std::size_t metric = 0; metric < sizeof(metricKeys) / sizeof(metricKeys[0]); ++metric)
				r.declareMetric(metricKeys[metric]);
			r.resizeCounters(tableLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeCounter(tableLane, i, static_cast<int>(i));
			const auto stableGreater = [&r, &tableLane](std::size_t first, std::size_t second)
			{
				const std::size_t firstIndex = static_cast<std::size_t>(r.counter(tableLane, first));
				const std::size_t secondIndex = static_cast<std::size_t>(r.counter(tableLane, second));
				const SortItem a = r.at(firstIndex), b = r.at(secondIndex);
				r.compare(a, b, firstIndex, secondIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return order == ComparisonResult::Greater ||
					(order == ComparisonResult::Equal && firstIndex > secondIndex);
			};
			const auto tableSwap = [&r, &tableLane](std::size_t first, std::size_t second)
			{
				if (first == second) return;
				const int firstValue = r.counter(tableLane, first), secondValue = r.counter(tableLane, second);
				r.writeCounter(tableLane, first, secondValue);
				r.writeCounter(tableLane, second, firstValue);
				r.addMetric("table_index_swaps");
			};
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &stableGreater, &tableSwap, &tableLane](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				if (end - first < 3)
				{
					if (stableGreater(first, first + 1)) tableSwap(first, first + 1);
					return;
				}
				r.addMetric("partitions");
				const std::size_t begin = r.trace.events.size();
				const std::size_t middle = first + (end - 1 - first) / 2;
				if (stableGreater(first, middle)) tableSwap(first, middle);
				bool medianIsReady = false;
				if (stableGreater(middle, end - 1))
				{
					tableSwap(middle, end - 1);
					medianIsReady = stableGreater(first, middle);
				}
				if (!medianIsReady) tableSwap(first, middle);
				const std::size_t pivot = first;
				std::size_t left = first + 1, right = end;
				for (;;)
				{
					while (left < right && !stableGreater(left, pivot)) ++left;
					if (right == 0) break;
					--right;
					while (right >= left && stableGreater(right, pivot))
					{
						if (right == 0) break;
						--right;
					}
					if (left < right) tableSwap(left++, right);
					else break;
				}
				tableSwap(first, right);
				const std::size_t split = right;
				r.annotate(tableLane, RegionKind::Contiguous, first, end, 1, -1, "Indirect table partition", begin);
				if (split > first) sortRange(first, split, depth + 1);
				if (split + 1 < end) sortRange(split + 1, end, depth + 1);
			};
			if (count > 1) sortRange(0, count, 1);
			std::set<int> tablePermutation;
			for (std::size_t i = 0; i < count; ++i)
			{
				const int mapped = r.counter(tableLane, i);
				if (mapped < 0 || static_cast<std::size_t>(mapped) >= count || !tablePermutation.insert(mapped).second)
					throw std::logic_error("Table Sort produced a non-permutation index table.");
			}
			for (std::size_t first = 0; first < count; ++first)
			{
				const int mapped = r.counter(tableLane, first);
				if (mapped == static_cast<int>(first)) continue;
				const SortItem held = r.at(first);
				r.hold(first);
				std::size_t current = first;
				std::size_t next = static_cast<std::size_t>(mapped);
				std::vector<std::size_t> cyclePositions(1, first);
				do
				{
					r.shift(next, current);
					current = next;
					cyclePositions.push_back(current);
					require(cyclePositions.size() <= count, "Table Sort encountered a non-closing permutation cycle.");
					next = static_cast<std::size_t>(r.counter(tableLane, current));
				} while (next != first);
				r.place(current, held);
				for (std::size_t move = 0; move < cyclePositions.size(); ++move)
					r.writeCounter(tableLane, cyclePositions[move], static_cast<int>(cyclePositions[move]));
				r.addMetric("permutation_cycles");
				r.addMetric("permutation_cycle_moves", cyclePositions.size() - 1);
			}
			r.resizeCounters(tableLane, 0);
		}

		typedef void (*AlgorithmStrategy)(Recorder&);

		SortTrace buildSelectedAlgorithm(const TraceBuildRequest& request, SortAlgorithm algorithm,
			AlgorithmStrategy strategy = nullptr)
		{
			std::unique_ptr<Recorder> recorderStorage;
			try
			{
				recorderStorage.reset(new Recorder(request));
			}
			catch (const TraceBudgetReached& limit)
			{
				SortTrace failed;
				failed.algorithm = algorithm;
				const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
				if (descriptor) { failed.familyId = descriptor->familyId; failed.variantId = descriptor->variantId; }
				failed.direction = request.direction;
				failed.domain = effectiveDomain(request);
				failed.profile = inputProfileForDomain(failed.domain);
				failed.input = request.input;
				failed.initialState = makeInitialState(request.input);
				failed.snapshots.insert(std::make_pair(static_cast<std::size_t>(0), failed.initialState));
				failed.complete = false;
				failed.completionReason = limit.reason;
				return failed;
			}
			Recorder& recorder = *recorderStorage;
			try
			{
				if (strategy != nullptr) strategy(recorder);
				else switch (algorithm)
				{
				case SortAlgorithm::ArrayvAsynchronous: asynchronousSort(recorder); break;





				case SortAlgorithm::ArrayvQuadMerge: quadMergeSort(recorder); break;
				case SortAlgorithm::ArrayvBufferPartitionMerge: bufferPartitionMergeSort(recorder); break;
				case SortAlgorithm::ArrayvParallelBlockMerge: parallelBlockMergeSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedRotateMerge: optimizedRotateMergeSort(recorder); break;
				case SortAlgorithm::ArrayvDropMerge: dropMergeSort(recorder); break;
				case SortAlgorithm::ArrayvMedianMerge: medianMergeSort(recorder); break;


				case SortAlgorithm::ArrayvFlan: flanSort(recorder); break;
				case SortAlgorithm::ArrayvFlux: fluxSort(recorder); break;






				case SortAlgorithm::ArrayvBlockInsertion: blockInsertionSort(recorder); break;
				case SortAlgorithm::ArrayvPatience: patienceSort(recorder); break;
				case SortAlgorithm::ArrayvMergeInsertion: mergeInsertionSort(recorder); break;
				case SortAlgorithm::ArrayvBlockSwapMerge: blockSwapMergeSort(recorder); break;
				case SortAlgorithm::ArrayvWeavedMerge: weavedMergeSort(recorder); break;
				case SortAlgorithm::ArrayvTwinMerge: twinMergeSort(recorder); break;
				case SortAlgorithm::ArrayvTim: timFamilySort(recorder, false); break;
				case SortAlgorithm::ArrayvCocktailMerge: timFamilySort(recorder, true); break;
				case SortAlgorithm::ArrayvImprovedInPlaceMerge: improvedInPlaceMergeSort(recorder); break;
				case SortAlgorithm::ArrayvStablePermutation: stablePermutationSort(recorder); break;
				case SortAlgorithm::ArrayvTable: tableSort(recorder); break;
				default: throw std::invalid_argument("The requested sorting algorithm is not registered.");
				}
				recorder.finish();
			}
			catch (const OperationLimitReached&)
			{
				recorder.finishPartial("Global trace operation budget reached (" + std::to_string(request.maxOperations) + " operations).");
			}
			catch (const TraceBudgetReached& limit)
			{
				recorder.finishPartial(limit.reason);
			}
			if (algorithm == SortAlgorithm::ArrayvBufferedStooge && recorder.trace.complete)
			{
				bool ordered = true;
				for (std::size_t i = 1; i < recorder.state.slots.size(); ++i)
					if (compareItems(recorder.state.slots[i - 1].item, recorder.state.slots[i].item, request.direction) == ComparisonResult::Greater)
					{
						ordered = false;
						break;
					}
				if (!ordered) recorder.trace.outcome = AlgorithmOutcome::IncompleteDemonstration;
			}
			sortinglogic::populateAudioEventIndices(recorder.trace);
			std::string reason;
			if (!validateState(recorder.state, request.input.size(), &reason)) throw std::logic_error(reason.c_str());
			return recorder.trace;
		}

#define SORT_REQUEST_BUILDER(name, id) \
		SortTrace name(const TraceBuildRequest& request) { return buildSelectedAlgorithm(request, SortAlgorithm::id); }
#define SORT_STRATEGY_REQUEST_BUILDER(name, id, strategy) \
		SortTrace name(const TraceBuildRequest& request) { return buildSelectedAlgorithm(request, SortAlgorithm::id, strategy); }
		SORT_STRATEGY_REQUEST_BUILDER(buildInsertionRequest, Insertion, &strategies::insertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildBubbleRequest, Bubble, &strategies::bubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildSelectionRequest, Selection, &strategies::selection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBingoRequest, ArrayvBingo, &strategies::bingo)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDoubleSelectionRequest, ArrayvDoubleSelection, &strategies::doubleSelection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableSelectionRequest, ArrayvStableSelection, &strategies::stableSelection)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBadSortRequest, ArrayvBadSort, &strategies::badSort)
		SORT_REQUEST_BUILDER(buildArrayvAsynchronousRequest, ArrayvAsynchronous)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDMergeRequest, ArrayvPDMerge, &strategies::pdMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableCycleRequest, ArrayvStableCycle, &strategies::stableCycleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicTournamentRequest, ArrayvClassicTournament, &strategies::classicTournamentSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTournamentRequest, ArrayvTournament, &strategies::tournamentSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmoothSortRequest, ArrayvSmoothSort, &strategies::smoothSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinomialSmoothSortRequest, ArrayvBinomialSmoothSort, &strategies::binomialSmoothSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvImprovedBlockSelectionRequest, ArrayvImprovedBlockSelection, &strategies::improvedBlockSelectionSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessHybridQuickRequest, ArrayvStacklessHybridQuick, &strategies::stacklessHybridQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessDualPivotQuickRequest, ArrayvStacklessDualPivotQuick, &strategies::stacklessDualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvHanoiRequest, ArrayvHanoi, &strategies::hanoiSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLibraryRequest, ArrayvLibrary, &strategies::librarySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimplifiedLibraryRequest, ArrayvSimplifiedLibrary, &strategies::simplifiedLibrarySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveMergeRequest, ArrayvWeaveMerge, &strategies::weaveMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLaziestRequest, ArrayvLaziest, &strategies::laziestStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazierestRequest, ArrayvLazierest, &strategies::lazierestStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazyStableRequest, ArrayvLazyStable, &strategies::lazyStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedLazyStableRequest, ArrayvOptimizedLazyStable, &strategies::optimizedLazyStableSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFifthMergeRequest, ArrayvFifthMerge, &strategies::fifthMergeSortStrategy)
		SORT_REQUEST_BUILDER(buildArrayvQuadMergeRequest, ArrayvQuadMerge)
		SORT_REQUEST_BUILDER(buildArrayvBufferPartitionMergeRequest, ArrayvBufferPartitionMerge)
		SORT_REQUEST_BUILDER(buildArrayvParallelBlockMergeRequest, ArrayvParallelBlockMerge)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedRotateMergeRequest, ArrayvOptimizedRotateMerge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGrailRequest, ArrayvGrail, &strategies::grailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvParallelGrailRequest, ArrayvParallelGrail, &strategies::parallelGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAdaptiveGrailRequest, ArrayvAdaptiveGrail, &strategies::adaptiveGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDQBranchedRequest, ArrayvPDQBranched, &strategies::pdqBranchedStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPDQBranchlessRequest, ArrayvPDQBranchless, &strategies::pdqBranchlessStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSqrtRequest, ArrayvSqrt, &strategies::sqrtMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSynchronousSqrtRequest, ArrayvSynchronousSqrt, &strategies::synchronousSqrtMergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvEctaRequest, ArrayvEcta, &strategies::ectaStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircularGrailRequest, ArrayvCircularGrail, &strategies::circularGrailSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnstableGrailRequest, ArrayvUnstableGrail, &strategies::unstableGrailSort)
		SORT_REQUEST_BUILDER(buildArrayvDropMergeRequest, ArrayvDropMerge)
		SORT_REQUEST_BUILDER(buildArrayvMedianMergeRequest, ArrayvMedianMerge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRemiRequest, ArrayvRemi, &strategies::remiSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvChaliceRequest, ArrayvChalice, &strategies::chaliceSortStrategy)
		SORT_REQUEST_BUILDER(buildArrayvFlanRequest, ArrayvFlan)
		SORT_REQUEST_BUILDER(buildArrayvFluxRequest, ArrayvFlux)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvKotaRequest, ArrayvKota, &strategies::kotaMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedWeaveMergeRequest, ArrayvOptimizedWeaveMerge, &strategies::optimizedWeaveMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWikiRequest, ArrayvWiki, &strategies::wikiMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvYujisBufferedMerge2Request, ArrayvYujisBufferedMerge2, &strategies::yujiBufferedMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAndreyRequest, ArrayvAndrey, &strategies::andreyMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvNewShuffleMergeRequest, ArrayvNewShuffleMerge, &strategies::newShuffleMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCycleRequest, ArrayvCycle, &strategies::cycleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildShellRequest, Shell, &strategies::shell)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryInsertionRequest, ArrayvBinaryInsertion, &strategies::binaryInsertion)
		SORT_REQUEST_BUILDER(buildArrayvBlockInsertionRequest, ArrayvBlockInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDoubleInsertionRequest, ArrayvDoubleInsertion, &strategies::linearDoubleInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryDoubleInsertionRequest, ArrayvBinaryDoubleInsertion, &strategies::binaryDoubleInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroSortRequest, ArrayvIntroSort, &strategies::introSortStrategy)
		SORT_REQUEST_BUILDER(buildArrayvPatienceRequest, ArrayvPatience)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShellParallelRequest, ArrayvShellParallel, &strategies::shellSortParallel)
		SORT_REQUEST_BUILDER(buildArrayvMergeInsertionRequest, ArrayvMergeInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStalinOverwriteRequest, ArrayvStalinOverwrite, &strategies::stalinOverwrite)
		SORT_STRATEGY_REQUEST_BUILDER(buildCanonicalStalinFilterRequest, CanonicalStalinFilter, &strategies::canonicalStalinFilter)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicTreeRequest, ArrayvClassicTree, &strategies::classicTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTreeRequest, ArrayvTree, &strategies::treeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStrandRequest, ArrayvStrand, &strategies::strandSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAATreeRequest, ArrayvAATree, &strategies::aaTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAVLTreeRequest, ArrayvAVLTree, &strategies::avlTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSplayTreeRequest, ArrayvSplayTree, &strategies::splayTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRedBlackTreeRequest, ArrayvRedBlackTree, &strategies::redBlackTreeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRecursiveShellRequest, ArrayvRecursiveShell, &strategies::recursiveShell)
		SORT_STRATEGY_REQUEST_BUILDER(buildMergeRequest, Merge, &strategies::mergeSortStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildQuickRequest, Quick, &strategies::quick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLLQuickRequest, ArrayvLLQuick, &strategies::leftLeftQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLRQuickRequest, ArrayvLRQuick, &strategies::leftRightQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLRQuickParallelRequest, ArrayvLRQuickParallel, &strategies::leftRightQuickParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildHeapRequest, Heap, &strategies::heapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryHeapRequest, ArrayvTernaryHeap, &strategies::ternaryHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBaseNHeapRequest, ArrayvBaseNHeap, &strategies::baseNHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMinHeapRequest, ArrayvMinHeap, &strategies::minHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFlippedMinHeapRequest, ArrayvFlippedMinHeap, &strategies::flippedMinHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBottomUpHeapRequest, ArrayvBottomUpHeap, &strategies::bottomUpHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinomialHeapRequest, ArrayvBinomialHeap, &strategies::binomialHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLazyHeapRequest, ArrayvLazyHeap, &strategies::lazyHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMinMaxHeapRequest, ArrayvMinMaxHeap, &strategies::minMaxHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOutOfPlaceHeapRequest, ArrayvOutOfPlaceHeap, &strategies::outOfPlaceHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTriangularHeapRequest, ArrayvTriangularHeap, &strategies::triangularHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeakHeapRequest, ArrayvWeakHeap, &strategies::weakHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPoplarHeapRequest, ArrayvPoplarHeap, &strategies::poplarHeapSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildCountingRequest, Counting, &strategies::counting)
		SORT_STRATEGY_REQUEST_BUILDER(buildRadixRequest, Radix, &strategies::radix)
		SORT_STRATEGY_REQUEST_BUILDER(buildGnomeRequest, Gnome, &strategies::gnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildCocktailRequest, Cocktail, &strategies::cocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildCombRequest, Comb, &strategies::comb)
		SORT_STRATEGY_REQUEST_BUILDER(buildOddEvenRequest, OddEven, &strategies::oddEven)
		SORT_STRATEGY_REQUEST_BUILDER(buildPancakeRequest, Pancake, &strategies::pancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBurntPancakeRequest, ArrayvBurntPancake, &strategies::arrayvBurntPancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildCanonicalBurntPancakeRequest, CanonicalBurntPancake, &strategies::canonicalBurntPancake)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPancakeInsertionRequest, ArrayvPancakeInsertion, &strategies::arrayvPancakeInsertion)
		SORT_STRATEGY_REQUEST_BUILDER(buildStoogeRequest, Stooge, &strategies::stooge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBufferedStoogeRequest, ArrayvBufferedStooge, &strategies::bufferedStoogeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildBogoRequest, Bogo, &strategies::bogo)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBozoRequest, ArrayvBozo, &strategies::bozoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBubbleBogoRequest, ArrayvBubbleBogo, &strategies::bubbleBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvExchangeBogoRequest, ArrayvExchangeBogo, &strategies::exchangeBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDeterministicBogoRequest, ArrayvDeterministicBogo, &strategies::deterministicBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSelectionBogoRequest, ArrayvSelectionBogo, &strategies::selectionBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLessBogoRequest, ArrayvLessBogo, &strategies::lessBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCocktailBogoRequest, ArrayvCocktailBogo, &strategies::cocktailBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMedianQuickBogoRequest, ArrayvMedianQuickBogo, &strategies::medianQuickBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvQuickBogoRequest, ArrayvQuickBogo, &strategies::quickBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRandomGuessRequest, ArrayvRandomGuess, &strategies::randomGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedGuessRequest, ArrayvOptimizedGuess, &strategies::optimizedGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmartGuessRequest, ArrayvSmartGuess, &strategies::smartGuessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGuessRequest, ArrayvGuess, &strategies::guessSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBogoBogoRequest, ArrayvBogoBogo, &strategies::bogoBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMergeBogoRequest, ArrayvMergeBogo, &strategies::mergeBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSmartBogoBogoRequest, ArrayvSmartBogoBogo, &strategies::smartBogoBogoSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDualPivotQuickRequest, ArrayvDualPivotQuick, &strategies::dualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedDualPivotQuickRequest, ArrayvOptimizedDualPivotQuick, &strategies::optimizedDualPivotQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableQuickRequest, ArrayvStableQuick, &strategies::stableQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStableQuickParallelRequest, ArrayvStableQuickParallel, &strategies::stableQuickParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvForcedStableQuickRequest, ArrayvForcedStableQuick, &strategies::forcedStableQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryLLQuickRequest, ArrayvTernaryLLQuick, &strategies::ternaryLeftLeftQuick)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTernaryLRQuickRequest, ArrayvTernaryLRQuick, &strategies::ternaryLeftRightQuick)
		SORT_REQUEST_BUILDER(buildArrayvStablePermutationRequest, ArrayvStablePermutation)
		SORT_REQUEST_BUILDER(buildArrayvTableRequest, ArrayvTable)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryMergeRequest, ArrayvBinaryMerge, &strategies::binaryMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIterativeTopDownMergeRequest, ArrayvIterativeTopDownMerge, &strategies::iterativeTopDownMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvInPlaceMergeRequest, ArrayvInPlaceMerge, &strategies::inPlaceMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvParallelMergeRequest, ArrayvParallelMerge, &strategies::parallelMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMergeRequest, ArrayvRotateMerge, &strategies::rotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMergeParallelRequest, ArrayvRotateMergeParallel, &strategies::parallelRotateMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessRotateMergeRequest, ArrayvStacklessRotateMerge, &strategies::stacklessRotateMergeStrategy)
		SORT_REQUEST_BUILDER(buildArrayvBlockSwapMergeRequest, ArrayvBlockSwapMerge)
		SORT_REQUEST_BUILDER(buildArrayvWeavedMergeRequest, ArrayvWeavedMerge)
		SORT_REQUEST_BUILDER(buildArrayvTwinMergeRequest, ArrayvTwinMerge)
		SORT_REQUEST_BUILDER(buildArrayvTimRequest, ArrayvTim)
		SORT_REQUEST_BUILDER(buildArrayvCocktailMergeRequest, ArrayvCocktailMerge)
		SORT_REQUEST_BUILDER(buildArrayvImprovedInPlaceMergeRequest, ArrayvImprovedInPlaceMerge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedBottomUpMergeRequest, ArrayvOptimizedBottomUpMerge, &strategies::optimizedBottomUpMergeStrategy)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedBubbleRequest, ArrayvOptimizedBubble, &strategies::optimizedBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnoptimizedBubbleRequest, ArrayvUnoptimizedBubble, &strategies::unoptimizedBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSwaplessBubbleRequest, ArrayvSwaplessBubble, &strategies::swaplessBubble)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryGnomeRequest, ArrayvBinaryGnome, &strategies::binaryGnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircleIterativeRequest, ArrayvCircleIterative, &strategies::circleSortIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircleRecursiveRequest, ArrayvCircleRecursive, &strategies::circleSortRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroCircleIterativeRequest, ArrayvIntroCircleIterative, &strategies::introCircleSortIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIntroCircleRecursiveRequest, ArrayvIntroCircleRecursive, &strategies::introCircleSortRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvHybridCombRequest, ArrayvHybridComb, &strategies::hybridComb)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicThreeSmoothCombRequest, ArrayvClassicThreeSmoothComb, &strategies::classicThreeSmoothComb)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombIterativeRequest, ArrayvThreeSmoothCombIterative, &strategies::threeSmoothCombIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombRecursiveRequest, ArrayvThreeSmoothCombRecursive, &strategies::recursiveThreeSmoothSequential)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvThreeSmoothCombParallelRequest, ArrayvThreeSmoothCombParallel, &strategies::recursiveThreeSmoothParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicIterativeRequest, ArrayvBitonicIterative, &strategies::bitonicIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicRecursiveRequest, ArrayvBitonicRecursive, &strategies::bitonicRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBitonicParallelRequest, ArrayvBitonicParallel, &strategies::bitonicParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonIterativeRequest, ArrayvBoseNelsonIterative, &strategies::boseNelsonIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonRecursiveRequest, ArrayvBoseNelsonRecursive, &strategies::boseNelsonRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBoseNelsonParallelRequest, ArrayvBoseNelsonParallel, &strategies::boseNelsonParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeIterativeRequest, ArrayvOddEvenMergeIterative, &strategies::oddEvenMergeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeRecursiveRequest, ArrayvOddEvenMergeRecursive, &strategies::oddEvenMergeRecursiveDefault)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOddEvenMergeParallelRequest, ArrayvOddEvenMergeParallel, &strategies::oddEvenMergeParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDiamondIterativeRequest, ArrayvDiamondIterative, &strategies::diamondIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvDiamondRecursiveRequest, ArrayvDiamondRecursive, &strategies::diamondRecursiveVariant)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseIterativeRequest, ArrayvPairwiseIterative, &strategies::pairwiseIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseRecursiveRequest, ArrayvPairwiseRecursive, &strategies::pairwiseRecursiveDefault)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCreaseRequest, ArrayvCrease, &strategies::creaseSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFoldRequest, ArrayvFold, &strategies::foldSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMergeExchangeIterativeRequest, ArrayvMergeExchangeIterative, &strategies::mergeExchangeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseMergeIterativeRequest, ArrayvPairwiseMergeIterative, &strategies::pairwiseMergeIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPairwiseMergeRecursiveRequest, ArrayvPairwiseMergeRecursive, &strategies::pairwiseMergeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveIterativeRequest, ArrayvWeaveIterative, &strategies::weaveIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveParallelRequest, ArrayvWeaveParallel, &strategies::weaveParallel)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvWeaveRecursiveRequest, ArrayvWeaveRecursive, &strategies::weaveRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMatrixRequest, ArrayvMatrix, &strategies::matrixSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedStoogeRequest, ArrayvOptimizedStooge, &strategies::optimizedStooge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedStoogeStudioRequest, ArrayvOptimizedStoogeStudio, &strategies::optimizedStoogeStudio)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvQuadStoogeRequest, ArrayvQuadStooge, &strategies::quadStooge)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSlowRequest, ArrayvSlow, &strategies::slowSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCircloidRequest, ArrayvCircloid, &strategies::circloidSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSillyRequest, ArrayvSilly, &strategies::sillySort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSnuffleRequest, ArrayvSnuffle, &strategies::snuffleSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvCompleteGraphRequest, ArrayvCompleteGraph, &strategies::completeGraphSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFunRequest, ArrayvFun, &strategies::funSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedCocktailRequest, ArrayvOptimizedCocktail, &strategies::optimizedCocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvUnoptimizedCocktailRequest, ArrayvUnoptimizedCocktail, &strategies::unoptimizedCocktail)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvOptimizedGnomeRequest, ArrayvOptimizedGnome, &strategies::optimizedGnome)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShoveRequest, ArrayvShove, &strategies::shove)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSlopeRequest, ArrayvSlope, &strategies::slope)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvAmericanFlagRequest, ArrayvAmericanFlag, &strategies::americanFlag)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryQuickIterativeRequest, ArrayvBinaryQuickIterative, &strategies::binaryQuickIterative)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvBinaryQuickRecursiveRequest, ArrayvBinaryQuickRecursive, &strategies::binaryQuickRecursive)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvClassicGravityRequest, ArrayvClassicGravity, &strategies::classicGravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvFlashRequest, ArrayvFlash, &strategies::flash)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvGravityRequest, ArrayvGravity, &strategies::gravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvInPlaceLsdRadixRequest, ArrayvInPlaceLsdRadix, &strategies::inPlaceLsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvIndexRequest, ArrayvIndex, &strategies::indexSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvLsdRadixRequest, ArrayvLsdRadix, &strategies::radix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvMsdRadixRequest, ArrayvMsdRadix, &strategies::msdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvPigeonholeRequest, ArrayvPigeonhole, &strategies::pigeonhole)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateLsdRadixRequest, ArrayvRotateLsdRadix, &strategies::rotateLsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvRotateMsdRadixRequest, ArrayvRotateMsdRadix, &strategies::rotateMsdRadix)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvShatterRequest, ArrayvShatter, &strategies::shatterSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimpleShatterRequest, ArrayvSimpleShatter, &strategies::simpleShatterSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvSimplisticGravityRequest, ArrayvSimplisticGravity, &strategies::simplisticGravity)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStaticRequest, ArrayvStatic, &strategies::staticSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvTimeRequest, ArrayvTime, &strategies::timeSort)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessAmericanFlagRequest, ArrayvStacklessAmericanFlag, &strategies::stacklessAmericanFlag)
		SORT_STRATEGY_REQUEST_BUILDER(buildArrayvStacklessBinaryQuickRequest, ArrayvStacklessBinaryQuick, &strategies::stacklessBinaryRadix)
#undef SORT_REQUEST_BUILDER
#undef SORT_STRATEGY_REQUEST_BUILDER
	}

	const std::vector<AlgorithmDescriptor>& algorithmCatalog()
	{
		static const std::vector<AlgorithmDescriptor> algorithms = {
			AlgorithmDescriptor(SortAlgorithm::Insertion, "insertion", "Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"insertion-methods", "insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Bubble, "bubble", "Bubble Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Selection, "selection", "Selection Sort", "O(n²)", "O(1)", false, true, true, true, &buildSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBingo, "arrayv.bingo", "Bingo Sort", "O(n·m), best O(n + m²)", "O(1)", false, true, true, true, &buildArrayvBingoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "bingo", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDoubleSelection, "arrayv.double_selection", "Double Selection Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvDoubleSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "double-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableSelection, "arrayv.stable_selection", "Stable Selection Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvStableSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "stable-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBadSort, "arrayv.bad_sort", "Bad Sort (Cubic Selection)", "O(n³)", "O(1)", false, true, true, true, &buildArrayvBadSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("bad_sort_candidate_checks", "Suffix-minimum candidates"),
					AlgorithmMetricDescriptor("bad_sort_comparisons", "Cubic comparisons"),
					AlgorithmMetricDescriptor("bad_sort_exchanges", "Selection exchanges")
				}, 128, "checks each suffix candidate against every later item until it finds the first suffix minimum, then exchanges it into place; selection-like but cubic",
				"selection-sorts", "bad-cubic-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAsynchronous, "arrayv.asynchronous_sort", "Asynchronous Sort (Value Scan)", "O(n × key-range)", "O(n) auxiliary item lane", true, false, false, true, &buildArrayvAsynchronousRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("asynchronous_key_passes", "Value passes"),
					AlgorithmMetricDescriptor("asynchronous_source_probes", "Auxiliary probes"),
					AlgorithmMetricDescriptor("asynchronous_emitted_items", "Emitted items")
				}, 384, "copies items to an auxiliary scan lane, advances through the ordered integer-key range, probes the lane on each pass, writes eligible unconsumed items to output, and records consumption separately; stable for equal keys",
				"selection-sorts", "asynchronous-value-scan", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDMerge, "arrayv.pd_merge_sort", "Pattern-Defeating Merge Sort", "O(n log r)", "O(n) auxiliary item lane", true, false, true, true, &buildArrayvPDMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdmerge_run_count", "Natural runs"),
					AlgorithmMetricDescriptor("pdmerge_run_reversals", "Descending runs reversed"),
					AlgorithmMetricDescriptor("pdmerge_merge_passes", "Pairwise merge passes"),
					AlgorithmMetricDescriptor("pdmerge_merges", "Run merges"),
					AlgorithmMetricDescriptor("pdmerge_buffered_items", "Items copied into smaller-side buffers")
				}, MaxItemCount, "detects natural ascending and strict descending runs, reverses descending runs, merges adjacent run pairs in rounds, and buffers the smaller side for stable forward or backward merging",
				"merge-hybrid", "pattern-defeating", "Merge Hybrids",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCycle, "arrayv.cycle", "Cycle Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvCycleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"general permutation sort", "selection-sorts", "cycle", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableCycle, "arrayv.stable_cycle_sort", "Stable Cycle Sort", "O(n²)", "O(n) reference lane and packed flags", true, false, true, true, &buildArrayvStableCycleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stable_cycle_cycles", "Cycles"),
					AlgorithmMetricDescriptor("stable_cycle_rank_probes", "Stable-rank probes"),
					AlgorithmMetricDescriptor("stable_cycle_item_moves", "Cycle item moves"),
					AlgorithmMetricDescriptor("stable_cycle_fixed_positions", "Fixed destinations")
				}, MaxItemCount, "computes each item's stable destination from key order and original equal-key order, moves items through reversible cycle carries, and records completed destinations in a packed bit workspace",
				"selection-sorts", "stable-cycle", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicTournament, "arrayv.classic_tournament_sort", "Classic Tournament Sort", "O(n log n)", "O(n) winner tree and output lane", true, false, true, true, &buildArrayvClassicTournamentRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("classic_tournament_tree_comparisons", "Winner-tree comparisons"),
					AlgorithmMetricDescriptor("classic_tournament_tree_updates", "Winner-tree updates"),
					AlgorithmMetricDescriptor("classic_tournament_extractions", "Extracted winners"),
					AlgorithmMetricDescriptor("classic_tournament_output_writes", "Output lane writes"),
					AlgorithmMetricDescriptor("classic_tournament_padding_leaves", "Padded leaves")
				}, MaxItemCount, "builds a complete power-of-two winner tree, repeatedly emits the root winner, disables its leaf, and recomputes only that winner path with stable left-to-right ties",
				"selection-sorts", "classic-tournament", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTournament, "arrayv.tournament_sort", "Tournament Sort (Loser Tree)", "O(n log n)", "O(n) winner-loser tree and output lane", true, false, true, true, &buildArrayvTournamentRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tournament_matches", "Tournament matches"),
					AlgorithmMetricDescriptor("tournament_loser_replays", "Replayed loser nodes"),
					AlgorithmMetricDescriptor("tournament_tree_updates", "Tree updates"),
					AlgorithmMetricDescriptor("tournament_outputs", "Output items"),
					AlgorithmMetricDescriptor("tournament_padding_leaves", "Padded leaves")
				}, MaxItemCount, "stores each match winner and loser separately; after extracting the root winner, replays the saved loser path from its leaf to update the next winner",
				"selection-sorts", "loser-tree-tournament", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmoothSort, "arrayv.smooth_sort", "Smooth Sort", "O(n) best, O(n log n) worst", "O(1) auxiliary", false, true, true, true, &buildArrayvSmoothSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("smooth_sift_calls", "Leonardo heap sifts"),
					AlgorithmMetricDescriptor("smooth_trinkle_calls", "Forest trinkles"),
					AlgorithmMetricDescriptor("smooth_heap_comparisons", "Heap comparisons"),
					AlgorithmMetricDescriptor("smooth_heap_moves", "Held-key moves"),
					AlgorithmMetricDescriptor("smooth_heap_insertions", "Heap insertions"),
					AlgorithmMetricDescriptor("smooth_heap_extractions", "Heap extractions"),
					AlgorithmMetricDescriptor("smooth_max_leonardo_order", "Maximum Leonardo order")
				}, MaxItemCount, "builds an adaptive forest of Leonardo heaps, maintaining it with sift and trinkle paths before splitting each heap during extraction",
				"selection-sorts", "smooth", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinomialSmoothSort, "arrayv.binomial_smooth_sort", "Binomial Smooth Sort", "O(n log n)", "O(1) auxiliary", false, true, true, true, &buildArrayvBinomialSmoothSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binomial_smooth_thrift_calls", "Thrift calls"),
					AlgorithmMetricDescriptor("binomial_smooth_comparisons", "Heap comparisons"),
					AlgorithmMetricDescriptor("binomial_smooth_swaps", "Heap swaps"),
					AlgorithmMetricDescriptor("binomial_smooth_insertions", "Forest insertions"),
					AlgorithmMetricDescriptor("binomial_smooth_heap_visits", "Visited heap nodes"),
					AlgorithmMetricDescriptor("binomial_smooth_max_height", "Maximum binomial height")
				}, MaxItemCount, "grows a forest of binary-indexed binomial heaps using recursive thrift repairs, then reheapifies each remaining root during extraction",
				"selection-sorts", "binomial-smooth", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvImprovedBlockSelection, "arrayv.improved_block_selection_sort", "Improved Block Selection Merge Sort", "O(n log² n) comparisons", "O(1) auxiliary", false, true, true, true, &buildArrayvImprovedBlockSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_selection_runs", "Merge run widths"),
					AlgorithmMetricDescriptor("block_selection_blocks_selected", "Selected blocks"),
					AlgorithmMetricDescriptor("block_selection_block_swaps", "Block item swaps"),
					AlgorithmMetricDescriptor("block_selection_rotations", "In-place rotations"),
					AlgorithmMetricDescriptor("block_selection_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("block_selection_forward_merges", "Forward merges"),
					AlgorithmMetricDescriptor("block_selection_backward_merges", "Backward merges"),
					AlgorithmMetricDescriptor("block_selection_max_block_length", "Maximum block length")
				}, MaxItemCount, "forms power-of-two blocks, selects blocks into order, and completes each run with forward and backward in-place merges",
				"merge-hybrid", "improved-block-selection", "Merge Hybrids", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessHybridQuick, "arrayv.stackless_hybrid_quick_sort", "Stackless Hybrid Quick Sort", "O(n log n) average", "O(1) explicit stack; O(n²) worst case", false, true, true, true, &buildArrayvStacklessHybridQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stackless_hybrid_maximum_scans", "Maximum scans"),
					AlgorithmMetricDescriptor("stackless_hybrid_maximum_items_parked", "Maximum items parked"),
					AlgorithmMetricDescriptor("stackless_hybrid_partitions", "Hoare partitions"),
					AlgorithmMetricDescriptor("stackless_hybrid_partition_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("stackless_hybrid_insertion_runs", "Insertion fallback runs"),
					AlgorithmMetricDescriptor("stackless_hybrid_duplicate_skips", "Equal-key prefix skips"),
					AlgorithmMetricDescriptor("stackless_hybrid_max_active_range", "Largest active range")
				}, MaxItemCount, "parks the maximum suffix, iteratively partitions the active prefix with Hoare boundaries, and finishes each reduced range with binary insertion",
				"partition-sorts", "stackless-hybrid", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::CurrentMaximum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessDualPivotQuick, "arrayv.stackless_dual_pivot_quick_sort", "Stackless Dual-Pivot Quick Sort", "O(n log n) average, O(n²) worst", "O(1) explicit stack", false, true, true, true, &buildArrayvStacklessDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stackless_dual_maximum_scans", "Maximum scans"),
					AlgorithmMetricDescriptor("stackless_dual_maximum_items_parked", "Maximum items parked"),
					AlgorithmMetricDescriptor("stackless_dual_partitions", "Dual-pivot partitions"),
					AlgorithmMetricDescriptor("stackless_dual_partition_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("stackless_dual_insertion_runs", "Insertion fallback runs"),
					AlgorithmMetricDescriptor("stackless_dual_duplicate_skips", "Equal-key prefix skips"),
					AlgorithmMetricDescriptor("stackless_dual_max_active_range", "Largest active range")
				}, MaxItemCount, "parks the maximum suffix, samples two pivots from fractional positions, partitions around both, then iterates through insertion-sized ranges without recursion",
				"partition-sorts", "stackless-dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::CurrentMaximum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Shell, "shell", "Shell Sort", "sequence dependent", "O(1)", false, true, true, true, &buildShellRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Halving), "halving", "Halving", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence, true),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Knuth), "knuth", "Knuth", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Ciura), "ciura", "Ciura", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Tokuda), "tokuda", "Tokuda", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence)
				},
				std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"),
					AlgorithmMetricDescriptor("last_gap", "Current gap")
				}, MaxItemCount, "general permutation sort", "insertion-methods", "shell", "Insertion Methods",
				std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryInsertion, "arrayv.binary_insertion", "Binary Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvBinaryInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"general permutation sort", "insertion-methods", "binary-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvHanoi, "arrayv.hanoi_sort", "Hanoi Sort", "practical only at tiny sizes", "O(n) auxiliary peg storage", false, false, true, true, &buildArrayvHanoiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("hanoi_item_transfers", "Peg item transfers"),
					AlgorithmMetricDescriptor("hanoi_duplicate_batches", "Grouped equal-key moves"),
					AlgorithmMetricDescriptor("hanoi_comparisons", "Peg comparisons"),
					AlgorithmMetricDescriptor("hanoi_tower_moves", "Tower schedule moves")
				}, 16, "Hanoi-inspired stack sort that moves consecutive equal keys together between two auxiliary pegs and the input stack",
				"insertion-methods", "hanoi", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLibrary, "arrayv.library_sort", "Library Sort", "O(n log n) expected, O(n²) worst", "O(n) item workspace with 15 gaps per shelf block", false, false, true, true, &buildArrayvLibraryRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("library_rebalances", "Factor-four rebalances"),
					AlgorithmMetricDescriptor("library_gap_inserts", "Items inserted into gaps"),
					AlgorithmMetricDescriptor("library_block_search_comparisons", "Shelf-block comparisons"),
					AlgorithmMetricDescriptor("library_local_search_comparisons", "Within-shelf comparisons"),
					AlgorithmMetricDescriptor("library_spills", "Full-shelf spill operations")
				}, MaxItemCount, "uses 15-gap shelf blocks, randomized equal-key shelf selection, and factor-four retrieval/rebalance phases",
				"insertion-methods", "library", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem, SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSimplifiedLibrary, "arrayv.simplified_library_sort", "Simplified Library Sort", "O(n log n) average, O(n²) worst", "O(n) item, rank, and location workspaces", true, false, true, true, &buildArrayvSimplifiedLibraryRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("simplified_library_rebalances", "Shelf rebalances"),
					AlgorithmMetricDescriptor("simplified_library_gap_inserts", "Items routed into gaps"),
					AlgorithmMetricDescriptor("simplified_library_binary_comparisons", "Rank-search comparisons"),
					AlgorithmMetricDescriptor("simplified_library_small_fallbacks", "Small-input fallbacks")
				}, MaxItemCount, "stable library sort that records each incoming item's rank, scatters shelves during factor-four rebalances, and insertion-sorts the resulting gaps",
				"insertion-methods", "simplified-library", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveMerge, "arrayv.weave_merge_sort", "Weave Merge Sort", "O(n²) worst", "O(log n) recursion, O(1) item workspace", false, true, true, true, &buildArrayvWeaveMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("weave_recursive_merges", "Recursive weave merges"),
					AlgorithmMetricDescriptor("weave_interleave_swaps", "Interleave rotations"),
					AlgorithmMetricDescriptor("weave_insertion_comparisons", "Weave insertion comparisons"),
					AlgorithmMetricDescriptor("weave_insertion_swaps", "Weave insertion swaps")
				}, 128, "recursively interleaves two sorted runs with adjacent rotations, then insertion-orders the woven range",
				"merge-hybrid", "weave-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLaziest, "arrayv.laziest_sort", "Laziest Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvLaziestRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("laziest_block_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("laziest_merges", "Suffix-fold in-place merges"),
					AlgorithmMetricDescriptor("laziest_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("laziest_exponential_probes", "Exponential-search probes"),
					AlgorithmMetricDescriptor("laziest_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms square-root-sized stable insertion runs, then folds them into a sorted suffix with exponential-search rotations",
				"merge-hybrid", "laziest", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazierest, "arrayv.lazierest_sort", "Lazierest Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvLazierestRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("lazierest_block_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("lazierest_merges", "In-place merges"),
					AlgorithmMetricDescriptor("lazierest_fragmented_merges", "Fragmented suffix folds"),
					AlgorithmMetricDescriptor("lazierest_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("lazierest_exponential_probes", "Exponential-search probes"),
					AlgorithmMetricDescriptor("lazierest_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms ceil-cuberoot-sized stable runs, combines square-sized runs, then performs fragmented stable suffix folds with exponential-search rotations",
				"merge-hybrid", "lazierest", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazyStable, "arrayv.lazy_stable_sort", "Lazy Stable Sort", "O(n²) worst case", "O(1)", true, true, true, true, &buildArrayvLazyStableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("lazy_pair_comparisons", "Initial pair comparisons"),
					AlgorithmMetricDescriptor("lazy_pair_swaps", "Initial pair swaps"),
					AlgorithmMetricDescriptor("lazy_merge_comparisons", "No-buffer merge comparisons"),
					AlgorithmMetricDescriptor("lazy_rotation_swaps", "No-buffer rotation swaps"),
					AlgorithmMetricDescriptor("lazy_merge_passes", "Bottom-up merge passes")
				}, MaxItemCount, "orders adjacent pairs and merges bottom-up with the Grail no-buffer stable merge kernel",
				"merge-hybrid", "lazy-stable", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedLazyStable, "arrayv.optimized_lazy_stable_sort", "Optimized Lazy Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvOptimizedLazyStableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_lazy_runs", "Natural insertion runs"),
					AlgorithmMetricDescriptor("optimized_lazy_run_reversals", "Strict descending run swaps"),
					AlgorithmMetricDescriptor("optimized_lazy_insertion_comparisons", "Run formation comparisons"),
					AlgorithmMetricDescriptor("optimized_lazy_shifts", "Run insertion shifts"),
					AlgorithmMetricDescriptor("optimized_lazy_merge_comparisons", "No-buffer merge comparisons"),
					AlgorithmMetricDescriptor("optimized_lazy_rotation_swaps", "No-buffer rotation swaps"),
					AlgorithmMetricDescriptor("optimized_lazy_merge_passes", "Bottom-up merge passes")
				}, MaxItemCount, "forms stable natural insertion runs of 16 items, reversing only strict descents, then uses bottom-up Grail no-buffer merges",
				"merge-hybrid", "optimized-lazy-stable", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFifthMerge, "arrayv.fifth_merge_sort", "Fifth Merge Sort", "O(n log n) comparisons", "O(n) merge workspace", true, false, true, true, &buildArrayvFifthMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("fifth_partition_count", "Initial partitions"),
					AlgorithmMetricDescriptor("fifth_insertion_runs", "Eight-item insertion runs"),
					AlgorithmMetricDescriptor("fifth_binary_merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("fifth_merges", "Five-way merges"),
					AlgorithmMetricDescriptor("fifth_comparisons", "Five-way merge comparisons"),
					AlgorithmMetricDescriptor("fifth_auxiliary_writes", "Buffer item writes")
				}, MaxItemCount, "splits input into five balanced partitions with the remainder in the first partition, sorts each through eight-item insertion runs and buffered binary passes, then stably merges the five runs through a reusable lane",
				"merge-hybrid", "fifth-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuadMerge, "arrayv.quad_sort", "Quad Sort", "O(n log n) comparisons", "O(n) merge workspace", true, false, true, true, &buildArrayvQuadMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("quad_merged_groups", "Four-run merge groups"),
					AlgorithmMetricDescriptor("quad_leaf_runs", "Eight-item insertion leaves"),
					AlgorithmMetricDescriptor("quad_merge_layers", "Bottom-up quad layers"),
					AlgorithmMetricDescriptor("quad_merges", "Four-way merges"),
					AlgorithmMetricDescriptor("quad_comparisons", "Four-way merge comparisons"),
					AlgorithmMetricDescriptor("quad_auxiliary_writes", "Buffer item writes")
				}, MaxItemCount, "forms eight-item insertion leaves, merges groups of up to four sorted runs bottom-up, and uses a stable reusable buffer lane for each quad merge",
				"merge-hybrid", "quad", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBufferPartitionMerge, "arrayv.buffer_partition_merge_sort", "Buffer Partition Merge Sort", "O(n log² n) worst-case comparisons", "O(n) merge workspace", false, false, true, true, &buildArrayvBufferPartitionMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("buffer_partition_selections", "Median-rank selections"),
					AlgorithmMetricDescriptor("buffer_partition_select_passes", "Selection partitions"),
					AlgorithmMetricDescriptor("buffer_partition_fallbacks", "Bounded selection fallbacks"),
					AlgorithmMetricDescriptor("buffer_partition_merges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("buffer_partition_comparisons", "Selection comparisons")
				}, MaxItemCount, "selects a median boundary for each merge range with median-of-three quickselect and a bounded heap/insertion fallback, recursively sorts both partitions, then merges through an auxiliary lane",
				"merge-hybrid", "buffer-partition", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ProbedItem, SemanticRole::WriteTarget, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelBlockMerge, "arrayv.parallel_block_merge_sort", "Parallel Block Merge Sort", "O(n log n)", "O(n) reusable merge workspace", true, false, true, true, &buildArrayvParallelBlockMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_block_runs", "Sixteen-item insertion runs"),
					AlgorithmMetricDescriptor("parallel_block_tasks", "Independent block merges"),
					AlgorithmMetricDescriptor("parallel_block_layers", "Parallel merge layers"),
					AlgorithmMetricDescriptor("parallel_block_comparisons", "Block merge comparisons")
				}, MaxItemCount, "forms stable 16-item runs then merges disjoint adjacent blocks in deterministic parallel phase groups with one reusable buffer lane",
				"merge-hybrid", "parallel-block-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedRotateMerge, "arrayv.optimized_rotate_merge_sort", "Optimized Rotate Merge Sort", "O(n log n) buffered; O(n log² n) rotation fallback", "Configured power-of-two temporary buffer", true, false, true, true, &buildArrayvOptimizedRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(16, "items_16", "16 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(32, "items_32", "32 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(64, "items_64", "64 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize, true),
					AlgorithmOptionDescriptor(128, "items_128", "128 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(256, "items_256", "256 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize)
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_rotate_runs", "32-item insertion runs"),
					AlgorithmMetricDescriptor("optimized_rotate_merges", "Merge ranges"),
					AlgorithmMetricDescriptor("optimized_rotate_buffer_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("optimized_rotate_in_place_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("optimized_rotate_buffer_writes", "Temporary item writes"),
					AlgorithmMetricDescriptor("optimized_rotate_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("optimized_rotate_trimmed_items", "Items excluded by boundary searches")
				}, MaxItemCount, "forms stable 32-item insertion runs; trims already ordered boundaries before merging; uses the configured power-of-two buffer for the smaller run and stable in-place rotations when the run exceeds capacity",
				"merge-hybrid", "optimized-rotate", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvGrail, "arrayv.grail_sort", "Grail Sort", "O(n log n)", "O(1), fixed 512-item, or O(√n) buffer", true, false, true, true, &buildArrayvGrailRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(0, "in_place", "In-place", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode, true),
					AlgorithmOptionDescriptor(1, "static_buffer", "Static buffer (up to 512 items)", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode),
					AlgorithmOptionDescriptor(2, "dynamic_buffer", "Dynamic √n buffer", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode)
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("grail_runs", "Sixteen-item insertion runs"),
					AlgorithmMetricDescriptor("grail_merges", "Block merge ranges"),
					AlgorithmMetricDescriptor("grail_buffered_merges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("grail_rotation_merges", "Stable rotation fallback ranges"),
					AlgorithmMetricDescriptor("grail_buffer_writes", "Temporary buffer writes"),
					AlgorithmMetricDescriptor("grail_comparisons", "Merge comparisons")
				}, MaxItemCount, "stable bottom-up block merge with the pinned no-buffer, fixed-buffer, and dynamic-buffer choices; small runs use binary insertion and oversized merges use stable rotations",
				"merge-hybrid", "grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelGrail, "arrayv.parallel_grail_sort", "Parallel Grail Sort", "O(n log n)", "O(√n) reusable merge buffer", true, false, true, true, &buildArrayvParallelGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_grail_run_size", "Insertion run size"),
					AlgorithmMetricDescriptor("parallel_grail_runs", "Initial insertion runs"),
					AlgorithmMetricDescriptor("parallel_grail_layers", "Deterministic parallel merge layers"),
					AlgorithmMetricDescriptor("parallel_grail_tasks", "Independent merge tasks"),
					AlgorithmMetricDescriptor("parallel_grail_comparisons", "Merge-layer comparisons"),
					AlgorithmMetricDescriptor("parallel_grail_buffered_merges", "Buffer-backed merges"),
					AlgorithmMetricDescriptor("parallel_grail_rotation_merges", "No-buffer rotation fallbacks"),
					AlgorithmMetricDescriptor("parallel_grail_buffer_writes", "Temporary item writes"),
					AlgorithmMetricDescriptor("parallel_grail_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms stable 16-item runs then merges independent adjacent ranges in deterministic phase layers, using a square-root buffer and stable rotation merges when a side is too large",
				"merge-hybrid", "parallel-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAdaptiveGrail, "arrayv.adaptive_grail_sort", "Adaptive Grail Sort", "O(n log n) worst case; benefits from existing runs", "O(1) item workspace", true, true, true, true, &buildArrayvAdaptiveGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("adaptive_grail_natural_runs", "Detected monotone runs"),
					AlgorithmMetricDescriptor("adaptive_grail_reversed_runs", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("adaptive_grail_extensions", "Runs extended to minimum length"),
					AlgorithmMetricDescriptor("adaptive_grail_merges", "Adaptive run merges"),
					AlgorithmMetricDescriptor("adaptive_grail_comparisons", "Run detection comparisons"),
					AlgorithmMetricDescriptor("adaptive_grail_rotation_swaps", "Run reversal swaps")
				}, MaxItemCount, "detects natural monotone runs, reverses only strict descents, extends short runs to 32 items, and merges adjacent runs when the stack size rule requires it using the stable Grail no-buffer merge kernel",
				"merge-hybrid", "adaptive-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDQBranched, "arrayv.pdq_branched_sort", "PDQ Sort (Branched)", "Average O(n log n); worst O(n log n)", "O(log n) stack", false, true, true, true, &buildArrayvPDQBranchedRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdq_partitions", "Partitions"), AlgorithmMetricDescriptor("pdq_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("pdq_bad_partitions", "Unbalanced partitions"), AlgorithmMetricDescriptor("pdq_heap_fallbacks", "Heap fallbacks"),
					AlgorithmMetricDescriptor("pdq_maximum_depth", "Maximum recursion depth"), AlgorithmMetricDescriptor("pdq_insertion_ranges", "Insertion-sort leaves")
				}, MaxItemCount, "pattern-defeating quicksort trace with median-of-three Hoare partitions, bounded depth, unbalanced-partition detection, heap fallback, and insertion-sort leaves",
				"quick", "pdq-branched", "Quick Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDQBranchless, "arrayv.pdq_branchless_sort", "PDQ Sort (Branchless Three-Way)", "Average O(n log n); worst O(n log n)", "O(log n) stack", false, true, true, true, &buildArrayvPDQBranchlessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdq_partitions", "Partitions"), AlgorithmMetricDescriptor("pdq_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("pdq_bad_partitions", "Unbalanced partitions"), AlgorithmMetricDescriptor("pdq_heap_fallbacks", "Heap fallbacks"),
					AlgorithmMetricDescriptor("pdq_maximum_depth", "Maximum recursion depth"), AlgorithmMetricDescriptor("pdq_equal_ranges", "Equal-key bands"),
					AlgorithmMetricDescriptor("pdq_insertion_ranges", "Insertion-sort leaves")
				}, MaxItemCount, "branchless-source counterpart represented by a three-way equal-band partition, with bounded depth, pattern-bad partition fallback, and insertion-sort leaves",
				"quick", "pdq-branchless", "Quick Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSqrt, "arrayv.sqrt_sort", "Sqrt Sort", "O(n log n)", "O(√n) item workspace", true, false, true, true, &buildArrayvSqrtRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("sqrt_run_size", "Square-root run size"), AlgorithmMetricDescriptor("sqrt_runs", "Insertion-sorted blocks"),
					AlgorithmMetricDescriptor("sqrt_merges", "Buffered merges"), AlgorithmMetricDescriptor("sqrt_merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("sqrt_buffered_merges", "Buffer-backed merges"), AlgorithmMetricDescriptor("sqrt_rotation_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("sqrt_buffer_writes", "Temporary buffer writes"), AlgorithmMetricDescriptor("sqrt_comparisons", "Merge comparisons")
				}, MaxItemCount, "sorts √n-sized insertion runs and merges them bottom-up with a reusable √n buffer, falling back to the stable Grail rotation merge when a full run does not fit",
				"merge-hybrid", "sqrt", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSynchronousSqrt, "arrayv.synchronous_sqrt_sort", "Synchronous Sqrt Sort", "O(n log n)", "O(√n) item workspace", true, false, true, true, &buildArrayvSynchronousSqrtRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("sqrt_run_size", "Square-root run size"), AlgorithmMetricDescriptor("sqrt_runs", "Insertion-sorted blocks"),
					AlgorithmMetricDescriptor("sqrt_merges", "Buffered merges"), AlgorithmMetricDescriptor("sqrt_merge_passes", "Synchronous merge layers"),
					AlgorithmMetricDescriptor("sqrt_synchronous_layers", "Grouped merge layers"), AlgorithmMetricDescriptor("sqrt_buffered_merges", "Buffer-backed merges"),
					AlgorithmMetricDescriptor("sqrt_rotation_merges", "Rotation fallback merges"), AlgorithmMetricDescriptor("sqrt_buffer_writes", "Temporary buffer writes"),
					AlgorithmMetricDescriptor("sqrt_comparisons", "Merge comparisons")
				}, MaxItemCount, "uses the same square-root buffer strategy but groups independent block merges into deterministic synchronous phase layers for playback",
				"merge-hybrid", "synchronous-sqrt", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvEcta, "arrayv.ecta_sort", "Ecta Sort", "O(n log n)", "O(n) merge workspace plus block tags", true, false, true, true, &buildArrayvEctaRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("ecta_min_run", "Adaptive insertion run size"),
					AlgorithmMetricDescriptor("ecta_insertion_runs", "Binary insertion runs"),
					AlgorithmMetricDescriptor("ecta_four_way_groups", "Four-run ping-pong groups"),
					AlgorithmMetricDescriptor("ecta_four_way_merges", "Four-way merge groups"),
					AlgorithmMetricDescriptor("ecta_four_way_comparisons", "Four-way comparisons"),
					AlgorithmMetricDescriptor("ecta_four_way_auxiliary_writes", "Four-way buffer writes"),
					AlgorithmMetricDescriptor("ecta_four_way_passes", "Four-way passes"),
					AlgorithmMetricDescriptor("ecta_binary_merges", "Binary tail merges"),
					AlgorithmMetricDescriptor("ecta_binary_passes", "Binary tail passes")
				}, MaxItemCount, "adapts the insertion-run size, merges four runs per ping-pong layer while the block capacity allows, then finishes the remaining levels with stable binary merges",
				"merge-hybrid", "ecta", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircularGrail, "arrayv.circular_grail_sort", "Circular Grail Sort", "O(n log n)", "O(n) circular merge lane", true, false, true, true, &buildArrayvCircularGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circular_grail_run_size", "Square-root run size"), AlgorithmMetricDescriptor("circular_grail_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("circular_grail_merges", "Ring-buffer merge ranges"), AlgorithmMetricDescriptor("circular_grail_passes", "Merge layers"),
					AlgorithmMetricDescriptor("circular_grail_comparisons", "Merge comparisons"), AlgorithmMetricDescriptor("circular_grail_buffer_writes", "Circular lane writes"),
					AlgorithmMetricDescriptor("circular_grail_wraps", "Ring boundary wraps")
				}, MaxItemCount, "orders square-root-sized runs then performs stable merges through a reusable circular lane whose head advances between ranges and layers",
				"merge-hybrid", "circular-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnstableGrail, "arrayv.unstable_grail_sort", "Unstable Grail Sort", "O(n log n)", "O(n) merge workspace", false, false, true, true, &buildArrayvUnstableGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("unstable_grail_runs", "Sixteen-item insertion runs"), AlgorithmMetricDescriptor("unstable_grail_merges", "Block merge ranges"),
					AlgorithmMetricDescriptor("unstable_grail_passes", "Merge layers"), AlgorithmMetricDescriptor("unstable_grail_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("unstable_grail_buffer_writes", "Buffer item writes"),
					AlgorithmMetricDescriptor("unstable_grail_equal_right_choices", "Equal-key right-run selections")
				}, MaxItemCount, "uses buffered block merges with a right-biased equality rule, so equal-key records may change relative order while key order and item identity remain intact",
				"merge-hybrid", "unstable-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDropMerge, "arrayv.drop_merge_sort", "Drop Merge Sort", "O(n log n) typical; heap fallback for disorder", "O(n) monotone-run and drop lanes", false, true, true, true, &buildArrayvDropMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("drop_merge_kept", "Items kept in the monotone subsequence"),
					AlgorithmMetricDescriptor("drop_merge_dropped", "Items buffered after an inversion"),
					AlgorithmMetricDescriptor("drop_merge_scan_comparisons", "Run scan and merge comparisons"),
					AlgorithmMetricDescriptor("drop_merge_early_fallbacks", "Early heap fallbacks"),
					AlgorithmMetricDescriptor("drop_merge_suffix_heap_sorts", "Buffered suffix sorts"),
					AlgorithmMetricDescriptor("drop_merge_final_merges", "Final subsequence merges")
				}, MaxItemCount, "keeps a monotone subsequence, buffers out-of-order items and merges their sorted suffix; when more than 60 percent of a quarter-or-larger scan is dropped, it abandons the subsequence and heap-sorts the original range",
				"merge-hybrid", "drop-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMedianMerge, "arrayv.median_merge_sort", "Median Merge Sort", "O(n log n) typical; O(n²) partition worst case", "O(n) merge buffer", false, true, true, true, &buildArrayvMedianMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("median_merge_partitions", "Quick partitions"),
					AlgorithmMetricDescriptor("median_merge_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("median_merge_bad_partitions", "Highly unbalanced splits"),
					AlgorithmMetricDescriptor("median_merge_median_of_medians_pivots", "Median-of-medians pivots"),
					AlgorithmMetricDescriptor("median_merge_initial_runs", "Sixteen-item merge-sort runs"),
					AlgorithmMetricDescriptor("median_merge_merge_ranges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("median_merge_merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("median_merge_maximum_depth", "Quick partition depth"),
					AlgorithmMetricDescriptor("median_merge_partition_fallbacks", "No-progress partition fallbacks")
				}, MaxItemCount, "uses median-of-three partitions, switches to a median-of-medians pivot after a highly unbalanced split, and merge-sorts the smaller partition while continuing iteratively through the larger one",
				"merge-hybrid", "median-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRemi, "arrayv.remi_sort", "Remi Sort", "O(n log n) comparisons with a cbrt-way merge tree", "O(n) trace buffer; O(cbrt(n)) run cursor workspace", true, false, true, true, &buildArrayvRemiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("remi_branching_factor", "Cubic-root merge fanout"),
					AlgorithmMetricDescriptor("remi_run_length", "Square-root-cube insertion run length"),
					AlgorithmMetricDescriptor("remi_initial_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("remi_merge_passes", "Multiway merge levels"),
					AlgorithmMetricDescriptor("remi_merges", "Priority-queue merge groups"),
					AlgorithmMetricDescriptor("remi_comparisons", "Multiway merge comparisons"),
					AlgorithmMetricDescriptor("remi_auxiliary_writes", "Merge buffer writes"),
					AlgorithmMetricDescriptor("remi_insertion_runs", "Small binary-insertion runs"),
					AlgorithmMetricDescriptor("remi_binary_merge_passes", "Initial run merge passes")
				}, MaxItemCount, "forms insertion-sorted runs of ceil(cuberoot(N)) squared items, then merges up to ceil(cuberoot(N)) runs at a time through deterministic priority-queue-style selections and replayable run cursors",
				"merge-hybrid", "remi", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvChalice, "arrayv.chalice_sort", "Chalice Sort", "O(n log n) block-merge adaptation", "O(n) auxiliary item workspace", true, false, true, true, &buildArrayvChaliceRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("chalice_distinct_keys", "Distinct block keys collected"),
					AlgorithmMetricDescriptor("chalice_key_probes", "Key selection probes"),
					AlgorithmMetricDescriptor("chalice_blocks", "Sorted blocks"),
					AlgorithmMetricDescriptor("chalice_merge_levels", "Block merge levels"),
					AlgorithmMetricDescriptor("chalice_merges", "Block merges"),
					AlgorithmMetricDescriptor("chalice_comparisons", "Key selection comparisons"),
					AlgorithmMetricDescriptor("chalice_auxiliary_writes", "Key workspace writes"),
					AlgorithmMetricDescriptor("chalice_insertion_runs", "Leaf insertion runs"),
					AlgorithmMetricDescriptor("chalice_binary_merge_passes", "Leaf merge passes")
				}, MaxItemCount, "trace-native block merge adaptation: samples distinct key items, sorts square-root-cubed runs, and combines adjacent runs with reversible two-way merge workspaces",
				"merge-hybrid", "chalice", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlan, "arrayv.flan_sort", "Flan Sort", "O(n log n) average; O(n²) worst", "O(log n) explicit range stack", false, true, true, true, &buildArrayvFlanRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("flan_partitions", "Three-way partitions"),
					AlgorithmMetricDescriptor("flan_pivot_comparisons", "Ninther sample comparisons"),
					AlgorithmMetricDescriptor("flan_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("flan_equal_items", "Items in pivot bands"),
					AlgorithmMetricDescriptor("flan_insertion_fallbacks", "Small-range insertion fallbacks"),
					AlgorithmMetricDescriptor("flan_maximum_depth", "Maximum partition depth")
				}, MaxItemCount, "trace-native Flan adaptation: median-of-nine sample pivot, three-way partitioning, iterative smaller-range scheduling, and insertion-sized leaves",
				"partition-sorts", "flan", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlux, "arrayv.flux_sort", "Flux Sort", "O(n log n) average; O(n²) worst", "O(log n) explicit range stack", false, true, true, true, &buildArrayvFluxRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("flux_analyzed_transitions", "Input transitions analyzed"),
					AlgorithmMetricDescriptor("flux_descending_transitions", "Descending transitions"),
					AlgorithmMetricDescriptor("flux_sorted_fast_paths", "Already-sorted fast paths"),
					AlgorithmMetricDescriptor("flux_reversed_fast_paths", "Reverse fast paths"),
					AlgorithmMetricDescriptor("flux_near_ordered_fallbacks", "Near-ordered insertion fallbacks"),
					AlgorithmMetricDescriptor("flux_partitions", "Median-of-five partitions"),
					AlgorithmMetricDescriptor("flux_pivot_comparisons", "Pivot sample comparisons"),
					AlgorithmMetricDescriptor("flux_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("flux_equal_items", "Items in pivot bands"),
					AlgorithmMetricDescriptor("flux_insertion_fallbacks", "Small-range fallbacks"),
					AlgorithmMetricDescriptor("flux_maximum_depth", "Maximum partition depth")
				}, MaxItemCount, "trace-native Flux adaptation: analyzes adjacent disorder, handles sorted and reversed inputs directly, uses insertion on near-ordered inputs, and partitions general inputs around a median-of-five sample",
				"partition-sorts", "flux", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvKota, "arrayv.kota_sort", "Kota Sort", "O(n log n) merge comparisons", "O(n) trace buffer; O(cuberoot(n)) initial runs", true, false, true, true, &buildArrayvKotaRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("kota_block_length", "Initial block length"),
					AlgorithmMetricDescriptor("kota_initial_blocks", "Initial sorted blocks"),
					AlgorithmMetricDescriptor("kota_merge_levels", "Four-block merge levels"),
					AlgorithmMetricDescriptor("kota_merges", "Block merges"),
					AlgorithmMetricDescriptor("kota_comparisons", "Four-way merge comparisons"),
					AlgorithmMetricDescriptor("kota_auxiliary_writes", "Merge buffer writes")
				}, MaxItemCount, "trace-native Kota block adaptation: sorts square-root-sized blocks, then merges up to four adjacent runs per pass using replayable run cursors",
				"merge-hybrid", "kota", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedWeaveMerge, "arrayv.optimized_weave_merge_sort", "Optimized Weave Merge Sort", "O(n log n) comparisons; O(n log² n) moves", "O(log n) merge stack", true, true, true, true, &buildArrayvOptimizedWeaveMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_weave_blocks", "Weave blocks"),
					AlgorithmMetricDescriptor("optimized_weave_bit_reversals", "Bit-reversal swaps"),
					AlgorithmMetricDescriptor("optimized_weave_insertion_passes", "Base weave insertions"),
					AlgorithmMetricDescriptor("optimized_weave_merge_comparisons", "Rotation-merge bound comparisons"),
					AlgorithmMetricDescriptor("optimized_weave_rotation_swaps", "Rotation swaps"),
					AlgorithmMetricDescriptor("optimized_weave_rotations", "Rotations")
				}, MaxItemCount, "trace-native optimized-weave adaptation: initializes two-item leaves and combines them with stable binary-cut rotations in a bottom-up power-of-two schedule",
				"merge-hybrid", "optimized-weave-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWiki, "arrayv.wiki_sort", "Wiki Sort", "O(n log n), adaptive on natural runs", "O(sqrt(n)) reusable block cache", true, false, true, true, &buildArrayvWikiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("wiki_natural_runs", "Natural ascending runs"),
					AlgorithmMetricDescriptor("wiki_reversed_runs", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("wiki_extensions", "Min-run extensions"),
					AlgorithmMetricDescriptor("wiki_merges", "Run merges"),
					AlgorithmMetricDescriptor("wiki_buffered_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("wiki_rotation_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("wiki_buffer_writes", "Cache item writes"),
					AlgorithmMetricDescriptor("wiki_comparisons", "Merge and run comparisons"),
					AlgorithmMetricDescriptor("wiki_merge_comparisons", "Cache-bound searches"),
					AlgorithmMetricDescriptor("wiki_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "trace-native Wiki adaptation: forms natural runs, extends short runs, then merges through a reusable square-root cache with stable rotation fallback",
				"merge-hybrid", "wiki", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvYujisBufferedMerge2, "arrayv.yujis_buffered_merge_sort2", "Yuji's Buffered Merge Sort 2", "O(n log n)", "O(n/2) reusable item buffer", true, false, true, true, &buildArrayvYujisBufferedMerge2Request,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("yuji_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("yuji_merge_levels", "Merge levels"),
					AlgorithmMetricDescriptor("yuji_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("yuji_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("yuji_buffer_writes", "Redistributed buffer writes")
				}, MaxItemCount, "trace-native Yuji buffered-merge adaptation: creates insertion-sized runs and alternates forward or backward merging depending on which side fits the half-size buffer",
				"merge-hybrid", "yujis-buffered-merge-2", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAndrey, "arrayv.andrey_sort", "Andrey Sort", "O(n log n) comparisons; O(n log² n) moves", "O(log n) recursive merge stack", true, true, true, true, &buildArrayvAndreyRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("andrey_runs", "Square-root-sized runs"),
					AlgorithmMetricDescriptor("andrey_merge_levels", "Block merge levels"),
					AlgorithmMetricDescriptor("andrey_merge_comparisons", "Binary-bound comparisons"),
					AlgorithmMetricDescriptor("andrey_rotations", "Stable block rotations"),
					AlgorithmMetricDescriptor("andrey_rotation_swaps", "Rotation item swaps")
				}, MaxItemCount, "trace-native Andrey adaptation: insertion-sorts square-root-sized blocks and joins them with stable recursive binary-cut in-place merges",
				"merge-sort", "andrey", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvNewShuffleMerge, "arrayv.new_shuffle_merge_sort", "New Shuffle Merge Sort", "O(n log n) comparisons; O(n²) movement worst case", "O(1) auxiliary item workspace", true, true, true, true, &buildArrayvNewShuffleMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("new_shuffle_runs", "Pair runs"),
					AlgorithmMetricDescriptor("new_shuffle_merge_levels", "Shuffle merge levels"),
					AlgorithmMetricDescriptor("new_shuffle_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("new_shuffle_rotations", "Right-run rotations")
				}, 512, "trace-native New Shuffle adaptation: forms ordered pairs and merges adjacent runs by stable rotations that insert each right-run item at its output boundary; capped at 512 items for the trace memory budget",
				"merge-sort", "new-shuffle-merge", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBlockInsertion, "arrayv.block_insertion", "Block Insertion Sort", "O(n²) worst case; adapts to existing monotone runs", "O(log n) recursion, O(1) item workspace", true, true, true, true, &buildArrayvBlockInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_insertion_runs", "Monotone runs"),
					AlgorithmMetricDescriptor("block_insertion_run_reversals", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("block_insertion_short_insertions", "Short-run insertions"),
					AlgorithmMetricDescriptor("block_insertion_merges", "Run merges"),
					AlgorithmMetricDescriptor("block_insertion_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("block_insertion_rotations", "Stable block rotations")
				}, MaxItemCount, "detects monotone runs, reverses strict descending runs, inserts singleton or two-item runs into the sorted prefix, and symmetrically merges longer runs in place",
				"insertion-methods", "block-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDoubleInsertion, "arrayv.double_insertion", "Double Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvDoubleInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"stable center-outward insertion of key pairs, with linear searches and a final right-side insertion",
				"insertion-methods", "double-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryDoubleInsertion, "arrayv.binary_double_insertion", "Binary Double Insertion Sort", "O(n²) moves, O(n log n) comparisons", "O(1)", true, true, true, true, &buildArrayvBinaryDoubleInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"stable center-outward insertion of key pairs, using binary bounds and a final right-side insertion",
				"insertion-methods", "binary-double-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroSort, "arrayv.intro_sort", "IntroSort", "average O(n log n), worst O(n log n)", "O(log n) stack", false, false, true, true, &buildArrayvIntroSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"),
					AlgorithmMetricDescriptor("heap_fallbacks", "Heap fallbacks"), AlgorithmMetricDescriptor("insertion_passes", "Final insertion passes")
				}, MaxItemCount, "median-of-three Hoare partitions with a 2 floor(log2 N) depth budget, heap fallback, and final whole-array insertion pass at threshold 16",
				"hybrid-sorts", "intro-sort", "Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPatience, "arrayv.patience_sort", "Patience Sort", "O(n log n)", "O(n) auxiliary piles and priority queue", false, false, true, true, &buildArrayvPatienceRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pile_count", "Piles"), AlgorithmMetricDescriptor("pile_assignments", "Pile assignments"),
					AlgorithmMetricDescriptor("maximum_pile_height", "Maximum pile height"),
					AlgorithmMetricDescriptor("heap_pushes", "Priority queue pushes"), AlgorithmMetricDescriptor("heap_pops", "Priority queue pops")
				}, MaxItemCount, "binary-searches sorted pile tops, pushes items onto auxiliary piles, then merges pile tops with a priority queue; equal-key pile placement follows deterministic binary-search matching",
				"insertion-methods", "patience", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvShellParallel, "arrayv.shell_sort_parallel", "Shell Sort (Parallel Phases)", "sequence dependent", "O(1)", false, true, true, true, &buildArrayvShellParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap"),
					AlgorithmMetricDescriptor("parallel_tasks", "Residue tasks")
				}, MaxItemCount, "extended-Ciura gap passes sort disjoint residue classes; the original concurrent threads are represented as deterministic phase groups",
				"insertion-methods", "shell-parallel", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeInsertion, "arrayv.merge_insertion_sort", "Merge-Insertion Sort", "comparison-optimized; O(n²) movement", "O(1)", false, true, true, true, &buildArrayvMergeInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_levels", "Block levels"), AlgorithmMetricDescriptor("block_searches", "Block searches"),
					AlgorithmMetricDescriptor("block_orderings", "Block orderings"), AlgorithmMetricDescriptor("block_insertions", "Block insertions"),
					AlgorithmMetricDescriptor("block_swaps", "Block swaps"), AlgorithmMetricDescriptor("block_reversals", "Block reversals")
				}, MaxItemCount, "in-place block-based merge-insertion schedule using doubling block sizes, block binary search, Jacobsthal-guided insertion groups, block swaps, and block reversals",
				"merge-hybrid", "merge-insertion", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStalinOverwrite, "arrayv.stalin_overwrite", "Stalin Sort (Pinned Overwrite)", "O(n)", "O(1)", false, true, true, true, &buildArrayvStalinOverwriteRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("value_rewrites", "Value rewrites") },
				MaxItemCount, "pinned source behavior: replace each out-of-order cell's key with the previous cell's key while retaining that cell's stable identity",
				"novelty-sorts", "stalin-overwrite", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::CanonicalStalinFilter, "canonical.stalin_discard_filter", "Stalin Sort (Discard Filter)", "O(n)", "O(n) discard provenance", false, false, true, true, &buildCanonicalStalinFilterRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("discarded_items", "Discarded items"), AlgorithmMetricDescriptor("kept_items", "Retained items")
				}, MaxItemCount, "canonical monotone-subsequence demonstration that sends every out-of-order item to a reversible discard lane",
				"novelty-sorts", "stalin-discard", "Novelty Sorts", std::vector<SemanticRole>{ SemanticRole::DiscardedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicTree, "arrayv.classic_tree_sort", "Classic Tree Sort", "O(n²) worst case", "O(n) index workspaces", true, false, true, true, &buildArrayvClassicTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "pinned index-array unbalanced BST: lower and upper child-index workspaces, equal keys descend right, then iterative in-order extraction",
				"tree-sorts", "classic-unbalanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTree, "arrayv.tree_sort", "Tree Sort (Unbalanced)", "O(n²) worst case", "O(n) nodes", true, false, true, true, &buildArrayvTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "unbalanced pointer-style BST with stable right insertion for equal keys, node-item workspace, child-index lanes, and in-order extraction",
				"tree-sorts", "unbalanced-node", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStrand, "arrayv.strand_sort", "Strand Sort", "O(n²) worst case", "O(n) auxiliary strands", false, false, true, true, &buildArrayvStrandRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("strands", "Extracted strands"), AlgorithmMetricDescriptor("strand_items", "Strand items"),
					AlgorithmMetricDescriptor("merge_comparisons", "Merge comparisons"), AlgorithmMetricDescriptor("last_strand_length", "Last strand length")
				}, MaxItemCount, "repeatedly extracts a direction-ordered subsequence from the remaining prefix, merges it into a persistent sorted run, then materializes that run through item-preserving swaps",
				"merge-sort", "strand", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAATree, "arrayv.aa_tree_sort", "Tree Sort (AA Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvAATreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "AA level height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_skews", "Skews"),
					AlgorithmMetricDescriptor("tree_splits", "Splits"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "AA tree insertion with level-based skew and split rebalancing, equal keys routed right, and iterative in-order extraction",
				"tree-sorts", "aa-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAVLTree, "arrayv.avl_tree_sort", "Tree Sort (AVL Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvAVLTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "height-balanced AVL insertion with single and double rotations, equal keys routed right, and iterative in-order extraction",
				"tree-sorts", "avl-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSplayTree, "arrayv.splay_tree_sort", "Splay Tree Sort", "O(n log n) amortized", "O(n) nodes", false, false, true, true, &buildArrayvSplayTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Splay rotations"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "splays each insertion key toward the root with zig, zig-zig, and zig-zag rotations before inserting the new node above the root",
				"tree-sorts", "splay", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRedBlackTree, "arrayv.red_black_tree_sort", "Tree Sort (Red-Black Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvRedBlackTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_recolors", "Recolors"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "red-black BST insertion with explicit parent links, recoloring and mirrored single/double rotations; equal keys route right",
				"tree-sorts", "red-black-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRecursiveShell, "arrayv.recursive_shell", "Recursive Shell Sort (3-Smooth)", "sequence dependent", "O(1)", false, true, true, true, &buildArrayvRecursiveShellRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "insertion-methods", "recursive-shell", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Merge, "merge", "Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildMergeRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::BottomUp), "bottom_up", "Bottom-up", "execution_mode",
						AlgorithmOptionTarget::MergeExecutionMode, true),
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::TopDown), "top_down", "Top-down", "execution_mode",
						AlgorithmOptionTarget::MergeExecutionMode),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AnimatedWrites), "animated_writes", "Animated item writes", "writeback_mode",
						AlgorithmOptionTarget::MergeWritebackMode),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AtomicRangeWrite), "compare_then_write_range", "Compare, then write range", "writeback_mode",
						AlgorithmOptionTarget::MergeWritebackMode, true)
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes")
				}, MaxItemCount, "general permutation sort", "merge-sort", "merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryMerge, "arrayv.binary_merge_sort", "Binary Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvBinaryMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binary_insertion_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("merged_ranges", "Merged ranges"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "recursive merge with stable binary-insertion runs below 64 items", "merge-hybrid", "binary-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIterativeTopDownMerge, "arrayv.iterative_top_down_merge_sort", "Iterative Top-down Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvIterativeTopDownMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge layers")
				}, MaxItemCount, "iteratively merges proportionally partitioned subarrays over ceil-power-of-two layers", "merge-sort", "iterative-top-down", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvInPlaceMerge, "arrayv.in_place_merge_sort", "In-Place Merge Sort", "O(n²) movement", "O(log n) stack", false, true, true, true, &buildArrayvInPlaceMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("in_place_merges", "In-place merges"),
					AlgorithmMetricDescriptor("in_place_merge_swaps", "Adjacent merge swaps"),
					AlgorithmMetricDescriptor("merge_comparisons", "Merge comparisons")
				}, 512, "stable recursive merge; each incoming right-run item replaces a larger left item, then the displaced value bubbles through the right run",
				"merge-sort", "in-place-bubble-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelMerge, "arrayv.parallel_merge_sort", "Parallel Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvParallelMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_merge_tasks", "Parallel sibling task groups"),
					AlgorithmMetricDescriptor("merge_passes", "Merges"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "top-down merge with deterministic sibling phase groups; the app serializes task order in the trace worker while preserving parallel grouping",
				"merge-sort", "parallel", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMerge, "arrayv.rotate_merge_sort", "Rotate Merge Sort", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("rotate_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps"),
					AlgorithmMetricDescriptor("binary_search_comparisons", "Binary-search comparisons")
				}, MaxItemCount, "bottom-up stable merge using asymmetric binary searches and in-place block-swap rotations",
				"merge-sort", "rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMergeParallel, "arrayv.rotate_merge_sort_parallel", "Rotate Merge Sort (Deterministic Parallel Phases)", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvRotateMergeParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Recursive merge tasks"),
					AlgorithmMetricDescriptor("rotate_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps"),
					AlgorithmMetricDescriptor("binary_search_comparisons", "Binary-search comparisons"),
					AlgorithmMetricDescriptor("parallel_sort_tasks", "Parallel sort groups"),
					AlgorithmMetricDescriptor("parallel_merge_tasks", "Parallel merge groups"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "top-down rotate merge; disjoint sort and merge siblings are serialized into deterministic semantic phase groups",
				"merge-sort", "parallel-rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessRotateMerge, "arrayv.stackless_rotate_merge_sort", "Stackless Rotate Merge Sort", "O(n log² n)", "O(1) item workspace", true, true, true, true, &buildArrayvStacklessRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("partition_merges", "Partition merges"),
					AlgorithmMetricDescriptor("partition_merge_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps")
				}, MaxItemCount, "iterative stable partition merging with binary split searches and in-place block rotations; no recursion or auxiliary item lane",
				"merge-sort", "stackless-rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBlockSwapMerge, "arrayv.block_swap_merge_sort", "Block-Swap Merge Sort", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvBlockSwapMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("block_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("block_merge_comparisons", "Binary-search comparisons"),
					AlgorithmMetricDescriptor("block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("block_item_swaps", "Item swaps")
				}, MaxItemCount, "bottom-up merge using a symmetric binary search for the largest interchangeable suffix/prefix, followed by block swaps",
				"merge-sort", "block-swap", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeavedMerge, "arrayv.weaved_merge_sort", "Weaved Merge Sort", "O(n log n)", "O(n) auxiliary", false, false, true, true, &buildArrayvWeavedMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("weave_merge_ranges", "Stride merge ranges"),
					AlgorithmMetricDescriptor("weave_depth", "Maximum stride depth"),
					AlgorithmMetricDescriptor("weave_comparisons", "Weave comparisons")
				}, MaxItemCount, "recursively merges interleaved residue classes through a shared auxiliary array; source index-based equal-key routing can reorder original equal items across strides",
				"merge-sort", "weaved", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTwinMerge, "arrayv.twin_sort", "Twin Sort", "O(n log n)", "O(n) allocated buffer (at most n/2 items)", true, false, true, true, &buildArrayvTwinMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("twin_pair_comparisons", "Pair and merge comparisons"),
					AlgorithmMetricDescriptor("twin_run_reversals", "Detected run reversals"),
					AlgorithmMetricDescriptor("twin_merge_passes", "Tail-merge passes"),
					AlgorithmMetricDescriptor("twin_merged_ranges", "Merged ranges"),
					AlgorithmMetricDescriptor("twin_buffer_writes", "Right-run buffer writes")
				}, MaxItemCount, "normalizes alternating descending pair-runs, then bottom-up merges from the tail using a half-size right-run buffer",
				"merge-sort", "twin", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTim, "arrayv.tim_sort", "Tim Sort", "O(n log n), adaptive O(n)", "O(n) temporary item workspace", true, false, true, true, &buildArrayvTimRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tim_runs", "Natural runs"),
					AlgorithmMetricDescriptor("tim_natural_runs", "Detected natural runs"),
					AlgorithmMetricDescriptor("tim_run_reversals", "Descending runs reversed"),
					AlgorithmMetricDescriptor("tim_binary_insertions", "Min-run insertions"),
					AlgorithmMetricDescriptor("tim_min_run", "Minimum run length"),
					AlgorithmMetricDescriptor("tim_merge_passes", "Run merges"),
					AlgorithmMetricDescriptor("tim_merge_comparisons", "Merge comparisons")
				}, MaxItemCount, "detects natural monotone runs, reverses strictly descending runs, extends short runs with stable binary insertion, and collapses runs by TimSort length invariants",
				"merge-hybrid", "tim", "Merge Hybrids"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCocktailMerge, "arrayv.cocktail_merge_sort", "Cocktail Merge Sort", "O(n log n) with fixed min-run preparation", "O(n) temporary item workspace", true, false, true, true, &buildArrayvCocktailMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tim_cocktail_runs", "Cocktail-built runs"),
					AlgorithmMetricDescriptor("tim_min_run", "Minimum run length"),
					AlgorithmMetricDescriptor("tim_merge_passes", "Run merges"),
					AlgorithmMetricDescriptor("tim_merge_comparisons", "Merge comparisons")
				}, MaxItemCount, "sorts fixed min-run chunks with Cocktail Shaker passes, then merges those runs with TimSort's stack invariants",
				"merge-hybrid", "cocktail-merge", "Merge Hybrids"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvImprovedInPlaceMerge, "arrayv.improved_in_place_merge_sort", "Improved In-Place Merge Sort", "O(n log² n)", "O(log n) recursion, O(1) item workspace", true, true, true, true, &buildArrayvImprovedInPlaceMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("inplace_merge_ranges", "Symmetric merge ranges"),
					AlgorithmMetricDescriptor("inplace_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("inplace_merge_rotations", "Stable block rotations"),
					AlgorithmMetricDescriptor("inplace_merge_rotation_swaps", "Rotation item swaps"),
					AlgorithmMetricDescriptor("inplace_merge_insertion_runs", "Small insertion runs")
				}, MaxItemCount, "stable symmetric in-place merge with lower/upper-bound cuts and order-preserving triple-reversal rotations; small ranges use binary insertion",
				"merge-sort", "improved-in-place", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedBottomUpMerge, "arrayv.optimized_bottom_up_merge_sort", "Optimized Bottom-Up Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvOptimizedBottomUpMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binary_insertion_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("merge_passes", "Merge passes")
				}, MaxItemCount, "forms stable 16-item binary-insertion runs, then alternates merge passes between primary and auxiliary lanes", "merge-hybrid", "optimized-bottom-up-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Quick, "quick", "Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average, O(n) worst", false, true, true, true, &buildQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Partitions"),
					AlgorithmMetricDescriptor("maximum_depth", "Maximum depth")
				}, MaxItemCount, "general permutation sort", "partition-sorts", "quick", "Partition Sorts",
				std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLLQuick, "arrayv.ll_quick_sort", "Quick Sort (Left-Left Pointers)", "O(n²) worst", "O(n) recursion", false, false, true, true, &buildArrayvLLQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 128,
				"Lomuto partition with a final-element pivot and left-side boundary; restricted to 128 items for stack safety",
				"partition-sorts", "left-left", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLRQuick, "arrayv.lr_quick_sort", "Quick Sort (Left-Right Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvLRQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"Hoare partition with inward left-right scans around the midpoint value",
				"partition-sorts", "left-right", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLRQuickParallel, "arrayv.lr_quick_sort_parallel", "Quick Sort (Left-Right Parallel Schedule)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvLRQuickParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"midpoint Hoare partition; disjoint recursive branches represented as deterministic phase groups",
				"partition-sorts", "left-right-parallel", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDualPivotQuick, "arrayv.dual_pivot_quick_sort", "Dual-Pivot Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("insertion_fallback_ranges", "Insertion fallback ranges") }, MaxItemCount,
				"two sampled pivots, three-way partitioning, adaptive divisor and insertion fallback for ranges of four items or fewer",
				"partition-sorts", "dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedDualPivotQuick, "arrayv.optimized_dual_pivot_quick_sort", "Optimized Dual-Pivot Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvOptimizedDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("insertion_fallback_ranges", "Insertion fallback ranges"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, MaxItemCount,
				"two sampled pivots, adaptive divisor, 27-item insertion fallback and an equal-key gathering pass for balanced duplicate-heavy partitions",
				"partition-sorts", "optimized-dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableQuick, "arrayv.stable_quick_sort", "Stable Quick Sort", "O(n²) worst", "O(n) auxiliary", true, false, true, true, &buildArrayvStableQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"stable first-pivot partition into ordered left and right reference lanes, followed by stable recursive passes",
				"partition-sorts", "stable-quick", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableQuickParallel, "arrayv.stable_quick_sort_parallel", "Stable Quick Sort (Deterministic Phases)", "expected O(n log n)", "O(n) auxiliary", true, false, true, true, &buildArrayvStableQuickParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"stable random-pivot partitions with a seeded pivot stream; disjoint recursive branches are grouped in deterministic phase spans",
				"partition-sorts", "stable-quick-parallel", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvForcedStableQuick, "arrayv.forced_stable_quick_sort", "Forced-Stable Quick Sort", "average O(n log n), worst O(n²)", "O(n) order workspace", true, false, true, true, &buildArrayvForcedStableQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("stable_tie_breaks", "Original-order tie checks") }, MaxItemCount,
				"median-of-three Hoare partition with an integer original-position workspace used as a stable tie-break key",
				"partition-sorts", "forced-stable-quick", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryLLQuick, "arrayv.ternary_ll_quick_sort", "Ternary Quick Sort (Left-Left Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, true, true, true, &buildArrayvTernaryLLQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, 128,
				"median-of-three left-left scans move equal keys to a trailing block before placing the pivot-equivalent region",
				"partition-sorts", "ternary-left-left", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryLRQuick, "arrayv.ternary_lr_quick_sort", "Ternary Quick Sort (Left-Right Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, true, true, true, &buildArrayvTernaryLRQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, 128,
				"median-of-three inward scans collect equal keys at both ends, then rotate them beside the pivot",
				"partition-sorts", "ternary-left-right", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStablePermutation, "arrayv.stable_permutation_sort", "Stable Permutation Sort", "O(n! × n)", "O(n) index workspace", false, false, true, true, &buildArrayvStablePermutationRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("index_permutation_swaps", "Index permutation swaps"),
					AlgorithmMetricDescriptor("permutations_examined", "Permutations examined")
				}, 8, "factorial Heap-style index permutation enumeration with a hard eight-item safety limit",
				"permutation-sorts", "stable-permutation", "Permutation Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTable, "arrayv.table_sort", "Table Sort", "average O(n log n), worst O(n²)", "O(n) index table", true, false, true, true, &buildArrayvTableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Table partitions"),
					AlgorithmMetricDescriptor("maximum_depth", "Maximum table depth"),
					AlgorithmMetricDescriptor("table_index_swaps", "Index table swaps"),
					AlgorithmMetricDescriptor("permutation_cycles", "Permutation cycles"),
					AlgorithmMetricDescriptor("permutation_cycle_moves", "Cycle moves")
				}, MaxItemCount, "stable indirect quicksort of an integer index table followed by cycle-based primary-array permutation",
				"indirect-sorts", "table", "Indirect Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Heap, "heap", "Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryHeap, "arrayv.ternary_heap", "Ternary Heap Sort", "O(n log₃ n)", "O(1)", false, true, true, true, &buildArrayvTernaryHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "ternary", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBaseNHeap, "arrayv.base_n_heap", "Base-N Max Heap Sort", "O(n logᵦ n)", "O(1)", false, true, true, true, &buildArrayvBaseNHeapRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(4, "base_4", "Base 4", "heap_arity", AlgorithmOptionTarget::HeapArity, true),
					AlgorithmOptionDescriptor(2, "base_2", "Base 2", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(3, "base_3", "Base 3", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(8, "base_8", "Base 8", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(16, "base_16", "Base 16", "heap_arity", AlgorithmOptionTarget::HeapArity)
				}, std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "base-n", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMinHeap, "arrayv.min_heap", "Min Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvMinHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "min-heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlippedMinHeap, "arrayv.flipped_min_heap", "Flipped Min Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvFlippedMinHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "flipped-min-heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBottomUpHeap, "arrayv.bottom_up_heap", "Bottom-Up Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvBottomUpHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "bottom-up", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinomialHeap, "arrayv.binomial_heap", "Binomial Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvBinomialHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "binomial", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazyHeap, "arrayv.lazy_heap", "Lazy Heap Sort", "O(n√n)", "O(1)", false, true, true, true, &buildArrayvLazyHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "lazy", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMinMaxHeap, "arrayv.min_max_heap", "Min-Max Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvMinMaxHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "min-max", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOutOfPlaceHeap, "arrayv.out_of_place_heap", "Out-of-Place Heap Sort", "O(n log n)", "O(n)", false, false, true, true, &buildArrayvOutOfPlaceHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "out-of-place", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTriangularHeap, "arrayv.triangular_heap", "Triangular Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvTriangularHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "triangular", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeakHeap, "arrayv.weak_heap", "Weak Heap Sort", "O(n log n)", "O(n) bits", false, true, true, true, &buildArrayvWeakHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "weak", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPoplarHeap, "arrayv.poplar_heap", "Poplar Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvPoplarHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "poplar", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Counting, "counting", "Counting Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildCountingRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"distribution-sorts", "counting", "Distribution Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Radix, "radix", "Radix Sort", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildRadixRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("digit_passes", "Digit passes")
				}, MaxItemCount, "general permutation sort", "distribution-sorts", "radix", "Distribution Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Gnome, "gnome", "Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "gnome", "Exchange Sorts", std::vector<SemanticRole>(), true),
			AlgorithmDescriptor(SortAlgorithm::Cocktail, "cocktail", "Cocktail Shaker Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Comb, "comb", "Comb Sort", "O(n²) worst", "O(1)", false, true, true, true, &buildCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::OddEven, "odd_even", "Odd-Even Transposition Sort", "O(n²)", "O(1)", true, true, true, true, &buildOddEvenRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "odd-even", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Pancake, "pancake", "Pancake Sort", "O(n²)", "O(1)", false, true, true, true, &buildPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "pancake", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBurntPancake, "arrayv.burnt_pancake", "Burnt Pancake Sort (ArrayV schedule)", "O(n²)", "O(1)", false, true, true, true, &buildArrayvBurntPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort with orientation changes",
				"pancake-sorts", "arrayv-burnt", "Pancake Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum, SemanticRole::CurrentMinimum }),
			AlgorithmDescriptor(SortAlgorithm::CanonicalBurntPancake, "burnt-pancake-canonical", "Burnt Pancake Sort (Canonical)", "O(n²)", "O(1)", false, true, true, true, &buildCanonicalBurntPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort with orientation changes",
				"pancake-sorts", "burnt-canonical", "Pancake Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum, SemanticRole::CurrentMinimum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPancakeInsertion, "arrayv.pancake_insertion", "Pancake Insertion Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvPancakeInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"pancake-sorts", "pancake-insertion", "Pancake Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Stooge, "stooge", "Stooge Sort", "O(n^2.71)", "O(n)", false, true, true, true, &buildStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16, "novelty demonstration",
				"recursive-novelty", "stooge", "Recursive Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBufferedStooge, "arrayv.buffered_stooge_sort", "Buffered Stooge Sort", "superlinear recursive growth", "O(log n) stack, O(1) extra item workspace", false, true, true, true, &buildArrayvBufferedStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("buffered_stooge_calls", "Recursive calls"),
					AlgorithmMetricDescriptor("buffered_stooge_comparisons", "Comparisons"),
					AlgorithmMetricDescriptor("buffered_stooge_buffer_swaps", "Front-buffer swaps"),
					AlgorithmMetricDescriptor("buffered_stooge_cleanup_swaps", "Cleanup swaps")
				}, 8, "recursively sorts middle and right thirds, exchanges their merge into the front of the same array, then cleans up by recursive tail sorting and backward bubbling",
				"recursive-novelty", "buffered-stooge", "Recursive Novelty Sorts"),
		AlgorithmDescriptor(SortAlgorithm::Bogo, "bogo", "Bogo Sort", "O(n·n!) expected", "O(1)", false, true, true, true, &buildBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 8, "stochastic novelty demonstration",
				"stochastic-novelty", "bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBozo, "arrayv.bozo_sort", "Bozo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvBozoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random pair swaps") }, 8,
				"stochastic permutation sort using unconditional random pair swaps; seeded and limited to 8 items",
				"stochastic-novelty", "bozo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBubbleBogo, "arrayv.bubble_bogo_sort", "Bubble Bogo Sort", "O(n² log n) expected", "O(1)", false, true, true, true, &buildArrayvBubbleBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random adjacent probes") }, 8,
				"stochastic adjacent-exchange sort; seeded and limited to 8 items",
				"stochastic-novelty", "bubble-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvExchangeBogo, "arrayv.exchange_bogo_sort", "Exchange Bogo Sort", "O(n² log n) expected", "O(1)", false, true, true, true, &buildArrayvExchangeBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random pair probes") }, 8,
				"stochastic inversion-only random-pair exchange sort; seeded and limited to 8 items",
				"stochastic-novelty", "exchange-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDeterministicBogo, "arrayv.deterministic_bogo_sort", "Deterministic Bogo Sort", "O(n·n!)", "O(n) recursion", false, false, true, true, &buildArrayvDeterministicBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("permutation_checks", "Permutations checked") }, 5,
				"deterministic Heap-permutation enumeration; factorial demonstration limited to 5 items",
				"stochastic-novelty", "deterministic-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSelectionBogo, "arrayv.selection_bogo_sort", "Selection Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvSelectionBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random minimum swaps") }, 8,
				"seeded random selection of a suffix minimum into each next output position; limited to 8 items",
				"stochastic-novelty", "selection-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLessBogo, "arrayv.less_bogo_sort", "Less Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvLessBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Suffix shuffles") }, 8,
				"seeded Fisher-Yates suffix shuffle until each next output position contains a minimum; limited to 8 items",
				"stochastic-novelty", "less-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCocktailBogo, "arrayv.cocktail_bogo_sort", "Cocktail Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvCocktailBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Active-range shuffles") }, 8,
				"bidirectional Less Bogo variant that fixes an active range minimum or maximum before shuffling; seeded and limited to 8 items",
				"stochastic-novelty", "cocktail-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMedianQuickBogo, "arrayv.median_quick_bogo_sort", "Median Quick Bogo Sort", "unbounded expected", "O(log n) recursion", false, false, true, true, &buildArrayvMedianQuickBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Range shuffles"), AlgorithmMetricDescriptor("split_partitions", "Valid half splits"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 8,
				"seeded full-range shuffle until the midpoint halves are value-separated, then recurse; limited to 8 items",
				"stochastic-novelty", "median-quick-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuickBogo, "arrayv.quick_bogo_sort", "Quick Bogo Sort", "unbounded expected", "O(n) recursion", false, false, true, true, &buildArrayvQuickBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Range shuffles"), AlgorithmMetricDescriptor("partitions", "Valid partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 5,
				"seeded Fisher-Yates shuffle until a tracked pivot separates its range, then recurse; limited to 5 items",
				"stochastic-novelty", "quick-bogo", "Stochastic Novelty Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRandomGuess, "arrayv.random_guess_sort", "Random Guess Sort", "unbounded expected", "O(n)", false, false, true, true, &buildArrayvRandomGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_guesses", "Random index guesses") }, 5,
				"seeded random index tuples checked for stable ordering, then materialized through an inspectable index workspace; limited to 5 items",
				"stochastic-novelty", "random-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedGuess, "arrayv.optimized_guess_sort", "Optimized Guess Sort", "O(n^n) worst", "O(n)", true, false, true, true, &buildArrayvOptimizedGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples") }, 5,
				"least-significant-index-first exhaustive tuple enumeration, stopping at the first stably sorted index sequence; limited to 5 items",
				"stochastic-novelty", "optimized-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmartGuess, "arrayv.smart_guess_sort", "Smart Guess Sort", "O(n^n) worst", "O(n)", true, false, true, true, &buildArrayvSmartGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples") }, 6,
				"reverse-scanned stable-order check with carry advancement from the detected inversion; limited to 6 items",
				"stochastic-novelty", "smart-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvGuess, "arrayv.guess_sort", "Guess Sort", "O(n^(n+2)) worst", "O(n)", false, false, true, true, &buildArrayvGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples"), AlgorithmMetricDescriptor("index_pair_checks", "Index-pair uniqueness checks") }, 4,
				"exhaustive n-ary index tuples with all-pairs uniqueness and ordering checks; retains the latest valid stable permutation and is limited to 4 items",
				"stochastic-novelty", "guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBogoBogo, "arrayv.bogo_bogo_sort", "Bogo Bogo Sort", "unbounded nested expected", "O(n²) nested workspaces", false, false, true, true, &buildArrayvBogoBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("nested_checks", "Nested sortedness checks"), AlgorithmMetricDescriptor("random_shuffles", "Nested shuffles") }, 3,
				"recursively sorts copied prefixes, shuffles the final element into order, compares the candidate against the nested sorted copy, then shuffles the main range; trace-expansion ceiling is three items",
				"stochastic-novelty", "bogo-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeBogo, "arrayv.merge_bogo_sort", "Merge Bogo Sort", "unbounded random interleavings", "O(n) auxiliary", false, true, true, true, &buildArrayvMergeBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_weaves", "Random merge weaves") }, 8,
				"recursively sorts both halves, then samples a seeded stable weave of those runs until the merged range is ordered; limited to eight items",
				"stochastic-novelty", "merge-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmartBogoBogo, "arrayv.smart_bogo_bogo_sort", "Smart Bogo Bogo Sort", "unbounded nested expected", "O(n) recursion", false, false, true, true, &buildArrayvSmartBogoBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Prefix shuffles"), AlgorithmMetricDescriptor("recursive_prefix_retries", "Recursive prefix retries") }, 3,
				"recursively sorts each prefix; when its boundary exceeds the final item, it shuffles the full active prefix and repeats the recursive prefix sort; trace-expansion ceiling is three items",
				"stochastic-novelty", "smart-bogo-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedBubble, "arrayv.optimized_bubble", "Optimized Bubble Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildArrayvOptimizedBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnoptimizedBubble, "arrayv.unoptimized_bubble", "Unoptimized Bubble Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvUnoptimizedBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "unoptimized-bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSwaplessBubble, "arrayv.swapless_bubble", "Swapless Bubble Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvSwaplessBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general stable permutation sort",
				"exchange-sorts", "swapless-bubble", "Exchange Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryGnome, "arrayv.binary_gnome", "Binary Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvBinaryGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "binary-gnome", "Exchange Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircleIterative, "arrayv.circle_iterative", "Circle Sort (Iterative)", "O(n log² n)", "O(1) auxiliary", false, true, true, true, &buildArrayvCircleIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "circle-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircleRecursive, "arrayv.circle_recursive", "Circle Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvCircleRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "circle-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroCircleIterative, "arrayv.intro_circle_sort_iterative", "Introspective Circle Sort (Iterative)", "circle passes with O(n²) binary-insertion fallback", "O(1)", false, true, true, true, &buildArrayvIntroCircleIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circle_passes", "Circle passes"), AlgorithmMetricDescriptor("binary_fallbacks", "Binary insertion fallbacks")
				}, MaxItemCount, "iterative mirrored-pair passes stop after the pinned half-logarithmic threshold and use binary insertion",
				"exchange-sorts", "intro-circle-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroCircleRecursive, "arrayv.intro_circle_sort_recursive", "Introspective Circle Sort (Recursive)", "circle passes with O(n²) binary-insertion fallback", "O(log n) recursion", false, false, true, true, &buildArrayvIntroCircleRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circle_passes", "Circle passes"), AlgorithmMetricDescriptor("binary_fallbacks", "Binary insertion fallbacks")
				}, MaxItemCount, "recursive mirrored-pair passes stop after the pinned half-logarithmic threshold and use binary insertion",
				"exchange-sorts", "intro-circle-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvHybridComb, "arrayv.hybrid_comb_sort", "Hybrid Comb Sort", "O(n²) worst; comb passes before insertion", "O(1)", false, true, true, true, &buildArrayvHybridCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Last gap"),
					AlgorithmMetricDescriptor("insertion_fallbacks", "Insertion fallbacks")
				}, MaxItemCount, "shrink-factor 1.3 comb passes switch to insertion sort once gap reaches min(8, N/32)",
				"exchange-sorts", "hybrid-comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicThreeSmoothComb, "arrayv.classic_three_smooth_comb", "Classic 3-Smooth Comb Sort", "O(n²) worst", "O(1)", false, true, true, true, &buildArrayvClassicThreeSmoothCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "classic-three-smooth-comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombIterative, "arrayv.three_smooth_comb_iterative", "3-Smooth Comb Sort (Iterative)", "O(n²) worst", "O(1)", false, true, true, true, &buildArrayvThreeSmoothCombIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort; single pass per 3-smooth gap", "exchange-sorts", "three-smooth-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombRecursive, "arrayv.three_smooth_comb_recursive", "Recursive 3-Smooth Comb Sort", "O(n²) worst", "O(log n) stack", false, true, true, true, &buildArrayvThreeSmoothCombRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "three-smooth-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombParallel, "arrayv.three_smooth_comb_parallel", "Parallel 3-Smooth Comb Sort (deterministic phases)", "O(n²) worst", "O(log n) scheduled depth", false, true, true, true, &buildArrayvThreeSmoothCombParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "three-smooth-parallel", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicIterative, "arrayv.bitonic_sort_iterative", "Bitonic Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvBitonicIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bitonic-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicRecursive, "arrayv.bitonic_sort_recursive", "Bitonic Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvBitonicRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bitonic-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicParallel, "arrayv.bitonic_sort_parallel", "Bitonic Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvBitonicParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "bitonic-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonIterative, "arrayv.bose_nelson_sort_iterative", "Bose-Nelson Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvBoseNelsonIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bose-nelson-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonRecursive, "arrayv.bose_nelson_sort_recursive", "Bose-Nelson Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvBoseNelsonRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bose-nelson-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonParallel, "arrayv.bose_nelson_sort_parallel", "Bose-Nelson Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvBoseNelsonParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "bose-nelson-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeIterative, "arrayv.odd_even_merge_sort_iterative", "Odd-Even Merge Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvOddEvenMergeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "odd-even-merge-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeRecursive, "arrayv.odd_even_merge_sort_recursive", "Odd-Even Merge Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvOddEvenMergeRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "odd-even-merge-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeParallel, "arrayv.odd_even_merge_sort_parallel", "Odd-Even Merge Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvOddEvenMergeParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "odd-even-merge-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDiamondIterative, "arrayv.diamond_sort_iterative", "Diamond Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvDiamondIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "diamond-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDiamondRecursive, "arrayv.diamond_sort_recursive", "Diamond Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvDiamondRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "diamond-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseIterative, "arrayv.pairwise_sort_iterative", "Pairwise Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvPairwiseIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseRecursive, "arrayv.pairwise_sort_recursive", "Pairwise Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvPairwiseRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCrease, "arrayv.crease_sort", "Crease Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvCreaseRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "crease", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFold, "arrayv.fold_sort", "Fold Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvFoldRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "fold", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeExchangeIterative, "arrayv.merge_exchange_iterative", "Merge-Exchange Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvMergeExchangeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "merge-exchange-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseMergeIterative, "arrayv.pairwise_merge_iterative", "Pairwise Merge Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvPairwiseMergeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-merge-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseMergeRecursive, "arrayv.pairwise_merge_recursive", "Pairwise Merge Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvPairwiseMergeRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-merge-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveIterative, "arrayv.weave_iterative", "Weave Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvWeaveIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "weave-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveParallel, "arrayv.weave_parallel", "Weave Sort (Parallel Schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvWeaveParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "weave-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveRecursive, "arrayv.weave_recursive", "Weave Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvWeaveRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "weave-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMatrix, "arrayv.matrix_sort", "Matrix Sort", "Data-dependent iterative matrix sorting", "O(n) auxiliary recursion", false, true, true, true, &buildArrayvMatrixRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("matrix_interleave_passes", "Matrix interleave passes") },
				128, "general permutation sort; matrix interleave and insertion phases", "sorting-networks", "matrix", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedStooge, "arrayv.optimized_stooge", "Optimized Stooge Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvOptimizedStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 128,
				"incomplete demonstration; endpoint exchange with forward and backward scans can leave an inversion", "stooge-sorts", "optimized", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedStoogeStudio, "arrayv.optimized_stooge_studio", "Optimized Stooge Sort (Studio)", "O(n²) worst, O(n) near-sorted", "O(n) recursion", true, true, true, true, &buildArrayvOptimizedStoogeStudioRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 64,
				"stable permutation sort; change-guided recursive Stooge network", "stooge-sorts", "studio-optimized", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuadStooge, "arrayv.quad_stooge", "Quad Stooge Sort", "O(n^2.58) approximate", "O(log n) stack", false, true, true, true, &buildArrayvQuadStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort; six overlapping half-range calls", "stooge-sorts", "quad", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSlow, "arrayv.slow_sort", "Slow Sort", "super-polynomial practical growth", "O(log n) stack", false, true, true, true, &buildArrayvSlowRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort", "slow-sorts", "slow", "Slow Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircloid, "arrayv.circloid", "Circloid Sort", "O(n log² n)", "O(log n) stack", false, true, true, true, &buildArrayvCircloidRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"recursive mirrored-comparison exchange sort with repeated whole-range passes", "exchange-sorts", "circloid", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSilly, "arrayv.silly_sort", "Silly Sort", "exponential practical growth", "O(n) stack", false, true, true, true, &buildArrayvSillyRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort; equal minima are exchanged", "novelty-sorts", "silly", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSnuffle, "arrayv.snuffle_sort", "Snuffle Sort", "high recursive growth", "O(n) stack", false, true, true, true, &buildArrayvSnuffleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"overlapping recursive endpoint-exchange demonstration", "novelty-sorts", "snuffle", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCompleteGraph, "arrayv.complete_graph", "Complete Graph Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvCompleteGraphRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Comparator layers"), AlgorithmMetricDescriptor("network_comparators", "Comparators") }, MaxItemCount,
				"deterministic complete-graph comparator schedule emitted in reversible network layers", "sorting-networks", "complete-graph", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFun, "arrayv.fun_sort", "Fun Sort", "O(n² log n) worst", "O(1)", false, true, true, true, &buildArrayvFunRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 64,
				"incomplete demonstration: unordered binary-search exchanges can converge without globally sorting; bounded to 64 items", "novelty-sorts", "fun", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedCocktail, "arrayv.optimized_cocktail", "Optimized Cocktail Shaker Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildArrayvOptimizedCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnoptimizedCocktail, "arrayv.unoptimized_cocktail", "Unoptimized Cocktail Shaker Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvUnoptimizedCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "unoptimized-cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedGnome, "arrayv.optimized_gnome", "Optimized Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvOptimizedGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-gnome", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvShove, "arrayv.shove", "Shove Sort", "O(n³) worst", "O(1)", false, true, true, true, &buildArrayvShoveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 512, "general permutation sort",
				"exchange-sorts", "shove", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSlope, "arrayv.slope", "Slope Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvSlopeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "slope", "Exchange Sorts"),
		AlgorithmDescriptor(SortAlgorithm::ArrayvAmericanFlag, "arrayv.american_flag", "American Flag Sort", "O(w(n + R))", "O(R + w)", false, true, false, true, &buildArrayvAmericanFlagRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") },
			MaxItemCount, "general integer permutation sort", "integer-radix", "american_flag", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryQuickIterative, "arrayv.binary_quick_iterative", "Binary Quick Sort (Iterative)", "O(w·n)", "O(n)", false, true, false, true, &buildArrayvBinaryQuickIterativeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "binary_quick_iterative", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryQuickRecursive, "arrayv.binary_quick_recursive", "Binary Quick Sort (Recursive)", "O(w·n)", "O(w)", false, true, false, true, &buildArrayvBinaryQuickRecursiveRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "binary_quick_recursive", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvClassicGravity, "arrayv.classic_gravity", "Classic Gravity Sort", "O(n·k)", "O(n·k)", true, false, false, true, &buildArrayvClassicGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 128, "general integer distribution sort", "gravity-sorts", "classic_gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvFlash, "arrayv.flash", "Flash Sort", "O(n + m)", "O(m)", false, true, false, true, &buildArrayvFlashRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "flash", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvGravity, "arrayv.gravity", "Gravity Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "gravity-sorts", "gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvInPlaceLsdRadix, "arrayv.in_place_lsd_radix", "In-place LSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvInPlaceLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "inplace_lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvIndex, "arrayv.index", "Index Sort", "O(n)", "O(1)", false, true, false, true, &buildArrayvIndexRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "distribution-sorts", "index", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvLsdRadix, "arrayv.lsd_radix", "LSD Radix Sort (base 4)", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildArrayvLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvMsdRadix, "arrayv.msd_radix", "MSD Radix Sort", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildArrayvMsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "msd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvPigeonhole, "arrayv.pigeonhole", "Pigeonhole Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvPigeonholeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "pigeonhole", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvRotateLsdRadix, "arrayv.rotate_lsd_radix", "Rotate LSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvRotateLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "rotate_lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMsdRadix, "arrayv.rotate_msd_radix", "Rotate MSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvRotateMsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "rotate_msd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvShatter, "arrayv.shatter", "Shatter Sort", "O(n log n)", "O(n + √n)", true, false, false, true, &buildArrayvShatterRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "shatter", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvSimpleShatter, "arrayv.simple_shatter", "Simple Shatter Sort", "O(w·n)", "O(1)", false, true, false, true, &buildArrayvSimpleShatterRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "distribution-sorts", "simple_shatter", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvSimplisticGravity, "arrayv.simplistic_gravity", "Simplistic Gravity Sort", "O(n·k)", "O(k)", true, false, false, true, &buildArrayvSimplisticGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "gravity-sorts", "simplistic_gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStatic, "arrayv.static", "Static Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvStaticRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "static", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvTime, "arrayv.time", "Time Sort (deterministic simulation)", "O(n log n)", "O(n)", true, false, false, true, &buildArrayvTimeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("maximum_deadline", "Maximum deadline") }, 128,
			"deterministic timing demonstration", "novelty-distribution", "time", "Novelty Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessAmericanFlag, "arrayv.stackless_american_flag", "Stackless American Flag Sort", "O(w(n + R))", "O(R)", false, true, false, true, &buildArrayvStacklessAmericanFlagRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer permutation sort", "integer-radix", "stackless_american_flag", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessBinaryQuick, "arrayv.stackless_binary_quick", "Stackless Binary Quick Sort", "O(w·n)", "O(1)", false, true, false, true, &buildArrayvStacklessBinaryQuickRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "stackless_binary_quick", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation })
		};
		return algorithms;
	}

	const AlgorithmDescriptor* algorithmDescriptor(SortAlgorithm algorithm)
	{
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i) if (catalog[i].id == algorithm) return &catalog[i];
		return nullptr;
	}

	const AlgorithmDescriptor* algorithmDescriptorById(const FamilyId& family, const VariantId& variant)
	{
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			if (catalog[i].familyId == family && catalog[i].variantId == variant) return &catalog[i];
		// Settings v4 originally stored each algorithm ID as its family and "standard" as its variant.
		// Keep those pairs readable while normalizing loaded settings to the current grouped IDs.
		if (variant == "standard")
			for (std::size_t i = 0; i < catalog.size(); ++i)
				if (family == catalog[i].stableId) return &catalog[i];
		return nullptr;
	}

	bool algorithmAcceptsItemCount(SortAlgorithm algorithm, std::size_t itemCount)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor && itemCount <= descriptor->safeMaxItemCount;
	}

	const char* algorithmDisplayName(SortAlgorithm algorithm)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor ? descriptor->displayName : "Unknown Algorithm";
	}

	bool algorithmSupports(SortAlgorithm algorithm, InputProfile profile)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor && (profile == InputProfile::NormalizedFloat
			? descriptor->supportsNormalizedFloat : descriptor->supportsIntegerKeys);
	}

	bool algorithmSupports(SortAlgorithm algorithm, InputDomain domain)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (!descriptor) return false;
		if (!descriptor->supportedDomains.empty())
			return std::find(descriptor->supportedDomains.begin(), descriptor->supportedDomains.end(), domain) != descriptor->supportedDomains.end();
		return algorithmSupports(algorithm, inputProfileForDomain(domain));
	}

	const char* inputDomainName(InputDomain domain)
	{
		switch (domain)
		{
		case InputDomain::NormalizedFloats: return "Normalized floats";
		case InputDomain::NonnegativeIntegerKeys: return "Nonnegative integer keys";
		case InputDomain::SignedIntegerKeys: return "Signed integer keys";
		case InputDomain::IntegerPermutation: return "Unique integer permutation";
		}
		return "Normalized floats";
	}

	const char* inputPatternName(InputPattern pattern)
	{
		switch (pattern)
		{
		case InputPattern::Random: return "Random";
		case InputPattern::Sorted: return "Sorted";
		case InputPattern::Reversed: return "Reversed";
		case InputPattern::NearlySorted: return "Nearly sorted";
		case InputPattern::FewUnique: return "Few unique";
		case InputPattern::OrganPipe: return "Organ pipe";
		case InputPattern::AndrutReference: return "Andrut reference (100 bars)";
		}
		return "Random";
	}

	bool inputPatternCompatible(InputDomain domain, InputPattern pattern)
	{
		if (pattern == InputPattern::AndrutReference)
			return domain == InputDomain::NormalizedFloats;
		if (domain == InputDomain::IntegerPermutation &&
			(pattern == InputPattern::FewUnique || pattern == InputPattern::OrganPipe)) return false;
		return static_cast<int>(domain) >= static_cast<int>(InputDomain::NormalizedFloats) &&
			static_cast<int>(domain) <= static_cast<int>(InputDomain::IntegerPermutation) &&
			static_cast<int>(pattern) >= static_cast<int>(InputPattern::Random) &&
			static_cast<int>(pattern) <= static_cast<int>(InputPattern::AndrutReference);
	}

	InputProfile inputProfileForDomain(InputDomain domain)
	{
		return domain == InputDomain::NormalizedFloats ? InputProfile::NormalizedFloat : InputProfile::IntegerKeys;
	}

	const char* shellGapSequenceName(ShellGapSequence sequence)
	{
		switch (sequence)
		{
		case ShellGapSequence::Halving: return "Halving";
		case ShellGapSequence::Knuth: return "Knuth";
		case ShellGapSequence::Ciura: return "Ciura";
		case ShellGapSequence::Tokuda: return "Tokuda";
		}
		return "Halving";
	}

	}
