#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		void stooge(Recorder& r)
		{
			std::function<void(std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t first, std::size_t last)
			{
				const SortItem a = r.at(first), b = r.at(last);
				r.compare(a, b, first, last);
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(first, last);
				if (last - first + 1 > 2)
				{
					const std::size_t third = (last - first + 1) / 3;
					sortRange(first, last - third);
					sortRange(first + third, last);
					sortRange(first, last - third);
				}
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size() - 1);
		}

		void bufferedStoogeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("buffered_stooge_calls");
			r.declareMetric("buffered_stooge_comparisons");
			r.declareMetric("buffered_stooge_buffer_swaps");
			r.declareMetric("buffered_stooge_cleanup_swaps");
			if (count < 2) return;

			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem left = r.at(first), right = r.at(second);
				r.compare(left, right, first, second,
					"main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric("buffered_stooge_comparisons");
				return compareItems(left, right, r.trace.direction);
			};
			const auto exchange = [&r](std::size_t first, std::size_t second, bool cleanup)
			{
				if (first == second) return;
				r.swap(first, second, SemanticRole::WriteTarget);
				r.addMetric(cleanup ? "buffered_stooge_cleanup_swaps" : "buffered_stooge_buffer_swaps");
			};

			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &compareAt, &exchange, &sortRange, count](std::size_t first, std::size_t last,
				std::size_t depth)
			{
				r.chargeWork();
				r.addMetric("buffered_stooge_calls");
				if (last - first < 2) return;
				if (depth > count * 3 + 3) throw std::logic_error("Buffered Stooge recursion exceeded its bounded depth.");
				if (last - first == 2)
				{
					if (compareAt(first, first + 1) == ComparisonResult::Greater) exchange(first, first + 1, false);
					return;
				}

				const std::size_t length = last - first;
				const std::size_t third = first + (length + 2) / 3;
				std::size_t twoThirds = first + (2 * length + 2) / 3;
				if (twoThirds - third < third - first) --twoThirds;
				if ((length - 2) % 3 == 0) --twoThirds;
				if (twoThirds < third || twoThirds >= last)
					throw std::logic_error("Buffered Stooge produced an invalid third split.");

				const int phase = r.beginPhase("Buffered Stooge range " + std::to_string(first) + "–" + std::to_string(last));
				sortRange(third, twoThirds, depth + 1);
				sortRange(twoThirds, last, depth + 1);

				std::size_t left = third, right = twoThirds, buffer = first;
				while (left < twoThirds && right < last)
				{
					r.chargeWork();
					if (compareAt(left, right) == ComparisonResult::Greater)
						exchange(buffer++, right++, false);
					else
						exchange(buffer++, left++, false);
				}
				while (right < last)
				{
					r.chargeWork();
					exchange(buffer++, right++, false);
				}
				r.annotate("main", RegionKind::Contiguous, first, buffer, 1, phase,
					"Buffered third merge", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);

				sortRange(twoThirds, last, depth + 1);
				std::size_t tailLeft = twoThirds - 1, tailRight = last - 1;
				while (tailRight > tailLeft && tailLeft >= first)
				{
					r.chargeWork();
					if (compareAt(tailLeft, tailRight) == ComparisonResult::Greater)
					{
						for (std::size_t index = tailLeft; index < tailRight; ++index)
						{
							r.chargeWork();
							exchange(index, index + 1, true);
						}
						if (tailLeft == first) break;
						--tailLeft;
					}
					--tailRight;
				}
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Buffered Stooge cleanup scan", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
				r.endPhase(phase, "Buffered Stooge range " + std::to_string(first) + "–" + std::to_string(last));
			};
			sortRange(0, count, 0);
		}

		static bool stoogeCompareSwap(Recorder& r, std::size_t first, std::size_t second)
		{
			if (first >= r.state.slots.size() || second >= r.state.slots.size() || first == second) return false;
			const SortItem a = r.at(first), b = r.at(second);
			r.compare(a, b, first, second);
			if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater) return false;
			r.swap(first, second);
			return true;
		}

		void optimizedStooge(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const auto compareSwapAscending = [&r](std::size_t first, std::size_t second)
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				if (compareItems(a, b, SortDirection::Ascending) == ComparisonResult::Greater) r.swap(first, second);
			};
			compareSwapAscending(0, n - 1);
			std::size_t left = 0, right = n - 2;
			while (left < right)
			{
				std::size_t index = right;
				while (left < index)
				{
					compareSwapAscending(left, index);
					++left;
					--index;
				}
				left = 0;
				--right;
			}
			left = 1;
			right = n - 1;
			const std::size_t finalRight = right;
			while (left < right)
			{
				std::size_t index = left;
				while (index < right)
				{
					compareSwapAscending(index, right);
					++index;
					--right;
				}
				++left;
				right = finalRight;
			}
			if (r.trace.direction == SortDirection::Descending)
				for (std::size_t first = 0, last = n - 1; first < last; ++first, --last) r.swap(first, last);
		}

		void optimizedStoogeStudio(Recorder& r)
		{
			std::function<bool(std::size_t, std::size_t, std::size_t, bool)> sortRange;
			sortRange = [&r, &sortRange](std::size_t first, std::size_t middle, std::size_t end, bool merge) -> bool
			{
				if (first >= middle || end > r.state.slots.size()) return false;
				if (end - first == 2) return stoogeCompareSwap(r, first, middle);
				const std::size_t firstThird = (first + first + end) / 3;
				const std::size_t secondThird = (first + end + end + 2) / 3;
				bool leftChanged = false, rightChanged = false;
				if (middle < secondThird)
				{
					leftChanged = sortRange(first, middle, secondThird, merge);
					if (merge)
					{
						rightChanged = sortRange(std::max(first + secondThird - middle, firstThird), secondThird, end, true);
						if (rightChanged) sortRange(first + secondThird - middle, firstThird, 2 * firstThird - first, true);
					}
					else
					{
						rightChanged = sortRange(firstThird, secondThird, end, false);
						if (rightChanged) sortRange(first, firstThird, 2 * firstThird - first, true);
					}
				}
				else
				{
					rightChanged = sortRange(firstThird, middle, end, merge);
					if (rightChanged) sortRange(first, firstThird, firstThird + end - middle, true);
				}
				return leftChanged || rightChanged;
			};
			if (r.state.slots.size() > 1) sortRange(0, 1, r.state.slots.size(), false);
		}

		static void quadStooge(Recorder& r, std::size_t first, std::size_t length)
		{
			if (length >= 2) stoogeCompareSwap(r, first, first + length - 1);
			if (length <= 2) return;
			const std::size_t firstLength = length / 2;
			const std::size_t secondLength = (length + 1) / 2;
			const std::size_t middleLength = (firstLength + 1) / 2 + (secondLength + 1) / 2;
			quadStooge(r, first, firstLength);
			quadStooge(r, first + firstLength, secondLength);
			quadStooge(r, first + firstLength / 2, middleLength);
			quadStooge(r, first + firstLength, secondLength);
			quadStooge(r, first, firstLength);
			if (length > 3) quadStooge(r, first + firstLength / 2, middleLength);
		}

		void quadStooge(Recorder& r)
		{
			quadStooge(r, 0, r.state.slots.size());
		}

		static void slowSort(Recorder& r, std::size_t first, std::size_t last)
		{
			if (first >= last) return;
			const std::size_t middle = first + (last - first) / 2;
			slowSort(r, first, middle);
			slowSort(r, middle + 1, last);
			stoogeCompareSwap(r, middle, last);
			slowSort(r, first, last - 1);
		}

		void slowSort(Recorder& r)
		{
			if (r.state.slots.size() > 1) slowSort(r, 0, r.state.slots.size() - 1);
		}

		static void sillySort(Recorder& r, std::size_t first, std::size_t last)
		{
			if (first >= last) return;
			const std::size_t middle = first + (last - first) / 2;
			sillySort(r, first, middle);
			sillySort(r, middle + 1, last);
			const SortItem left = r.at(first), right = r.at(middle + 1);
			r.compare(left, right, first, middle + 1);
			if (compareItems(left, right, r.trace.direction) != ComparisonResult::Less) r.swap(first, middle + 1);
			sillySort(r, first + 1, last);
		}

		void sillySort(Recorder& r)
		{
			if (r.state.slots.size() > 1) sillySort(r, 0, r.state.slots.size() - 1);
		}

		static void snuffleSort(Recorder& r, std::size_t first, std::size_t last)
		{
			if (last < first || last - first + 1 < 2) return;
			const SortItem left = r.at(first), right = r.at(last);
			r.compare(left, right, first, last);
			if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) r.swap(first, last);
			const std::size_t length = last - first + 1;
			if (length >= 3)
			{
				const std::size_t middle = first + (last - first) / 2;
				for (std::size_t i = 0; i < length / 2; ++i)
				{
					snuffleSort(r, first, middle);
					snuffleSort(r, middle, last);
				}
			}
		}

		void snuffleSort(Recorder& r)
		{
			if (r.state.slots.size() > 1) snuffleSort(r, 0, r.state.slots.size() - 1);
		}

		static bool circloidCircle(Recorder& r, std::size_t first, std::size_t last)
		{
			std::size_t left = first, right = last;
			bool swapped = false;
			while (left < right)
			{
				const SortItem a = r.at(left), b = r.at(right);
				r.compare(a, b, left, right);
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(left, right);
					swapped = true;
				}
				++left;
				--right;
				if (left == right) ++right;
			}
			return swapped;
		}

		static bool circloidPass(Recorder& r, std::size_t first, std::size_t last)
		{
			if (first >= last) return false;
			const std::size_t middle = first + (last - first) / 2;
			const bool leftChanged = circloidPass(r, first, middle);
			const bool rightChanged = circloidPass(r, middle + 1, last);
			const bool centerChanged = circloidCircle(r, first, last);
			return centerChanged || leftChanged || rightChanged;
		}

		void circloidSort(Recorder& r)
		{
			if (r.state.slots.size() < 2) return;
			while (circloidPass(r, 0, r.state.slots.size() - 1)) {}
		}

		static void completeGraphCompareSwap(Recorder& r, std::size_t first, std::size_t second)
		{
			if (first >= r.state.slots.size() || second >= r.state.slots.size() || first == second) return;
			const SortItem left = r.at(first), right = r.at(second);
			r.compare(left, right, first, second);
			r.addMetric("network_comparators");
			if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) r.swap(first, second);
		}

		static void completeGraphSplit(Recorder& r, std::size_t first, std::size_t middle, std::size_t end)
		{
			if (end <= first || end - first < 2) return;
			std::size_t left = first, right = end;
			const std::size_t pairs = (right - left) / 2;
			const bool odd = ((right - left) & 1u) != 0;
			std::size_t center = 0;
			if (odd)
			{
				if (middle - left > right - middle) center = left++;
				else center = --right;
			}

			for (std::size_t offset = 0; offset < pairs; ++offset)
			{
				std::size_t a = left;
				for (std::size_t j = offset; j < pairs; ++j)
				{
					r.chargeWork();
					completeGraphCompareSwap(r, a++, middle + j);
				}
				for (std::size_t j = 0; j < offset; ++j)
				{
					r.chargeWork();
					completeGraphCompareSwap(r, a++, middle + j);
				}
			}
			if (odd)
			{
				if (center < middle)
					for (std::size_t j = 0; j < pairs; ++j) completeGraphCompareSwap(r, center, middle + j);
				else
					for (std::size_t j = 0; j < pairs; ++j) completeGraphCompareSwap(r, left + j, center);
			}
		}

		void completeGraphSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::size_t end = 2;
			while (end <= count - 1) end <<= 1;
			for (std::size_t distance = 2; distance <= end; distance <<= 1)
			{
				const int group = r.beginPhase("Complete Graph network layer d=" + std::to_string(distance));
				std::size_t first = 0, remainder = 0;
				while (first < count)
				{
					r.chargeWork();
					std::size_t middle = first;
					remainder += count;
					while (remainder >= distance)
					{
						remainder -= distance;
						++middle;
					}
					std::size_t blockEnd = middle;
					remainder += count;
					while (remainder >= distance)
					{
						remainder -= distance;
						++blockEnd;
					}
					if (blockEnd <= first) blockEnd = first + 1;
					completeGraphSplit(r, first, middle, blockEnd);
					first = blockEnd;
				}
				r.addMetric("network_layers");
				r.endPhase(group, "Complete Graph network layer");
				if (distance > end / 2) break;
			}
		}

		void funSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			for (std::size_t index = 1; index < count; ++index)
			{
				bool done = false;
				std::size_t attempts = 0;
				while (!done)
				{
					if (++attempts > count * 2 + 4) break;
					done = true;
					const SortItem key = r.at(index);
					std::size_t low = 0, high = count - 1;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t middle = low + (high - low) / 2;
						const SortItem probe = r.at(middle);
						r.compare(probe, key, middle, index, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						if (compareItems(probe, key, r.trace.direction) == ComparisonResult::Less) low = middle + 1;
						else high = middle;
					}

					const SortItem atPosition = r.at(low);
					r.compare(atPosition, key, low, index, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
					if (compareItems(atPosition, key, r.trace.direction) != ComparisonResult::Equal)
					{
						if (index + 1 < low) r.swap(index, low - 1);
						else if (index > low) r.swap(index, low);
						done = false;
					}
				}
			}
		}
	}
}