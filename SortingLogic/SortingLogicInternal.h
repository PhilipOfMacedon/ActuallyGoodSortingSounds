#pragma once

#include "SortingLogic.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

// Private implementation helpers shared by the trace recorder and replay
// implementation. Keep this header inside the SortingLogic project; it is not
// part of the public facade.
namespace sortinglogic
{
	namespace detail
	{
		struct OperationLimitReached {};
		struct TraceBudgetReached
		{
			std::string reason;
			explicit TraceBudgetReached(const std::string& text) : reason(text) {}
		};

		inline InputDomain effectiveDomain(const TraceBuildRequest& request)
		{
			// Preserve source compatibility for existing callers that set only the old profile field.
			if (request.domain == InputDomain::NormalizedFloats && request.profile == InputProfile::IntegerKeys)
				return InputDomain::NonnegativeIntegerKeys;
			return request.domain;
		}

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

		inline std::size_t dynamicStringBytes(const std::string& value)
		{
			// Small-string storage is included in the containing object already.
			return value.capacity() > 15 ? value.capacity() + 1 : 0;
		}

		inline std::size_t accountedStateBytes(const SortState& state)
		{
			std::size_t bytes = sizeof(SortState) + state.slots.capacity() * sizeof(SortSlot) +
				state.discardedItems.capacity() * sizeof(SortItem);
			for (std::map<std::string, std::vector<SortSlot> >::const_iterator i = state.auxiliaryItems.begin();
				i != state.auxiliaryItems.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + i->second.capacity() * sizeof(SortSlot);
			for (std::map<std::string, std::vector<int> >::const_iterator i = state.integerWorkspaces.begin();
				i != state.integerWorkspaces.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + i->second.capacity() * sizeof(int);
			for (std::map<std::string, std::uint64_t>::const_iterator i = state.metrics.algorithm.begin();
				i != state.metrics.algorithm.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + sizeof(i->second);
			return bytes;
		}

		inline std::size_t accountedEventBytes(const SemanticEvent& event)
		{
			std::size_t bytes = sizeof(SemanticEvent) + dynamicStringBytes(event.firstLane) + dynamicStringBytes(event.secondLane) +
				dynamicStringBytes(event.workspaceName) + dynamicStringBytes(event.metricName) +
				dynamicStringBytes(event.sourceWorkspace) + dynamicStringBytes(event.destinationWorkspace) + dynamicStringBytes(event.reconstructionLabel) +
				dynamicStringBytes(event.secondSourceWorkspace) + dynamicStringBytes(event.secondDestinationWorkspace) +
				(event.beforeSlots.capacity() + event.afterSlots.capacity()) * sizeof(SortSlot) +
				event.beforeCounters.capacity() * sizeof(int);
			if (event.type == SemanticEventType::ResizeAuxiliary) bytes += event.afterSize * sizeof(SortSlot);
			if (event.type == SemanticEventType::ResizeCounterWorkspace) bytes += event.afterSize * sizeof(int);
			return bytes;
		}
	}
}
