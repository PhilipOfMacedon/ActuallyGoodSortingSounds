#include "GrailTraceUtilities.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace sortinglogic
{
	namespace detail
	{
		void grailRotate(Recorder& r, std::size_t position, std::size_t leftLength, std::size_t rightLength,
			const char* metricPrefix)
		{
			while (leftLength != 0 && rightLength != 0)
			{
				if (leftLength <= rightLength)
				{
					for (std::size_t offset = 0; offset < leftLength; ++offset)
					{
						r.swap(position + offset, position + leftLength + offset, SemanticRole::WriteTarget);
						r.addMetric(std::string(metricPrefix) + "_rotation_swaps");
					}
					position += leftLength;
					rightLength -= leftLength;
				}
				else
				{
					const std::size_t offset = leftLength - rightLength;
					for (std::size_t index = 0; index < rightLength; ++index)
					{
						r.swap(position + offset + index, position + leftLength + index, SemanticRole::WriteTarget);
						r.addMetric(std::string(metricPrefix) + "_rotation_swaps");
					}
					leftLength -= rightLength;
				}
			}
		}

		std::size_t grailBinarySearch(Recorder& r, std::size_t position, std::size_t length, std::size_t keyIndex,
			bool lowerBound, const char* metricPrefix)
		{
			std::ptrdiff_t left = -1;
			std::ptrdiff_t right = static_cast<std::ptrdiff_t>(length);
			const SortItem key = r.at(keyIndex);
			while (left < right - 1)
			{
				r.chargeWork();
				const std::ptrdiff_t middle = left + (right - left) / 2;
				const SortItem item = r.at(position + static_cast<std::size_t>(middle));
				r.compare(item, key, position + static_cast<std::size_t>(middle), keyIndex, "main", "main",
					SemanticRole::ProbedItem, SemanticRole::ComparedItem);
				r.addMetric(std::string(metricPrefix) + "_merge_comparisons");
				const ComparisonResult order = compareItems(item, key, r.trace.direction);
				if (lowerBound ? order == ComparisonResult::Less : order != ComparisonResult::Greater)
					left = middle;
				else right = middle;
			}
			return static_cast<std::size_t>(right);
		}

		void grailMergeWithoutBuffer(Recorder& r, std::size_t position, std::size_t leftLength, std::size_t rightLength,
			const char* metricPrefix)
		{
			if (leftLength == 0 || rightLength == 0) return;
			const std::size_t mergeStart = position;
			const std::size_t mergeLength = leftLength + rightLength;
			const std::size_t begin = r.trace.events.size();
			if (leftLength < rightLength)
			{
				while (leftLength != 0)
				{
					r.chargeWork();
					const std::size_t insertion = grailBinarySearch(r, position + leftLength, rightLength, position, true, metricPrefix);
					if (insertion != 0)
					{
						grailRotate(r, position, leftLength, insertion, metricPrefix);
						position += insertion;
						rightLength -= insertion;
					}
					if (rightLength == 0) break;
					do
					{
						++position;
						--leftLength;
						if (leftLength == 0) break;
						const SortItem left = r.at(position), right = r.at(position + leftLength);
						r.compare(left, right, position, position + leftLength, "main", "main");
						r.addMetric(std::string(metricPrefix) + "_merge_comparisons");
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) break;
					} while (true);
				}
			}
			else
			{
				while (rightLength != 0)
				{
					r.chargeWork();
					const std::size_t keyIndex = position + leftLength + rightLength - 1;
					const std::size_t insertion = grailBinarySearch(r, position, leftLength, keyIndex, false, metricPrefix);
					if (insertion != leftLength)
					{
						grailRotate(r, position + insertion, leftLength - insertion, rightLength, metricPrefix);
						leftLength = insertion;
					}
					if (leftLength == 0) break;
					do
					{
						--rightLength;
						if (rightLength == 0) break;
						const std::size_t leftIndex = position + leftLength - 1;
						const std::size_t rightIndex = position + leftLength + rightLength - 1;
						const SortItem left = r.at(leftIndex), right = r.at(rightIndex);
						r.compare(left, right, leftIndex, rightIndex, "main", "main");
						r.addMetric(std::string(metricPrefix) + "_merge_comparisons");
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) break;
					} while (true);
				}
			}
			r.annotate("main", RegionKind::Contiguous, mergeStart, mergeStart + mergeLength, 1, -1, "Stable no-buffer merge", begin);
		}

		void grailBufferedMerge(Recorder& r, std::size_t first, std::size_t middle, std::size_t last,
			std::size_t bufferCapacity, const char* lane, const char* metricPrefix)
		{
			if (first >= middle || middle >= last) return;
			const SortItem boundaryLeft = r.at(middle - 1), boundaryRight = r.at(middle);
			r.compare(boundaryLeft, boundaryRight, middle - 1, middle, "main", "main");
			r.addMetric(std::string(metricPrefix) + "_comparisons");
			if (compareItems(boundaryLeft, boundaryRight, r.trace.direction) != ComparisonResult::Greater) return;
			const std::size_t leftCount = middle - first, rightCount = last - middle;
			if (bufferCapacity == 0 || (std::min)(leftCount, rightCount) > bufferCapacity)
			{
				grailMergeWithoutBuffer(r, first, leftCount, rightCount, metricPrefix);
				r.addMetric(std::string(metricPrefix) + "_rotation_merges");
				return;
			}
			if (leftCount <= rightCount)
			{
				for (std::size_t i = 0; i < leftCount; ++i)
				{
					r.writeAuxiliary(lane, i, r.at(first + i));
					r.addMetric(std::string(metricPrefix) + "_buffer_writes");
				}
				std::size_t left = 0, right = middle, output = first;
				while (left < leftCount && right < last)
				{
					const SortItem a = r.auxiliaryAt(lane, left), b = r.at(right);
					r.compare(a, b, left, right, lane, "main", SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
					r.addMetric(std::string(metricPrefix) + "_comparisons");
					if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
						r.writeMain(output++, r.auxiliaryAt(lane, left++));
					else r.writeMain(output++, r.at(right++));
				}
				while (left < leftCount) r.writeMain(output++, r.auxiliaryAt(lane, left++));
			}
			else
			{
				for (std::size_t i = 0; i < rightCount; ++i)
				{
					r.writeAuxiliary(lane, i, r.at(middle + i));
					r.addMetric(std::string(metricPrefix) + "_buffer_writes");
				}
				std::size_t left = middle, right = rightCount, output = last;
				while (left > first && right > 0)
				{
					const SortItem a = r.at(left - 1), b = r.auxiliaryAt(lane, right - 1);
					r.compare(a, b, left - 1, right - 1, "main", lane, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
					r.addMetric(std::string(metricPrefix) + "_comparisons");
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
						r.writeMain(--output, r.at(--left));
					else r.writeMain(--output, r.auxiliaryAt(lane, --right));
				}
				while (right > 0) r.writeMain(--output, r.auxiliaryAt(lane, --right));
			}
			r.addMetric(std::string(metricPrefix) + "_buffered_merges");
		}
	}
}
