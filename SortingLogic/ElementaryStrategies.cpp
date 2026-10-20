#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void bubble(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t end = n; end > 1; --end)
			{
				bool changed = false;
				for (std::size_t i = 1; i < end; ++i)
				{
					const SortItem a = r.at(i - 1), b = r.at(i);
					r.compare(a, b, i - 1, i);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(i - 1, i);
						changed = true;
					}
				}
				if (!changed) break;
			}
		}

		void optimizedBubble(Recorder& r)
		{
			// ArrayV's Optimized Bubble variant skips the suffix already proven in
			// order on each pass. Its boundary is derived from the final run of
			// ordered comparisons, rather than simply shrinking by one.
			std::ptrdiff_t end = static_cast<std::ptrdiff_t>(r.state.slots.size()) - 1;
			while (end > 0)
			{
				std::size_t consecutiveSorted = 1;
				for (std::ptrdiff_t j = 0; j < end; ++j)
				{
					const SortItem a = r.at(static_cast<std::size_t>(j));
					const SortItem b = r.at(static_cast<std::size_t>(j + 1));
					r.compare(a, b, static_cast<std::size_t>(j), static_cast<std::size_t>(j + 1));
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(static_cast<std::size_t>(j), static_cast<std::size_t>(j + 1));
						consecutiveSorted = 1;
					}
					else ++consecutiveSorted;
				}
				end -= static_cast<std::ptrdiff_t>(consecutiveSorted);
			}
		}

		void unoptimizedBubble(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			bool sorted = false;
			while (!sorted)
			{
				sorted = true;
				for (std::size_t i = 0; i + 1 < n; ++i)
				{
					const SortItem a = r.at(i), b = r.at(i + 1);
					r.compare(a, b, i, i + 1);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(i, i + 1);
						sorted = false;
					}
				}
			}
		}

		void swaplessBubble(Recorder& r)
		{
			std::size_t end = r.state.slots.size();
			while (end > 1)
			{
				r.hold(0);
				std::size_t gap = 0;
				std::size_t lastInversion = 0;
				for (std::size_t i = 1; i < end; ++i)
				{
					const SortItem carried = r.state.heldItem, current = r.at(i);
					r.compare(carried, current, gap, i, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(carried, current, r.trace.direction) == ComparisonResult::Greater)
					{
						r.shift(i, gap);
						gap = i;
						lastInversion = i;
					}
					else
					{
						r.place(gap, carried);
						r.hold(i);
						gap = i;
					}
				}
				r.place(gap, r.state.heldItem);
				if (lastInversion == 0) break;
				end = lastInversion;
			}
		}

		void selection(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t i = 0; i < n; ++i)
			{
				std::size_t selected = i;
				for (std::size_t j = i + 1; j < n; ++j)
				{
					const SortItem current = r.at(selected), candidate = r.at(j);
					const SemanticRole role = r.trace.direction == SortDirection::Ascending
						? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum;
					r.compare(current, candidate, selected, j, "main", "main", role, SemanticRole::ComparedItem);
					if (compareItems(candidate, current, r.trace.direction) == ComparisonResult::Less) selected = j;
				}
				r.swap(i, selected, r.trace.direction == SortDirection::Ascending ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum);
			}
		}

		void doubleSelection(Recorder& r)
		{
			std::size_t left = 0, right = r.state.slots.empty() ? 0 : r.state.slots.size() - 1;
			while (left < right)
			{
				std::size_t first = left, last = left;
				for (std::size_t i = left; i <= right; ++i)
				{
					const SortItem candidate = r.at(i);
					const SortItem lowItem = r.at(first), highItem = r.at(last);
					r.compare(candidate, lowItem, i, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMinimum);
					if (compareItems(candidate, lowItem, r.trace.direction) == ComparisonResult::Less) first = i;
					r.compare(candidate, highItem, i, last, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(candidate, highItem, r.trace.direction) == ComparisonResult::Greater) last = i;
				}
				if (last == left) last = first;
				r.swap(left, first, SemanticRole::CurrentMinimum);
				r.swap(right, last, SemanticRole::CurrentMaximum);
				++left;
				--right;
			}
		}

		void stableSelection(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			for (std::size_t first = 0; first + 1 < n; ++first)
			{
				std::size_t minimum = first;
				for (std::size_t i = first + 1; i < n; ++i)
				{
					const SortItem a = r.at(minimum), b = r.at(i);
					r.compare(a, b, minimum, i, "main", "main", SemanticRole::CurrentMinimum, SemanticRole::ComparedItem);
					if (compareItems(b, a, r.trace.direction) == ComparisonResult::Less) minimum = i;
				}
				if (minimum == first) continue;
				const SortItem key = r.at(minimum);
				r.hold(minimum);
				for (std::size_t i = minimum; i > first; --i) r.shift(i - 1, i);
				r.place(first, key);
			}
		}

		void bingo(Recorder& r)
		{
			std::size_t end = r.state.slots.size();
			while (end > 1)
			{
				std::size_t maximumIndex = 0;
				for (std::size_t i = 1; i < end; ++i)
				{
					const SortItem a = r.at(i), b = r.at(maximumIndex);
					r.compare(a, b, i, maximumIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) maximumIndex = i;
				}
				const SortItem maximum = r.at(maximumIndex);
				std::size_t i = 0;
				while (i < end)
				{
					const SortItem candidate = r.at(i);
					r.compare(candidate, maximum, i, maximumIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(candidate, maximum, r.trace.direction) == ComparisonResult::Equal)
					{
						r.swap(i, end - 1, SemanticRole::CurrentMaximum);
						--end;
					}
					else ++i;
				}
			}
		}

		void gnome(Recorder& r)
		{
			std::size_t i = 1;
			while (i < r.state.slots.size())
			{
				const SortItem a = r.at(i - 1), b = r.at(i);
				r.compare(a, b, i - 1, i);
				if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater) ++i;
				else { r.swap(i - 1, i); if (i > 1) --i; }
			}
		}

		void cocktail(Recorder& r)
		{
			if (r.state.slots.size() < 2) return;
			std::size_t first = 0, last = r.state.slots.size() - 1;
			bool changed = true;
			while (changed && first < last)
			{
				changed = false;
				for (std::size_t i = first; i < last; ++i)
				{
					const SortItem a = r.at(i), b = r.at(i + 1);
					r.compare(a, b, i, i + 1);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { r.swap(i, i + 1); changed = true; }
				}
				if (!changed) break;
				--last;
				changed = false;
				for (std::size_t i = last; i > first; --i)
				{
					const SortItem a = r.at(i - 1), b = r.at(i);
					r.compare(a, b, i - 1, i);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { r.swap(i - 1, i); changed = true; }
				}
				++first;
			}
		}

		void optimizedCocktail(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::ptrdiff_t start = 0, end = static_cast<std::ptrdiff_t>(n) - 1;
			while (start < end)
			{
				std::size_t consecutiveSorted = 1;
				for (std::ptrdiff_t i = start; i < end; ++i)
				{
					const SortItem a = r.at(static_cast<std::size_t>(i)), b = r.at(static_cast<std::size_t>(i + 1));
					r.compare(a, b, static_cast<std::size_t>(i), static_cast<std::size_t>(i + 1));
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
						{ r.swap(static_cast<std::size_t>(i), static_cast<std::size_t>(i + 1)); consecutiveSorted = 1; }
					else ++consecutiveSorted;
				}
				end -= static_cast<std::ptrdiff_t>(std::min<std::size_t>(consecutiveSorted, static_cast<std::size_t>(end - start + 1)));
				if (start >= end) break;
				consecutiveSorted = 1;
				for (std::ptrdiff_t i = end; i > start; --i)
				{
					const SortItem a = r.at(static_cast<std::size_t>(i - 1)), b = r.at(static_cast<std::size_t>(i));
					r.compare(a, b, static_cast<std::size_t>(i - 1), static_cast<std::size_t>(i));
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
						{ r.swap(static_cast<std::size_t>(i - 1), static_cast<std::size_t>(i)); consecutiveSorted = 1; }
					else ++consecutiveSorted;
				}
				start += static_cast<std::ptrdiff_t>(std::min<std::size_t>(consecutiveSorted, static_cast<std::size_t>(end - start + 1)));
			}
		}

		void unoptimizedCocktail(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t pass = 0; pass < n / 2; ++pass)
			{
				const std::size_t forwardEnd = n - pass - 1;
				for (std::size_t i = pass; i < forwardEnd; ++i)
				{
					const SortItem a = r.at(i), b = r.at(i + 1);
					r.compare(a, b, i, i + 1);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(i, i + 1);
				}
				for (std::size_t i = forwardEnd; i > pass; --i)
				{
					const SortItem a = r.at(i - 1), b = r.at(i);
					r.compare(a, b, i - 1, i);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(i - 1, i);
				}
			}
		}

		void comb(Recorder& r)
		{
			std::size_t gap = r.state.slots.size();
			bool changed = true;
			while (gap > 1 || changed)
			{
				gap = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(gap) / 1.3));
				changed = false;
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				for (std::size_t i = 0; i + gap < r.state.slots.size(); ++i)
				{
					const SortItem a = r.at(i), b = r.at(i + gap);
					r.compare(a, b, i, i + gap);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { r.swap(i, i + gap); changed = true; }
				}
				r.annotate("main", RegionKind::Strided, 0, r.state.slots.size(), gap, -1, "Comb gap " + std::to_string(gap), begin);
			}
		}

		void oddEven(Recorder& r)
		{
			bool changed = true;
			while (changed)
			{
				changed = false;
				for (std::size_t parity = 0; parity < 2; ++parity)
				{
					const std::size_t begin = r.trace.events.size();
					const int group = r.beginPhase(parity == 0 ? "Even compare-exchange phase" : "Odd compare-exchange phase");
					for (std::size_t i = parity; i + 1 < r.state.slots.size(); i += 2)
					{
						const SortItem a = r.at(i), b = r.at(i + 1);
						r.compare(a, b, i, i + 1);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { r.swap(i, i + 1); changed = true; }
					}
					r.endPhase(group, parity == 0 ? "Even compare-exchange phase" : "Odd compare-exchange phase");
					r.annotate("main", RegionKind::Group, parity, r.state.slots.size(), 2, group,
						parity == 0 ? "Even compare-exchange phase" : "Odd compare-exchange phase", begin);
				}
			}
		}

	}
}
