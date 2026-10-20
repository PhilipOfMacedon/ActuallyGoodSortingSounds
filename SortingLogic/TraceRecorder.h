#pragma once

#include "SortingLogicInternal.h"

#include <algorithm>
#include <string>
#include <utility>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder
		{
		public:
			explicit Recorder(const TraceBuildRequest& request)
				: trace(), state(makeInitialState(request.input)), maxOperations(request.maxOperations),
				maxWorkSteps(request.maxWorkSteps), maxTraceBytes(request.maxTraceBytes),
				completionScanEnabled(request.completionScanEnabled), nextGroupId(0)
			{
				trace.algorithm = request.algorithm;
				const AlgorithmDescriptor* algorithm = algorithmDescriptor(request.algorithm);
				trace.familyId = algorithm ? algorithm->familyId : FamilyId();
				trace.variantId = algorithm ? algorithm->variantId : VariantId();
				trace.direction = request.direction;
				trace.domain = effectiveDomain(request);
				trace.profile = inputProfileForDomain(trace.domain);
				trace.pattern = request.pattern;
				trace.configuration = request.configuration;
				trace.shellGapSequence = trace.configuration.shellGapSequence;
				trace.mergeWritebackMode = trace.configuration.mergeWritebackMode;
				trace.mergeExecutionMode = trace.configuration.mergeExecutionMode;
				trace.mergeBufferVisualizationEnabled = request.mergeBufferVisualizationEnabled;
				trace.radixBase = trace.configuration.radixBase;
				trace.bucketCount = request.bucketCount;
				trace.timeMagnitude = trace.configuration.timeMagnitude;
				trace.rotateMergeBufferSize = trace.configuration.rotateMergeBufferSize;
				trace.inputSeed = request.inputSeed;
				trace.input = request.input;
				const AlgorithmDescriptor* descriptor = algorithmDescriptor(request.algorithm);
				trace.outcome = descriptor && descriptor->stable
					? AlgorithmOutcome::SortedStablePermutation : AlgorithmOutcome::SortedPermutation;
				if (descriptor)
					for (std::size_t i = 0; i < descriptor->metrics.size(); ++i)
						state.metrics.algorithm[descriptor->metrics[i].key] = 0;
				trace.initialState = state;
				trace.snapshots.insert(std::make_pair(static_cast<std::size_t>(0), state));
				trace.accountedBytes = accountedStateBytes(state) * 2u;
				if (trace.accountedBytes > maxTraceBytes)
					throw TraceBudgetReached("Trace workspace byte budget is too small for the initial input state.");
			}

			SortTrace trace;
			SortState state;
			std::size_t maxOperations;
			std::size_t maxWorkSteps;
			std::size_t maxTraceBytes;
			bool completionScanEnabled;
			int nextGroupId;

			void chargeWork(std::size_t amount = 1)
			{
				if (amount > maxWorkSteps - std::min(maxWorkSteps, trace.workSteps))
					throw TraceBudgetReached("Internal trace work budget reached (" + std::to_string(maxWorkSteps) + " steps).");
				trace.workSteps += amount;
			}

			SortItem at(std::size_t index) const
			{
				require(index < state.slots.size() && state.slots[index].occupied, "Primary item read is out of range or empty.");
				return state.slots[index].item;
			}

			SortItem auxiliaryAt(const std::string& lane, std::size_t index) const
			{
				std::map<std::string, std::vector<SortSlot> >::const_iterator found = state.auxiliaryItems.find(lane);
				require(found != state.auxiliaryItems.end() && index < found->second.size() && found->second[index].occupied,
					"Auxiliary item read is out of range or empty.");
				return found->second[index].item;
			}

			void append(SemanticEvent event)
			{
				if (event.type != SemanticEventType::Complete && trace.operationCount >= maxOperations)
					throw OperationLimitReached();
				chargeWork();
				const std::size_t eventBytes = accountedEventBytes(event);
				const std::size_t cursor = trace.events.size() + 1;
				const bool snapshotDue = cursor % SnapshotInterval == 0 || event.type == SemanticEventType::Complete;
				const std::size_t snapshotBytes = snapshotDue ? accountedStateBytes(state) + sizeof(std::pair<const std::size_t, SortState>) : 0;
				if (eventBytes > maxTraceBytes - std::min(maxTraceBytes, trace.accountedBytes) ||
					snapshotBytes > maxTraceBytes - std::min(maxTraceBytes, trace.accountedBytes + std::min(eventBytes, maxTraceBytes)))
					throw TraceBudgetReached("Accounted trace memory budget reached (" + std::to_string(maxTraceBytes / (1024u * 1024u)) + " MiB).");
				 event.sequence = trace.events.size();
				if (event.type == SemanticEventType::Compare) trace.comparisonEventIndices.push_back(event.sequence);
				else if (event.type == SemanticEventType::Probe) trace.probeEventIndices.push_back(event.sequence);
				trace.events.push_back(event);
				applyEventForward(state, trace.events.back());
				trace.accountedBytes += eventBytes;
				if (event.type != SemanticEventType::Complete) ++trace.operationCount;
				const std::size_t publishedCursor = trace.events.size();
				if (publishedCursor % SnapshotInterval == 0 || event.type == SemanticEventType::Complete)
				{
					trace.snapshots[publishedCursor] = state;
					trace.accountedBytes += accountedStateBytes(state) + sizeof(std::pair<const std::size_t, SortState>);
				}
			}

			void compare(const SortItem& first, const SortItem& second, std::size_t firstIndex, std::size_t secondIndex,
				const std::string& firstLane = "main", const std::string& secondLane = "main",
				SemanticRole firstRole = SemanticRole::ComparedItem, SemanticRole secondRole = SemanticRole::ComparedItem,
				bool isCompletionScan = false)
			{
				SemanticEvent event;
				event.type = SemanticEventType::Compare;
				event.completionScan = isCompletionScan;
				event.firstIndex = firstIndex;
				event.secondIndex = secondIndex;
				event.firstLane = firstLane;
				event.secondLane = secondLane;
				event.firstItemId = first.stableId;
				event.secondItemId = second.stableId;
				event.comparison = compareItems(first, second, trace.direction);
				event.firstRole = firstRole;
				event.secondRole = secondRole;
				append(event);
			}

			void probeItem(const std::string& lane, std::size_t index, const SortItem& item,
				SemanticRole role = SemanticRole::ProbedItem)
			{
				SemanticEvent event;
				event.type = SemanticEventType::Probe;
				event.firstIndex = index;
				event.firstLane = lane;
				event.firstItemId = item.stableId;
				event.item = item;
				event.firstRole = role;
				append(event);
			}

			void probe(std::size_t index, SemanticRole role = SemanticRole::ProbedItem, const std::string& lane = "main")
			{
				probeItem(lane, index, at(index), role);
			}

			void probeAuxiliary(const std::string& lane, std::size_t index,
				SemanticRole role = SemanticRole::ProbedItem)
			{
				probeItem(lane, index, auxiliaryAt(lane, index), role);
			}

			void discardMain(std::size_t index)
			{
				SemanticEvent event;
				event.type = SemanticEventType::DiscardItem;
				event.firstIndex = index;
				event.item = at(index);
				event.firstItemId = event.item.stableId;
				event.firstRole = SemanticRole::DiscardedItem;
				append(event);
			}

			void replaceScalar(std::size_t index, const SortItem& replacement)
			{
				require(index < state.slots.size() && state.slots[index].occupied &&
					state.slots[index].item.stableId == replacement.stableId,
					"Scalar replacement must preserve the cell's logical item identity.");
				SemanticEvent event;
				event.type = SemanticEventType::ReplaceScalar;
				event.firstIndex = index;
				event.firstItemId = replacement.stableId;
				event.beforeItem = state.slots[index].item;
				event.item = replacement;
				event.firstRole = SemanticRole::WriteTarget;
				append(event);
			}

			void compactAfterDiscard(std::size_t index)
			{
				require(index < state.slots.size() && !state.slots[index].occupied,
					"Compaction requires a discarded gap in the primary sequence.");
				SemanticEvent event;
				event.type = SemanticEventType::CompactRange;
				event.firstIndex = index;
				event.beforeSlots.assign(state.slots.begin() + index, state.slots.end());
				event.afterSlots.assign(state.slots.begin() + index + 1, state.slots.end());
				event.metricDelta = event.afterSlots.size();
				event.firstRole = SemanticRole::WriteTarget;
				append(event);
			}

			int beginPhase(const std::string& label)
			{
				SemanticEvent event;
				event.type = SemanticEventType::PhaseBegin;
				event.workspaceName = label;
				event.groupId = nextGroupId++;
				append(event);
				return event.groupId;
			}

			void endPhase(int groupId, const std::string& label)
			{
				SemanticEvent event;
				event.type = SemanticEventType::PhaseEnd;
				event.workspaceName = label;
				event.groupId = groupId;
				append(event);
			}

			void hold(std::size_t index)
			{
				SemanticEvent event;
				event.type = SemanticEventType::HoldKey;
				event.firstIndex = index;
				event.item = at(index);
				event.firstItemId = event.item.stableId;
				event.firstRole = SemanticRole::HeldKey;
				append(event);
			}

			void shift(std::size_t from, std::size_t to)
			{
				SemanticEvent event;
				event.type = SemanticEventType::ShiftItem;
				event.firstIndex = from;
				event.secondIndex = to;
				event.item = at(from);
				event.firstItemId = event.item.stableId;
				event.firstRole = SemanticRole::ShiftedItem;
				event.secondRole = SemanticRole::WriteTarget;
				append(event);
			}

			void place(std::size_t index, const SortItem& item)
			{
				SemanticEvent event;
				event.type = SemanticEventType::PlaceKey;
				event.firstIndex = index;
				event.item = item;
				event.firstItemId = item.stableId;
				event.firstRole = SemanticRole::WriteTarget;
				append(event);
			}

			void carryWrite(std::size_t index)
			{
				require(state.hasHeldItem && index < state.slots.size() && index != state.gapIndex && state.slots[index].occupied,
					"Cycle write requires a held item and an occupied destination outside the gap.");
				SemanticEvent event;
				event.type = SemanticEventType::CarryWrite;
				event.firstIndex = index;
				event.secondIndex = state.gapIndex;
				event.item = state.heldItem;
				event.beforeItem = state.slots[index].item;
				event.firstItemId = event.item.stableId;
				event.secondItemId = event.beforeItem.stableId;
				event.firstRole = SemanticRole::WriteTarget;
				append(event);
			}

			void swap(std::size_t first, std::size_t second, SemanticRole role = SemanticRole::ComparedItem)
			{
				if (first == second) return;
				SemanticEvent event;
				event.type = SemanticEventType::Swap;
				event.firstIndex = first;
				event.secondIndex = second;
				event.firstItemId = at(first).stableId;
				event.secondItemId = at(second).stableId;
				event.firstRole = role;
				event.secondRole = SemanticRole::WriteTarget;
				append(event);
			}

			void swapAuxiliary(const std::string& lane, std::size_t first, std::size_t second)
			{
				if (first == second) return;
				const SortItem firstItem = auxiliaryAt(lane, first);
				const SortItem secondItem = auxiliaryAt(lane, second);
				SemanticEvent event;
				event.type = SemanticEventType::Swap;
				event.firstIndex = first;
				event.secondIndex = second;
				event.firstLane = lane;
				event.secondLane = lane;
				event.firstItemId = firstItem.stableId;
				event.secondItemId = secondItem.stableId;
				event.firstRole = SemanticRole::AuxiliaryCopy;
				event.secondRole = SemanticRole::WriteTarget;
				append(event);
			}

			void setOrientation(std::size_t index, bool reversed)
			{
				require(index < state.slots.size() && state.slots[index].occupied, "Orientation index is out of range or empty.");
				if (state.slots[index].item.orientationReversed == reversed) return;
				SemanticEvent event;
				event.type = SemanticEventType::SetOrientation;
				event.firstIndex = index;
				event.firstItemId = state.slots[index].item.stableId;
				event.beforeBoolean = state.slots[index].item.orientationReversed;
				event.afterBoolean = reversed;
				append(event);
			}

			void writeMain(std::size_t index, const SortItem& item, SemanticRole role = SemanticRole::WriteTarget)
			{
				SemanticEvent event;
				event.type = SemanticEventType::WriteItem;
				event.firstIndex = index;
				event.firstLane = "main";
				event.beforeOccupied = state.slots[index].occupied;
				if (event.beforeOccupied) event.beforeItem = state.slots[index].item;
				for (std::size_t source = 0; source < state.slots.size(); ++source)
					if (source != index && state.slots[source].occupied && state.slots[source].item.stableId == item.stableId)
					{
						event.secondIndex = source;
						event.beforeSecondOccupied = true;
						event.beforeSecondItem = state.slots[source].item;
						break;
					}
				event.item = item;
				event.firstItemId = item.stableId;
				event.firstRole = role;
				append(event);
			}

			void writeMainRange(std::size_t first, const std::vector<SortItem>& items)
			{
				require(first <= state.slots.size() && items.size() <= state.slots.size() - first,
					"Primary range write is out of range.");
				SemanticEvent event;
				event.type = SemanticEventType::WriteRange;
				event.firstIndex = first;
				event.firstLane = "main";
				event.beforeSlots.reserve(items.size());
				event.afterSlots.reserve(items.size());
				for (std::size_t i = 0; i < items.size(); ++i)
				{
					event.beforeSlots.push_back(state.slots[first + i]);
					event.afterSlots.push_back(SortSlot(items[i]));
				}
				if (!items.empty())
				{
					event.item = items.front();
					event.firstItemId = items.front().stableId;
				}
				event.metricDelta = static_cast<std::uint64_t>(items.size());
				event.firstRole = SemanticRole::WriteTarget;
				append(event);
			}

			void resizeAuxiliary(const std::string& lane, std::size_t size)
			{
				const std::map<std::string, std::vector<SortSlot> >::const_iterator found = state.auxiliaryItems.find(lane);
				const std::size_t oldSize = found == state.auxiliaryItems.end() ? 0 : found->second.size();
				if (oldSize == size) return;
				SemanticEvent event;
				event.type = SemanticEventType::ResizeAuxiliary;
				event.workspaceName = lane;
				event.beforeSize = oldSize;
				event.afterSize = size;
				if (size < oldSize)
					event.beforeSlots.assign(found->second.begin() + size, found->second.end());
				event.metricDelta = state.metrics.peakAuxiliaryCapacity;
				append(event);
			}

			void writeAuxiliary(const std::string& lane, std::size_t index, const SortItem& item)
			{
				std::map<std::string, std::vector<SortSlot> >::const_iterator found = state.auxiliaryItems.find(lane);
				if (found == state.auxiliaryItems.end() || index >= found->second.size()) resizeAuxiliary(lane, index + 1);
				SemanticEvent event;
				event.type = SemanticEventType::AuxiliaryItemWrite;
				event.workspaceName = lane;
				event.firstIndex = index;
				const std::vector<SortSlot>& slots = state.auxiliaryItems[lane];
				event.beforeOccupied = slots[index].occupied;
				if (event.beforeOccupied) event.beforeItem = slots[index].item;
				event.item = item;
				event.firstItemId = item.stableId;
				event.firstRole = SemanticRole::AuxiliaryCopy;
				append(event);
			}

			void resizeCounters(const std::string& lane, std::size_t size)
			{
				const std::map<std::string, std::vector<int> >::const_iterator found = state.integerWorkspaces.find(lane);
				const std::size_t oldSize = found == state.integerWorkspaces.end() ? 0 : found->second.size();
				if (oldSize == size) return;
				SemanticEvent event;
				event.type = SemanticEventType::ResizeCounterWorkspace;
				event.workspaceName = lane;
				event.beforeSize = oldSize;
				event.afterSize = size;
				if (size < oldSize)
					event.beforeCounters.assign(found->second.begin() + size, found->second.end());
				event.metricDelta = state.metrics.peakAuxiliaryCapacity;
				append(event);
			}

			int counter(const std::string& lane, std::size_t index) const
			{
				return state.integerWorkspaces.find(lane)->second[index];
			}

			void writeCounter(const std::string& lane, std::size_t index, int value)
			{
				SemanticEvent event;
				event.type = SemanticEventType::CounterWrite;
				event.workspaceName = lane;
				event.firstIndex = index;
				event.beforeCounter = state.integerWorkspaces[lane][index];
				event.afterCounter = value;
				append(event);
			}

			void setMetric(const std::string& name, std::uint64_t value)
			{
				const std::map<std::string, std::uint64_t>::const_iterator found = state.metrics.algorithm.find(name);
				const std::uint64_t old = found == state.metrics.algorithm.end() ? 0 : found->second;
				if (old == value) return;
				SemanticEvent event;
				event.type = SemanticEventType::CounterWrite;
				event.workspaceName = "__metric";
				event.metricName = name;
				event.beforeOccupied = found != state.metrics.algorithm.end();
				event.beforeCounter = static_cast<int>(old);
				event.afterCounter = static_cast<int>(value);
				append(event);
			}

			void declareMetric(const std::string& name)
			{
				if (state.metrics.algorithm.find(name) != state.metrics.algorithm.end()) return;
				SemanticEvent event;
				event.type = SemanticEventType::CounterWrite;
				event.workspaceName = "__metric";
				event.metricName = name;
				event.beforeOccupied = false;
				event.beforeCounter = 0;
				event.afterCounter = 0;
				append(event);
			}

			void addMetric(const std::string& name, std::uint64_t amount = 1)
			{
				const std::map<std::string, std::uint64_t>::const_iterator found = state.metrics.algorithm.find(name);
				setMetric(name, (found == state.metrics.algorithm.end() ? 0 : found->second) + amount);
			}

			void annotate(const std::string& lane, RegionKind kind, std::size_t first, std::size_t last,
				std::size_t stride, int group, const std::string& label, std::size_t begin)
			{
				RegionAnnotation region;
				region.lane = lane;
				region.kind = kind;
				region.first = first;
				region.last = last;
				region.stride = std::max<std::size_t>(1, stride);
				region.group = group;
				region.label = label;
				region.beginCursor = begin;
				region.endCursor = trace.events.size();
				if (region.endCursor <= region.beginCursor) region.endCursor = region.beginCursor + 1;
				trace.annotations.push_back(region);
			}

			void finish()
			{
				if (completionScanEnabled)
					for (std::size_t i = 0; i + 1 < state.slots.size(); ++i)
						compare(at(i), at(i + 1), i, i + 1, "main", "main",
							SemanticRole::ComparedItem, SemanticRole::ComparedItem, true);
				SemanticEvent event;
				event.type = SemanticEventType::Complete;
				append(event);
				trace.complete = true;
				trace.snapshots[trace.events.size()] = state;
			}

			void finishPartial(const std::string& reason)
			{
				trace.complete = false;
				trace.completionReason = reason;
			const std::size_t cursor = trace.events.size();
			if (trace.snapshots.find(cursor) == trace.snapshots.end())
			{
				const std::size_t snapshotBytes = accountedStateBytes(state) + sizeof(std::pair<const std::size_t, SortState>);
				if (snapshotBytes <= maxTraceBytes - std::min(maxTraceBytes, trace.accountedBytes))
				{
					trace.snapshots[cursor] = state;
					trace.accountedBytes += snapshotBytes;
				}
			}
			}
		};

	}
}
