#include "AlgorithmStrategies.h"
#include "GrailTraceUtilities.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::grailBinarySearch;
		using detail::grailRotate;
		using detail::grailMergeWithoutBuffer;
		using detail::grailBufferedMerge;

		void grailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "grail_runs", "grail_merges", "grail_buffered_merges", "grail_rotation_merges",
				"grail_buffer_writes", "grail_comparisons" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t bufferCapacity = 0;
			const GrailBufferMode bufferMode = static_cast<GrailBufferMode>(r.trace.configuration.value("grail_buffer_mode", 0));
			if (bufferMode == GrailBufferMode::StaticBuffer)
				bufferCapacity = (std::min)(std::size_t(512), count / 2);
			else if (bufferMode == GrailBufferMode::DynamicBuffer)
				bufferCapacity = static_cast<std::size_t>(std::sqrt(static_cast<double>(count)));
			if (bufferCapacity > 0) r.resizeAuxiliary("grail_buffer", bufferCapacity);
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("grail_runs");
			}
		for (std::size_t width = runSize; width < count;)
		{
			const int phase = r.beginPhase("Grail block merge level width " + std::to_string(width));
			for (std::size_t first = 0; first < count; first += width * 2)
			{
				const std::size_t middle = (std::min)(count, first + width);
				const std::size_t last = (std::min)(count, middle + width);
				if (middle < last)
				{
					grailBufferedMerge(r, first, middle, last, bufferCapacity, "grail_buffer", "grail");
					r.addMetric("grail_merges");
				}
			}
			r.endPhase(phase, "Grail block merge level");
			if (width > count / 2) break;
			width *= 2;
		}
		if (bufferCapacity > 0) r.resizeAuxiliary("grail_buffer", 0);
		}

		void parallelGrailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "parallel_grail_run_size", "parallel_grail_runs", "parallel_grail_layers",
				"parallel_grail_tasks", "parallel_grail_comparisons", "parallel_grail_buffered_merges",
				"parallel_grail_rotation_merges", "parallel_grail_buffer_writes", "parallel_grail_rotation_swaps" })
				r.declareMetric(key);
			if (count < 2) return;

			const std::size_t bufferCapacity = (std::max)(std::size_t(1), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			const std::size_t runSize = 16;
			r.setMetric("parallel_grail_run_size", static_cast<std::uint64_t>(runSize));
			r.resizeAuxiliary("parallel_grail_buffer", bufferCapacity);
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("parallel_grail_runs");
			}

			for (std::size_t width = runSize; width < count;)
			{
				const int phase = r.beginPhase("Parallel Grail block-merge layer width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t begin = r.trace.events.size();
						const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
						grailBufferedMerge(r, first, middle, last, bufferCapacity, "parallel_grail_buffer", "parallel_grail");
						r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
							"Parallel Grail independent merge block", begin);
						r.addMetric("parallel_grail_tasks");
						r.addMetric("parallel_grail_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					}
				}
				r.endPhase(phase, "Parallel Grail block-merge layer");
				r.addMetric("parallel_grail_layers");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("parallel_grail_buffer", 0);
		}

		void adaptiveGrailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "adaptive_grail_natural_runs", "adaptive_grail_reversed_runs", "adaptive_grail_extensions",
				"adaptive_grail_merges", "adaptive_grail_comparisons", "adaptive_grail_rotation_swaps" }) r.declareMetric(key);
			if (count < 2) return;
			struct Run { std::size_t first, last; Run(std::size_t a, std::size_t b) : first(a), last(b) {} };
			std::vector<Run> runs;
			const std::size_t minimumRun = 32;
			for (std::size_t first = 0; first < count;)
			{
				std::size_t last = first + 1;
				if (last < count)
				{
					SortItem a = r.at(last - 1), b = r.at(last);
					r.compare(a, b, last - 1, last, "main", "main");
					r.addMetric("adaptive_grail_comparisons");
					const bool descending = compareItems(a, b, r.trace.direction) == ComparisonResult::Greater;
					++last;
					while (last < count)
					{
						a = r.at(last - 1); b = r.at(last);
						r.compare(a, b, last - 1, last, "main", "main");
						r.addMetric("adaptive_grail_comparisons");
						const ComparisonResult order = compareItems(a, b, r.trace.direction);
						if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
						++last;
					}
					if (descending)
					{
						for (std::size_t left = first, right = last; left < right && left < --right; ++left)
						{
							r.swap(left, right, SemanticRole::WriteTarget);
							r.addMetric("adaptive_grail_rotation_swaps");
						}
						r.addMetric("adaptive_grail_reversed_runs");
					}
				}
				const std::size_t targetEnd = (std::min)(count, (std::max)(last, first + minimumRun));
				if (targetEnd > last)
				{
					strategies::binaryInsertionRange(r, first, targetEnd);
					last = targetEnd;
					r.addMetric("adaptive_grail_extensions");
				}
				runs.push_back(Run(first, last));
				r.addMetric("adaptive_grail_natural_runs");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Adaptive Grail natural run", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
				first = last;
				while (runs.size() >= 2 && runs[runs.size() - 2].last == runs.back().first &&
					(runs[runs.size() - 2].last - runs[runs.size() - 2].first <= runs.back().last - runs.back().first || first == count))
				{
					Run right = runs.back(); runs.pop_back();
					Run left = runs.back(); runs.pop_back();
					const int phase = r.beginPhase("Adaptive Grail run merge");
					grailMergeWithoutBuffer(r, left.first, left.last - left.first, right.last - right.first, "adaptive_grail");
					r.addMetric("adaptive_grail_merges");
					runs.push_back(Run(left.first, right.last));
					r.endPhase(phase, "Adaptive Grail run merge");
				}
			}
		while (runs.size() > 1)
		{
			Run right = runs.back(); runs.pop_back();
			Run left = runs.back(); runs.pop_back();
			const int phase = r.beginPhase("Adaptive Grail final merge");
			grailMergeWithoutBuffer(r, left.first, left.last - left.first, right.last - right.first, "adaptive_grail");
			r.addMetric("adaptive_grail_merges");
			runs.push_back(Run(left.first, right.last));
			r.endPhase(phase, "Adaptive Grail final merge");
		}
		}

		static void sqrtMergeSort(Recorder& r, bool synchronous)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "sqrt_run_size", "sqrt_runs", "sqrt_merges", "sqrt_merge_passes",
				"sqrt_buffered_merges", "sqrt_rotation_merges", "sqrt_buffer_writes", "sqrt_comparisons", "sqrt_synchronous_layers" })
				r.declareMetric(key);
			if (count < 2) return;
			const std::size_t blockLength = (std::max)(std::size_t(16), static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
			r.setMetric("sqrt_run_size", static_cast<std::uint64_t>(blockLength));
			r.resizeAuxiliary("sqrt_buffer", blockLength);
			for (std::size_t first = 0; first < count; first += blockLength)
			{
				const std::size_t last = (std::min)(count, first + blockLength);
				strategies::binaryInsertionRange(r, first, last);
				r.addMetric("sqrt_runs");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Square-root insertion block",
					r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
			}
		for (std::size_t width = blockLength; width < count;)
		{
			const int phase = synchronous ? r.beginPhase("Synchronous square-root merge layer width " + std::to_string(width)) : -1;
			for (std::size_t first = 0; first < count; first += width * 2)
			{
				const std::size_t middle = (std::min)(count, first + width);
				const std::size_t last = (std::min)(count, middle + width);
				if (middle < last)
				{
					const std::size_t begin = r.trace.events.size();
					grailBufferedMerge(r, first, middle, last, blockLength, "sqrt_buffer", "sqrt");
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Square-root buffered merge", begin);
					r.addMetric("sqrt_merges");
				}
			}
			r.addMetric("sqrt_merge_passes");
			if (synchronous)
			{
				r.addMetric("sqrt_synchronous_layers");
				r.endPhase(phase, "Synchronous square-root merge layer");
			}
			if (width > count / 2) break;
			width *= 2;
		}
		r.resizeAuxiliary("sqrt_buffer", 0);
		}



		void sqrtMergeSortStrategy(Recorder& r) { sqrtMergeSort(r, false); }
		void synchronousSqrtMergeSortStrategy(Recorder& r) { sqrtMergeSort(r, true); }

		void circularGrailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "circular_grail_run_size", "circular_grail_runs", "circular_grail_merges",
				"circular_grail_passes", "circular_grail_comparisons", "circular_grail_buffer_writes",
				"circular_grail_wraps" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t runSize = (std::max)(std::size_t(8), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			r.setMetric("circular_grail_run_size", static_cast<std::uint64_t>(runSize));
			r.resizeAuxiliary("circular_grail_buffer", count);
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("circular_grail_runs");
			}
		std::size_t ringHead = count / 3;
		for (std::size_t width = runSize; width < count;)
		{
			const int phase = r.beginPhase("Circular Grail ring-merge layer width " + std::to_string(width));
			for (std::size_t first = 0; first < count; first += width * 2)
			{
				const std::size_t middle = (std::min)(count, first + width);
				const std::size_t last = (std::min)(count, middle + width);
				if (middle < last)
				{
					const std::size_t begin = r.trace.events.size();
					const auto slot = [count, ringHead](std::size_t offset)
					{
						return (ringHead + offset) % count;
					};
					for (std::size_t offset = 0; offset < last - first; ++offset)
					{
						if (ringHead + offset >= count) r.addMetric("circular_grail_wraps");
						r.writeAuxiliary("circular_grail_buffer", slot(offset), r.at(first + offset));
						r.addMetric("circular_grail_buffer_writes");
					}
					std::size_t left = 0, right = middle - first, output = 0;
					while (left < middle - first && right < last - first)
					{
						const std::size_t leftSlot = slot(left), rightSlot = slot(right);
						const SortItem a = r.auxiliaryAt("circular_grail_buffer", leftSlot);
						const SortItem b = r.auxiliaryAt("circular_grail_buffer", rightSlot);
						r.compare(a, b, leftSlot, rightSlot, "circular_grail_buffer", "circular_grail_buffer",
							SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						r.addMetric("circular_grail_comparisons");
						const std::size_t chosen = compareItems(a, b, r.trace.direction) != ComparisonResult::Greater ? left++ : right++;
						r.writeMain(first + output++, r.auxiliaryAt("circular_grail_buffer", slot(chosen)), SemanticRole::WriteTarget);
					}
					while (left < middle - first) r.writeMain(first + output++, r.auxiliaryAt("circular_grail_buffer", slot(left++)), SemanticRole::WriteTarget);
					while (right < last - first) r.writeMain(first + output++, r.auxiliaryAt("circular_grail_buffer", slot(right++)), SemanticRole::WriteTarget);
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Circular buffer merge range", begin);
					r.addMetric("circular_grail_merges");
					ringHead = (ringHead + width + (last - first) / 2 + 1) % count;
				}
			}
			r.addMetric("circular_grail_passes");
			r.endPhase(phase, "Circular Grail ring-merge layer");
			if (width > count / 2) break;
			width *= 2;
		}
		r.resizeAuxiliary("circular_grail_buffer", 0);
		}

		void unstableGrailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "unstable_grail_runs", "unstable_grail_merges", "unstable_grail_passes",
				"unstable_grail_comparisons", "unstable_grail_buffer_writes", "unstable_grail_equal_right_choices" }) r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("unstable_grail_buffer", count);
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < count; first += runSize)
			{
				strategies::binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("unstable_grail_runs");
			}
		for (std::size_t width = runSize; width < count;)
		{
			const int phase = r.beginPhase("Unstable Grail block merge layer width " + std::to_string(width));
			for (std::size_t first = 0; first < count; first += width * 2)
			{
				const std::size_t middle = (std::min)(count, first + width);
				const std::size_t last = (std::min)(count, middle + width);
				if (middle < last)
				{
					for (std::size_t i = first; i < last; ++i)
					{
						r.writeAuxiliary("unstable_grail_buffer", i, r.at(i));
						r.addMetric("unstable_grail_buffer_writes");
					}
					std::size_t left = first, right = middle;
					const std::size_t begin = r.trace.events.size();
					for (std::size_t output = first; output < last; ++output)
					{
						if (left == middle) r.writeMain(output, r.auxiliaryAt("unstable_grail_buffer", right++));
						else if (right == last) r.writeMain(output, r.auxiliaryAt("unstable_grail_buffer", left++));
						else
						{
							const SortItem a = r.auxiliaryAt("unstable_grail_buffer", left);
							const SortItem b = r.auxiliaryAt("unstable_grail_buffer", right);
							r.compare(a, b, left, right, "unstable_grail_buffer", "unstable_grail_buffer");
							r.addMetric("unstable_grail_comparisons");
							const ComparisonResult order = compareItems(a, b, r.trace.direction);
							if (order == ComparisonResult::Less)
								r.writeMain(output, r.auxiliaryAt("unstable_grail_buffer", left++));
							else
							{
								if (order == ComparisonResult::Equal) r.addMetric("unstable_grail_equal_right_choices");
								r.writeMain(output, r.auxiliaryAt("unstable_grail_buffer", right++));
							}
						}
					}
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Unstable right-biased block merge", begin);
					r.addMetric("unstable_grail_merges");
				}
			}
			r.addMetric("unstable_grail_passes");
			r.endPhase(phase, "Unstable Grail block merge layer");
			if (width > count / 2) break;
			width *= 2;
		}
		r.resizeAuxiliary("unstable_grail_buffer", 0);
		}
	}
}
