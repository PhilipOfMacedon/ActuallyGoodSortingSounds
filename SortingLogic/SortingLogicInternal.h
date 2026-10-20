#pragma once

#include "SortingLogic.h"

#include <algorithm>
#include <stdexcept>

// Private implementation helpers shared by the trace recorder and replay
// implementation. Keep this header inside the SortingLogic project; it is not
// part of the public facade.
namespace sortinglogic
{
	namespace detail
	{
		inline void require(bool condition, const char* message)
		{
			if (!condition) throw std::logic_error(message);
		}

		inline bool sameItem(const SortItem& a, const SortItem& b)
		{
			return a.stableId == b.stableId && a.value == b.value && a.integerKey == b.integerKey &&
				a.hasIntegerKey == b.hasIntegerKey && a.orientationReversed == b.orientationReversed;
		}

		inline void clearHeldItem(SortState& state)
		{
			state.hasHeldItem = false;
			state.gapIndex = NoGapIndex;
			state.heldItem = SortItem();
		}

		inline std::size_t totalAuxiliaryCapacity(const SortState& state)
		{
			std::size_t total = 0;
			for (std::map<std::string, std::vector<SortSlot> >::const_iterator i = state.auxiliaryItems.begin();
				i != state.auxiliaryItems.end(); ++i)
				total += i->second.size();
			for (std::map<std::string, std::vector<int> >::const_iterator i = state.integerWorkspaces.begin();
				i != state.integerWorkspaces.end(); ++i)
				total += i->second.size();
			return total;
		}
	}
}
