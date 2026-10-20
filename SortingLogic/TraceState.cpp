#include "SortingLogicInternal.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace sortinglogic
{
	using detail::clearHeldItem;
	using detail::require;
	using detail::sameItem;
	using detail::totalAuxiliaryCapacity;

SemanticEvent::SemanticEvent()
		: sequence(0), type(SemanticEventType::Complete), firstIndex(NoGapIndex), secondIndex(NoGapIndex),
		firstItemId(NoItemId), secondItemId(NoItemId), item(), beforeItem(), beforeSecondItem(), beforeSlots(), afterSlots(), beforeCounters(), comparison(ComparisonResult::Equal),
		firstLane("main"), secondLane("main"), workspaceName(), firstRole(SemanticRole::ComparedItem),
			secondRole(SemanticRole::ComparedItem), completionScan(false), beforeOccupied(false), beforeSecondOccupied(false),
			beforeBoolean(false), afterBoolean(false),
		beforeCounter(0), afterCounter(0), beforeSize(0), afterSize(0), metricName(), metricDelta(0), groupId(-1)
	{
	}

	SortTrace::SortTrace()
		: algorithm(SortAlgorithm::Insertion), familyId("insertion"), variantId("standard"),
		direction(SortDirection::Ascending), profile(InputProfile::NormalizedFloat),
		domain(InputDomain::NormalizedFloats), pattern(InputPattern::Random), configuration(), shellGapSequence(ShellGapSequence::Halving),
		mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite), mergeExecutionMode(MergeExecutionMode::BottomUp),
		mergeBufferVisualizationEnabled(true), radixBase(0), bucketCount(0), timeMagnitude(10), rotateMergeBufferSize(64),
		inputSeed(0), input(), initialState(), events(),
		comparisonEventIndices(), probeEventIndices(), audioEventIndices(), annotations(), snapshots(), operationCount(0), workSteps(0),
		accountedBytes(0), complete(true), outcome(AlgorithmOutcome::SortedPermutation), completionReason()
	{
	}

	RenderSnapshot::RenderSnapshot()
		: state(), activeRoles(), activeRegions(), metrics(), direction(SortDirection::Ascending),
		algorithm(SortAlgorithm::Insertion), familyId("insertion"), variantId("standard"),
		profile(InputProfile::NormalizedFloat), domain(InputDomain::NormalizedFloats),
		pattern(InputPattern::Random), shellGapSequence(ShellGapSequence::Halving), mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite),
		mergeExecutionMode(MergeExecutionMode::BottomUp), mergeBufferVisualizationEnabled(true),
		cursor(0), eventCount(0), itemCount(0), audioEligibleEventCount(0), inputSeed(0), durationSeconds(10.0f), elapsedSeconds(0.0f),
		hasTrace(false), buildingTrace(false), playing(false), complete(false), partial(false), runCompatible(true),
		compatibilityMessage(), completionReason(), status()
	{
	}

	SortState makeInitialState(const std::vector<SortItem>& items)
	{
		SortState state;
		state.slots.reserve(items.size());
		state.discardedItems.clear();
		for (std::size_t i = 0; i < items.size(); ++i) state.slots.push_back(SortSlot(items[i]));
		return state;
	}

	ComparisonResult compareItems(const SortItem& left, const SortItem& right, SortDirection direction)
	{
		int order = 0;
		if (left.hasIntegerKey && right.hasIntegerKey)
			order = left.integerKey < right.integerKey ? -1 : (left.integerKey > right.integerKey ? 1 : 0);
		else order = left.value < right.value ? -1 : (left.value > right.value ? 1 : 0);
		if (direction == SortDirection::Descending) order = -order;
		return order < 0 ? ComparisonResult::Less : (order > 0 ? ComparisonResult::Greater : ComparisonResult::Equal);
	}

	void applyEventForward(SortState& state, const SemanticEvent& event)
	{
		switch (event.type)
		{
		case SemanticEventType::Compare:
			if (!event.completionScan) ++state.metrics.comparisons;
			if (event.firstLane == "main" && event.secondRole == SemanticRole::HeldKey)
			{
				require(state.hasHeldItem, "Insertion comparison requires a held key.");
				require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied, "Compare left slot is missing.");
				require(state.slots[event.firstIndex].item.stableId == event.firstItemId, "Compare left item identity mismatch.");
				require(state.heldItem.stableId == event.secondItemId, "Compare held item identity mismatch.");
			}
			break;
		case SemanticEventType::Probe:
		case SemanticEventType::PhaseBegin:
		case SemanticEventType::PhaseEnd:
			break;
		case SemanticEventType::SetOrientation:
			require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied &&
				state.slots[event.firstIndex].item.stableId == event.firstItemId &&
				state.slots[event.firstIndex].item.orientationReversed == event.beforeBoolean,
				"Orientation event before-state mismatch.");
			state.slots[event.firstIndex].item.orientationReversed = event.afterBoolean;
			break;
		case SemanticEventType::ReplaceScalar:
			require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied &&
				sameItem(state.slots[event.firstIndex].item, event.beforeItem) &&
				event.item.stableId == event.beforeItem.stableId,
				"Scalar replacement before-state mismatch.");
			state.slots[event.firstIndex].item = event.item;
			++state.metrics.primaryWrites;
			break;
		case SemanticEventType::CompactRange:
			require(event.firstIndex < state.slots.size() &&
				event.beforeSlots.size() == state.slots.size() - event.firstIndex &&
				event.afterSlots.size() + 1 == event.beforeSlots.size(),
				"Compaction range sizes are invalid.");
			for (std::size_t i = 0; i < event.beforeSlots.size(); ++i)
			{
				const SortSlot& current = state.slots[event.firstIndex + i];
				const SortSlot& before = event.beforeSlots[i];
				require(current.occupied == before.occupied && (!current.occupied || sameItem(current.item, before.item)),
					"Compaction range before-state mismatch.");
			}
			state.slots.erase(state.slots.begin() + event.firstIndex, state.slots.end());
			state.slots.insert(state.slots.end(), event.afterSlots.begin(), event.afterSlots.end());
			state.metrics.primaryWrites += event.metricDelta;
			break;
		case SemanticEventType::HoldKey:
			require(!state.hasHeldItem && event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied, "Invalid hold event.");
			require(sameItem(state.slots[event.firstIndex].item, event.item), "Hold event item mismatch.");
			state.slots[event.firstIndex].occupied = false;
			state.hasHeldItem = true; state.heldItem = event.item; state.gapIndex = event.firstIndex;
			break;
		case SemanticEventType::ShiftItem:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && event.secondIndex < state.slots.size(), "Invalid shift event.");
			require(state.gapIndex == event.secondIndex && !state.slots[event.secondIndex].occupied && state.slots[event.firstIndex].occupied, "Shift gap mismatch.");
			state.slots[event.secondIndex] = state.slots[event.firstIndex];
			state.slots[event.firstIndex].occupied = false;
			state.gapIndex = event.firstIndex;
			break;
		case SemanticEventType::PlaceKey:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && state.gapIndex == event.firstIndex && !state.slots[event.firstIndex].occupied, "Invalid place event.");
			require(sameItem(state.heldItem, event.item), "Place event item mismatch.");
			state.slots[event.firstIndex] = SortSlot(state.heldItem); clearHeldItem(state);
			break;
		case SemanticEventType::CarryWrite:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && event.secondIndex == state.gapIndex &&
				event.firstIndex != state.gapIndex && state.slots[event.firstIndex].occupied &&
				sameItem(state.heldItem, event.item) && sameItem(state.slots[event.firstIndex].item, event.beforeItem),
				"Cycle carry write before-state mismatch.");
			state.slots[event.firstIndex] = SortSlot(state.heldItem);
			state.heldItem = event.beforeItem;
			++state.metrics.primaryWrites;
			break;
		case SemanticEventType::Swap:
		{
			std::vector<SortSlot>* firstLane = event.firstLane.empty() || event.firstLane == "main"
				? &state.slots : &state.auxiliaryItems[event.firstLane];
			std::vector<SortSlot>* secondLane = event.secondLane.empty() || event.secondLane == "main"
				? &state.slots : &state.auxiliaryItems[event.secondLane];
			require(event.firstIndex < firstLane->size() && event.secondIndex < secondLane->size(), "Swap index is out of range.");
			std::swap((*firstLane)[event.firstIndex], (*secondLane)[event.secondIndex]); ++state.metrics.swaps;
			break;
		}
		case SemanticEventType::WriteItem:
			require(event.firstIndex < state.slots.size(), "Primary write index is out of range.");
			if (event.secondIndex != NoGapIndex)
			{
				require(event.secondIndex < state.slots.size(), "Primary source index is out of range.");
				state.slots[event.secondIndex].occupied = false;
			}
			state.slots[event.firstIndex] = SortSlot(event.item); ++state.metrics.primaryWrites;
			break;
		case SemanticEventType::WriteRange:
			require(event.beforeSlots.size() == event.afterSlots.size() &&
				event.firstIndex <= state.slots.size() && event.afterSlots.size() <= state.slots.size() - event.firstIndex,
				"Primary range write payload is invalid.");
			for (std::size_t i = 0; i < event.afterSlots.size(); ++i)
			{
				const SortSlot& current = state.slots[event.firstIndex + i];
				const SortSlot& before = event.beforeSlots[i];
				require(current.occupied == before.occupied && (!current.occupied || sameItem(current.item, before.item)),
					"Primary range write before-state mismatch.");
				state.slots[event.firstIndex + i] = event.afterSlots[i];
			}
			state.metrics.primaryWrites += static_cast<std::uint64_t>(event.afterSlots.size());
			break;
		case SemanticEventType::DiscardItem:
			require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied &&
				sameItem(state.slots[event.firstIndex].item, event.item), "Discard event item mismatch.");
			state.slots[event.firstIndex].occupied = false;
			state.discardedItems.push_back(event.item);
			++state.metrics.discards;
			break;
		case SemanticEventType::AuxiliaryItemWrite:
		{
			std::vector<SortSlot>& lane = state.auxiliaryItems[event.workspaceName];
			require(event.firstIndex < lane.size(), "Auxiliary write index is out of range.");
			lane[event.firstIndex] = SortSlot(event.item); ++state.metrics.auxiliaryItemWrites;
			break;
		}
		case SemanticEventType::CounterWrite:
			if (event.workspaceName == "__metric") state.metrics.algorithm[event.metricName] = static_cast<std::uint64_t>(event.afterCounter);
			else
			{
				std::vector<int>& lane = state.integerWorkspaces[event.workspaceName];
				require(event.firstIndex < lane.size(), "Counter write index is out of range.");
				lane[event.firstIndex] = event.afterCounter; ++state.metrics.auxiliaryCounterUpdates;
			}
			break;
		case SemanticEventType::ResizeAuxiliary:
			if (event.afterSize == 0) state.auxiliaryItems.erase(event.workspaceName);
			else state.auxiliaryItems[event.workspaceName].resize(event.afterSize);
			state.metrics.peakAuxiliaryCapacity = std::max(state.metrics.peakAuxiliaryCapacity, totalAuxiliaryCapacity(state));
			break;
		case SemanticEventType::ResizeCounterWorkspace:
		{
			const std::map<std::string, std::vector<int> >::const_iterator found = state.integerWorkspaces.find(event.workspaceName);
			const std::size_t currentSize = found == state.integerWorkspaces.end() ? 0 : found->second.size();
			require(currentSize == event.beforeSize, "Counter workspace resize before-size mismatch.");
			require(event.beforeCounters.size() == (event.afterSize < event.beforeSize ? event.beforeSize - event.afterSize : 0),
				"Counter workspace resize payload is invalid.");
			if (event.afterSize == 0) state.integerWorkspaces.erase(event.workspaceName);
			else state.integerWorkspaces[event.workspaceName].resize(event.afterSize, 0);
			state.metrics.peakAuxiliaryCapacity = std::max(state.metrics.peakAuxiliaryCapacity, totalAuxiliaryCapacity(state));
			break;
		}
		case SemanticEventType::Complete:
			require(!state.hasHeldItem, "Trace cannot complete while an item is held.");
			break;
		}
	}

	void applyEventBackward(SortState& state, const SemanticEvent& event)
	{
		switch (event.type)
		{
		case SemanticEventType::Compare:
			if (!event.completionScan && state.metrics.comparisons) --state.metrics.comparisons;
			break;
		case SemanticEventType::Probe:
		case SemanticEventType::PhaseBegin:
		case SemanticEventType::PhaseEnd:
			break;
		case SemanticEventType::SetOrientation:
			require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied &&
				state.slots[event.firstIndex].item.stableId == event.firstItemId &&
				state.slots[event.firstIndex].item.orientationReversed == event.afterBoolean,
				"Orientation event after-state mismatch.");
			state.slots[event.firstIndex].item.orientationReversed = event.beforeBoolean;
			break;
		case SemanticEventType::ReplaceScalar:
			require(event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied &&
				sameItem(state.slots[event.firstIndex].item, event.item),
				"Scalar replacement after-state mismatch.");
			state.slots[event.firstIndex].item = event.beforeItem;
			--state.metrics.primaryWrites;
			break;
		case SemanticEventType::CompactRange:
			require(state.slots.size() == event.firstIndex + event.afterSlots.size(),
				"Compaction range current size is invalid.");
			for (std::size_t i = 0; i < event.afterSlots.size(); ++i)
			{
				const SortSlot& current = state.slots[event.firstIndex + i];
				const SortSlot& after = event.afterSlots[i];
				require(current.occupied == after.occupied && (!current.occupied || sameItem(current.item, after.item)),
					"Compaction range after-state mismatch.");
			}
			state.slots.erase(state.slots.begin() + event.firstIndex, state.slots.end());
			state.slots.insert(state.slots.end(), event.beforeSlots.begin(), event.beforeSlots.end());
			state.metrics.primaryWrites -= event.metricDelta;
			break;
		case SemanticEventType::HoldKey:
			require(state.hasHeldItem && event.firstIndex < state.slots.size(), "Invalid reverse hold.");
			state.slots[event.firstIndex] = SortSlot(state.heldItem); clearHeldItem(state); break;
		case SemanticEventType::ShiftItem:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && event.secondIndex < state.slots.size(), "Invalid reverse shift.");
			state.slots[event.firstIndex] = state.slots[event.secondIndex]; state.slots[event.secondIndex].occupied = false; state.gapIndex = event.secondIndex; break;
		case SemanticEventType::PlaceKey:
			require(!state.hasHeldItem && event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied, "Invalid reverse place.");
			state.heldItem = state.slots[event.firstIndex].item; state.slots[event.firstIndex].occupied = false; state.hasHeldItem = true; state.gapIndex = event.firstIndex; break;
		case SemanticEventType::CarryWrite:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && event.secondIndex == state.gapIndex &&
				event.firstIndex != state.gapIndex && state.slots[event.firstIndex].occupied &&
				sameItem(state.slots[event.firstIndex].item, event.item) && sameItem(state.heldItem, event.beforeItem),
				"Reverse cycle carry write after-state mismatch.");
			state.slots[event.firstIndex] = SortSlot(event.beforeItem);
			state.heldItem = event.item;
			if (state.metrics.primaryWrites) --state.metrics.primaryWrites;
			break;
		case SemanticEventType::Swap:
		{
			std::vector<SortSlot>* firstLane = event.firstLane.empty() || event.firstLane == "main"
				? &state.slots : &state.auxiliaryItems[event.firstLane];
			std::vector<SortSlot>* secondLane = event.secondLane.empty() || event.secondLane == "main"
				? &state.slots : &state.auxiliaryItems[event.secondLane];
			require(event.firstIndex < firstLane->size() && event.secondIndex < secondLane->size(), "Reverse swap index is out of range.");
			std::swap((*firstLane)[event.firstIndex], (*secondLane)[event.secondIndex]); if (state.metrics.swaps) --state.metrics.swaps; break;
		}
		case SemanticEventType::WriteItem:
			state.slots[event.firstIndex] = event.beforeOccupied ? SortSlot(event.beforeItem) : SortSlot();
			if (event.secondIndex != NoGapIndex)
				state.slots[event.secondIndex] = event.beforeSecondOccupied ? SortSlot(event.beforeSecondItem) : SortSlot();
			if (state.metrics.primaryWrites) --state.metrics.primaryWrites;
			break;
		case SemanticEventType::WriteRange:
			require(event.beforeSlots.size() == event.afterSlots.size() &&
				event.firstIndex <= state.slots.size() && event.afterSlots.size() <= state.slots.size() - event.firstIndex,
				"Reverse primary range write payload is invalid.");
			for (std::size_t i = 0; i < event.beforeSlots.size(); ++i)
			{
				const SortSlot& current = state.slots[event.firstIndex + i];
				const SortSlot& after = event.afterSlots[i];
				require(current.occupied == after.occupied && (!current.occupied || sameItem(current.item, after.item)),
					"Reverse primary range write after-state mismatch.");
				state.slots[event.firstIndex + i] = event.beforeSlots[i];
			}
			state.metrics.primaryWrites -= std::min<std::uint64_t>(state.metrics.primaryWrites,
				static_cast<std::uint64_t>(event.beforeSlots.size()));
			break;
		case SemanticEventType::DiscardItem:
			require(!state.discardedItems.empty() && sameItem(state.discardedItems.back(), event.item), "Reverse discard item mismatch.");
			state.discardedItems.pop_back();
			state.slots[event.firstIndex] = SortSlot(event.item);
			if (state.metrics.discards) --state.metrics.discards;
			break;
		case SemanticEventType::AuxiliaryItemWrite:
			state.auxiliaryItems[event.workspaceName][event.firstIndex] = event.beforeOccupied ? SortSlot(event.beforeItem) : SortSlot(); if (state.metrics.auxiliaryItemWrites) --state.metrics.auxiliaryItemWrites; break;
		case SemanticEventType::CounterWrite:
			if (event.workspaceName == "__metric")
			{
				if (event.beforeOccupied) state.metrics.algorithm[event.metricName] = static_cast<std::uint64_t>(event.beforeCounter);
				else state.metrics.algorithm.erase(event.metricName);
			}
			else { state.integerWorkspaces[event.workspaceName][event.firstIndex] = event.beforeCounter; if (state.metrics.auxiliaryCounterUpdates) --state.metrics.auxiliaryCounterUpdates; }
			break;
		case SemanticEventType::ResizeAuxiliary:
			if (event.beforeSize == 0) state.auxiliaryItems.erase(event.workspaceName);
			else
			{
				std::vector<SortSlot>& lane = state.auxiliaryItems[event.workspaceName];
				lane.resize(event.beforeSize);
				for (std::size_t i = 0; i < event.beforeSlots.size(); ++i)
					lane[event.afterSize + i] = event.beforeSlots[i];
			}
			state.metrics.peakAuxiliaryCapacity = static_cast<std::size_t>(event.metricDelta); break;
		case SemanticEventType::ResizeCounterWorkspace:
			if (event.beforeSize == 0) state.integerWorkspaces.erase(event.workspaceName);
			else
			{
				std::vector<int>& lane = state.integerWorkspaces[event.workspaceName];
				lane.resize(event.beforeSize, 0);
				if (event.afterSize < event.beforeSize)
					for (std::size_t i = 0; i < event.beforeCounters.size(); ++i)
						lane[event.afterSize + i] = event.beforeCounters[i];
			}
			state.metrics.peakAuxiliaryCapacity = static_cast<std::size_t>(event.metricDelta); break;
		case SemanticEventType::Complete: break;
		}
	}

	bool validateState(const SortState& state, std::size_t expectedItemCount, std::string* reason)
	{
		std::set<std::uint64_t> primaryIds;
		std::set<std::uint64_t> logicalIds;
		for (std::size_t i = 0; i < state.slots.size(); ++i)
			if (state.slots[i].occupied)
			{
				if (!primaryIds.insert(state.slots[i].item.stableId).second) { if (reason) *reason = "A primary item identity appears more than once."; return false; }
				logicalIds.insert(state.slots[i].item.stableId);
			}
		if (state.hasHeldItem)
		{
			if (state.gapIndex >= state.slots.size() || state.slots[state.gapIndex].occupied) { if (reason) *reason = "Held item has no matching gap."; return false; }
			if (!primaryIds.insert(state.heldItem.stableId).second) { if (reason) *reason = "Held item duplicates a primary identity."; return false; }
			logicalIds.insert(state.heldItem.stableId);
		}
		for (std::size_t i = 0; i < state.discardedItems.size(); ++i)
		{
			if (!primaryIds.insert(state.discardedItems[i].stableId).second)
			{
				if (reason) *reason = "A discarded item still appears in active storage.";
				return false;
			}
			logicalIds.insert(state.discardedItems[i].stableId);
		}
		for (std::map<std::string, std::vector<SortSlot> >::const_iterator lane = state.auxiliaryItems.begin(); lane != state.auxiliaryItems.end(); ++lane)
			for (std::size_t i = 0; i < lane->second.size(); ++i)
				if (lane->second[i].occupied) logicalIds.insert(lane->second[i].item.stableId);
		if (logicalIds.size() != expectedItemCount) { if (reason) *reason = "The workspace lost a logical item identity."; return false; }
		if (reason) reason->clear();
		return true;
	}

	std::vector<std::size_t> shellGaps(std::size_t count, ShellGapSequence sequence)
	{
		std::vector<std::size_t> gaps;
		if (count < 2) return gaps;
		if (sequence == ShellGapSequence::Halving)
		{
			for (std::size_t gap = count / 2; gap > 0; gap /= 2) gaps.push_back(gap);
		}
		else if (sequence == ShellGapSequence::Knuth)
		{
			std::size_t gap = 1;
			while (gap < count) { gaps.push_back(gap); if (gap > (count - 1) / 3) break; gap = gap * 3 + 1; }
			std::reverse(gaps.begin(), gaps.end());
		}
		else if (sequence == ShellGapSequence::Ciura)
		{
			const std::size_t ciura[] = { 1, 4, 10, 23, 57, 132, 301, 701, 1750 };
			for (std::size_t i = 0; i < sizeof(ciura) / sizeof(ciura[0]); ++i) if (ciura[i] < count) gaps.push_back(ciura[i]);
			std::reverse(gaps.begin(), gaps.end());
		}
		else
		{
			for (std::size_t k = 0; ; ++k)
			{
				const double power = std::pow(9.0 / 4.0, static_cast<double>(k));
				const std::size_t gap = static_cast<std::size_t>(std::ceil((9.0 * power - 4.0) / 5.0));
				if (gap >= count) break;
				gaps.push_back(gap);
			}
			std::reverse(gaps.begin(), gaps.end());
		}
		return gaps;
	}


}
