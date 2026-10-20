#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <string>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void pancake(Recorder& r)
		{
			for (std::size_t end = r.state.slots.size(); end > 1; --end)
			{
				std::size_t maximum = 0;
				for (std::size_t i = 1; i < end; ++i)
				{
					const SortItem a = r.at(maximum), b = r.at(i);
					r.compare(a, b, maximum, i, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Less) maximum = i;
				}
				if (maximum == end - 1) continue;
				const std::size_t begin = r.trace.events.size();
				for (std::size_t a = 0, b = maximum; a < b; ++a, --b) r.swap(a, b, SemanticRole::CurrentMaximum);
				for (std::size_t a = 0, b = end - 1; a < b; ++a, --b) r.swap(a, b, SemanticRole::CurrentMaximum);
				r.annotate("main", RegionKind::Contiguous, 0, end, 1, -1, "Pancake prefix flip", begin);
			}
		}

		void pancakeFlip(Recorder& r, std::size_t length, bool burnt, const std::string& label)
		{
			if (length == 0) return;
			const std::size_t begin = r.trace.events.size();
			const int group = r.beginPhase(label);
			for (std::size_t left = 0, right = length - 1; left < right; ++left, --right)
				r.swap(left, right, SemanticRole::ProcessedItem);
			if (burnt)
				for (std::size_t i = 0; i < length; ++i)
					r.setOrientation(i, !r.at(i).orientationReversed);
			r.endPhase(group, label);
			r.annotate("main", RegionKind::Contiguous, 0, length, 1, group, label, begin);
		}

		std::size_t pancakeExtreme(Recorder& r, std::size_t end, SemanticRole& role)
		{
			std::size_t extreme = 0;
			role = r.trace.direction == SortDirection::Ascending ? SemanticRole::CurrentMaximum : SemanticRole::CurrentMinimum;
			for (std::size_t i = 1; i < end; ++i)
			{
				const SortItem candidate = r.at(i), current = r.at(extreme);
				r.compare(candidate, current, i, extreme, "main", "main", role, SemanticRole::ComparedItem);
				if (compareItems(candidate, current, r.trace.direction) != ComparisonResult::Less) extreme = i;
			}
			return extreme;
		}

		void arrayvBurntPancake(Recorder& r)
		{
			for (std::size_t end = r.state.slots.size(); end > 1; --end)
			{
				SemanticRole role = SemanticRole::CurrentMaximum;
				const std::size_t extreme = pancakeExtreme(r, end, role);
				if (extreme + 1 == end) continue;
				// Preserve the pinned source's four-reversal schedule. Inclusive source
				// endpoints become prefix lengths here; its negative final bound is a no-op.
				pancakeFlip(r, extreme + 1, true, "ArrayV burnt prefix flip");
				pancakeFlip(r, end, true, "ArrayV burnt placement flip");
				pancakeFlip(r, end - 1, true, "ArrayV burnt inner flip");
				if (extreme > 0) pancakeFlip(r, extreme, true, "ArrayV burnt final flip");
			}
		}

		void canonicalBurntPancake(Recorder& r)
		{
			for (std::size_t end = r.state.slots.size(); end > 1; --end)
			{
				SemanticRole role = SemanticRole::CurrentMaximum;
				const std::size_t extreme = pancakeExtreme(r, end, role);
				if (extreme + 1 == end && !r.at(extreme).orientationReversed) continue;
				// Move the chosen item to the top, orient it down, then place it upright.
				// Earlier positions may be disturbed; they remain in the active prefix.
				if (extreme > 0) pancakeFlip(r, extreme + 1, true, "Burnt pancake to top");
				if (!r.at(0).orientationReversed) pancakeFlip(r, 1, true, "Burnt pancake orientation correction");
				pancakeFlip(r, end, true, "Burnt pancake to destination");
			}
		}

		ComparisonResult pancakeCompare(Recorder& r, std::size_t first, std::size_t second)
		{
			const SortItem a = r.at(first), b = r.at(second);
			r.compare(a, b, first, second);
			return compareItems(a, b, r.trace.direction);
		}

		std::size_t pancakeMonoboundForward(Recorder& r, std::size_t start, std::size_t end, std::size_t valueIndex)
		{
			std::size_t top = end - start;
			while (top > 1)
			{
				const std::size_t middle = top / 2;
				const SortItem key = r.at(valueIndex), candidate = r.at(end - middle);
				r.compare(key, candidate, valueIndex, end - middle, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater) end -= middle;
				top -= middle;
			}
			const SortItem key = r.at(valueIndex), candidate = r.at(end - 1);
			r.compare(key, candidate, valueIndex, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
			return compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater ? end - 1 : end;
		}

		std::size_t pancakeMonoboundBackward(Recorder& r, std::size_t start, std::size_t end, std::size_t valueIndex)
		{
			std::size_t top = end - start;
			while (top > 1)
			{
				const std::size_t middle = top / 2;
				const SortItem candidate = r.at(start + middle), key = r.at(valueIndex);
				r.compare(candidate, key, start + middle, valueIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				if (compareItems(candidate, key, r.trace.direction) == ComparisonResult::Greater) start += middle;
				top -= middle;
			}
			const SortItem candidate = r.at(start), key = r.at(valueIndex);
			r.compare(candidate, key, start, valueIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
			return compareItems(candidate, key, r.trace.direction) == ComparisonResult::Greater ? start + 1 : start;
		}

		bool pancakeFront(Recorder& r, std::size_t length)
		{
			bool forward = true;
			if (pancakeCompare(r, 0, 1) == ComparisonResult::Greater)
				pancakeFlip(r, 2, false, "Pancake insertion front pair");
			if (length > 2 && pancakeCompare(r, 1, 2) == ComparisonResult::Greater)
			{
				if (pancakeCompare(r, 0, 2) == ComparisonResult::Greater)
				{
					pancakeFlip(r, 2, false, "Pancake insertion front decision");
					forward = false;
				}
				else
				{
					pancakeFlip(r, 3, false, "Pancake insertion front decision");
					pancakeFlip(r, 2, false, "Pancake insertion front correction");
					forward = false;
				}
			}
			return forward;
		}

		void arrayvPancakeInsertion(Recorder& r)
		{
			const std::size_t length = r.state.slots.size();
			if (length < 2) return;
			bool forward = pancakeFront(r, length);
			for (std::size_t i = 3; i < length; ++i)
			{
				if (forward)
				{
					if (pancakeCompare(r, i - 1, i) != ComparisonResult::Greater) continue;
					if (pancakeCompare(r, 0, i) == ComparisonResult::Greater)
						pancakeFlip(r, i, false, "Pancake insertion reverse prefix");
					else
					{
						const std::size_t position = pancakeMonoboundForward(r, 0, i, i);
						pancakeFlip(r, i + 1, false, "Pancake insertion bring key forward");
						const std::size_t end = i - position;
						if (end > 0) pancakeFlip(r, end + 1, false, "Pancake insertion rotate prefix");
						if (end > 1) pancakeFlip(r, end, false, "Pancake insertion restore prefix");
					}
					forward = false;
				}
				else
				{
					if (pancakeCompare(r, i - 1, i) == ComparisonResult::Greater) continue;
					if (pancakeCompare(r, 0, i) != ComparisonResult::Greater)
						pancakeFlip(r, i, false, "Pancake insertion restore forward prefix");
					else
					{
						const std::size_t position = pancakeMonoboundBackward(r, 0, i, i);
						pancakeFlip(r, i + 1, false, "Pancake insertion bring key backward");
						const std::size_t end = i - position;
						if (end > 0) pancakeFlip(r, end + 1, false, "Pancake insertion rotate backward prefix");
						if (end > 1) pancakeFlip(r, end, false, "Pancake insertion restore backward prefix");
					}
					forward = true;
				}
			}
			if (!forward) pancakeFlip(r, length, false, "Pancake insertion final orientation");
		}

	}
}
