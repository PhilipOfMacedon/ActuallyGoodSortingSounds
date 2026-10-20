#include "PartitionTraceUtilities.h"
#include "TraceRecorder.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sortinglogic
{
	namespace detail
	{
		std::pair<SortItem, std::size_t> sampledMedian(Recorder& r,
			const std::vector<std::size_t>& positions, const char* metricPrefix)
		{
			std::vector<std::pair<SortItem, std::size_t> > samples;
			for (std::size_t i = 0; i < positions.size(); ++i)
				samples.push_back(std::make_pair(r.at(positions[i]), positions[i]));
			for (std::size_t i = 1; i < samples.size(); ++i)
			{
				const std::pair<SortItem, std::size_t> value = samples[i];
				std::size_t j = i;
				while (j > 0)
				{
					r.chargeWork();
					r.compare(value.first, samples[j - 1].first, value.second, samples[j - 1].second,
						"main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					r.addMetric(std::string(metricPrefix) + "_pivot_comparisons");
					if (compareItems(value.first, samples[j - 1].first, r.trace.direction) != ComparisonResult::Less) break;
					samples[j] = samples[j - 1];
					--j;
				}
				samples[j] = value;
			}
			return samples[samples.size() / 2];
		}

		std::size_t traceMedianOfThree(Recorder& r, std::size_t first, std::size_t middle, std::size_t last)
		{
			SortItem a = r.at(first), b = r.at(middle), c = r.at(last);
			r.compare(c, a, last, first);
			if (compareItems(c, a, r.trace.direction) == ComparisonResult::Less) r.swap(first, last);
			a = r.at(first); b = r.at(middle); c = r.at(last);
			r.compare(b, a, middle, first);
			if (compareItems(b, a, r.trace.direction) == ComparisonResult::Less) r.swap(middle, first);
			a = r.at(first); b = r.at(middle); c = r.at(last);
			r.compare(c, b, last, middle);
			if (compareItems(c, b, r.trace.direction) == ComparisonResult::Less) r.swap(last, middle);
			return middle;
		}

		std::size_t traceHoarePartition(Recorder& r, std::size_t first, std::size_t end, std::size_t pivotIndex)
		{
			const SortItem pivot = r.at(pivotIndex);
			std::size_t left = first;
			std::ptrdiff_t right = static_cast<std::ptrdiff_t>(end) - 1;
			for (;;)
			{
				while (left < end)
				{
					const SortItem item = r.at(left);
					r.compare(item, pivot, left, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					if (compareItems(item, pivot, r.trace.direction) != ComparisonResult::Less) break;
					++left;
				}
				while (right >= static_cast<std::ptrdiff_t>(first))
				{
					const std::size_t index = static_cast<std::size_t>(right);
					const SortItem item = r.at(index);
					r.compare(pivot, item, pivotIndex, index, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					if (compareItems(pivot, item, r.trace.direction) != ComparisonResult::Less) break;
					--right;
				}
				if (static_cast<std::ptrdiff_t>(left) >= right) return left;
				const std::size_t rightIndex = static_cast<std::size_t>(right);
				r.swap(left, rightIndex);
				if (pivotIndex == left) pivotIndex = rightIndex;
				else if (pivotIndex == rightIndex) pivotIndex = left;
				++left;
				--right;
			}
		}

		static void siftDown(Recorder& r, std::size_t first, std::size_t root, std::size_t heapSize)
		{
			for (;;)
			{
				const std::size_t leftChild = root * 2 + 1;
				if (leftChild >= heapSize) return;
				std::size_t greatest = leftChild;
				const std::size_t rightChild = leftChild + 1;
				if (rightChild < heapSize)
				{
					const SortItem leftItem = r.at(first + leftChild), rightItem = r.at(first + rightChild);
					r.compare(leftItem, rightItem, first + leftChild, first + rightChild);
					if (compareItems(leftItem, rightItem, r.trace.direction) == ComparisonResult::Less) greatest = rightChild;
				}
				const SortItem parent = r.at(first + root), child = r.at(first + greatest);
				r.compare(parent, child, first + root, first + greatest);
				if (compareItems(parent, child, r.trace.direction) != ComparisonResult::Less) return;
				r.swap(first + root, first + greatest);
				root = greatest;
			}
		}

		void traceHeapSortRange(Recorder& r, std::size_t first, std::size_t end)
		{
			const std::size_t length = end - first;
			for (std::size_t start = length / 2; start > 0; --start)
				siftDown(r, first, start - 1, length);
			for (std::size_t remaining = length; remaining > 1; --remaining)
			{
				r.swap(first, first + remaining - 1);
				siftDown(r, first, 0, remaining - 1);
			}
		}
	}
}
