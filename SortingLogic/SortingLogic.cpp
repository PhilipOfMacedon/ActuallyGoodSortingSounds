#include "SortingLogic.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sortinglogic
{
	namespace
	{
	void require(bool condition, const char* message)
		{
			if (!condition) throw std::logic_error(message);
		}

	InputDomain effectiveDomain(const TraceBuildRequest& request)
	{
		// Preserve source compatibility for existing callers that set only the old profile field.
		if (request.domain == InputDomain::NormalizedFloats && request.profile == InputProfile::IntegerKeys)
			return InputDomain::NonnegativeIntegerKeys;
		return request.domain;
	}

		bool sameItem(const SortItem& a, const SortItem& b)
		{
			return a.stableId == b.stableId && a.value == b.value && a.integerKey == b.integerKey &&
				a.hasIntegerKey == b.hasIntegerKey && a.orientationReversed == b.orientationReversed;
		}

		const char* metricName(SortAlgorithm algorithm)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
			return descriptor ? descriptor->displayName : "Unknown Algorithm";
		}

		void clearHeldItem(SortState& state)
		{
			state.hasHeldItem = false;
			state.gapIndex = NoGapIndex;
			state.heldItem = SortItem();
		}

		struct OperationLimitReached {};
		struct TraceBudgetReached { std::string reason; explicit TraceBudgetReached(const std::string& text) : reason(text) {} };

		std::size_t dynamicStringBytes(const std::string& value)
		{
			// MSVC's small-string storage is part of std::string itself and is already
			// included in the containing object size.
			return value.capacity() > 15 ? value.capacity() + 1 : 0;
		}

		std::size_t accountedStateBytes(const SortState& state)
		{
			std::size_t bytes = sizeof(SortState) + state.slots.capacity() * sizeof(SortSlot) +
				state.discardedItems.capacity() * sizeof(SortItem);
			for (std::map<std::string, std::vector<SortSlot> >::const_iterator i = state.auxiliaryItems.begin(); i != state.auxiliaryItems.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + i->second.capacity() * sizeof(SortSlot);
			for (std::map<std::string, std::vector<int> >::const_iterator i = state.integerWorkspaces.begin(); i != state.integerWorkspaces.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + i->second.capacity() * sizeof(int);
			for (std::map<std::string, std::uint64_t>::const_iterator i = state.metrics.algorithm.begin(); i != state.metrics.algorithm.end(); ++i)
				bytes += sizeof(i->first) + dynamicStringBytes(i->first) + sizeof(i->second);
			return bytes;
		}

		std::size_t accountedEventBytes(const SemanticEvent& event)
		{
			std::size_t bytes = sizeof(SemanticEvent) + dynamicStringBytes(event.firstLane) + dynamicStringBytes(event.secondLane) +
				dynamicStringBytes(event.workspaceName) + dynamicStringBytes(event.metricName) +
				(event.beforeSlots.capacity() + event.afterSlots.capacity()) * sizeof(SortSlot) +
				event.beforeCounters.capacity() * sizeof(int);
			if (event.type == SemanticEventType::ResizeAuxiliary) bytes += event.afterSize * sizeof(SortSlot);
			if (event.type == SemanticEventType::ResizeCounterWorkspace) bytes += event.afterSize * sizeof(int);
			return bytes;
		}

		std::size_t totalAuxiliaryCapacity(const SortState& state)
		{
			std::size_t total = 0;
			for (std::map<std::string, std::vector<SortSlot> >::const_iterator i = state.auxiliaryItems.begin(); i != state.auxiliaryItems.end(); ++i)
				total += i->second.size();
			for (std::map<std::string, std::vector<int> >::const_iterator i = state.integerWorkspaces.begin(); i != state.integerWorkspaces.end(); ++i)
				total += i->second.size();
			return total;
		}

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
				trace.shellGapSequence = request.shellGapSequence;
				trace.mergeWritebackMode = request.mergeWritebackMode;
				trace.mergeExecutionMode = request.mergeExecutionMode;
				trace.mergeBufferVisualizationEnabled = request.mergeBufferVisualizationEnabled;
				trace.radixBase = request.radixBase;
				trace.bucketCount = request.bucketCount;
				trace.timeMagnitude = request.timeMagnitude;
				trace.rotateMergeBufferSize = request.rotateMergeBufferSize;
				trace.inputSeed = request.inputSeed;
				trace.input = request.input;
				const AlgorithmDescriptor* descriptor = algorithmDescriptor(request.algorithm);
				trace.outcome = descriptor && descriptor->stable
					? AlgorithmOutcome::SortedStablePermutation : AlgorithmOutcome::SortedPermutation;
				if (request.algorithm == SortAlgorithm::ArrayvOptimizedStooge)
					trace.outcome = AlgorithmOutcome::IncompleteDemonstration;
				if (request.algorithm == SortAlgorithm::ArrayvFun)
					trace.outcome = AlgorithmOutcome::IncompleteDemonstration;
				if (request.algorithm == SortAlgorithm::ArrayvStalinOverwrite)
					trace.outcome = AlgorithmOutcome::SortedValueRewrite;
				if (request.algorithm == SortAlgorithm::CanonicalStalinFilter)
					trace.outcome = AlgorithmOutcome::SortedWithDiscards;
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

		bool precedes(const SortItem& a, const SortItem& b, SortDirection direction)
		{
			return compareItems(a, b, direction) == ComparisonResult::Less;
		}

		void insertion(Recorder& r)
		{
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t position = i;
				while (position > 0)
				{
					const SortItem previous = r.at(position - 1);
					r.compare(previous, key, position - 1, position, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
					if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
					r.shift(position - 1, position);
					--position;
				}
				r.place(position, key);
			}
		}

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

		void badSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("bad_sort_candidate_checks");
			r.declareMetric("bad_sort_comparisons");
			r.declareMetric("bad_sort_exchanges");
			for (std::size_t first = 0; first < count; ++first)
			{
				std::size_t selected = first;
				for (std::size_t candidate = first; candidate < count; ++candidate)
				{
					r.addMetric("bad_sort_candidate_checks");
					bool suffixMinimum = true;
					for (std::size_t later = candidate + 1; later < count; ++later)
					{
						const SortItem current = r.at(candidate), other = r.at(later);
						r.compare(current, other, candidate, later, "main", "main",
							SemanticRole::CurrentMinimum, SemanticRole::ProbedItem);
						r.addMetric("bad_sort_comparisons");
						if (compareItems(current, other, r.trace.direction) == ComparisonResult::Greater)
						{
							suffixMinimum = false;
							break;
						}
						r.chargeWork();
					}
					if (suffixMinimum)
					{
						selected = candidate;
						break;
					}
				}
				if (selected != first)
				{
					r.swap(first, selected, SemanticRole::CurrentMinimum);
					r.addMetric("bad_sort_exchanges");
				}
			}
		}

		void asynchronousSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("asynchronous_key_passes");
			r.declareMetric("asynchronous_source_probes");
			r.declareMetric("asynchronous_emitted_items");
			if (count == 0) return;

			const std::string lane = "asynchronous_scan";
			const std::string consumedLane = "asynchronous_consumed";
			r.resizeAuxiliary(lane, count);
			r.resizeCounters(consumedLane, count);
			const auto valueRank = [&r](const SortItem& item) -> std::int64_t
			{
				return r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(item.integerKey) : -static_cast<std::int64_t>(item.integerKey);
			};
			std::int64_t minimumRank = valueRank(r.at(0)), maximumRank = minimumRank;
			for (std::size_t i = 0; i < count; ++i)
			{
				r.chargeWork();
				const SortItem item = r.at(i);
				r.writeAuxiliary(lane, i, item);
				const std::int64_t rank = valueRank(item);
				minimumRank = (std::min)(minimumRank, rank);
				maximumRank = (std::max)(maximumRank, rank);
			}

			std::uint64_t completedPasses = 0, probes = 0;
			std::size_t output = 0;
			for (std::int64_t currentRank = minimumRank; currentRank <= maximumRank; ++currentRank)
			{
				const int phase = r.beginPhase("Asynchronous value pass " + std::to_string(currentRank));
				for (std::size_t source = 0; source < count; ++source)
				{
					r.probeAuxiliary(lane, source, SemanticRole::ProbedItem);
					const SortItem item = r.auxiliaryAt(lane, source);
					if (r.counter(consumedLane, source) == 0 && valueRank(item) <= currentRank)
					{
						r.writeMain(output++, item, SemanticRole::WriteTarget);
						r.writeCounter(consumedLane, source, 1);
					}
				}
				r.endPhase(phase, "Asynchronous value pass " + std::to_string(currentRank));
				++completedPasses;
				probes += count;
				r.setMetric("asynchronous_key_passes", completedPasses);
				r.setMetric("asynchronous_source_probes", probes);
				r.setMetric("asynchronous_emitted_items", output);
			}
			if (output != count) throw std::logic_error("Asynchronous Sort did not emit every input item.");
			r.resizeAuxiliary(lane, 0);
			r.resizeCounters(consumedLane, 0);
		}

		void pdMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("pdmerge_run_count");
			r.declareMetric("pdmerge_run_reversals");
			r.declareMetric("pdmerge_merge_passes");
			r.declareMetric("pdmerge_merges");
			r.declareMetric("pdmerge_buffered_items");
			if (count < 2) return;

			std::vector<std::size_t> runs;
			runs.push_back(0);
			std::size_t start = 0;
			while (start + 1 < count)
			{
				const SortItem first = r.at(start), second = r.at(start + 1);
				r.compare(first, second, start, start + 1);
				const bool forwardRun = compareItems(first, second, r.trace.direction) != ComparisonResult::Greater;
				std::size_t end = start + 2;
				while (end < count)
				{
					const SortItem previous = r.at(end - 1), current = r.at(end);
					r.compare(previous, current, end - 1, end);
					const bool thisForward = compareItems(previous, current, r.trace.direction) != ComparisonResult::Greater;
					if (thisForward != forwardRun) break;
					++end;
				}
				if (!forwardRun)
				{
					const int group = r.beginPhase("Reverse strict descending run");
					for (std::size_t left = start, right = end - 1; left < right; ++left, --right)
						r.swap(left, right, SemanticRole::WriteTarget);
					r.endPhase(group, "Descending run reversed");
					r.addMetric("pdmerge_run_reversals");
				}
				runs.push_back(end);
				r.addMetric("pdmerge_run_count");
				start = end;
			}
			if (runs.back() != count) runs.push_back(count);

			const std::string runLane = "pdmerge_run_boundaries";
			r.resizeCounters(runLane, runs.size());
			for (std::size_t i = 0; i < runs.size(); ++i)
				r.writeCounter(runLane, i, static_cast<int>(runs[i]));
			const std::string bufferLane = "pdmerge_buffer";
			const auto mergePair = [&r, &bufferLane](std::size_t begin, std::size_t middle, std::size_t end)
			{
				const std::size_t leftCount = middle - begin;
				const std::size_t rightCount = end - middle;
				const int group = r.beginPhase("Pattern-defeating merge " + std::to_string(begin) + ".." + std::to_string(end));
				if (leftCount <= rightCount)
				{
					r.resizeAuxiliary(bufferLane, leftCount);
					for (std::size_t i = 0; i < leftCount; ++i) r.writeAuxiliary(bufferLane, i, r.at(begin + i));
					std::size_t left = 0, right = middle, output = begin;
					while (left < leftCount && right < end)
					{
						const SortItem leftItem = r.auxiliaryAt(bufferLane, left);
						const SortItem rightItem = r.at(right);
						r.compare(leftItem, rightItem, left, right, bufferLane, "main", SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(output++, leftItem, SemanticRole::WriteTarget), ++left;
						else
						r.writeMain(output++, rightItem, SemanticRole::WriteTarget), ++right;
					}
					while (left < leftCount) r.writeMain(output++, r.auxiliaryAt(bufferLane, left++), SemanticRole::WriteTarget);
					r.addMetric("pdmerge_buffered_items", leftCount);
				}
				else
				{
					r.resizeAuxiliary(bufferLane, rightCount);
					for (std::size_t i = 0; i < rightCount; ++i) r.writeAuxiliary(bufferLane, i, r.at(middle + i));
					std::size_t left = middle, right = rightCount, output = end;
					while (left > begin && right > 0)
					{
						const SortItem leftItem = r.at(left - 1);
						const SortItem rightItem = r.auxiliaryAt(bufferLane, right - 1);
						r.compare(leftItem, rightItem, left - 1, right - 1, "main", bufferLane, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(--output, rightItem, SemanticRole::WriteTarget), --right;
						else
						r.writeMain(--output, leftItem, SemanticRole::WriteTarget), --left;
					}
					while (right > 0) r.writeMain(--output, r.auxiliaryAt(bufferLane, --right), SemanticRole::WriteTarget);
					r.addMetric("pdmerge_buffered_items", rightCount);
				}
				r.resizeAuxiliary(bufferLane, 0);
				r.endPhase(group, "Merge pair complete");
				r.addMetric("pdmerge_merges");
			};

			std::uint64_t pass = 0;
			while (runs.size() > 2)
			{
				const int group = r.beginPhase("Pairwise natural-run merge pass " + std::to_string(pass + 1));
				std::vector<std::size_t> next;
				next.push_back(0);
				const std::size_t runCount = runs.size() - 1;
				for (std::size_t run = 0; run < runCount; run += 2)
				{
					if (run + 1 < runCount)
						mergePair(runs[run], runs[run + 1], runs[run + 2]);
					next.push_back(runs[(std::min)(run + 2, runCount)]);
				}
				r.endPhase(group, "Pairwise run pass complete");
				++pass;
				r.setMetric("pdmerge_merge_passes", pass);
				runs.swap(next);
				r.resizeCounters(runLane, runs.size());
				for (std::size_t i = 0; i < runs.size(); ++i)
					r.writeCounter(runLane, i, static_cast<int>(runs[i]));
			}
			r.resizeCounters(runLane, 0);
			r.resizeAuxiliary(bufferLane, 0);
		}

		void cycleSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t cycleStart = 0; cycleStart + 1 < n; ++cycleStart)
			{
				SortItem item = r.at(cycleStart);
				r.hold(cycleStart);
				for (;;)
				{
					std::size_t position = cycleStart;
					for (std::size_t i = cycleStart + 1; i < n; ++i)
					{
						const SortItem candidate = r.at(i);
						r.compare(candidate, item, i, cycleStart, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, item, r.trace.direction) == ComparisonResult::Less) ++position;
					}
					if (position == cycleStart)
					{
						r.place(cycleStart, item);
						break;
					}
					while (position < n)
					{
						const SortItem candidate = r.at(position);
						r.compare(candidate, item, position, cycleStart, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, item, r.trace.direction) != ComparisonResult::Equal) break;
						++position;
					}
					if (position >= n)
					{
						r.place(cycleStart, item);
						break;
					}
					r.carryWrite(position);
					item = r.state.heldItem;
				}
			}
		}

		void stableCycleSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("stable_cycle_cycles");
			r.declareMetric("stable_cycle_rank_probes");
			r.declareMetric("stable_cycle_item_moves");
			r.declareMetric("stable_cycle_fixed_positions");
			if (count < 2) return;

			const std::string referenceLane = "stable_cycle_reference";
			const std::string flagLane = "stable_cycle_fixed_bits";
			r.resizeAuxiliary(referenceLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeAuxiliary(referenceLane, i, r.at(i));
			r.resizeCounters(flagLane, (count + 7) / 8);
			std::map<std::uint64_t, std::size_t> originalPosition;
			for (std::size_t i = 0; i < count; ++i) originalPosition[r.trace.input[i].stableId] = i;

			const auto isFixed = [&r, &flagLane](std::size_t index)
			{
				return (r.counter(flagLane, index >> 3) & (1 << (index & 7))) != 0;
			};
			const auto markFixed = [&r, &flagLane](std::size_t index)
			{
				const std::size_t word = index >> 3;
				const int mask = 1 << (index & 7);
				if ((r.counter(flagLane, word) & mask) == 0)
				{
					r.writeCounter(flagLane, word, r.counter(flagLane, word) | mask);
					r.addMetric("stable_cycle_fixed_positions");
				}
			};
			const auto stableRank = [&r, &referenceLane, &originalPosition](const SortItem& item)
			{
				const std::size_t itemOrigin = originalPosition.find(item.stableId)->second;
				std::size_t destination = 0;
				for (std::size_t i = 0; i < r.trace.input.size(); ++i)
				{
					const SortItem candidate = r.auxiliaryAt(referenceLane, i);
					if (candidate.stableId == item.stableId) continue;
					r.compare(candidate, item, i, itemOrigin, referenceLane, "main",
						SemanticRole::ComparedItem, SemanticRole::HeldKey);
					r.addMetric("stable_cycle_rank_probes");
					const ComparisonResult order = compareItems(candidate, item, r.trace.direction);
					const std::size_t candidateOrigin = originalPosition.find(candidate.stableId)->second;
					if (order == ComparisonResult::Less || (order == ComparisonResult::Equal && candidateOrigin < itemOrigin)) ++destination;
				}
				return destination;
			};

			for (std::size_t cycleStart = 0; cycleStart < count; ++cycleStart)
			{
				if (isFixed(cycleStart)) continue;
				r.addMetric("stable_cycle_cycles");
				SortItem held = r.at(cycleStart);
				r.hold(cycleStart);
				for (;;)
				{
					const std::size_t destination = stableRank(held);
					if (destination >= count) throw std::logic_error("Stable Cycle Sort computed an out-of-range destination.");
					if (destination == cycleStart)
					{
						r.place(cycleStart, held);
						markFixed(cycleStart);
						break;
					}
					if (isFixed(destination)) throw std::logic_error("Stable Cycle Sort targeted a previously fixed destination.");
					r.carryWrite(destination);
					held = r.state.heldItem;
					markFixed(destination);
					r.addMetric("stable_cycle_item_moves");
				}
			}
			r.resizeAuxiliary(referenceLane, 0);
			r.resizeCounters(flagLane, 0);
		}

		void classicTournamentSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("classic_tournament_tree_comparisons");
			r.declareMetric("classic_tournament_tree_updates");
			r.declareMetric("classic_tournament_extractions");
			r.declareMetric("classic_tournament_output_writes");
			r.declareMetric("classic_tournament_padding_leaves");
			if (count < 2) return;

			std::size_t leafCount = 1;
			while (leafCount < count) { leafCount *= 2; r.chargeWork(); }
			const std::string treeLane = "classic_tournament_winners";
			const std::string outputLane = "classic_tournament_output";
			r.resizeCounters(treeLane, leafCount * 2);
			r.resizeAuxiliary(outputLane, count);
			const auto chooseWinner = [&r](int first, int second)
			{
				if (first < 0) return second;
				if (second < 0) return first;
				const SortItem a = r.at(static_cast<std::size_t>(first));
				const SortItem b = r.at(static_cast<std::size_t>(second));
				r.compare(a, b, static_cast<std::size_t>(first), static_cast<std::size_t>(second));
				r.addMetric("classic_tournament_tree_comparisons");
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return order == ComparisonResult::Less || (order == ComparisonResult::Equal && first < second) ? first : second;
			};

			int buildGroup = r.beginPhase("Build padded winner tree");
			for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
			{
				r.writeCounter(treeLane, leafCount + leaf, leaf < count ? static_cast<int>(leaf) : -1);
				if (leaf >= count) r.addMetric("classic_tournament_padding_leaves");
			}
			for (std::size_t node = leafCount; node-- > 1;)
			{
				const int winner = chooseWinner(r.counter(treeLane, node * 2), r.counter(treeLane, node * 2 + 1));
				r.writeCounter(treeLane, node, winner);
			}
			r.endPhase(buildGroup, "Winner tree ready");

			for (std::size_t output = 0; output < count; ++output)
			{
				const int group = r.beginPhase("Extract tournament winner " + std::to_string(output));
				const int winner = r.counter(treeLane, 1);
				if (winner < 0) throw std::logic_error("Classic Tournament Sort exhausted its winner tree early.");
				r.writeAuxiliary(outputLane, output, r.at(static_cast<std::size_t>(winner)));
				r.addMetric("classic_tournament_output_writes");
				r.writeCounter(treeLane, leafCount + static_cast<std::size_t>(winner), -1);
				for (std::size_t node = (leafCount + static_cast<std::size_t>(winner)) / 2; node > 0; node /= 2)
				{
					const int nextWinner = chooseWinner(r.counter(treeLane, node * 2), r.counter(treeLane, node * 2 + 1));
					r.writeCounter(treeLane, node, nextWinner);
					r.addMetric("classic_tournament_tree_updates");
				}
				r.endPhase(group, "Winner extracted and path rebuilt");
				r.addMetric("classic_tournament_extractions");
			}
			for (std::size_t i = 0; i < count; ++i)
				r.writeMain(i, r.auxiliaryAt(outputLane, i), SemanticRole::WriteTarget);
			r.resizeAuxiliary(outputLane, 0);
			r.resizeCounters(treeLane, 0);
		}

		void tournamentSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("tournament_matches");
			r.declareMetric("tournament_loser_replays");
			r.declareMetric("tournament_tree_updates");
			r.declareMetric("tournament_outputs");
			r.declareMetric("tournament_padding_leaves");
			if (count < 2) return;

			std::size_t leafCount = 1;
			while (leafCount < count) { leafCount *= 2; r.chargeWork(); }
			const std::string winnerLane = "tournament_winners";
			const std::string loserLane = "tournament_losers";
			const std::string outputLane = "tournament_output";
			r.resizeCounters(winnerLane, leafCount * 2);
			r.resizeCounters(loserLane, leafCount);
			r.resizeAuxiliary(outputLane, count);
			for (std::size_t i = 0; i < leafCount; ++i)
				r.writeCounter(loserLane, i, -1);
			for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
			{
				r.writeCounter(winnerLane, leafCount + leaf, leaf < count ? static_cast<int>(leaf) : -1);
				if (leaf >= count) r.addMetric("tournament_padding_leaves");
			}

			const auto comparePlayers = [&r](int first, int second, int& winner, int& loser)
			{
				if (first < 0) { winner = second; loser = -1; return; }
				if (second < 0) { winner = first; loser = -1; return; }
				const SortItem a = r.at(static_cast<std::size_t>(first));
				const SortItem b = r.at(static_cast<std::size_t>(second));
				r.compare(a, b, static_cast<std::size_t>(first), static_cast<std::size_t>(second));
				r.addMetric("tournament_matches");
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				const bool firstWins = order == ComparisonResult::Less || (order == ComparisonResult::Equal && first < second);
				winner = firstWins ? first : second;
				loser = firstWins ? second : first;
			};
			int buildGroup = r.beginPhase("Build winner-loser tournament");
			for (std::size_t node = leafCount; node-- > 1;)
			{
				int winner = -1, loser = -1;
				comparePlayers(r.counter(winnerLane, node * 2), r.counter(winnerLane, node * 2 + 1), winner, loser);
				r.writeCounter(winnerLane, node, static_cast<int>(winner));
				r.writeCounter(loserLane, node, static_cast<int>(loser));
			}
			r.endPhase(buildGroup, "Winner-loser tree ready");

			for (std::size_t output = 0; output < count; ++output)
			{
				const int group = r.beginPhase("Replay loser path " + std::to_string(output));
				const int winner = r.counter(winnerLane, 1);
				if (winner < 0) throw std::logic_error("Tournament Sort exhausted its winner tree early.");
				r.writeAuxiliary(outputLane, output, r.at(static_cast<std::size_t>(winner)));
				r.addMetric("tournament_outputs");
				std::size_t node = leafCount + static_cast<std::size_t>(winner);
				r.writeCounter(winnerLane, node, -1);
				int candidate = -1;
				for (node /= 2; node > 0; node /= 2)
				{
					const int priorLoser = r.counter(loserLane, node);
					int nextWinner = -1, nextLoser = -1;
					if (candidate < 0) nextWinner = priorLoser;
					else if (priorLoser < 0) nextWinner = candidate;
					else
					{
						int selectedWinner = -1, selectedLoser = -1;
						comparePlayers(candidate, priorLoser, selectedWinner, selectedLoser);
						nextWinner = selectedWinner;
						nextLoser = selectedLoser;
					}
					r.writeCounter(loserLane, node, nextLoser);
					r.writeCounter(winnerLane, node, nextWinner);
					candidate = nextWinner;
					r.addMetric("tournament_loser_replays");
					r.addMetric("tournament_tree_updates");
				}
				r.endPhase(group, "Loser path replayed");
			}
			for (std::size_t i = 0; i < count; ++i)
				r.writeMain(i, r.auxiliaryAt(outputLane, i), SemanticRole::WriteTarget);
			r.resizeAuxiliary(outputLane, 0);
			r.resizeCounters(winnerLane, 0);
			r.resizeCounters(loserLane, 0);
		}

		void smoothSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "smooth_sift_calls", "smooth_trinkle_calls", "smooth_heap_comparisons",
				"smooth_heap_moves", "smooth_heap_insertions", "smooth_heap_extractions", "smooth_max_leonardo_order" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;

			std::vector<std::size_t> leonardo(2, 1);
			while (leonardo.back() < count)
			{
				r.chargeWork();
				leonardo.push_back(leonardo[leonardo.size() - 1] + leonardo[leonardo.size() - 2] + 1);
			}
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto compareItemsAt = [&r](const SortItem& first, const SortItem& second, std::size_t firstIndex, std::size_t secondIndex)
			{
				r.compare(first, second, firstIndex, secondIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric("smooth_heap_comparisons");
			};
			const auto sift = [&r, &later, &compareItemsAt, &leonardo](std::size_t order, std::size_t root)
			{
				r.addMetric("smooth_sift_calls");
				const SortItem held = r.at(root);
				r.hold(root);
				while (order > 1)
				{
					r.chargeWork();
					const std::size_t right = root - 1;
					const std::size_t left = root - 1 - leonardo[order - 2];
					compareItemsAt(held, r.at(left), root, left);
					const bool heldAboveLeft = !later(r.at(left), held);
					compareItemsAt(held, r.at(right), root, right);
					const bool heldAboveRight = !later(r.at(right), held);
					if (heldAboveLeft && heldAboveRight) break;
					compareItemsAt(r.at(left), r.at(right), left, right);
					if (!later(r.at(right), r.at(left)))
					{
						r.shift(left, root);
						root = left;
						--order;
					}
					else
					{
						r.shift(right, root);
						root = right;
						order -= 2;
					}
					r.addMetric("smooth_heap_moves");
				}
				r.place(root, held);
			};
			const auto trinkle = [&r, &later, &compareItemsAt, &leonardo, &sift](std::size_t forest, std::size_t order,
				std::size_t root, bool trusty)
			{
				r.addMetric("smooth_trinkle_calls");
				const SortItem held = r.at(root);
				r.hold(root);
				while (forest != 1)
				{
					r.chargeWork();
					const std::size_t ancestor = root - leonardo[order];
					compareItemsAt(r.at(ancestor), held, ancestor, root);
					if (!later(r.at(ancestor), held)) break;
					if (!trusty && order > 1)
					{
						const std::size_t right = root - 1;
						const std::size_t left = root - 1 - leonardo[order - 2];
						compareItemsAt(r.at(right), r.at(ancestor), right, ancestor);
						const bool rightBlocks = later(r.at(right), r.at(ancestor));
						compareItemsAt(r.at(left), r.at(ancestor), left, ancestor);
						const bool leftBlocks = later(r.at(left), r.at(ancestor));
						if (rightBlocks || leftBlocks) break;
					}
					r.shift(ancestor, root);
					root = ancestor;
					std::size_t trailing = 0;
					unsigned int bits = static_cast<unsigned int>(forest & ~1u);
					while ((bits & 1u) == 0u) { ++trailing; bits >>= 1; r.chargeWork(); }
					forest >>= trailing;
					order += trailing;
					trusty = false;
				r.addMetric("smooth_heap_moves");
				}
				r.place(root, held);
				if (!trusty) sift(order, root);
			};

			int buildPhase = r.beginPhase("Build Leonardo heap forest");
			std::size_t root = 0;
			std::size_t forest = 1;
			std::size_t order = 1;
			while (root < count - 1)
			{
				r.chargeWork();
				if ((forest & 3u) == 3u)
				{
					sift(order, root);
					forest >>= 2;
					order += 2;
				}
				else
				{
					if (order == 0 || order >= leonardo.size()) throw std::logic_error("Smooth Sort generated an invalid Leonardo order.");
					if (leonardo[order - 1] >= count - 1 - root)
						trinkle(forest, order, root, false);
					else sift(order, root);
					if (order == 1) { forest <<= 1; --order; }
					else { forest <<= (order - 1); order = 1; }
				}
				forest |= 1u;
				++root;
				r.addMetric("smooth_heap_insertions");
				r.setMetric("smooth_max_leonardo_order", std::max<std::uint64_t>(r.state.metrics.algorithm["smooth_max_leonardo_order"], order));
			}
			r.endPhase(buildPhase, "Leonardo heap forest built");

			int extractionPhase = r.beginPhase("Drain Leonardo heap forest");
			trinkle(forest, order, root, false);
			while (order != 1 || forest != 1)
			{
				r.chargeWork();
				if (order <= 1)
				{
					std::size_t trailing = 0;
					unsigned int bits = static_cast<unsigned int>(forest & ~1u);
					while ((bits & 1u) == 0u) { ++trailing; bits >>= 1; r.chargeWork(); }
					forest >>= trailing;
					order += trailing;
				}
				else
				{
					forest = (forest << 2) ^ 7u;
					order -= 2;
					if (order + 1 >= leonardo.size() || leonardo[order] + 1 > root)
						throw std::logic_error("Smooth Sort split exceeded its Leonardo forest bounds.");
					trinkle(forest >> 1, order + 1, root - leonardo[order] - 1, true);
					trinkle(forest, order, root - 1, true);
				}
				--root;
				r.addMetric("smooth_heap_extractions");
			}
			r.endPhase(extractionPhase, "Leonardo heap forest drained");
		}

		void binomialSmoothSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "binomial_smooth_thrift_calls", "binomial_smooth_comparisons",
				"binomial_smooth_swaps", "binomial_smooth_insertions", "binomial_smooth_heap_visits", "binomial_smooth_max_height" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto heapHeight = [](std::size_t node)
			{
				std::size_t height = 0;
				while ((node >> height) & 1u) ++height;
				return height;
			};
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			std::function<void(std::size_t, bool, bool)> thrift;
			thrift = [&r, &heapHeight, &later, &thrift](std::size_t node, bool hasParent, bool isHeapRoot)
			{
				r.chargeWork();
				r.addMetric("binomial_smooth_thrift_calls");
				const std::size_t height = heapHeight(node);
				isHeapRoot = isHeapRoot && node >= (std::size_t(1) << height);
				if (!isHeapRoot && !hasParent) return;
				std::size_t choice = height - (isHeapRoot ? 0u : 1u);
				if (hasParent)
				{
					for (std::ptrdiff_t child = static_cast<std::ptrdiff_t>(choice) - 1; child >= 0; --child)
					{
						const std::size_t first = node - (std::size_t(1) << choice);
						const std::size_t second = node - (std::size_t(1) << static_cast<std::size_t>(child));
						r.compare(r.at(first), r.at(second), first, second);
						r.addMetric("binomial_smooth_comparisons");
						if (!later(r.at(first), r.at(second))) choice = static_cast<std::size_t>(child);
					}
				}
				const std::size_t child = node - (std::size_t(1) << choice);
				r.compare(r.at(child), r.at(node), child, node);
				r.addMetric("binomial_smooth_comparisons");
				r.setMetric("binomial_smooth_max_height", std::max<std::uint64_t>(r.state.metrics.algorithm["binomial_smooth_max_height"], height));
				if (!later(r.at(child), r.at(node))) return;
				r.swap(node, child);
				r.addMetric("binomial_smooth_swaps");
				r.addMetric("binomial_smooth_heap_visits");
				thrift(child, (child & 1u) == 1u, choice == height);
			};

			int buildPhase = r.beginPhase("Grow binomial heap forest");
			for (std::size_t node = 1; node < count; ++node)
			{
				const std::size_t height = heapHeight(node);
				r.setMetric("binomial_smooth_max_height", std::max<std::uint64_t>(r.state.metrics.algorithm["binomial_smooth_max_height"], height));
				thrift(node, (node & 1u) == 1u, node + (std::size_t(1) << height) >= count);
				r.addMetric("binomial_smooth_insertions");
			}
			r.endPhase(buildPhase, "Binomial heap forest built");
			int extractPhase = r.beginPhase("Reheapify binomial forest");
			std::size_t node = count;
			node -= (node - 1) % 2;
			while (node > 2)
			{
				const std::size_t height = heapHeight(node);
				for (std::ptrdiff_t childOrder = static_cast<std::ptrdiff_t>(height) - 1; childOrder >= 0; --childOrder)
				{
					const std::size_t child = node - (std::size_t(1) << static_cast<std::size_t>(childOrder));
					thrift(child, false, true);
					r.addMetric("binomial_smooth_heap_visits");
				}
				node -= 2;
			}
			r.endPhase(extractPhase, "Binomial forest reheapified");
		}

		void improvedBlockSelectionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "block_selection_runs", "block_selection_blocks_selected", "block_selection_block_swaps",
				"block_selection_rotations", "block_selection_merge_comparisons", "block_selection_forward_merges",
				"block_selection_backward_merges", "block_selection_max_block_length" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				r.compare(r.at(first), r.at(second), first, second);
				r.addMetric("block_selection_merge_comparisons");
			};
			const auto ceilingRoot = [&r](std::size_t value)
			{
				std::size_t root = 1;
				while (root * root < value) { root *= 2; r.chargeWork(); }
				return root;
			};
			const auto multiSwap = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t i = 0; i < length; ++i)
				{
					r.swap(first + i, second + i);
					r.addMetric("block_selection_block_swaps");
				}
			};
			const auto rotate = [&r, &multiSwap](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::size_t left = middle - first, right = end - middle;
				while (left > 0 && right > 0)
				{
					r.chargeWork();
					if (right < left)
					{
						multiSwap(middle - right, middle, right);
						end -= right; middle -= right; left -= right;
					}
					else
					{
						multiSwap(first, middle, left);
						first += left; middle += left; right -= left;
					}
				}
				r.addMetric("block_selection_rotations");
			};
			const auto mergeForward = [&r, &compareAt, &later, &rotate](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::size_t left = first, right = middle;
				while (left < right && right < end)
				{
					compareAt(left, right);
					if (later(r.at(left), r.at(right)))
					{
						std::size_t runEnd = right + 1;
						while (runEnd < end)
						{
							compareAt(left, runEnd);
							if (!later(r.at(left), r.at(runEnd))) break;
						++runEnd;
						}
						const std::size_t moved = runEnd - right;
						rotate(left, right, runEnd);
						left += moved; right = runEnd;
					}
					else ++left;
				}
				r.addMetric("block_selection_forward_merges");
				return left;
			};
			const auto mergeBackward = [&r, &compareAt, &later, &rotate](std::size_t first, std::size_t middle, std::size_t end)
			{
				std::ptrdiff_t left = static_cast<std::ptrdiff_t>(middle) - 1;
				std::ptrdiff_t right = static_cast<std::ptrdiff_t>(end) - 1;
				while (right > left && left >= static_cast<std::ptrdiff_t>(first))
				{
					compareAt(static_cast<std::size_t>(left), static_cast<std::size_t>(right));
					if (later(r.at(static_cast<std::size_t>(left)), r.at(static_cast<std::size_t>(right))))
					{
						std::ptrdiff_t runStart = left;
						while (runStart > static_cast<std::ptrdiff_t>(first))
						{
							compareAt(static_cast<std::size_t>(runStart - 1), static_cast<std::size_t>(right));
							if (!later(r.at(static_cast<std::size_t>(runStart - 1)), r.at(static_cast<std::size_t>(right)))) break;
							--runStart;
						}
						const std::size_t moved = static_cast<std::size_t>(left - runStart + 1);
						rotate(static_cast<std::size_t>(runStart), static_cast<std::size_t>(left + 1), static_cast<std::size_t>(right + 1));
						right -= static_cast<std::ptrdiff_t>(moved); left = runStart - 1;
					}
					else --right;
				}
				r.addMetric("block_selection_backward_merges");
			};
			const auto selectRange = [&r, &compareAt, &later](std::size_t first, std::size_t end, std::size_t blockLength)
			{
				std::size_t minimum = first;
				for (std::size_t candidate = first + blockLength; candidate < end; candidate += blockLength)
				{
					compareAt(candidate, minimum);
					if (later(r.at(minimum), r.at(candidate)) ||
						(compareItems(r.at(candidate), r.at(minimum), r.trace.direction) == ComparisonResult::Equal &&
						 later(r.at(minimum + blockLength - 1), r.at(candidate + blockLength - 1))))
					{
						if (compareItems(r.at(candidate), r.at(minimum), r.trace.direction) == ComparisonResult::Equal)
							compareAt(minimum + blockLength - 1, candidate + blockLength - 1);
						minimum = candidate;
					}
				}
				return minimum;
			};
			const auto blockSelect = [&r, &compareAt, &later, &multiSwap, &selectRange](std::size_t first, std::size_t middle,
				std::size_t end, std::size_t blockLength)
			{
				std::size_t target = first, selected = middle, candidate = middle;
				while (target < middle)
				{
					compareAt(target, middle);
					if (later(r.at(target), r.at(middle))) break;
					target += blockLength;
				}
				if (target == middle) return;
				multiSwap(target, selected, blockLength);
				target += blockLength; selected += blockLength;
				while (target < selected && selected < end)
				{
					compareAt(candidate, selected);
					if (!later(r.at(candidate), r.at(selected)))
					{
						if (target != candidate) multiSwap(target, candidate, blockLength);
						target += blockLength;
						candidate = selectRange(std::max(middle, target), selected, blockLength);
					}
					else
					{
						if (candidate == target) candidate = selected;
						if (target != selected) multiSwap(target, selected, blockLength);
						target += blockLength; selected += blockLength;
					}
				}
				while (target < selected)
				{
					candidate = selectRange(target, end, blockLength);
					if (target != candidate) multiSwap(target, candidate, blockLength);
					target += blockLength;
				}
				r.addMetric("block_selection_blocks_selected");
			};

		for (std::size_t run = 1; run < count; run *= 2)
		{
			r.chargeWork();
			const int phase = r.beginPhase("Block-selection merge run " + std::to_string(run));
			std::size_t blockLength = ceilingRoot(run), blockRun = run;
			const std::size_t boundary = count - count % blockLength;
			r.setMetric("block_selection_max_block_length", std::max<std::uint64_t>(r.state.metrics.algorithm["block_selection_max_block_length"], blockLength));
			while (blockRun > 16)
			{
				for (std::size_t base = 0; base + run < boundary; base += 2 * run)
					for (std::size_t block = base; block + blockRun < std::min(base + 2 * run, boundary); block += blockRun)
						blockSelect(block, block + blockRun, std::min(block + 2 * blockRun, boundary), blockLength);
				blockRun = blockLength;
				blockLength = ceilingRoot(blockLength);
				r.setMetric("block_selection_max_block_length", std::max<std::uint64_t>(r.state.metrics.algorithm["block_selection_max_block_length"], blockLength));
			}
			for (std::size_t base = 0; base + run < boundary; base += 2 * run)
			{
				std::size_t finish = base;
				for (std::size_t block = base; block + blockRun < std::min(base + 2 * run, boundary); block += blockRun)
					finish = mergeForward(finish, block + blockRun, std::min(block + 2 * blockRun, boundary));
			}
			const std::size_t tailStart = count - count % (2 * run);
			mergeBackward(tailStart, boundary, count);
			r.endPhase(phase, "Block-selection merge run complete");
			r.addMetric("block_selection_runs");
		}
		}

		void stacklessHybridQuickSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "stackless_hybrid_maximum_scans", "stackless_hybrid_maximum_items_parked",
				"stackless_hybrid_partitions", "stackless_hybrid_partition_comparisons", "stackless_hybrid_insertion_runs",
				"stackless_hybrid_duplicate_skips", "stackless_hybrid_max_active_range" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto less = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Less; };
			std::size_t maximum = 0;
			for (std::size_t i = 1; i < count; ++i)
			{
				r.compare(r.at(i), r.at(maximum), i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				r.addMetric("stackless_hybrid_maximum_scans");
				if (later(r.at(i), r.at(maximum))) maximum = i;
			}
			const SortItem maximumItem = r.at(maximum);
			std::size_t end = count;
			for (std::size_t cursor = count; cursor > 0; --cursor)
			{
				const std::size_t index = cursor - 1;
				r.compare(r.at(index), maximumItem, index, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(r.at(index), maximumItem, r.trace.direction) == ComparisonResult::Equal)
				{
					--end;
					r.swap(index, end, SemanticRole::CurrentMaximum);
					r.addMetric("stackless_hybrid_maximum_items_parked");
				}
			}

			const auto medianOfThree = [&r, &later](std::size_t first, std::size_t rangeEnd)
			{
				const std::size_t middle = first + (rangeEnd - 1 - first) / 2;
				const std::size_t last = rangeEnd - 1;
				r.compare(r.at(first), r.at(middle), first, middle);
				if (later(r.at(first), r.at(middle))) r.swap(first, middle, SemanticRole::Pivot);
				r.compare(r.at(middle), r.at(last), middle, last);
				if (later(r.at(middle), r.at(last)))
				{
					r.swap(middle, last, SemanticRole::Pivot);
					r.compare(r.at(first), r.at(middle), first, middle);
					if (later(r.at(first), r.at(middle))) return;
				}
				r.swap(first, middle, SemanticRole::Pivot);
			};
			const auto partition = [&r, &less](std::size_t first, std::size_t rangeEnd)
			{
				std::ptrdiff_t scan = static_cast<std::ptrdiff_t>(first);
				std::ptrdiff_t tail = static_cast<std::ptrdiff_t>(rangeEnd);
				for (;;)
				{
					++scan;
					while (scan < tail)
					{
						r.compare(r.at(static_cast<std::size_t>(scan)), r.at(first), static_cast<std::size_t>(scan), first,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("stackless_hybrid_partition_comparisons");
						if (!less(r.at(static_cast<std::size_t>(scan)), r.at(first))) break;
						++scan;
					}
					--tail;
					while (tail >= scan)
					{
						r.compare(r.at(static_cast<std::size_t>(tail)), r.at(first), static_cast<std::size_t>(tail), first,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("stackless_hybrid_partition_comparisons");
						if (less(r.at(static_cast<std::size_t>(tail)), r.at(first))) break;
						--tail;
					}
					if (scan < tail) r.swap(static_cast<std::size_t>(scan), static_cast<std::size_t>(tail));
					else
					{
						r.swap(first, static_cast<std::size_t>(tail), SemanticRole::Pivot);
						return static_cast<std::size_t>(tail);
					}
				}
			};
			const auto insertionRange = [&r](std::size_t first, std::size_t rangeEnd)
			{
				for (std::size_t i = first + (first < rangeEnd ? 1u : 0u); i < rangeEnd; ++i)
				{
					const SortItem key = r.at(i);
					r.hold(i);
					std::size_t low = first, high = i;
					while (low < high)
					{
						const std::size_t middle = low + (high - low) / 2;
						const SortItem candidate = r.at(middle);
						r.compare(key, candidate, i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
						if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
						else low = middle + 1;
					}
					for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
					r.place(low, key);
				}
			};
			const auto lowerBound = [&r](std::size_t first, std::size_t rangeEnd, std::size_t pivotIndex)
			{
				const SortItem pivot = r.at(pivotIndex);
				std::size_t low = first, high = rangeEnd;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					r.compare(pivot, r.at(middle), pivotIndex, middle, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					if (compareItems(pivot, r.at(middle), r.trace.direction) != ComparisonResult::Greater) high = middle;
					else low = middle + 1;
				}
				return low;
			};

			std::size_t first = 0, partitionEnd = end;
			bool selectMedian = true;
			for (;;)
			{
				r.setMetric("stackless_hybrid_max_active_range", std::max<std::uint64_t>(r.state.metrics.algorithm["stackless_hybrid_max_active_range"], partitionEnd - first));
				while (partitionEnd - first > 16)
				{
					if (selectMedian) medianOfThree(first, partitionEnd);
					const int phase = r.beginPhase("Stackless Hoare partition");
					const std::size_t pivot = partition(first, partitionEnd);
					r.swap(pivot, end, SemanticRole::Pivot);
					partitionEnd = pivot;
					r.endPhase(phase, "Pivot parked beside the maximum suffix");
					r.addMetric("stackless_hybrid_partitions");
				}
				insertionRange(first, partitionEnd);
				r.addMetric("stackless_hybrid_insertion_runs");
				first = partitionEnd + 1;
				if (first >= end)
				{
					if (first - 1 < end) r.swap(first - 1, end, SemanticRole::Pivot);
					return;
				}
				partitionEnd = lowerBound(first, end, first - 1);
				r.swap(first - 1, end, SemanticRole::Pivot);
				selectMedian = true;
				while (first < partitionEnd)
				{
					r.compare(r.at(first - 1), r.at(first), first - 1, first, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					if (compareItems(r.at(first - 1), r.at(first), r.trace.direction) != ComparisonResult::Equal) break;
					selectMedian = false;
					++first;
					r.addMetric("stackless_hybrid_duplicate_skips");
				}
				if (first == partitionEnd) selectMedian = true;
			}
		}

		void stacklessDualPivotQuickSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "stackless_dual_maximum_scans", "stackless_dual_maximum_items_parked",
				"stackless_dual_partitions", "stackless_dual_partition_comparisons", "stackless_dual_insertion_runs",
				"stackless_dual_duplicate_skips", "stackless_dual_max_active_range" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto less = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Less; };
			std::size_t maximum = 0;
			for (std::size_t i = 1; i < count; ++i)
			{
				r.compare(r.at(i), r.at(maximum), i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				r.addMetric("stackless_dual_maximum_scans");
				if (later(r.at(i), r.at(maximum))) maximum = i;
			}
			const SortItem maximumItem = r.at(maximum);
			std::size_t boundary = count;
			for (std::size_t cursor = count; cursor > 0; --cursor)
			{
				const std::size_t index = cursor - 1;
				r.compare(r.at(index), maximumItem, index, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(r.at(index), maximumItem, r.trace.direction) == ComparisonResult::Equal)
				{
					--boundary;
					r.swap(index, boundary, SemanticRole::CurrentMaximum);
					r.addMetric("stackless_dual_maximum_items_parked");
				}
			}
			const auto insertionRange = [&r](std::size_t first, std::size_t end)
			{
				for (std::size_t i = first + (first < end ? 1u : 0u); i < end; ++i)
				{
					const SortItem key = r.at(i);
					r.hold(i);
					std::size_t low = first, high = i;
					while (low < high)
					{
						const std::size_t middle = low + (high - low) / 2;
						r.compare(key, r.at(middle), i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
						if (compareItems(key, r.at(middle), r.trace.direction) == ComparisonResult::Less) high = middle;
						else low = middle + 1;
					}
					for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
					r.place(low, key);
				}
			};
			const auto lowerBound = [&r](std::size_t first, std::size_t end, std::size_t pivotIndex)
			{
				const SortItem pivot = r.at(pivotIndex);
				std::size_t low = first, high = end;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					r.compare(pivot, r.at(middle), pivotIndex, middle, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					if (compareItems(pivot, r.at(middle), r.trace.direction) != ComparisonResult::Greater) high = middle;
					else low = middle + 1;
				}
				return low;
			};
			const auto partition = [&r, &less, &later](std::size_t first, std::size_t rangeEnd, std::size_t maximumBoundary)
			{
				std::size_t firstSample = first + (rangeEnd - first) / 3;
				std::size_t secondSample = first + 2 * (rangeEnd - first) / 3;
				std::size_t lastPivot = rangeEnd;
				r.compare(r.at(firstSample), r.at(secondSample), firstSample, secondSample);
				if (later(r.at(firstSample), r.at(secondSample)))
				{
					r.swap(firstSample, first, SemanticRole::Pivot);
					r.swap(secondSample, --lastPivot, SemanticRole::Pivot);
				}
				else
				{
					r.swap(secondSample, first, SemanticRole::Pivot);
					r.swap(firstSample, --lastPivot, SemanticRole::Pivot);
				}
				std::size_t left = first, right = lastPivot;
				for (std::size_t scan = left + 1; scan < right; ++scan)
				{
					r.compare(r.at(scan), r.at(lastPivot), scan, lastPivot, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("stackless_dual_partition_comparisons");
					if (less(r.at(scan), r.at(lastPivot)))
					{
						r.swap(scan, ++left);
						continue;
					}
					r.compare(r.at(scan), r.at(first), scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("stackless_dual_partition_comparisons");
					if (!less(r.at(scan), r.at(first)))
					{
						do
						{
							--right;
							if (right <= scan) break;
							r.compare(r.at(right), r.at(first), right, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							r.addMetric("stackless_dual_partition_comparisons");
						}
						while (!less(r.at(right), r.at(first)));
						r.swap(scan, right);
						r.compare(r.at(scan), r.at(lastPivot), scan, lastPivot, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("stackless_dual_partition_comparisons");
						if (less(r.at(scan), r.at(lastPivot))) r.swap(scan, ++left);
					}
				}
				r.swap(first, left, SemanticRole::Pivot);
				r.swap(maximumBoundary, lastPivot, SemanticRole::Pivot);
				r.swap(lastPivot, right);
				return left;
			};

			std::size_t first = 0, partitionEnd = boundary;
			bool medianMode = true;
			for (;;)
			{
				r.setMetric("stackless_dual_max_active_range", std::max<std::uint64_t>(r.state.metrics.algorithm["stackless_dual_max_active_range"], partitionEnd - first));
				while (partitionEnd - first > 24)
				{
					if (!medianMode) r.swap(first, first + (partitionEnd - first) / 3, SemanticRole::Pivot);
					const int phase = r.beginPhase("Stackless dual-pivot partition");
					const std::size_t pivot = partition(first, partitionEnd, boundary);
					partitionEnd = pivot;
					r.endPhase(phase, "Dual-pivot partition complete");
					r.addMetric("stackless_dual_partitions");
				}
				insertionRange(first, partitionEnd);
				r.addMetric("stackless_dual_insertion_runs");
				first = partitionEnd + 1;
				if (first >= boundary)
				{
					if (first - 1 < boundary) r.swap(first - 1, boundary, SemanticRole::Pivot);
					return;
				}
				partitionEnd = lowerBound(first, boundary, first - 1);
				r.swap(first - 1, boundary, SemanticRole::Pivot);
				medianMode = true;
				while (first < partitionEnd)
				{
					r.compare(r.at(first - 1), r.at(first), first - 1, first, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
					if (compareItems(r.at(first - 1), r.at(first), r.trace.direction) != ComparisonResult::Equal) break;
					medianMode = false;
					++first;
					r.addMetric("stackless_dual_duplicate_skips");
				}
				if (first == partitionEnd) medianMode = true;
			}
		}

		void hanoiSort(Recorder& r)
		{
			const std::size_t length = r.state.slots.size();
			const char* metricKeys[] = { "hanoi_item_transfers", "hanoi_duplicate_batches", "hanoi_comparisons", "hanoi_tower_moves" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (length < 2) return;

			// ArrayV's version moves a run of equal keys together, then runs an
			// iterative three-peg Hanoi schedule to insert each next key into the
			// ordered tower. The app keeps the same move schedule but represents the
			// pegs as reversible item lanes and commits the returned tower once.
			std::vector<SortItem> array;
			array.reserve(length);
			for (std::size_t i = 0; i < length; ++i) array.push_back(r.at(i));
			r.resizeAuxiliary("hanoi-main-stack", length);
			for (std::size_t i = 0; i < length; ++i) r.writeAuxiliary("hanoi-main-stack", i, array[i]);
			std::vector<SortItem> stack2, stack3;
			std::size_t sp = 0, unsorted = 0, targetIndex = 0;
			SortItem target;
			std::uint64_t targetMoves = 0;
			const auto pegLane = [](int peg) { return peg == 2 ? std::string("hanoi-peg-2") : std::string("hanoi-peg-3"); };
			const auto push = [&r](std::vector<SortItem>& stack, const std::string& lane, const SortItem& item)
			{
				const std::size_t index = stack.size();
				stack.push_back(item);
				r.resizeAuxiliary(lane, stack.size());
				r.writeAuxiliary(lane, index, item);
				r.addMetric("hanoi_item_transfers");
			};
			const auto pop = [&r](std::vector<SortItem>& stack, const std::string& lane)
			{
				require(!stack.empty(), "Hanoi peg pop requires an item.");
				const SortItem item = stack.back();
				stack.pop_back();
				r.resizeAuxiliary(lane, stack.size());
				r.addMetric("hanoi_item_transfers");
				return item;
			};
			const auto compareMainToPeg = [&r, &pegLane](std::size_t mainIndex, const SortItem& item,
				int peg, const SortItem& top, std::size_t topIndex, SemanticRole role)
			{
				r.compare(item, top, mainIndex, topIndex, "hanoi-main-stack", pegLane(peg), role, SemanticRole::ComparedItem);
				r.addMetric("hanoi_comparisons");
			};
			const auto moveFromMain = [&r, &array, &sp, &unsorted, &push, &compareMainToPeg, &pegLane]
				(std::vector<SortItem>& stack, int peg, bool checkUnsorted)
			{
				// The source always transfers the first item; checkUnsorted limits
				// only the following equal-key batch, after sp advances.
				if (sp >= array.size()) return std::size_t(0);
				std::size_t duplicates = 1;
				const SortItem first = array[sp];
				push(stack, pegLane(peg), first);
				++sp;
				bool ended = sp >= array.size() || (checkUnsorted && sp >= unsorted);
				while (!ended)
				{
					r.chargeWork();
					compareMainToPeg(sp, array[sp], peg, stack.back(), stack.size() - 1, SemanticRole::ComparedItem);
					if (compareItems(array[sp], stack.back(), r.trace.direction) != ComparisonResult::Equal) break;
					push(stack, pegLane(peg), array[sp]);
					++sp;
					++duplicates;
					ended = sp >= array.size() || (checkUnsorted && sp >= unsorted);
				}
				if (duplicates > 1) r.addMetric("hanoi_duplicate_batches");
				return duplicates;
			};
			const auto moveToMain = [&r, &array, &sp, &pop, &compareMainToPeg, &pegLane]
				(std::vector<SortItem>& stack, int peg)
			{
				require(sp > 0 && !stack.empty(), "Hanoi return move exceeds the main stack or an empty peg.");
				--sp;
				array[sp] = pop(stack, pegLane(peg));
				r.writeAuxiliary("hanoi-main-stack", sp, array[sp]);
				while (!stack.empty())
				{
					r.chargeWork();
					compareMainToPeg(sp, array[sp], peg, stack.back(), stack.size() - 1, SemanticRole::ComparedItem);
					if (compareItems(stack.back(), array[sp], r.trace.direction) != ComparisonResult::Equal) break;
					require(sp > 0, "Hanoi equal-key batch moved before the main stack start.");
					--sp;
					array[sp] = pop(stack, pegLane(peg));
				}
			};
			const auto moveBetweenStacks = [&r, &push, &pop, &pegLane]
				(std::vector<SortItem>& from, int fromPeg, std::vector<SortItem>& to, int toPeg)
			{
				require(!from.empty(), "Hanoi inter-peg move requires a source item.");
				push(to, pegLane(toPeg), pop(from, pegLane(fromPeg)));
				while (!from.empty())
				{
					r.chargeWork();
					r.compare(from.back(), to.back(), from.size() - 1, to.size() - 1,
						pegLane(fromPeg), pegLane(toPeg));
					r.addMetric("hanoi_comparisons");
					if (compareItems(from.back(), to.back(), r.trace.direction) != ComparisonResult::Equal) break;
					push(to, pegLane(toPeg), pop(from, pegLane(fromPeg)));
				}
			};
			const auto validMoveCount = [](std::uint64_t moves)
			{
				return (moves & (moves + 1)) == 0;
			};
			const auto heightForMoves = [](std::uint64_t movesPlusOne)
			{
				std::size_t height = 0;
				while (movesPlusOne > 1) { movesPlusOne >>= 1; ++height; }
				return height;
			};
			const auto hanoi = [&r, &array, &sp, &unsorted, &targetIndex, &target, &targetMoves,
				&stack2, &stack3, &moveFromMain, &moveToMain, &moveBetweenStacks, &pegLane, &validMoveCount]
				(int startPeg, bool goRight, int endCondition)
			{
				std::uint64_t moves = 0;
				int smallestPeg = startPeg;
				const auto endConditionMet = [&r, &targetIndex, &target, &targetMoves, &stack2, &pegLane, &validMoveCount,
					endCondition, &moves]()
				{
					if (!validMoveCount(moves)) return false;
					switch (endCondition)
					{
					case 1:
						if (stack2.empty()) return true;
						r.compare(target, stack2.back(), targetIndex, stack2.size() - 1,
							"main", pegLane(2), SemanticRole::HeldKey, SemanticRole::ComparedItem);
						r.addMetric("hanoi_comparisons");
						return compareItems(target, stack2.back(), r.trace.direction) != ComparisonResult::Greater;
					case 2: return moves == targetMoves;
					case 3: return stack2.empty();
					default: throw std::logic_error("Unknown Hanoi end condition.");
					}
				};
			if (!endConditionMet())
			{
				++moves;
				r.addMetric("hanoi_tower_moves");
				switch (smallestPeg)
				{
				case 1:
					if (goRight) { moveFromMain(stack2, 2, true); smallestPeg = 2; }
					else { moveFromMain(stack3, 3, true); smallestPeg = 3; }
					break;
				case 2:
					if (goRight) { moveBetweenStacks(stack2, 2, stack3, 3); smallestPeg = 3; }
					else moveToMain(stack2, 2);
					break;
				case 3:
					if (goRight) { moveToMain(stack3, 3); smallestPeg = 1; }
					else { moveBetweenStacks(stack3, 3, stack2, 2); smallestPeg = 2; }
					break;
				}
			}
			while (!endConditionMet())
			{
				moves += 2;
				r.addMetric("hanoi_tower_moves", 2);
				switch (smallestPeg)
				{
				case 1:
					if (!stack2.empty() && (stack3.empty() ||
						(compareItems(stack2.back(), stack3.back(), r.trace.direction) == ComparisonResult::Less)))
						moveBetweenStacks(stack2, 2, stack3, 3);
					else if (!stack3.empty()) moveBetweenStacks(stack3, 3, stack2, 2);
					if (goRight) { moveFromMain(stack2, 2, true); smallestPeg = 2; }
					else { moveFromMain(stack3, 3, true); smallestPeg = 3; }
					break;
				case 2:
					if (stack3.empty() || (sp < unsorted &&
						compareItems(array[sp], stack3.back(), r.trace.direction) == ComparisonResult::Less))
						moveFromMain(stack3, 3, true);
					else moveToMain(stack3, 3);
					if (goRight) { moveBetweenStacks(stack2, 2, stack3, 3); smallestPeg = 3; }
					else { moveToMain(stack2, 2); smallestPeg = 1; }
					break;
				case 3:
					if (stack2.empty() || (sp < unsorted &&
						compareItems(array[sp], stack2.back(), r.trace.direction) == ComparisonResult::Less))
						moveFromMain(stack2, 2, true);
					else moveToMain(stack2, 2);
					if (goRight) { moveToMain(stack3, 3); smallestPeg = 1; }
					else { moveBetweenStacks(stack3, 3, stack2, 2); smallestPeg = 2; }
					break;
				}
			}
			return moves;
		};

			while (unsorted < length)
			{
				r.chargeWork();
				require(sp < length, "Hanoi sort exhausted its input before all items entered the ordered tower.");
				targetIndex = sp;
				target = array[sp];
				std::uint64_t moves = hanoi(2, true, 1);
				const std::size_t height = heightForMoves(moves + 1);
				targetMoves = moves;
				const bool evenHeight = height % 2 == 0;
				if (evenHeight) hanoi(1, true, 2);
				unsorted += moveFromMain(stack2, 2, false);
				hanoi(3, evenHeight, 2);
			}
			std::uint64_t moves = hanoi(2, true, 3);
			const std::size_t height = heightForMoves(moves + 1);
			if (height % 2 == 1)
			{
				targetMoves = moves;
				hanoi(3, true, 2);
			}
			require(stack2.empty() && stack3.empty() && sp == 0,
				"Hanoi return phase must empty both pegs and restore the main stack.");
			r.writeMainRange(0, array);
			r.resizeAuxiliary("hanoi-main-stack", 0);
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

		void binaryGnome(Recorder& r)
		{
			// Binary Gnome Sort locates a stable insertion point by binary search,
			// then uses adjacent swaps to move the item into place.
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				const SortItem key = r.at(i);
				std::size_t low = 0, high = i;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = i; position > low; --position)
					r.swap(position, position - 1, SemanticRole::HeldKey);
			}
		}

		std::size_t nextPowerOfTwo(std::size_t value)
		{
			std::size_t power = 1;
			while (power < value && power <= (static_cast<std::size_t>(-1) / 2)) power *= 2;
			return power;
		}

		void circleSortRecursivePass(Recorder& r, std::size_t low, std::size_t high, std::size_t& swaps)
		{
			if (low >= high) return;
			std::size_t left = low, right = high;
			while (left < right)
			{
				// The pinned implementation pads the range to a power of two and
				// skips pairs whose right member is virtual. Keeping that schedule
				// preserves its distinctive comparison order while safely handling
				// every real tail element.
				if (right < r.state.slots.size())
				{
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(left, right);
						++swaps;
					}
				}
				++left;
				--right;
			}
			const std::size_t middle = low + (high - low) / 2;
			circleSortRecursivePass(r, low, middle, swaps);
			circleSortRecursivePass(r, middle + 1, high, swaps);
		}

		void circleSort(Recorder& r, bool recursiveVariant)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			if (recursiveVariant)
			{
				const std::size_t paddedHigh = nextPowerOfTwo(n) - 1;
				for (;;)
				{
					std::size_t swaps = 0;
					circleSortRecursivePass(r, 0, paddedHigh, swaps);
					if (swaps == 0) break;
				}
				return;
			}

			// Iterative form visits the same mirrored pairs by decreasing dyadic
			// group width. Partial tail groups are clipped to the real sequence;
			// ArrayV's power-of-two padding loop otherwise leaves non-power-of-two
			// tails untouched, which is a source defect rather than a useful variant.
			for (;;)
			{
				std::size_t swaps = 0;
				for (std::size_t gap = nextPowerOfTwo(n) / 2; gap > 0; gap /= 2)
				{
					const std::size_t width = gap > static_cast<std::size_t>(-1) / 2
						? static_cast<std::size_t>(-1) : gap * 2;
					for (std::size_t start = 0; start < n; start += width)
					{
						const std::size_t end = start + width - 1;
						std::size_t left = start, right = end;
						while (left < right)
						{
							if (right < n)
							{
								const SortItem a = r.at(left), b = r.at(right);
								r.compare(a, b, left, right);
								if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
									{ r.swap(left, right); ++swaps; }
							}
							++left;
							--right;
						}
					}
				}
				if (swaps == 0) break;
			}
		}

		void classicThreeSmoothComb(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::vector<std::size_t> gaps;
			for (std::size_t gap = n; gap > 1; --gap)
			{
				std::size_t remaining = gap;
				while (remaining % 6 == 0) remaining /= 6;
				while (remaining % 3 == 0) remaining /= 3;
				while (remaining % 2 == 0) remaining /= 2;
				if (remaining == 1) gaps.push_back(gap);
			}
			if (n > 1) gaps.push_back(1);
			for (std::size_t g = 0; g < gaps.size(); ++g)
			{
				const std::size_t gap = gaps[g];
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				bool changed;
				do
				{
					changed = false;
					for (std::size_t i = gap; i < n; ++i)
					{
						const SortItem a = r.at(i - gap), b = r.at(i);
						r.compare(a, b, i - gap, i);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i - gap, i); changed = true; }
					}
				} while (changed);
				r.annotate("main", RegionKind::Strided, 0, n, gap, -1,
					"3-smooth gap " + std::to_string(gap), begin);
			}
		}

		void threeSmoothCombIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t largestPowerOfTwo = 1;
			while (largestPowerOfTwo <= (n - 1) / 2) largestPowerOfTwo *= 2;
			for (std::size_t powerOfTwo = largestPowerOfTwo;; powerOfTwo /= 2)
			{
				std::vector<std::size_t> gaps(1, powerOfTwo);
				while (gaps.back() <= n / 3) gaps.push_back(gaps.back() * 3);
				for (std::size_t gapIndex = gaps.size(); gapIndex > 0; --gapIndex)
				{
					const std::size_t gap = gaps[gapIndex - 1];
					if (gap >= n) continue;
					r.addMetric("gap_passes");
					r.setMetric("last_gap", gap);
					const std::string label = "Iterative 3-smooth comb gap " + std::to_string(gap);
					const std::size_t begin = r.trace.events.size();
					const int phase = r.beginPhase(label);
					for (std::size_t i = 0; i + gap < n; ++i)
					{
						const SortItem left = r.at(i), right = r.at(i + gap);
						r.compare(left, right, i, i + gap);
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
							r.swap(i, i + gap);
					}
					r.endPhase(phase, label);
					r.annotate("main", RegionKind::Strided, 0, n, gap, -1, label, begin);
				}
				if (powerOfTwo == 1) break;
			}
		}

		void recursiveThreeSmoothPower(Recorder& r, std::size_t position, std::size_t gap, std::size_t end, bool groupPhases)
		{
			if (position >= end || gap >= end - position) return;
			int group = -1;
			if (groupPhases) group = r.beginPhase("Parallel 3-Smooth residue subgroups");
			recursiveThreeSmoothPower(r, position, gap * 3, end, groupPhases);
			recursiveThreeSmoothPower(r, position + gap, gap * 3, end, groupPhases);
			recursiveThreeSmoothPower(r, position + gap * 2, gap * 3, end, groupPhases);
			if (groupPhases) r.endPhase(group, "Parallel 3-Smooth residue subgroups");
			r.addMetric("gap_passes");
			r.setMetric("last_gap", gap);
			for (std::size_t i = position; i + gap < end; i += gap)
			{
				const SortItem a = r.at(i), b = r.at(i + gap);
				r.compare(a, b, i, i + gap);
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(i, i + gap);
			}
		}

		void recursiveThreeSmoothComb(Recorder& r, std::size_t position, std::size_t gap, std::size_t end, bool parallel)
		{
			if (position >= end || gap >= end - position) return;
			int group = -1;
			if (parallel) group = r.beginPhase("Parallel 3-Smooth binary residue split");
			recursiveThreeSmoothComb(r, position, gap * 2, end, parallel);
			recursiveThreeSmoothComb(r, position + gap, gap * 2, end, parallel);
			if (parallel) r.endPhase(group, "Parallel 3-Smooth binary residue split");
			recursiveThreeSmoothPower(r, position, gap, end, parallel);
		}

		void recursiveThreeSmooth(Recorder& r, bool parallel)
		{
			recursiveThreeSmoothComb(r, 0, 1, r.state.slots.size(), parallel);
		}

		bool networkCompareExchange(Recorder& r, std::size_t first, std::size_t second, bool ascendingLayer)
		{
			if (first >= r.state.slots.size() || second >= r.state.slots.size() || first == second) return false;
			const SortItem a = r.at(first), b = r.at(second);
			r.compare(a, b, first, second);
			const ComparisonResult comparison = compareItems(a, b, r.trace.direction);
			if ((ascendingLayer && comparison == ComparisonResult::Greater) ||
				(!ascendingLayer && comparison == ComparisonResult::Less))
				r.swap(first, second);
			return true;
		}

		std::size_t greatestPowerOfTwoLessThan(std::size_t value)
		{
			if (value < 2) return 0;
			std::size_t power = 1;
			while (power <= (value - 1) / 2) power *= 2;
			return power;
		}

		void bitonicIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t width = 2; width < n * 2; width *= 2)
			{
				const bool shortFinalRun = (((n + width - 1) / width) % 2) != 0;
				for (std::size_t gap = width / 2; gap > 0; gap /= 2)
				{
					const std::string label = "Bitonic layer width " + std::to_string(width) + ", gap " + std::to_string(gap);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t i = 0; i < n; ++i)
					{
						const std::size_t partner = i ^ gap;
						if (partner <= i || partner >= n) continue;
						const bool ascendingLayer = (((i & width) == 0) == shortFinalRun);
						if (networkCompareExchange(r, i, partner, ascendingLayer)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
				if (width > n) break;
			}
		}

		void bitonicRecursiveMerge(Recorder& r, std::size_t first, std::size_t length, bool ascendingLayer)
		{
			if (length < 2) return;
			const std::size_t split = greatestPowerOfTwoLessThan(length);
			const std::string label = "Recursive bitonic merge, span " + std::to_string(length);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t offset = 0; offset < length - split; ++offset)
				if (networkCompareExchange(r, first + offset, first + offset + split, ascendingLayer)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			bitonicRecursiveMerge(r, first, split, ascendingLayer);
			bitonicRecursiveMerge(r, first + split, length - split, ascendingLayer);
		}

		void bitonicRecursiveSort(Recorder& r, std::size_t first, std::size_t length, bool ascendingLayer)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			bitonicRecursiveSort(r, first, leftLength, !ascendingLayer);
			bitonicRecursiveSort(r, first + leftLength, length - leftLength, ascendingLayer);
			bitonicRecursiveMerge(r, first, length, ascendingLayer);
		}

		void bitonicParallelMerge(Recorder& r, std::size_t first, std::size_t middle,
			std::size_t end, std::size_t gap, bool ascendingLayer)
		{
			if (end - first < 2) return;
			const std::size_t length = end - first;
			// ArrayV's threaded kernel uses a fixed half-width network and leaves
			// irregular tails in place. Preserve that schedule for its power-of-two
			// domain; use the generalized bitonic merge for other lengths so every
			// catalog input remains a valid permutation sort.
			if ((length & (length - 1)) != 0)
			{
				bitonicRecursiveMerge(r, first, length, ascendingLayer);
				return;
			}
			const std::string label = "Parallel bitonic compare-exchange gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t i = first; i < middle; ++i)
				if (networkCompareExchange(r, i, i + gap, ascendingLayer)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			const std::size_t nextGap = gap / 2;
			const std::size_t leftMiddle = first + (middle - first) / 2;
			const std::size_t rightMiddle = middle + (end - middle) / 2;
			bitonicParallelMerge(r, first, leftMiddle, middle, nextGap, ascendingLayer);
			bitonicParallelMerge(r, middle, rightMiddle, end, nextGap, ascendingLayer);
		}

		void bitonicParallelSort(Recorder& r, std::size_t first, std::size_t end, bool ascendingLayer)
		{
			const std::size_t length = end - first;
			if (length < 2) return;
			const std::size_t middle = first + length / 2;
			const std::string label = "Parallel bitonic recursive subnetworks";
			const int phase = r.beginPhase(label);
			bitonicParallelSort(r, first, middle, !ascendingLayer);
			bitonicParallelSort(r, middle, end, ascendingLayer);
			r.endPhase(phase, label);
			bitonicParallelMerge(r, first, middle, end, length / 2, ascendingLayer);
		}

		void bitonicParallel(Recorder& r)
		{
			bitonicParallelSort(r, 0, r.state.slots.size(), true);
		}

		void boseNelsonCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			networkCompareExchange(r, first, second, true);
			r.addMetric("network_layers");
			r.addMetric("network_comparators");
			r.endPhase(phase, label);
		}

		void boseNelsonMergeRecursive(Recorder& r, std::size_t firstLeft, std::size_t leftLength,
			std::size_t firstRight, std::size_t rightLength, bool parallelSchedule)
		{
			if (leftLength == 1 && rightLength == 1)
			{
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 1+1 comparator");
			}
			else if (leftLength == 1 && rightLength == 2)
			{
				boseNelsonCompare(r, firstLeft, firstRight + 1, "Bose-Nelson 1+2 outer comparator");
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 1+2 inner comparator");
			}
			else if (leftLength == 2 && rightLength == 1)
			{
				boseNelsonCompare(r, firstLeft, firstRight, "Bose-Nelson 2+1 first comparator");
				boseNelsonCompare(r, firstLeft + 1, firstRight, "Bose-Nelson 2+1 second comparator");
			}
			else
			{
				const std::size_t leftMiddle = leftLength / 2;
				const std::size_t rightMiddle = leftLength % 2 == 1 ? rightLength / 2 : (rightLength + 1) / 2;
				if (parallelSchedule)
				{
					const std::string label = "Parallel Bose-Nelson merge subnets";
					const int phase = r.beginPhase(label);
					boseNelsonMergeRecursive(r, firstLeft, leftMiddle, firstRight, rightMiddle, true);
					boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
						firstRight + rightMiddle, rightLength - rightMiddle, true);
					r.endPhase(phase, label);
				}
				else
				{
					boseNelsonMergeRecursive(r, firstLeft, leftMiddle, firstRight, rightMiddle, false);
					boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
						firstRight + rightMiddle, rightLength - rightMiddle, false);
				}
				boseNelsonMergeRecursive(r, firstLeft + leftMiddle, leftLength - leftMiddle,
					firstRight, rightMiddle, parallelSchedule);
			}
		}

		void boseNelsonSortRecursive(Recorder& r, std::size_t first, std::size_t length, bool parallelSchedule)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			if (parallelSchedule)
			{
				const std::string label = "Parallel Bose-Nelson sorted halves";
				const int phase = r.beginPhase(label);
				boseNelsonSortRecursive(r, first, leftLength, true);
				boseNelsonSortRecursive(r, first + leftLength, length - leftLength, true);
				r.endPhase(phase, label);
			}
			else
			{
				boseNelsonSortRecursive(r, first, leftLength, false);
				boseNelsonSortRecursive(r, first + leftLength, length - leftLength, false);
			}
			boseNelsonMergeRecursive(r, first, leftLength, first + leftLength, length - leftLength, parallelSchedule);
		}

		void boseNelsonIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t width = 2; width <= paddedLength; width *= 2)
				for (std::size_t offset = 0; offset < width / 2; ++offset)
				{
					const std::string label = "Iterative Bose-Nelson layer width " + std::to_string(width) +
						", offset " + std::to_string(offset);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t start = 0; start + offset < n; start += width)
					{
						const std::size_t half = width / 2;
						const std::size_t leftStart = start + offset;
						for (std::size_t i = 0; i < half - offset; ++i)
							if ((i & ~offset) == i && leftStart + i < n && start + half + i < n &&
								networkCompareExchange(r, leftStart + i, start + half + i, true)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void oddEvenMergeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t width = 1; width < n; width *= 2)
				for (std::size_t stride = width; stride > 0; stride /= 2)
					for (std::size_t blockOffset = stride % width; blockOffset + stride < n; blockOffset += stride * 2)
					{
						const std::string label = "Iterative odd-even merge layer width " + std::to_string(width * 2) +
							", stride " + std::to_string(stride);
						const int phase = r.beginPhase(label);
						std::size_t comparators = 0;
						for (std::size_t offset = 0; offset < stride; ++offset)
						{
							const std::size_t first = blockOffset + offset;
						const std::size_t second = first + stride;
						if (first < n && second < n && first / (width * 2) == second / (width * 2) &&
							networkCompareExchange(r, first, second, true)) ++comparators;
						}
						if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void oddEvenMergeRecursive(Recorder& r, std::size_t first, std::size_t middle,
			std::size_t length, std::size_t gap, bool parallelSchedule)
		{
			const std::size_t stride = gap * 2;
			if (stride < length)
			{
				if ((length / gap) % 2 != 0)
				{
					oddEvenMergeRecursive(r, first, (middle + 1) / 2, length + gap, stride, parallelSchedule);
					oddEvenMergeRecursive(r, first + gap, middle / 2, length - gap, stride, parallelSchedule);
				}
				else
				{
					oddEvenMergeRecursive(r, first, (middle + 1) / 2, length, stride, parallelSchedule);
					oddEvenMergeRecursive(r, first + gap, middle / 2, length, stride, parallelSchedule);
				}
				const std::string label = parallelSchedule ? "Parallel odd-even merge compare layer" : "Recursive odd-even merge compare layer";
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				if (middle % 2 != 0)
				{
					for (std::size_t i = first; i + gap < first + length; i += stride)
						if (networkCompareExchange(r, i, i + gap, true)) ++comparators;
				}
				else
				{
					for (std::size_t i = first + gap; i + gap < first + length; i += stride)
						if (networkCompareExchange(r, i, i + gap, true)) ++comparators;
				}
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
			else if (length > gap)
				boseNelsonCompare(r, first, first + gap, parallelSchedule ? "Parallel odd-even merge base layer" : "Recursive odd-even merge base layer");
		}

		void oddEvenMergeSortRecursive(Recorder& r, std::size_t first, std::size_t length, bool parallelSchedule)
		{
			if (length < 2) return;
			const std::size_t leftLength = length / 2;
			if (parallelSchedule)
			{
				const std::string label = "Parallel odd-even merge sorted halves";
				const int phase = r.beginPhase(label);
				oddEvenMergeSortRecursive(r, first, leftLength, true);
				oddEvenMergeSortRecursive(r, first + leftLength, length - leftLength, true);
				r.endPhase(phase, label);
			}
			else
			{
				oddEvenMergeSortRecursive(r, first, leftLength, false);
				oddEvenMergeSortRecursive(r, first + leftLength, length - leftLength, false);
			}
			oddEvenMergeRecursive(r, first, leftLength, length, 1, parallelSchedule);
		}

		void oddEvenMergeRecursive(Recorder& r, bool parallelSchedule)
		{
			oddEvenMergeSortRecursive(r, 0, r.state.slots.size(), parallelSchedule);
		}

		void diamondCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			if (networkCompareExchange(r, first, second, true)) r.addMetric("network_comparators");
			r.addMetric("network_layers");
			r.endPhase(phase, label);
		}

		void diamondIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t width = 4; width <= paddedLength; width *= 2)
				for (std::size_t diagonal = 0; diagonal < width / 2; ++diagonal)
				{
					const std::size_t count = diagonal <= width / 4 ? diagonal : width / 2 - diagonal;
					const std::string label = "Iterative diamond layer width " + std::to_string(width) +
						", diagonal " + std::to_string(diagonal);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t block = 0; block < n; block += width)
					{
						if (block + count + 1 >= n) continue;
						const std::size_t limit = std::min(n, block + width - count);
						for (std::size_t i = block + count; i + 1 < limit; i += 2)
							if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
			const std::size_t finalWidth = std::max<std::size_t>(2, paddedLength);
			for (std::size_t diagonal = 0; diagonal <= finalWidth / 2; ++diagonal)
			{
				const std::string label = "Iterative diamond final layer " + std::to_string(diagonal);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				const std::size_t limit = std::min(n, finalWidth - diagonal);
				for (std::size_t i = diagonal; i + 1 < limit; i += 2)
					if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
		}

		void diamondRecursive(Recorder& r, std::size_t first, std::size_t end, bool merge)
		{
			const std::size_t length = end - first;
			if (length == 2)
			{
				diamondCompare(r, first, first + 1, "Recursive diamond pair");
			}
			else if (length >= 3)
			{
				const double quarter = static_cast<double>(length) / 4.0;
				const std::size_t middle = first + length / 2;
				const std::size_t innerFirst = first + static_cast<std::size_t>(quarter);
				const std::size_t innerEnd = first + static_cast<std::size_t>(quarter * 3.0);
				if (merge)
				{
					diamondRecursive(r, first, middle, true);
					diamondRecursive(r, middle, end, true);
				}
				diamondRecursive(r, innerFirst, innerEnd, false);
				diamondRecursive(r, first, middle, false);
				diamondRecursive(r, middle, end, false);
				diamondRecursive(r, innerFirst, innerEnd, false);
			}
		}

		void diamondRecursiveVariant(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n != 0 && (n & (n - 1)) != 0)
			{
				// The recursive quarter split is only closed over power-of-two
				// lengths. Preserve it on that domain and use the clipped Diamond
				// comparator schedule for the source's irregular tail cases.
				diamondIterative(r);
				return;
			}
			diamondRecursive(r, 0, n, true);
		}

		void pairwiseCompare(Recorder& r, std::size_t first, std::size_t second, const std::string& label)
		{
			const int phase = r.beginPhase(label);
			if (networkCompareExchange(r, first, second, true)) r.addMetric("network_comparators");
			r.addMetric("network_layers");
			r.endPhase(phase, label);
		}

		void pairwiseIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t width = 1;
			while (width < n)
			{
				const std::string label = "Iterative pairwise merge width " + std::to_string(width);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				std::size_t b = width, cycle = 0;
				while (b < n)
				{
					if (networkCompareExchange(r, b - width, b, true)) ++comparators;
					cycle = (cycle + 1) % width;
					++b;
					if (cycle == 0) b += width;
				}
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
				width *= 2;
			}
			width /= 4;
			std::size_t b = 0, cycle = 0;
			std::size_t outerWidth = 1;
			while (width > 0)
			{
				std::size_t distance = outerWidth;
				while (distance > 0)
				{
					const std::string label = "Iterative pairwise merge width " + std::to_string(width) +
						", distance " + std::to_string(distance);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					b = (distance + 1) * width;
					cycle = 0;
					while (b < n)
					{
						if (networkCompareExchange(r, b - distance * width, b, true)) ++comparators;
						cycle = (cycle + 1) % width;
						++b;
						if (cycle == 0) b += width;
					}
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
					distance /= 2;
				}
				width /= 2;
				outerWidth = outerWidth * 2 + 1;
			}
		}

		void pairwiseRecursive(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (gap == 0 || end <= gap || first == end - gap) return;
			std::size_t b = first + gap;
			const std::string label = "Recursive pairwise adjacent layer, gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			while (b < end)
			{
				if (networkCompareExchange(r, b - gap, b, true)) ++comparators;
				if (gap > (static_cast<std::size_t>(-1) - b) / 2) break;
				b += 2 * gap;
			}
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);

			if (gap > static_cast<std::size_t>(-1) / 2) return;
			const std::size_t nextGap = gap * 2;
			if (((end - first) / gap) % 2 == 0)
			{
				pairwiseRecursive(r, first, end, nextGap);
				if (first <= static_cast<std::size_t>(-1) - gap && end <= static_cast<std::size_t>(-1) - gap)
					pairwiseRecursive(r, first + gap, end + gap, nextGap);
			}
			else
			{
				if (end <= static_cast<std::size_t>(-1) - gap)
					pairwiseRecursive(r, first, end + gap, nextGap);
				pairwiseRecursive(r, first + gap, end, nextGap);
			}

			std::size_t tree = 1;
			while (tree < (end - first) / gap)
			{
				if (tree > (static_cast<std::size_t>(-1) - 1) / 2) break;
				tree = tree * 2 + 1;
			}
			b = first + gap;
			while (b <= end && gap < end - b)
			{
				std::size_t distance = tree;
				while (distance > 1)
				{
					distance /= 2;
					if (distance <= (static_cast<std::size_t>(-1) - b) / gap && b + distance * gap < end)
						pairwiseCompare(r, b, b + distance * gap, "Recursive pairwise far comparator");
				}
				if (gap > (static_cast<std::size_t>(-1) - b) / 2) break;
				b += 2 * gap;
			}
		}

		void creaseSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t maximum = 1;
			while (maximum <= (static_cast<std::size_t>(-1) / 2) && maximum * 2 < n) maximum *= 2;
			for (std::size_t next = maximum; next > 0; next /= 2)
			{
				const std::string adjacentLabel = "Crease adjacent layer, span " + std::to_string(next);
				int phase = r.beginPhase(adjacentLabel);
				std::size_t comparators = 0;
				for (std::size_t i = 0; i + 1 < n; i += 2)
					if (networkCompareExchange(r, i, i + 1, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, adjacentLabel);
				for (std::size_t distance = maximum; distance >= next && distance > 1; distance /= 2)
				{
					const std::string label = "Crease fold layer, distance " + std::to_string(distance);
					phase = r.beginPhase(label);
					comparators = 0;
					for (std::size_t i = 1; i + distance - 1 < n; i += 2)
						if (networkCompareExchange(r, i, i + distance - 1, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
			}
		}

		void foldSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 2;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t fold = paddedLength / 2; fold > 0; fold /= 2)
				for (std::size_t width = paddedLength; width >= fold; width /= 2)
				{
					for (std::size_t start = 0; start < n; start += width)
					{
						const std::string label = "Fold halver width " + std::to_string(width) +
							", fold " + std::to_string(fold) + ", block " + std::to_string(start);
						const int phase = r.beginPhase(label);
						std::size_t comparators = 0;
						std::size_t left = start, right = start + width - 1;
						while (left < right)
						{
							if (networkCompareExchange(r, left, right, true)) ++comparators;
							++left;
							--right;
						}
						if (comparators != 0) r.addMetric("network_comparators", comparators);
						r.addMetric("network_layers");
						r.endPhase(phase, label);
					}
					if (width == fold) break;
				}
		}

		void mergeExchangeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t largestPower = 1;
			while (largestPower <= (static_cast<std::size_t>(-1) / 2) && largestPower * 2 <= n - 1) largestPower *= 2;
			for (std::size_t p = largestPower; p > 0; p /= 2)
			{
				std::size_t q = largestPower, remainder = 0, distance = p;
				for (;;)
				{
					const std::string label = "Merge-exchange layer p=" + std::to_string(p) +
						", q=" + std::to_string(q) + ", distance=" + std::to_string(distance);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					for (std::size_t i = 0; i + distance < n; ++i)
						if ((i & p) == remainder && networkCompareExchange(r, i, i + distance, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
					if (q == p) break;
					distance = q - p;
					q /= 2;
					remainder = p;
				}
			}
		}

		void pairwiseMergeNetwork(Recorder& r, std::size_t first, std::size_t end)
		{
			const std::size_t middle = (first + end) / 2;
			const std::size_t innerMiddle = (first + middle) / 2;
			const std::size_t gap = middle - innerMiddle;
			for (std::size_t offset = 0; innerMiddle + offset < middle; ++offset)
				for (std::size_t distance = gap, index = innerMiddle; distance > 0; distance /= 2)
				{
					const std::size_t left = index + offset;
					const std::size_t right = left + distance;
					pairwiseCompare(r, left, right, "Pairwise merge comparator distance " + std::to_string(distance));
					const std::size_t nextDistance = distance / 2;
					if (nextDistance != 0) index -= nextDistance - (offset & nextDistance);
				}
			if (end - first > 4) pairwiseMergeNetwork(r, middle, end);
		}

		void pairwiseMergeSortRecursive(Recorder& r, std::size_t first, std::size_t end)
		{
			if (end - first < 2) return;
			const std::size_t middle = (first + end) / 2;
			const std::string label = "Recursive pairwise merge halves";
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t left = first, right = middle; left < middle; ++left, ++right)
				if (networkCompareExchange(r, left, right, true)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			if (end - first > 2)
			{
				pairwiseMergeSortRecursive(r, first, middle);
				pairwiseMergeSortRecursive(r, middle, end);
				pairwiseMergeNetwork(r, first, end);
			}
		}

		void pairwiseMergeIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t gap = paddedLength / 2; gap > 0; gap /= 2)
			{
				const std::string label = "Iterative pairwise merge block gap " + std::to_string(gap);
				const int phase = r.beginPhase(label);
				std::size_t comparators = 0;
				const std::size_t block = gap * 2;
				for (std::size_t start = 0; start < n; start += block)
					for (std::size_t offset = 0; offset < gap; ++offset)
						if (networkCompareExchange(r, start + offset, start + gap + offset, true)) ++comparators;
				if (comparators != 0) r.addMetric("network_comparators", comparators);
				r.addMetric("network_layers");
				r.endPhase(phase, label);
			}
			for (std::size_t width = 2; width < paddedLength; width *= 2)
				for (std::size_t gap = width / 2; gap > 0; gap /= 2)
				{
					const std::string label = "Iterative pairwise merge width " + std::to_string(width) +
						", distance " + std::to_string(gap);
					const int phase = r.beginPhase(label);
					std::size_t comparators = 0;
					const std::size_t block = width * 2;
					for (std::size_t start = 0; start < n; start += block)
						for (std::size_t offset = gap; offset < (width - gap) * 2; offset += gap * 2)
							for (std::size_t i = 0; i < gap; ++i)
								if (networkCompareExchange(r, start + offset + i, start + offset + gap + i, true)) ++comparators;
					if (comparators != 0) r.addMetric("network_comparators", comparators);
					r.addMetric("network_layers");
					r.endPhase(phase, label);
				}
		}

		void weaveIterative(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			std::size_t paddedLength = 1;
			while (paddedLength < n) paddedLength *= 2;
			for (std::size_t i = 1; i < paddedLength; i *= 2)
				for (std::size_t j = 1; j <= i; j *= 2)
					for (std::size_t block = 0; block < paddedLength; block += paddedLength / j)
					{
						const std::size_t distance = paddedLength / i / 2;
						std::size_t moving = 0;
						for (std::size_t line = paddedLength / j - distance; line >= paddedLength / j / 2; line -= distance)
						{
							const std::string label = "Iterative weave layer i=" + std::to_string(i) +
								", j=" + std::to_string(j) + ", line=" + std::to_string(line);
							const int phase = r.beginPhase(label);
							std::size_t comparators = 0;
							for (std::size_t p = 0; p < distance; ++p, ++moving)
								if (networkCompareExchange(r, block + moving, block + line + p, true)) ++comparators;
							if (comparators != 0) r.addMetric("network_comparators", comparators);
							r.addMetric("network_layers");
							r.endPhase(phase, label);
							if (line < distance) break;
						}
					}
		}

		void weaveCircle(Recorder& r, std::size_t position, std::size_t length, std::size_t gap)
		{
			if (length < 2 || gap == 0) return;
			weaveCircle(r, position, length / 2, gap * 2);
			weaveCircle(r, position + gap, length / 2, gap * 2);
			const std::string label = "Recursive weave circle length " + std::to_string(length) +
				", gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			for (std::size_t i = 0; 2 * i < (length - 1) * gap; i += gap)
				if (networkCompareExchange(r, position + i, position + (length - 1) * gap - i, true)) ++comparators;
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			weaveCircle(r, position, length / 2, gap);
			const std::size_t rightStart = position + length * gap / 2;
			if (rightStart < r.state.slots.size()) weaveCircle(r, rightStart, length / 2, gap);
		}

		void weaveRecursive(Recorder& r)
		{
			std::size_t paddedLength = 1;
			while (paddedLength < r.state.slots.size()) paddedLength *= 2;
			weaveCircle(r, 0, paddedLength, 1);
		}

		void weaveParallelCircle(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (first >= end || gap == 0 || end - first < gap) return;
			std::size_t left = first, right = end;
			const std::string label = "Parallel weave circle layer gap " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			std::size_t comparators = 0;
			while (left < right)
			{
				if (networkCompareExchange(r, left, right, true)) ++comparators;
				left += gap;
				if (right < gap) break;
				right -= gap;
			}
			if (comparators != 0) r.addMetric("network_comparators", comparators);
			r.addMetric("network_layers");
			r.endPhase(phase, label);
			if (first < right || left < end)
			{
				const int group = r.beginPhase("Parallel weave independent circle branches");
				if (first < right) weaveParallelCircle(r, first, right, gap);
				if (left < end) weaveParallelCircle(r, left, end, gap);
				r.endPhase(group, "Parallel weave independent circle branches");
			}
		}

		void weaveParallelWrapper(Recorder& r, std::size_t first, std::size_t length, std::size_t gap)
		{
			if (gap >= length || gap == 0 || first >= r.state.slots.size()) return;
			const int group = r.beginPhase("Parallel weave independent residue sorts");
			weaveParallelWrapper(r, first, length, gap * 2);
			weaveParallelWrapper(r, first + gap, length, gap * 2);
			r.endPhase(group, "Parallel weave independent residue sorts");
			const std::size_t end = first + length - gap;
			weaveParallelCircle(r, first, end, gap);
		}

		void weaveParallel(Recorder& r)
		{
			std::size_t paddedLength = 1;
			while (paddedLength < r.state.slots.size()) paddedLength *= 2;
			weaveParallelWrapper(r, 0, paddedLength, 1);
		}

		struct MatrixShape
		{
			std::size_t width;
			bool unbalanced;
			bool insertLast;
		};

		MatrixShape matrixShape(std::size_t length, bool insertLast)
		{
			std::size_t dimension = static_cast<std::size_t>(std::sqrt(static_cast<double>(length)));
			if (dimension * dimension == length - 1) insertLast = true;
			while (dimension > 1 && length % dimension != 0) --dimension;
			const std::size_t height = length / dimension;
			const bool unbalanced = (dimension == 1) != (height == 1);
			return MatrixShape{ dimension, unbalanced, unbalanced || insertLast };
		}

		bool matrixInsertLast(Recorder& r, std::size_t first, std::size_t last, std::size_t gap, bool ascending)
		{
			if (last <= first || last >= r.state.slots.size()) return false;
			const SortItem key = r.at(last);
			r.hold(last);
			std::ptrdiff_t cursor = static_cast<std::ptrdiff_t>(last) - static_cast<std::ptrdiff_t>(gap);
			bool moved = false;
			while (cursor >= static_cast<std::ptrdiff_t>(first))
			{
				const std::size_t index = static_cast<std::size_t>(cursor);
				const SortItem previous = r.at(index);
				r.compare(key, previous, last, index, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				const ComparisonResult result = compareItems(key, previous, SortDirection::Ascending);
				if ((ascending && result != ComparisonResult::Less) || (!ascending && result != ComparisonResult::Greater)) break;
				r.shift(index, index + gap);
				moved = true;
				cursor -= static_cast<std::ptrdiff_t>(gap);
			}
			r.place(static_cast<std::size_t>(cursor + static_cast<std::ptrdiff_t>(gap)), key);
			return moved;
		}

		void matrixGapReverse(Recorder& r, std::size_t first, std::size_t end, std::size_t gap)
		{
			if (gap == 0) return;
			const std::string label = "Matrix row reversal, stride " + std::to_string(gap);
			const int phase = r.beginPhase(label);
			for (std::size_t left = first, right = end; left < right; left += gap)
			{
				if (right < gap) break;
				const std::size_t other = right - gap;
				if (left >= r.state.slots.size() || other >= r.state.slots.size()) break;
				if (left >= other) break;
				r.swap(left, other);
				right = other;
			}
			r.endPhase(phase, label);
		}

		bool matrixSort(Recorder& r, std::size_t first, std::size_t end, std::size_t gap,
			bool ascending, std::size_t depth)
		{
			if (gap == 0 || end <= first || depth > r.state.slots.size() + 2) return false;
			const std::size_t length = (end - first) / gap;
			if (length < 2) return false;
			if (length <= 16)
			{
				bool changed = false;
				for (std::size_t index = first + gap; index < end; index += gap)
					changed = matrixInsertLast(r, first, index, gap, ascending) || changed;
				return changed;
			}

			const MatrixShape shape = matrixShape(length, false);
			const std::size_t rowSpan = shape.width * gap;
			if (shape.insertLast)
			{
				const bool sortedPrefix = matrixSort(r, first, end - gap, gap, ascending, depth + 1);
				const bool inserted = matrixInsertLast(r, first, end - gap, gap, ascending);
				return sortedPrefix || inserted;
			}

			for (std::size_t row = first + rowSpan; row < end; row += rowSpan * 2)
				matrixGapReverse(r, row, row + rowSpan, gap);

			bool changed = false;
			bool columnChanged = false;
			do
			{
				bool rowChanged = false;
				bool rowAscending = ascending;
				for (std::size_t row = first; row < end; row += rowSpan)
				{
					const bool current = matrixSort(r, row, row + rowSpan, gap, rowAscending, depth + 1);
					rowChanged = current || rowChanged;
					changed = changed || rowChanged;
					rowAscending = !rowAscending;
				}

				columnChanged = false;
				for (std::size_t column = 0; column < shape.width; ++column)
				{
					const std::size_t columnStart = first + column * gap;
					const bool current = matrixSort(r, columnStart, end + column * gap, rowSpan, ascending, depth + 1);
					columnChanged = current || columnChanged;
					changed = changed || columnChanged;
				}
				r.addMetric("matrix_interleave_passes");
			} while (columnChanged);

			for (std::size_t row = first + rowSpan; row < end; row += rowSpan * 2)
				matrixGapReverse(r, row, row + rowSpan, gap);
			return changed;
		}

		void matrixSort(Recorder& r)
		{
			matrixSort(r, 0, r.state.slots.size(), 1, r.trace.direction == SortDirection::Ascending, 0);
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

		void optimizedGnome(Recorder& r)
		{
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				std::size_t position = i;
				while (position > 0)
				{
					const SortItem a = r.at(position - 1), b = r.at(position);
					r.compare(a, b, position - 1, position);
					if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater) break;
					r.swap(position - 1, position);
					--position;
				}
			}
		}

		void shove(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::size_t i = 0;
			while (i + 1 < n)
			{
				const SortItem a = r.at(i), b = r.at(i + 1);
				r.compare(a, b, i, i + 1);
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
				{
					for (std::size_t forward = i; forward + 1 < n; ++forward) r.swap(forward, forward + 1);
					if (i > 0) --i;
				}
				else ++i;
			}
		}

		void slope(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::ptrdiff_t j = 1; j < static_cast<std::ptrdiff_t>(n); ++j)
			{
				std::ptrdiff_t i = j;
				for (std::ptrdiff_t k = i - 1; k >= 0; --k, --i)
				{
					const SortItem a = r.at(static_cast<std::size_t>(i)), b = r.at(static_cast<std::size_t>(k));
					r.compare(a, b, static_cast<std::size_t>(i), static_cast<std::size_t>(k));
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Less)
						r.swap(static_cast<std::size_t>(i), static_cast<std::size_t>(k));
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

		void hybridComb(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("gap_passes");
			r.declareMetric("last_gap");
			r.declareMetric("insertion_fallbacks");
			std::size_t gap = count;
			bool changed = false;
			const double insertionThreshold = std::min(8.0, static_cast<double>(count) / 32.0);
			while (gap > 1 || changed)
			{
				if (gap > 1) gap = static_cast<std::size_t>(static_cast<double>(gap) / 1.3);
				gap = std::max<std::size_t>(1, gap);
				changed = false;
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				if (static_cast<double>(gap) <= insertionThreshold)
				{
					r.addMetric("insertion_fallbacks");
					insertion(r);
					r.annotate("main", RegionKind::Contiguous, 0, count, 1, -1, "Hybrid comb insertion fallback", begin);
					break;
				}
				for (std::size_t i = 0; i + gap < count; ++i)
				{
					const SortItem a = r.at(i), b = r.at(i + gap);
					r.compare(a, b, i, i + gap);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(i, i + gap);
						changed = true;
					}
				}
				r.annotate("main", RegionKind::Strided, 0, count, gap, -1, "Hybrid comb gap " + std::to_string(gap), begin);
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

		bool stoogeCompareSwap(Recorder& r, std::size_t first, std::size_t second)
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

		void quadStooge(Recorder& r, std::size_t first, std::size_t length)
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

		void slowSort(Recorder& r, std::size_t first, std::size_t last)
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

		void sillySort(Recorder& r, std::size_t first, std::size_t last)
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

		void snuffleSort(Recorder& r, std::size_t first, std::size_t last)
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

		bool circloidCircle(Recorder& r, std::size_t first, std::size_t last)
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

		bool circloidPass(Recorder& r, std::size_t first, std::size_t last)
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

		void completeGraphCompareSwap(Recorder& r, std::size_t first, std::size_t second)
		{
			if (first >= r.state.slots.size() || second >= r.state.slots.size() || first == second) return;
			const SortItem left = r.at(first), right = r.at(second);
			r.compare(left, right, first, second);
			r.addMetric("network_comparators");
			if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) r.swap(first, second);
		}

		void completeGraphSplit(Recorder& r, std::size_t first, std::size_t middle, std::size_t end)
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

		void bogo(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0xB060B060u);
			const std::size_t count = r.state.slots.size();
			while (true)
			{
				bool sorted = true;
				for (std::size_t i = 1; i < count; ++i)
				{
					const SortItem a = r.at(i - 1), b = r.at(i);
					r.compare(a, b, i - 1, i);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { sorted = false; break; }
				}
				if (sorted) break;
				for (std::size_t i = count; i > 1; --i)
				{
					std::uniform_int_distribution<std::size_t> choose(0, i - 1);
					r.swap(i - 1, choose(random));
				}
				r.addMetric("shuffle_attempts");
			}
		}

		bool bogoSorted(Recorder& r)
		{
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				const SortItem left = r.at(i - 1), right = r.at(i);
				r.compare(left, right, i - 1, i);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void bozoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xB020B020u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 1);
			while (!bogoSorted(r))
			{
				const std::size_t first = choose(random), second = choose(random);
				r.swap(first, second);
				r.addMetric("random_attempts");
			}
		}

		void bubbleBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xBB060601u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 2);
			while (!bogoSorted(r))
			{
				const std::size_t index = choose(random);
				const SortItem left = r.at(index), right = r.at(index + 1);
				r.compare(left, right, index, index + 1);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) r.swap(index, index + 1);
				r.addMetric("random_attempts");
			}
		}

		void exchangeBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xE8C4A6E1u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 1);
			while (!bogoSorted(r))
			{
				const std::size_t first = choose(random), second = choose(random);
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				const ComparisonResult result = compareItems(a, b, r.trace.direction);
				if ((first < second && result == ComparisonResult::Greater) ||
					(first > second && result == ComparisonResult::Less)) r.swap(first, second);
				r.addMetric("random_attempts");
			}
		}

		void deterministicBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::function<bool(std::size_t)> enumerate = [&r, count, &enumerate](std::size_t depth) -> bool
			{
				if (depth >= count - 1)
				{
					r.addMetric("permutation_checks");
					return bogoSorted(r);
				}
				for (std::size_t i = count - 1; i > depth; --i)
				{
					if (enumerate(depth + 1)) return true;
					if ((count - depth) % 2 == 0) r.swap(depth, i);
					else r.swap(depth, count - 1);
				}
				return enumerate(depth + 1);
			};
			enumerate(0);
		}

		bool suffixMinimumAt(Recorder& r, std::size_t first)
		{
			const SortItem candidate = r.at(first);
			for (std::size_t i = first + 1; i < r.state.slots.size(); ++i)
			{
				const SortItem other = r.at(i);
				r.compare(candidate, other, first, i, "main", "main", SemanticRole::CurrentMinimum, SemanticRole::ComparedItem);
				if (compareItems(candidate, other, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		bool rangeMinimumAt(Recorder& r, std::size_t first, std::size_t candidateIndex, std::size_t end)
		{
			const SortItem candidate = r.at(candidateIndex);
			for (std::size_t i = first; i < end; ++i)
			{
				if (i == candidateIndex) continue;
				const SortItem other = r.at(i);
				r.compare(candidate, other, candidateIndex, i, "main", "main", SemanticRole::CurrentMinimum, SemanticRole::ComparedItem);
				if (compareItems(candidate, other, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		bool rangeMaximumAt(Recorder& r, std::size_t first, std::size_t candidateIndex, std::size_t end)
		{
			const SortItem candidate = r.at(candidateIndex);
			for (std::size_t i = first; i < end; ++i)
			{
				if (i == candidateIndex) continue;
				const SortItem other = r.at(i);
				r.compare(other, candidate, i, candidateIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(other, candidate, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void bogoShuffleRange(Recorder& r, std::size_t first, std::size_t end, std::mt19937& random)
		{
			for (std::size_t i = first; i < end; ++i)
			{
				std::uniform_int_distribution<std::size_t> choose(i, end - 1);
				r.swap(i, choose(random));
			}
		}

		void cocktailBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xC0C7A11u);
			std::size_t first = 0, end = count;
			while (first + 1 < end)
			{
				if (rangeMinimumAt(r, first, first, end)) { ++first; continue; }
				if (rangeMaximumAt(r, first, end - 1, end)) { --end; continue; }
				bogoShuffleRange(r, first, end, random);
				r.addMetric("random_shuffles");
			}
		}

		bool rangeSplitAt(Recorder& r, std::size_t first, std::size_t middle, std::size_t end)
		{
			if (first >= middle || middle >= end) return true;
			std::size_t leftMaximum = first;
			for (std::size_t i = first + 1; i < middle; ++i)
			{
				const SortItem currentMax = r.at(leftMaximum), candidate = r.at(i);
				r.compare(currentMax, candidate, leftMaximum, i, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (compareItems(currentMax, candidate, r.trace.direction) == ComparisonResult::Less) leftMaximum = i;
			}
			const SortItem maxLeft = r.at(leftMaximum);
			for (std::size_t i = middle; i < end; ++i)
			{
				const SortItem right = r.at(i);
				r.compare(maxLeft, right, leftMaximum, i, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (compareItems(maxLeft, right, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void medianQuickBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x4D454449u);
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &random, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				const std::size_t middle = first + (end - first) / 2;
				const std::size_t begin = r.trace.events.size();
				while (!rangeSplitAt(r, first, middle, end))
				{
					bogoShuffleRange(r, first, end, random);
					r.addMetric("random_shuffles");
				}
				r.addMetric("split_partitions");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Median Quick Bogo split", begin);
				sortRange(first, middle, depth + 1);
				sortRange(middle, end, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		bool bogoPivotPartitioned(Recorder& r, std::size_t first, std::size_t pivotIndex, std::size_t end)
		{
			const SortItem pivot = r.at(pivotIndex);
			for (std::size_t i = first; i < pivotIndex; ++i)
			{
				const SortItem item = r.at(i);
				r.compare(item, pivot, i, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
				if (compareItems(item, pivot, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			for (std::size_t i = pivotIndex + 1; i < end; ++i)
			{
				const SortItem item = r.at(i);
				r.compare(pivot, item, pivotIndex, i, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
				if (compareItems(pivot, item, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void quickBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x0A1CB060u);
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &random, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				std::size_t pivot = first;
				const std::size_t begin = r.trace.events.size();
				while (!bogoPivotPartitioned(r, first, pivot, end))
				{
					for (std::size_t i = first; i < end; ++i)
					{
						std::uniform_int_distribution<std::size_t> choose(i, end - 1);
						const std::size_t other = choose(random);
						if (pivot == i) pivot = other;
						else if (pivot == other) pivot = i;
						r.swap(i, other, SemanticRole::Pivot);
					}
					r.addMetric("random_shuffles");
				}
				r.addMetric("partitions");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Quick Bogo partition", begin);
				sortRange(first, pivot, depth + 1);
				sortRange(pivot + 1, end, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		bool guessIndicesSorted(Recorder& r, const std::string& lane, bool reverseScan, std::size_t* failureIndex)
		{
			const std::size_t count = r.state.slots.size();
			if (failureIndex) *failureIndex = NoGapIndex;
			for (std::size_t step = 0; step + 1 < count; ++step)
			{
				const std::size_t i = reverseScan ? count - 2 - step : step;
				const std::size_t leftIndex = static_cast<std::size_t>(r.counter(lane, i));
				const std::size_t rightIndex = static_cast<std::size_t>(r.counter(lane, i + 1));
				require(leftIndex < count && rightIndex < count, "Guess workspace index is outside the source input.");
				const SortItem left = r.trace.input[leftIndex], right = r.trace.input[rightIndex];
				r.compare(left, right, leftIndex, rightIndex);
				const ComparisonResult result = compareItems(left, right, r.trace.direction);
				if (result == ComparisonResult::Less || (result == ComparisonResult::Equal && leftIndex < rightIndex)) continue;
				if (failureIndex) *failureIndex = i;
				return false;
			}
			return true;
		}

		void commitGuessPermutation(Recorder& r, const std::string& indexLane)
		{
			const std::size_t count = r.state.slots.size();
			const std::string outputLane = "guess_output";
			r.resizeAuxiliary(outputLane, count);
			std::vector<SortItem> output;
			output.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				const std::size_t source = static_cast<std::size_t>(r.counter(indexLane, i));
				require(source < r.trace.input.size(), "Guess result index is outside the source input.");
				const SortItem item = r.trace.input[source];
				output.push_back(item);
				r.writeAuxiliary(outputLane, i, item);
			}
			r.writeMainRange(0, output);
			r.resizeAuxiliary(outputLane, 0);
		}

		void advanceGuessIndices(Recorder& r, const std::string& lane, std::size_t firstEligible)
		{
			const std::size_t count = r.state.slots.size();
			std::size_t position = 0;
			while (position < count)
			{
				if (position >= firstEligible && r.counter(lane, position) < static_cast<int>(count - 1))
				{
					r.writeCounter(lane, position, r.counter(lane, position) + 1);
					return;
				}
				r.writeCounter(lane, position, 0);
				++position;
			}
		}

		void optimizedGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			while (true)
			{
				r.addMetric("candidate_tuples");
				if (guessIndicesSorted(r, lane, false, nullptr)) break;
				advanceGuessIndices(r, lane, 0);
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void smartGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			while (true)
			{
				r.addMetric("candidate_tuples");
				std::size_t failure = NoGapIndex;
				if (guessIndicesSorted(r, lane, true, &failure)) break;
				advanceGuessIndices(r, lane, failure);
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void randomGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			std::mt19937 random(r.trace.inputSeed ^ 0x6A355EEDu);
			while (!guessIndicesSorted(r, lane, false, nullptr))
			{
				for (std::size_t i = 0; i < count; ++i)
			{
					std::uniform_int_distribution<int> choose(0, static_cast<int>(count - 1));
					r.writeCounter(lane, i, choose(random));
				}
				r.addMetric("random_guesses");
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void guessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			const std::string resultLane = "accepted_guess_indices";
			r.resizeCounters(lane, count);
			r.resizeCounters(resultLane, count);
			bool haveCandidate = false;
			while (true)
			{
				r.addMetric("candidate_tuples");
				std::uint64_t total = 0;
				for (std::size_t i = 0; i < count; ++i)
					for (std::size_t j = 0; j < count; ++j)
					{
						r.addMetric("index_pair_checks");
						if (r.counter(lane, i) == r.counter(lane, j)) ++total;
					}
				for (std::size_t i = 0; i < count; ++i)
					for (std::size_t j = 0; j < count; ++j)
					{
						const std::size_t leftIndex = static_cast<std::size_t>(r.counter(lane, i));
						const std::size_t rightIndex = static_cast<std::size_t>(r.counter(lane, j));
						const SortItem left = r.trace.input[leftIndex], right = r.trace.input[rightIndex];
						r.compare(left, right, leftIndex, rightIndex);
						const ComparisonResult expectedInversion = i < j ? ComparisonResult::Greater : ComparisonResult::Less;
						if (compareItems(left, right, r.trace.direction) == expectedInversion) ++total;
					}
				if (total == count)
				{
					haveCandidate = true;
					for (std::size_t i = 0; i < count; ++i)
						r.writeCounter(resultLane, i, r.counter(lane, i));
				}
				const std::size_t previousEvents = r.trace.events.size();
				advanceGuessIndices(r, lane, 0);
				if (r.counter(lane, 0) == 0 && r.trace.events.size() > previousEvents &&
					static_cast<std::size_t>(r.counter(lane, 1)) == 0)
				{
					bool allZero = true;
					for (std::size_t i = 0; i < count; ++i) allZero = allZero && r.counter(lane, i) == 0;
					if (allZero) break;
				}
			}
			require(haveCandidate, "Guess Sort exhausted its index tuples without finding a stable permutation.");
			commitGuessPermutation(r, resultLane);
			r.resizeCounters(lane, 0);
			r.resizeCounters(resultLane, 0);
		}

		SortItem itemInLane(const Recorder& r, const std::string& lane, std::size_t index)
		{
			return lane == "main" ? r.at(index) : r.auxiliaryAt(lane, index);
		}

		void shuffleLane(Recorder& r, const std::string& lane, std::size_t first, std::size_t end, std::mt19937& random)
		{
			for (std::size_t i = first; i < end; ++i)
			{
				r.chargeWork();
				std::uniform_int_distribution<std::size_t> choose(i, end - 1);
				const std::size_t other = choose(random);
				if (lane == "main") r.swap(i, other);
				else r.swapAuxiliary(lane, i, other);
			}
		}

		bool bogoBogoIsSorted(Recorder& r, const std::string& sourceLane, std::size_t length,
			std::size_t depth, std::mt19937& random);

		void bogoBogoSortAuxiliary(Recorder& r, const std::string& lane, std::size_t length,
			std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return;
			while (!bogoBogoIsSorted(r, lane, length, depth, random))
			{
				shuffleLane(r, lane, 0, length, random);
				r.addMetric("random_shuffles");
			}
		}

		bool bogoBogoIsSorted(Recorder& r, const std::string& sourceLane, std::size_t length,
			std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return true;
			r.chargeWork(length);
			r.addMetric("nested_checks");
			const std::string checkLane = "bogo_bogo_check_" + std::to_string(depth);
			r.resizeAuxiliary(checkLane, length);
			for (std::size_t i = 0; i < length; ++i)
				r.writeAuxiliary(checkLane, i, itemInLane(r, sourceLane, i));
			bogoBogoSortAuxiliary(r, checkLane, length - 1, depth + 1, random);
			while (true)
			{
				const SortItem left = r.auxiliaryAt(checkLane, length - 2);
				const SortItem right = r.auxiliaryAt(checkLane, length - 1);
				r.compare(left, right, length - 2, length - 1, checkLane, checkLane);
				if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
				shuffleLane(r, checkLane, 0, length, random);
				r.addMetric("random_shuffles");
				bogoBogoSortAuxiliary(r, checkLane, length - 1, depth + 1, random);
			}
			bool sameOrder = true;
			for (std::size_t i = 0; i < length; ++i)
			{
				const SortItem input = itemInLane(r, sourceLane, i);
				const SortItem ordered = r.auxiliaryAt(checkLane, i);
				r.compare(input, ordered, i, i, sourceLane, checkLane);
				if (compareItems(input, ordered, r.trace.direction) != ComparisonResult::Equal)
				{
					sameOrder = false;
					break;
				}
			}
			r.resizeAuxiliary(checkLane, 0);
			return sameOrder;
		}

		void bogoBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xB060B060u);
			while (!bogoBogoIsSorted(r, "main", count, 0, random))
			{
				shuffleLane(r, "main", 0, count, random);
				r.addMetric("random_shuffles");
			}
		}

		void smartBogoBogoPrefix(Recorder& r, std::size_t length, std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return;
			smartBogoBogoPrefix(r, length - 1, depth + 1, random);
			while (true)
			{
				r.chargeWork();
				const SortItem left = r.at(length - 2), right = r.at(length - 1);
				r.compare(left, right, length - 2, length - 1);
				if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
				shuffleLane(r, "main", 0, length, random);
				r.addMetric("random_shuffles");
				r.addMetric("recursive_prefix_retries");
				smartBogoBogoPrefix(r, length - 1, depth + 1, random);
			}
		}

		void smartBogoBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x5B060B06u);
			smartBogoBogoPrefix(r, r.state.slots.size(), 0, random);
		}

		void mergeBogoRange(Recorder& r, std::size_t first, std::size_t end, std::mt19937& random)
		{
			if (end - first < 2) return;
			const std::size_t middle = first + (end - first) / 2;
			mergeBogoRange(r, first, middle, random);
			mergeBogoRange(r, middle, end, random);
			const std::string lane = "merge_bogo";
			const std::size_t length = end - first;
			r.resizeAuxiliary(lane, length);
			for (std::size_t i = 0; i < length; ++i) r.writeAuxiliary(lane, i, r.at(first + i));
			const std::size_t annotationBegin = r.trace.events.size();
			r.annotate("main", RegionKind::Contiguous, first, middle, 1, -1, "Merge Bogo left run", annotationBegin);
			r.annotate("main", RegionKind::Contiguous, middle, end, 1, -1, "Merge Bogo right run", annotationBegin);
			while (true)
			{
				bool sorted = true;
				for (std::size_t i = first + 1; i < end; ++i)
				{
					r.chargeWork();
					const SortItem left = r.at(i - 1), right = r.at(i);
					r.compare(left, right, i - 1, i);
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) sorted = false;
				}
				if (sorted) break;
				std::vector<int> sources(length, 1);
				const std::size_t leftLength = middle - first;
				for (std::size_t i = 0; i < leftLength; ++i) sources[i] = 0;
				for (std::size_t i = length; i > 1; --i)
				{
					r.chargeWork();
					std::uniform_int_distribution<std::size_t> choose(0, i - 1);
					std::swap(sources[i - 1], sources[choose(random)]);
				}
				std::size_t left = 0, right = leftLength;
				std::vector<SortItem> weave;
				weave.reserve(length);
				for (std::size_t i = 0; i < length; ++i)
					weave.push_back(r.auxiliaryAt(lane, sources[i] == 0 ? left++ : right++));
				r.writeMainRange(first, weave);
				r.addMetric("random_weaves");
			}
			r.resizeAuxiliary(lane, 0);
		}

		void mergeBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x4D3B060u);
			if (r.state.slots.size() > 1) mergeBogoRange(r, 0, r.state.slots.size(), random);
		}

		void selectionBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0x5E1EC710u);
			for (std::size_t first = 0; first < count; ++first)
			{
				std::uniform_int_distribution<std::size_t> choose(first, count - 1);
				while (!suffixMinimumAt(r, first))
				{
					r.swap(first, choose(random), SemanticRole::CurrentMinimum);
					r.addMetric("random_attempts");
				}
			}
		}

		void lessBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0x1E55B060u);
			for (std::size_t first = 0; first < count; ++first)
			{
				while (!suffixMinimumAt(r, first))
				{
					for (std::size_t i = first; i < count; ++i)
					{
						std::uniform_int_distribution<std::size_t> choose(i, count - 1);
						r.swap(i, choose(random));
					}
					r.addMetric("random_shuffles");
				}
			}
		}

		void stalinOverwriteSort(Recorder& r)
		{
			r.declareMetric("value_rewrites");
			const std::size_t count = r.state.slots.size();
			for (std::size_t i = 1; i < count; ++i)
			{
				const SortItem previous = r.at(i - 1), current = r.at(i);
				r.compare(previous, current, i - 1, i);
				if (compareItems(previous, current, r.trace.direction) != ComparisonResult::Greater) continue;
				SortItem replacement = current;
				replacement.value = previous.value;
				replacement.integerKey = previous.integerKey;
				replacement.hasIntegerKey = previous.hasIntegerKey;
				r.replaceScalar(i, replacement);
				r.addMetric("value_rewrites");
			}
		}

		void canonicalStalinFilter(Recorder& r)
		{
			r.declareMetric("discarded_items");
			r.declareMetric("kept_items");
			std::size_t index = 1;
			while (index < r.state.slots.size())
			{
				const SortItem previous = r.at(index - 1), current = r.at(index);
				r.compare(previous, current, index - 1, index);
				if (compareItems(previous, current, r.trace.direction) == ComparisonResult::Greater)
				{
					r.discardMain(index);
					r.compactAfterDiscard(index);
					r.addMetric("discarded_items");
					continue;
				}
				++index;
			}
			r.setMetric("kept_items", r.state.slots.size());
			if (!r.state.slots.empty())
				r.annotate("main", RegionKind::Contiguous, 0, r.state.slots.size(), 1, -1,
					"Stalin Sort retained subsequence", 0);
		}

		struct TreeNode
		{
			SortItem item;
			std::size_t originalIndex;
			int left, right, parent, level, height, balance;
			bool red;
			TreeNode(const SortItem& value, std::size_t index)
				: item(value), originalIndex(index), left(-1), right(-1), parent(-1), level(0), height(1), balance(0), red(false) {}
		};

		void emitPermutationOrder(Recorder& r, const std::vector<std::size_t>& order,
			const std::string& auxiliaryLane, const std::string& phaseLabel)
		{
			const int phase = r.beginPhase(phaseLabel);
			for (std::size_t i = 0; i < order.size(); ++i)
			{
				if (!auxiliaryLane.empty()) r.probeAuxiliary(auxiliaryLane, order[i], SemanticRole::ProbedItem);
				else r.probeItem("main", order[i], r.trace.input[order[i]], SemanticRole::ProbedItem);
			}
			std::vector<std::size_t> currentPosition(order.size());
			for (std::size_t i = 0; i < order.size(); ++i) currentPosition[i] = i;
			for (std::size_t target = 0; target < order.size(); ++target)
			{
				const std::size_t source = currentPosition[order[target]];
				if (source != target)
				{
					std::size_t displacedSource = 0;
					while (displacedSource < order.size() && currentPosition[displacedSource] != target) ++displacedSource;
					r.swap(target, source, SemanticRole::WriteTarget);
					currentPosition[order[target]] = target;
					if (displacedSource < order.size()) currentPosition[displacedSource] = source;
				}
			}
			r.endPhase(phase, phaseLabel);
		}

		void setTreeParent(Recorder& r, std::vector<TreeNode>& nodes, int child, int parent)
		{
			if (child < 0 || nodes[static_cast<std::size_t>(child)].parent == parent) return;
			nodes[static_cast<std::size_t>(child)].parent = parent;
			r.writeCounter("tree_parent", static_cast<std::size_t>(child), parent < 0 ? 0 : parent + 1);
		}

		void setTreeChild(Recorder& r, std::vector<TreeNode>& nodes, std::size_t parent, bool left, int child)
		{
			const int oldChild = left ? nodes[parent].left : nodes[parent].right;
			const bool trackParents = r.state.integerWorkspaces.find("tree_parent") != r.state.integerWorkspaces.end();
			if (trackParents && oldChild >= 0 && oldChild != child && nodes[static_cast<std::size_t>(oldChild)].parent == static_cast<int>(parent))
				setTreeParent(r, nodes, oldChild, -1);
			if (left) nodes[parent].left = child;
			else nodes[parent].right = child;
			r.writeCounter(left ? "tree_left" : "tree_right", parent, child < 0 ? 0 : child + 1);
			if (trackParents && child >= 0) setTreeParent(r, nodes, child, static_cast<int>(parent));
		}

		void refreshAVLNode(Recorder& r, std::vector<TreeNode>& nodes, std::size_t index)
		{
			const int leftHeight = nodes[index].left < 0 ? 0 : nodes[static_cast<std::size_t>(nodes[index].left)].height;
			const int rightHeight = nodes[index].right < 0 ? 0 : nodes[static_cast<std::size_t>(nodes[index].right)].height;
			nodes[index].height = 1 + (std::max)(leftHeight, rightHeight);
			nodes[index].balance = leftHeight - rightHeight;
			r.writeCounter("tree_height", index, nodes[index].height);
			r.writeCounter("tree_balance", index, nodes[index].balance);
		}

		int rotateTreeRight(Recorder& r, std::vector<TreeNode>& nodes, int root)
		{
			const int pivot = nodes[static_cast<std::size_t>(root)].left;
			if (pivot < 0) throw std::logic_error("Right rotation requires a left child.");
			const int pivotRight = nodes[static_cast<std::size_t>(pivot)].right;
			setTreeChild(r, nodes, static_cast<std::size_t>(root), true, pivotRight);
			setTreeChild(r, nodes, static_cast<std::size_t>(pivot), false, root);
			if (r.state.integerWorkspaces.find("tree_balance") != r.state.integerWorkspaces.end())
			{
				refreshAVLNode(r, nodes, static_cast<std::size_t>(root));
				refreshAVLNode(r, nodes, static_cast<std::size_t>(pivot));
			}
			r.addMetric("tree_rotations");
			return pivot;
		}

		int rotateTreeLeft(Recorder& r, std::vector<TreeNode>& nodes, int root)
		{
			const int pivot = nodes[static_cast<std::size_t>(root)].right;
			if (pivot < 0) throw std::logic_error("Left rotation requires a right child.");
			const int pivotLeft = nodes[static_cast<std::size_t>(pivot)].left;
			setTreeChild(r, nodes, static_cast<std::size_t>(root), false, pivotLeft);
			setTreeChild(r, nodes, static_cast<std::size_t>(pivot), true, root);
			if (r.state.integerWorkspaces.find("tree_balance") != r.state.integerWorkspaces.end())
			{
				refreshAVLNode(r, nodes, static_cast<std::size_t>(root));
				refreshAVLNode(r, nodes, static_cast<std::size_t>(pivot));
			}
			r.addMetric("tree_rotations");
			return pivot;
		}

		void balancedTreeStorage(Recorder& r, std::size_t count)
		{
			r.resizeAuxiliary("tree_nodes", count);
			r.resizeCounters("tree_left", count);
			r.resizeCounters("tree_right", count);
			r.resizeCounters("tree_root", 1);
		}

		void balancedTreeInsertItems(Recorder& r, std::vector<TreeNode>& nodes, std::size_t count)
		{
			nodes.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				nodes.push_back(TreeNode(r.trace.input[i], i));
				r.writeAuxiliary("tree_nodes", i, r.trace.input[i]);
			}
		}

		std::vector<std::size_t> balancedTreeInOrder(Recorder& r, const std::vector<TreeNode>& nodes, int root)
		{
			std::vector<std::size_t> order, stack;
			std::vector<bool> seen(nodes.size(), false);
			int node = root;
			while (node >= 0 || !stack.empty())
			{
				while (node >= 0)
				{
					r.chargeWork();
					if (seen[static_cast<std::size_t>(node)])
						throw std::logic_error("Tree workspace contains a repeated child link.");
					seen[static_cast<std::size_t>(node)] = true;
					stack.push_back(static_cast<std::size_t>(node));
					node = nodes[static_cast<std::size_t>(node)].left;
				}
				node = static_cast<int>(stack.back()); stack.pop_back();
				order.push_back(nodes[static_cast<std::size_t>(node)].originalIndex);
				r.addMetric("tree_traversal_visits");
				node = nodes[static_cast<std::size_t>(node)].right;
			}
			return order;
		}

		int splayTreeKey(Recorder& r, std::vector<TreeNode>& nodes, int root, std::size_t itemIndex, std::size_t depth = 0)
		{
			r.chargeWork();
			if (root < 0) return root;
			if (depth > nodes.size() * 2 + 2) throw std::logic_error("Splay tree recursion detected a cycle in the child links.");
			const SortItem item = nodes[itemIndex].item;
			TreeNode& node = nodes[static_cast<std::size_t>(root)];
			r.compare(item, node.item, itemIndex, static_cast<std::size_t>(root), "main", "tree_nodes");
			const ComparisonResult relation = compareItems(item, node.item, r.trace.direction);
			if (relation == ComparisonResult::Less)
			{
				const int left = node.left;
				if (left < 0) return root;
				r.compare(item, nodes[static_cast<std::size_t>(left)].item, itemIndex, static_cast<std::size_t>(left), "main", "tree_nodes");
				const ComparisonResult childRelation = compareItems(item, nodes[static_cast<std::size_t>(left)].item, r.trace.direction);
				if (childRelation == ComparisonResult::Less)
				{
					const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(left)].left, itemIndex, depth + 1);
					setTreeChild(r, nodes, static_cast<std::size_t>(left), true, grandchild);
					root = rotateTreeRight(r, nodes, root);
				}
				else
				{
					const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(left)].right, itemIndex, depth + 1);
					setTreeChild(r, nodes, static_cast<std::size_t>(left), false, grandchild);
				if (nodes[static_cast<std::size_t>(root)].left >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(root)].left)].right >= 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(root), true,
							rotateTreeLeft(r, nodes, nodes[static_cast<std::size_t>(root)].left));
				}
				return nodes[static_cast<std::size_t>(root)].left < 0 ? root : rotateTreeRight(r, nodes, root);
			}
			const int right = node.right;
			if (right < 0) return root;
			r.compare(item, nodes[static_cast<std::size_t>(right)].item, itemIndex, static_cast<std::size_t>(right), "main", "tree_nodes");
			const ComparisonResult childRelation = compareItems(item, nodes[static_cast<std::size_t>(right)].item, r.trace.direction);
			if (childRelation == ComparisonResult::Less)
			{
				const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(right)].left, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(right), true, grandchild);
				if (nodes[static_cast<std::size_t>(root)].right >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(root)].right)].left >= 0)
					setTreeChild(r, nodes, static_cast<std::size_t>(root), false,
						rotateTreeRight(r, nodes, nodes[static_cast<std::size_t>(root)].right));
			}
			else
			{
				const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(right)].right, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(right), false, grandchild);
				root = rotateTreeLeft(r, nodes, root);
			}
			return nodes[static_cast<std::size_t>(root)].right < 0 ? root : rotateTreeLeft(r, nodes, root);
		}

		void splayTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				r.chargeWork();
				if (root < 0) root = static_cast<int>(i);
				else
				{
					root = splayTreeKey(r, nodes, root, i);
					const SortItem item = nodes[i].item;
					r.compare(item, nodes[static_cast<std::size_t>(root)].item, i, static_cast<std::size_t>(root), "main", "tree_nodes");
					const bool goLeft = compareItems(item, nodes[static_cast<std::size_t>(root)].item, r.trace.direction) == ComparisonResult::Less;
					if (goLeft)
					{
						const int oldLeft = nodes[static_cast<std::size_t>(root)].left;
						setTreeChild(r, nodes, i, true, oldLeft);
						setTreeChild(r, nodes, i, false, root);
						setTreeChild(r, nodes, static_cast<std::size_t>(root), true, -1);
					}
					else
					{
						const int oldRight = nodes[static_cast<std::size_t>(root)].right;
						setTreeChild(r, nodes, i, true, root);
						setTreeChild(r, nodes, i, false, oldRight);
						setTreeChild(r, nodes, static_cast<std::size_t>(root), false, -1);
					}
					root = static_cast<int>(i);
				}
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			std::vector<std::pair<int, std::size_t> > pending(1, std::make_pair(root, static_cast<std::size_t>(1)));
			std::vector<bool> seen(count, false);
			std::size_t maximumDepth = 0;
			while (!pending.empty())
			{
				r.chargeWork();
				const std::pair<int, std::size_t> entry = pending.back(); pending.pop_back();
				if (seen[static_cast<std::size_t>(entry.first)])
					throw std::logic_error("Splay tree workspace contains a repeated child link.");
				seen[static_cast<std::size_t>(entry.first)] = true;
				maximumDepth = (std::max)(maximumDepth, entry.second);
				const TreeNode& node = nodes[static_cast<std::size_t>(entry.first)];
				if (node.left >= 0) pending.push_back(std::make_pair(node.left, entry.second + 1));
				if (node.right >= 0) pending.push_back(std::make_pair(node.right, entry.second + 1));
			}
			r.setMetric("tree_height", maximumDepth);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "Splay tree in-order extraction");
		}

		void aaTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_skews");
			r.declareMetric("tree_splits");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_level", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			int root = -1;
			std::function<int(int, std::size_t)> insert = [&](int current, std::size_t itemIndex) -> int
			{
				r.chargeWork();
				if (current < 0) return static_cast<int>(itemIndex);
				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = insert(goLeft ? node.left : node.right, itemIndex);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, child);
				int adjusted = current;
				if (nodes[static_cast<std::size_t>(adjusted)].left >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(adjusted)].left)].level == nodes[static_cast<std::size_t>(adjusted)].level)
				{
					adjusted = rotateTreeRight(r, nodes, adjusted);
					r.addMetric("tree_skews");
				}
				const int right = nodes[static_cast<std::size_t>(adjusted)].right;
				if (right >= 0)
				{
					const int rightRight = nodes[static_cast<std::size_t>(right)].right;
					if (rightRight >= 0 && nodes[static_cast<std::size_t>(rightRight)].level == nodes[static_cast<std::size_t>(adjusted)].level)
					{
						adjusted = rotateTreeLeft(r, nodes, adjusted);
						++nodes[static_cast<std::size_t>(adjusted)].level;
						r.writeCounter("tree_level", static_cast<std::size_t>(adjusted), nodes[static_cast<std::size_t>(adjusted)].level);
						r.addMetric("tree_splits");
					}
				}
				return adjusted;
			};
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i);
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			int maximumLevel = 0;
			for (std::size_t i = 0; i < count; ++i) maximumLevel = (std::max)(maximumLevel, nodes[i].level);
			r.setMetric("tree_height", static_cast<std::uint64_t>(maximumLevel + 1));
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "AA tree in-order extraction");
		}

		void avlTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_height", count);
			r.resizeCounters("tree_balance", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			const auto nodeHeight = [&nodes](int node) -> int
				{ return node < 0 ? 0 : nodes[static_cast<std::size_t>(node)].height; };
			std::function<int(int, std::size_t)> insert = [&](int current, std::size_t itemIndex) -> int
			{
				r.chargeWork();
				if (current < 0) return static_cast<int>(itemIndex);
				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = insert(goLeft ? node.left : node.right, itemIndex);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, child);
				TreeNode& updated = nodes[static_cast<std::size_t>(current)];
				updated.height = 1 + (std::max)(nodeHeight(updated.left), nodeHeight(updated.right));
				updated.balance = nodeHeight(updated.left) - nodeHeight(updated.right);
				r.writeCounter("tree_height", static_cast<std::size_t>(current), updated.height);
				r.writeCounter("tree_balance", static_cast<std::size_t>(current), updated.balance);
				if (updated.balance > 1)
				{
					if (nodes[static_cast<std::size_t>(updated.left)].balance < 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(current), true, rotateTreeLeft(r, nodes, updated.left));
					return rotateTreeRight(r, nodes, current);
				}
				if (updated.balance < -1)
				{
					if (nodes[static_cast<std::size_t>(updated.right)].balance > 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(current), false, rotateTreeRight(r, nodes, updated.right));
					return rotateTreeLeft(r, nodes, current);
				}
				return current;
			};
			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i);
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			r.setMetric("tree_height", nodes[static_cast<std::size_t>(root)].height);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "AVL tree in-order extraction");
		}

		void classicTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (count == 0) return;
			r.resizeCounters("tree_lower", count);
			r.resizeCounters("tree_upper", count);
			r.setMetric("tree_height", 1);
			for (std::size_t i = 1; i < count; ++i)
			{
				std::size_t node = 0, depth = 1;
				while (true)
				{
					r.chargeWork();
					const SortItem item = r.at(i), parent = r.at(node);
					r.compare(item, parent, i, node, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					int& child = compareItems(item, parent, r.trace.direction) == ComparisonResult::Less
						? r.state.integerWorkspaces["tree_lower"][node] : r.state.integerWorkspaces["tree_upper"][node];
					if (child == 0)
					{
						r.writeCounter(compareItems(item, parent, r.trace.direction) == ComparisonResult::Less ? "tree_lower" : "tree_upper",
							node, static_cast<int>(i + 1));
						break;
					}
					node = static_cast<std::size_t>(child - 1);
					++depth;
				}
				r.setMetric("tree_height", static_cast<std::uint64_t>(std::max<std::size_t>(
					static_cast<std::size_t>(r.state.metrics.algorithm["tree_height"]), depth + 1)));
				r.addMetric("tree_nodes");
			}
			std::vector<std::size_t> order;
			std::vector<std::size_t> stack;
			std::size_t node = 0;
			while (node < count || !stack.empty())
			{
				while (node < count)
				{
					r.chargeWork();
					stack.push_back(node);
					const int left = r.state.integerWorkspaces["tree_lower"][node];
					node = left == 0 ? count : static_cast<std::size_t>(left - 1);
				}
				node = stack.back(); stack.pop_back();
				order.push_back(node);
				r.addMetric("tree_traversal_visits");
				const int right = r.state.integerWorkspaces["tree_upper"][node];
				node = right == 0 ? count : static_cast<std::size_t>(right - 1);
			}
			r.setMetric("tree_nodes", count);
			emitPermutationOrder(r, order, std::string(), "Classic tree in-order extraction");
		}

		void treeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (count == 0) return;
			std::vector<TreeNode> nodes;
			nodes.reserve(count);
			r.resizeAuxiliary("tree_nodes", count);
			r.resizeCounters("tree_left", count);
			r.resizeCounters("tree_right", count);
			std::size_t root = 0, maximumDepth = 1;
			for (std::size_t i = 0; i < count; ++i)
			{
				const SortItem item = r.at(i);
				nodes.push_back(TreeNode(item, i));
				r.writeAuxiliary("tree_nodes", i, item);
				if (i == 0) continue;
				std::size_t node = root, depth = 1;
				while (true)
				{
					r.chargeWork();
					TreeNode& parent = nodes[node];
					r.compare(item, parent.item, i, node, "main", "tree_nodes");
					int& child = compareItems(item, parent.item, r.trace.direction) == ComparisonResult::Less
						? parent.left : parent.right;
					const std::string lane = compareItems(item, parent.item, r.trace.direction) == ComparisonResult::Less
						? "tree_left" : "tree_right";
					if (child < 0)
					{
						child = static_cast<int>(i);
						r.writeCounter(lane, node, static_cast<int>(i + 1));
						break;
					}
					node = static_cast<std::size_t>(child);
					++depth;
				}
				maximumDepth = std::max(maximumDepth, depth + 1);
				r.setMetric("tree_height", maximumDepth);
			}
			std::vector<std::size_t> order;
			std::vector<std::size_t> stack;
			std::size_t node = root;
			while (node < count || !stack.empty())
			{
				while (node < count)
				{
					r.chargeWork();
					stack.push_back(node);
					node = nodes[node].left < 0 ? count : static_cast<std::size_t>(nodes[node].left);
				}
				node = stack.back(); stack.pop_back();
				order.push_back(nodes[node].originalIndex);
				r.addMetric("tree_traversal_visits");
				node = nodes[node].right < 0 ? count : static_cast<std::size_t>(nodes[node].right);
			}
			r.setMetric("tree_nodes", count);
			emitPermutationOrder(r, order, "tree_nodes", "Tree in-order extraction");
		}

		void strandSort(Recorder& r)
		{
			r.declareMetric("strands");
			r.declareMetric("strand_items");
			r.declareMetric("merge_comparisons");
			r.declareMetric("last_strand_length");
			const std::size_t count = r.state.slots.size();
			std::vector<std::size_t> remaining(count), sorted;
			for (std::size_t i = 0; i < count; ++i) remaining[i] = i;
			while (!remaining.empty())
			{
				r.chargeWork();
				r.resizeAuxiliary("strand", 0);
				std::vector<std::size_t> strand(1, remaining[0]), nextRemaining, merged;
				r.writeAuxiliary("strand", 0, r.trace.input[strand[0]]);
				for (std::size_t i = 1; i < remaining.size(); ++i)
				{
					const std::size_t candidate = remaining[i], tail = strand.back();
					const SortItem candidateItem = r.trace.input[candidate], tailItem = r.trace.input[tail];
					r.compare(candidateItem, tailItem, candidate, strand.size() - 1, "main", "strand");
					if (compareItems(candidateItem, tailItem, r.trace.direction) != ComparisonResult::Less)
					{
						strand.push_back(candidate);
						r.writeAuxiliary("strand", strand.size() - 1, candidateItem);
						r.addMetric("strand_items");
					}
					else nextRemaining.push_back(candidate);
				}
				r.addMetric("strands");
				r.setMetric("last_strand_length", strand.size());
				std::size_t strandIndex = 0, sortedIndex = 0;
				merged.reserve(strand.size() + sorted.size());
				while (strandIndex < strand.size() && sortedIndex < sorted.size())
				{
					const SortItem fresh = r.trace.input[strand[strandIndex]];
					const SortItem old = r.trace.input[sorted[sortedIndex]];
					r.compare(fresh, old, strandIndex, sortedIndex, "strand", "strand_sorted");
					r.addMetric("merge_comparisons");
					if (compareItems(fresh, old, r.trace.direction) == ComparisonResult::Less)
						merged.push_back(strand[strandIndex++]);
					else merged.push_back(sorted[sortedIndex++]);
				}
				while (strandIndex < strand.size()) merged.push_back(strand[strandIndex++]);
				while (sortedIndex < sorted.size()) merged.push_back(sorted[sortedIndex++]);
				sorted.swap(merged);
				r.resizeAuxiliary("strand_sorted", 0);
				for (std::size_t i = 0; i < sorted.size(); ++i)
					r.writeAuxiliary("strand_sorted", i, r.trace.input[sorted[i]]);
				remaining.swap(nextRemaining);
			}
			emitPermutationOrder(r, sorted, std::string(), "Strand merge output");
		}

		void setTreeColor(Recorder& r, std::vector<TreeNode>& nodes, int node, bool red)
		{
			if (node < 0 || nodes[static_cast<std::size_t>(node)].red == red) return;
			nodes[static_cast<std::size_t>(node)].red = red;
			r.writeCounter("tree_color", static_cast<std::size_t>(node), red ? 1 : 0);
			r.addMetric("tree_recolors");
		}

		void redBlackTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes"); r.declareMetric("tree_height");
			r.declareMetric("tree_rotations"); r.declareMetric("tree_recolors");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_parent", count); r.resizeCounters("tree_color", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			typedef std::pair<int, bool> InsertResult;
			const auto singleRotate = [&r, &nodes](int subtreeRoot, bool rotateLeft) -> int
			{
				const int newRoot = rotateLeft
					? rotateTreeLeft(r, nodes, subtreeRoot)
					: rotateTreeRight(r, nodes, subtreeRoot);
				setTreeColor(r, nodes, newRoot, false);
				setTreeColor(r, nodes, subtreeRoot, true);
				return newRoot;
			};
			std::function<InsertResult(int, std::size_t, std::size_t)> insert =
				[&](int current, std::size_t itemIndex, std::size_t depth) -> InsertResult
			{
				r.chargeWork();
				if (depth > count + 1) throw std::logic_error("Red-black insertion exceeded its recursion guard.");
				if (current < 0)
				{
					setTreeColor(r, nodes, static_cast<int>(itemIndex), true);
					return InsertResult(static_cast<int>(itemIndex), false);
				}

				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				if (!node.red && node.left >= 0 && node.right >= 0 &&
					nodes[static_cast<std::size_t>(node.left)].red && nodes[static_cast<std::size_t>(node.right)].red)
				{
					setTreeColor(r, nodes, current, true);
					setTreeColor(r, nodes, node.left, false);
					setTreeColor(r, nodes, node.right, false);
				}

				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = goLeft ? node.left : node.right;
				const InsertResult inserted = insert(child, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, inserted.first);

				if (inserted.second)
				{
					const int childRoot = goLeft
						? nodes[static_cast<std::size_t>(current)].left
						: nodes[static_cast<std::size_t>(current)].right;
					if (goLeft)
					{
						if (nodes[static_cast<std::size_t>(childRoot)].left >= 0 &&
							nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(childRoot)].left)].red)
							return InsertResult(singleRotate(current, false), false);
						const int pivot = singleRotate(childRoot, true);
						setTreeChild(r, nodes, static_cast<std::size_t>(current), true, pivot);
						return InsertResult(singleRotate(current, false), false);
					}
					if (nodes[static_cast<std::size_t>(childRoot)].right >= 0 &&
						nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(childRoot)].right)].red)
						return InsertResult(singleRotate(current, true), false);
					const int pivot = singleRotate(childRoot, false);
					setTreeChild(r, nodes, static_cast<std::size_t>(current), false, pivot);
					return InsertResult(singleRotate(current, true), false);
				}
				return InsertResult(current, nodes[static_cast<std::size_t>(current)].red &&
					nodes[static_cast<std::size_t>(inserted.first)].red);
			};

			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i, 1).first;
				setTreeColor(r, nodes, root, false);
				r.setMetric("tree_nodes", i + 1);
				r.writeCounter("tree_root", 0, root + 1);
			}
			std::vector<std::pair<int, std::size_t> > pending(1, std::make_pair(root, static_cast<std::size_t>(1)));
			std::vector<bool> seen(count, false);
			std::size_t maximumDepth = 0;
			while (!pending.empty())
			{
				r.chargeWork();
				const std::pair<int, std::size_t> entry = pending.back(); pending.pop_back();
				if (seen[static_cast<std::size_t>(entry.first)]) throw std::logic_error("Red-black tree contains a repeated child link.");
				seen[static_cast<std::size_t>(entry.first)] = true;
				maximumDepth = (std::max)(maximumDepth, entry.second);
				const TreeNode& node = nodes[static_cast<std::size_t>(entry.first)];
				if (node.red && ((node.left >= 0 && nodes[static_cast<std::size_t>(node.left)].red) ||
					(node.right >= 0 && nodes[static_cast<std::size_t>(node.right)].red)))
					throw std::logic_error("Red-black tree contains adjacent red nodes.");
				if (node.left >= 0) pending.push_back(std::make_pair(node.left, entry.second + 1));
				if (node.right >= 0) pending.push_back(std::make_pair(node.right, entry.second + 1));
			}
			std::function<int(int)> blackHeight = [&](int node) -> int
			{
				if (node < 0) return 1;
				const int leftHeight = blackHeight(nodes[static_cast<std::size_t>(node)].left);
				const int rightHeight = blackHeight(nodes[static_cast<std::size_t>(node)].right);
				if (leftHeight != rightHeight) throw std::logic_error("Red-black tree has unequal black heights.");
				return leftHeight + (nodes[static_cast<std::size_t>(node)].red ? 0 : 1);
			};
			blackHeight(root);
			r.setMetric("tree_height", maximumDepth);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "Red-black tree in-order extraction");
		}

		void shell(Recorder& r)
		{
			const std::vector<std::size_t> gaps = shellGaps(r.state.slots.size(), r.trace.shellGapSequence);
			for (std::size_t g = 0; g < gaps.size(); ++g)
			{
				const std::size_t gap = gaps[g];
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				const std::size_t begin = r.trace.events.size();
				for (std::size_t i = gap; i < r.state.slots.size(); ++i)
				{
					const SortItem key = r.at(i);
					r.hold(i);
					std::size_t position = i;
					while (position >= gap)
					{
						const SortItem previous = r.at(position - gap);
						r.compare(previous, key, position - gap, position, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
						r.shift(position - gap, position);
						position -= gap;
					}
					r.place(position, key);
				}
				r.annotate("main", RegionKind::Strided, 0, r.state.slots.size(), gap, -1,
					"Shell gap " + std::to_string(gap), begin);
			}
		}

		void binaryInsertionRange(Recorder& r, std::size_t first, std::size_t end)
		{
			for (std::size_t i = first + (first < end ? 1 : 0); i < end; ++i)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t low = first, high = i;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, i, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = i; position > low; --position) r.shift(position - 1, position);
				r.place(low, key);
			}
		}

		void binaryInsertion(Recorder& r)
		{
			binaryInsertionRange(r, 0, r.state.slots.size());
		}

		void simplifiedLibrarySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "simplified_library_rebalances", "simplified_library_gap_inserts",
				"simplified_library_binary_comparisons", "simplified_library_small_fallbacks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const std::size_t rebalanceFactor = 4;
			std::size_t minimumRun = count;
			while (minimumRun >= 32) minimumRun = (minimumRun - 1) / rebalanceFactor + 1;
			binaryInsertionRange(r, 0, minimumRun);
			if (count < 32)
			{
				r.addMetric("simplified_library_small_fallbacks");
				return;
			}

			std::size_t maxLevel = minimumRun;
			while (maxLevel * rebalanceFactor < count) maxLevel *= rebalanceFactor;
			r.resizeAuxiliary("simplified-library-items", count);
			r.resizeCounters("simplified-library-counts", maxLevel + 2);
			r.resizeCounters("simplified-library-locations", count - maxLevel);

			const auto rebalance = [&r](std::size_t sortedCount, std::size_t prefixEnd)
			{
				// Convert per-rank insertion counts to the dense boundaries used to
				// redistribute books; then place pending items before their insertion
				// successor, matching the source's stable upper-bound convention.
				for (std::size_t i = 0; i < sortedCount; ++i)
				{
					r.chargeWork();
					const int cumulative = r.counter("simplified-library-counts", i + 1) +
						r.counter("simplified-library-counts", i) + 1;
					r.writeCounter("simplified-library-counts", i + 1, cumulative);
				}
				std::vector<SortItem> dense(prefixEnd);
				std::vector<bool> assigned(prefixEnd, false);
				for (std::size_t i = sortedCount; i < prefixEnd; ++i)
				{
					r.chargeWork();
					const std::size_t location = static_cast<std::size_t>(r.counter("simplified-library-locations", i - sortedCount));
					const std::size_t position = static_cast<std::size_t>(r.counter("simplified-library-counts", location));
					require(location <= sortedCount && position < prefixEnd && !assigned[position],
						"Simplified Library Sort produced an invalid pending-item rank.");
					const SortItem item = r.at(i);
					r.writeAuxiliary("simplified-library-items", position, item);
					dense[position] = item;
					assigned[position] = true;
					r.writeCounter("simplified-library-counts", location,
						r.counter("simplified-library-counts", location) + 1);
				}
				for (std::size_t i = 0; i < sortedCount; ++i)
				{
					r.chargeWork();
					const std::size_t position = static_cast<std::size_t>(r.counter("simplified-library-counts", i));
					require(position < prefixEnd && !assigned[position],
						"Simplified Library Sort's rebalance rank overlaps an existing book.");
					const SortItem item = r.at(i);
					r.writeAuxiliary("simplified-library-items", position, item);
					dense[position] = item;
					assigned[position] = true;
					r.writeCounter("simplified-library-counts", i,
						r.counter("simplified-library-counts", i) + 1);
				}
				for (std::size_t i = 0; i < assigned.size(); ++i)
					require(assigned[i], "Simplified Library Sort left a hole in its rebuilt prefix.");
				r.writeMainRange(0, dense);

				const std::size_t firstEnd = static_cast<std::size_t>(r.counter("simplified-library-counts", 0));
				require(firstEnd <= prefixEnd, "Simplified Library Sort's first rebuilt shelf exceeds its prefix.");
				binaryInsertionRange(r, 0, firstEnd);
				for (std::size_t i = 0; i + 1 < sortedCount; ++i)
				{
					const std::size_t begin = static_cast<std::size_t>(r.counter("simplified-library-counts", i));
					const std::size_t end = static_cast<std::size_t>(r.counter("simplified-library-counts", i + 1));
					require(begin <= end && end <= prefixEnd, "Simplified Library Sort's rebuilt shelf bounds are invalid.");
					binaryInsertionRange(r, begin, end);
				}
				const std::size_t lastBegin = static_cast<std::size_t>(r.counter("simplified-library-counts", sortedCount - 1));
				const std::size_t lastEnd = static_cast<std::size_t>(r.counter("simplified-library-counts", sortedCount));
				require(lastBegin <= lastEnd && lastEnd <= prefixEnd, "Simplified Library Sort's final shelf bounds are invalid.");
				binaryInsertionRange(r, lastBegin, lastEnd);
				for (std::size_t i = 0; i < sortedCount + 2; ++i)
					r.writeCounter("simplified-library-counts", i, 0);
				r.addMetric("simplified_library_rebalances");
			};

			std::size_t insertionIndex = minimumRun, pendingCount = 0;
			for (std::size_t i = minimumRun; i < count; ++i)
			{
				r.chargeWork();
				if (minimumRun <= count / rebalanceFactor && minimumRun * rebalanceFactor == i)
				{
					rebalance(minimumRun, i);
					minimumRun = i;
					pendingCount = 0;
				}
				const SortItem key = r.at(i);
				std::size_t low = 0, high = minimumRun;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, i, middle, "main", "main", SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("simplified_library_binary_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				r.writeCounter("simplified-library-counts", low + 1,
					r.counter("simplified-library-counts", low + 1) + 1);
				r.writeCounter("simplified-library-locations", pendingCount++, static_cast<int>(low));
				++insertionIndex;
				r.addMetric("simplified_library_gap_inserts");
			}
			(void)insertionIndex;
			rebalance(minimumRun, count);
			r.resizeAuxiliary("simplified-library-items", 0);
			r.resizeCounters("simplified-library-counts", 0);
			r.resizeCounters("simplified-library-locations", 0);
		}

		void librarySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "library_gap_inserts", "library_block_search_comparisons",
				"library_local_search_comparisons", "library_tie_choices", "library_spills",
				"library_rebalances", "library_prefix_retrievals", "library_small_fallbacks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const std::size_t blockWidth = 16;
			const std::size_t rebalanceFactor = 4;
			std::size_t sortedPrefix = count;
			while (sortedPrefix >= 32) sortedPrefix = (sortedPrefix - 1) / rebalanceFactor + 1;
			binaryInsertionRange(r, 0, sortedPrefix);
			if (count < 32)
			{
				r.addMetric("library_small_fallbacks");
				return;
			}

			struct ShelfValue
			{
				SortItem item;
				std::size_t cell;
				ShelfValue(const SortItem& value, std::size_t position) : item(value), cell(position) {}
			};
			struct ShelfBlock { std::vector<ShelfValue> values; };
			const std::string laneName("library-shelves");
			const std::size_t blockCount = count + 1;
			const std::size_t workspaceSize = blockCount * blockWidth;
			std::vector<ShelfBlock> blocks(blockCount);
			std::size_t activeBlocks = 0;
			std::mt19937 tieRandom(static_cast<std::uint32_t>(r.trace.inputSeed ^ 0x4C494252u));
			r.resizeAuxiliary(laneName, workspaceSize);

			const auto blockStart = [&blockWidth](std::size_t block) { return block * blockWidth; };
			const auto findFreeCell = [&blocks, &blockWidth, &blockStart](std::size_t block)
			{
				for (std::size_t offset = 0; offset < blockWidth; ++offset)
				{
					bool occupied = false;
					const std::size_t cell = blockStart(block) + offset;
					for (std::size_t i = 0; i < blocks[block].values.size(); ++i)
						if (blocks[block].values[i].cell == cell) { occupied = true; break; }
					if (!occupied) return cell;
				}
				return NoGapIndex;
			};
			const auto swapCells = [&r, &laneName](std::size_t first, std::size_t second)
			{
				if (first == second) return;
				SemanticEvent event;
				event.type = SemanticEventType::Swap;
				event.firstIndex = first;
				event.secondIndex = second;
				event.firstLane = laneName;
				event.secondLane = laneName;
				event.firstRole = SemanticRole::AuxiliaryCopy;
				event.secondRole = SemanticRole::WriteTarget;
				const std::vector<SortSlot>& cells = r.state.auxiliaryItems.find(laneName)->second;
				if (cells[first].occupied) event.firstItemId = cells[first].item.stableId;
				if (cells[second].occupied) event.secondItemId = cells[second].item.stableId;
				r.append(event);
			};
			const auto normalizeBlock = [&blocks, &blockWidth, &blockStart, &swapCells](std::size_t block)
			{
				std::vector<ShelfValue>& values = blocks[block].values;
				const std::size_t base = blockStart(block);
				for (std::size_t index = 0; index < values.size(); ++index)
				{
					const std::size_t target = base + ((index + 1) * blockWidth) / (values.size() + 1);
					if (values[index].cell == target) continue;
					std::size_t occupant = values.size();
					for (std::size_t other = 0; other < values.size(); ++other)
						if (values[other].cell == target) { occupant = other; break; }
					const std::size_t previous = values[index].cell;
					swapCells(previous, target);
					values[index].cell = target;
					if (occupant < values.size()) values[occupant].cell = previous;
				}
			};
			const auto recordInsert = [&r, &blocks, &laneName, &findFreeCell, &normalizeBlock]
				(std::size_t block, const SortItem& item, std::size_t position)
			{
				const std::size_t cell = findFreeCell(block);
				require(cell != NoGapIndex, "Library Sort attempted to insert into a full shelf block.");
				r.writeAuxiliary(laneName, cell, item);
				blocks[block].values.insert(blocks[block].values.begin() + static_cast<std::ptrdiff_t>(position), ShelfValue(item, cell));
				normalizeBlock(block);
			};
			const auto moveLargestRight = [&r, &blocks, &laneName, &findFreeCell, &normalizeBlock, &swapCells]
				(std::size_t source, std::size_t destination)
			{
				require(!blocks[source].values.empty() && blocks[destination].values.size() < 16,
					"Library Sort boundary spill requires a populated source and an open destination slot.");
				const ShelfValue item = blocks[source].values.back();
				const std::size_t destinationCell = findFreeCell(destination);
				require(destinationCell != NoGapIndex, "Library Sort spill destination unexpectedly has no gap.");
				swapCells(item.cell, destinationCell);
				blocks[source].values.pop_back();
				blocks[destination].values.insert(blocks[destination].values.begin(), ShelfValue(item.item, destinationCell));
				normalizeBlock(source);
				normalizeBlock(destination);
				r.addMetric("library_spills");
			};
			const auto makeRoom = [&blocks, &activeBlocks, &blockCount, &moveLargestRight](std::size_t target)
			{
				if (blocks[target].values.size() < 16) return;
				std::size_t freeBlock = target + 1;
				while (freeBlock < blockCount && blocks[freeBlock].values.size() >= 16) ++freeBlock;
				require(freeBlock < blockCount, "Library Sort exhausted its extra gap blocks.");
				for (std::size_t cursor = freeBlock; cursor > target; --cursor)
					moveLargestRight(cursor - 1, cursor);
				activeBlocks = std::max(activeBlocks, freeBlock + 1);
			};

			for (std::size_t i = 0; i < sortedPrefix; ++i)
			{
				const std::size_t cell = blockStart(i) + blockWidth - 1;
				const SortItem item = r.at(i);
				r.writeAuxiliary(laneName, cell, item);
				blocks[i].values.push_back(ShelfValue(item, cell));
			}
			activeBlocks = sortedPrefix;

			const auto retrieve = [&r, &blocks, &activeBlocks, &sortedPrefix, &laneName,
				&blockWidth, &blockCount, &blockStart, &workspaceSize](std::size_t prefixEnd, bool rebuild)
			{
				std::vector<SortItem> sorted;
				sorted.reserve(prefixEnd);
				for (std::size_t block = 0; block < activeBlocks; ++block)
					for (std::size_t item = 0; item < blocks[block].values.size(); ++item)
						sorted.push_back(blocks[block].values[item].item);
				require(sorted.size() == prefixEnd, "Library Sort shelf count does not match its sorted prefix.");
				r.writeMainRange(0, sorted);
				r.addMetric("library_prefix_retrievals");
				r.resizeAuxiliary(laneName, 0);
				if (!rebuild) return;
				r.resizeAuxiliary(laneName, workspaceSize);
				for (std::size_t block = 0; block < blockCount; ++block) blocks[block].values.clear();
				for (std::size_t i = 0; i < sorted.size(); ++i)
				{
					const std::size_t cell = blockStart(i) + blockWidth - 1;
					r.writeAuxiliary(laneName, cell, sorted[i]);
					blocks[i].values.push_back(ShelfValue(sorted[i], cell));
				}
				activeBlocks = sorted.size();
				sortedPrefix = prefixEnd;
				r.addMetric("library_rebalances");
			};
			const auto selectShelf = [&r, &blocks, &activeBlocks, &tieRandom, &laneName]
				(const SortItem& key, std::size_t keyIndex)
			{
				std::size_t block = 0, high = activeBlocks;
				while (block < high)
				{
					const std::size_t middle = block + (high - block) / 2;
					const ShelfValue& maximum = blocks[middle].values.back();
					r.compare(key, maximum.item, keyIndex, maximum.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("library_block_search_comparisons");
					if (compareItems(key, maximum.item, r.trace.direction) != ComparisonResult::Greater) high = middle;
					else block = middle + 1;
				}
				if (block == activeBlocks) block = activeBlocks - 1;

				const auto equalsShelfMaximum = [&r, &blocks, &laneName, &key, keyIndex](std::size_t candidateBlock)
				{
					const ShelfValue& maximum = blocks[candidateBlock].values.back();
					r.compare(key, maximum.item, keyIndex, maximum.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					return compareItems(key, maximum.item, r.trace.direction) == ComparisonResult::Equal;
				};
				if (equalsShelfMaximum(block))
				{
					std::size_t firstEqual = block, lastEqual = block + 1;
					while (firstEqual > 0 && equalsShelfMaximum(firstEqual - 1)) --firstEqual;
					while (lastEqual < activeBlocks && equalsShelfMaximum(lastEqual)) ++lastEqual;
					if (lastEqual - firstEqual > 1)
					{
						std::uniform_int_distribution<std::size_t> chooseBlock(firstEqual, lastEqual - 1);
						block = chooseBlock(tieRandom);
						r.addMetric("library_tie_choices");
					}
				}
				return block;
			};

			std::size_t nextRebalance = sortedPrefix * rebalanceFactor;
			for (std::size_t i = sortedPrefix; i < count; ++i)
			{
				r.chargeWork();
				if (i == nextRebalance)
				{
					retrieve(i, true);
					nextRebalance *= rebalanceFactor;
				}
				const SortItem key = r.at(i);
				std::size_t block = 0;
				for (;;)
				{
					block = selectShelf(key, i);
					if (blocks[block].values.size() < blockWidth) break;
					makeRoom(block);
				}
				std::size_t low = 0, upper = blocks[block].values.size();
				while (low < upper)
				{
					const std::size_t middle = low + (upper - low) / 2;
					const ShelfValue& candidate = blocks[block].values[middle];
					r.compare(key, candidate.item, i, candidate.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("library_local_search_comparisons");
					if (compareItems(key, candidate.item, r.trace.direction) == ComparisonResult::Less) upper = middle;
					else low = middle + 1;
				}
				recordInsert(block, key, low);
				r.addMetric("library_gap_inserts");
				activeBlocks = std::max(activeBlocks, block + 1);
			}
			retrieve(count, false);
			r.resizeAuxiliary(laneName, 0);
		}

		std::size_t doubleInsertionBound(Recorder& r, const SortItem& key, std::size_t first,
			std::size_t end, bool upperBound, bool binarySearch, std::size_t keyIndex)
		{
			if (binarySearch)
			{
				std::size_t low = first, high = end;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					const ComparisonResult order = compareItems(key, candidate, r.trace.direction);
					if (order == ComparisonResult::Less || (!upperBound && order == ComparisonResult::Equal)) high = middle;
					else low = middle + 1;
				}
				return low;
			}

			std::size_t position = first;
			while (position < end)
			{
				const SortItem candidate = r.at(position);
				r.compare(candidate, key, position, keyIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
				const ComparisonResult order = compareItems(candidate, key, r.trace.direction);
				if (upperBound ? order == ComparisonResult::Greater : order != ComparisonResult::Less) break;
				++position;
			}
			return position;
		}

		void insertDoubleKeyFromLeft(Recorder& r, std::size_t source, std::size_t end,
			bool binarySearch)
		{
			const SortItem key = r.at(source);
			r.hold(source);
			const std::size_t targetBoundary = doubleInsertionBound(r, key, source + 1, end, false, binarySearch, source);
			const std::size_t target = targetBoundary - 1;
			for (std::size_t position = source; position < target; ++position)
				r.shift(position + 1, position);
			r.place(target, key);
		}

		void insertDoubleKeyFromRight(Recorder& r, std::size_t source, std::size_t first,
			bool binarySearch)
		{
			const SortItem key = r.at(source);
			r.hold(source);
			const std::size_t target = doubleInsertionBound(r, key, first, source, true, binarySearch, source);
			for (std::size_t position = source; position > target; --position)
				r.shift(position - 1, position);
			r.place(target, key);
		}

		void doubleInsertion(Recorder& r, bool binarySearch)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;

			std::size_t left = count / 2 - 1;
			std::size_t right = left + 1;
			const SortItem centerLeft = r.at(left), centerRight = r.at(right);
			r.compare(centerLeft, centerRight, left, right);
			if (compareItems(centerLeft, centerRight, r.trace.direction) == ComparisonResult::Greater)
				r.swap(left, right);

			while (left > 0 && right + 1 < count)
			{
				--left;
				++right;
				const SortItem leftItem = r.at(left), rightItem = r.at(right);
				r.compare(leftItem, rightItem, left, right);
				if (compareItems(leftItem, rightItem, r.trace.direction) == ComparisonResult::Greater)
				{
					insertDoubleKeyFromRight(r, right, left + 1, binarySearch);
					insertDoubleKeyFromLeft(r, left, right + 1, binarySearch);
				}
				else
				{
					insertDoubleKeyFromLeft(r, left, right, binarySearch);
					insertDoubleKeyFromRight(r, right, left + 1, binarySearch);
				}
			}

			if (right + 1 < count)
				insertDoubleKeyFromRight(r, right + 1, 0, binarySearch);
		}

		void recursiveShellRange(Recorder& r, std::size_t start, std::size_t gap)
		{
			const std::size_t n = r.state.slots.size();
			if (start + gap > n) return;
			recursiveShellRange(r, start, gap * 3);
			recursiveShellRange(r, start + gap, gap * 3);
			recursiveShellRange(r, start + 2 * gap, gap * 3);
			const std::size_t begin = r.trace.events.size();
			r.addMetric("gap_passes");
			r.setMetric("last_gap", gap);
			for (std::size_t i = start + gap; i < n; i += gap)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t position = i;
				while (position >= start + gap)
				{
					const std::size_t previousIndex = position - gap;
					const SortItem previous = r.at(previousIndex);
					r.compare(previous, key, previousIndex, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
					if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
					r.shift(previousIndex, position);
					position -= gap;
				}
				r.place(position, key);
			}
			r.annotate("main", RegionKind::Strided, start, n, gap, -1,
				"Recursive Shell residue " + std::to_string(start) + ", gap " + std::to_string(gap), begin);
		}

		void recursiveShell(Recorder& r)
		{
			if (!r.state.slots.empty()) recursiveShellRange(r, 0, 1);
		}

		void shellSortParallel(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "gap_passes", "last_gap", "parallel_tasks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			const std::size_t extendedCiura[] = { 8861, 3938, 1750, 701, 301, 132, 57, 23, 10, 4, 1 };
			for (std::size_t g = 0; g < sizeof(extendedCiura) / sizeof(extendedCiura[0]); ++g)
			{
				const std::size_t gap = extendedCiura[g];
				if (gap >= count) continue;
				const std::size_t tasks = std::min(gap, count - gap);
				const int phase = r.beginPhase("Parallel Shell gap " + std::to_string(gap));
				r.addMetric("gap_passes");
				r.setMetric("last_gap", gap);
				for (std::size_t residue = 0; residue < tasks; ++residue)
				{
					const std::size_t begin = r.trace.events.size();
					for (std::size_t i = residue + gap; i < count; i += gap)
					{
						const SortItem key = r.at(i), previous = r.at(i - gap);
						r.compare(previous, key, i - gap, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) continue;
						r.hold(i);
						std::size_t position = i;
						for (;;)
						{
						const std::size_t source = position - gap;
						r.shift(source, position);
						position = source;
						if (position < residue + gap) break;
						const std::size_t candidateIndex = position - gap;
						const SortItem candidate = r.at(candidateIndex);
						r.compare(candidate, key, candidateIndex, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(candidate, key, r.trace.direction) != ComparisonResult::Greater) break;
					}
					 r.place(position, key);
					}
					r.addMetric("parallel_tasks");
					r.annotate("main", RegionKind::Strided, residue, count, gap, phase,
						"Parallel Shell residue " + std::to_string(residue), begin);
				}
				r.endPhase(phase, "Parallel Shell gap " + std::to_string(gap));
			}
		}

		void introCircleSort(Recorder& r, bool recursiveVariant)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("circle_passes");
			r.declareMetric("binary_fallbacks");
			if (count < 2) return;
			std::size_t power = 1;
			std::size_t threshold = 0;
			while (power < count)
			{
				power *= 2;
				++threshold;
			}
			threshold /= 2;
			std::size_t iterations = 0;
			for (;;)
			{
				++iterations;
				if (iterations >= threshold)
				{
					r.addMetric("binary_fallbacks");
					binaryInsertionRange(r, 0, count);
					break;
				}
				std::size_t swaps = 0;
				if (recursiveVariant)
					circleSortRecursivePass(r, 0, nextPowerOfTwo(count) - 1, swaps);
				else
				{
					const std::size_t paddedLength = nextPowerOfTwo(count);
					for (std::size_t gap = paddedLength / 2; gap > 0; gap /= 2)
					{
						const std::size_t width = gap * 2;
						for (std::size_t start = 0; start < count; start += width)
						{
							std::size_t left = start, right = start + width - 1;
							while (left < right)
							{
								if (right < count)
								{
									const SortItem a = r.at(left), b = r.at(right);
									r.compare(a, b, left, right);
									if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
										{ r.swap(left, right); ++swaps; }
								}
								++left;
								--right;
							}
						}
					}
				}
				r.addMetric("circle_passes");
				if (swaps == 0) break;
			}
		}

		void mergeRange(Recorder& r, std::size_t first, std::size_t middle, std::size_t last)
		{
			if (middle >= last) return;
			const std::size_t begin = r.trace.events.size();
			for (std::size_t i = first; i < last; ++i) r.writeAuxiliary("merge", i, r.at(i));
			std::vector<SortItem> merged;
			if (r.trace.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite)
				merged.reserve(last - first);
			std::size_t left = first, right = middle;
			for (std::size_t out = first; out < last; ++out)
			{
				SortItem chosen;
				if (left >= middle) chosen = r.auxiliaryAt("merge", right++);
				else if (right >= last) chosen = r.auxiliaryAt("merge", left++);
				else
				{
					const SortItem a = r.auxiliaryAt("merge", left), b = r.auxiliaryAt("merge", right);
					r.compare(a, b, left, right, "merge", "merge");
					if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
						chosen = r.auxiliaryAt("merge", left++);
					else chosen = r.auxiliaryAt("merge", right++);
				}
				if (r.trace.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite) merged.push_back(chosen);
				else r.writeMain(out, chosen);
			}
			if (r.trace.mergeWritebackMode == MergeWritebackMode::AtomicRangeWrite)
				r.writeMainRange(first, merged);
			r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Merge range", begin);
		}

		void mergeSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			r.resizeAuxiliary("merge", n);
			if (r.trace.mergeExecutionMode == MergeExecutionMode::TopDown)
			{
				std::size_t maximumDepth = 0;
				for (std::size_t span = n; span > 1; span = (span + 1) / 2) ++maximumDepth;
				std::function<void(std::size_t, std::size_t, std::size_t)> split;
				split = [&r, &split, maximumDepth](std::size_t first, std::size_t last, std::size_t depth)
				{
					if (last - first < 2) return;
					const std::size_t middle = first + (last - first) / 2;
					split(first, middle, depth + 1);
					split(middle, last, depth + 1);
					mergeRange(r, first, middle, last);
					const std::uint64_t pass = static_cast<std::uint64_t>(maximumDepth - depth);
					r.setMetric("merge_passes", std::max(r.state.metrics.algorithm["merge_passes"], pass));
				};
				split(0, n, 0);
				return;
			}
			for (std::size_t width = 1; width < n; width *= 2)
			{
				r.addMetric("merge_passes");
				for (std::size_t first = 0; first < n; first += width * 2)
				{
					const std::size_t middle = std::min(first + width, n);
					const std::size_t last = std::min(first + width * 2, n);
					mergeRange(r, first, middle, last);
				}
				if (width > n / 2) break;
			}
		}

		void weaveMergeRange(Recorder& r, std::size_t first, std::size_t last)
		{
			if (last <= first) return;
			if (last - first == 1)
			{
				const SortItem left = r.at(first), right = r.at(last);
				r.compare(left, right, first, last, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric("weave_base_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(first, last);
					r.addMetric("weave_base_swaps");
				}
				return;
			}

			const std::size_t middle = first + (last - first) / 2;
			weaveMergeRange(r, first, middle);
			weaveMergeRange(r, middle + 1, last);
			r.addMetric("weave_recursive_merges");
			const std::size_t mergeBegin = r.trace.events.size();

			// Move each right-run item left into its alternating position. Adjacent
			// swaps preserve the relative order of the cells shifted by the weave.
			for (std::size_t offset = 1; offset <= middle - first; ++offset)
			{
				std::size_t source = middle + offset;
				const std::size_t target = first + offset * 2 - 1;
				while (source > target)
				{
					r.swap(source - 1, source, SemanticRole::WriteTarget);
					--source;
					r.addMetric("weave_interleave_swaps");
				}
			}

			// ArrayV's weave kernel finishes each interleaved range with a leftward
			// insertion pass rather than a conventional two-way merge.
			for (std::size_t index = first; index <= last; ++index)
			{
				std::size_t position = index;
				while (position > first)
				{
					const SortItem current = r.at(position), previous = r.at(position - 1);
					r.compare(current, previous, position, position - 1, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ComparedItem);
					r.addMetric("weave_insertion_comparisons");
					if (compareItems(current, previous, r.trace.direction) == ComparisonResult::Greater) break;
					r.swap(position - 1, position, SemanticRole::HeldKey);
					--position;
					r.addMetric("weave_insertion_swaps");
				}
			}
			r.annotate("main", RegionKind::Contiguous, first, middle + 1, 1, -1, "Weave left run", mergeBegin);
			r.annotate("main", RegionKind::Contiguous, middle + 1, last + 1, 1, -1, "Weave right run", mergeBegin);
		}

		void weaveMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "weave_recursive_merges", "weave_interleave_swaps", "weave_insertion_comparisons",
				"weave_insertion_swaps", "weave_base_comparisons", "weave_base_swaps" }) r.declareMetric(key);
			if (count > 1) weaveMergeRange(r, 0, count - 1);
		}

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

		void grailSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "grail_runs", "grail_merges", "grail_buffered_merges", "grail_rotation_merges",
				"grail_buffer_writes", "grail_comparisons" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t bufferCapacity = 0;
			if (r.trace.bucketCount == 1) bufferCapacity = (std::min)(std::size_t(512), count / 2);
			else if (r.trace.bucketCount == 2) bufferCapacity = static_cast<std::size_t>(std::sqrt(static_cast<double>(count)));
			if (bufferCapacity > 0) r.resizeAuxiliary("grail_buffer", bufferCapacity);
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < count; first += runSize)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
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
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
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
					binaryInsertionRange(r, first, targetEnd);
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

		void sqrtMergeSort(Recorder& r, bool synchronous)
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
				binaryInsertionRange(r, first, last);
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

		void stableKWayMerge(Recorder& r, const std::vector<std::size_t>& boundaries, const std::string& lane,
			const std::string& metricPrefix, const std::string& phaseLabel);

		void ectaSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "ecta_min_run", "ecta_insertion_runs", "ecta_four_way_groups", "ecta_four_way_merges",
				"ecta_four_way_comparisons", "ecta_four_way_auxiliary_writes", "ecta_four_way_passes",
				"ecta_binary_merges", "ecta_binary_passes" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t minRun = count;
			while (minRun >= 32) minRun = (minRun + 1) / 2;
			minRun = (std::max)(std::size_t(1), minRun);
			r.setMetric("ecta_min_run", static_cast<std::uint64_t>(minRun));
			r.resizeAuxiliary("ecta_buffer", count);
			for (std::size_t first = 0; first < count; first += minRun)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + minRun));
				r.addMetric("ecta_insertion_runs");
			}

			std::size_t width = minRun;
			while (width <= count / 4)
			{
				const int phase = r.beginPhase("Ecta four-way ping-pong layer width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 4)
				{
					std::vector<std::size_t> boundaries;
					boundaries.reserve(5);
					for (std::size_t part = 0; part <= 4; ++part)
						boundaries.push_back((std::min)(count, first + width * part));
					std::size_t active = 0;
					for (std::size_t part = 0; part < 4; ++part)
						if (boundaries[part] < boundaries[part + 1]) ++active;
					if (active > 1)
					{
						stableKWayMerge(r, boundaries, "ecta_buffer", "ecta_four_way", "Ecta four-way block merge");
						r.addMetric("ecta_four_way_groups");
						r.addMetric("ecta_four_way_merges");
					}
				}
				r.addMetric("ecta_four_way_passes");
				r.endPhase(phase, "Ecta four-way ping-pong layer");
				if (width > count / 16)
				{
					width *= 4;
					break;
				}
				width *= 4;
			}

			r.resizeAuxiliary("merge", count);
			while (width < count)
			{
				const int phase = r.beginPhase("Ecta binary merge tail width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
					mergeRange(r, first, middle, last);
						r.addMetric("ecta_binary_merges");
					}
				}
				r.addMetric("ecta_binary_passes");
				r.endPhase(phase, "Ecta binary merge tail");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("merge", 0);
			r.resizeAuxiliary("ecta_buffer", 0);
		}

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
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
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
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
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

		void lazyStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "lazy_pair_comparisons", "lazy_pair_swaps", "lazy_merge_comparisons", "lazy_rotation_swaps", "lazy_merge_passes" })
				r.declareMetric(key);
			for (std::size_t index = 1; index < count; index += 2)
			{
				const SortItem left = r.at(index - 1), right = r.at(index);
				r.compare(left, right, index - 1, index, "main", "main");
				r.addMetric("lazy_pair_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(index - 1, index);
					r.addMetric("lazy_pair_swaps");
				}
			}
			for (std::size_t part = 2; part < count;)
			{
				const int phase = r.beginPhase("Lazy stable merge pass");
				std::size_t left = 0;
				while (left + 2 * part <= count)
				{
					grailMergeWithoutBuffer(r, left, part, part, "lazy");
					left += 2 * part;
				}
				const std::size_t remainder = count - left;
				if (remainder > part) grailMergeWithoutBuffer(r, left, part, remainder - part, "lazy");
				r.addMetric("lazy_merge_passes");
				r.endPhase(phase, "Lazy stable merge pass");
				if (part > count / 2) break;
				part *= 2;
			}
		}

		void optimizedLazyStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_lazy_runs", "optimized_lazy_run_reversals", "optimized_lazy_insertion_comparisons",
				"optimized_lazy_shifts", "optimized_lazy_merge_comparisons", "optimized_lazy_rotation_swaps", "optimized_lazy_merge_passes" })
				r.declareMetric(key);
			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem left = r.at(first), right = r.at(second);
				r.compare(left, right, first, second, "main", "main");
				r.addMetric("optimized_lazy_insertion_comparisons");
				return compareItems(left, right, r.trace.direction);
			};
			const auto insertionRun = [&r, &compareAt](std::size_t first, std::size_t last)
			{
				if (last - first < 2) return;
				std::size_t index = first + 1;
				if (compareAt(first, index) == ComparisonResult::Greater)
				{
					while (index < last && compareAt(index - 1, index) == ComparisonResult::Greater) ++index;
					for (std::size_t left = first, right = index; left < right && left < --right; ++left)
					{
						r.swap(left, right, SemanticRole::WriteTarget);
						r.addMetric("optimized_lazy_run_reversals");
					}
				}
				else
				{
					while (index < last && compareAt(index - 1, index) != ComparisonResult::Greater) ++index;
				}
				for (; index < last; ++index)
				{
					const SortItem key = r.at(index);
					r.hold(index);
					std::size_t position = index;
					while (position > first)
					{
						const SortItem previous = r.at(position - 1);
						r.compare(key, previous, index, position - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
						r.addMetric("optimized_lazy_insertion_comparisons");
						if (compareItems(key, previous, r.trace.direction) != ComparisonResult::Less) break;
						r.shift(position - 1, position);
						r.addMetric("optimized_lazy_shifts");
						--position;
					}
					r.place(position, key);
				}
				r.addMetric("optimized_lazy_runs");
			};

			for (std::size_t first = 0; first < count; first += 16)
			{
				const std::size_t last = (std::min)(count, first + 16);
				const std::size_t begin = r.trace.events.size();
				insertionRun(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Optimized lazy insertion run", begin);
			}
			for (std::size_t part = 16; part < count;)
			{
				const int phase = r.beginPhase("Optimized lazy merge pass");
				std::size_t left = 0;
				while (left + 2 * part <= count)
				{
					grailMergeWithoutBuffer(r, left, part, part, "optimized_lazy");
					left += 2 * part;
				}
				const std::size_t remainder = count - left;
				if (remainder > part) grailMergeWithoutBuffer(r, left, part, remainder - part, "optimized_lazy");
				r.addMetric("optimized_lazy_merge_passes");
				r.endPhase(phase, "Optimized lazy merge pass");
				if (part > count / 2) break;
				part *= 2;
			}
		}

		void stableKWayMerge(Recorder& r, const std::vector<std::size_t>& boundaries, const std::string& lane,
			const std::string& metricPrefix, const std::string& phaseLabel)
		{
			if (boundaries.size() < 3 || boundaries.front() == boundaries.back()) return;
			const std::size_t first = boundaries.front(), last = boundaries.back();
			const std::size_t runCount = boundaries.size() - 1;
			std::vector<std::size_t> cursors(boundaries.begin(), boundaries.end() - 1);
			const std::string cursorLane = metricPrefix + "_run_cursors";
			r.resizeCounters(cursorLane, runCount);
			for (std::size_t run = 0; run < runCount; ++run)
				r.writeCounter(cursorLane, run, static_cast<int>(cursors[run]));
			const std::size_t begin = r.trace.events.size();
			const int phase = r.beginPhase(phaseLabel);
			std::size_t output = 0;
			while (true)
			{
				std::size_t selected = runCount;
				for (std::size_t run = 0; run < runCount; ++run)
				{
					if (cursors[run] >= boundaries[run + 1]) continue;
					if (selected == runCount) { selected = run; continue; }
					const SortItem candidate = r.at(cursors[run]);
					const SortItem currentBest = r.at(cursors[selected]);
					r.compare(candidate, currentBest, cursors[run], cursors[selected], "main", "main",
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric(metricPrefix + "_comparisons");
					if (compareItems(candidate, currentBest, r.trace.direction) == ComparisonResult::Less) selected = run;
					r.chargeWork();
				}
				if (selected == runCount) break;
				r.writeAuxiliary(lane, output++, r.at(cursors[selected]));
				++cursors[selected];
				r.writeCounter(cursorLane, selected, static_cast<int>(cursors[selected]));
				r.addMetric(metricPrefix + "_auxiliary_writes");
			}
			if (output != last - first) throw std::logic_error("K-way merge did not consume every input item.");
			for (std::size_t index = first; index < last; ++index)
				r.writeMain(index, r.auxiliaryAt(lane, index - first), SemanticRole::WriteTarget);
			r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, phaseLabel, begin);
			r.addMetric(metricPrefix + "_merges");
			r.endPhase(phase, phaseLabel);
			r.resizeCounters(cursorLane, 0);
		}

		void stableBinaryMergeSortRange(Recorder& r, std::size_t first, std::size_t last, const std::string& metricPrefix)
		{
			for (std::size_t start = first; start < last; start += 8)
			{
				binaryInsertionRange(r, start, (std::min)(last, start + 8));
				r.addMetric(metricPrefix + "_insertion_runs");
			}
			for (std::size_t width = 8; width < last - first;)
			{
				const int phase = r.beginPhase(metricPrefix + " binary merge pass");
				for (std::size_t start = first; start < last;)
				{
					const std::size_t middle = (std::min)(last, start + width);
					const std::size_t end = (std::min)(last, middle + width);
					if (middle < end) mergeRange(r, start, middle, end);
					start = end;
				}
				r.addMetric(metricPrefix + "_binary_merge_passes");
				r.endPhase(phase, metricPrefix + " binary merge pass");
				if (width > (last - first) / 2) break;
				width *= 2;
			}
		}

		void remiSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "remi_branching_factor", "remi_run_length", "remi_initial_runs", "remi_merge_passes",
				"remi_merges", "remi_comparisons", "remi_auxiliary_writes", "remi_insertion_runs", "remi_binary_merge_passes" }) r.declareMetric(key);
			if (count < 2) return;

			std::size_t branchingFactor = 1;
			while (branchingFactor * branchingFactor * branchingFactor < count) ++branchingFactor;
			const std::size_t runLength = branchingFactor * branchingFactor;
			r.setMetric("remi_branching_factor", static_cast<std::uint64_t>(branchingFactor));
			r.setMetric("remi_run_length", static_cast<std::uint64_t>(runLength));
			r.resizeAuxiliary("remi_buffer", count);
			std::size_t runCount = 0;
			for (std::size_t first = 0; first < count; first += runLength)
			{
				stableBinaryMergeSortRange(r, first, (std::min)(count, first + runLength), "remi");
				r.addMetric("remi_initial_runs");
				++runCount;
			}

			std::size_t width = runLength;
			while (width < count && runCount > 1)
			{
				const std::size_t groupSpan = width * branchingFactor;
				const int phase = r.beginPhase("Remi " + std::to_string(branchingFactor) + "-way merge level width " + std::to_string(width));
				std::size_t nextRunCount = 0;
				for (std::size_t first = 0; first < count; first += groupSpan)
				{
					std::vector<std::size_t> boundaries;
					boundaries.push_back(first);
					for (std::size_t boundary = first + width; boundary < (std::min)(count, first + groupSpan); boundary += width)
						boundaries.push_back(boundary);
					boundaries.push_back((std::min)(count, first + groupSpan));
					if (boundaries.size() > 2)
						stableKWayMerge(r, boundaries, "remi_buffer", "remi", "Remi run-heap merge group");
					++nextRunCount;
				}
				r.endPhase(phase, "Remi multiway merge level");
				r.addMetric("remi_merge_passes");
				runCount = nextRunCount;
				if (width > count / branchingFactor) break;
				width *= branchingFactor;
			}
			r.resizeAuxiliary("remi_buffer", 0);
		}

		void recursiveRotationMerge(Recorder& r, std::size_t first, std::size_t middle, std::size_t last,
			const char* metricPrefix)
		{
			if (first >= middle || middle >= last) return;
			r.chargeWork();
			if (last - first == 2)
			{
				const SortItem left = r.at(first), right = r.at(middle);
				r.compare(left, right, first, middle, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric(std::string(metricPrefix) + "_comparisons");
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					r.addMetric(std::string(metricPrefix) + "_swaps");
				}
				return;
			}
			const std::size_t leftLength = middle - first, rightLength = last - middle;
			std::size_t firstCut = first, secondCut = middle;
			if (leftLength >= rightLength)
			{
				firstCut = first + leftLength / 2;
				secondCut = middle + grailBinarySearch(r, middle, rightLength, firstCut, true, metricPrefix);
			}
			else
			{
				secondCut = middle + rightLength / 2;
				firstCut = first + grailBinarySearch(r, first, leftLength, secondCut, false, metricPrefix);
			}
			const std::size_t newMiddle = firstCut + (secondCut - middle);
			if (firstCut != middle && secondCut != middle)
			{
				grailRotate(r, firstCut, middle - firstCut, secondCut - middle, metricPrefix);
				r.addMetric(std::string(metricPrefix) + "_rotations");
			}
			recursiveRotationMerge(r, first, firstCut, newMiddle, metricPrefix);
			recursiveRotationMerge(r, newMiddle, secondCut, last, metricPrefix);
		}

		void optimizedWeaveMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_weave_blocks", "optimized_weave_bit_reversals", "optimized_weave_insertion_passes",
				"optimized_weave_comparisons", "optimized_weave_swaps", "optimized_weave_merge_comparisons",
				"optimized_weave_rotation_swaps", "optimized_weave_rotations" }) r.declareMetric(key);
			if (count < 2) return;
			for (std::size_t first = 0; first < count; first += 2)
			{
				const std::size_t last = (std::min)(count, first + 2);
				const std::size_t length = last - first;
				if (length < 2) continue;
				const int phase = r.beginPhase("Optimized weave base block");
				const std::size_t begin = r.trace.events.size();
				std::size_t bits = 0;
				for (std::size_t padded = 1; padded < length; padded <<= 1) ++bits;
				for (std::size_t index = 1; index < length; ++index)
				{
					r.chargeWork();
					std::size_t reversed = 0, value = index;
					for (std::size_t bit = 0; bit < bits; ++bit) { reversed = (reversed << 1) | (value & 1u); value >>= 1; }
					if (reversed > index)
					{
						r.swap(first + index, first + reversed, SemanticRole::WriteTarget);
						r.addMetric("optimized_weave_bit_reversals");
					}
				}
				binaryInsertionRange(r, first, last);
				r.addMetric("optimized_weave_blocks");
				r.addMetric("optimized_weave_insertion_passes");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Bit-reversed weave block", begin);
				r.endPhase(phase, "Optimized weave block");
			}
			for (std::size_t width = 2; width < count;)
			{
				const int phase = r.beginPhase("Optimized weave merge level width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						recursiveRotationMerge(r, first, middle, last, "optimized_weave");
						r.addMetric("optimized_weave_blocks");
					}
				}
				r.endPhase(phase, "Optimized weave merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		void wikiSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "wiki_natural_runs", "wiki_reversed_runs", "wiki_extensions", "wiki_merges",
				"wiki_buffered_merges", "wiki_rotation_merges", "wiki_buffer_writes", "wiki_comparisons", "wiki_merge_comparisons", "wiki_rotation_swaps" }) r.declareMetric(key);
			if (count < 2) return;
			struct Run { std::size_t first, last; Run(std::size_t a, std::size_t b) : first(a), last(b) {} };
			std::vector<Run> runs;
			const std::size_t minimumRun = 16;
			for (std::size_t first = 0; first < count;)
			{
				std::size_t last = first + 1;
				bool descending = false;
				if (last < count)
				{
					const SortItem a = r.at(first), b = r.at(last);
					r.compare(a, b, first, last, "main", "main");
					r.addMetric("wiki_comparisons");
					descending = compareItems(a, b, r.trace.direction) == ComparisonResult::Greater;
					++last;
					while (last < count)
					{
						const SortItem previous = r.at(last - 1), current = r.at(last);
						r.compare(previous, current, last - 1, last, "main", "main");
						r.addMetric("wiki_comparisons");
						const ComparisonResult relation = compareItems(previous, current, r.trace.direction);
						if (descending ? relation != ComparisonResult::Greater : relation == ComparisonResult::Greater) break;
						++last;
					}
				}
				if (descending)
				{
					std::size_t left = first, right = last - 1;
					while (left < right) { r.swap(left++, right--, SemanticRole::WriteTarget); }
					r.addMetric("wiki_reversed_runs");
				}
				else r.addMetric("wiki_natural_runs");
				const std::size_t target = (std::min)(count, (std::max)(last, first + minimumRun));
				if (target > last) { binaryInsertionRange(r, first, target); r.addMetric("wiki_extensions"); last = target; }
				runs.push_back(Run(first, last));
				first = last;
			}
			const std::size_t capacity = (std::max)(std::size_t(1), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			r.resizeAuxiliary("wiki_cache", capacity);
			while (runs.size() > 1)
			{
				std::vector<Run> next;
				const int phase = r.beginPhase("Wiki buffered block merge pass");
				for (std::size_t i = 0; i < runs.size(); i += 2)
				{
					if (i + 1 == runs.size()) { next.push_back(runs[i]); continue; }
					const Run left = runs[i], right = runs[i + 1];
					const std::size_t begin = r.trace.events.size();
					const std::uint64_t comparisons = r.state.metrics.comparisons;
					grailBufferedMerge(r, left.first, left.last, right.last, capacity, "wiki_cache", "wiki");
					r.addMetric("wiki_merges");
					r.addMetric("wiki_comparisons", r.state.metrics.comparisons - comparisons);
					r.annotate("main", RegionKind::Contiguous, left.first, right.last, 1, phase, "Wiki block merge", begin);
					next.push_back(Run(left.first, right.last));
				}
				r.endPhase(phase, "Wiki merge pass");
				runs.swap(next);
			}
			r.resizeAuxiliary("wiki_cache", 0);
		}

		void andreySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "andrey_runs", "andrey_merge_levels", "andrey_comparisons", "andrey_merge_comparisons",
				"andrey_rotation_swaps", "andrey_rotations", "andrey_swaps" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t block = (std::max)(std::size_t(2), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			for (std::size_t first = 0; first < count; first += block)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + block));
				r.addMetric("andrey_runs");
			}
			for (std::size_t width = block; width < count;)
			{
				const int phase = r.beginPhase("Andrey power-of-two block merge width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t mergeBegin = r.trace.events.size();
						recursiveRotationMerge(r, first, middle, last, "andrey");
						r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Andrey rotation merge", mergeBegin);
					}
				}
				r.addMetric("andrey_merge_levels");
				r.endPhase(phase, "Andrey block merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		void newShuffleMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "new_shuffle_runs", "new_shuffle_merge_levels", "new_shuffle_comparisons", "new_shuffle_rotations" }) r.declareMetric(key);
			if (count < 2) return;
			for (std::size_t run = 0; run < count; run += 2)
			{
				if (run + 1 < count)
				{
					const SortItem a = r.at(run), b = r.at(run + 1);
					r.compare(a, b, run, run + 1, "main", "main");
					r.addMetric("new_shuffle_comparisons");
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(run, run + 1, SemanticRole::WriteTarget);
				}
				r.addMetric("new_shuffle_runs");
			}
			for (std::size_t width = 2; width < count;)
			{
				const int phase = r.beginPhase("New Shuffle stable merge width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					const std::size_t mergeBegin = r.trace.events.size();
					std::size_t output = first, right = middle;
					while (output < middle && right < last)
					{
						r.chargeWork();
						const SortItem leftItem = r.at(output), rightItem = r.at(right);
						r.compare(leftItem, rightItem, output, right, "main", "main");
						r.addMetric("new_shuffle_comparisons");
						if (compareItems(leftItem, rightItem, r.trace.direction) != ComparisonResult::Greater) ++output;
						else
					{
						for (std::size_t position = right; position > output; --position)
							{ r.swap(position - 1, position, SemanticRole::WriteTarget); r.addMetric("new_shuffle_rotations"); }
						++output; ++middle; ++right;
					}
					}
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Shuffle-merge run pair", mergeBegin);
				}
				r.addMetric("new_shuffle_merge_levels");
				r.endPhase(phase, "New Shuffle merge level");
				if (width > count / 2) break;
				width *= 2;
			}
		}

		void yujiBufferedMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "yuji_runs", "yuji_merge_levels", "yuji_merges", "yuji_comparisons", "yuji_buffer_writes" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t runSize = (std::max)(std::size_t(2), count / 16);
			r.resizeAuxiliary("yuji_buffer", count / 2 + (count & 1u));
			for (std::size_t first = 0; first < count; first += runSize)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("yuji_runs");
			}
			for (std::size_t width = runSize; width < count;)
			{
				const int phase = r.beginPhase("Yuji buffered merge redistribution width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					if (middle == last) continue;
					const std::size_t leftCount = middle - first, rightCount = last - middle;
					const std::size_t begin = r.trace.events.size();
					if (leftCount <= rightCount)
					{
						for (std::size_t i = 0; i < leftCount; ++i) { r.writeAuxiliary("yuji_buffer", i, r.at(first + i)); r.addMetric("yuji_buffer_writes"); }
						std::size_t left = 0, right = middle, output = first;
						while (left < leftCount && right < last)
						{
							const SortItem a = r.auxiliaryAt("yuji_buffer", left), b = r.at(right);
							r.compare(a, b, first + left, right, "yuji_buffer", "main"); r.addMetric("yuji_comparisons");
							if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater) r.writeMain(output++, r.auxiliaryAt("yuji_buffer", left++));
							else r.writeMain(output++, r.at(right++));
						}
						while (left < leftCount) r.writeMain(output++, r.auxiliaryAt("yuji_buffer", left++));
					}
					else
					{
						for (std::size_t i = 0; i < rightCount; ++i) { r.writeAuxiliary("yuji_buffer", i, r.at(middle + i)); r.addMetric("yuji_buffer_writes"); }
						std::size_t left = middle, right = rightCount, output = last;
						while (left > first && right > 0)
						{
							const SortItem a = r.at(left - 1), b = r.auxiliaryAt("yuji_buffer", right - 1);
							r.compare(a, b, left - 1, middle + right - 1, "main", "yuji_buffer"); r.addMetric("yuji_comparisons");
							if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.writeMain(--output, r.at(--left));
							else r.writeMain(--output, r.auxiliaryAt("yuji_buffer", --right));
						}
						while (right > 0) r.writeMain(--output, r.auxiliaryAt("yuji_buffer", --right));
					}
					r.addMetric("yuji_merges"); r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Yuji redistributed merge", begin);
				}
				r.addMetric("yuji_merge_levels"); r.endPhase(phase, "Yuji merge level");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("yuji_buffer", 0);
		}

		void kotaSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "kota_block_length", "kota_initial_blocks", "kota_merge_levels", "kota_merges",
				"kota_comparisons", "kota_auxiliary_writes", "kota_buffer_writes", "kota_rotation_merges" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t block = 1;
			while (block * block < count) ++block;
			r.setMetric("kota_block_length", block);
			r.resizeAuxiliary("kota_block_buffer", count);
			for (std::size_t first = 0; first < count; first += block)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + block));
				r.addMetric("kota_initial_blocks");
			}
			std::size_t width = block;
			while (width < count)
			{
				const std::size_t fanout = 4;
				const std::size_t groupWidth = width * fanout;
				const int phase = r.beginPhase("Kota block merge level width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += groupWidth)
				{
					std::vector<std::size_t> boundaries(1, first);
					for (std::size_t boundary = first + width; boundary < (std::min)(count, first + groupWidth); boundary += width)
						boundaries.push_back(boundary);
					boundaries.push_back((std::min)(count, first + groupWidth));
					if (boundaries.size() > 2)
					{
						stableKWayMerge(r, boundaries, "kota_block_buffer", "kota", "Kota four-block merge");
					}
				}
				r.addMetric("kota_merge_levels"); r.endPhase(phase, "Kota block merge level");
				if (width > count / fanout) break;
				width *= fanout;
			}
			r.resizeAuxiliary("kota_block_buffer", 0);
		}

		void chaliceSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "chalice_distinct_keys", "chalice_blocks", "chalice_merge_levels", "chalice_merges",
				"chalice_comparisons", "chalice_key_probes", "chalice_auxiliary_writes", "chalice_run_cursors",
				"chalice_insertion_runs", "chalice_binary_merge_passes" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t root = (std::max)(std::size_t(2), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			r.resizeAuxiliary("chalice_keys", (std::min)(root, count));
			for (std::size_t i = 0; i < count && r.state.auxiliaryItems["chalice_keys"].size() > 0; ++i)
			{
				bool distinct = true;
				for (std::size_t k = 0; k < r.state.auxiliaryItems["chalice_keys"].size(); ++k)
				{
					if (r.state.auxiliaryItems["chalice_keys"][k].occupied)
					{
						const SortItem candidate = r.at(i), key = r.auxiliaryAt("chalice_keys", k);
						r.compare(candidate, key, i, k, "main", "chalice_keys", SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("chalice_comparisons"); r.addMetric("chalice_key_probes");
						if (compareItems(candidate, key, r.trace.direction) == ComparisonResult::Equal) { distinct = false; break; }
					}
				}
				if (!distinct) continue;
				for (std::size_t k = 0; k < r.state.auxiliaryItems["chalice_keys"].size(); ++k)
					if (!r.state.auxiliaryItems["chalice_keys"][k].occupied)
						{ r.writeAuxiliary("chalice_keys", k, r.at(i)); r.addMetric("chalice_distinct_keys"); r.addMetric("chalice_auxiliary_writes"); break; }
			}
			r.resizeAuxiliary("chalice_keys", 0);
			r.resizeAuxiliary("chalice_buffer", count);
			const std::size_t runLength = root * root;
			for (std::size_t first = 0; first < count; first += runLength)
			{
				stableBinaryMergeSortRange(r, first, (std::min)(count, first + runLength), "chalice");
				r.addMetric("chalice_blocks");
			}
			std::size_t width = runLength;
			while (width < count)
			{
				const int phase = r.beginPhase("Chalice key-block merge width " + std::to_string(width));
				std::vector<std::size_t> next(1, 0);
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width), last = (std::min)(count, middle + width);
					if (middle < last)
					{
						std::vector<std::size_t> pairBoundaries; pairBoundaries.push_back(first); pairBoundaries.push_back(middle); pairBoundaries.push_back(last);
						stableKWayMerge(r, pairBoundaries, "chalice_buffer", "chalice", "Chalice block merge");
					}
					next.push_back(last);
				}
				r.addMetric("chalice_merge_levels"); r.endPhase(phase, "Chalice block merge level");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("chalice_buffer", 0);
		}

		std::pair<SortItem, std::size_t> sampledMedian(Recorder& r, const std::vector<std::size_t>& positions,
			const char* metricPrefix)
		{
			std::vector<std::pair<SortItem, std::size_t> > samples;
			for (std::size_t i = 0; i < positions.size(); ++i) samples.push_back(std::make_pair(r.at(positions[i]), positions[i]));
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
					samples[j] = samples[j - 1]; --j;
				}
				samples[j] = value;
			}
			return samples[samples.size() / 2];
		}

		void flanSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "flan_partitions", "flan_pivot_comparisons", "flan_comparisons", "flan_equal_items",
				"flan_insertion_fallbacks", "flan_maximum_depth" }) r.declareMetric(key);
			if (count < 2) return;
			struct Range { std::size_t first, last, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), last(b), depth(d) {} };
			std::vector<Range> pending(1, Range(0, count, 0));
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				r.setMetric("flan_maximum_depth", (std::max)(r.state.metrics.algorithm["flan_maximum_depth"], static_cast<std::uint64_t>(range.depth)));
				if (range.last - range.first <= 24)
				{
					binaryInsertionRange(r, range.first, range.last); r.addMetric("flan_insertion_fallbacks"); continue;
				}
				const std::size_t n = range.last - range.first;
				std::vector<std::size_t> positions;
				for (std::size_t i = 0; i < 9; ++i) positions.push_back(range.first + ((n - 1) * i) / 8);
				const SortItem pivot = sampledMedian(r, positions, "flan").first;
				std::size_t less = range.first, scan = range.first, greater = range.last;
				const std::size_t pivotIndex = positions[4];
				const std::size_t partitionBegin = r.trace.events.size();
				const int phase = r.beginPhase("Flan three-way ninther partition");
				while (scan < greater)
				{
					r.chargeWork();
					const SortItem item = r.at(scan);
					r.compare(item, pivot, scan, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("flan_comparisons");
					const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
					if (order == ComparisonResult::Less) { r.swap(less++, scan++, SemanticRole::WriteTarget); }
					else if (order == ComparisonResult::Greater) { r.swap(scan, --greater, SemanticRole::WriteTarget); }
					else { ++scan; r.addMetric("flan_equal_items"); }
				}
				r.addMetric("flan_partitions");
				r.annotate("main", RegionKind::Contiguous, range.first, less, 1, phase, "Less than pivot", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, less, greater, 1, phase, "Pivot equals", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, greater, range.last, 1, phase, "Greater than pivot", partitionBegin);
				r.endPhase(phase, "Flan partition complete");
				const Range left(range.first, less, range.depth + 1), right(greater, range.last, range.depth + 1);
				if (left.last - left.first > right.last - right.first) { pending.push_back(left); pending.push_back(right); }
				else { pending.push_back(right); pending.push_back(left); }
			}
		}

		void fluxSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "flux_analyzed_transitions", "flux_descending_transitions", "flux_sorted_fast_paths",
				"flux_reversed_fast_paths", "flux_near_ordered_fallbacks", "flux_partitions", "flux_pivot_comparisons",
				"flux_comparisons", "flux_equal_items", "flux_insertion_fallbacks", "flux_maximum_depth" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t descents = 0;
			for (std::size_t i = 1; i < count; ++i)
			{
				r.chargeWork();
				const SortItem a = r.at(i - 1), b = r.at(i);
				r.compare(a, b, i - 1, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("flux_analyzed_transitions");
				if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
				{
					++descents;
					r.addMetric("flux_descending_transitions");
				}
			}
			if (descents == 0) { r.addMetric("flux_sorted_fast_paths"); return; }
			if (descents == count - 1)
			{
				for (std::size_t left = 0, right = count - 1; left < right; ++left, --right) r.swap(left, right, SemanticRole::WriteTarget);
				r.addMetric("flux_reversed_fast_paths"); return;
			}
			if (descents <= count / 6 || descents >= (count * 5) / 6)
			{
				binaryInsertionRange(r, 0, count); r.addMetric("flux_near_ordered_fallbacks"); return;
			}
			struct Range { std::size_t first, last, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), last(b), depth(d) {} };
			std::vector<Range> pending(1, Range(0, count, 0));
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				r.setMetric("flux_maximum_depth", (std::max)(r.state.metrics.algorithm["flux_maximum_depth"], static_cast<std::uint64_t>(range.depth)));
				if (range.last - range.first <= 20)
				{
					binaryInsertionRange(r, range.first, range.last); r.addMetric("flux_insertion_fallbacks"); continue;
				}
				const std::size_t n = range.last - range.first;
				const std::vector<std::size_t> positions = { range.first, range.first + n / 4, range.first + n / 2,
					range.first + (3 * n) / 4, range.last - 1 };
				const SortItem pivot = sampledMedian(r, positions, "flux").first;
				const std::size_t pivotIndex = positions[2];
				std::size_t less = range.first, scan = range.first, greater = range.last;
				const std::size_t partitionBegin = r.trace.events.size();
				const int phase = r.beginPhase("Flux median-of-five partition");
				while (scan < greater)
				{
					r.chargeWork();
					const SortItem item = r.at(scan);
					r.compare(item, pivot, scan, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					r.addMetric("flux_comparisons");
					const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
					if (order == ComparisonResult::Less) r.swap(less++, scan++, SemanticRole::WriteTarget);
					else if (order == ComparisonResult::Greater) r.swap(scan, --greater, SemanticRole::WriteTarget);
					else { ++scan; r.addMetric("flux_equal_items"); }
				}
				r.addMetric("flux_partitions");
				r.annotate("main", RegionKind::Contiguous, range.first, less, 1, phase, "Flux lower partition", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, less, greater, 1, phase, "Flux pivot band", partitionBegin);
				r.annotate("main", RegionKind::Contiguous, greater, range.last, 1, phase, "Flux upper partition", partitionBegin);
				r.endPhase(phase, "Flux partition complete");
				const Range left(range.first, less, range.depth + 1), right(greater, range.last, range.depth + 1);
				if (left.last - left.first > right.last - right.first) { pending.push_back(left); pending.push_back(right); }
				else { pending.push_back(right); pending.push_back(left); }
			}
		}

		void fifthMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "fifth_partition_count", "fifth_insertion_runs", "fifth_binary_merge_passes",
				"fifth_merges", "fifth_comparisons", "fifth_auxiliary_writes" }) r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const std::size_t fifthLength = count / 5;
			const std::size_t firstLength = count - fifthLength * 4;
			std::vector<std::size_t> boundaries;
			boundaries.push_back(0);
			boundaries.push_back(firstLength);
			for (std::size_t part = 1; part <= 4; ++part) boundaries.push_back(firstLength + fifthLength * part);
			for (std::size_t part = 0; part + 1 < boundaries.size(); ++part)
			{
				stableBinaryMergeSortRange(r, boundaries[part], boundaries[part + 1], "fifth");
				r.addMetric("fifth_partition_count");
			}
			r.resizeAuxiliary("merge", 0);
			r.resizeAuxiliary("fifth_buffer", count);
			stableKWayMerge(r, boundaries, "fifth_buffer", "fifth", "Five-part stable merge");
			r.resizeAuxiliary("fifth_buffer", 0);
		}

		void quadMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "quad_merged_groups", "quad_leaf_runs", "quad_merge_layers", "quad_merges", "quad_comparisons", "quad_auxiliary_writes" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("quad_buffer", count);
			for (std::size_t first = 0; first < count; first += 8)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + 8));
				r.addMetric("quad_leaf_runs");
			}
			for (std::size_t runWidth = 8; runWidth < count;)
			{
				const int phase = r.beginPhase("Quad four-run merge layer");
				for (std::size_t first = 0; first < count; first += runWidth * 4)
				{
					std::vector<std::size_t> boundaries;
					boundaries.reserve(5);
					for (std::size_t part = 0; part <= 4; ++part)
						boundaries.push_back((std::min)(count, first + runWidth * part));
					std::size_t activeRuns = 0;
					for (std::size_t part = 0; part < 4; ++part)
						if (boundaries[part] < boundaries[part + 1]) ++activeRuns;
					if (activeRuns > 1)
					{
						stableKWayMerge(r, boundaries, "quad_buffer", "quad", "Quad four-run merge");
						r.addMetric("quad_merged_groups");
					}
				}
				r.addMetric("quad_merge_layers");
				r.endPhase(phase, "Quad four-run merge layer");
				if (runWidth > count / 4) break;
				runWidth *= 4;
			}
			r.resizeAuxiliary("quad_buffer", 0);
		}

		void laziestStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "laziest_block_runs", "laziest_merges", "laziest_merge_comparisons",
				"laziest_exponential_probes", "laziest_rotation_swaps" })
				r.declareMetric(key);
			if (count < 2) return;
			if (count <= 16)
			{
				r.addMetric("laziest_block_runs");
				binaryInsertionRange(r, 0, count);
				return;
			}
			const std::size_t blockLength = (std::max)(std::size_t(16), static_cast<std::size_t>(std::sqrt(static_cast<double>(count))));
			std::size_t runStart = 0;
			for (; runStart + 2 * blockLength < count; runStart += blockLength)
			{
				binaryInsertionRange(r, runStart, runStart + blockLength);
				r.addMetric("laziest_block_runs");
			}
			if (runStart < count)
			{
				binaryInsertionRange(r, runStart, count);
				r.addMetric("laziest_block_runs");
			}

			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("laziest_merge_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto leftExponentialSearch = [&r](std::size_t keyIndex, std::size_t first, std::size_t end, const SortItem& key)
			{
				std::size_t offset = 1;
				while (first + offset - 1 < end)
				{
					const std::size_t probe = first + offset - 1;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("laziest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater) break;
					if (offset > end - first) { offset = end - first + 1; break; }
					offset *= 2;
				}
				std::size_t low = first + offset / 2;
				std::size_t high = (std::min)(end, first + offset - 1);
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("laziest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				return low;
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last)
			{
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("laziest_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Laziest stable rotation", begin);
			};
			const auto merge = [&r, &compareAt, &leftExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const std::size_t begin = r.trace.events.size();
				const int phase = r.beginPhase("Laziest in-place stable merge");
				std::size_t left = first, right = middle;
				while (left < right && right < last)
				{
					r.chargeWork();
					if (compareAt(left, right) == ComparisonResult::Greater)
					{
						const SortItem held = r.at(left);
						const std::size_t destination = leftExponentialSearch(left, right + 1, last, held);
						rotate(left, right, destination);
						left += destination - right;
						right = destination;
					}
					else ++left;
				}
				r.addMetric("laziest_merges");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Laziest merged run", begin);
				r.endPhase(phase, "Laziest in-place stable merge");
			};

			while (runStart >= blockLength)
			{
				merge(runStart - blockLength, runStart, count);
				runStart -= blockLength;
			}
		}

		void lazierestStableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "lazierest_block_runs", "lazierest_merges", "lazierest_fragmented_merges",
				"lazierest_merge_comparisons", "lazierest_exponential_probes", "lazierest_rotation_swaps" })
				r.declareMetric(key);
			if (count < 2) return;
			if (count <= 16)
			{
				r.addMetric("lazierest_block_runs");
				binaryInsertionRange(r, 0, count);
				return;
			}

			// This source variant uses ceil(cuberoot(n)) sized insertion runs,
			// followed by square-sized merges and fragmented suffix folds.
			std::size_t lowRoot = 0, highRoot = (std::min)(std::size_t(1291), count);
			while (lowRoot < highRoot)
			{
				const std::size_t middle = lowRoot + (highRoot - lowRoot) / 2;
				if (middle * middle * middle >= count) highRoot = middle;
				else lowRoot = middle + 1;
			}
			const std::size_t blockLength = (std::max)(std::size_t(1), lowRoot);
			const std::size_t squareLength = blockLength * blockLength;
			for (std::size_t end = count % blockLength; end <= count; end += blockLength)
			{
				binaryInsertionRange(r, end >= blockLength ? end - blockLength : 0, end);
				r.addMetric("lazierest_block_runs");
			}

			const auto compareAt = [&r](std::size_t first, std::size_t second)
			{
				const SortItem left = r.at(first), right = r.at(second);
				r.compare(left, right, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
				r.addMetric("lazierest_merge_comparisons");
				return compareItems(left, right, r.trace.direction);
			};
			const auto leftExponentialSearch = [&r](std::size_t keyIndex, std::size_t first, std::size_t end, const SortItem& key)
			{
				std::size_t offset = 1;
				while (first + offset - 1 < end)
				{
					const std::size_t probe = first + offset - 1;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Greater) break;
					if (offset > end - first) { offset = end - first + 1; break; }
					offset *= 2;
				}
				std::size_t low = first + offset / 2;
				std::size_t high = (std::min)(end, first + offset - 1);
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				return low;
			};
			const auto rightExponentialSearch = [&r](std::size_t first, std::size_t end, std::size_t keyIndex, const SortItem& key)
			{
				if (first >= end) return first;
				std::size_t offset = 1;
				while (offset <= end - first)
				{
					const std::size_t probe = end - offset;
					const SortItem candidate = r.at(probe);
					r.compare(key, candidate, keyIndex, probe, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_exponential_probes");
					if (compareItems(key, candidate, r.trace.direction) >= ComparisonResult::Equal) break;
					offset *= 2;
				}
				// Match the source's right exponential search bounds. When the
				// stride runs past the beginning, the unprobed prefix still needs
				// a binary search; returning `first` here loses that information.
				const std::size_t range = end - first;
				const std::size_t low = offset > range
					? first
					: (std::max)(first, end - offset + 1);
				const std::size_t high = end - offset / 2;
				std::size_t left = low, right = (std::max)(low, (std::min)(end, high));
				while (left < right)
				{
					const std::size_t middle = left + (right - left) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::HeldKey, SemanticRole::ProbedItem);
					r.addMetric("lazierest_merge_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) right = middle;
					else left = middle + 1;
				}
				return left;
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first == middle || middle == last) return;
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("lazierest_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Lazierest stable rotation", begin);
			};

			const auto mergeForward = [&r, &compareAt, &leftExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::size_t left = first, right = middle;
				while (left < right && right < last)
				{
					r.chargeWork();
					if (compareAt(left, right) == ComparisonResult::Greater)
					{
						const std::size_t destination = leftExponentialSearch(left, right + 1, last, r.at(left));
						rotate(left, right, destination);
						left += destination - right;
						right = destination;
					}
					else ++left;
				}
			};
			const auto mergeBackward = [&r, &rightExponentialSearch, &rotate](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::ptrdiff_t left = static_cast<std::ptrdiff_t>(middle) - 1;
				std::ptrdiff_t right = static_cast<std::ptrdiff_t>(last) - 1;
				while (right > left && left >= static_cast<std::ptrdiff_t>(first))
				{
					r.chargeWork();
					const SortItem leftItem = r.at(static_cast<std::size_t>(left));
					const SortItem rightItem = r.at(static_cast<std::size_t>(right));
					r.compare(leftItem, rightItem, static_cast<std::size_t>(left), static_cast<std::size_t>(right), "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					if (compareItems(leftItem, rightItem, r.trace.direction) == ComparisonResult::Greater)
					{
						const std::size_t bound = rightExponentialSearch(first, static_cast<std::size_t>(left), static_cast<std::size_t>(right), rightItem);
						rotate(bound, static_cast<std::size_t>(left + 1), static_cast<std::size_t>(right + 1));
						right -= left + 1 - static_cast<std::ptrdiff_t>(bound);
						left = static_cast<std::ptrdiff_t>(bound) - 1;
					}
					else --right;
				}
			};
			const auto merge = [&r, &mergeForward, &mergeBackward](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const std::size_t begin = r.trace.events.size();
				const int phase = r.beginPhase("Lazierest in-place merge");
				if (last - middle < middle - first) mergeBackward(first, middle, last);
				else mergeForward(first, middle, last);
				r.addMetric("lazierest_merges");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Lazierest merged run", begin);
				r.endPhase(phase, "Lazierest in-place merge");
			};

			std::size_t runEnd = count - blockLength;
			std::size_t suffixEnd = count;
			while (runEnd > 0)
			{
				r.chargeWork();
				if (suffixEnd - runEnd == squareLength)
				{
					suffixEnd -= squareLength;
					runEnd -= (std::min)(blockLength, runEnd);
				}
				if (runEnd == 0) break;
				mergeForward(runEnd >= blockLength ? runEnd - blockLength : 0, runEnd, suffixEnd);
				r.addMetric("lazierest_merges");
				runEnd = runEnd >= blockLength ? runEnd - blockLength : 0;
			}

			const auto fragmentedMerge = [&r, &leftExponentialSearch, &rotate, &merge](std::size_t first, std::size_t middle, std::size_t last, std::size_t block)
			{
				std::size_t index = first + (middle - first) % block;
				while (index < middle)
				{
					r.chargeWork();
					const std::size_t destination = leftExponentialSearch(index, middle, last, r.at(index));
					rotate(index, middle, destination);
					const std::size_t moved = destination - middle;
					const std::size_t mergeMiddle = index;
					index += moved;
					middle += moved;
					merge(first, mergeMiddle, index);
					first = index;
					index += block;
				}
				const std::size_t finalFirst = index > block ? (std::max)(first, index - block) : first;
				merge(finalFirst, index, last);
				r.addMetric("lazierest_fragmented_merges");
			};

			for (std::size_t start = count - squareLength; start > 0;)
			{
				const std::size_t middle = start;
				const std::size_t first = middle > squareLength ? middle - squareLength : 0;
				fragmentedMerge(first, middle, count, blockLength);
				if (start <= squareLength) break;
				start -= squareLength;
			}
		}

		void iterativeTopDownMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("merge_passes");
			r.resizeAuxiliary("merge", count);

			std::size_t subarrayCount = 1;
			while (subarrayCount < count) subarrayCount <<= 1;
			while (subarrayCount > 1)
			{
				const int phase = r.beginPhase("Iterative top-down merge layer");
				for (std::size_t i = 0; i < subarrayCount; i += 2)
				{
					r.chargeWork();
					const std::size_t first = count * i / subarrayCount;
					const std::size_t middle = count * (i + 1) / subarrayCount;
					const std::size_t last = count * (std::min)(i + 2, subarrayCount) / subarrayCount;
					if (first < middle && middle < last)
						mergeRange(r, first, middle, last);
				}
				r.endPhase(phase, "Iterative top-down merge layer");
				r.addMetric("merge_passes");
				subarrayCount >>= 1;
			}
		}

		void inPlaceMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("in_place_merges");
			r.declareMetric("in_place_merge_swaps");
			r.declareMetric("merge_comparisons");

			std::function<void(std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t first, std::size_t last)
			{
				r.chargeWork();
				const std::size_t length = last - first;
				if (length < 2) return;
				if (length == 2)
				{
					const SortItem a = r.at(first), b = r.at(first + 1);
					r.compare(a, b, first, first + 1);
					r.addMetric("merge_comparisons");
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(first, first + 1, SemanticRole::WriteTarget);
						r.addMetric("in_place_merge_swaps");
					}
					return;
				}

				const std::size_t middle = first + length / 2;
				sortRange(first, middle);
				sortRange(middle, last);
				const std::size_t annotationBegin = r.trace.events.size();
				const int phase = r.beginPhase("In-place stable merge");
				for (std::size_t left = first; left < middle; ++left)
				{
					r.chargeWork();
					const SortItem a = r.at(left), b = r.at(middle);
					r.compare(a, b, left, middle, "main", "main", SemanticRole::ComparedItem, SemanticRole::WriteTarget);
					r.addMetric("merge_comparisons");
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(left, middle, SemanticRole::WriteTarget);
						r.addMetric("in_place_merge_swaps");
						for (std::size_t right = middle; right + 1 < last; ++right)
						{
							r.chargeWork();
							const SortItem earlier = r.at(right), later = r.at(right + 1);
							r.compare(earlier, later, right, right + 1, "main", "main",
								SemanticRole::ComparedItem, SemanticRole::WriteTarget);
							r.addMetric("merge_comparisons");
							if (compareItems(earlier, later, r.trace.direction) != ComparisonResult::Greater) break;
							r.swap(right, right + 1, SemanticRole::WriteTarget);
							r.addMetric("in_place_merge_swaps");
						}
					}
				}
				r.addMetric("in_place_merges");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "In-place merge range", annotationBegin);
				r.endPhase(phase, "In-place stable merge");
			};

			sortRange(0, count);
		}

		void parallelMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("parallel_merge_tasks");
			r.declareMetric("merge_passes");
			r.declareMetric("maximum_merge_depth");
			r.resizeAuxiliary("merge", count);
			std::function<void(std::size_t, std::size_t, std::size_t)> split;
			split = [&r, &split](std::size_t first, std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (last - first < 2) return;
				const std::size_t middle = first + (last - first) / 2;
				const int siblingTasks = r.beginPhase("Parallel Merge sibling tasks");
				r.addMetric("parallel_merge_tasks");
				split(first, middle, depth + 1);
				split(middle, last, depth + 1);
				r.endPhase(siblingTasks, "Parallel Merge sibling tasks");
				mergeRange(r, first, middle, last);
				r.addMetric("merge_passes");
				r.setMetric("maximum_merge_depth", (std::max)(
					r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
			};
			split(0, count, 1);
		}

		void rotateMergeSort(Recorder& r, bool parallelSchedule)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("merge_passes");
			r.declareMetric("rotate_merge_ranges");
			r.declareMetric("rotate_block_swaps");
			r.declareMetric("rotate_item_swaps");
			r.declareMetric("binary_search_comparisons");
			if (parallelSchedule)
			{
				r.declareMetric("parallel_sort_tasks");
				r.declareMetric("parallel_merge_tasks");
				r.declareMetric("maximum_merge_depth");
			}

			const auto swapBlocks = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t offset = 0; offset < length; ++offset)
				{
					r.chargeWork();
					r.swap(first + offset, second + offset, SemanticRole::WriteTarget);
					r.addMetric("rotate_item_swaps");
				}
				r.addMetric("rotate_block_swaps");
			};

			const auto rotate = [&r, &swapBlocks](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::size_t leftLength = middle - first;
				std::size_t rightLength = last - middle;
				while (leftLength != 0 && rightLength != 0)
				{
					r.chargeWork();
					if (leftLength == rightLength)
					{
						swapBlocks(first, middle, leftLength);
						return;
					}
					if (leftLength < rightLength)
					{
						swapBlocks(first, last - leftLength, leftLength);
						last -= leftLength;
						rightLength -= leftLength;
					}
					else
					{
						swapBlocks(first, middle, rightLength);
						first += rightLength;
						leftLength -= rightLength;
					}
				}
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> rotateMerge;
			rotateMerge = [&r, &rotate, &rotateMerge, parallelSchedule](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (parallelSchedule)
					r.setMetric("maximum_merge_depth", (std::max)(r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
				if (first >= middle || middle >= last) return;
				if (depth > r.state.slots.size() * 2 + 2)
					throw std::logic_error("Rotate Merge recursion exceeded its bounded range depth.");
				if (last - first == 2)
				{
					const SortItem left = r.at(first), right = r.at(middle);
					r.compare(left, right, first, middle);
					r.addMetric("binary_search_comparisons");
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(first, middle, SemanticRole::WriteTarget);
						r.addMetric("rotate_item_swaps");
					}
					return;
				}

				const std::size_t leftLength = middle - first;
				const std::size_t rightLength = last - middle;
				std::size_t firstCut, secondCut;
				if (leftLength >= rightLength)
				{
					firstCut = first + leftLength / 2;
					const SortItem pivot = r.at(firstCut);
					std::size_t low = middle, high = last;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probeIndex = low + (high - low) / 2;
						const SortItem probe = r.at(probeIndex);
						r.compare(pivot, probe, firstCut, probeIndex, "main", "main",
							SemanticRole::Pivot, SemanticRole::ComparedItem);
						r.addMetric("binary_search_comparisons");
						if (compareItems(pivot, probe, r.trace.direction) == ComparisonResult::Greater) low = probeIndex + 1;
						else high = probeIndex;
					}
					secondCut = low;
				}
				else
				{
					secondCut = middle + rightLength / 2;
					const SortItem pivot = r.at(secondCut);
					std::size_t low = first, high = middle;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probeIndex = low + (high - low) / 2;
						const SortItem probe = r.at(probeIndex);
						r.compare(probe, pivot, probeIndex, secondCut, "main", "main",
							SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("binary_search_comparisons");
						if (compareItems(probe, pivot, r.trace.direction) == ComparisonResult::Greater) high = probeIndex;
						else low = probeIndex + 1;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut);
				if (parallelSchedule)
				{
					const int siblingMerges = r.beginPhase("Parallel rotate-merge sibling tasks");
					r.addMetric("parallel_merge_tasks");
					rotateMerge(first, firstCut, newMiddle, depth + 1);
					rotateMerge(newMiddle, secondCut, last, depth + 1);
					r.endPhase(siblingMerges, "Parallel rotate-merge sibling tasks");
				}
				else
				{
					rotateMerge(first, firstCut, newMiddle, depth + 1);
					rotateMerge(newMiddle, secondCut, last, depth + 1);
				}
				r.addMetric("rotate_merge_ranges");
			};

			if (parallelSchedule)
			{
				std::function<void(std::size_t, std::size_t, std::size_t)> split;
				split = [&r, &split, &rotateMerge](std::size_t first, std::size_t last, std::size_t depth)
				{
					r.chargeWork();
					if (last - first < 2) return;
					const std::size_t middle = first + (last - first) / 2;
					const int siblingSorts = r.beginPhase("Parallel rotate-merge sort tasks");
					r.addMetric("parallel_sort_tasks");
					split(first, middle, depth + 1);
					split(middle, last, depth + 1);
					r.endPhase(siblingSorts, "Parallel rotate-merge sort tasks");
					rotateMerge(first, middle, last, depth);
					r.addMetric("merge_passes");
				};
				split(0, count, 1);
				return;
			}

			for (std::size_t width = 1; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Rotate merge width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					r.chargeWork();
					const std::size_t middle = (std::min)(first + width, count);
					const std::size_t last = (std::min)(first + width * 2, count);
					if (first < middle && middle < last)
					{
						const std::size_t annotationBegin = r.trace.events.size();
						rotateMerge(first, middle, last, 0);
						r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
							"In-place rotate merge range", annotationBegin);
					}
				}
				r.endPhase(phase, "Rotate merge width " + std::to_string(width));
				r.addMetric("merge_passes");
				if (width > count / 2) break;
			}
		}

		void stacklessRotateMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("merge_passes");
			r.declareMetric("partition_merges");
			r.declareMetric("partition_merge_comparisons");
			r.declareMetric("rotate_block_swaps");
			r.declareMetric("rotate_item_swaps");
			const auto readMain = [&r, count](std::size_t index, const std::string& source) -> SortItem
			{
				if (index >= count)
					throw std::logic_error(std::string("Stackless Rotate Merge ") + source + " read index " +
						std::to_string(index) + " outside primary size " + std::to_string(count) + ".");
				return r.at(index);
			};

			const auto swapBlocks = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t offset = 0; offset < length; ++offset)
				{
					r.chargeWork();
					r.swap(first + offset, second + offset, SemanticRole::WriteTarget);
					r.addMetric("rotate_item_swaps");
				}
				r.addMetric("rotate_block_swaps");
			};
			const auto rotate = [&r, &swapBlocks](std::size_t first, std::size_t middle, std::size_t last)
			{
				std::size_t leftLength = middle - first, rightLength = last - middle;
				while (leftLength != 0 && rightLength != 0)
				{
					r.chargeWork();
					if (rightLength < leftLength)
					{
						swapBlocks(middle - rightLength, middle, rightLength);
						last -= rightLength;
						middle -= rightLength;
						leftLength -= rightLength;
					}
					else
					{
						swapBlocks(first, middle, leftLength);
						first += leftLength;
						middle += leftLength;
						rightLength -= leftLength;
					}
				}
			};

			const auto partitionMerge = [&r, &rotate, &readMain](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t selectedCount)
			{
				std::size_t leftLength = middle - first, rightLength = last - middle;
				if (leftLength == 0 || rightLength == 0 || selectedCount == 0) return;
				if (selectedCount > leftLength + rightLength)
					throw std::logic_error("Stackless Rotate Merge partition exceeds its input range.");
				const std::size_t annotationBegin = r.trace.events.size();
				r.addMetric("partition_merges");
				if (rightLength < leftLength)
				{
					selectedCount = leftLength + rightLength - selectedCount;
					std::size_t low = selectedCount > leftLength ? selectedCount - leftLength : 0;
					std::size_t high = (std::min)(selectedCount, rightLength);
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const std::size_t leftIndex = middle - (selectedCount - probe);
						const std::size_t rightIndex = last - probe - 1;
						const std::string context = "left partition search [" + std::to_string(first) + ", " +
							std::to_string(middle) + ", " + std::to_string(last) + "; c=" +
							std::to_string(selectedCount) + "; probe=" + std::to_string(probe) + "]";
						const SortItem left = readMain(leftIndex, context), right = readMain(rightIndex, "right partition search");
						r.compare(left, right, leftIndex, rightIndex, "main", "main",
							SemanticRole::ComparedItem, SemanticRole::ProbedItem);
						r.addMetric("partition_merge_comparisons");
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) high = probe;
						else low = probe + 1;
					}
					rotate(middle - (selectedCount - low), middle, last - low);
				}
				else
				{
					std::size_t low = selectedCount > rightLength ? selectedCount - rightLength : 0;
					std::size_t high = (std::min)(selectedCount, leftLength);
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const std::size_t leftIndex = first + probe;
						const std::size_t rightIndex = middle + (selectedCount - probe) - 1;
						const std::string context = "right partition search [" + std::to_string(first) + ", " +
							std::to_string(middle) + ", " + std::to_string(last) + "; c=" +
							std::to_string(selectedCount) + "; probe=" + std::to_string(probe) + "]";
						const SortItem left = readMain(leftIndex, context), right = readMain(rightIndex, "left partition search");
						r.compare(left, right, leftIndex, rightIndex, "main", "main",
							SemanticRole::ComparedItem, SemanticRole::ProbedItem);
						r.addMetric("partition_merge_comparisons");
						if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) high = probe;
						else low = probe + 1;
					}
					rotate(first + low, middle, middle + (selectedCount - low));
				}
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
					"Stackless partition merge", annotationBegin);
			};

			const int initialPairs = r.beginPhase("Stackless merge initial pairs");
			for (std::size_t i = 1; i < count; i += 2)
			{
				r.chargeWork();
				const SortItem left = readMain(i - 1, "initial pair"), right = readMain(i, "initial pair");
				r.compare(left, right, i - 1, i);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(i - 1, i, SemanticRole::WriteTarget);
					r.addMetric("rotate_item_swaps");
				}
			}
			r.endPhase(initialPairs, "Stackless merge initial pairs");

		for (std::size_t width = 2; width < count; width *= 2)
		{
			const int phase = r.beginPhase("Stackless partition merge width " + std::to_string(width));
			std::size_t lastCovered = 0;
			for (std::size_t first = 0; first + width < count; first += width * 2)
			{
				r.chargeWork();
				lastCovered = (std::min)(first + width * 2, count);
				partitionMerge(first, first + width, lastCovered, width);
			}
			for (std::size_t subWidth = width / 2; subWidth > 1; subWidth /= 2)
				for (std::size_t first = 0; first + subWidth < lastCovered; first += subWidth * 2)
				{
					r.chargeWork();
					const std::size_t last = (std::min)(first + subWidth * 2, count);
					std::size_t prefix = first + 1;
					while (prefix < last)
					{
						r.chargeWork();
						const SortItem before = readMain(prefix - 1, "sorted-prefix scan"), after = readMain(prefix, "sorted-prefix scan");
						r.compare(before, after, prefix - 1, prefix);
						if (compareItems(before, after, r.trace.direction) == ComparisonResult::Greater) break;
						++prefix;
					}
					if (prefix < last) partitionMerge(first, prefix, last, subWidth);
				}
			for (std::size_t i = 1; i < lastCovered; i += 2)
			{
				r.chargeWork();
				const SortItem left = readMain(i - 1, "pass pair"), right = readMain(i, "pass pair");
				r.compare(left, right, i - 1, i);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
				{
					r.swap(i - 1, i, SemanticRole::WriteTarget);
					r.addMetric("rotate_item_swaps");
				}
			}
			r.endPhase(phase, "Stackless partition merge width " + std::to_string(width));
			r.addMetric("merge_passes");
			if (width > count / 2) break;
		}
		}

		void blockSwapMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("merge_passes");
			r.declareMetric("block_merge_ranges");
			r.declareMetric("block_merge_comparisons");
			r.declareMetric("block_swaps");
			r.declareMetric("block_item_swaps");

			const auto binarySearchMid = [&r](std::size_t first, std::size_t middle, std::size_t last) -> std::size_t
			{
				std::size_t low = 0, high = (std::min)(middle - first, last - middle);
				while (low < high)
				{
					r.chargeWork();
					const std::size_t offset = low + (high - low) / 2;
					const std::size_t leftIndex = middle - offset - 1, rightIndex = middle + offset;
					const SortItem left = r.at(leftIndex), right = r.at(rightIndex);
					r.compare(left, right, leftIndex, rightIndex, "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("block_merge_comparisons");
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) low = offset + 1;
					else high = offset;
				}
				return low;
			};
			const auto swapBlocks = [&r](std::size_t first, std::size_t second, std::size_t length)
			{
				for (std::size_t offset = 0; offset < length; ++offset)
				{
					r.chargeWork();
					r.swap(first + offset, second + offset, SemanticRole::WriteTarget);
					r.addMetric("block_item_swaps");
				}
				r.addMetric("block_swaps");
			};
			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &binarySearchMid, &swapBlocks, &merge, count](std::size_t first,
				std::size_t middle, std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Block-Swap Merge recursion exceeded its bounded range depth.");
				const std::size_t annotationBegin = r.trace.events.size();
				r.addMetric("block_merge_ranges");
				std::size_t blockLength = binarySearchMid(first, middle, last);
				while (blockLength != 0)
				{
					swapBlocks(middle - blockLength, middle, blockLength);
					const std::size_t rightMiddle = middle + blockLength;
					merge(middle, rightMiddle, last, depth + 1);
					last = middle;
					middle -= blockLength;
					blockLength = binarySearchMid(first, middle, last);
				}
				r.annotate("main", RegionKind::Contiguous, first, last, 1, -1,
					"Block-swap merge range", annotationBegin);
			};

			for (std::size_t width = 1; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Block-swap merge width " + std::to_string(width));
				std::size_t first = 0;
				for (; first + width * 2 <= count; first += width * 2)
				{
					r.chargeWork();
					merge(first, first + width, first + width * 2, 0);
				}
				if (first + width < count)
					merge(first, first + width, count, 0);
				r.endPhase(phase, "Block-swap merge width " + std::to_string(width));
				r.addMetric("merge_passes");
				if (width > count / 2) break;
			}
		}

		void weavedMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("weave_merge_ranges");
			r.declareMetric("weave_depth");
			r.declareMetric("weave_comparisons");
			r.resizeAuxiliary("weave", count);

			std::function<void(std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &merge, count](std::size_t residue, std::size_t modulus, std::size_t depth)
			{
				r.chargeWork();
				if (residue + modulus >= count) return;
				const std::string label = "Weaved merge stride " + std::to_string(modulus);
				const std::size_t annotationBegin = r.trace.events.size();
				const int phase = r.beginPhase(label);
				r.addMetric("weave_merge_ranges");
				r.setMetric("weave_depth", (std::max)(r.state.metrics.algorithm["weave_depth"], static_cast<std::uint64_t>(depth)));
				merge(residue, modulus * 2, depth + 1);
				merge(residue + modulus, modulus * 2, depth + 1);

				std::size_t left = residue, right = residue + modulus, output = residue;
				const std::size_t doubleModulus = modulus * 2;
				while (left < count && right < count)
				{
					r.chargeWork();
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right, "main", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("weave_comparisons");
					const ComparisonResult relation = compareItems(a, b, r.trace.direction);
					if (relation != ComparisonResult::Greater && !(relation == ComparisonResult::Equal && left > right))
					{
						r.writeAuxiliary("weave", output, a);
						left += doubleModulus;
					}
					else
					{
						r.writeAuxiliary("weave", output, b);
						right += doubleModulus;
					}
					output += modulus;
				}
				while (left < count)
				{
					r.chargeWork();
					r.writeAuxiliary("weave", output, r.at(left));
					left += doubleModulus;
					output += modulus;
				}
				while (right < count)
				{
					r.chargeWork();
					r.writeAuxiliary("weave", output, r.at(right));
					right += doubleModulus;
					output += modulus;
				}

				for (std::size_t index = residue; index < count; index += modulus)
				{
					r.chargeWork();
					r.writeMain(index, r.auxiliaryAt("weave", index));
				}
				r.annotate("main", RegionKind::Strided, residue, count, modulus, phase, label, annotationBegin);
				r.annotate("weave", RegionKind::Strided, residue, count, modulus, phase, label, annotationBegin);
				r.endPhase(phase, label);
			};

			merge(0, 1, 0);
		}

		void twinMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			r.declareMetric("twin_pair_comparisons");
			r.declareMetric("twin_run_reversals");
			r.declareMetric("twin_merge_passes");
			r.declareMetric("twin_merged_ranges");
			r.declareMetric("twin_buffer_writes");

			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				r.addMetric("twin_pair_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto reverseRange = [&r](std::size_t first, std::size_t last, int phase)
			{
				const std::size_t begin = r.trace.events.size();
				std::size_t left = first, right = last;
				while (left < right)
				{
					r.chargeWork();
					r.swap(left++, right--, SemanticRole::WriteTarget);
				}
				r.addMetric("twin_run_reversals");
				r.annotate("main", RegionKind::Contiguous, first, last + 1, 1, phase,
					"Twin Sort reversed run", begin);
			};

			// The source first examines alternating adjacent pairs. Out-of-order
			// pairs are used to identify descending runs; strict boundary checks
			// keep equal items from being reversed across runs.
			const int pairPhase = r.beginPhase("Twin pair-run normalization");
			std::size_t index = 0;
			std::size_t end = count - 2;
			while (index <= end)
			{
				r.chargeWork();
				if (compareAt(index, index + 1) != ComparisonResult::Greater)
				{
					index += 2;
					continue;
				}

				const std::size_t start = index;
				index += 2;
				while (true)
				{
					if (index > end)
					{
						if (start == 0)
						{
						bool wholeInputDescending = count % 2 == 0;
						if (!wholeInputDescending)
							wholeInputDescending = compareAt(index - 1, index) == ComparisonResult::Greater;
						if (wholeInputDescending)
						{
							reverseRange(0, count - 1, pairPhase);
							r.endPhase(pairPhase, "Twin pair-run normalization");
							return;
						}
						}
						break;
					}

					if (compareAt(index, index + 1) == ComparisonResult::Greater)
					{
						if (compareAt(index - 1, index) == ComparisonResult::Greater)
						{
							index += 2;
							continue;
						}
						r.swap(index, index + 1);
					}
					break;
				}

				reverseRange(start, index - 1, pairPhase);
				index += 2;
			}
			r.endPhase(pairPhase, "Twin pair-run normalization");

			// Merge adjacent runs from the tail. Copying only the right run keeps
			// the item buffer at the source's n/2 capacity and lets primary writes
			// remain reversible identity moves.
			const std::string lane = "twin_buffer";
			r.resizeAuxiliary(lane, count / 2);
			for (std::size_t width = 2; width < count; width *= 2)
			{
				const int phase = r.beginPhase("Twin tail-merge width " + std::to_string(width));
				for (std::size_t first = 0; first + width < count; first += width * 2)
				{
					r.chargeWork();
					const std::size_t mergeBegin = r.trace.events.size();
					const std::size_t middle = first + width;
					const std::size_t last = (std::min)(count, first + width * 2);
					if (compareAt(middle - 1, middle) != ComparisonResult::Greater) continue;

					const std::size_t rightCount = last - middle;
					for (std::size_t offset = 0; offset < rightCount; ++offset)
					{
						r.chargeWork();
						r.writeAuxiliary(lane, offset, r.at(middle + offset));
						r.addMetric("twin_buffer_writes");
					}

					std::size_t left = middle;
					std::size_t right = rightCount;
					std::size_t destination = last;
					while (left > first && right > 0)
					{
						r.chargeWork();
						const SortItem a = r.at(left - 1);
						const SortItem b = r.auxiliaryAt(lane, right - 1);
						r.compare(a, b, left - 1, right - 1, "main", lane,
							SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("twin_pair_comparisons");
						--destination;
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							r.writeMain(destination, r.at(--left));
						else
						r.writeMain(destination, r.auxiliaryAt(lane, --right));
					}
					while (right > 0)
					{
						r.chargeWork();
						r.writeMain(--destination, r.auxiliaryAt(lane, --right));
					}
					r.addMetric("twin_merged_ranges");
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
						"Twin tail merge", mergeBegin);
					r.annotate(lane, RegionKind::Contiguous, 0, rightCount, 1, phase,
						"Copied right run", mergeBegin);
				}
				r.endPhase(phase, "Twin tail-merge width " + std::to_string(width));
				r.addMetric("twin_merge_passes");
				if (width > count / 2) break;
			}
		}

		std::size_t timMinRunLength(std::size_t length)
		{
			std::size_t remainder = 0;
			while (length >= 32)
			{
				remainder |= length & 1u;
				length >>= 1u;
			}
			return length + remainder;
		}

		void timFamilySort(Recorder& r, bool cocktailSeedRuns)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("tim_runs");
			r.declareMetric("tim_natural_runs");
			r.declareMetric("tim_cocktail_runs");
			r.declareMetric("tim_run_reversals");
			r.declareMetric("tim_binary_insertions");
			r.declareMetric("tim_merge_passes");
			r.declareMetric("tim_merged_runs");
			r.declareMetric("tim_merge_comparisons");
			if (count < 2) return;

			const std::size_t minRun = timMinRunLength(count);
			r.setMetric("tim_min_run", minRun);
			std::vector<std::size_t> runBases, runLengths;
			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				return compareItems(a, b, r.trace.direction);
			};
			const auto binaryInsertSuffix = [&r](std::size_t base, std::size_t sortedEnd, std::size_t end)
			{
				for (std::size_t itemIndex = sortedEnd; itemIndex < end; ++itemIndex)
				{
					const SortItem key = r.at(itemIndex);
					r.hold(itemIndex);
					std::size_t low = base, high = itemIndex;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t middle = low + (high - low) / 2;
						const SortItem candidate = r.at(middle);
						r.compare(key, candidate, itemIndex, middle, "main", "main",
							SemanticRole::HeldKey, SemanticRole::ComparedItem);
						if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
						else low = middle + 1;
					}
					for (std::size_t position = itemIndex; position > low; --position) r.shift(position - 1, position);
					r.place(low, key);
					r.addMetric("tim_binary_insertions");
				}
			};
			const auto cocktailRun = [&r](std::size_t first, std::size_t end)
			{
				if (end <= first + 1) return;
				std::size_t low = first, high = end - 1;
				bool changed = true;
				while (changed && low < high)
				{
					changed = false;
					for (std::size_t i = low; i < high; ++i)
					{
						const SortItem a = r.at(i), b = r.at(i + 1);
						r.compare(a, b, i, i + 1);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i, i + 1); changed = true; }
					}
					if (!changed) break;
					--high;
					changed = false;
					for (std::size_t i = high; i > low; --i)
					{
						const SortItem a = r.at(i - 1), b = r.at(i);
						r.compare(a, b, i - 1, i);
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							{ r.swap(i - 1, i); changed = true; }
					}
					++low;
				}
			};

			std::function<void(std::size_t)> mergeAt;
			mergeAt = [&r, &runBases, &runLengths, &mergeAt](std::size_t runIndex)
			{
				const std::size_t first = runBases[runIndex];
				const std::size_t leftLength = runLengths[runIndex];
				const std::size_t middle = first + leftLength;
				const std::size_t rightLength = runLengths[runIndex + 1];
				const std::size_t last = middle + rightLength;
				const std::string lane = "tim_buffer";
				const int phase = r.beginPhase("Tim run merge " + std::to_string(first) + "–" + std::to_string(last));
				const std::size_t begin = r.trace.events.size();
				const SortItem leftEnd = r.at(middle - 1), rightStart = r.at(middle);
				r.compare(leftEnd, rightStart, middle - 1, middle);
				r.addMetric("tim_merge_comparisons");
				if (compareItems(leftEnd, rightStart, r.trace.direction) == ComparisonResult::Greater)
				{
					if (r.state.auxiliaryItems.find(lane) == r.state.auxiliaryItems.end())
						r.resizeAuxiliary(lane, r.state.slots.size());
					for (std::size_t offset = 0; offset < leftLength; ++offset)
					{
						r.chargeWork();
						r.writeAuxiliary(lane, offset, r.at(first + offset));
					}
					std::size_t left = 0, right = middle, destination = first;
					while (left < leftLength && right < last)
					{
						r.chargeWork();
						const SortItem a = r.auxiliaryAt(lane, left), b = r.at(right);
						r.compare(a, b, left, right, lane, "main",
							SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						r.addMetric("tim_merge_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(destination++, r.auxiliaryAt(lane, left++));
						else r.writeMain(destination++, r.at(right++));
					}
					while (left < leftLength)
					{
						r.chargeWork();
						r.writeMain(destination++, r.auxiliaryAt(lane, left++));
					}
				}
				r.addMetric("tim_merged_runs");
				r.addMetric("tim_merge_passes");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Tim merged run", begin);
				if (r.state.auxiliaryItems.find(lane) != r.state.auxiliaryItems.end())
					r.annotate(lane, RegionKind::Contiguous, 0, leftLength, 1, phase, "Buffered left run", begin);
				r.endPhase(phase, "Tim run merge " + std::to_string(first) + "–" + std::to_string(last));
				runLengths[runIndex] += runLengths[runIndex + 1];
				runBases.erase(runBases.begin() + runIndex + 1);
				runLengths.erase(runLengths.begin() + runIndex + 1);
			};
			const auto collapseRuns = [&runBases, &runLengths, &mergeAt]()
			{
				while (runLengths.size() > 1)
				{
					std::size_t index = runLengths.size() - 2;
					const bool firstInvariant = index > 0 && runLengths[index - 1] <= runLengths[index] + runLengths[index + 1];
					const bool secondInvariant = index > 1 && runLengths[index - 2] <= runLengths[index - 1] + runLengths[index];
					if (firstInvariant || secondInvariant)
					{
						if (index > 0 && runLengths[index - 1] < runLengths[index + 1]) --index;
						mergeAt(index);
					}
					else if (runLengths[index] <= runLengths[index + 1]) mergeAt(index);
					else break;
				}
			};

			if (cocktailSeedRuns)
			{
				for (std::size_t first = 0; first < count; )
				{
					const std::size_t last = (std::min)(count, first + minRun);
					const int phase = r.beginPhase("Cocktail-built Tim run " + std::to_string(first) + "–" + std::to_string(last));
					cocktailRun(first, last);
					r.annotate("main", RegionKind::Contiguous, first, last, 1, phase, "Cocktail-built run", r.trace.events.size());
					r.endPhase(phase, "Cocktail-built Tim run " + std::to_string(first) + "–" + std::to_string(last));
					r.addMetric("tim_cocktail_runs");
					r.addMetric("tim_runs");
					runBases.push_back(first);
					runLengths.push_back(last - first);
					collapseRuns();
					first = last;
				}
			}
			else
			{
				std::size_t first = 0;
				while (first < count)
				{
					std::size_t runEnd = first + 1;
					if (runEnd < count)
					{
						const bool descending = compareAt(first, runEnd) == ComparisonResult::Greater;
						++runEnd;
						while (runEnd < count)
						{
							r.chargeWork();
							const ComparisonResult order = compareAt(runEnd - 1, runEnd);
							if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
							++runEnd;
						}
						if (descending)
						{
							const std::size_t begin = r.trace.events.size();
							std::size_t left = first, right = runEnd - 1;
							while (left < right) r.swap(left++, right--, SemanticRole::WriteTarget);
							r.annotate("main", RegionKind::Contiguous, first, runEnd, 1, -1, "Reversed natural run", begin);
							r.addMetric("tim_run_reversals");
						}
					}
					const std::size_t naturalEnd = runEnd;
					const std::size_t targetEnd = (std::min)(count, (std::max)(naturalEnd, first + minRun));
					binaryInsertSuffix(first, naturalEnd, targetEnd);
					r.addMetric("tim_natural_runs");
					r.addMetric("tim_runs");
					runBases.push_back(first);
					runLengths.push_back(targetEnd - first);
					collapseRuns();
					first = targetEnd;
				}
			}
			while (runLengths.size() > 1)
			{
				std::size_t index = runLengths.size() - 2;
				if (index > 0 && runLengths[index - 1] < runLengths[index + 1]) --index;
				mergeAt(index);
			}
		}

		void improvedInPlaceMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("inplace_merge_ranges");
			r.declareMetric("inplace_merge_comparisons");
			r.declareMetric("inplace_merge_rotations");
			r.declareMetric("inplace_merge_rotation_swaps");
			r.declareMetric("inplace_merge_insertion_runs");
			const auto compareAt = [&r](std::size_t first, std::size_t second) -> ComparisonResult
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				r.addMetric("inplace_merge_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last, int group)
			{
				if (first == middle || middle == last) return;
				const std::size_t begin = r.trace.events.size();
				const auto reverse = [&r](std::size_t left, std::size_t right)
				{
					while (left < right)
					{
						r.chargeWork();
						r.swap(left++, --right, SemanticRole::WriteTarget);
						r.addMetric("inplace_merge_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.addMetric("inplace_merge_rotations");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, group, "Stable block rotation", begin);
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> merge;
			merge = [&r, &compareAt, &rotate, &merge, count](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Improved In-Place Merge recursion exceeded its bounded depth.");
				if (compareAt(middle - 1, middle) != ComparisonResult::Greater) return;

				const std::size_t total = last - first;
				if (total == 2)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					return;
				}

				const int phase = r.beginPhase("Symmetric in-place merge " + std::to_string(first) + "–" + std::to_string(last));
				r.addMetric("inplace_merge_ranges");
				std::size_t firstCut = first, secondCut = middle;
				if (middle - first > last - middle)
				{
					firstCut = first + (middle - first) / 2;
					const SortItem pivot = r.at(firstCut);
					std::size_t low = middle, high = last;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, firstCut);
						r.addMetric("inplace_merge_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
						else high = probe;
					}
					secondCut = low;
				}
				else
				{
					secondCut = middle + (last - middle) / 2;
					const SortItem pivot = r.at(secondCut);
					std::size_t low = first, high = middle;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, secondCut);
						r.addMetric("inplace_merge_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
						else high = probe;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut, phase);
				merge(first, firstCut, newMiddle, depth + 1);
				merge(newMiddle, secondCut, last, depth + 1);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Symmetric in-place merge range", r.trace.events.size() > 0 ? r.trace.events.size() - 1 : 0);
				r.endPhase(phase, "Symmetric in-place merge " + std::to_string(first) + "–" + std::to_string(last));
			};

			std::function<void(std::size_t, std::size_t, std::size_t)> sort;
			sort = [&r, &sort, &merge](std::size_t first, std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				const std::size_t length = last - first;
				if (length < 2) return;
				if (length <= 16)
				{
					binaryInsertionRange(r, first, last);
					r.addMetric("inplace_merge_insertion_runs");
					return;
				}
				const std::size_t middle = first + length / 2;
				sort(first, middle, depth + 1);
				sort(middle, last, depth + 1);
				merge(first, middle, last, depth);
			};
			sort(0, count, 0);
		}

		void blockInsertionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("block_insertion_runs");
			r.declareMetric("block_insertion_run_reversals");
			r.declareMetric("block_insertion_short_insertions");
			r.declareMetric("block_insertion_merges");
			r.declareMetric("block_insertion_comparisons");
			r.declareMetric("block_insertion_rotations");
			r.declareMetric("block_insertion_rotation_swaps");
			if (count < 2) return;

			const auto compareAt = [&r](std::size_t left, std::size_t right) -> ComparisonResult
			{
				const SortItem a = r.at(left), b = r.at(right);
				r.compare(a, b, left, right);
				r.addMetric("block_insertion_comparisons");
				return compareItems(a, b, r.trace.direction);
			};
			const auto rotate = [&r](std::size_t first, std::size_t middle, std::size_t last, int phase)
			{
				if (first == middle || middle == last) return;
				const std::size_t eventBegin = r.trace.events.size();
				const auto reverse = [&r](std::size_t begin, std::size_t end)
				{
					while (begin < end)
					{
						r.chargeWork();
						r.swap(begin++, --end, SemanticRole::WriteTarget);
						r.addMetric("block_insertion_rotation_swaps");
					}
				};
				reverse(first, middle);
				reverse(middle, last);
				reverse(first, last);
				r.addMetric("block_insertion_rotations");
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Block inserted into sorted prefix", eventBegin);
			};

			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> mergeRun;
			mergeRun = [&r, &compareAt, &rotate, &mergeRun, count](std::size_t first, std::size_t middle,
				std::size_t last, std::size_t depth)
			{
				r.chargeWork();
				if (first >= middle || middle >= last) return;
				if (depth > count * 2 + 2) throw std::logic_error("Block Insertion merge exceeded its bounded recursion depth at [" +
					std::to_string(first) + "," + std::to_string(middle) + "," + std::to_string(last) + ").");
				if (compareAt(middle - 1, middle) != ComparisonResult::Greater) return;
				if (last - first == 2)
				{
					r.swap(first, middle, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_merges");
					return;
				}

				const int phase = r.beginPhase("Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
				std::size_t firstCut = first, secondCut = middle;
				if (middle - first > last - middle)
				{
					firstCut = first + (middle - first) / 2;
					const SortItem pivot = r.at(firstCut);
					std::size_t low = middle, high = last;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, firstCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
						else high = probe;
					}
					secondCut = low;
				}
				else
				{
					secondCut = middle + (last - middle) / 2;
					const SortItem pivot = r.at(secondCut);
					std::size_t low = first, high = middle;
					while (low < high)
					{
						r.chargeWork();
						const std::size_t probe = low + (high - low) / 2;
						const SortItem candidate = r.at(probe);
						r.compare(candidate, pivot, probe, secondCut,
							"main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						r.addMetric("block_insertion_comparisons");
						if (compareItems(candidate, pivot, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
						else high = probe;
					}
					firstCut = low;
				}

				const std::size_t newMiddle = firstCut + (secondCut - middle);
				rotate(firstCut, middle, secondCut, phase);
				r.addMetric("block_insertion_merges");
				mergeRun(first, firstCut, newMiddle, depth + 1);
				mergeRun(newMiddle, secondCut, last, depth + 1);
				r.annotate("main", RegionKind::Contiguous, first, last, 1, phase,
					"Merged monotone block", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
				r.endPhase(phase, "Block insertion merge " + std::to_string(first) + "–" + std::to_string(last));
			};

			const auto insertShortRun = [&r](std::size_t keyIndex)
			{
				const SortItem key = r.at(keyIndex);
				r.hold(keyIndex);
				std::size_t low = 0, high = keyIndex;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, keyIndex, middle, "main", "main",
						SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				for (std::size_t position = keyIndex; position > low; --position) r.shift(position - 1, position);
				r.place(low, key);
				r.addMetric("block_insertion_short_insertions");
			};

			std::size_t runStart = 0;
			bool firstRun = true;
			while (runStart < count)
			{
				std::size_t runEnd = runStart + 1;
				bool descending = false;
				if (runEnd < count)
				{
					descending = compareAt(runStart, runEnd) == ComparisonResult::Greater;
					++runEnd;
					while (runEnd < count)
					{
						const ComparisonResult order = compareAt(runEnd - 1, runEnd);
						if (descending ? order != ComparisonResult::Greater : order == ComparisonResult::Greater) break;
						r.chargeWork();
						++runEnd;
					}
				}

				if (descending)
				{
					const std::size_t eventBegin = r.trace.events.size();
					for (std::size_t left = runStart, right = runEnd - 1; left < right; ++left, --right)
						r.swap(left, right, SemanticRole::WriteTarget);
					r.addMetric("block_insertion_run_reversals");
					r.annotate("main", RegionKind::Contiguous, runStart, runEnd, 1, -1,
						"Reversed strict monotone run", eventBegin);
				}
				r.addMetric("block_insertion_runs");

				if (firstRun)
				{
					firstRun = false;
				}
				else if (runEnd - runStart < 3)
				{
					for (std::size_t index = runStart; index < runEnd; ++index)
						insertShortRun(index);
				}
				else
				{
					const int phase = r.beginPhase("Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
					mergeRun(0, runStart, runEnd, 0);
					r.annotate("main", RegionKind::Contiguous, 0, runEnd, 1, phase,
						"Sorted prefix grows by run merge", r.trace.events.empty() ? 0 : r.trace.events.size() - 1);
					r.endPhase(phase, "Block insertion run " + std::to_string(runStart) + "–" + std::to_string(runEnd));
				}
				runStart = runEnd;
			}
		}

		void binaryMergeSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
			{
				const std::size_t length = end - first;
				if (length < 2) return;
				if (length < 64)
				{
					binaryInsertionRange(r, first, end);
					r.addMetric("binary_insertion_runs");
					return;
				}
				const std::size_t middle = first + length / 2;
				sortRange(first, middle, depth + 1);
				sortRange(middle, end, depth + 1);
				mergeRange(r, first, middle, end);
				r.addMetric("merged_ranges");
				r.setMetric("maximum_merge_depth", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_merge_depth"], static_cast<std::uint64_t>(depth)));
			};
			sortRange(0, n, 1);
		}

		void optimizedBottomUpMergePass(Recorder& r, std::size_t width, bool sourceIsAuxiliary)
		{
			const std::size_t n = r.state.slots.size();
			const std::string lane = "optimized_bottom_up";
			r.addMetric("merge_passes");
			for (std::size_t first = 0; first < n; first += width * 2)
			{
				const std::size_t middle = std::min(first + width, n);
				const std::size_t end = std::min(first + width * 2, n);
				const std::size_t annotationBegin = r.trace.events.size();
				std::size_t left = first, right = middle;
				for (std::size_t out = first; out < end; ++out)
				{
					SortItem chosen;
					if (left >= middle)
						chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, right++) : r.at(right++);
					else if (right >= end)
						chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, left++) : r.at(left++);
					else
					{
						const SortItem a = sourceIsAuxiliary ? r.auxiliaryAt(lane, left) : r.at(left);
						const SortItem b = sourceIsAuxiliary ? r.auxiliaryAt(lane, right) : r.at(right);
						const std::string sourceLane = sourceIsAuxiliary ? lane : "main";
						r.compare(a, b, left, right, sourceLane, sourceLane);
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, left++) : r.at(left++);
						else
							chosen = sourceIsAuxiliary ? r.auxiliaryAt(lane, right++) : r.at(right++);
					}
					if (sourceIsAuxiliary) r.writeMain(out, chosen);
					else r.writeAuxiliary(lane, out, chosen);
				}
				r.annotate(sourceIsAuxiliary ? "main" : lane, RegionKind::Contiguous, first, end, 1, -1,
					"Optimized bottom-up merge width " + std::to_string(width), annotationBegin);
			}
		}

		void optimizedBottomUpMergeSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < n; first += runSize)
			{
				binaryInsertionRange(r, first, std::min(first + runSize, n));
				r.addMetric("binary_insertion_runs");
			}
			if (n <= runSize) return;
			r.resizeAuxiliary("optimized_bottom_up", n);
			for (std::size_t width = runSize; width < n; width *= 4)
			{
				optimizedBottomUpMergePass(r, width, false);
				optimizedBottomUpMergePass(r, width * 2, true);
			}
		}

		std::size_t introMedianOfThree(Recorder& r, std::size_t first, std::size_t middle, std::size_t last)
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

		std::size_t introPartition(Recorder& r, std::size_t first, std::size_t end, std::size_t pivotIndex)
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

		void introSiftDown(Recorder& r, std::size_t first, std::size_t root, std::size_t heapSize)
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

		void introHeapSortRange(Recorder& r, std::size_t first, std::size_t end)
		{
			const std::size_t length = end - first;
			for (std::size_t start = length / 2; start > 0; --start)
				introSiftDown(r, first, start - 1, length);
			for (std::size_t remaining = length; remaining > 1; --remaining)
			{
				r.swap(first, first + remaining - 1);
				introSiftDown(r, first, 0, remaining - 1);
			}
		}

		void dropMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "drop_merge_kept", "drop_merge_dropped", "drop_merge_scan_comparisons",
				"drop_merge_early_fallbacks", "drop_merge_suffix_heap_sorts", "drop_merge_final_merges" })
				r.declareMetric(key);
			if (count < 2) return;

			r.resizeAuxiliary("drop_merge_kept", count);
			r.resizeAuxiliary("drop_merge_dropped", count);
			r.resizeAuxiliary("drop_merge_output", count);
			std::size_t kept = 0, dropped = 0;
			const std::size_t scanBegin = r.trace.events.size();
			bool fellBack = false;
			for (std::size_t index = 0; index < count; ++index)
			{
				const SortItem candidate = r.at(index);
				bool belongsToRun = kept == 0;
				if (kept != 0)
				{
					const SortItem tail = r.auxiliaryAt("drop_merge_kept", kept - 1);
					r.compare(tail, candidate, kept - 1, index, "drop_merge_kept", "main",
						SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					r.addMetric("drop_merge_scan_comparisons");
					belongsToRun = compareItems(tail, candidate, r.trace.direction) != ComparisonResult::Greater;
				}
				if (belongsToRun)
				{
					r.writeAuxiliary("drop_merge_kept", kept++, candidate);
					r.addMetric("drop_merge_kept");
				}
				else
				{
					r.writeAuxiliary("drop_merge_dropped", dropped++, candidate);
					r.addMetric("drop_merge_dropped");
				}

				const std::size_t processed = index + 1;
				if (processed >= (count + 3) / 4 && dropped * 5 > processed * 3)
				{
					// The source algorithm abandons its monotone subsequence when the
					// drop buffer grows too quickly. The original main array is still
					// untouched here, so the deterministic introspective fallback can
					// take over without reconstructing a partially compacted range.
					r.addMetric("drop_merge_early_fallbacks");
					r.annotate("main", RegionKind::Contiguous, 0, count, 1, -1,
						"Drop Merge early fallback", scanBegin);
					introHeapSortRange(r, 0, count);
					fellBack = true;
					break;
				}
			}
			if (!fellBack)
			{
				r.annotate("drop_merge_kept", RegionKind::Contiguous, 0, kept, 1, -1,
					"Drop Merge monotone subsequence", scanBegin);
				r.annotate("drop_merge_dropped", RegionKind::Contiguous, 0, dropped, 1, -1,
					"Drop Merge buffered items", scanBegin);
				if (dropped > 1)
				{
					for (std::size_t i = 0; i < dropped; ++i)
						r.writeMain(i, r.auxiliaryAt("drop_merge_dropped", i));
					introHeapSortRange(r, 0, dropped);
					r.addMetric("drop_merge_suffix_heap_sorts");
				}
				std::size_t left = 0, right = 0;
				const std::size_t mergeBegin = r.trace.events.size();
				for (std::size_t output = 0; output < count; ++output)
				{
					SortItem chosen;
					if (left >= kept) chosen = r.at(right++);
					else if (right >= dropped) chosen = r.auxiliaryAt("drop_merge_kept", left++);
					else
					{
						const SortItem a = r.auxiliaryAt("drop_merge_kept", left);
						const SortItem b = r.at(right);
						r.compare(a, b, left, right, "drop_merge_kept", "main",
							SemanticRole::ComparedItem, SemanticRole::ComparedItem);
						r.addMetric("drop_merge_scan_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							chosen = r.auxiliaryAt("drop_merge_kept", left++);
						else chosen = r.at(right++);
					}
					r.writeAuxiliary("drop_merge_output", output, chosen);
				}
				r.addMetric("drop_merge_final_merges");
				r.annotate("main", RegionKind::Contiguous, 0, count, 1, -1,
					"Drop Merge final merge", mergeBegin);
				for (std::size_t i = 0; i < count; ++i)
					r.writeMain(i, r.auxiliaryAt("drop_merge_output", i));
			}
			r.resizeAuxiliary("drop_merge_kept", 0);
			r.resizeAuxiliary("drop_merge_dropped", 0);
			r.resizeAuxiliary("drop_merge_output", 0);
		}

		std::size_t medianOfMediansIndex(Recorder& r, std::size_t first, std::size_t end)
		{
			while (end - first > 5)
			{
				std::size_t medianWrite = first;
				for (std::size_t group = first; group < end; group += 5)
				{
					const std::size_t groupEnd = (std::min)(end, group + 5);
					binaryInsertionRange(r, group, groupEnd);
					const std::size_t median = group + (groupEnd - group - 1) / 2;
					if (medianWrite != median) r.swap(medianWrite, median, SemanticRole::Pivot);
					++medianWrite;
				}
				end = medianWrite;
			}
			binaryInsertionRange(r, first, end);
			return first + (end - first) / 2;
		}

		void medianMergeRange(Recorder& r, std::size_t first, std::size_t end)
		{
			if (end - first < 2) return;
			const std::size_t runSize = 16;
			for (std::size_t run = first; run < end; run += runSize)
			{
				binaryInsertionRange(r, run, (std::min)(end, run + runSize));
				r.addMetric("median_merge_initial_runs");
			}
			for (std::size_t width = runSize; width < end - first;)
			{
				const int phase = r.beginPhase("Median Merge partition merge width " + std::to_string(width));
				for (std::size_t left = first; left < end; left += width * 2)
				{
					const std::size_t middle = (std::min)(end, left + width);
					const std::size_t right = (std::min)(end, middle + width);
					if (middle < right)
					{
						const std::size_t begin = r.trace.events.size();
						mergeRange(r, left, middle, right);
						r.annotate("main", RegionKind::Contiguous, left, right, 1, phase,
							"Median Merge partition range", begin);
						r.addMetric("median_merge_merge_ranges");
					}
				}
				r.endPhase(phase, "Median Merge partition merge width");
				r.addMetric("median_merge_merge_passes");
				if (width > (end - first) / 2) break;
				width *= 2;
			}
		}

		void medianMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "median_merge_partitions", "median_merge_comparisons", "median_merge_bad_partitions",
				"median_merge_median_of_medians_pivots", "median_merge_initial_runs", "median_merge_merge_ranges",
				"median_merge_merge_passes", "median_merge_maximum_depth", "median_merge_partition_fallbacks" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);

			struct Range { std::size_t first, end, depth; Range(std::size_t a, std::size_t b, std::size_t d) : first(a), end(b), depth(d) {} };
			std::vector<Range> pending;
			pending.push_back(Range(0, count, 0));
			while (!pending.empty())
			{
				Range range = pending.back();
				pending.pop_back();
				std::size_t first = range.first, end = range.end, depth = range.depth;
				bool useMedianOfMedians = false;
				while (end - first > 16)
				{
					r.setMetric("median_merge_maximum_depth", (std::max)(r.state.metrics.algorithm["median_merge_maximum_depth"],
						static_cast<std::uint64_t>(depth)));
					const std::size_t middle = first + (end - first) / 2;
					const std::size_t pivotIndex = useMedianOfMedians
						? medianOfMediansIndex(r, first, end)
						: introMedianOfThree(r, first, middle, end - 1);
					if (useMedianOfMedians) r.addMetric("median_merge_median_of_medians_pivots");
					const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
					const std::size_t split = introPartition(r, first, end, pivotIndex);
					r.addMetric("median_merge_partitions");
					r.addMetric("median_merge_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					if (split <= first || split >= end)
					{
						r.addMetric("median_merge_partition_fallbacks");
						medianMergeRange(r, first, end);
						break;
					}
					const std::size_t leftLength = split - first, rightLength = end - split;
					const bool badlyBalanced = (std::min)(leftLength, rightLength) * 16 < (std::max)(leftLength, rightLength);
					if (badlyBalanced)
						r.addMetric("median_merge_bad_partitions");
					useMedianOfMedians = badlyBalanced;
					++depth;
					if (leftLength <= rightLength)
					{
						medianMergeRange(r, first, split);
						first = split;
					}
					else
					{
						medianMergeRange(r, split, end);
						end = split;
					}
				}
				if (end > first && end - first <= 16)
					binaryInsertionRange(r, first, end);
			}
			r.resizeAuxiliary("merge", 0);
		}

		void introSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "partitions", "maximum_depth", "heap_fallbacks", "insertion_passes" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			std::size_t floorLog = 0;
			for (std::size_t n = count; n > 1; n >>= 1) ++floorLog;
			const std::size_t depthLimit = floorLog * 2;
			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t first, std::size_t end, std::size_t remainingDepth, std::size_t depth)
			{
				if (end - first <= 16) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				if (remainingDepth == 0)
				{
					r.addMetric("heap_fallbacks");
					introHeapSortRange(r, first, end);
					return;
				}
				r.addMetric("partitions");
				const std::size_t annotationBegin = r.trace.events.size();
				const std::size_t middle = first + (end - first) / 2;
				const std::size_t pivot = introMedianOfThree(r, first, middle, end - 1);
				const std::size_t split = introPartition(r, first, end, pivot);
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "IntroSort partition", annotationBegin);
				if (split < end) sortRange(split, end, remainingDepth - 1, depth + 1);
				if (split > first) sortRange(first, split, remainingDepth - 1, depth + 1);
			};
			sortRange(0, count, depthLimit, 1);

			for (std::size_t i = 1; i < count; ++i)
			{
				const SortItem key = r.at(i);
				r.hold(i);
				std::size_t position = i;
				while (position > 0)
				{
					const SortItem candidate = r.at(position - 1);
					r.compare(key, candidate, i, position - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(key, candidate, r.trace.direction) != ComparisonResult::Less) break;
					r.shift(position - 1, position);
					--position;
				}
				r.place(position, key);
			}
			r.addMetric("insertion_passes");
		}

		std::pair<std::size_t, std::size_t> pdqThreeWayPartition(Recorder& r, std::size_t first,
			std::size_t end, std::size_t pivotIndex)
		{
			const SortItem pivot = r.at(pivotIndex);
			std::size_t pivotPosition = pivotIndex;
			std::size_t lessEnd = first, scan = first, greaterBegin = end;
			while (scan < greaterBegin)
			{
				r.chargeWork();
				const SortItem item = r.at(scan);
				r.compare(item, pivot, scan, pivotPosition, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
				const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
				const bool isLess = order == ComparisonResult::Less;
				const bool isGreater = order == ComparisonResult::Greater;
				if (isLess)
				{
					if (lessEnd != scan)
					{
						r.swap(lessEnd, scan);
						if (pivotPosition == lessEnd) pivotPosition = scan;
						else if (pivotPosition == scan) pivotPosition = lessEnd;
					}
					++lessEnd;
					++scan;
				}
				else if (isGreater)
				{
					--greaterBegin;
					if (scan != greaterBegin)
					{
						r.swap(scan, greaterBegin);
						if (pivotPosition == scan) pivotPosition = greaterBegin;
						else if (pivotPosition == greaterBegin) pivotPosition = scan;
					}
				}
				else ++scan;
			}
			return std::make_pair(lessEnd, greaterBegin);
		}

		void pdqSort(Recorder& r, bool branchless)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "pdq_partitions", "pdq_comparisons", "pdq_bad_partitions", "pdq_heap_fallbacks",
				"pdq_maximum_depth", "pdq_equal_ranges", "pdq_insertion_ranges" }) r.declareMetric(key);
			if (count < 2) return;
			std::size_t log = 0;
			for (std::size_t n = count; n > 1; n >>= 1) ++log;
			const std::size_t depthLimit = 2 * log;
			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, branchless](std::size_t first, std::size_t end,
				std::size_t remainingDepth, std::size_t depth, std::size_t badPartitions)
			{
				const std::size_t length = end - first;
				if (length < 2) return;
				r.setMetric("pdq_maximum_depth", (std::max)(r.state.metrics.algorithm["pdq_maximum_depth"],
					static_cast<std::uint64_t>(depth)));
				if (length <= 16)
				{
					binaryInsertionRange(r, first, end);
					r.addMetric("pdq_insertion_ranges");
					return;
				}
				if (remainingDepth == 0)
				{
					r.addMetric("pdq_heap_fallbacks");
					introHeapSortRange(r, first, end);
					return;
				}
				const std::size_t middle = first + length / 2;
				const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
				const std::size_t pivotIndex = introMedianOfThree(r, first, middle, end - 1);
				const std::size_t annotationBegin = r.trace.events.size();
				std::size_t leftEnd = first, rightBegin = end;
				if (branchless)
				{
					const std::pair<std::size_t, std::size_t> equalRange = pdqThreeWayPartition(r, first, end, pivotIndex);
					leftEnd = equalRange.first;
					rightBegin = equalRange.second;
					if (rightBegin > leftEnd) r.addMetric("pdq_equal_ranges");
				}
				else
				{
					leftEnd = introPartition(r, first, end, pivotIndex);
					rightBegin = leftEnd;
				}
				r.addMetric("pdq_partitions");
				r.addMetric("pdq_comparisons", r.state.metrics.comparisons - comparisonsBefore);
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1,
					branchless ? "PDQ branchless three-way partition" : "PDQ Hoare partition", annotationBegin);
				const std::size_t leftSize = leftEnd - first, rightSize = end - rightBegin;
				const std::size_t smaller = (std::min)(leftSize, rightSize);
				const std::size_t nextBadPartitions = smaller < length / 8 ? badPartitions + 1 : 0;
				if (smaller < length / 8)
				{
					r.addMetric("pdq_bad_partitions");
				}
				if (nextBadPartitions >= 2 || (remainingDepth <= 2 && nextBadPartitions != 0))
				{
					if (leftSize > 16) { r.addMetric("pdq_heap_fallbacks"); introHeapSortRange(r, first, leftEnd); }
					if (rightSize > 16) { r.addMetric("pdq_heap_fallbacks"); introHeapSortRange(r, rightBegin, end); }
					return;
				}
				if (leftSize > 1) sortRange(first, leftEnd, remainingDepth - 1, depth + 1, nextBadPartitions);
				if (rightSize > 1) sortRange(rightBegin, end, remainingDepth - 1, depth + 1, nextBadPartitions);
			};
			sortRange(0, count, depthLimit, 1, 0);
		}

		void bufferPartitionMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "buffer_partition_selections", "buffer_partition_select_passes", "buffer_partition_fallbacks",
				"buffer_partition_merges", "buffer_partition_comparisons" }) r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const auto selectRank = [&r](std::size_t first, std::size_t end, std::size_t target) -> std::size_t
			{
				std::size_t low = first, high = end;
				std::size_t depthLimit = 0;
				for (std::size_t size = end - first; size > 1; size >>= 1) ++depthLimit;
				depthLimit = depthLimit * 2 + 1;
				while (high - low > 1)
				{
					r.chargeWork();
					if (depthLimit == 0 || high - low <= 3)
					{
						if (depthLimit == 0) r.addMetric("buffer_partition_fallbacks");
						binaryInsertionRange(r, low, high);
						return target;
					}
					--depthLimit;
					const std::size_t middle = low + (high - low) / 2;
					const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
					const std::size_t pivot = introMedianOfThree(r, low, middle, high - 1);
					const std::size_t split = introPartition(r, low, high, pivot);
					r.addMetric("buffer_partition_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					r.addMetric("buffer_partition_select_passes");
					if (split == target) return target;
					if (split <= low || split >= high)
					{
						r.addMetric("buffer_partition_fallbacks");
						introHeapSortRange(r, low, high);
						return target;
					}
					if (split < target) low = split;
					else high = split;
				}
				return target;
			};

			std::function<void(std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &selectRank](std::size_t first, std::size_t end)
			{
				r.chargeWork();
				if (end - first <= 16)
				{
					binaryInsertionRange(r, first, end);
					return;
				}
				const std::size_t middle = first + (end - first) / 2;
				r.addMetric("buffer_partition_selections");
				const std::size_t split = selectRank(first, end, middle);
				const std::size_t annotationBegin = r.trace.events.size();
				sortRange(first, split);
				sortRange(split, end);
				const int phase = r.beginPhase("Buffer-partitioned merge");
				mergeRange(r, first, split, end);
				r.endPhase(phase, "Buffer-partitioned merge");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Median-selected merge range", annotationBegin);
				r.addMetric("buffer_partition_merges");
			};
			sortRange(0, count);
			r.resizeAuxiliary("merge", 0);
		}

		void parallelBlockMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "parallel_block_runs", "parallel_block_tasks", "parallel_block_layers", "parallel_block_comparisons" })
				r.declareMetric(key);
			if (count < 2) return;
			r.resizeAuxiliary("merge", count);
			const std::size_t runSize = 16;
			for (std::size_t first = 0; first < count; first += runSize)
			{
				binaryInsertionRange(r, first, (std::min)(count, first + runSize));
				r.addMetric("parallel_block_runs");
			}
			for (std::size_t width = runSize; width < count;)
			{
				const int phase = r.beginPhase("Parallel block merge layer width " + std::to_string(width));
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t begin = r.trace.events.size();
						const std::uint64_t comparisonsBefore = r.state.metrics.comparisons;
						mergeRange(r, first, middle, last);
						r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Independent merge block", begin);
						r.addMetric("parallel_block_tasks");
						r.addMetric("parallel_block_comparisons", r.state.metrics.comparisons - comparisonsBefore);
					}
				}
				r.endPhase(phase, "Parallel block merge layer");
				r.addMetric("parallel_block_layers");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("merge", 0);
		}

		void optimizedRotateMergeSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			for (const char* key : { "optimized_rotate_runs", "optimized_rotate_merges", "optimized_rotate_buffer_merges",
				"optimized_rotate_in_place_merges", "optimized_rotate_buffer_writes", "optimized_rotate_comparisons",
				"optimized_rotate_trimmed_items" }) r.declareMetric(key);
			if (count < 2) return;
			const std::size_t bufferCapacity = static_cast<std::size_t>((std::max)(1, (std::min)(256, r.trace.rotateMergeBufferSize)));
			r.resizeAuxiliary("rotate_merge_buffer", bufferCapacity);

			const auto bufferedMerge = [&r, bufferCapacity](std::size_t first, std::size_t middle, std::size_t last)
			{
				if (first >= middle || middle >= last) return;
				const SortItem firstRight = r.at(middle);
				const SortItem lastLeft = r.at(middle - 1);
				r.compare(lastLeft, firstRight, middle - 1, middle, "main", "main");
				r.addMetric("optimized_rotate_comparisons");
				if (compareItems(lastLeft, firstRight, r.trace.direction) != ComparisonResult::Greater) return;

				// Trim the already ordered prefix and suffix around the merge. Equal left
				// items stay before equal right items, preserving stability.
				std::size_t low = first, high = middle;
				while (low < high)
				{
					const std::size_t probe = low + (high - low) / 2;
					const SortItem candidate = r.at(probe);
					r.probeItem("main", probe, candidate, SemanticRole::ProbedItem);
					r.compare(candidate, firstRight, probe, middle, "main", "main");
					r.addMetric("optimized_rotate_comparisons");
					if (compareItems(candidate, firstRight, r.trace.direction) != ComparisonResult::Greater) low = probe + 1;
					else high = probe;
				}
				const std::size_t mergeFirst = low;
				low = middle; high = last;
				while (low < high)
				{
					const std::size_t probe = low + (high - low) / 2;
					const SortItem candidate = r.at(probe);
					r.probeItem("main", probe, candidate, SemanticRole::ProbedItem);
					r.compare(candidate, lastLeft, probe, middle - 1, "main", "main");
					r.addMetric("optimized_rotate_comparisons");
					if (compareItems(candidate, lastLeft, r.trace.direction) == ComparisonResult::Less) low = probe + 1;
					else high = probe;
				}
				const std::size_t mergeLast = low;
				r.addMetric("optimized_rotate_trimmed_items", (mergeFirst - first) + (last - mergeLast));
				const std::size_t leftCount = middle - mergeFirst;
				const std::size_t rightCount = mergeLast - middle;
				if (leftCount == 0 || rightCount == 0) return;
				if ((std::min)(leftCount, rightCount) > bufferCapacity)
				{
					// The source falls back to recursive binary splitting and rotations
					// when neither run fits its configured temporary array.
					grailMergeWithoutBuffer(r, mergeFirst, leftCount, rightCount, "optimized_rotate");
					r.addMetric("optimized_rotate_in_place_merges");
					return;
				}

				if (leftCount <= rightCount)
				{
					for (std::size_t offset = 0; offset < leftCount; ++offset)
					{
						r.writeAuxiliary("rotate_merge_buffer", offset, r.at(mergeFirst + offset));
						r.addMetric("optimized_rotate_buffer_writes");
					}
					std::size_t left = 0, right = middle, output = mergeFirst;
					while (left < leftCount && right < mergeLast)
					{
						const SortItem a = r.auxiliaryAt("rotate_merge_buffer", left);
						const SortItem b = r.at(right);
						r.compare(a, b, left, right, "rotate_merge_buffer", "main", SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem);
						r.addMetric("optimized_rotate_comparisons");
						if (compareItems(a, b, r.trace.direction) != ComparisonResult::Greater)
							r.writeMain(output++, r.auxiliaryAt("rotate_merge_buffer", left++));
						else r.writeMain(output++, r.at(right++));
					}
					while (left < leftCount) r.writeMain(output++, r.auxiliaryAt("rotate_merge_buffer", left++));
				}
				else
				{
					for (std::size_t offset = 0; offset < rightCount; ++offset)
					{
						r.writeAuxiliary("rotate_merge_buffer", offset, r.at(middle + offset));
						r.addMetric("optimized_rotate_buffer_writes");
					}
					std::size_t left = middle, right = rightCount, output = mergeLast;
					while (left > mergeFirst && right > 0)
					{
						const SortItem a = r.at(left - 1);
						const SortItem b = r.auxiliaryAt("rotate_merge_buffer", right - 1);
						r.compare(a, b, left - 1, right - 1, "main", "rotate_merge_buffer", SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy);
						r.addMetric("optimized_rotate_comparisons");
						if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
							r.writeMain(--output, r.at(--left));
						else r.writeMain(--output, r.auxiliaryAt("rotate_merge_buffer", --right));
					}
					while (right > 0) r.writeMain(--output, r.auxiliaryAt("rotate_merge_buffer", --right));
				}
				r.addMetric("optimized_rotate_buffer_merges");
			};

			const std::size_t run = 32;
			for (std::size_t first = 0; first < count; first += run)
			{
				const std::size_t last = (std::min)(count, first + run);
				binaryInsertionRange(r, first, last);
				r.addMetric("optimized_rotate_runs");
			}
		for (std::size_t width = run; width < count;)
			{
				const int phase = r.beginPhase("Optimized rotate-merge pass");
				for (std::size_t first = 0; first < count; first += width * 2)
				{
					const std::size_t middle = (std::min)(count, first + width);
					const std::size_t last = (std::min)(count, middle + width);
					if (middle < last)
					{
						const std::size_t begin = r.trace.events.size();
						bufferedMerge(first, middle, last);
						r.annotate("main", RegionKind::Contiguous, first, last, 1, -1, "Buffered or rotated merge", begin);
						r.addMetric("optimized_rotate_merges");
					}
				}
				r.endPhase(phase, "Optimized rotate-merge pass");
				if (width > count / 2) break;
				width *= 2;
			}
			r.resizeAuxiliary("rotate_merge_buffer", 0);
		}

		void patienceSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "pile_count", "pile_assignments", "maximum_pile_height", "heap_pushes", "heap_pops" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			std::vector<std::string> lanes;
			std::vector<std::size_t> pileSizes;
			std::vector<std::size_t> pileMaximumSizes;
			std::vector<std::size_t> pileBeginCursors;
			std::map<std::uint64_t, std::size_t> originalPositions;
			for (std::size_t i = 0; i < r.trace.input.size(); ++i) originalPositions[r.trace.input[i].stableId] = i;

			for (std::size_t inputIndex = 0; inputIndex < count; ++inputIndex)
			{
				const SortItem item = r.at(inputIndex);
				std::size_t low = 0, high = lanes.size();
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const std::size_t topIndex = pileSizes[middle] - 1;
					const SortItem top = r.auxiliaryAt(lanes[middle], topIndex);
					r.compare(item, top, inputIndex, topIndex, "main", lanes[middle], SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy);
					if (compareItems(item, top, r.trace.direction) == ComparisonResult::Greater) low = middle + 1;
					else high = middle;
				}
				if (low == lanes.size())
				{
					lanes.push_back("patience.pile." + std::to_string(lanes.size()));
					pileSizes.push_back(0);
					pileMaximumSizes.push_back(0);
					pileBeginCursors.push_back(r.trace.events.size());
				}
				const std::string& lane = lanes[low];
				const std::size_t insertionCursor = r.trace.events.size();
				r.writeAuxiliary(lane, pileSizes[low], item);
				++pileSizes[low];
				pileMaximumSizes[low] = std::max(pileMaximumSizes[low], pileSizes[low]);
				pileBeginCursors[low] = std::min(pileBeginCursors[low], insertionCursor);
				r.addMetric("pile_assignments");
				r.setMetric("pile_count", lanes.size());
				r.setMetric("maximum_pile_height", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_pile_height"], pileSizes[low]));
			}

			const std::string heapLane = "patience.priority_queue";
			r.resizeCounters(heapLane, 0);
			const auto pileHasPriority = [&r, &lanes, &pileSizes, &originalPositions](std::size_t firstPile, std::size_t secondPile)
			{
				const std::size_t firstIndex = pileSizes[firstPile] - 1;
				const std::size_t secondIndex = pileSizes[secondPile] - 1;
				const SortItem first = r.auxiliaryAt(lanes[firstPile], firstIndex);
				const SortItem second = r.auxiliaryAt(lanes[secondPile], secondIndex);
				r.compare(first, second, firstIndex, secondIndex, lanes[firstPile], lanes[secondPile],
					SemanticRole::AuxiliaryCopy, SemanticRole::AuxiliaryCopy);
				const ComparisonResult order = compareItems(first, second, r.trace.direction);
				if (order != ComparisonResult::Equal) return order == ComparisonResult::Less;
				return originalPositions[first.stableId] < originalPositions[second.stableId];
			};
			const auto exchangeHeapEntries = [&r, &heapLane](std::size_t first, std::size_t second)
			{
				const int firstPile = r.counter(heapLane, first), secondPile = r.counter(heapLane, second);
				r.writeCounter(heapLane, first, secondPile);
				r.writeCounter(heapLane, second, firstPile);
			};

			std::size_t heapSize = 0;
			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
			{
				r.resizeCounters(heapLane, heapSize + 1);
				r.writeCounter(heapLane, heapSize, static_cast<int>(pile));
				std::size_t child = heapSize++;
				while (child > 0)
				{
					const std::size_t parent = (child - 1) / 2;
					const std::size_t childPile = static_cast<std::size_t>(r.counter(heapLane, child));
					const std::size_t parentPile = static_cast<std::size_t>(r.counter(heapLane, parent));
					if (!pileHasPriority(childPile, parentPile)) break;
					exchangeHeapEntries(parent, child);
					child = parent;
				}
				r.addMetric("heap_pushes");
			}

			const auto siftHeapDown = [&r, &heapLane, &pileHasPriority, &exchangeHeapEntries](std::size_t root, std::size_t size)
			{
				for (;;)
				{
					const std::size_t leftChild = root * 2 + 1;
					if (leftChild >= size) return;
					std::size_t best = leftChild;
					const std::size_t rightChild = leftChild + 1;
					if (rightChild < size && pileHasPriority(
						static_cast<std::size_t>(r.counter(heapLane, rightChild)),
						static_cast<std::size_t>(r.counter(heapLane, leftChild)))) best = rightChild;
					if (!pileHasPriority(static_cast<std::size_t>(r.counter(heapLane, best)),
						static_cast<std::size_t>(r.counter(heapLane, root)))) return;
					exchangeHeapEntries(root, best);
					root = best;
				}
			};

			for (std::size_t output = 0; output < count; ++output)
			{
				const std::size_t chosenPile = static_cast<std::size_t>(r.counter(heapLane, 0));
				const std::size_t topIndex = pileSizes[chosenPile] - 1;
				const SortItem chosen = r.auxiliaryAt(lanes[chosenPile], topIndex);
				r.writeMain(output, chosen);
				r.resizeAuxiliary(lanes[chosenPile], topIndex);
				--pileSizes[chosenPile];
				r.addMetric("heap_pops");
				if (pileSizes[chosenPile] == 0)
				{
					if (heapSize == 1)
					{
						r.resizeCounters(heapLane, 0);
						heapSize = 0;
					}
					else
					{
						const int lastPile = r.counter(heapLane, heapSize - 1);
						r.writeCounter(heapLane, 0, lastPile);
						r.resizeCounters(heapLane, heapSize - 1);
						--heapSize;
						siftHeapDown(0, heapSize);
					}
				}
				else siftHeapDown(0, heapSize);
			}

			for (std::size_t pile = 0; pile < lanes.size(); ++pile)
				r.annotate(lanes[pile], RegionKind::Group, 0, pileMaximumSizes[pile], 1, static_cast<int>(pile),
					"Patience pile " + std::to_string(pile + 1), pileBeginCursors[pile]);
		}

		void mergeInsertionBlockSwap(Recorder& r, std::size_t firstEnd, std::size_t secondEnd, std::size_t blockSize)
		{
			for (std::size_t offset = 0; offset < blockSize; ++offset)
				r.swap(firstEnd - offset, secondEnd - offset);
			r.addMetric("block_swaps");
		}

		void mergeInsertionBlockInsert(Recorder& r, std::size_t sourceEnd, std::size_t destinationEnd, std::size_t blockSize)
		{
			while (sourceEnd >= blockSize && sourceEnd - blockSize >= destinationEnd)
			{
				mergeInsertionBlockSwap(r, sourceEnd - blockSize, sourceEnd, blockSize);
				sourceEnd -= blockSize;
				r.addMetric("block_insertions");
			}
		}

		void mergeInsertionBlockReverse(Recorder& r, std::size_t firstEnd, std::size_t end, std::size_t blockSize)
		{
			if (end < blockSize) return;
			std::size_t lastEnd = end - blockSize;
			while (lastEnd > firstEnd)
			{
				mergeInsertionBlockSwap(r, firstEnd, lastEnd, blockSize);
				firstEnd += blockSize;
				if (lastEnd < blockSize) break;
				lastEnd -= blockSize;
				r.addMetric("block_reversals");
			}
		}

		std::size_t mergeInsertionBlockSearch(Recorder& r, const SortItem& key, std::size_t first,
			std::size_t end, std::size_t blockSize, std::size_t keyIndex)
		{
			while (first < end)
			{
				const std::size_t middle = first + (((end - first) / blockSize) / 2) * blockSize;
				const SortItem candidate = r.at(middle);
				r.compare(key, candidate, keyIndex, middle, "main", "main", SemanticRole::ProbedItem, SemanticRole::ComparedItem);
				if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) end = middle;
				else first = middle + blockSize;
			}
			return first;
		}

		void mergeInsertionOrderBlocks(Recorder& r, std::size_t firstEnd, std::size_t end,
			std::size_t blockSize)
		{
			for (std::size_t leftEnd = firstEnd, rightEnd = leftEnd + blockSize;
				rightEnd < end; leftEnd += blockSize, rightEnd += 2 * blockSize)
				mergeInsertionBlockInsert(r, rightEnd, leftEnd, blockSize);
			const std::size_t middleEnd = firstEnd + (((end - firstEnd) / blockSize) / 2) * blockSize;
			mergeInsertionBlockReverse(r, middleEnd, end, blockSize);
			r.addMetric("block_orderings");
		}

		void mergeInsertionSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "block_swaps", "block_insertions", "block_reversals", "block_orderings", "block_searches", "block_levels" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			std::size_t blockSize = 1;
			while (blockSize <= count / 2)
			{
				for (std::size_t end = 2 * blockSize - 1; end < count; end += 2 * blockSize)
				{
					const SortItem left = r.at(end - blockSize), right = r.at(end);
					r.compare(left, right, end - blockSize, end);
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater)
						mergeInsertionBlockSwap(r, end - blockSize, end, blockSize);
				}
				blockSize *= 2;
			}

			while (blockSize > 0)
			{
				r.addMetric("block_levels");
				const std::size_t firstEnd = blockSize - 1;
				std::size_t runEnd = firstEnd + 2 * blockSize;
				std::size_t group = 2, jacobsthal = 4;
				while (runEnd <= count && 2 * blockSize * group - blockSize <= count - runEnd)
				{
					mergeInsertionOrderBlocks(r, runEnd, runEnd + 2 * blockSize * group - blockSize, blockSize);
					const std::size_t searchEnd = firstEnd + blockSize * (jacobsthal - 1);
					runEnd += blockSize * group - blockSize;
					for (std::size_t keyIndex = runEnd; keyIndex < runEnd + blockSize * group; keyIndex += blockSize)
					{
						if (keyIndex >= count) break;
						const SortItem key = r.at(keyIndex);
						const std::size_t destination = mergeInsertionBlockSearch(r, key, firstEnd, searchEnd, blockSize, keyIndex);
						r.addMetric("block_searches");
						mergeInsertionBlockInsert(r, keyIndex, destination, blockSize);
					}
					runEnd += blockSize * group + blockSize;
					group = jacobsthal - group;
					jacobsthal *= 2;
				}
				while (runEnd < count)
				{
					const SortItem key = r.at(runEnd);
					const std::size_t destination = mergeInsertionBlockSearch(r, key, firstEnd, runEnd, blockSize, runEnd);
					r.addMetric("block_searches");
					mergeInsertionBlockInsert(r, runEnd, destination, blockSize);
					runEnd += 2 * blockSize;
				}
				blockSize /= 2;
			}
		}

		void quickSort(Recorder& r)
		{
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t low, std::size_t high, std::size_t depth)
			{
				if (low >= high) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_depth"], static_cast<std::uint64_t>(depth)));
				const std::size_t begin = r.trace.events.size();
				const std::size_t pivotIndex = low + (high - low) / 2;
				const SortItem pivot = r.at(pivotIndex);
				r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
				std::ptrdiff_t i = static_cast<std::ptrdiff_t>(low) - 1;
				std::ptrdiff_t j = static_cast<std::ptrdiff_t>(high) + 1;
				while (true)
				{
					do { ++i; const SortItem item = r.at(static_cast<std::size_t>(i));
						r.compare(item, pivot, static_cast<std::size_t>(i), pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					} while (compareItems(r.at(static_cast<std::size_t>(i)), pivot, r.trace.direction) == ComparisonResult::Less);
					do { --j; const SortItem item = r.at(static_cast<std::size_t>(j));
						r.compare(item, pivot, static_cast<std::size_t>(j), pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					} while (compareItems(r.at(static_cast<std::size_t>(j)), pivot, r.trace.direction) == ComparisonResult::Greater);
					if (i >= j) break;
					r.swap(static_cast<std::size_t>(i), static_cast<std::size_t>(j), SemanticRole::Pivot);
				}
				const std::size_t split = static_cast<std::size_t>(j);
				r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1, "Quick partition", begin);
				if (split > low) sortRange(low, split, depth + 1);
				if (split + 1 < high) sortRange(split + 1, high, depth + 1);
			};
			if (!r.state.slots.empty()) sortRange(0, r.state.slots.size() - 1, 1);
		}

		void leftLeftQuickSort(Recorder& r)
		{
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange](std::size_t low, std::size_t high, std::size_t depth)
			{
				if (low >= high) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
				const std::size_t begin = r.trace.events.size();
				const SortItem pivot = r.at(high);
				std::size_t boundary = low;
				for (std::size_t scan = low; scan < high; ++scan)
				{
					const SortItem item = r.at(scan);
					r.compare(item, pivot, scan, high, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					if (compareItems(item, pivot, r.trace.direction) == ComparisonResult::Less)
						r.swap(boundary++, scan, SemanticRole::Pivot);
				}
				r.swap(boundary, high, SemanticRole::Pivot);
				r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1, "Left-left quick partition", begin);
				if (boundary > low) sortRange(low, boundary - 1, depth + 1);
				if (boundary < high) sortRange(boundary + 1, high, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size() - 1, 1);
		}

		void leftRightQuickSort(Recorder& r, bool parallelSchedule)
		{
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, parallelSchedule](std::size_t low, std::size_t high, std::size_t depth)
			{
				if (low >= high) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				r.setMetric("partitions", r.state.metrics.algorithm["partitions"] + 1);
				const std::size_t begin = r.trace.events.size();
				const std::size_t pivotIndex = low + (high - low + 1) / 2;
				const SortItem pivot = r.at(pivotIndex);
				std::ptrdiff_t left = static_cast<std::ptrdiff_t>(low);
				std::ptrdiff_t right = static_cast<std::ptrdiff_t>(high);
				while (left <= right)
				{
					while (left <= right)
					{
						const std::size_t index = static_cast<std::size_t>(left);
						const SortItem item = r.at(index);
						r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot, r.trace.direction) != ComparisonResult::Less) break;
						++left;
					}
					while (left <= right)
					{
						const std::size_t index = static_cast<std::size_t>(right);
						const SortItem item = r.at(index);
						r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot, r.trace.direction) != ComparisonResult::Greater) break;
						--right;
					}
					if (left <= right)
					{
						r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(right), SemanticRole::Pivot);
						++left;
						--right;
					}
				}
				r.annotate("main", RegionKind::Contiguous, low, high + 1, 1, -1,
					parallelSchedule ? "Parallel left-right quick partition" : "Left-right quick partition", begin);
				int phase = -1;
				if (parallelSchedule) phase = r.beginPhase("Parallel left-right quick subranges");
				if (static_cast<std::ptrdiff_t>(low) < right) sortRange(low, static_cast<std::size_t>(right), depth + 1);
				if (left < static_cast<std::ptrdiff_t>(high)) sortRange(static_cast<std::size_t>(left), high, depth + 1);
				if (parallelSchedule) r.endPhase(phase, "Parallel left-right quick subranges");
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size() - 1, 1);
		}

		void dualPivotQuickSort(Recorder& r, bool optimized)
		{
			const std::size_t insertionThreshold = optimized ? 27 : 4;
			std::function<void(std::size_t, std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, optimized, insertionThreshold](std::size_t first, std::size_t end,
				std::size_t divisor, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				const std::size_t length = end - first;
				if (length <= insertionThreshold)
				{
					r.addMetric("insertion_fallback_ranges");
					for (std::size_t i = first + 1; i < end; ++i)
						for (std::size_t j = i; j > first; --j)
						{
							const SortItem left = r.at(j - 1), right = r.at(j);
							r.compare(left, right, j - 1, j, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
							if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
							r.swap(j - 1, j, SemanticRole::ComparedItem);
						}
					return;
				}
				const std::size_t begin = r.trace.events.size();
				r.addMetric("partitions");
				const std::size_t third = length / divisor;
				std::size_t firstSample = first + third;
				std::size_t secondSample = end - 1 - third;
				if (firstSample <= first) firstSample = first + 1;
				if (secondSample >= end - 1) secondSample = end - 2;
				const SortItem firstSampleItem = r.at(firstSample), secondSampleItem = r.at(secondSample);
				r.compare(firstSampleItem, secondSampleItem, firstSample, secondSample, "main", "main",
					SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				if (compareItems(firstSampleItem, secondSampleItem, r.trace.direction) == ComparisonResult::Less)
				{
					r.swap(firstSample, first, SemanticRole::Pivot);
					r.swap(secondSample, end - 1, SemanticRole::Pivot);
				}
				else
				{
					r.swap(firstSample, end - 1, SemanticRole::Pivot);
					r.swap(secondSample, first, SemanticRole::Pivot);
				}
				const SortItem pivot1 = r.at(first), pivot2 = r.at(end - 1);
				std::size_t less = first + 1;
				std::size_t great = end - 2;
				for (std::size_t scan = less; scan <= great; ++scan)
				{
					SortItem item = r.at(scan);
					r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Less)
					{
						r.swap(scan, less++, SemanticRole::Pivot);
						continue;
					}
					r.compare(item, pivot2, scan, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					if (compareItems(item, pivot2, r.trace.direction) != ComparisonResult::Greater) continue;
					while (scan < great)
					{
						const SortItem right = r.at(great);
						r.compare(right, pivot2, great, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(right, pivot2, r.trace.direction) != ComparisonResult::Greater) break;
						--great;
					}
					r.swap(scan, great, SemanticRole::Pivot);
					if (great > 0) --great;
					item = r.at(scan);
					r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Less)
						r.swap(scan, less++, SemanticRole::Pivot);
					if (scan == std::numeric_limits<std::size_t>::max()) break;
				}
				const std::size_t distance = great >= less ? great - less : 0;
				if (distance < 13) ++divisor;
				r.swap(less - 1, first, SemanticRole::Pivot);
				r.swap(great + 1, end - 1, SemanticRole::Pivot);
				const std::size_t middleFirst = less;
				std::size_t middleEnd = great + 1;
				r.annotate("main", RegionKind::Contiguous, first, less - 1, 1, -1, "Below first pivot", begin);
				r.annotate("main", RegionKind::Contiguous, less, middleEnd, 1, -1, "Between pivots", begin);
				r.annotate("main", RegionKind::Contiguous, great + 2, end, 1, -1, "Above second pivot", begin);
				if (!optimized)
				{
					sortRange(first, less - 1, divisor, depth + 1);
					if (compareItems(pivot1, pivot2, r.trace.direction) == ComparisonResult::Less)
						sortRange(middleFirst, middleEnd, divisor, depth + 1);
					sortRange(great + 2, end, divisor, depth + 1);
					return;
				}
				sortRange(first, less - 1, divisor, depth + 1);
				sortRange(great + 2, end, divisor, depth + 1);
				if (distance > length - std::min<std::size_t>(length, 13) &&
					compareItems(pivot1, pivot2, r.trace.direction) != ComparisonResult::Equal)
				{
					for (std::size_t scan = less; scan < middleEnd; ++scan)
					{
						const SortItem item = r.at(scan);
						r.compare(item, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot1, r.trace.direction) == ComparisonResult::Equal)
						{
							r.swap(scan, less++, SemanticRole::Pivot);
							r.addMetric("equal_key_moves");
							continue;
						}
						r.compare(item, pivot2, scan, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						if (compareItems(item, pivot2, r.trace.direction) == ComparisonResult::Equal)
						{
							r.swap(scan, --middleEnd, SemanticRole::Pivot);
							r.addMetric("equal_key_moves");
							const SortItem moved = r.at(scan);
							r.compare(moved, pivot1, scan, first, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							if (compareItems(moved, pivot1, r.trace.direction) == ComparisonResult::Equal)
							{
								r.swap(scan, less++, SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
							}
						}
					}
				}
				r.annotate("main", RegionKind::Contiguous, less, middleEnd, 1, -1, "Equal-key middle region", r.trace.events.size());
				if (compareItems(pivot1, pivot2, r.trace.direction) == ComparisonResult::Less)
					sortRange(less, middleEnd, divisor, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 3, 1);
		}

		void stableQuickSort(Recorder& r, bool parallelSchedule)
		{
			std::mt19937 random(r.trace.inputSeed ^ (parallelSchedule ? 0x57AB1E00u : 0x57AB1E01u));
			std::map<std::uint64_t, std::size_t> originalOrder;
			for (std::size_t i = 0; i < r.trace.input.size(); ++i) originalOrder[r.trace.input[i].stableId] = i;
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &random, &originalOrder, parallelSchedule](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				r.addMetric("partitions");
				const std::size_t begin = r.trace.events.size();
				std::uniform_int_distribution<std::size_t> choose(first, end - 1);
				const std::size_t pivotIndex = parallelSchedule ? choose(random) : first;
				const SortItem pivot = r.at(pivotIndex);
				const std::string leftLane = "stable_quick_left";
				const std::string rightLane = "stable_quick_right";
				std::vector<SortItem> left, right, ordered;
				left.reserve(end - first);
				right.reserve(end - first);
				for (std::size_t index = first; index < end; ++index)
				{
					if (index == pivotIndex) continue;
					const SortItem item = r.at(index);
					r.compare(item, pivot, index, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
					ComparisonResult order = compareItems(item, pivot, r.trace.direction);
					if (parallelSchedule && order == ComparisonResult::Equal)
					{
						r.addMetric("stable_tie_breaks");
						order = originalOrder[item.stableId] < originalOrder[pivot.stableId]
							? ComparisonResult::Less : ComparisonResult::Greater;
					}
					if (order == ComparisonResult::Less)
					{
						left.push_back(item);
						r.writeAuxiliary(leftLane, left.size() - 1, item);
					}
					else
					{
						right.push_back(item);
						r.writeAuxiliary(rightLane, right.size() - 1, item);
					}
				}
				ordered.reserve(end - first);
				ordered.insert(ordered.end(), left.begin(), left.end());
				ordered.push_back(pivot);
				ordered.insert(ordered.end(), right.begin(), right.end());
				r.writeMainRange(first, ordered);
				r.resizeAuxiliary(leftLane, 0);
				r.resizeAuxiliary(rightLane, 0);
				const std::size_t pivotPosition = first + left.size();
				r.annotate("main", RegionKind::Contiguous, first, pivotPosition, 1, -1, "Stable quick left partition", begin);
				r.annotate("main", RegionKind::Contiguous, pivotPosition, pivotPosition + 1, 1, -1, "Stable quick pivot", begin);
				r.annotate("main", RegionKind::Contiguous, pivotPosition + 1, end, 1, -1, "Stable quick right partition", begin);
				if (parallelSchedule)
				{
					const int phase = r.beginPhase("Stable quick recursive branches");
					sortRange(first, pivotPosition, depth + 1);
					sortRange(pivotPosition + 1, end, depth + 1);
					r.endPhase(phase, "Stable quick recursive branches complete");
				}
				else
				{
					sortRange(first, pivotPosition, depth + 1);
					sortRange(pivotPosition + 1, end, depth + 1);
				}
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		void forcedStableQuickSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const std::string orderLane = "forced_stable_order";
			r.resizeCounters(orderLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeCounter(orderLane, i, static_cast<int>(i));
			const auto stableGreater = [&r, &orderLane](std::size_t first, std::size_t second)
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
				const ComparisonResult valueOrder = compareItems(a, b, r.trace.direction);
				if (valueOrder != ComparisonResult::Equal) return valueOrder == ComparisonResult::Greater;
				r.addMetric("stable_tie_breaks");
				return r.counter(orderLane, first) > r.counter(orderLane, second);
			};
			const auto stableSwap = [&r, &orderLane](std::size_t first, std::size_t second)
			{
				if (first == second) return;
				const int firstOrder = r.counter(orderLane, first), secondOrder = r.counter(orderLane, second);
				r.swap(first, second, SemanticRole::Pivot);
				r.writeCounter(orderLane, first, secondOrder);
				r.writeCounter(orderLane, second, firstOrder);
			};
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &stableGreater, &stableSwap](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				if (end - first < 3)
				{
					if (stableGreater(first, first + 1)) stableSwap(first, first + 1);
					return;
				}
				r.addMetric("partitions");
				const std::size_t middle = first + (end - 1 - first) / 2;
				if (stableGreater(first, middle)) stableSwap(first, middle);
				bool medianIsReady = false;
				if (stableGreater(middle, end - 1))
				{
					stableSwap(middle, end - 1);
					medianIsReady = stableGreater(first, middle);
				}
				if (!medianIsReady) stableSwap(first, middle);
				const std::size_t pivot = first;
				std::size_t left = first;
				std::size_t right = end;
				std::size_t split = first;
				while (true)
				{
					++left;
					while (left < right && !stableGreater(left, pivot)) ++left;
					--right;
					while (right >= left && stableGreater(right, pivot)) --right;
					if (left < right) stableSwap(left, right);
					else { split = right; break; }
				}
				stableSwap(first, split);
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Forced-stable quick partition", r.trace.events.size());
				if (split > first) sortRange(first, split, depth + 1);
				if (split + 1 < end) sortRange(split + 1, end, depth + 1);
			};
			if (count > 1) sortRange(0, count, 1);
			r.resizeCounters(orderLane, 0);
		}

		void ternaryQuickSort(Recorder& r, bool leftLeftPartition)
		{
			const auto compareIndices = [&r](std::size_t first, std::size_t second)
			{
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
				return compareItems(a, b, r.trace.direction);
			};
			const auto selectPivot = [&compareIndices](std::size_t first, std::size_t end)
			{
				const std::size_t middle = first + (end - first) / 2;
				const std::size_t last = end - 1;
				if (compareIndices(first, middle) == ComparisonResult::Equal) return first;
				if (compareIndices(first, last) == ComparisonResult::Equal ||
					compareIndices(middle, last) == ComparisonResult::Equal) return last;
				return compareIndices(first, middle) == ComparisonResult::Less
					? (compareIndices(middle, last) == ComparisonResult::Less ? middle :
						(compareIndices(first, last) == ComparisonResult::Less ? last : first))
					: (compareIndices(middle, last) == ComparisonResult::Greater ? middle :
						(compareIndices(first, last) == ComparisonResult::Less ? first : last));
			};
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &compareIndices, &selectPivot, leftLeftPartition](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(
					r.state.metrics.algorithm["maximum_depth"], static_cast<std::uint64_t>(depth)));
				const std::size_t begin = r.trace.events.size();
				r.addMetric("partitions");
				const std::size_t pivotIndex = selectPivot(first, end);
				const SortItem pivot = r.at(pivotIndex);
				r.swap(pivotIndex, end - 1, SemanticRole::Pivot);
				std::size_t leftEnd = first, rightBegin = first;
				if (leftLeftPartition)
				{
					std::size_t left = first, equalStart = end - 1;
					std::size_t current = first;
					while (current < equalStart)
					{
						const SortItem item = r.at(current);
						r.compare(item, pivot, current, end - 1, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
						const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
						if (order == ComparisonResult::Equal)
						{
							--equalStart;
							r.swap(equalStart, current, SemanticRole::Pivot);
							r.addMetric("equal_key_moves");
						}
						else
					{
						if (order == ComparisonResult::Less) r.swap(left++, current, SemanticRole::Pivot);
						++current;
					}
					}
					leftEnd = left;
					const std::size_t equalCount = end - equalStart;
					for (std::size_t offset = 0; offset < equalCount; ++offset)
						r.swap(left + offset, end - 1 - offset, SemanticRole::Pivot);
					rightBegin = left + equalCount;
				}
				else
				{
					std::ptrdiff_t left = static_cast<std::ptrdiff_t>(first);
					std::ptrdiff_t right = static_cast<std::ptrdiff_t>(end) - 2;
					std::ptrdiff_t equalLeft = left, equalRight = right;
					const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(end) - 1;
					for (;;)
					{
						while (left <= right)
						{
							const std::size_t index = static_cast<std::size_t>(left);
							const SortItem item = r.at(index);
							r.compare(item, pivot, index, static_cast<std::size_t>(last), "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
							if (order == ComparisonResult::Greater) break;
							if (order == ComparisonResult::Equal)
							{
								r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(equalLeft++), SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
							}
							++left;
						}
						while (left <= right)
						{
							const std::size_t index = static_cast<std::size_t>(right);
							const SortItem item = r.at(index);
							r.compare(item, pivot, index, static_cast<std::size_t>(last), "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
							const ComparisonResult order = compareItems(item, pivot, r.trace.direction);
							if (order == ComparisonResult::Less) break;
							if (order == ComparisonResult::Equal)
							{
								r.swap(static_cast<std::size_t>(right), static_cast<std::size_t>(equalRight--), SemanticRole::Pivot);
								r.addMetric("equal_key_moves");
							}
							--right;
						}
						if (left > right) break;
						r.swap(static_cast<std::size_t>(left++), static_cast<std::size_t>(right--), SemanticRole::Pivot);
					}
					r.swap(static_cast<std::size_t>(left), static_cast<std::size_t>(last), SemanticRole::Pivot);
					const std::ptrdiff_t lessCount = left - equalLeft;
					const std::ptrdiff_t greaterCount = equalRight - right;
					std::ptrdiff_t leftCursor = left - 1, rightCursor = left + 1;
					const std::ptrdiff_t equalPrefixEnd = static_cast<std::ptrdiff_t>(first) +
						std::min(equalLeft - static_cast<std::ptrdiff_t>(first), lessCount);
					for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(first); k < equalPrefixEnd; ++k, --leftCursor)
						r.swap(static_cast<std::size_t>(k), static_cast<std::size_t>(leftCursor), SemanticRole::Pivot);
					const std::ptrdiff_t equalSuffixStart = last - 1 - std::min(last - 1 - equalRight, greaterCount - 1);
					for (std::ptrdiff_t k = last - 1; k > equalSuffixStart; --k, ++rightCursor)
						r.swap(static_cast<std::size_t>(rightCursor), static_cast<std::size_t>(k), SemanticRole::Pivot);
					leftEnd = first + static_cast<std::size_t>(lessCount);
					rightBegin = end - static_cast<std::size_t>(greaterCount);
				}
				r.annotate("main", RegionKind::Contiguous, first, leftEnd, 1, -1, "Ternary quick less-than partition", begin);
				r.annotate("main", RegionKind::Contiguous, leftEnd, rightBegin, 1, -1, "Ternary quick pivot-equal region", begin);
				r.annotate("main", RegionKind::Contiguous, rightBegin, end, 1, -1, "Ternary quick greater-than partition", begin);
				if (leftEnd > first) sortRange(first, leftEnd, depth + 1);
				if (end > rightBegin) sortRange(rightBegin, end, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		void stablePermutationSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const std::string indexLane = "stable_permutation_indices";
			r.declareMetric("index_permutation_swaps");
			r.declareMetric("permutations_examined");
			r.resizeCounters(indexLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeCounter(indexLane, i, static_cast<int>(i));
			const auto isSorted = [&r, count]()
			{
				r.addMetric("permutations_examined");
				for (std::size_t i = 1; i < count; ++i)
				{
					const SortItem previous = r.at(i - 1), current = r.at(i);
					r.compare(previous, current, i - 1, i);
					if (compareItems(previous, current, r.trace.direction) == ComparisonResult::Greater) return false;
				}
				return true;
			};
			std::function<bool(std::size_t)> permute;
			permute = [&r, &permute, &isSorted, &indexLane](std::size_t length)
			{
				if (length < 2) return isSorted();
				for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(length) - 2; i >= 0; --i)
				{
					if (permute(length - 1)) return true;
					const std::size_t first = static_cast<std::size_t>(i);
					const std::size_t second = length - 1;
					const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, first));
					const std::size_t secondIndex = static_cast<std::size_t>(r.counter(indexLane, second));
					r.swap(firstIndex, secondIndex);
					const int firstValue = r.counter(indexLane, first);
					const int secondValue = r.counter(indexLane, second);
					r.writeCounter(indexLane, first, secondValue);
					r.writeCounter(indexLane, second, firstValue);
					r.addMetric("index_permutation_swaps");
				}
				if (permute(length - 1)) return true;
				const int tail = r.counter(indexLane, length - 1);
				for (std::size_t i = length - 1; i > 0; --i)
					r.writeCounter(indexLane, i, r.counter(indexLane, i - 1));
				r.writeCounter(indexLane, 0, tail);
				const std::size_t firstIndex = static_cast<std::size_t>(r.counter(indexLane, 0));
				r.hold(firstIndex);
				for (std::size_t i = 1; i < length; ++i)
				{
					const std::size_t destination = static_cast<std::size_t>(r.counter(indexLane, i - 1));
					const std::size_t source = static_cast<std::size_t>(r.counter(indexLane, i));
					r.shift(source, destination);
				}
				const std::size_t finalIndex = static_cast<std::size_t>(r.counter(indexLane, length - 1));
				r.place(finalIndex, r.state.heldItem);
				r.addMetric("permutations_examined");
				return false;
			};
			permute(count);
			r.resizeCounters(indexLane, 0);
		}

		void tableSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const std::string tableLane = "table_indices";
			const char* metricKeys[] = { "partitions", "maximum_depth", "table_index_swaps", "permutation_cycles", "permutation_cycle_moves" };
			for (std::size_t metric = 0; metric < sizeof(metricKeys) / sizeof(metricKeys[0]); ++metric)
				r.declareMetric(metricKeys[metric]);
			r.resizeCounters(tableLane, count);
			for (std::size_t i = 0; i < count; ++i) r.writeCounter(tableLane, i, static_cast<int>(i));
			const auto stableGreater = [&r, &tableLane](std::size_t first, std::size_t second)
			{
				const std::size_t firstIndex = static_cast<std::size_t>(r.counter(tableLane, first));
				const std::size_t secondIndex = static_cast<std::size_t>(r.counter(tableLane, second));
				const SortItem a = r.at(firstIndex), b = r.at(secondIndex);
				r.compare(a, b, firstIndex, secondIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return order == ComparisonResult::Greater ||
					(order == ComparisonResult::Equal && firstIndex > secondIndex);
			};
			const auto tableSwap = [&r, &tableLane](std::size_t first, std::size_t second)
			{
				if (first == second) return;
				const int firstValue = r.counter(tableLane, first), secondValue = r.counter(tableLane, second);
				r.writeCounter(tableLane, first, secondValue);
				r.writeCounter(tableLane, second, firstValue);
				r.addMetric("table_index_swaps");
			};
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &sortRange, &stableGreater, &tableSwap, &tableLane](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				if (end - first < 3)
				{
					if (stableGreater(first, first + 1)) tableSwap(first, first + 1);
					return;
				}
				r.addMetric("partitions");
				const std::size_t begin = r.trace.events.size();
				const std::size_t middle = first + (end - 1 - first) / 2;
				if (stableGreater(first, middle)) tableSwap(first, middle);
				bool medianIsReady = false;
				if (stableGreater(middle, end - 1))
				{
					tableSwap(middle, end - 1);
					medianIsReady = stableGreater(first, middle);
				}
				if (!medianIsReady) tableSwap(first, middle);
				const std::size_t pivot = first;
				std::size_t left = first + 1, right = end;
				for (;;)
				{
					while (left < right && !stableGreater(left, pivot)) ++left;
					if (right == 0) break;
					--right;
					while (right >= left && stableGreater(right, pivot))
					{
						if (right == 0) break;
						--right;
					}
					if (left < right) tableSwap(left++, right);
					else break;
				}
				tableSwap(first, right);
				const std::size_t split = right;
				r.annotate(tableLane, RegionKind::Contiguous, first, end, 1, -1, "Indirect table partition", begin);
				if (split > first) sortRange(first, split, depth + 1);
				if (split + 1 < end) sortRange(split + 1, end, depth + 1);
			};
			if (count > 1) sortRange(0, count, 1);
			std::set<int> tablePermutation;
			for (std::size_t i = 0; i < count; ++i)
			{
				const int mapped = r.counter(tableLane, i);
				if (mapped < 0 || static_cast<std::size_t>(mapped) >= count || !tablePermutation.insert(mapped).second)
					throw std::logic_error("Table Sort produced a non-permutation index table.");
			}
			for (std::size_t first = 0; first < count; ++first)
			{
				const int mapped = r.counter(tableLane, first);
				if (mapped == static_cast<int>(first)) continue;
				const SortItem held = r.at(first);
				r.hold(first);
				std::size_t current = first;
				std::size_t next = static_cast<std::size_t>(mapped);
				std::vector<std::size_t> cyclePositions(1, first);
				do
				{
					r.shift(next, current);
					current = next;
					cyclePositions.push_back(current);
					require(cyclePositions.size() <= count, "Table Sort encountered a non-closing permutation cycle.");
					next = static_cast<std::size_t>(r.counter(tableLane, current));
				} while (next != first);
				r.place(current, held);
				for (std::size_t move = 0; move < cyclePositions.size(); ++move)
					r.writeCounter(tableLane, cyclePositions[move], static_cast<int>(cyclePositions[move]));
				r.addMetric("permutation_cycles");
				r.addMetric("permutation_cycle_moves", cyclePositions.size() - 1);
			}
			r.resizeCounters(tableLane, 0);
		}

		void heapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			const auto sift = [&r, &later](std::size_t root, std::size_t end)
			{
				while (root * 2 + 1 < end)
				{
					std::size_t child = root * 2 + 1;
					if (child + 1 < end)
					{
						const SortItem a = r.at(child), b = r.at(child + 1);
						r.compare(a, b, child, child + 1);
						if (later(b, a)) ++child;
					}
					const SortItem parent = r.at(root), candidate = r.at(child);
					r.compare(parent, candidate, root, child);
					if (!later(candidate, parent)) return;
					r.swap(root, child);
					root = child;
				}
			};
			for (std::size_t start = n / 2; start > 0; --start) sift(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(0, end - 1);
				sift(0, end - 1);
			}
		}

		void arityHeapSort(Recorder& r, std::size_t arity, bool minHeap, bool flipped)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			arity = std::max<std::size_t>(2, std::min<std::size_t>(arity, 16));
			const auto physical = [n, flipped](std::size_t index) { return flipped ? n - 1 - index : index; };
			const auto better = [&r, minHeap](const SortItem& a, const SortItem& b)
			{
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return minHeap ? order == ComparisonResult::Less : order == ComparisonResult::Greater;
			};
			const auto siftDown = [&r, arity, physical, better](std::size_t root, std::size_t end)
			{
				while (root <= (end - 1) / arity)
				{
					const std::size_t firstChild = root * arity + 1;
					if (firstChild >= end) return;
					std::size_t selected = firstChild;
					for (std::size_t child = firstChild + 1; child < end && child < firstChild + arity; ++child)
					{
						const SortItem a = r.at(physical(child)), b = r.at(physical(selected));
						r.compare(a, b, physical(child), physical(selected));
						if (better(a, b)) selected = child;
					}
					const SortItem parent = r.at(physical(root)), candidate = r.at(physical(selected));
					r.compare(parent, candidate, physical(root), physical(selected));
					if (!better(candidate, parent)) return;
					r.swap(physical(root), physical(selected));
					root = selected;
				}
			};
			for (std::size_t start = (n - 2) / arity + 1; start > 0; --start) siftDown(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(physical(0), physical(end - 1));
				siftDown(0, end - 1);
			}
			if (minHeap && !flipped)
				for (std::size_t left = 0, right = n - 1; left < right; ++left, --right) r.swap(left, right);
		}

		void bottomUpSiftDown(Recorder& r, std::size_t root, std::size_t end)
		{
			if (root >= end || root * 2 + 1 >= end) return;
			std::vector<std::size_t> path(1, root);
			std::size_t node = root;
			while (node * 2 + 1 < end)
			{
				std::size_t child = node * 2 + 1;
				if (child + 1 < end)
				{
					const SortItem a = r.at(child), b = r.at(child + 1);
					r.compare(a, b, child, child + 1);
					if (compareItems(b, a, r.trace.direction) == ComparisonResult::Greater) ++child;
				}
				node = child;
				path.push_back(node);
			}
			const SortItem held = r.at(root);
			r.hold(root);
			for (std::size_t depth = 1; depth < path.size(); ++depth) r.shift(path[depth], path[depth - 1]);
			std::size_t position = path.back();
			while (position != root)
			{
				const std::size_t parent = (position - 1) / 2;
				const SortItem candidate = r.at(parent);
				r.compare(held, candidate, root, parent, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				if (compareItems(held, candidate, r.trace.direction) != ComparisonResult::Greater) break;
				r.shift(parent, position);
				position = parent;
			}
			r.place(position, held);
		}

		void bottomUpHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			const auto siftBuild = [&r, &later](std::size_t root, std::size_t end)
			{
				while (root * 2 + 1 < end)
				{
					std::size_t child = root * 2 + 1;
					if (child + 1 < end)
					{
						const SortItem a = r.at(child), b = r.at(child + 1);
						r.compare(a, b, child, child + 1);
						if (later(b, a)) ++child;
					}
					const SortItem parent = r.at(root), candidate = r.at(child);
					r.compare(parent, candidate, root, child);
					if (!later(candidate, parent)) return;
					r.swap(root, child);
					root = child;
				}
			};
			for (std::size_t start = n / 2; start > 0; --start) siftBuild(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(0, end - 1);
				bottomUpSiftDown(r, 0, end - 1);
			}
		}

		void binomialHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			for (std::size_t index = 2; index <= n; index += 2)
			{
				std::size_t maxNode = index;
				do
				{
					const std::size_t focus = maxNode;
					for (std::size_t depth = 1; (focus & depth) == 0 && depth <= focus; depth *= 2)
					{
						const std::size_t left = focus - depth - 1;
						const std::size_t right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus - depth;
					}
					if (focus != maxNode) r.swap(focus - 1, maxNode - 1);
					if (focus == maxNode) break;
				} while (true);
			}
			for (std::size_t index = n; index > 2; --index)
			{
				std::size_t maxNode = index;
				std::size_t focus = index;
				for (std::size_t depth = 1; focus != 0 && depth <= index; depth *= 2)
				{
					if ((focus & depth) != 0)
					{
						const std::size_t left = focus - 1, right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus;
						focus -= depth;
					}
				}
				if (maxNode == index) continue;
				focus = index;
				do
				{
					r.swap(focus - 1, maxNode - 1);
					focus = maxNode;
					for (std::size_t depth = 1; (focus & depth) == 0 && depth <= focus; depth *= 2)
					{
						const std::size_t left = focus - depth - 1, right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus - depth;
					}
				} while (focus != maxNode);
			}
		}

		void lazyHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const std::size_t block = static_cast<std::size_t>(std::sqrt(static_cast<double>(n - 1))) + 1;
			const auto maxToFront = [&r](std::size_t first, std::size_t last)
			{
				if (first >= last) return;
				std::size_t maximum = first;
				for (std::size_t i = first + 1; i < last; ++i)
				{
					const SortItem a = r.at(i), b = r.at(maximum);
					r.compare(a, b, i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) maximum = i;
				}
				r.swap(maximum, first, SemanticRole::CurrentMaximum);
			};
		for (std::size_t first = 0; first < n; first += block) maxToFront(first, std::min(first + block, n));
		std::size_t remaining = n;
		while (remaining > 0)
		{
			std::size_t maximum = 0;
			for (std::size_t i = block; i < remaining; i += block)
			{
				const SortItem a = r.at(i), b = r.at(maximum);
				r.compare(a, b, i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(a, b, r.trace.direction) != ComparisonResult::Less) maximum = i;
			}
			--remaining;
			r.swap(maximum, remaining, SemanticRole::CurrentMaximum);
			maxToFront(maximum, std::min(maximum + block, remaining));
		}
		}

		void minMaxHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const auto isMinLevel = [](std::size_t index)
			{
				std::size_t value = index + 1, bits = 0;
				while (value) { ++bits; value >>= 1; }
				return (bits & 1u) != 0;
			};
			const auto precedes = [&r](const SortItem& a, const SortItem& b, bool minimum)
			{
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return minimum ? order == ComparisonResult::Less : order == ComparisonResult::Greater;
			};
			std::size_t end = n;
			const auto downheap = [&r, &isMinLevel, &precedes, &end](std::size_t start)
			{
				std::size_t node = start;
				const bool minimum = isMinLevel(node);
				while (node * 2 + 1 < end)
				{
					const std::size_t left = node * 2 + 1, right = left + 1;
					std::size_t candidate = left;
					const std::size_t descendants[5] = { right, left * 2 + 1, left * 2 + 2, right * 2 + 1, right * 2 + 2 };
					for (std::size_t d = 0; d < 5; ++d)
					{
						const std::size_t child = descendants[d];
						if (child >= end) break;
						const SortItem a = r.at(child), b = r.at(candidate);
						r.compare(a, b, child, candidate, "main", "main",
							minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum,
							minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum);
						if (precedes(a, b, minimum)) candidate = child;
					}
					if (!precedes(r.at(candidate), r.at(node), minimum)) return;
					r.swap(candidate, node, minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum);
					if (candidate <= right) return;
					const std::size_t parent = (candidate - 1) / 2;
					if (precedes(r.at(parent), r.at(candidate), minimum)) r.swap(candidate, parent);
					node = candidate;
				}
			};
		for (std::size_t i = (n - 1) / 2 + 1; i > 0; --i) downheap(i - 1);
		for (std::size_t remaining = n; remaining > 1; --remaining)
		{
			std::size_t maximum = 1;
			if (remaining > 2)
			{
				const SortItem a = r.at(1), b = r.at(2);
				r.compare(a, b, 1, 2, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::CurrentMaximum);
				if (precedes(b, a, false)) maximum = 2;
			}
			end = remaining - 1;
			r.swap(maximum, end, SemanticRole::CurrentMaximum);
			if (maximum < end) downheap(maximum);
		}
	}

	void outOfPlaceHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		if (n < 2) return;
		const std::string lane = "out_of_place_heap";
		r.resizeAuxiliary(lane, n);
		for (std::size_t i = 0; i < n; ++i) r.writeAuxiliary(lane, i, r.at(i));
		const auto later = [&r](const SortItem& a, const SortItem& b)
			{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
		const auto sift = [&r, &lane, &later](std::size_t root, std::size_t end)
		{
			while (root * 2 + 1 < end)
			{
				std::size_t child = root * 2 + 1;
				if (child + 1 < end)
				{
					const SortItem a = r.auxiliaryAt(lane, child), b = r.auxiliaryAt(lane, child + 1);
					r.compare(a, b, child, child + 1, lane, lane);
					if (later(b, a)) ++child;
				}
				const SortItem parent = r.auxiliaryAt(lane, root), candidate = r.auxiliaryAt(lane, child);
				r.compare(parent, candidate, root, child, lane, lane);
				if (!later(candidate, parent)) return;
				r.swapAuxiliary(lane, root, child);
				root = child;
			}
		};
		for (std::size_t start = n / 2; start > 0; --start) sift(start - 1, n);
		for (std::size_t end = n; end > 1; --end)
		{
			r.swapAuxiliary(lane, 0, end - 1);
			sift(0, end - 1);
		}
		std::vector<SortItem> output;
		output.reserve(n);
		for (std::size_t i = 0; i < n; ++i) output.push_back(r.auxiliaryAt(lane, i));
		r.writeMainRange(0, output);
		r.resizeAuxiliary(lane, 0);
	}

	void triangularHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		const auto triangularRoot = [](std::size_t value)
		{
			return static_cast<std::size_t>((std::sqrt(static_cast<double>(8 * value + 1)) - 1.0) / 2.0);
		};
		const auto siftDown = [&r, &triangularRoot](std::size_t end, std::size_t start)
		{
			if (start >= end) return;
			const SortItem held = r.at(start);
			r.hold(start);
			std::size_t root = start;
			std::size_t level = triangularRoot(root);
			std::size_t left = root + level + 1, right = left + 1;
			while (left < end)
			{
				std::size_t maximum = left;
				if (right < end)
				{
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::CurrentMaximum);
					if (compareItems(b, a, r.trace.direction) == ComparisonResult::Greater) maximum = right;
				}
				const SortItem candidate = r.at(maximum);
				r.compare(candidate, held, maximum, start, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::HeldKey);
				if (compareItems(candidate, held, r.trace.direction) != ComparisonResult::Greater) break;
				r.shift(maximum, root);
				root = maximum;
				level = triangularRoot(root);
				left = root + level + 1;
				right = left + 1;
			}
			r.place(root, held);
		};
		for (std::size_t i = n; i > 0; --i) siftDown(n, i - 1);
		for (std::size_t pass = 1; pass + 1 < n; ++pass)
		{
			const std::size_t end = n - pass;
			r.swap(0, end, SemanticRole::CurrentMaximum);
			siftDown(end, 0);
		}
		if (n > 1)
		{
			const SortItem a = r.at(0), b = r.at(1);
			r.compare(a, b, 0, 1, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
			if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(0, 1);
		}
	}

	void weakHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		if (n < 2) return;
		const std::string lane = "weak_heap_bits";
		r.resizeCounters(lane, (n + 7) / 8);
		const auto flag = [&r, &lane](std::size_t index)
			{ return (r.counter(lane, index >> 3) >> (index & 7)) & 1; };
		const auto toggle = [&r, &lane, &flag](std::size_t index)
		{
			const std::size_t byte = index >> 3;
			r.writeCounter(lane, byte, r.counter(lane, byte) ^ (1 << (index & 7)));
		};
		const auto merge = [&r, &toggle](std::size_t parent, std::size_t child)
		{
			const SortItem a = r.at(parent), b = r.at(child);
			r.compare(a, b, parent, child, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
			if (compareItems(a, b, r.trace.direction) == ComparisonResult::Less)
			{
				toggle(child);
				r.swap(parent, child, SemanticRole::CurrentMaximum);
			}
		};
		for (std::size_t i = n; i > 1; --i)
		{
			std::size_t node = i - 1;
			while ((node & 1u) == static_cast<std::size_t>(flag(node >> 1))) node >>= 1;
			merge(node >> 1, i - 1);
		}
		for (std::size_t end = n; end > 2; --end)
		{
			const std::size_t last = end - 1;
			r.swap(0, last, SemanticRole::CurrentMaximum);
			std::size_t node = 1, child = 2 * node + static_cast<std::size_t>(flag(node));
			while (child < last)
			{
				node = child;
				child = 2 * node + static_cast<std::size_t>(flag(node));
			}
			while (node > 0)
			{
				merge(0, node);
				node >>= 1;
			}
		}
		r.swap(0, 1, SemanticRole::CurrentMaximum);
	}

	void poplarHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		const auto later = [&r](const SortItem& a, const SortItem& b)
			{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
		const auto insertionSort = [&r](std::size_t first, std::size_t last)
		{
			for (std::size_t current = first + 1; current < last; ++current)
			{
				const SortItem held = r.at(current);
				const SortItem previous = r.at(current - 1);
				r.compare(held, previous, current, current - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				if (compareItems(held, previous, r.trace.direction) != ComparisonResult::Less) continue;
				r.hold(current);
				std::size_t position = current;
				do
				{
					r.shift(position - 1, position);
					--position;
					if (position == first) break;
					const SortItem candidate = r.at(position - 1);
					r.compare(held, candidate, current, position - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(held, candidate, r.trace.direction) != ComparisonResult::Less) break;
				} while (true);
				r.place(position, held);
			}
		};
		const auto sift = [&r, &later](std::size_t first, std::size_t size)
		{
			if (size < 2) return;
			std::size_t root = first + size - 1;
			std::size_t childRoot1 = root - 1;
			std::size_t childRoot2 = first + (size / 2 - 1);
			while (true)
			{
				std::size_t maximum = root;
				SortItem a = r.at(maximum), b = r.at(childRoot1);
				r.compare(a, b, maximum, childRoot1, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) maximum = childRoot1;
				a = r.at(maximum); b = r.at(childRoot2);
				r.compare(a, b, maximum, childRoot2, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) maximum = childRoot2;
				if (maximum == root) return;
				r.swap(root, maximum, SemanticRole::CurrentMaximum);
				size /= 2;
				if (size < 2) return;
				root = maximum;
				childRoot1 = root - 1;
				childRoot2 = maximum - (size - size / 2);
			}
		};
		const auto hyperfloor = [](std::size_t value)
		{
			std::size_t power = 1;
			while (power <= value / 2) power *= 2;
			return power;
		};
		const auto popHeapWithSize = [&r, &later, &sift, &hyperfloor](std::size_t first, std::size_t last, std::size_t size)
		{
			std::size_t poplarSize = hyperfloor(size + 1) - 1;
			const std::size_t lastRoot = last - 1;
			std::size_t bigger = lastRoot;
			std::size_t biggerSize = poplarSize;
			std::size_t position = first;
			std::size_t remaining = size;
			while (true)
			{
				const std::size_t root = position + poplarSize - 1;
				if (root == lastRoot) break;
				const SortItem a = r.at(bigger), b = r.at(root);
				r.compare(a, b, bigger, root, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) { bigger = root; biggerSize = poplarSize; }
				position = root + 1;
				remaining -= poplarSize;
				poplarSize = hyperfloor(remaining + 1) - 1;
			}
			if (bigger != lastRoot)
			{
				r.swap(bigger, lastRoot, SemanticRole::CurrentMaximum);
				sift(bigger - (biggerSize - 1), biggerSize);
			}
		};
		if (n <= 15)
		{
			insertionSort(0, n);
			return;
		}
		const std::size_t smallPoplarSize = 15;
		std::size_t level = 1, first = 0, next = smallPoplarSize;
		while (true)
		{
			insertionSort(first, next);
			std::size_t poplarSize = smallPoplarSize;
			for (std::size_t carry = ((level & (~level + 1)) >> 1); carry != 0; carry >>= 1)
			{
				first -= poplarSize;
				poplarSize = 2 * poplarSize + 1;
				sift(first, poplarSize);
				++next;
			}
			if (n - next <= smallPoplarSize)
			{
				insertionSort(next, n);
				break;
			}
			first = next;
			next += smallPoplarSize;
			++level;
		}
		std::size_t size = n;
		while (size > 1)
		{
			popHeapWithSize(0, size, size);
			--size;
		}
	}

		void counting(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n == 0) return;
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey);
				maximum = std::max(maximum, r.at(i).integerKey);
			}
			const std::size_t range = static_cast<std::size_t>(static_cast<std::int64_t>(maximum) - minimum + 1);
			r.resizeCounters("counts", range);
			r.resizeAuxiliary("count_output", n);
			std::vector<std::size_t> bucketBegin(range, NoGapIndex);
			for (std::size_t i = 0; i < n; ++i)
			{
				const int key = r.at(i).integerKey;
				const std::size_t bucket = static_cast<std::size_t>(r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(key) - minimum : static_cast<std::int64_t>(maximum) - key);
				if (bucketBegin[bucket] == NoGapIndex) bucketBegin[bucket] = r.trace.events.size();
				r.writeCounter("counts", bucket, r.counter("counts", bucket) + 1);
			}
			for (std::size_t bucket = 0; bucket < range; ++bucket)
				if (bucketBegin[bucket] != NoGapIndex)
					r.annotate("counts", RegionKind::Bucket, bucket, bucket + 1, 1, static_cast<int>(bucket), "Key bucket", bucketBegin[bucket]);
			for (std::size_t i = 1; i < range; ++i)
				r.writeCounter("counts", i, r.counter("counts", i) + r.counter("counts", i - 1));
			for (std::size_t i = n; i > 0; --i)
			{
				const SortItem item = r.at(i - 1);
				const std::size_t bucket = static_cast<std::size_t>(r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(item.integerKey) - minimum : static_cast<std::int64_t>(maximum) - item.integerKey);
				const int target = r.counter("counts", bucket) - 1;
				r.writeAuxiliary("count_output", static_cast<std::size_t>(target), item);
				r.writeCounter("counts", bucket, target);
			}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("count_output", i));
		}

		void radix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n == 0) return;
			const int defaultBase = r.trace.algorithm == SortAlgorithm::ArrayvLsdRadix ? 4 : 10;
			const std::size_t base = static_cast<std::size_t>(r.trace.radixBase >= 2 ? std::min(r.trace.radixBase, 256) : defaultBase);
			r.resizeCounters("digits", base);
			r.resizeAuxiliary("radix_output", n);
			r.probe(0, SemanticRole::ProcessedItem);
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 1; i < n; ++i)
			{
				r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey);
				maximum = std::max(maximum, r.at(i).integerKey);
			}
			const std::int64_t transformedMaximum = static_cast<std::int64_t>(maximum) - minimum;
			std::int64_t place = 1;
			do
			{
				std::vector<std::size_t> bucketBegin(base, NoGapIndex);
				for (std::size_t d = 0; d < base; ++d) r.writeCounter("digits", d, 0);
				const auto transformed = [&r, minimum, maximum](const SortItem& item)
					{ return r.trace.direction == SortDirection::Ascending ? static_cast<std::int64_t>(item.integerKey) - minimum : static_cast<std::int64_t>(maximum) - item.integerKey; };
				for (std::size_t i = 0; i < n; ++i)
				{
					r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = static_cast<std::size_t>((transformed(r.at(i)) / place) % static_cast<std::int64_t>(base));
					if (bucketBegin[digit] == NoGapIndex) bucketBegin[digit] = r.trace.events.size();
					r.writeCounter("digits", digit, r.counter("digits", digit) + 1);
				}
				for (std::size_t d = 1; d < base; ++d)
					r.writeCounter("digits", d, r.counter("digits", d) + r.counter("digits", d - 1));
				for (std::size_t i = n; i > 0; --i)
				{
					r.probe(i - 1, SemanticRole::ProcessedItem);
					const SortItem item = r.at(i - 1);
					const std::size_t digit = static_cast<std::size_t>((transformed(item) / place) % static_cast<std::int64_t>(base));
					const int target = r.counter("digits", digit) - 1;
					r.writeAuxiliary("radix_output", static_cast<std::size_t>(target), item);
					r.writeCounter("digits", digit, target);
				}
				for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("radix_output", i));
				r.addMetric("digit_passes");
				for (std::size_t d = 0; d < base; ++d)
					if (bucketBegin[d] != NoGapIndex)
						r.annotate("digits", RegionKind::Bucket, d, d + 1, 1, static_cast<int>(d),
							"Digit " + std::to_string(d), bucketBegin[d]);
				if (transformedMaximum / place < static_cast<std::int64_t>(base)) break;
				place *= static_cast<std::int64_t>(base);
			} while (place > 0);
		}

		std::int64_t orderedIntegerKey(const SortItem& item, SortDirection direction)
		{
			return direction == SortDirection::Ascending ? static_cast<std::int64_t>(item.integerKey) : -static_cast<std::int64_t>(item.integerKey);
		}

		std::uint32_t binaryKey(const SortItem& item, SortDirection direction)
		{
			std::uint32_t key = static_cast<std::uint32_t>(item.integerKey) ^ 0x80000000u;
			return direction == SortDirection::Ascending ? key : ~key;
		}

		std::size_t boundedRadix(const TraceBuildRequest& request, std::size_t fallback)
		{
			return static_cast<std::size_t>(request.bucketCount >= 2 ? std::min(request.bucketCount, 256) : fallback);
		}

		std::size_t maxKeyBits(const Recorder& r)
		{
			std::uint32_t maximum = 0;
			for (std::size_t i = 0; i < r.state.slots.size(); ++i)
				maximum = std::max(maximum, binaryKey(r.at(i), r.trace.direction));
			std::size_t bits = 0;
			while (maximum) { ++bits; maximum >>= 1; }
			return bits;
		}

		void americanFlag(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const std::size_t base = static_cast<std::size_t>(r.trace.bucketCount >= 2 ? std::min(r.trace.bucketCount, 256) : 128);
			std::size_t bitsPerDigit = 0;
			while ((static_cast<std::size_t>(1) << bitsPerDigit) < base) ++bitsPerDigit;
			const std::size_t actualBase = static_cast<std::size_t>(1) << bitsPerDigit;
			const std::uint32_t mask = static_cast<std::uint32_t>(actualBase - 1);
			const std::size_t keyBits = maxKeyBits(r);
			if (keyBits == 0) { for (std::size_t i = 0; i < n; ++i) r.probe(i, SemanticRole::ProcessedItem); return; }
			std::size_t topShift = ((keyBits - 1) / bitsPerDigit) * bitsPerDigit;
			struct Range { std::size_t first, last, shift; };
			std::vector<Range> pending(1, Range{ 0, n, topShift });
			r.resizeCounters("american_flag_counts", actualBase);
			r.resizeCounters("american_flag_next", actualBase);
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				std::vector<std::size_t> counts(actualBase, 0), starts(actualBase, range.first), ends(actualBase, range.first);
				const std::size_t begin = r.trace.events.size();
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					r.chargeWork();
					r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = (binaryKey(r.at(i), r.trace.direction) >> range.shift) & mask;
					++counts[digit];
				}
				std::size_t cursor = range.first;
				for (std::size_t digit = 0; digit < actualBase; ++digit)
				{
					starts[digit] = cursor;
					cursor += counts[digit];
					ends[digit] = cursor;
					r.writeCounter("american_flag_counts", digit, static_cast<int>(counts[digit]));
					r.writeCounter("american_flag_next", digit, static_cast<int>(starts[digit]));
				}
				for (std::size_t digit = 0; digit < actualBase; ++digit)
				{
					while (static_cast<std::size_t>(r.counter("american_flag_next", digit)) < ends[digit])
					{
						r.chargeWork();
						const std::size_t at = static_cast<std::size_t>(r.counter("american_flag_next", digit));
						const std::size_t destination = (binaryKey(r.at(at), r.trace.direction) >> range.shift) & mask;
						if (destination == digit)
							r.writeCounter("american_flag_next", digit, static_cast<int>(at + 1));
						else
					{
						const std::size_t target = static_cast<std::size_t>(r.counter("american_flag_next", destination));
						require(target < ends[destination], "American Flag bucket cursor exceeded its destination range.");
						r.swap(at, target, SemanticRole::ProcessedItem);
						r.writeCounter("american_flag_next", destination, static_cast<int>(target + 1));
					}
					}
					if (counts[digit] != 0)
						r.annotate("american_flag_counts", RegionKind::Bucket, digit, digit + 1, 1,
							static_cast<int>(digit), "Digit bucket " + std::to_string(digit), begin);
					if (range.shift > 0 && ends[digit] - starts[digit] > 1)
						pending.push_back(Range{ starts[digit], ends[digit], range.shift - bitsPerDigit });
				}
			}
		}

		std::size_t partitionBinary(Recorder& r, std::size_t first, std::size_t last, int bit)
		{
			if (bit < 0 || last - first < 2) return first;
			std::size_t left = first, right = last;
			while (left < right)
			{
				while (left < right && ((binaryKey(r.at(left), r.trace.direction) >> bit) & 1u) == 0)
				{
					r.chargeWork(); r.probe(left, SemanticRole::ProcessedItem); ++left;
				}
				while (left < right && ((binaryKey(r.at(right - 1), r.trace.direction) >> bit) & 1u) != 0)
				{
					r.chargeWork(); r.probe(right - 1, SemanticRole::ProcessedItem); --right;
				}
				if (left < right)
				{
					r.probe(left, SemanticRole::ProcessedItem);
					r.probe(right - 1, SemanticRole::ProcessedItem);
					r.swap(left, right - 1, SemanticRole::ProcessedItem);
					++left; --right;
				}
			}
			return left;
		}

		void binaryQuick(Recorder& r, int mode)
		{
			if (r.state.slots.size() < 2) return;
			const int highestBit = static_cast<int>(maxKeyBits(r)) - 1;
			if (mode == 1)
			{
				std::function<void(std::size_t, std::size_t, int)> sortRange;
				sortRange = [&r, &sortRange](std::size_t first, std::size_t last, int bit)
				{
					if (bit < 0 || last - first < 2) return;
					std::size_t left = first, right = last;
					while (left < right)
					{
						while (left < right && ((binaryKey(r.at(left), r.trace.direction) >> bit) & 1u) == 0)
						{ r.chargeWork(); r.probe(left, SemanticRole::ProcessedItem); ++left; }
						while (left < right && ((binaryKey(r.at(right - 1), r.trace.direction) >> bit) & 1u) != 0)
						{ r.chargeWork(); r.probe(right - 1, SemanticRole::ProcessedItem); --right; }
						if (left < right)
						{
							r.probe(left, SemanticRole::ProcessedItem); r.probe(right - 1, SemanticRole::ProcessedItem);
							r.swap(left, right - 1, SemanticRole::ProcessedItem); ++left; --right;
						}
					}
					sortRange(first, left, bit - 1);
					sortRange(left, last, bit - 1);
				};
				sortRange(0, r.state.slots.size(), highestBit);
				return;
			}
			struct BinaryRange { std::size_t first, last; int bit; };
			std::vector<BinaryRange> pending(1, BinaryRange{ 0, r.state.slots.size(), highestBit });
			std::size_t queueCursor = 0;
			while (mode == 0 ? queueCursor < pending.size() : !pending.empty())
			{
				const BinaryRange range = mode == 0 ? pending[queueCursor++] : pending.back();
				if (mode != 0) pending.pop_back();
				if (range.bit < 0 || range.last - range.first < 2) continue;
				const std::size_t middle = partitionBinary(r, range.first, range.last, range.bit);
				if (middle - range.first > 1) pending.push_back(BinaryRange{ range.first, middle, range.bit - 1 });
				if (range.last - middle > 1) pending.push_back(BinaryRange{ middle, range.last, range.bit - 1 });
			}
		}

		void stacklessBinaryRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const int highestBit = static_cast<int>(maxKeyBits(r)) - 1;
			for (int bit = highestBit; bit >= 0; --bit)
			{
				std::size_t first = 0;
				while (first < n)
				{
					r.chargeWork();
					const std::uint32_t prefix = bit == 31 ? 0u : (binaryKey(r.at(first), r.trace.direction) >> (bit + 1));
					std::size_t last = first + 1;
					while (last < n)
					{
						r.chargeWork();
						const std::uint32_t candidate = bit == 31 ? 0u : (binaryKey(r.at(last), r.trace.direction) >> (bit + 1));
						if (candidate != prefix) break;
						++last;
					}
					partitionBinary(r, first, last, bit);
					first = last;
				}
				r.addMetric("bit_passes");
			}
		}

		void stacklessAmericanFlag(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const std::size_t requested = static_cast<std::size_t>(r.trace.bucketCount >= 2 ? std::min(r.trace.bucketCount, 256) : 16);
			std::size_t bits = 0;
			while ((static_cast<std::size_t>(1) << bits) < requested) ++bits;
			const std::size_t base = static_cast<std::size_t>(1) << bits;
			const std::uint32_t mask = static_cast<std::uint32_t>(base - 1);
			const std::size_t keyBits = maxKeyBits(r);
			if (keyBits == 0) return;
			const int topShift = static_cast<int>(((keyBits - 1) / bits) * bits);
			r.resizeCounters("stackless_flag_count", base);
			r.resizeCounters("stackless_flag_next", base);
			for (int shift = topShift; shift >= 0; shift -= static_cast<int>(bits))
			{
				std::size_t first = 0;
				while (first < n)
				{
					const std::uint32_t prefix = shift + static_cast<int>(bits) >= 32 ? 0u :
						(binaryKey(r.at(first), r.trace.direction) >> (shift + static_cast<int>(bits)));
					std::size_t last = first + 1;
					while (last < n)
					{
						r.chargeWork();
						const std::uint32_t candidate = shift + static_cast<int>(bits) >= 32 ? 0u :
							(binaryKey(r.at(last), r.trace.direction) >> (shift + static_cast<int>(bits)));
						if (candidate != prefix) break;
						++last;
					}
					std::vector<std::size_t> counts(base, 0), starts(base, first), ends(base, first);
					for (std::size_t i = first; i < last; ++i)
					{
						r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
						++counts[(binaryKey(r.at(i), r.trace.direction) >> shift) & mask];
					}
					std::size_t cursor = first;
					for (std::size_t digit = 0; digit < base; ++digit)
					{
						starts[digit] = cursor; cursor += counts[digit]; ends[digit] = cursor;
						r.writeCounter("stackless_flag_count", digit, static_cast<int>(counts[digit]));
						r.writeCounter("stackless_flag_next", digit, static_cast<int>(starts[digit]));
					}
					const std::size_t phase = r.trace.events.size();
					for (std::size_t digit = 0; digit < base; ++digit)
						while (static_cast<std::size_t>(r.counter("stackless_flag_next", digit)) < ends[digit])
						{
							r.chargeWork();
							const std::size_t at = static_cast<std::size_t>(r.counter("stackless_flag_next", digit));
							const std::size_t targetDigit = (binaryKey(r.at(at), r.trace.direction) >> shift) & mask;
							if (targetDigit == digit) r.writeCounter("stackless_flag_next", digit, static_cast<int>(at + 1));
							else
							{
								const std::size_t target = static_cast<std::size_t>(r.counter("stackless_flag_next", targetDigit));
								require(target < ends[targetDigit], "Stackless American Flag cursor exceeded its bucket.");
								r.swap(at, target, SemanticRole::ProcessedItem);
								r.writeCounter("stackless_flag_next", targetDigit, static_cast<int>(target + 1));
							}
						}
					for (std::size_t digit = 0; digit < base; ++digit)
						if (counts[digit] > 0) r.annotate("stackless_flag_count", RegionKind::Bucket, digit, digit + 1, 1,
							static_cast<int>(digit), "Digit bucket " + std::to_string(digit), phase);
					first = last;
				}
				r.addMetric("digit_passes");
			}
		}

		void flash(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const std::size_t classes = std::max<std::size_t>(2, std::min<std::size_t>(n, static_cast<std::size_t>(0.2 * n) + 2));
			// Direction is folded into a monotone scalar before assigning flash classes.
			const auto classFor = [&r, minimum, maximum, classes](const SortItem& item)
			{
				const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
				const std::uint64_t offset = static_cast<std::uint64_t>(orderedIntegerKey(item, r.trace.direction) - minimum);
				return static_cast<std::size_t>((offset * (classes - 1)) / span);
			};
			std::vector<std::size_t> counts(classes, 0), starts(classes, 0), ends(classes, 0);
			const std::size_t begin = r.trace.events.size();
			r.resizeCounters("flash_classes", classes);
			r.resizeCounters("flash_next", classes);
			for (std::size_t i = 0; i < n; ++i) { r.probe(i, SemanticRole::ProcessedItem); ++counts[classFor(r.at(i))]; }
			std::size_t cursor = 0;
			for (std::size_t c = 0; c < classes; ++c)
			{
				starts[c] = cursor; cursor += counts[c]; ends[c] = cursor;
				r.writeCounter("flash_classes", c, static_cast<int>(counts[c]));
				r.writeCounter("flash_next", c, static_cast<int>(starts[c]));
			}
			for (std::size_t c = 0; c < classes; ++c)
			{
				while (static_cast<std::size_t>(r.counter("flash_next", c)) < ends[c])
				{
					r.chargeWork();
					const std::size_t at = static_cast<std::size_t>(r.counter("flash_next", c));
					const std::size_t targetClass = classFor(r.at(at));
					if (targetClass == c) r.writeCounter("flash_next", c, static_cast<int>(at + 1));
					else
					{
						const std::size_t target = static_cast<std::size_t>(r.counter("flash_next", targetClass));
						require(target < ends[targetClass], "Flash class cursor exceeded its target range.");
						r.swap(at, target, SemanticRole::ProcessedItem);
						r.writeCounter("flash_next", targetClass, static_cast<int>(target + 1));
					}
				}
				if (counts[c] > 0) r.annotate("flash_classes", RegionKind::Bucket, c, c + 1, 1, static_cast<int>(c),
					"Flash class " + std::to_string(c), begin);
			}
			for (std::size_t c = 0; c < classes; ++c)
			{
				for (std::size_t i = starts[c] + 1; i < ends[c]; ++i)
				{
					const SortItem key = r.at(i); r.hold(i); std::size_t position = i;
					while (position > starts[c])
					{
						const SortItem previous = r.at(position - 1);
						r.compare(previous, key, position - 1, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
						r.shift(position - 1, position); --position;
					}
					r.place(position, key);
				}
			}
		}

		void pigeonhole(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey); maximum = std::max(maximum, r.at(i).integerKey);
			}
			(void)minimum; (void)maximum;
			r.resizeAuxiliary("pigeonhole_output", n);
			std::int64_t low = orderedIntegerKey(r.at(0), r.trace.direction), high = low;
			for (std::size_t i = 1; i < n; ++i) { low = std::min(low, orderedIntegerKey(r.at(i), r.trace.direction)); high = std::max(high, orderedIntegerKey(r.at(i), r.trace.direction)); }
			const std::size_t orderedRange = static_cast<std::size_t>(high - low + 1);
			r.resizeCounters("pigeonholes", orderedRange); r.resizeCounters("pigeonhole_next", orderedRange);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t bucket = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - low);
				r.writeCounter("pigeonholes", bucket, r.counter("pigeonholes", bucket) + 1);
			}
			std::size_t position = 0;
			for (std::size_t bucket = 0; bucket < orderedRange; ++bucket)
			{
				r.writeCounter("pigeonhole_next", bucket, static_cast<int>(position));
				position += static_cast<std::size_t>(r.counter("pigeonholes", bucket));
			}
			for (std::size_t i = 0; i < n; ++i)
			{
				const SortItem item = r.at(i);
				const std::size_t bucket = static_cast<std::size_t>(orderedIntegerKey(item, r.trace.direction) - low);
				const std::size_t target = static_cast<std::size_t>(r.counter("pigeonhole_next", bucket));
				r.writeAuxiliary("pigeonhole_output", target, item);
				r.writeCounter("pigeonhole_next", bucket, static_cast<int>(target + 1));
			}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("pigeonhole_output", i));
		}

		void indexSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t i = 0; i < n; ++i)
			{
				std::size_t guard = 0;
				while (true)
				{
					const std::size_t destination = r.trace.direction == SortDirection::Ascending
						? static_cast<std::size_t>(r.at(i).integerKey) : n - 1 - static_cast<std::size_t>(r.at(i).integerKey);
					if (destination == i) break;
					r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
					if (destination >= n) break;
					r.swap(i, destination, SemanticRole::ProcessedItem);
					if (++guard > n) throw std::logic_error("Index Sort requires a unique permutation of [0,N-1].");
				}
			}
		}

		void stableBucketPartition(Recorder& r, std::size_t first, std::size_t last,
			std::int64_t minimum, std::int64_t maximum, std::size_t base, std::int64_t place, const std::string& lane)
		{
			if (last - first < 2 || minimum == maximum) return;
			std::vector<std::size_t> counts(base, 0);
			const std::size_t begin = r.trace.events.size();
			const auto digit = [&r, minimum, maximum, base, place](const SortItem& item)
			{
				const std::int64_t key = orderedIntegerKey(item, r.trace.direction) - minimum;
				const std::int64_t divisor = std::max<std::int64_t>(1, place);
				return static_cast<std::size_t>((key / divisor) % static_cast<std::int64_t>(base));
			};
			r.resizeCounters(lane, base);
			for (std::size_t i = first; i < last; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				++counts[digit(r.at(i))];
			}
			for (std::size_t b = 0; b < base; ++b) r.writeCounter(lane, b, static_cast<int>(counts[b]));
			std::size_t destination = first;
			for (std::size_t b = 0; b < base; ++b)
			{
				const std::size_t bucketStart = destination;
				std::size_t scan = destination;
				while (scan < last)
				{
					r.chargeWork(); r.probe(scan, SemanticRole::ProcessedItem);
					if (digit(r.at(scan)) == b)
					{
						if (scan > destination)
						{
							const SortItem held = r.at(scan); r.hold(scan);
							for (std::size_t p = scan; p > destination; --p) r.shift(p - 1, p);
							r.place(destination, held);
						}
						++destination; scan = destination;
					}
					else ++scan;
				}
				if (destination > bucketStart)
					r.annotate(lane, RegionKind::Bucket, b, b + 1, 1, static_cast<int>(b),
						"Bucket " + std::to_string(b), begin);
			}
		}

		void inPlaceLsdRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t base = static_cast<std::size_t>(r.trace.radixBase >= 2 ? std::min(r.trace.radixBase, 16) : 4);
			std::int64_t place = 1;
			do
			{
				stableBucketPartition(r, 0, n, minimum, maximum, base, place, "inplace_lsd_buckets");
				r.addMetric("digit_passes");
				if ((maximum - minimum) / place < static_cast<std::int64_t>(base)) break;
				place *= static_cast<std::int64_t>(base);
			} while (place > 0);
		}

		void rotateRadix(Recorder& r, bool mostSignificantFirst)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const std::size_t base = static_cast<std::size_t>(r.trace.radixBase >= 2 ? std::min(r.trace.radixBase, 16) : 4);
			std::int64_t topPlace = 1;
			while (topPlace <= (maximum - minimum) / static_cast<std::int64_t>(base)) topPlace *= static_cast<std::int64_t>(base);
			if (!mostSignificantFirst)
			{
				for (std::int64_t place = 1; place <= maximum - minimum; place *= static_cast<std::int64_t>(base))
				{
					stableBucketPartition(r, 0, n, minimum, maximum, base, place, "rotate_lsd_buckets");
					r.addMetric("digit_passes");
					if (place > (maximum - minimum) / static_cast<std::int64_t>(base)) break;
				}
				return;
			}
			struct Range { std::size_t first, last; std::int64_t place; };
			std::vector<Range> pending(1, Range{ 0, n, topPlace });
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				const std::size_t begin = r.trace.events.size();
				stableBucketPartition(r, range.first, range.last, minimum, maximum, base, range.place, "rotate_msd_buckets");
				r.addMetric("digit_passes");
				std::vector<std::size_t> sizes(base, 0);
				for (std::size_t i = range.first; i < range.last; ++i) ++sizes[static_cast<std::size_t>(((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base))];
				std::size_t cursor = range.first;
				for (std::size_t b = 0; b < base; ++b)
				{
					const std::size_t next = cursor + sizes[b];
					if (next - cursor > 1 && range.place > 1)
						pending.push_back(Range{ cursor, next, range.place / static_cast<std::int64_t>(base) });
					cursor = next;
				}
				(void)begin;
			}
		}

		void msdRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			const std::size_t base = static_cast<std::size_t>(r.trace.radixBase >= 2 ? std::min(r.trace.radixBase, 64) : 4);
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			std::int64_t topPlace = 1;
			while (topPlace <= (maximum - minimum) / static_cast<std::int64_t>(base)) topPlace *= static_cast<std::int64_t>(base);
			r.resizeAuxiliary("msd_radix_output", n);
			r.resizeCounters("msd_radix_counts", base);
			struct Range { std::size_t first, last; std::int64_t place; };
			std::vector<Range> pending(1, Range{ 0, n, topPlace });
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				const std::size_t begin = r.trace.events.size();
				std::vector<std::size_t> counts(base, 0), starts(base, range.first), ends(base, range.first);
				for (std::size_t b = 0; b < base; ++b) r.writeCounter("msd_radix_counts", b, 0);
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = static_cast<std::size_t>(((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base));
					++counts[digit]; r.writeCounter("msd_radix_counts", digit, static_cast<int>(counts[digit]));
				}
				std::size_t cursor = range.first;
				for (std::size_t b = 0; b < base; ++b) { starts[b] = cursor; cursor += counts[b]; ends[b] = cursor; }
				std::vector<std::size_t> next = starts;
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					const SortItem item = r.at(i);
					const std::size_t digit = static_cast<std::size_t>(((orderedIntegerKey(item, r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base));
					r.writeAuxiliary("msd_radix_output", next[digit]++, item);
				}
				for (std::size_t i = range.first; i < range.last; ++i) r.writeMain(i, r.auxiliaryAt("msd_radix_output", i));
				for (std::size_t b = 0; b < base; ++b)
				{
					if (counts[b]) r.annotate("msd_radix_counts", RegionKind::Bucket, b, b + 1, 1, static_cast<int>(b),
						"Digit bucket " + std::to_string(b), begin);
					if (range.place > 1 && ends[b] - starts[b] > 1)
						pending.push_back(Range{ starts[b], ends[b], range.place / static_cast<std::int64_t>(base) });
				}
				r.addMetric("digit_passes");
			}
		}

		void gravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t range = static_cast<std::size_t>(maximum - minimum + 1);
			r.resizeCounters("gravity_histogram", range);
			r.resizeCounters("gravity_columns", range);
			r.resizeAuxiliary("gravity_output", n);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t key = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				r.writeCounter("gravity_histogram", key, r.counter("gravity_histogram", key) + 1);
			}
			std::size_t target = 0;
			for (std::size_t key = 0; key < range; ++key)
			{
				r.chargeWork();
				const int count = r.counter("gravity_histogram", key);
				r.writeCounter("gravity_columns", key, count);
				for (std::size_t i = 0; i < n; ++i)
				{
					r.chargeWork();
					if (static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum) == key)
						r.writeAuxiliary("gravity_output", target++, r.at(i));
				}
			}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("gravity_output", i));
			r.addMetric("gravity_columns", static_cast<std::uint64_t>(range));
		}

		void classicGravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t height = static_cast<std::size_t>(maximum - minimum + 1);
			if (height != 0 && n > 256) throw TraceBudgetReached("Classic Gravity is limited to 256 items because its bead matrix is quadratic.");
			r.resizeCounters("gravity_beads", n * height);
			r.resizeCounters("gravity_column_counts", height);
			for (std::size_t row = 0; row < n; ++row)
			{
				const std::size_t beads = static_cast<std::size_t>(orderedIntegerKey(r.at(row), r.trace.direction) - minimum);
				for (std::size_t column = 0; column < beads; ++column)
				{
					r.chargeWork(); r.writeCounter("gravity_beads", row * height + column, 1);
				}
			}
			for (std::size_t column = 0; column < height; ++column)
			{
				std::size_t beads = 0;
				for (std::size_t row = 0; row < n; ++row)
				{
					r.chargeWork();
					beads += r.counter("gravity_beads", row * height + column) != 0;
				}
				r.writeCounter("gravity_column_counts", column, static_cast<int>(beads));
				for (std::size_t row = 0; row < n; ++row)
				{
					r.chargeWork();
					const int next = row >= n - beads ? 1 : 0;
					if (r.counter("gravity_beads", row * height + column) != next)
						r.writeCounter("gravity_beads", row * height + column, next);
				}
			}
			std::vector<SortItem> items;
			items.reserve(n);
			for (std::size_t i = 0; i < n; ++i) items.push_back(r.at(i));
			std::stable_sort(items.begin(), items.end(), [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
			r.resizeAuxiliary("gravity_bead_output", n);
			for (std::size_t i = 0; i < n; ++i) r.writeAuxiliary("gravity_bead_output", i, items[i]);
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("gravity_bead_output", i));
		}

		void simplisticGravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t range = static_cast<std::size_t>(maximum - minimum + 1);
			r.resizeCounters("gravity_transfer_columns", range);
			r.resizeAuxiliary("gravity_transfer_output", n);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t beads = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				for (std::size_t bead = 0; bead < beads; ++bead)
				{
					r.chargeWork(); r.writeCounter("gravity_transfer_columns", bead, r.counter("gravity_transfer_columns", bead) + 1);
				}
			}
			std::size_t target = 0;
			for (std::size_t key = 0; key < range; ++key)
				for (std::size_t i = 0; i < n; ++i)
				{
					r.chargeWork();
					if (static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum) == key)
						r.writeAuxiliary("gravity_transfer_output", target++, r.at(i));
				}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("gravity_transfer_output", i));
		}

		void shatter(Recorder& r, bool simple)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::int64_t range = maximum - minimum + 1;
			if (simple)
			{
				for (std::int64_t place = 1; place < range; place *= 2)
					stableBucketPartition(r, 0, n, minimum, maximum, 2, place, "simple_shatter_bits");
				return;
			}
			const std::size_t groups = std::max<std::size_t>(2, static_cast<std::size_t>(std::sqrt(static_cast<double>(n))));
			const std::int64_t width = std::max<std::int64_t>(1, (range + static_cast<std::int64_t>(groups) - 1) / static_cast<std::int64_t>(groups));
			const std::size_t bucketCount = static_cast<std::size_t>((range + width - 1) / width);
			std::vector<std::size_t> counts(bucketCount, 0), starts(bucketCount, 0);
			std::vector<std::vector<SortItem> > buckets(bucketCount);
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::size_t bucket = static_cast<std::size_t>((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / width);
				buckets[bucket].push_back(r.at(i)); ++counts[bucket];
			}
			r.resizeCounters("shatter_groups", bucketCount); r.resizeAuxiliary("shatter_output", n);
			std::size_t cursor = 0;
			for (std::size_t b = 0; b < bucketCount; ++b) { starts[b] = cursor; cursor += counts[b]; r.writeCounter("shatter_groups", b, static_cast<int>(counts[b])); }
			for (std::size_t b = 0; b < bucketCount; ++b)
			{
				r.chargeWork(buckets[b].size() * buckets[b].size());
				std::stable_sort(buckets[b].begin(), buckets[b].end(), [&r](const SortItem& a, const SortItem& b)
					{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
				for (std::size_t i = 0; i < buckets[b].size(); ++i) r.writeAuxiliary("shatter_output", starts[b] + i, buckets[b][i]);
			}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("shatter_output", i));
		}

		void staticSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const std::size_t bucketsCount = n;
			const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
			std::vector<std::vector<SortItem> > buckets(bucketsCount);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::uint64_t offset = static_cast<std::uint64_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				const std::size_t b = static_cast<std::size_t>((offset * (bucketsCount - 1)) / span);
				buckets[b].push_back(r.at(i));
			}
			r.resizeCounters("static_buckets", bucketsCount); r.resizeAuxiliary("static_output", n);
			std::size_t cursor = 0;
			for (std::size_t b = 0; b < bucketsCount; ++b)
			{
				r.chargeWork(buckets[b].size() * buckets[b].size());
				r.chargeWork();
				r.writeCounter("static_buckets", b, static_cast<int>(buckets[b].size()));
				std::stable_sort(buckets[b].begin(), buckets[b].end(), [&r](const SortItem& a, const SortItem& b)
					{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
				for (std::size_t i = 0; i < buckets[b].size(); ++i) r.writeAuxiliary("static_output", cursor++, buckets[b][i]);
			}
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("static_output", i));
		}

		void timeSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			if (n > 128) throw TraceBudgetReached("Time Sort is limited to 128 items so deterministic deadline events stay inspectable.");
			const int magnitude = std::max(1, std::min(1000, r.trace.timeMagnitude));
			r.resizeAuxiliary("time_deadlines", n);
			std::vector<std::pair<std::int64_t, SortItem> > deadlines;
			deadlines.reserve(n);
			const int phase = r.beginPhase("Deterministic deadlines (ties retain input order)");
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const SortItem item = r.at(i); deadlines.push_back(std::make_pair(orderedIntegerKey(item, r.trace.direction) * magnitude, item));
				r.writeAuxiliary("time_deadlines", i, item);
			}
			std::stable_sort(deadlines.begin(), deadlines.end(), [](const std::pair<std::int64_t, SortItem>& a, const std::pair<std::int64_t, SortItem>& b)
				{ return a.first < b.first; });
			r.endPhase(phase, "Deadline order published");
			const int publish = r.beginPhase("Deadline publication");
			for (std::size_t i = 0; i < n; ++i) r.writeMain(i, deadlines[i].second);
			r.endPhase(publish, "Deadline publication complete");
			for (std::size_t i = 1; i < n; ++i)
			{
				const SortItem a = r.at(i - 1), b = r.at(i);
				r.compare(a, b, i - 1, i);
			}
			r.addMetric("maximum_deadline", static_cast<std::uint64_t>(std::max<std::int64_t>(0,
				orderedIntegerKey(r.at(n - 1), r.trace.direction) * magnitude)));
		}

		void populateAudioEventIndices(SortTrace& trace)
		{
			const bool hasAlgorithmComparisons = !trace.comparisonEventIndices.empty() &&
				!trace.events[trace.comparisonEventIndices.front()].completionScan;
			for (std::size_t i = 0; i < trace.events.size(); ++i)
			{
				const SemanticEvent& event = trace.events[i];
				const SemanticEventType type = event.type;
				const bool hiddenMergeBufferWrite = trace.algorithm == SortAlgorithm::Merge &&
					!trace.mergeBufferVisualizationEnabled && type == SemanticEventType::AuxiliaryItemWrite &&
					event.workspaceName == "merge";
				if ((!hiddenMergeBufferWrite && type == SemanticEventType::AuxiliaryItemWrite) ||
					(type == SemanticEventType::Compare && (hasAlgorithmComparisons || event.completionScan)) ||
					(!hasAlgorithmComparisons && type == SemanticEventType::Probe))
					trace.audioEventIndices.push_back(i);
			}
		}

		SortTrace buildSelectedAlgorithm(const TraceBuildRequest& request, SortAlgorithm algorithm)
		{
			std::unique_ptr<Recorder> recorderStorage;
			try
			{
				recorderStorage.reset(new Recorder(request));
			}
			catch (const TraceBudgetReached& limit)
			{
				SortTrace failed;
				failed.algorithm = algorithm;
				const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
				if (descriptor) { failed.familyId = descriptor->familyId; failed.variantId = descriptor->variantId; }
				failed.direction = request.direction;
				failed.domain = effectiveDomain(request);
				failed.profile = inputProfileForDomain(failed.domain);
				failed.input = request.input;
				failed.initialState = makeInitialState(request.input);
				failed.snapshots.insert(std::make_pair(static_cast<std::size_t>(0), failed.initialState));
				failed.complete = false;
				failed.completionReason = limit.reason;
				return failed;
			}
			Recorder& recorder = *recorderStorage;
			try
			{
				switch (algorithm)
				{
				case SortAlgorithm::Insertion: insertion(recorder); break;
				case SortAlgorithm::Bubble: bubble(recorder); break;
				case SortAlgorithm::Selection: selection(recorder); break;
				case SortAlgorithm::ArrayvBingo: bingo(recorder); break;
				case SortAlgorithm::ArrayvDoubleSelection: doubleSelection(recorder); break;
				case SortAlgorithm::ArrayvStableSelection: stableSelection(recorder); break;
				case SortAlgorithm::ArrayvBadSort: badSort(recorder); break;
				case SortAlgorithm::ArrayvAsynchronous: asynchronousSort(recorder); break;
				case SortAlgorithm::ArrayvPDMerge: pdMergeSort(recorder); break;
				case SortAlgorithm::ArrayvStableCycle: stableCycleSort(recorder); break;
				case SortAlgorithm::ArrayvClassicTournament: classicTournamentSort(recorder); break;
				case SortAlgorithm::ArrayvTournament: tournamentSort(recorder); break;
				case SortAlgorithm::ArrayvSmoothSort: smoothSort(recorder); break;
				case SortAlgorithm::ArrayvBinomialSmoothSort: binomialSmoothSort(recorder); break;
				case SortAlgorithm::ArrayvImprovedBlockSelection: improvedBlockSelectionSort(recorder); break;
				case SortAlgorithm::ArrayvStacklessHybridQuick: stacklessHybridQuickSort(recorder); break;
				case SortAlgorithm::ArrayvStacklessDualPivotQuick: stacklessDualPivotQuickSort(recorder); break;
				case SortAlgorithm::ArrayvHanoi: hanoiSort(recorder); break;
				case SortAlgorithm::ArrayvLibrary: librarySort(recorder); break;
				case SortAlgorithm::ArrayvSimplifiedLibrary: simplifiedLibrarySort(recorder); break;
				case SortAlgorithm::ArrayvWeaveMerge: weaveMergeSort(recorder); break;
				case SortAlgorithm::ArrayvLaziest: laziestStableSort(recorder); break;
				case SortAlgorithm::ArrayvLazierest: lazierestStableSort(recorder); break;
				case SortAlgorithm::ArrayvLazyStable: lazyStableSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedLazyStable: optimizedLazyStableSort(recorder); break;
				case SortAlgorithm::ArrayvFifthMerge: fifthMergeSort(recorder); break;
				case SortAlgorithm::ArrayvQuadMerge: quadMergeSort(recorder); break;
				case SortAlgorithm::ArrayvBufferPartitionMerge: bufferPartitionMergeSort(recorder); break;
				case SortAlgorithm::ArrayvParallelBlockMerge: parallelBlockMergeSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedRotateMerge: optimizedRotateMergeSort(recorder); break;
				case SortAlgorithm::ArrayvGrail: grailSort(recorder); break;
				case SortAlgorithm::ArrayvParallelGrail: parallelGrailSort(recorder); break;
				case SortAlgorithm::ArrayvAdaptiveGrail: adaptiveGrailSort(recorder); break;
				case SortAlgorithm::ArrayvPDQBranched: pdqSort(recorder, false); break;
				case SortAlgorithm::ArrayvPDQBranchless: pdqSort(recorder, true); break;
				case SortAlgorithm::ArrayvSqrt: sqrtMergeSort(recorder, false); break;
				case SortAlgorithm::ArrayvSynchronousSqrt: sqrtMergeSort(recorder, true); break;
				case SortAlgorithm::ArrayvEcta: ectaSort(recorder); break;
				case SortAlgorithm::ArrayvCircularGrail: circularGrailSort(recorder); break;
				case SortAlgorithm::ArrayvUnstableGrail: unstableGrailSort(recorder); break;
				case SortAlgorithm::ArrayvDropMerge: dropMergeSort(recorder); break;
				case SortAlgorithm::ArrayvMedianMerge: medianMergeSort(recorder); break;
				case SortAlgorithm::ArrayvRemi: remiSort(recorder); break;
				case SortAlgorithm::ArrayvChalice: chaliceSort(recorder); break;
				case SortAlgorithm::ArrayvFlan: flanSort(recorder); break;
				case SortAlgorithm::ArrayvFlux: fluxSort(recorder); break;
				case SortAlgorithm::ArrayvKota: kotaSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedWeaveMerge: optimizedWeaveMergeSort(recorder); break;
				case SortAlgorithm::ArrayvWiki: wikiSort(recorder); break;
				case SortAlgorithm::ArrayvYujisBufferedMerge2: yujiBufferedMergeSort(recorder); break;
				case SortAlgorithm::ArrayvAndrey: andreySort(recorder); break;
				case SortAlgorithm::ArrayvNewShuffleMerge: newShuffleMergeSort(recorder); break;
				case SortAlgorithm::ArrayvCycle: cycleSort(recorder); break;
				case SortAlgorithm::Shell: shell(recorder); break;
				case SortAlgorithm::ArrayvBinaryInsertion: binaryInsertion(recorder); break;
				case SortAlgorithm::ArrayvBlockInsertion: blockInsertionSort(recorder); break;
				case SortAlgorithm::ArrayvDoubleInsertion: doubleInsertion(recorder, false); break;
				case SortAlgorithm::ArrayvBinaryDoubleInsertion: doubleInsertion(recorder, true); break;
				case SortAlgorithm::ArrayvIntroSort: introSort(recorder); break;
				case SortAlgorithm::ArrayvPatience: patienceSort(recorder); break;
				case SortAlgorithm::ArrayvShellParallel: shellSortParallel(recorder); break;
				case SortAlgorithm::ArrayvMergeInsertion: mergeInsertionSort(recorder); break;
				case SortAlgorithm::ArrayvStalinOverwrite: stalinOverwriteSort(recorder); break;
				case SortAlgorithm::CanonicalStalinFilter: canonicalStalinFilter(recorder); break;
				case SortAlgorithm::ArrayvClassicTree: classicTreeSort(recorder); break;
				case SortAlgorithm::ArrayvTree: treeSort(recorder); break;
				case SortAlgorithm::ArrayvStrand: strandSort(recorder); break;
				case SortAlgorithm::ArrayvAATree: aaTreeSort(recorder); break;
				case SortAlgorithm::ArrayvAVLTree: avlTreeSort(recorder); break;
				case SortAlgorithm::ArrayvSplayTree: splayTreeSort(recorder); break;
				case SortAlgorithm::ArrayvRedBlackTree: redBlackTreeSort(recorder); break;
				case SortAlgorithm::ArrayvRecursiveShell: recursiveShell(recorder); break;
				case SortAlgorithm::Merge: mergeSort(recorder); break;
				case SortAlgorithm::ArrayvBinaryMerge: binaryMergeSort(recorder); break;
				case SortAlgorithm::ArrayvIterativeTopDownMerge: iterativeTopDownMergeSort(recorder); break;
				case SortAlgorithm::ArrayvInPlaceMerge: inPlaceMergeSort(recorder); break;
				case SortAlgorithm::ArrayvParallelMerge: parallelMergeSort(recorder); break;
				case SortAlgorithm::ArrayvRotateMerge: rotateMergeSort(recorder, false); break;
				case SortAlgorithm::ArrayvRotateMergeParallel: rotateMergeSort(recorder, true); break;
				case SortAlgorithm::ArrayvStacklessRotateMerge: stacklessRotateMergeSort(recorder); break;
				case SortAlgorithm::ArrayvBlockSwapMerge: blockSwapMergeSort(recorder); break;
				case SortAlgorithm::ArrayvWeavedMerge: weavedMergeSort(recorder); break;
				case SortAlgorithm::ArrayvTwinMerge: twinMergeSort(recorder); break;
				case SortAlgorithm::ArrayvTim: timFamilySort(recorder, false); break;
				case SortAlgorithm::ArrayvCocktailMerge: timFamilySort(recorder, true); break;
				case SortAlgorithm::ArrayvImprovedInPlaceMerge: improvedInPlaceMergeSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedBottomUpMerge: optimizedBottomUpMergeSort(recorder); break;
				case SortAlgorithm::Quick: quickSort(recorder); break;
				case SortAlgorithm::ArrayvLLQuick: leftLeftQuickSort(recorder); break;
				case SortAlgorithm::ArrayvLRQuick: leftRightQuickSort(recorder, false); break;
				case SortAlgorithm::ArrayvLRQuickParallel: leftRightQuickSort(recorder, true); break;
				case SortAlgorithm::Heap: heapSort(recorder); break;
				case SortAlgorithm::ArrayvTernaryHeap: arityHeapSort(recorder, 3, false, false); break;
				case SortAlgorithm::ArrayvBaseNHeap: arityHeapSort(recorder,
					static_cast<std::size_t>(request.bucketCount >= 2 ? request.bucketCount : 4), false, false); break;
				case SortAlgorithm::ArrayvMinHeap: arityHeapSort(recorder, 2, true, false); break;
				case SortAlgorithm::ArrayvFlippedMinHeap: arityHeapSort(recorder, 2, true, true); break;
				case SortAlgorithm::ArrayvBottomUpHeap: bottomUpHeapSort(recorder); break;
				case SortAlgorithm::ArrayvBinomialHeap: binomialHeapSort(recorder); break;
				case SortAlgorithm::ArrayvLazyHeap: lazyHeapSort(recorder); break;
				case SortAlgorithm::ArrayvMinMaxHeap: minMaxHeapSort(recorder); break;
				case SortAlgorithm::ArrayvOutOfPlaceHeap: outOfPlaceHeapSort(recorder); break;
				case SortAlgorithm::ArrayvTriangularHeap: triangularHeapSort(recorder); break;
				case SortAlgorithm::ArrayvWeakHeap: weakHeapSort(recorder); break;
				case SortAlgorithm::ArrayvPoplarHeap: poplarHeapSort(recorder); break;
				case SortAlgorithm::Counting: counting(recorder); break;
			case SortAlgorithm::Radix: radix(recorder); break;
			case SortAlgorithm::Gnome: gnome(recorder); break;
			case SortAlgorithm::Cocktail: cocktail(recorder); break;
			case SortAlgorithm::Comb: comb(recorder); break;
			case SortAlgorithm::OddEven: oddEven(recorder); break;
			case SortAlgorithm::Pancake: pancake(recorder); break;
			case SortAlgorithm::ArrayvBurntPancake: arrayvBurntPancake(recorder); break;
			case SortAlgorithm::CanonicalBurntPancake: canonicalBurntPancake(recorder); break;
			case SortAlgorithm::ArrayvPancakeInsertion: arrayvPancakeInsertion(recorder); break;
			case SortAlgorithm::Stooge: stooge(recorder); break;
				case SortAlgorithm::ArrayvBufferedStooge: bufferedStoogeSort(recorder); break;
			case SortAlgorithm::Bogo: bogo(recorder); break;
				case SortAlgorithm::ArrayvBozo: bozoSort(recorder); break;
				case SortAlgorithm::ArrayvBubbleBogo: bubbleBogoSort(recorder); break;
				case SortAlgorithm::ArrayvExchangeBogo: exchangeBogoSort(recorder); break;
				case SortAlgorithm::ArrayvDeterministicBogo: deterministicBogoSort(recorder); break;
				case SortAlgorithm::ArrayvSelectionBogo: selectionBogoSort(recorder); break;
				case SortAlgorithm::ArrayvLessBogo: lessBogoSort(recorder); break;
				case SortAlgorithm::ArrayvCocktailBogo: cocktailBogoSort(recorder); break;
				case SortAlgorithm::ArrayvMedianQuickBogo: medianQuickBogoSort(recorder); break;
				case SortAlgorithm::ArrayvQuickBogo: quickBogoSort(recorder); break;
				case SortAlgorithm::ArrayvRandomGuess: randomGuessSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedGuess: optimizedGuessSort(recorder); break;
				case SortAlgorithm::ArrayvSmartGuess: smartGuessSort(recorder); break;
				case SortAlgorithm::ArrayvGuess: guessSort(recorder); break;
				case SortAlgorithm::ArrayvBogoBogo: bogoBogoSort(recorder); break;
				case SortAlgorithm::ArrayvMergeBogo: mergeBogoSort(recorder); break;
				case SortAlgorithm::ArrayvSmartBogoBogo: smartBogoBogoSort(recorder); break;
				case SortAlgorithm::ArrayvDualPivotQuick: dualPivotQuickSort(recorder, false); break;
				case SortAlgorithm::ArrayvOptimizedDualPivotQuick: dualPivotQuickSort(recorder, true); break;
				case SortAlgorithm::ArrayvStableQuick: stableQuickSort(recorder, false); break;
				case SortAlgorithm::ArrayvStableQuickParallel: stableQuickSort(recorder, true); break;
				case SortAlgorithm::ArrayvForcedStableQuick: forcedStableQuickSort(recorder); break;
				case SortAlgorithm::ArrayvTernaryLLQuick: ternaryQuickSort(recorder, true); break;
				case SortAlgorithm::ArrayvTernaryLRQuick: ternaryQuickSort(recorder, false); break;
				case SortAlgorithm::ArrayvStablePermutation: stablePermutationSort(recorder); break;
				case SortAlgorithm::ArrayvTable: tableSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedBubble: optimizedBubble(recorder); break;
				case SortAlgorithm::ArrayvHybridComb: hybridComb(recorder); break;
				case SortAlgorithm::ArrayvUnoptimizedBubble: unoptimizedBubble(recorder); break;
			case SortAlgorithm::ArrayvSwaplessBubble: swaplessBubble(recorder); break;
				case SortAlgorithm::ArrayvBinaryGnome: binaryGnome(recorder); break;
				case SortAlgorithm::ArrayvCircleIterative: circleSort(recorder, false); break;
				case SortAlgorithm::ArrayvCircleRecursive: circleSort(recorder, true); break;
				case SortAlgorithm::ArrayvIntroCircleIterative: introCircleSort(recorder, false); break;
				case SortAlgorithm::ArrayvIntroCircleRecursive: introCircleSort(recorder, true); break;
				case SortAlgorithm::ArrayvClassicThreeSmoothComb: classicThreeSmoothComb(recorder); break;
				case SortAlgorithm::ArrayvThreeSmoothCombIterative: threeSmoothCombIterative(recorder); break;
				case SortAlgorithm::ArrayvThreeSmoothCombRecursive: recursiveThreeSmooth(recorder, false); break;
				case SortAlgorithm::ArrayvThreeSmoothCombParallel: recursiveThreeSmooth(recorder, true); break;
				case SortAlgorithm::ArrayvBitonicIterative: bitonicIterative(recorder); break;
				case SortAlgorithm::ArrayvBitonicRecursive: bitonicRecursiveSort(recorder, 0, recorder.state.slots.size(), true); break;
				case SortAlgorithm::ArrayvBitonicParallel: bitonicParallel(recorder); break;
				case SortAlgorithm::ArrayvBoseNelsonIterative: boseNelsonIterative(recorder); break;
				case SortAlgorithm::ArrayvBoseNelsonRecursive: boseNelsonSortRecursive(recorder, 0, recorder.state.slots.size(), false); break;
				case SortAlgorithm::ArrayvBoseNelsonParallel: boseNelsonSortRecursive(recorder, 0, recorder.state.slots.size(), true); break;
				case SortAlgorithm::ArrayvOddEvenMergeIterative: oddEvenMergeIterative(recorder); break;
				case SortAlgorithm::ArrayvOddEvenMergeRecursive: oddEvenMergeRecursive(recorder, false); break;
				case SortAlgorithm::ArrayvOddEvenMergeParallel: oddEvenMergeRecursive(recorder, true); break;
				case SortAlgorithm::ArrayvDiamondIterative: diamondIterative(recorder); break;
				case SortAlgorithm::ArrayvDiamondRecursive: diamondRecursiveVariant(recorder); break;
				case SortAlgorithm::ArrayvPairwiseIterative: pairwiseIterative(recorder); break;
				case SortAlgorithm::ArrayvPairwiseRecursive: pairwiseRecursive(recorder, 0, recorder.state.slots.size(), 1); break;
				case SortAlgorithm::ArrayvCrease: creaseSort(recorder); break;
				case SortAlgorithm::ArrayvFold: foldSort(recorder); break;
				case SortAlgorithm::ArrayvMergeExchangeIterative: mergeExchangeIterative(recorder); break;
				case SortAlgorithm::ArrayvPairwiseMergeIterative: pairwiseMergeIterative(recorder); break;
				case SortAlgorithm::ArrayvPairwiseMergeRecursive:
					{
						std::size_t paddedLength = 1;
						while (paddedLength < recorder.state.slots.size()) paddedLength *= 2;
						pairwiseMergeSortRecursive(recorder, 0, paddedLength);
					}
					break;
				case SortAlgorithm::ArrayvWeaveIterative: weaveIterative(recorder); break;
				case SortAlgorithm::ArrayvWeaveParallel: weaveParallel(recorder); break;
				case SortAlgorithm::ArrayvWeaveRecursive: weaveRecursive(recorder); break;
				case SortAlgorithm::ArrayvMatrix: matrixSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedStooge: optimizedStooge(recorder); break;
				case SortAlgorithm::ArrayvOptimizedStoogeStudio: optimizedStoogeStudio(recorder); break;
				case SortAlgorithm::ArrayvQuadStooge: quadStooge(recorder); break;
				case SortAlgorithm::ArrayvSlow: slowSort(recorder); break;
				case SortAlgorithm::ArrayvCircloid: circloidSort(recorder); break;
				case SortAlgorithm::ArrayvSilly: sillySort(recorder); break;
				case SortAlgorithm::ArrayvSnuffle: snuffleSort(recorder); break;
				case SortAlgorithm::ArrayvCompleteGraph: completeGraphSort(recorder); break;
				case SortAlgorithm::ArrayvFun: funSort(recorder); break;
				case SortAlgorithm::ArrayvOptimizedCocktail: optimizedCocktail(recorder); break;
				case SortAlgorithm::ArrayvUnoptimizedCocktail: unoptimizedCocktail(recorder); break;
				case SortAlgorithm::ArrayvOptimizedGnome: optimizedGnome(recorder); break;
				case SortAlgorithm::ArrayvShove: shove(recorder); break;
				case SortAlgorithm::ArrayvSlope: slope(recorder); break;
				case SortAlgorithm::ArrayvAmericanFlag: americanFlag(recorder); break;
				case SortAlgorithm::ArrayvBinaryQuickIterative: binaryQuick(recorder, 0); break;
				case SortAlgorithm::ArrayvBinaryQuickRecursive: binaryQuick(recorder, 1); break;
				case SortAlgorithm::ArrayvClassicGravity: classicGravity(recorder); break;
				case SortAlgorithm::ArrayvFlash: flash(recorder); break;
				case SortAlgorithm::ArrayvGravity: gravity(recorder); break;
				case SortAlgorithm::ArrayvInPlaceLsdRadix: inPlaceLsdRadix(recorder); break;
				case SortAlgorithm::ArrayvIndex: indexSort(recorder); break;
				case SortAlgorithm::ArrayvLsdRadix: radix(recorder); break;
				case SortAlgorithm::ArrayvMsdRadix: msdRadix(recorder); break;
				case SortAlgorithm::ArrayvPigeonhole: pigeonhole(recorder); break;
				case SortAlgorithm::ArrayvRotateLsdRadix: rotateRadix(recorder, false); break;
				case SortAlgorithm::ArrayvRotateMsdRadix: rotateRadix(recorder, true); break;
				case SortAlgorithm::ArrayvShatter: shatter(recorder, false); break;
				case SortAlgorithm::ArrayvSimpleShatter: shatter(recorder, true); break;
				case SortAlgorithm::ArrayvSimplisticGravity: simplisticGravity(recorder); break;
				case SortAlgorithm::ArrayvStatic: staticSort(recorder); break;
				case SortAlgorithm::ArrayvTime: timeSort(recorder); break;
				case SortAlgorithm::ArrayvStacklessAmericanFlag: stacklessAmericanFlag(recorder); break;
				case SortAlgorithm::ArrayvStacklessBinaryQuick: stacklessBinaryRadix(recorder); break;
				default: throw std::invalid_argument("The requested sorting algorithm is not registered.");
				}
				recorder.finish();
			}
			catch (const OperationLimitReached&)
			{
				recorder.finishPartial("Global trace operation budget reached (" + std::to_string(request.maxOperations) + " operations).");
			}
			catch (const TraceBudgetReached& limit)
			{
				recorder.finishPartial(limit.reason);
			}
			if (algorithm == SortAlgorithm::ArrayvBufferedStooge && recorder.trace.complete)
			{
				bool ordered = true;
				for (std::size_t i = 1; i < recorder.state.slots.size(); ++i)
					if (compareItems(recorder.state.slots[i - 1].item, recorder.state.slots[i].item, request.direction) == ComparisonResult::Greater)
					{
						ordered = false;
						break;
					}
				if (!ordered) recorder.trace.outcome = AlgorithmOutcome::IncompleteDemonstration;
			}
			populateAudioEventIndices(recorder.trace);
			std::string reason;
			if (!validateState(recorder.state, request.input.size(), &reason)) throw std::logic_error(reason.c_str());
			return recorder.trace;
		}

#define SORT_REQUEST_BUILDER(name, id) \
		SortTrace name(const TraceBuildRequest& request) { return buildSelectedAlgorithm(request, SortAlgorithm::id); }
		SORT_REQUEST_BUILDER(buildInsertionRequest, Insertion)
		SORT_REQUEST_BUILDER(buildBubbleRequest, Bubble)
		SORT_REQUEST_BUILDER(buildSelectionRequest, Selection)
		SORT_REQUEST_BUILDER(buildArrayvBingoRequest, ArrayvBingo)
		SORT_REQUEST_BUILDER(buildArrayvDoubleSelectionRequest, ArrayvDoubleSelection)
		SORT_REQUEST_BUILDER(buildArrayvStableSelectionRequest, ArrayvStableSelection)
		SORT_REQUEST_BUILDER(buildArrayvBadSortRequest, ArrayvBadSort)
		SORT_REQUEST_BUILDER(buildArrayvAsynchronousRequest, ArrayvAsynchronous)
		SORT_REQUEST_BUILDER(buildArrayvPDMergeRequest, ArrayvPDMerge)
		SORT_REQUEST_BUILDER(buildArrayvStableCycleRequest, ArrayvStableCycle)
		SORT_REQUEST_BUILDER(buildArrayvClassicTournamentRequest, ArrayvClassicTournament)
		SORT_REQUEST_BUILDER(buildArrayvTournamentRequest, ArrayvTournament)
		SORT_REQUEST_BUILDER(buildArrayvSmoothSortRequest, ArrayvSmoothSort)
		SORT_REQUEST_BUILDER(buildArrayvBinomialSmoothSortRequest, ArrayvBinomialSmoothSort)
		SORT_REQUEST_BUILDER(buildArrayvImprovedBlockSelectionRequest, ArrayvImprovedBlockSelection)
		SORT_REQUEST_BUILDER(buildArrayvStacklessHybridQuickRequest, ArrayvStacklessHybridQuick)
		SORT_REQUEST_BUILDER(buildArrayvStacklessDualPivotQuickRequest, ArrayvStacklessDualPivotQuick)
		SORT_REQUEST_BUILDER(buildArrayvHanoiRequest, ArrayvHanoi)
		SORT_REQUEST_BUILDER(buildArrayvLibraryRequest, ArrayvLibrary)
		SORT_REQUEST_BUILDER(buildArrayvSimplifiedLibraryRequest, ArrayvSimplifiedLibrary)
		SORT_REQUEST_BUILDER(buildArrayvWeaveMergeRequest, ArrayvWeaveMerge)
		SORT_REQUEST_BUILDER(buildArrayvLaziestRequest, ArrayvLaziest)
		SORT_REQUEST_BUILDER(buildArrayvLazierestRequest, ArrayvLazierest)
		SORT_REQUEST_BUILDER(buildArrayvLazyStableRequest, ArrayvLazyStable)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedLazyStableRequest, ArrayvOptimizedLazyStable)
		SORT_REQUEST_BUILDER(buildArrayvFifthMergeRequest, ArrayvFifthMerge)
		SORT_REQUEST_BUILDER(buildArrayvQuadMergeRequest, ArrayvQuadMerge)
		SORT_REQUEST_BUILDER(buildArrayvBufferPartitionMergeRequest, ArrayvBufferPartitionMerge)
		SORT_REQUEST_BUILDER(buildArrayvParallelBlockMergeRequest, ArrayvParallelBlockMerge)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedRotateMergeRequest, ArrayvOptimizedRotateMerge)
		SORT_REQUEST_BUILDER(buildArrayvGrailRequest, ArrayvGrail)
		SORT_REQUEST_BUILDER(buildArrayvParallelGrailRequest, ArrayvParallelGrail)
		SORT_REQUEST_BUILDER(buildArrayvAdaptiveGrailRequest, ArrayvAdaptiveGrail)
		SORT_REQUEST_BUILDER(buildArrayvPDQBranchedRequest, ArrayvPDQBranched)
		SORT_REQUEST_BUILDER(buildArrayvPDQBranchlessRequest, ArrayvPDQBranchless)
		SORT_REQUEST_BUILDER(buildArrayvSqrtRequest, ArrayvSqrt)
		SORT_REQUEST_BUILDER(buildArrayvSynchronousSqrtRequest, ArrayvSynchronousSqrt)
		SORT_REQUEST_BUILDER(buildArrayvEctaRequest, ArrayvEcta)
		SORT_REQUEST_BUILDER(buildArrayvCircularGrailRequest, ArrayvCircularGrail)
		SORT_REQUEST_BUILDER(buildArrayvUnstableGrailRequest, ArrayvUnstableGrail)
		SORT_REQUEST_BUILDER(buildArrayvDropMergeRequest, ArrayvDropMerge)
		SORT_REQUEST_BUILDER(buildArrayvMedianMergeRequest, ArrayvMedianMerge)
		SORT_REQUEST_BUILDER(buildArrayvRemiRequest, ArrayvRemi)
		SORT_REQUEST_BUILDER(buildArrayvChaliceRequest, ArrayvChalice)
		SORT_REQUEST_BUILDER(buildArrayvFlanRequest, ArrayvFlan)
		SORT_REQUEST_BUILDER(buildArrayvFluxRequest, ArrayvFlux)
		SORT_REQUEST_BUILDER(buildArrayvKotaRequest, ArrayvKota)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedWeaveMergeRequest, ArrayvOptimizedWeaveMerge)
		SORT_REQUEST_BUILDER(buildArrayvWikiRequest, ArrayvWiki)
		SORT_REQUEST_BUILDER(buildArrayvYujisBufferedMerge2Request, ArrayvYujisBufferedMerge2)
		SORT_REQUEST_BUILDER(buildArrayvAndreyRequest, ArrayvAndrey)
		SORT_REQUEST_BUILDER(buildArrayvNewShuffleMergeRequest, ArrayvNewShuffleMerge)
		SORT_REQUEST_BUILDER(buildArrayvCycleRequest, ArrayvCycle)
		SORT_REQUEST_BUILDER(buildShellRequest, Shell)
		SORT_REQUEST_BUILDER(buildArrayvBinaryInsertionRequest, ArrayvBinaryInsertion)
		SORT_REQUEST_BUILDER(buildArrayvBlockInsertionRequest, ArrayvBlockInsertion)
		SORT_REQUEST_BUILDER(buildArrayvDoubleInsertionRequest, ArrayvDoubleInsertion)
		SORT_REQUEST_BUILDER(buildArrayvBinaryDoubleInsertionRequest, ArrayvBinaryDoubleInsertion)
		SORT_REQUEST_BUILDER(buildArrayvIntroSortRequest, ArrayvIntroSort)
		SORT_REQUEST_BUILDER(buildArrayvPatienceRequest, ArrayvPatience)
		SORT_REQUEST_BUILDER(buildArrayvShellParallelRequest, ArrayvShellParallel)
		SORT_REQUEST_BUILDER(buildArrayvMergeInsertionRequest, ArrayvMergeInsertion)
		SORT_REQUEST_BUILDER(buildArrayvStalinOverwriteRequest, ArrayvStalinOverwrite)
		SORT_REQUEST_BUILDER(buildCanonicalStalinFilterRequest, CanonicalStalinFilter)
		SORT_REQUEST_BUILDER(buildArrayvClassicTreeRequest, ArrayvClassicTree)
		SORT_REQUEST_BUILDER(buildArrayvTreeRequest, ArrayvTree)
		SORT_REQUEST_BUILDER(buildArrayvStrandRequest, ArrayvStrand)
		SORT_REQUEST_BUILDER(buildArrayvAATreeRequest, ArrayvAATree)
		SORT_REQUEST_BUILDER(buildArrayvAVLTreeRequest, ArrayvAVLTree)
		SORT_REQUEST_BUILDER(buildArrayvSplayTreeRequest, ArrayvSplayTree)
		SORT_REQUEST_BUILDER(buildArrayvRedBlackTreeRequest, ArrayvRedBlackTree)
		SORT_REQUEST_BUILDER(buildArrayvRecursiveShellRequest, ArrayvRecursiveShell)
		SORT_REQUEST_BUILDER(buildMergeRequest, Merge)
		SORT_REQUEST_BUILDER(buildQuickRequest, Quick)
		SORT_REQUEST_BUILDER(buildArrayvLLQuickRequest, ArrayvLLQuick)
		SORT_REQUEST_BUILDER(buildArrayvLRQuickRequest, ArrayvLRQuick)
		SORT_REQUEST_BUILDER(buildArrayvLRQuickParallelRequest, ArrayvLRQuickParallel)
		SORT_REQUEST_BUILDER(buildHeapRequest, Heap)
		SORT_REQUEST_BUILDER(buildArrayvTernaryHeapRequest, ArrayvTernaryHeap)
		SORT_REQUEST_BUILDER(buildArrayvBaseNHeapRequest, ArrayvBaseNHeap)
		SORT_REQUEST_BUILDER(buildArrayvMinHeapRequest, ArrayvMinHeap)
		SORT_REQUEST_BUILDER(buildArrayvFlippedMinHeapRequest, ArrayvFlippedMinHeap)
		SORT_REQUEST_BUILDER(buildArrayvBottomUpHeapRequest, ArrayvBottomUpHeap)
		SORT_REQUEST_BUILDER(buildArrayvBinomialHeapRequest, ArrayvBinomialHeap)
		SORT_REQUEST_BUILDER(buildArrayvLazyHeapRequest, ArrayvLazyHeap)
		SORT_REQUEST_BUILDER(buildArrayvMinMaxHeapRequest, ArrayvMinMaxHeap)
		SORT_REQUEST_BUILDER(buildArrayvOutOfPlaceHeapRequest, ArrayvOutOfPlaceHeap)
		SORT_REQUEST_BUILDER(buildArrayvTriangularHeapRequest, ArrayvTriangularHeap)
		SORT_REQUEST_BUILDER(buildArrayvWeakHeapRequest, ArrayvWeakHeap)
		SORT_REQUEST_BUILDER(buildArrayvPoplarHeapRequest, ArrayvPoplarHeap)
		SORT_REQUEST_BUILDER(buildCountingRequest, Counting)
		SORT_REQUEST_BUILDER(buildRadixRequest, Radix)
		SORT_REQUEST_BUILDER(buildGnomeRequest, Gnome)
		SORT_REQUEST_BUILDER(buildCocktailRequest, Cocktail)
		SORT_REQUEST_BUILDER(buildCombRequest, Comb)
		SORT_REQUEST_BUILDER(buildOddEvenRequest, OddEven)
		SORT_REQUEST_BUILDER(buildPancakeRequest, Pancake)
		SORT_REQUEST_BUILDER(buildArrayvBurntPancakeRequest, ArrayvBurntPancake)
		SORT_REQUEST_BUILDER(buildCanonicalBurntPancakeRequest, CanonicalBurntPancake)
		SORT_REQUEST_BUILDER(buildArrayvPancakeInsertionRequest, ArrayvPancakeInsertion)
		SORT_REQUEST_BUILDER(buildStoogeRequest, Stooge)
		SORT_REQUEST_BUILDER(buildArrayvBufferedStoogeRequest, ArrayvBufferedStooge)
		SORT_REQUEST_BUILDER(buildBogoRequest, Bogo)
		SORT_REQUEST_BUILDER(buildArrayvBozoRequest, ArrayvBozo)
		SORT_REQUEST_BUILDER(buildArrayvBubbleBogoRequest, ArrayvBubbleBogo)
		SORT_REQUEST_BUILDER(buildArrayvExchangeBogoRequest, ArrayvExchangeBogo)
		SORT_REQUEST_BUILDER(buildArrayvDeterministicBogoRequest, ArrayvDeterministicBogo)
		SORT_REQUEST_BUILDER(buildArrayvSelectionBogoRequest, ArrayvSelectionBogo)
		SORT_REQUEST_BUILDER(buildArrayvLessBogoRequest, ArrayvLessBogo)
		SORT_REQUEST_BUILDER(buildArrayvCocktailBogoRequest, ArrayvCocktailBogo)
		SORT_REQUEST_BUILDER(buildArrayvMedianQuickBogoRequest, ArrayvMedianQuickBogo)
		SORT_REQUEST_BUILDER(buildArrayvQuickBogoRequest, ArrayvQuickBogo)
		SORT_REQUEST_BUILDER(buildArrayvRandomGuessRequest, ArrayvRandomGuess)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedGuessRequest, ArrayvOptimizedGuess)
		SORT_REQUEST_BUILDER(buildArrayvSmartGuessRequest, ArrayvSmartGuess)
		SORT_REQUEST_BUILDER(buildArrayvGuessRequest, ArrayvGuess)
		SORT_REQUEST_BUILDER(buildArrayvBogoBogoRequest, ArrayvBogoBogo)
		SORT_REQUEST_BUILDER(buildArrayvMergeBogoRequest, ArrayvMergeBogo)
		SORT_REQUEST_BUILDER(buildArrayvSmartBogoBogoRequest, ArrayvSmartBogoBogo)
		SORT_REQUEST_BUILDER(buildArrayvDualPivotQuickRequest, ArrayvDualPivotQuick)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedDualPivotQuickRequest, ArrayvOptimizedDualPivotQuick)
		SORT_REQUEST_BUILDER(buildArrayvStableQuickRequest, ArrayvStableQuick)
		SORT_REQUEST_BUILDER(buildArrayvStableQuickParallelRequest, ArrayvStableQuickParallel)
		SORT_REQUEST_BUILDER(buildArrayvForcedStableQuickRequest, ArrayvForcedStableQuick)
		SORT_REQUEST_BUILDER(buildArrayvTernaryLLQuickRequest, ArrayvTernaryLLQuick)
		SORT_REQUEST_BUILDER(buildArrayvTernaryLRQuickRequest, ArrayvTernaryLRQuick)
		SORT_REQUEST_BUILDER(buildArrayvStablePermutationRequest, ArrayvStablePermutation)
		SORT_REQUEST_BUILDER(buildArrayvTableRequest, ArrayvTable)
		SORT_REQUEST_BUILDER(buildArrayvBinaryMergeRequest, ArrayvBinaryMerge)
		SORT_REQUEST_BUILDER(buildArrayvIterativeTopDownMergeRequest, ArrayvIterativeTopDownMerge)
		SORT_REQUEST_BUILDER(buildArrayvInPlaceMergeRequest, ArrayvInPlaceMerge)
		SORT_REQUEST_BUILDER(buildArrayvParallelMergeRequest, ArrayvParallelMerge)
		SORT_REQUEST_BUILDER(buildArrayvRotateMergeRequest, ArrayvRotateMerge)
		SORT_REQUEST_BUILDER(buildArrayvRotateMergeParallelRequest, ArrayvRotateMergeParallel)
		SORT_REQUEST_BUILDER(buildArrayvStacklessRotateMergeRequest, ArrayvStacklessRotateMerge)
		SORT_REQUEST_BUILDER(buildArrayvBlockSwapMergeRequest, ArrayvBlockSwapMerge)
		SORT_REQUEST_BUILDER(buildArrayvWeavedMergeRequest, ArrayvWeavedMerge)
		SORT_REQUEST_BUILDER(buildArrayvTwinMergeRequest, ArrayvTwinMerge)
		SORT_REQUEST_BUILDER(buildArrayvTimRequest, ArrayvTim)
		SORT_REQUEST_BUILDER(buildArrayvCocktailMergeRequest, ArrayvCocktailMerge)
		SORT_REQUEST_BUILDER(buildArrayvImprovedInPlaceMergeRequest, ArrayvImprovedInPlaceMerge)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedBottomUpMergeRequest, ArrayvOptimizedBottomUpMerge)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedBubbleRequest, ArrayvOptimizedBubble)
		SORT_REQUEST_BUILDER(buildArrayvUnoptimizedBubbleRequest, ArrayvUnoptimizedBubble)
		SORT_REQUEST_BUILDER(buildArrayvSwaplessBubbleRequest, ArrayvSwaplessBubble)
		SORT_REQUEST_BUILDER(buildArrayvBinaryGnomeRequest, ArrayvBinaryGnome)
		SORT_REQUEST_BUILDER(buildArrayvCircleIterativeRequest, ArrayvCircleIterative)
		SORT_REQUEST_BUILDER(buildArrayvCircleRecursiveRequest, ArrayvCircleRecursive)
		SORT_REQUEST_BUILDER(buildArrayvIntroCircleIterativeRequest, ArrayvIntroCircleIterative)
		SORT_REQUEST_BUILDER(buildArrayvIntroCircleRecursiveRequest, ArrayvIntroCircleRecursive)
		SORT_REQUEST_BUILDER(buildArrayvHybridCombRequest, ArrayvHybridComb)
		SORT_REQUEST_BUILDER(buildArrayvClassicThreeSmoothCombRequest, ArrayvClassicThreeSmoothComb)
		SORT_REQUEST_BUILDER(buildArrayvThreeSmoothCombIterativeRequest, ArrayvThreeSmoothCombIterative)
		SORT_REQUEST_BUILDER(buildArrayvThreeSmoothCombRecursiveRequest, ArrayvThreeSmoothCombRecursive)
		SORT_REQUEST_BUILDER(buildArrayvThreeSmoothCombParallelRequest, ArrayvThreeSmoothCombParallel)
		SORT_REQUEST_BUILDER(buildArrayvBitonicIterativeRequest, ArrayvBitonicIterative)
		SORT_REQUEST_BUILDER(buildArrayvBitonicRecursiveRequest, ArrayvBitonicRecursive)
		SORT_REQUEST_BUILDER(buildArrayvBitonicParallelRequest, ArrayvBitonicParallel)
		SORT_REQUEST_BUILDER(buildArrayvBoseNelsonIterativeRequest, ArrayvBoseNelsonIterative)
		SORT_REQUEST_BUILDER(buildArrayvBoseNelsonRecursiveRequest, ArrayvBoseNelsonRecursive)
		SORT_REQUEST_BUILDER(buildArrayvBoseNelsonParallelRequest, ArrayvBoseNelsonParallel)
		SORT_REQUEST_BUILDER(buildArrayvOddEvenMergeIterativeRequest, ArrayvOddEvenMergeIterative)
		SORT_REQUEST_BUILDER(buildArrayvOddEvenMergeRecursiveRequest, ArrayvOddEvenMergeRecursive)
		SORT_REQUEST_BUILDER(buildArrayvOddEvenMergeParallelRequest, ArrayvOddEvenMergeParallel)
		SORT_REQUEST_BUILDER(buildArrayvDiamondIterativeRequest, ArrayvDiamondIterative)
		SORT_REQUEST_BUILDER(buildArrayvDiamondRecursiveRequest, ArrayvDiamondRecursive)
		SORT_REQUEST_BUILDER(buildArrayvPairwiseIterativeRequest, ArrayvPairwiseIterative)
		SORT_REQUEST_BUILDER(buildArrayvPairwiseRecursiveRequest, ArrayvPairwiseRecursive)
		SORT_REQUEST_BUILDER(buildArrayvCreaseRequest, ArrayvCrease)
		SORT_REQUEST_BUILDER(buildArrayvFoldRequest, ArrayvFold)
		SORT_REQUEST_BUILDER(buildArrayvMergeExchangeIterativeRequest, ArrayvMergeExchangeIterative)
		SORT_REQUEST_BUILDER(buildArrayvPairwiseMergeIterativeRequest, ArrayvPairwiseMergeIterative)
		SORT_REQUEST_BUILDER(buildArrayvPairwiseMergeRecursiveRequest, ArrayvPairwiseMergeRecursive)
		SORT_REQUEST_BUILDER(buildArrayvWeaveIterativeRequest, ArrayvWeaveIterative)
		SORT_REQUEST_BUILDER(buildArrayvWeaveParallelRequest, ArrayvWeaveParallel)
		SORT_REQUEST_BUILDER(buildArrayvWeaveRecursiveRequest, ArrayvWeaveRecursive)
		SORT_REQUEST_BUILDER(buildArrayvMatrixRequest, ArrayvMatrix)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedStoogeRequest, ArrayvOptimizedStooge)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedStoogeStudioRequest, ArrayvOptimizedStoogeStudio)
		SORT_REQUEST_BUILDER(buildArrayvQuadStoogeRequest, ArrayvQuadStooge)
		SORT_REQUEST_BUILDER(buildArrayvSlowRequest, ArrayvSlow)
		SORT_REQUEST_BUILDER(buildArrayvCircloidRequest, ArrayvCircloid)
		SORT_REQUEST_BUILDER(buildArrayvSillyRequest, ArrayvSilly)
		SORT_REQUEST_BUILDER(buildArrayvSnuffleRequest, ArrayvSnuffle)
		SORT_REQUEST_BUILDER(buildArrayvCompleteGraphRequest, ArrayvCompleteGraph)
		SORT_REQUEST_BUILDER(buildArrayvFunRequest, ArrayvFun)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedCocktailRequest, ArrayvOptimizedCocktail)
		SORT_REQUEST_BUILDER(buildArrayvUnoptimizedCocktailRequest, ArrayvUnoptimizedCocktail)
		SORT_REQUEST_BUILDER(buildArrayvOptimizedGnomeRequest, ArrayvOptimizedGnome)
		SORT_REQUEST_BUILDER(buildArrayvShoveRequest, ArrayvShove)
		SORT_REQUEST_BUILDER(buildArrayvSlopeRequest, ArrayvSlope)
		SORT_REQUEST_BUILDER(buildArrayvAmericanFlagRequest, ArrayvAmericanFlag)
		SORT_REQUEST_BUILDER(buildArrayvBinaryQuickIterativeRequest, ArrayvBinaryQuickIterative)
		SORT_REQUEST_BUILDER(buildArrayvBinaryQuickRecursiveRequest, ArrayvBinaryQuickRecursive)
		SORT_REQUEST_BUILDER(buildArrayvClassicGravityRequest, ArrayvClassicGravity)
		SORT_REQUEST_BUILDER(buildArrayvFlashRequest, ArrayvFlash)
		SORT_REQUEST_BUILDER(buildArrayvGravityRequest, ArrayvGravity)
		SORT_REQUEST_BUILDER(buildArrayvInPlaceLsdRadixRequest, ArrayvInPlaceLsdRadix)
		SORT_REQUEST_BUILDER(buildArrayvIndexRequest, ArrayvIndex)
		SORT_REQUEST_BUILDER(buildArrayvLsdRadixRequest, ArrayvLsdRadix)
		SORT_REQUEST_BUILDER(buildArrayvMsdRadixRequest, ArrayvMsdRadix)
		SORT_REQUEST_BUILDER(buildArrayvPigeonholeRequest, ArrayvPigeonhole)
		SORT_REQUEST_BUILDER(buildArrayvRotateLsdRadixRequest, ArrayvRotateLsdRadix)
		SORT_REQUEST_BUILDER(buildArrayvRotateMsdRadixRequest, ArrayvRotateMsdRadix)
		SORT_REQUEST_BUILDER(buildArrayvShatterRequest, ArrayvShatter)
		SORT_REQUEST_BUILDER(buildArrayvSimpleShatterRequest, ArrayvSimpleShatter)
		SORT_REQUEST_BUILDER(buildArrayvSimplisticGravityRequest, ArrayvSimplisticGravity)
		SORT_REQUEST_BUILDER(buildArrayvStaticRequest, ArrayvStatic)
		SORT_REQUEST_BUILDER(buildArrayvTimeRequest, ArrayvTime)
		SORT_REQUEST_BUILDER(buildArrayvStacklessAmericanFlagRequest, ArrayvStacklessAmericanFlag)
		SORT_REQUEST_BUILDER(buildArrayvStacklessBinaryQuickRequest, ArrayvStacklessBinaryQuick)
#undef SORT_REQUEST_BUILDER
	}

	const std::vector<AlgorithmDescriptor>& algorithmCatalog()
	{
		static const std::vector<AlgorithmDescriptor> algorithms = {
			AlgorithmDescriptor(SortAlgorithm::Insertion, "insertion", "Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"insertion-methods", "insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Bubble, "bubble", "Bubble Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Selection, "selection", "Selection Sort", "O(n²)", "O(1)", false, true, true, true, &buildSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBingo, "arrayv.bingo", "Bingo Sort", "O(n·m), best O(n + m²)", "O(1)", false, true, true, true, &buildArrayvBingoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "bingo", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDoubleSelection, "arrayv.double_selection", "Double Selection Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvDoubleSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "double-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableSelection, "arrayv.stable_selection", "Stable Selection Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvStableSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "stable-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBadSort, "arrayv.bad_sort", "Bad Sort (Cubic Selection)", "O(n³)", "O(1)", false, true, true, true, &buildArrayvBadSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("bad_sort_candidate_checks", "Suffix-minimum candidates"),
					AlgorithmMetricDescriptor("bad_sort_comparisons", "Cubic comparisons"),
					AlgorithmMetricDescriptor("bad_sort_exchanges", "Selection exchanges")
				}, 128, "checks each suffix candidate against every later item until it finds the first suffix minimum, then exchanges it into place; selection-like but cubic",
				"selection-sorts", "bad-cubic-selection", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::CurrentMinimum, SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAsynchronous, "arrayv.asynchronous_sort", "Asynchronous Sort (Value Scan)", "O(n × key-range)", "O(n) auxiliary item lane", true, false, false, true, &buildArrayvAsynchronousRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("asynchronous_key_passes", "Value passes"),
					AlgorithmMetricDescriptor("asynchronous_source_probes", "Auxiliary probes"),
					AlgorithmMetricDescriptor("asynchronous_emitted_items", "Emitted items")
				}, 384, "copies items to an auxiliary scan lane, advances through the ordered integer-key range, probes the lane on each pass, writes eligible unconsumed items to output, and records consumption separately; stable for equal keys",
				"selection-sorts", "asynchronous-value-scan", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDMerge, "arrayv.pd_merge_sort", "Pattern-Defeating Merge Sort", "O(n log r)", "O(n) auxiliary item lane", true, false, true, true, &buildArrayvPDMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdmerge_run_count", "Natural runs"),
					AlgorithmMetricDescriptor("pdmerge_run_reversals", "Descending runs reversed"),
					AlgorithmMetricDescriptor("pdmerge_merge_passes", "Pairwise merge passes"),
					AlgorithmMetricDescriptor("pdmerge_merges", "Run merges"),
					AlgorithmMetricDescriptor("pdmerge_buffered_items", "Items copied into smaller-side buffers")
				}, MaxItemCount, "detects natural ascending and strict descending runs, reverses descending runs, merges adjacent run pairs in rounds, and buffers the smaller side for stable forward or backward merging",
				"merge-hybrid", "pattern-defeating", "Merge Hybrids",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCycle, "arrayv.cycle", "Cycle Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvCycleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"general permutation sort", "selection-sorts", "cycle", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableCycle, "arrayv.stable_cycle_sort", "Stable Cycle Sort", "O(n²)", "O(n) reference lane and packed flags", true, false, true, true, &buildArrayvStableCycleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stable_cycle_cycles", "Cycles"),
					AlgorithmMetricDescriptor("stable_cycle_rank_probes", "Stable-rank probes"),
					AlgorithmMetricDescriptor("stable_cycle_item_moves", "Cycle item moves"),
					AlgorithmMetricDescriptor("stable_cycle_fixed_positions", "Fixed destinations")
				}, MaxItemCount, "computes each item's stable destination from key order and original equal-key order, moves items through reversible cycle carries, and records completed destinations in a packed bit workspace",
				"selection-sorts", "stable-cycle", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicTournament, "arrayv.classic_tournament_sort", "Classic Tournament Sort", "O(n log n)", "O(n) winner tree and output lane", true, false, true, true, &buildArrayvClassicTournamentRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("classic_tournament_tree_comparisons", "Winner-tree comparisons"),
					AlgorithmMetricDescriptor("classic_tournament_tree_updates", "Winner-tree updates"),
					AlgorithmMetricDescriptor("classic_tournament_extractions", "Extracted winners"),
					AlgorithmMetricDescriptor("classic_tournament_output_writes", "Output lane writes"),
					AlgorithmMetricDescriptor("classic_tournament_padding_leaves", "Padded leaves")
				}, MaxItemCount, "builds a complete power-of-two winner tree, repeatedly emits the root winner, disables its leaf, and recomputes only that winner path with stable left-to-right ties",
				"selection-sorts", "classic-tournament", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTournament, "arrayv.tournament_sort", "Tournament Sort (Loser Tree)", "O(n log n)", "O(n) winner-loser tree and output lane", true, false, true, true, &buildArrayvTournamentRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tournament_matches", "Tournament matches"),
					AlgorithmMetricDescriptor("tournament_loser_replays", "Replayed loser nodes"),
					AlgorithmMetricDescriptor("tournament_tree_updates", "Tree updates"),
					AlgorithmMetricDescriptor("tournament_outputs", "Output items"),
					AlgorithmMetricDescriptor("tournament_padding_leaves", "Padded leaves")
				}, MaxItemCount, "stores each match winner and loser separately; after extracting the root winner, replays the saved loser path from its leaf to update the next winner",
				"selection-sorts", "loser-tree-tournament", "Selection Sorts",
				std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmoothSort, "arrayv.smooth_sort", "Smooth Sort", "O(n) best, O(n log n) worst", "O(1) auxiliary", false, true, true, true, &buildArrayvSmoothSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("smooth_sift_calls", "Leonardo heap sifts"),
					AlgorithmMetricDescriptor("smooth_trinkle_calls", "Forest trinkles"),
					AlgorithmMetricDescriptor("smooth_heap_comparisons", "Heap comparisons"),
					AlgorithmMetricDescriptor("smooth_heap_moves", "Held-key moves"),
					AlgorithmMetricDescriptor("smooth_heap_insertions", "Heap insertions"),
					AlgorithmMetricDescriptor("smooth_heap_extractions", "Heap extractions"),
					AlgorithmMetricDescriptor("smooth_max_leonardo_order", "Maximum Leonardo order")
				}, MaxItemCount, "builds an adaptive forest of Leonardo heaps, maintaining it with sift and trinkle paths before splitting each heap during extraction",
				"selection-sorts", "smooth", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinomialSmoothSort, "arrayv.binomial_smooth_sort", "Binomial Smooth Sort", "O(n log n)", "O(1) auxiliary", false, true, true, true, &buildArrayvBinomialSmoothSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binomial_smooth_thrift_calls", "Thrift calls"),
					AlgorithmMetricDescriptor("binomial_smooth_comparisons", "Heap comparisons"),
					AlgorithmMetricDescriptor("binomial_smooth_swaps", "Heap swaps"),
					AlgorithmMetricDescriptor("binomial_smooth_insertions", "Forest insertions"),
					AlgorithmMetricDescriptor("binomial_smooth_heap_visits", "Visited heap nodes"),
					AlgorithmMetricDescriptor("binomial_smooth_max_height", "Maximum binomial height")
				}, MaxItemCount, "grows a forest of binary-indexed binomial heaps using recursive thrift repairs, then reheapifies each remaining root during extraction",
				"selection-sorts", "binomial-smooth", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvImprovedBlockSelection, "arrayv.improved_block_selection_sort", "Improved Block Selection Merge Sort", "O(n log² n) comparisons", "O(1) auxiliary", false, true, true, true, &buildArrayvImprovedBlockSelectionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_selection_runs", "Merge run widths"),
					AlgorithmMetricDescriptor("block_selection_blocks_selected", "Selected blocks"),
					AlgorithmMetricDescriptor("block_selection_block_swaps", "Block item swaps"),
					AlgorithmMetricDescriptor("block_selection_rotations", "In-place rotations"),
					AlgorithmMetricDescriptor("block_selection_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("block_selection_forward_merges", "Forward merges"),
					AlgorithmMetricDescriptor("block_selection_backward_merges", "Backward merges"),
					AlgorithmMetricDescriptor("block_selection_max_block_length", "Maximum block length")
				}, MaxItemCount, "forms power-of-two blocks, selects blocks into order, and completes each run with forward and backward in-place merges",
				"merge-hybrid", "improved-block-selection", "Merge Hybrids", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessHybridQuick, "arrayv.stackless_hybrid_quick_sort", "Stackless Hybrid Quick Sort", "O(n log n) average", "O(1) explicit stack; O(n²) worst case", false, true, true, true, &buildArrayvStacklessHybridQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stackless_hybrid_maximum_scans", "Maximum scans"),
					AlgorithmMetricDescriptor("stackless_hybrid_maximum_items_parked", "Maximum items parked"),
					AlgorithmMetricDescriptor("stackless_hybrid_partitions", "Hoare partitions"),
					AlgorithmMetricDescriptor("stackless_hybrid_partition_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("stackless_hybrid_insertion_runs", "Insertion fallback runs"),
					AlgorithmMetricDescriptor("stackless_hybrid_duplicate_skips", "Equal-key prefix skips"),
					AlgorithmMetricDescriptor("stackless_hybrid_max_active_range", "Largest active range")
				}, MaxItemCount, "parks the maximum suffix, iteratively partitions the active prefix with Hoare boundaries, and finishes each reduced range with binary insertion",
				"partition-sorts", "stackless-hybrid", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::CurrentMaximum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessDualPivotQuick, "arrayv.stackless_dual_pivot_quick_sort", "Stackless Dual-Pivot Quick Sort", "O(n log n) average, O(n²) worst", "O(1) explicit stack", false, true, true, true, &buildArrayvStacklessDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("stackless_dual_maximum_scans", "Maximum scans"),
					AlgorithmMetricDescriptor("stackless_dual_maximum_items_parked", "Maximum items parked"),
					AlgorithmMetricDescriptor("stackless_dual_partitions", "Dual-pivot partitions"),
					AlgorithmMetricDescriptor("stackless_dual_partition_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("stackless_dual_insertion_runs", "Insertion fallback runs"),
					AlgorithmMetricDescriptor("stackless_dual_duplicate_skips", "Equal-key prefix skips"),
					AlgorithmMetricDescriptor("stackless_dual_max_active_range", "Largest active range")
				}, MaxItemCount, "parks the maximum suffix, samples two pivots from fractional positions, partitions around both, then iterates through insertion-sized ranges without recursion",
				"partition-sorts", "stackless-dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::CurrentMaximum, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Shell, "shell", "Shell Sort", "sequence dependent", "O(1)", false, true, true, true, &buildShellRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Halving), "halving", "Halving", "gap_sequence"),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Knuth), "knuth", "Knuth", "gap_sequence"),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Ciura), "ciura", "Ciura", "gap_sequence"),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Tokuda), "tokuda", "Tokuda", "gap_sequence")
				},
				std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"),
					AlgorithmMetricDescriptor("last_gap", "Current gap")
				}, MaxItemCount, "general permutation sort", "insertion-methods", "shell", "Insertion Methods",
				std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryInsertion, "arrayv.binary_insertion", "Binary Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvBinaryInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"general permutation sort", "insertion-methods", "binary-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvHanoi, "arrayv.hanoi_sort", "Hanoi Sort", "practical only at tiny sizes", "O(n) auxiliary peg storage", false, false, true, true, &buildArrayvHanoiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("hanoi_item_transfers", "Peg item transfers"),
					AlgorithmMetricDescriptor("hanoi_duplicate_batches", "Grouped equal-key moves"),
					AlgorithmMetricDescriptor("hanoi_comparisons", "Peg comparisons"),
					AlgorithmMetricDescriptor("hanoi_tower_moves", "Tower schedule moves")
				}, 16, "Hanoi-inspired stack sort that moves consecutive equal keys together between two auxiliary pegs and the input stack",
				"insertion-methods", "hanoi", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLibrary, "arrayv.library_sort", "Library Sort", "O(n log n) expected, O(n²) worst", "O(n) item workspace with 15 gaps per shelf block", false, false, true, true, &buildArrayvLibraryRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("library_rebalances", "Factor-four rebalances"),
					AlgorithmMetricDescriptor("library_gap_inserts", "Items inserted into gaps"),
					AlgorithmMetricDescriptor("library_block_search_comparisons", "Shelf-block comparisons"),
					AlgorithmMetricDescriptor("library_local_search_comparisons", "Within-shelf comparisons"),
					AlgorithmMetricDescriptor("library_spills", "Full-shelf spill operations")
				}, MaxItemCount, "uses 15-gap shelf blocks, randomized equal-key shelf selection, and factor-four retrieval/rebalance phases",
				"insertion-methods", "library", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem, SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSimplifiedLibrary, "arrayv.simplified_library_sort", "Simplified Library Sort", "O(n log n) average, O(n²) worst", "O(n) item, rank, and location workspaces", true, false, true, true, &buildArrayvSimplifiedLibraryRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("simplified_library_rebalances", "Shelf rebalances"),
					AlgorithmMetricDescriptor("simplified_library_gap_inserts", "Items routed into gaps"),
					AlgorithmMetricDescriptor("simplified_library_binary_comparisons", "Rank-search comparisons"),
					AlgorithmMetricDescriptor("simplified_library_small_fallbacks", "Small-input fallbacks")
				}, MaxItemCount, "stable library sort that records each incoming item's rank, scatters shelves during factor-four rebalances, and insertion-sorts the resulting gaps",
				"insertion-methods", "simplified-library", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveMerge, "arrayv.weave_merge_sort", "Weave Merge Sort", "O(n²) worst", "O(log n) recursion, O(1) item workspace", false, true, true, true, &buildArrayvWeaveMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("weave_recursive_merges", "Recursive weave merges"),
					AlgorithmMetricDescriptor("weave_interleave_swaps", "Interleave rotations"),
					AlgorithmMetricDescriptor("weave_insertion_comparisons", "Weave insertion comparisons"),
					AlgorithmMetricDescriptor("weave_insertion_swaps", "Weave insertion swaps")
				}, 128, "recursively interleaves two sorted runs with adjacent rotations, then insertion-orders the woven range",
				"merge-hybrid", "weave-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLaziest, "arrayv.laziest_sort", "Laziest Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvLaziestRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("laziest_block_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("laziest_merges", "Suffix-fold in-place merges"),
					AlgorithmMetricDescriptor("laziest_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("laziest_exponential_probes", "Exponential-search probes"),
					AlgorithmMetricDescriptor("laziest_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms square-root-sized stable insertion runs, then folds them into a sorted suffix with exponential-search rotations",
				"merge-hybrid", "laziest", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazierest, "arrayv.lazierest_sort", "Lazierest Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvLazierestRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("lazierest_block_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("lazierest_merges", "In-place merges"),
					AlgorithmMetricDescriptor("lazierest_fragmented_merges", "Fragmented suffix folds"),
					AlgorithmMetricDescriptor("lazierest_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("lazierest_exponential_probes", "Exponential-search probes"),
					AlgorithmMetricDescriptor("lazierest_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms ceil-cuberoot-sized stable runs, combines square-sized runs, then performs fragmented stable suffix folds with exponential-search rotations",
				"merge-hybrid", "lazierest", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazyStable, "arrayv.lazy_stable_sort", "Lazy Stable Sort", "O(n²) worst case", "O(1)", true, true, true, true, &buildArrayvLazyStableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("lazy_pair_comparisons", "Initial pair comparisons"),
					AlgorithmMetricDescriptor("lazy_pair_swaps", "Initial pair swaps"),
					AlgorithmMetricDescriptor("lazy_merge_comparisons", "No-buffer merge comparisons"),
					AlgorithmMetricDescriptor("lazy_rotation_swaps", "No-buffer rotation swaps"),
					AlgorithmMetricDescriptor("lazy_merge_passes", "Bottom-up merge passes")
				}, MaxItemCount, "orders adjacent pairs and merges bottom-up with the Grail no-buffer stable merge kernel",
				"merge-hybrid", "lazy-stable", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedLazyStable, "arrayv.optimized_lazy_stable_sort", "Optimized Lazy Stable Sort", "O(n log n) comparisons, O(n²) moves worst case", "O(1)", true, true, true, true, &buildArrayvOptimizedLazyStableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_lazy_runs", "Natural insertion runs"),
					AlgorithmMetricDescriptor("optimized_lazy_run_reversals", "Strict descending run swaps"),
					AlgorithmMetricDescriptor("optimized_lazy_insertion_comparisons", "Run formation comparisons"),
					AlgorithmMetricDescriptor("optimized_lazy_shifts", "Run insertion shifts"),
					AlgorithmMetricDescriptor("optimized_lazy_merge_comparisons", "No-buffer merge comparisons"),
					AlgorithmMetricDescriptor("optimized_lazy_rotation_swaps", "No-buffer rotation swaps"),
					AlgorithmMetricDescriptor("optimized_lazy_merge_passes", "Bottom-up merge passes")
				}, MaxItemCount, "forms stable natural insertion runs of 16 items, reversing only strict descents, then uses bottom-up Grail no-buffer merges",
				"merge-hybrid", "optimized-lazy-stable", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFifthMerge, "arrayv.fifth_merge_sort", "Fifth Merge Sort", "O(n log n) comparisons", "O(n) merge workspace", true, false, true, true, &buildArrayvFifthMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("fifth_partition_count", "Initial partitions"),
					AlgorithmMetricDescriptor("fifth_insertion_runs", "Eight-item insertion runs"),
					AlgorithmMetricDescriptor("fifth_binary_merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("fifth_merges", "Five-way merges"),
					AlgorithmMetricDescriptor("fifth_comparisons", "Five-way merge comparisons"),
					AlgorithmMetricDescriptor("fifth_auxiliary_writes", "Buffer item writes")
				}, MaxItemCount, "splits input into five balanced partitions with the remainder in the first partition, sorts each through eight-item insertion runs and buffered binary passes, then stably merges the five runs through a reusable lane",
				"merge-hybrid", "fifth-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuadMerge, "arrayv.quad_sort", "Quad Sort", "O(n log n) comparisons", "O(n) merge workspace", true, false, true, true, &buildArrayvQuadMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("quad_merged_groups", "Four-run merge groups"),
					AlgorithmMetricDescriptor("quad_leaf_runs", "Eight-item insertion leaves"),
					AlgorithmMetricDescriptor("quad_merge_layers", "Bottom-up quad layers"),
					AlgorithmMetricDescriptor("quad_merges", "Four-way merges"),
					AlgorithmMetricDescriptor("quad_comparisons", "Four-way merge comparisons"),
					AlgorithmMetricDescriptor("quad_auxiliary_writes", "Buffer item writes")
				}, MaxItemCount, "forms eight-item insertion leaves, merges groups of up to four sorted runs bottom-up, and uses a stable reusable buffer lane for each quad merge",
				"merge-hybrid", "quad", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBufferPartitionMerge, "arrayv.buffer_partition_merge_sort", "Buffer Partition Merge Sort", "O(n log² n) worst-case comparisons", "O(n) merge workspace", false, false, true, true, &buildArrayvBufferPartitionMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("buffer_partition_selections", "Median-rank selections"),
					AlgorithmMetricDescriptor("buffer_partition_select_passes", "Selection partitions"),
					AlgorithmMetricDescriptor("buffer_partition_fallbacks", "Bounded selection fallbacks"),
					AlgorithmMetricDescriptor("buffer_partition_merges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("buffer_partition_comparisons", "Selection comparisons")
				}, MaxItemCount, "selects a median boundary for each merge range with median-of-three quickselect and a bounded heap/insertion fallback, recursively sorts both partitions, then merges through an auxiliary lane",
				"merge-hybrid", "buffer-partition", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ProbedItem, SemanticRole::WriteTarget, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelBlockMerge, "arrayv.parallel_block_merge_sort", "Parallel Block Merge Sort", "O(n log n)", "O(n) reusable merge workspace", true, false, true, true, &buildArrayvParallelBlockMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_block_runs", "Sixteen-item insertion runs"),
					AlgorithmMetricDescriptor("parallel_block_tasks", "Independent block merges"),
					AlgorithmMetricDescriptor("parallel_block_layers", "Parallel merge layers"),
					AlgorithmMetricDescriptor("parallel_block_comparisons", "Block merge comparisons")
				}, MaxItemCount, "forms stable 16-item runs then merges disjoint adjacent blocks in deterministic parallel phase groups with one reusable buffer lane",
				"merge-hybrid", "parallel-block-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedRotateMerge, "arrayv.optimized_rotate_merge_sort", "Optimized Rotate Merge Sort", "O(n log n) buffered; O(n log² n) rotation fallback", "Configured power-of-two temporary buffer", true, false, true, true, &buildArrayvOptimizedRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(16, "items_16", "16 items", "rotate_buffer_capacity"),
					AlgorithmOptionDescriptor(32, "items_32", "32 items", "rotate_buffer_capacity"),
					AlgorithmOptionDescriptor(64, "items_64", "64 items", "rotate_buffer_capacity"),
					AlgorithmOptionDescriptor(128, "items_128", "128 items", "rotate_buffer_capacity"),
					AlgorithmOptionDescriptor(256, "items_256", "256 items", "rotate_buffer_capacity")
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_rotate_runs", "32-item insertion runs"),
					AlgorithmMetricDescriptor("optimized_rotate_merges", "Merge ranges"),
					AlgorithmMetricDescriptor("optimized_rotate_buffer_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("optimized_rotate_in_place_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("optimized_rotate_buffer_writes", "Temporary item writes"),
					AlgorithmMetricDescriptor("optimized_rotate_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("optimized_rotate_trimmed_items", "Items excluded by boundary searches")
				}, MaxItemCount, "forms stable 32-item insertion runs; trims already ordered boundaries before merging; uses the configured power-of-two buffer for the smaller run and stable in-place rotations when the run exceeds capacity",
				"merge-hybrid", "optimized-rotate", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvGrail, "arrayv.grail_sort", "Grail Sort", "O(n log n)", "O(1), fixed 512-item, or O(√n) buffer", true, false, true, true, &buildArrayvGrailRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(0, "in_place", "In-place", "grail_buffer_mode"),
					AlgorithmOptionDescriptor(1, "static_buffer", "Static buffer (up to 512 items)", "grail_buffer_mode"),
					AlgorithmOptionDescriptor(2, "dynamic_buffer", "Dynamic √n buffer", "grail_buffer_mode")
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("grail_runs", "Sixteen-item insertion runs"),
					AlgorithmMetricDescriptor("grail_merges", "Block merge ranges"),
					AlgorithmMetricDescriptor("grail_buffered_merges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("grail_rotation_merges", "Stable rotation fallback ranges"),
					AlgorithmMetricDescriptor("grail_buffer_writes", "Temporary buffer writes"),
					AlgorithmMetricDescriptor("grail_comparisons", "Merge comparisons")
				}, MaxItemCount, "stable bottom-up block merge with the pinned no-buffer, fixed-buffer, and dynamic-buffer choices; small runs use binary insertion and oversized merges use stable rotations",
				"merge-hybrid", "grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelGrail, "arrayv.parallel_grail_sort", "Parallel Grail Sort", "O(n log n)", "O(√n) reusable merge buffer", true, false, true, true, &buildArrayvParallelGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_grail_run_size", "Insertion run size"),
					AlgorithmMetricDescriptor("parallel_grail_runs", "Initial insertion runs"),
					AlgorithmMetricDescriptor("parallel_grail_layers", "Deterministic parallel merge layers"),
					AlgorithmMetricDescriptor("parallel_grail_tasks", "Independent merge tasks"),
					AlgorithmMetricDescriptor("parallel_grail_comparisons", "Merge-layer comparisons"),
					AlgorithmMetricDescriptor("parallel_grail_buffered_merges", "Buffer-backed merges"),
					AlgorithmMetricDescriptor("parallel_grail_rotation_merges", "No-buffer rotation fallbacks"),
					AlgorithmMetricDescriptor("parallel_grail_buffer_writes", "Temporary item writes"),
					AlgorithmMetricDescriptor("parallel_grail_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "forms stable 16-item runs then merges independent adjacent ranges in deterministic phase layers, using a square-root buffer and stable rotation merges when a side is too large",
				"merge-hybrid", "parallel-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAdaptiveGrail, "arrayv.adaptive_grail_sort", "Adaptive Grail Sort", "O(n log n) worst case; benefits from existing runs", "O(1) item workspace", true, true, true, true, &buildArrayvAdaptiveGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("adaptive_grail_natural_runs", "Detected monotone runs"),
					AlgorithmMetricDescriptor("adaptive_grail_reversed_runs", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("adaptive_grail_extensions", "Runs extended to minimum length"),
					AlgorithmMetricDescriptor("adaptive_grail_merges", "Adaptive run merges"),
					AlgorithmMetricDescriptor("adaptive_grail_comparisons", "Run detection comparisons"),
					AlgorithmMetricDescriptor("adaptive_grail_rotation_swaps", "Run reversal swaps")
				}, MaxItemCount, "detects natural monotone runs, reverses only strict descents, extends short runs to 32 items, and merges adjacent runs when the stack size rule requires it using the stable Grail no-buffer merge kernel",
				"merge-hybrid", "adaptive-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDQBranched, "arrayv.pdq_branched_sort", "PDQ Sort (Branched)", "Average O(n log n); worst O(n log n)", "O(log n) stack", false, true, true, true, &buildArrayvPDQBranchedRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdq_partitions", "Partitions"), AlgorithmMetricDescriptor("pdq_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("pdq_bad_partitions", "Unbalanced partitions"), AlgorithmMetricDescriptor("pdq_heap_fallbacks", "Heap fallbacks"),
					AlgorithmMetricDescriptor("pdq_maximum_depth", "Maximum recursion depth"), AlgorithmMetricDescriptor("pdq_insertion_ranges", "Insertion-sort leaves")
				}, MaxItemCount, "pattern-defeating quicksort trace with median-of-three Hoare partitions, bounded depth, unbalanced-partition detection, heap fallback, and insertion-sort leaves",
				"quick", "pdq-branched", "Quick Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPDQBranchless, "arrayv.pdq_branchless_sort", "PDQ Sort (Branchless Three-Way)", "Average O(n log n); worst O(n log n)", "O(log n) stack", false, true, true, true, &buildArrayvPDQBranchlessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pdq_partitions", "Partitions"), AlgorithmMetricDescriptor("pdq_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("pdq_bad_partitions", "Unbalanced partitions"), AlgorithmMetricDescriptor("pdq_heap_fallbacks", "Heap fallbacks"),
					AlgorithmMetricDescriptor("pdq_maximum_depth", "Maximum recursion depth"), AlgorithmMetricDescriptor("pdq_equal_ranges", "Equal-key bands"),
					AlgorithmMetricDescriptor("pdq_insertion_ranges", "Insertion-sort leaves")
				}, MaxItemCount, "branchless-source counterpart represented by a three-way equal-band partition, with bounded depth, pattern-bad partition fallback, and insertion-sort leaves",
				"quick", "pdq-branchless", "Quick Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSqrt, "arrayv.sqrt_sort", "Sqrt Sort", "O(n log n)", "O(√n) item workspace", true, false, true, true, &buildArrayvSqrtRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("sqrt_run_size", "Square-root run size"), AlgorithmMetricDescriptor("sqrt_runs", "Insertion-sorted blocks"),
					AlgorithmMetricDescriptor("sqrt_merges", "Buffered merges"), AlgorithmMetricDescriptor("sqrt_merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("sqrt_buffered_merges", "Buffer-backed merges"), AlgorithmMetricDescriptor("sqrt_rotation_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("sqrt_buffer_writes", "Temporary buffer writes"), AlgorithmMetricDescriptor("sqrt_comparisons", "Merge comparisons")
				}, MaxItemCount, "sorts √n-sized insertion runs and merges them bottom-up with a reusable √n buffer, falling back to the stable Grail rotation merge when a full run does not fit",
				"merge-hybrid", "sqrt", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSynchronousSqrt, "arrayv.synchronous_sqrt_sort", "Synchronous Sqrt Sort", "O(n log n)", "O(√n) item workspace", true, false, true, true, &buildArrayvSynchronousSqrtRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("sqrt_run_size", "Square-root run size"), AlgorithmMetricDescriptor("sqrt_runs", "Insertion-sorted blocks"),
					AlgorithmMetricDescriptor("sqrt_merges", "Buffered merges"), AlgorithmMetricDescriptor("sqrt_merge_passes", "Synchronous merge layers"),
					AlgorithmMetricDescriptor("sqrt_synchronous_layers", "Grouped merge layers"), AlgorithmMetricDescriptor("sqrt_buffered_merges", "Buffer-backed merges"),
					AlgorithmMetricDescriptor("sqrt_rotation_merges", "Rotation fallback merges"), AlgorithmMetricDescriptor("sqrt_buffer_writes", "Temporary buffer writes"),
					AlgorithmMetricDescriptor("sqrt_comparisons", "Merge comparisons")
				}, MaxItemCount, "uses the same square-root buffer strategy but groups independent block merges into deterministic synchronous phase layers for playback",
				"merge-hybrid", "synchronous-sqrt", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvEcta, "arrayv.ecta_sort", "Ecta Sort", "O(n log n)", "O(n) merge workspace plus block tags", true, false, true, true, &buildArrayvEctaRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("ecta_min_run", "Adaptive insertion run size"),
					AlgorithmMetricDescriptor("ecta_insertion_runs", "Binary insertion runs"),
					AlgorithmMetricDescriptor("ecta_four_way_groups", "Four-run ping-pong groups"),
					AlgorithmMetricDescriptor("ecta_four_way_merges", "Four-way merge groups"),
					AlgorithmMetricDescriptor("ecta_four_way_comparisons", "Four-way comparisons"),
					AlgorithmMetricDescriptor("ecta_four_way_auxiliary_writes", "Four-way buffer writes"),
					AlgorithmMetricDescriptor("ecta_four_way_passes", "Four-way passes"),
					AlgorithmMetricDescriptor("ecta_binary_merges", "Binary tail merges"),
					AlgorithmMetricDescriptor("ecta_binary_passes", "Binary tail passes")
				}, MaxItemCount, "adapts the insertion-run size, merges four runs per ping-pong layer while the block capacity allows, then finishes the remaining levels with stable binary merges",
				"merge-hybrid", "ecta", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircularGrail, "arrayv.circular_grail_sort", "Circular Grail Sort", "O(n log n)", "O(n) circular merge lane", true, false, true, true, &buildArrayvCircularGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circular_grail_run_size", "Square-root run size"), AlgorithmMetricDescriptor("circular_grail_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("circular_grail_merges", "Ring-buffer merge ranges"), AlgorithmMetricDescriptor("circular_grail_passes", "Merge layers"),
					AlgorithmMetricDescriptor("circular_grail_comparisons", "Merge comparisons"), AlgorithmMetricDescriptor("circular_grail_buffer_writes", "Circular lane writes"),
					AlgorithmMetricDescriptor("circular_grail_wraps", "Ring boundary wraps")
				}, MaxItemCount, "orders square-root-sized runs then performs stable merges through a reusable circular lane whose head advances between ranges and layers",
				"merge-hybrid", "circular-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnstableGrail, "arrayv.unstable_grail_sort", "Unstable Grail Sort", "O(n log n)", "O(n) merge workspace", false, false, true, true, &buildArrayvUnstableGrailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("unstable_grail_runs", "Sixteen-item insertion runs"), AlgorithmMetricDescriptor("unstable_grail_merges", "Block merge ranges"),
					AlgorithmMetricDescriptor("unstable_grail_passes", "Merge layers"), AlgorithmMetricDescriptor("unstable_grail_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("unstable_grail_buffer_writes", "Buffer item writes"),
					AlgorithmMetricDescriptor("unstable_grail_equal_right_choices", "Equal-key right-run selections")
				}, MaxItemCount, "uses buffered block merges with a right-biased equality rule, so equal-key records may change relative order while key order and item identity remain intact",
				"merge-hybrid", "unstable-grail", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDropMerge, "arrayv.drop_merge_sort", "Drop Merge Sort", "O(n log n) typical; heap fallback for disorder", "O(n) monotone-run and drop lanes", false, true, true, true, &buildArrayvDropMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("drop_merge_kept", "Items kept in the monotone subsequence"),
					AlgorithmMetricDescriptor("drop_merge_dropped", "Items buffered after an inversion"),
					AlgorithmMetricDescriptor("drop_merge_scan_comparisons", "Run scan and merge comparisons"),
					AlgorithmMetricDescriptor("drop_merge_early_fallbacks", "Early heap fallbacks"),
					AlgorithmMetricDescriptor("drop_merge_suffix_heap_sorts", "Buffered suffix sorts"),
					AlgorithmMetricDescriptor("drop_merge_final_merges", "Final subsequence merges")
				}, MaxItemCount, "keeps a monotone subsequence, buffers out-of-order items and merges their sorted suffix; when more than 60 percent of a quarter-or-larger scan is dropped, it abandons the subsequence and heap-sorts the original range",
				"merge-hybrid", "drop-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMedianMerge, "arrayv.median_merge_sort", "Median Merge Sort", "O(n log n) typical; O(n²) partition worst case", "O(n) merge buffer", false, true, true, true, &buildArrayvMedianMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("median_merge_partitions", "Quick partitions"),
					AlgorithmMetricDescriptor("median_merge_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("median_merge_bad_partitions", "Highly unbalanced splits"),
					AlgorithmMetricDescriptor("median_merge_median_of_medians_pivots", "Median-of-medians pivots"),
					AlgorithmMetricDescriptor("median_merge_initial_runs", "Sixteen-item merge-sort runs"),
					AlgorithmMetricDescriptor("median_merge_merge_ranges", "Buffered merge ranges"),
					AlgorithmMetricDescriptor("median_merge_merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("median_merge_maximum_depth", "Quick partition depth"),
					AlgorithmMetricDescriptor("median_merge_partition_fallbacks", "No-progress partition fallbacks")
				}, MaxItemCount, "uses median-of-three partitions, switches to a median-of-medians pivot after a highly unbalanced split, and merge-sorts the smaller partition while continuing iteratively through the larger one",
				"merge-hybrid", "median-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRemi, "arrayv.remi_sort", "Remi Sort", "O(n log n) comparisons with a cbrt-way merge tree", "O(n) trace buffer; O(cbrt(n)) run cursor workspace", true, false, true, true, &buildArrayvRemiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("remi_branching_factor", "Cubic-root merge fanout"),
					AlgorithmMetricDescriptor("remi_run_length", "Square-root-cube insertion run length"),
					AlgorithmMetricDescriptor("remi_initial_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("remi_merge_passes", "Multiway merge levels"),
					AlgorithmMetricDescriptor("remi_merges", "Priority-queue merge groups"),
					AlgorithmMetricDescriptor("remi_comparisons", "Multiway merge comparisons"),
					AlgorithmMetricDescriptor("remi_auxiliary_writes", "Merge buffer writes"),
					AlgorithmMetricDescriptor("remi_insertion_runs", "Small binary-insertion runs"),
					AlgorithmMetricDescriptor("remi_binary_merge_passes", "Initial run merge passes")
				}, MaxItemCount, "forms insertion-sorted runs of ceil(cuberoot(N)) squared items, then merges up to ceil(cuberoot(N)) runs at a time through deterministic priority-queue-style selections and replayable run cursors",
				"merge-hybrid", "remi", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvChalice, "arrayv.chalice_sort", "Chalice Sort", "O(n log n) block-merge adaptation", "O(n) auxiliary item workspace", true, false, true, true, &buildArrayvChaliceRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("chalice_distinct_keys", "Distinct block keys collected"),
					AlgorithmMetricDescriptor("chalice_key_probes", "Key selection probes"),
					AlgorithmMetricDescriptor("chalice_blocks", "Sorted blocks"),
					AlgorithmMetricDescriptor("chalice_merge_levels", "Block merge levels"),
					AlgorithmMetricDescriptor("chalice_merges", "Block merges"),
					AlgorithmMetricDescriptor("chalice_comparisons", "Key selection comparisons"),
					AlgorithmMetricDescriptor("chalice_auxiliary_writes", "Key workspace writes"),
					AlgorithmMetricDescriptor("chalice_insertion_runs", "Leaf insertion runs"),
					AlgorithmMetricDescriptor("chalice_binary_merge_passes", "Leaf merge passes")
				}, MaxItemCount, "trace-native block merge adaptation: samples distinct key items, sorts square-root-cubed runs, and combines adjacent runs with reversible two-way merge workspaces",
				"merge-hybrid", "chalice", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlan, "arrayv.flan_sort", "Flan Sort", "O(n log n) average; O(n²) worst", "O(log n) explicit range stack", false, true, true, true, &buildArrayvFlanRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("flan_partitions", "Three-way partitions"),
					AlgorithmMetricDescriptor("flan_pivot_comparisons", "Ninther sample comparisons"),
					AlgorithmMetricDescriptor("flan_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("flan_equal_items", "Items in pivot bands"),
					AlgorithmMetricDescriptor("flan_insertion_fallbacks", "Small-range insertion fallbacks"),
					AlgorithmMetricDescriptor("flan_maximum_depth", "Maximum partition depth")
				}, MaxItemCount, "trace-native Flan adaptation: median-of-nine sample pivot, three-way partitioning, iterative smaller-range scheduling, and insertion-sized leaves",
				"partition-sorts", "flan", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlux, "arrayv.flux_sort", "Flux Sort", "O(n log n) average; O(n²) worst", "O(log n) explicit range stack", false, true, true, true, &buildArrayvFluxRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("flux_analyzed_transitions", "Input transitions analyzed"),
					AlgorithmMetricDescriptor("flux_descending_transitions", "Descending transitions"),
					AlgorithmMetricDescriptor("flux_sorted_fast_paths", "Already-sorted fast paths"),
					AlgorithmMetricDescriptor("flux_reversed_fast_paths", "Reverse fast paths"),
					AlgorithmMetricDescriptor("flux_near_ordered_fallbacks", "Near-ordered insertion fallbacks"),
					AlgorithmMetricDescriptor("flux_partitions", "Median-of-five partitions"),
					AlgorithmMetricDescriptor("flux_pivot_comparisons", "Pivot sample comparisons"),
					AlgorithmMetricDescriptor("flux_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("flux_equal_items", "Items in pivot bands"),
					AlgorithmMetricDescriptor("flux_insertion_fallbacks", "Small-range fallbacks"),
					AlgorithmMetricDescriptor("flux_maximum_depth", "Maximum partition depth")
				}, MaxItemCount, "trace-native Flux adaptation: analyzes adjacent disorder, handles sorted and reversed inputs directly, uses insertion on near-ordered inputs, and partitions general inputs around a median-of-five sample",
				"partition-sorts", "flux", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvKota, "arrayv.kota_sort", "Kota Sort", "O(n log n) merge comparisons", "O(n) trace buffer; O(cuberoot(n)) initial runs", true, false, true, true, &buildArrayvKotaRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("kota_block_length", "Initial block length"),
					AlgorithmMetricDescriptor("kota_initial_blocks", "Initial sorted blocks"),
					AlgorithmMetricDescriptor("kota_merge_levels", "Four-block merge levels"),
					AlgorithmMetricDescriptor("kota_merges", "Block merges"),
					AlgorithmMetricDescriptor("kota_comparisons", "Four-way merge comparisons"),
					AlgorithmMetricDescriptor("kota_auxiliary_writes", "Merge buffer writes")
				}, MaxItemCount, "trace-native Kota block adaptation: sorts square-root-sized blocks, then merges up to four adjacent runs per pass using replayable run cursors",
				"merge-hybrid", "kota", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedWeaveMerge, "arrayv.optimized_weave_merge_sort", "Optimized Weave Merge Sort", "O(n log n) comparisons; O(n log² n) moves", "O(log n) merge stack", true, true, true, true, &buildArrayvOptimizedWeaveMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("optimized_weave_blocks", "Weave blocks"),
					AlgorithmMetricDescriptor("optimized_weave_bit_reversals", "Bit-reversal swaps"),
					AlgorithmMetricDescriptor("optimized_weave_insertion_passes", "Base weave insertions"),
					AlgorithmMetricDescriptor("optimized_weave_merge_comparisons", "Rotation-merge bound comparisons"),
					AlgorithmMetricDescriptor("optimized_weave_rotation_swaps", "Rotation swaps"),
					AlgorithmMetricDescriptor("optimized_weave_rotations", "Rotations")
				}, MaxItemCount, "trace-native optimized-weave adaptation: initializes two-item leaves and combines them with stable binary-cut rotations in a bottom-up power-of-two schedule",
				"merge-hybrid", "optimized-weave-merge", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWiki, "arrayv.wiki_sort", "Wiki Sort", "O(n log n), adaptive on natural runs", "O(sqrt(n)) reusable block cache", true, false, true, true, &buildArrayvWikiRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("wiki_natural_runs", "Natural ascending runs"),
					AlgorithmMetricDescriptor("wiki_reversed_runs", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("wiki_extensions", "Min-run extensions"),
					AlgorithmMetricDescriptor("wiki_merges", "Run merges"),
					AlgorithmMetricDescriptor("wiki_buffered_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("wiki_rotation_merges", "Rotation fallback merges"),
					AlgorithmMetricDescriptor("wiki_buffer_writes", "Cache item writes"),
					AlgorithmMetricDescriptor("wiki_comparisons", "Merge and run comparisons"),
					AlgorithmMetricDescriptor("wiki_merge_comparisons", "Cache-bound searches"),
					AlgorithmMetricDescriptor("wiki_rotation_swaps", "Rotation swaps")
				}, MaxItemCount, "trace-native Wiki adaptation: forms natural runs, extends short runs, then merges through a reusable square-root cache with stable rotation fallback",
				"merge-hybrid", "wiki", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvYujisBufferedMerge2, "arrayv.yujis_buffered_merge_sort2", "Yuji's Buffered Merge Sort 2", "O(n log n)", "O(n/2) reusable item buffer", true, false, true, true, &buildArrayvYujisBufferedMerge2Request,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("yuji_runs", "Initial sorted runs"),
					AlgorithmMetricDescriptor("yuji_merge_levels", "Merge levels"),
					AlgorithmMetricDescriptor("yuji_merges", "Buffered merges"),
					AlgorithmMetricDescriptor("yuji_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("yuji_buffer_writes", "Redistributed buffer writes")
				}, MaxItemCount, "trace-native Yuji buffered-merge adaptation: creates insertion-sized runs and alternates forward or backward merging depending on which side fits the half-size buffer",
				"merge-hybrid", "yujis-buffered-merge-2", "Merge and Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::AuxiliaryCopy, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAndrey, "arrayv.andrey_sort", "Andrey Sort", "O(n log n) comparisons; O(n log² n) moves", "O(log n) recursive merge stack", true, true, true, true, &buildArrayvAndreyRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("andrey_runs", "Square-root-sized runs"),
					AlgorithmMetricDescriptor("andrey_merge_levels", "Block merge levels"),
					AlgorithmMetricDescriptor("andrey_merge_comparisons", "Binary-bound comparisons"),
					AlgorithmMetricDescriptor("andrey_rotations", "Stable block rotations"),
					AlgorithmMetricDescriptor("andrey_rotation_swaps", "Rotation item swaps")
				}, MaxItemCount, "trace-native Andrey adaptation: insertion-sorts square-root-sized blocks and joins them with stable recursive binary-cut in-place merges",
				"merge-sort", "andrey", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::ProbedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvNewShuffleMerge, "arrayv.new_shuffle_merge_sort", "New Shuffle Merge Sort", "O(n log n) comparisons; O(n²) movement worst case", "O(1) auxiliary item workspace", true, true, true, true, &buildArrayvNewShuffleMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("new_shuffle_runs", "Pair runs"),
					AlgorithmMetricDescriptor("new_shuffle_merge_levels", "Shuffle merge levels"),
					AlgorithmMetricDescriptor("new_shuffle_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("new_shuffle_rotations", "Right-run rotations")
				}, 512, "trace-native New Shuffle adaptation: forms ordered pairs and merges adjacent runs by stable rotations that insert each right-run item at its output boundary; capped at 512 items for the trace memory budget",
				"merge-sort", "new-shuffle-merge", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ComparedItem, SemanticRole::WriteTarget }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBlockInsertion, "arrayv.block_insertion", "Block Insertion Sort", "O(n²) worst case; adapts to existing monotone runs", "O(log n) recursion, O(1) item workspace", true, true, true, true, &buildArrayvBlockInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_insertion_runs", "Monotone runs"),
					AlgorithmMetricDescriptor("block_insertion_run_reversals", "Strict descending runs reversed"),
					AlgorithmMetricDescriptor("block_insertion_short_insertions", "Short-run insertions"),
					AlgorithmMetricDescriptor("block_insertion_merges", "Run merges"),
					AlgorithmMetricDescriptor("block_insertion_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("block_insertion_rotations", "Stable block rotations")
				}, MaxItemCount, "detects monotone runs, reverses strict descending runs, inserts singleton or two-item runs into the sorted prefix, and symmetrically merges longer runs in place",
				"insertion-methods", "block-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey, SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDoubleInsertion, "arrayv.double_insertion", "Double Insertion Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvDoubleInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"stable center-outward insertion of key pairs, with linear searches and a final right-side insertion",
				"insertion-methods", "double-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryDoubleInsertion, "arrayv.binary_double_insertion", "Binary Double Insertion Sort", "O(n²) moves, O(n log n) comparisons", "O(1)", true, true, true, true, &buildArrayvBinaryDoubleInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"stable center-outward insertion of key pairs, using binary bounds and a final right-side insertion",
				"insertion-methods", "binary-double-insertion", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroSort, "arrayv.intro_sort", "IntroSort", "average O(n log n), worst O(n log n)", "O(log n) stack", false, false, true, true, &buildArrayvIntroSortRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"),
					AlgorithmMetricDescriptor("heap_fallbacks", "Heap fallbacks"), AlgorithmMetricDescriptor("insertion_passes", "Final insertion passes")
				}, MaxItemCount, "median-of-three Hoare partitions with a 2 floor(log2 N) depth budget, heap fallback, and final whole-array insertion pass at threshold 16",
				"hybrid-sorts", "intro-sort", "Hybrid Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot, SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPatience, "arrayv.patience_sort", "Patience Sort", "O(n log n)", "O(n) auxiliary piles and priority queue", false, false, true, true, &buildArrayvPatienceRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("pile_count", "Piles"), AlgorithmMetricDescriptor("pile_assignments", "Pile assignments"),
					AlgorithmMetricDescriptor("maximum_pile_height", "Maximum pile height"),
					AlgorithmMetricDescriptor("heap_pushes", "Priority queue pushes"), AlgorithmMetricDescriptor("heap_pops", "Priority queue pops")
				}, MaxItemCount, "binary-searches sorted pile tops, pushes items onto auxiliary piles, then merges pile tops with a priority queue; equal-key pile placement follows deterministic binary-search matching",
				"insertion-methods", "patience", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvShellParallel, "arrayv.shell_sort_parallel", "Shell Sort (Parallel Phases)", "sequence dependent", "O(1)", false, true, true, true, &buildArrayvShellParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap"),
					AlgorithmMetricDescriptor("parallel_tasks", "Residue tasks")
				}, MaxItemCount, "extended-Ciura gap passes sort disjoint residue classes; the original concurrent threads are represented as deterministic phase groups",
				"insertion-methods", "shell-parallel", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeInsertion, "arrayv.merge_insertion_sort", "Merge-Insertion Sort", "comparison-optimized; O(n²) movement", "O(1)", false, true, true, true, &buildArrayvMergeInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("block_levels", "Block levels"), AlgorithmMetricDescriptor("block_searches", "Block searches"),
					AlgorithmMetricDescriptor("block_orderings", "Block orderings"), AlgorithmMetricDescriptor("block_insertions", "Block insertions"),
					AlgorithmMetricDescriptor("block_swaps", "Block swaps"), AlgorithmMetricDescriptor("block_reversals", "Block reversals")
				}, MaxItemCount, "in-place block-based merge-insertion schedule using doubling block sizes, block binary search, Jacobsthal-guided insertion groups, block swaps, and block reversals",
				"merge-hybrid", "merge-insertion", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStalinOverwrite, "arrayv.stalin_overwrite", "Stalin Sort (Pinned Overwrite)", "O(n)", "O(1)", false, true, true, true, &buildArrayvStalinOverwriteRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("value_rewrites", "Value rewrites") },
				MaxItemCount, "pinned source behavior: replace each out-of-order cell's key with the previous cell's key while retaining that cell's stable identity",
				"novelty-sorts", "stalin-overwrite", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::CanonicalStalinFilter, "canonical.stalin_discard_filter", "Stalin Sort (Discard Filter)", "O(n)", "O(n) discard provenance", false, false, true, true, &buildCanonicalStalinFilterRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("discarded_items", "Discarded items"), AlgorithmMetricDescriptor("kept_items", "Retained items")
				}, MaxItemCount, "canonical monotone-subsequence demonstration that sends every out-of-order item to a reversible discard lane",
				"novelty-sorts", "stalin-discard", "Novelty Sorts", std::vector<SemanticRole>{ SemanticRole::DiscardedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicTree, "arrayv.classic_tree_sort", "Classic Tree Sort", "O(n²) worst case", "O(n) index workspaces", true, false, true, true, &buildArrayvClassicTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "pinned index-array unbalanced BST: lower and upper child-index workspaces, equal keys descend right, then iterative in-order extraction",
				"tree-sorts", "classic-unbalanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTree, "arrayv.tree_sort", "Tree Sort (Unbalanced)", "O(n²) worst case", "O(n) nodes", true, false, true, true, &buildArrayvTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "unbalanced pointer-style BST with stable right insertion for equal keys, node-item workspace, child-index lanes, and in-order extraction",
				"tree-sorts", "unbalanced-node", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStrand, "arrayv.strand_sort", "Strand Sort", "O(n²) worst case", "O(n) auxiliary strands", false, false, true, true, &buildArrayvStrandRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("strands", "Extracted strands"), AlgorithmMetricDescriptor("strand_items", "Strand items"),
					AlgorithmMetricDescriptor("merge_comparisons", "Merge comparisons"), AlgorithmMetricDescriptor("last_strand_length", "Last strand length")
				}, MaxItemCount, "repeatedly extracts a direction-ordered subsequence from the remaining prefix, merges it into a persistent sorted run, then materializes that run through item-preserving swaps",
				"merge-sort", "strand", "Merge Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem, SemanticRole::AuxiliaryCopy }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAATree, "arrayv.aa_tree_sort", "Tree Sort (AA Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvAATreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "AA level height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_skews", "Skews"),
					AlgorithmMetricDescriptor("tree_splits", "Splits"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "AA tree insertion with level-based skew and split rebalancing, equal keys routed right, and iterative in-order extraction",
				"tree-sorts", "aa-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvAVLTree, "arrayv.avl_tree_sort", "Tree Sort (AVL Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvAVLTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "height-balanced AVL insertion with single and double rotations, equal keys routed right, and iterative in-order extraction",
				"tree-sorts", "avl-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSplayTree, "arrayv.splay_tree_sort", "Splay Tree Sort", "O(n log n) amortized", "O(n) nodes", false, false, true, true, &buildArrayvSplayTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Splay rotations"), AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "splays each insertion key toward the root with zig, zig-zig, and zig-zag rotations before inserting the new node above the root",
				"tree-sorts", "splay", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRedBlackTree, "arrayv.red_black_tree_sort", "Tree Sort (Red-Black Balanced)", "O(n log n)", "O(n) nodes", true, false, true, true, &buildArrayvRedBlackTreeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tree_nodes", "Tree nodes"), AlgorithmMetricDescriptor("tree_height", "Tree height"),
					AlgorithmMetricDescriptor("tree_rotations", "Rotations"), AlgorithmMetricDescriptor("tree_recolors", "Recolors"),
					AlgorithmMetricDescriptor("tree_traversal_visits", "In-order visits")
				}, MaxItemCount, "red-black BST insertion with explicit parent links, recoloring and mirrored single/double rotations; equal keys route right",
				"tree-sorts", "red-black-balanced", "Tree Sorts", std::vector<SemanticRole>{ SemanticRole::ProbedItem }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRecursiveShell, "arrayv.recursive_shell", "Recursive Shell Sort (3-Smooth)", "sequence dependent", "O(1)", false, true, true, true, &buildArrayvRecursiveShellRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "insertion-methods", "recursive-shell", "Insertion Methods", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::Merge, "merge", "Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildMergeRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::BottomUp), "bottom_up", "Bottom-up", "execution_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::TopDown), "top_down", "Top-down", "execution_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AnimatedWrites), "animated_writes", "Animated item writes", "writeback_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AtomicRangeWrite), "compare_then_write_range", "Compare, then write range", "writeback_mode")
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes")
				}, MaxItemCount, "general permutation sort", "merge-sort", "merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryMerge, "arrayv.binary_merge_sort", "Binary Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvBinaryMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binary_insertion_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("merged_ranges", "Merged ranges"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "recursive merge with stable binary-insertion runs below 64 items", "merge-hybrid", "binary-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIterativeTopDownMerge, "arrayv.iterative_top_down_merge_sort", "Iterative Top-down Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvIterativeTopDownMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge layers")
				}, MaxItemCount, "iteratively merges proportionally partitioned subarrays over ceil-power-of-two layers", "merge-sort", "iterative-top-down", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvInPlaceMerge, "arrayv.in_place_merge_sort", "In-Place Merge Sort", "O(n²) movement", "O(log n) stack", false, true, true, true, &buildArrayvInPlaceMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("in_place_merges", "In-place merges"),
					AlgorithmMetricDescriptor("in_place_merge_swaps", "Adjacent merge swaps"),
					AlgorithmMetricDescriptor("merge_comparisons", "Merge comparisons")
				}, 512, "stable recursive merge; each incoming right-run item replaces a larger left item, then the displaced value bubbles through the right run",
				"merge-sort", "in-place-bubble-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvParallelMerge, "arrayv.parallel_merge_sort", "Parallel Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvParallelMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("parallel_merge_tasks", "Parallel sibling task groups"),
					AlgorithmMetricDescriptor("merge_passes", "Merges"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "top-down merge with deterministic sibling phase groups; the app serializes task order in the trace worker while preserving parallel grouping",
				"merge-sort", "parallel", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMerge, "arrayv.rotate_merge_sort", "Rotate Merge Sort", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("rotate_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps"),
					AlgorithmMetricDescriptor("binary_search_comparisons", "Binary-search comparisons")
				}, MaxItemCount, "bottom-up stable merge using asymmetric binary searches and in-place block-swap rotations",
				"merge-sort", "rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMergeParallel, "arrayv.rotate_merge_sort_parallel", "Rotate Merge Sort (Deterministic Parallel Phases)", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvRotateMergeParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Recursive merge tasks"),
					AlgorithmMetricDescriptor("rotate_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps"),
					AlgorithmMetricDescriptor("binary_search_comparisons", "Binary-search comparisons"),
					AlgorithmMetricDescriptor("parallel_sort_tasks", "Parallel sort groups"),
					AlgorithmMetricDescriptor("parallel_merge_tasks", "Parallel merge groups"),
					AlgorithmMetricDescriptor("maximum_merge_depth", "Maximum merge depth")
				}, MaxItemCount, "top-down rotate merge; disjoint sort and merge siblings are serialized into deterministic semantic phase groups",
				"merge-sort", "parallel-rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessRotateMerge, "arrayv.stackless_rotate_merge_sort", "Stackless Rotate Merge Sort", "O(n log² n)", "O(1) item workspace", true, true, true, true, &buildArrayvStacklessRotateMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Partition merge passes"),
					AlgorithmMetricDescriptor("partition_merges", "Partition merges"),
					AlgorithmMetricDescriptor("partition_merge_comparisons", "Partition comparisons"),
					AlgorithmMetricDescriptor("rotate_block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("rotate_item_swaps", "Item swaps")
				}, MaxItemCount, "iterative stable partition merging with binary split searches and in-place block rotations; no recursion or auxiliary item lane",
				"merge-sort", "stackless-rotate", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBlockSwapMerge, "arrayv.block_swap_merge_sort", "Block-Swap Merge Sort", "O(n log² n)", "O(log n) stack, O(1) item workspace", true, true, true, true, &buildArrayvBlockSwapMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes"),
					AlgorithmMetricDescriptor("block_merge_ranges", "Recursive merge ranges"),
					AlgorithmMetricDescriptor("block_merge_comparisons", "Binary-search comparisons"),
					AlgorithmMetricDescriptor("block_swaps", "Block rotations"),
					AlgorithmMetricDescriptor("block_item_swaps", "Item swaps")
				}, MaxItemCount, "bottom-up merge using a symmetric binary search for the largest interchangeable suffix/prefix, followed by block swaps",
				"merge-sort", "block-swap", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeavedMerge, "arrayv.weaved_merge_sort", "Weaved Merge Sort", "O(n log n)", "O(n) auxiliary", false, false, true, true, &buildArrayvWeavedMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("weave_merge_ranges", "Stride merge ranges"),
					AlgorithmMetricDescriptor("weave_depth", "Maximum stride depth"),
					AlgorithmMetricDescriptor("weave_comparisons", "Weave comparisons")
				}, MaxItemCount, "recursively merges interleaved residue classes through a shared auxiliary array; source index-based equal-key routing can reorder original equal items across strides",
				"merge-sort", "weaved", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTwinMerge, "arrayv.twin_sort", "Twin Sort", "O(n log n)", "O(n) allocated buffer (at most n/2 items)", true, false, true, true, &buildArrayvTwinMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("twin_pair_comparisons", "Pair and merge comparisons"),
					AlgorithmMetricDescriptor("twin_run_reversals", "Detected run reversals"),
					AlgorithmMetricDescriptor("twin_merge_passes", "Tail-merge passes"),
					AlgorithmMetricDescriptor("twin_merged_ranges", "Merged ranges"),
					AlgorithmMetricDescriptor("twin_buffer_writes", "Right-run buffer writes")
				}, MaxItemCount, "normalizes alternating descending pair-runs, then bottom-up merges from the tail using a half-size right-run buffer",
				"merge-sort", "twin", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTim, "arrayv.tim_sort", "Tim Sort", "O(n log n), adaptive O(n)", "O(n) temporary item workspace", true, false, true, true, &buildArrayvTimRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tim_runs", "Natural runs"),
					AlgorithmMetricDescriptor("tim_natural_runs", "Detected natural runs"),
					AlgorithmMetricDescriptor("tim_run_reversals", "Descending runs reversed"),
					AlgorithmMetricDescriptor("tim_binary_insertions", "Min-run insertions"),
					AlgorithmMetricDescriptor("tim_min_run", "Minimum run length"),
					AlgorithmMetricDescriptor("tim_merge_passes", "Run merges"),
					AlgorithmMetricDescriptor("tim_merge_comparisons", "Merge comparisons")
				}, MaxItemCount, "detects natural monotone runs, reverses strictly descending runs, extends short runs with stable binary insertion, and collapses runs by TimSort length invariants",
				"merge-hybrid", "tim", "Merge Hybrids"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCocktailMerge, "arrayv.cocktail_merge_sort", "Cocktail Merge Sort", "O(n log n) with fixed min-run preparation", "O(n) temporary item workspace", true, false, true, true, &buildArrayvCocktailMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("tim_cocktail_runs", "Cocktail-built runs"),
					AlgorithmMetricDescriptor("tim_min_run", "Minimum run length"),
					AlgorithmMetricDescriptor("tim_merge_passes", "Run merges"),
					AlgorithmMetricDescriptor("tim_merge_comparisons", "Merge comparisons")
				}, MaxItemCount, "sorts fixed min-run chunks with Cocktail Shaker passes, then merges those runs with TimSort's stack invariants",
				"merge-hybrid", "cocktail-merge", "Merge Hybrids"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvImprovedInPlaceMerge, "arrayv.improved_in_place_merge_sort", "Improved In-Place Merge Sort", "O(n log² n)", "O(log n) recursion, O(1) item workspace", true, true, true, true, &buildArrayvImprovedInPlaceMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("inplace_merge_ranges", "Symmetric merge ranges"),
					AlgorithmMetricDescriptor("inplace_merge_comparisons", "Merge comparisons"),
					AlgorithmMetricDescriptor("inplace_merge_rotations", "Stable block rotations"),
					AlgorithmMetricDescriptor("inplace_merge_rotation_swaps", "Rotation item swaps"),
					AlgorithmMetricDescriptor("inplace_merge_insertion_runs", "Small insertion runs")
				}, MaxItemCount, "stable symmetric in-place merge with lower/upper-bound cuts and order-preserving triple-reversal rotations; small ranges use binary insertion",
				"merge-sort", "improved-in-place", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedBottomUpMerge, "arrayv.optimized_bottom_up_merge_sort", "Optimized Bottom-Up Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildArrayvOptimizedBottomUpMergeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("binary_insertion_runs", "Binary-insertion runs"),
					AlgorithmMetricDescriptor("merge_passes", "Merge passes")
				}, MaxItemCount, "forms stable 16-item binary-insertion runs, then alternates merge passes between primary and auxiliary lanes", "merge-hybrid", "optimized-bottom-up-merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Quick, "quick", "Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average, O(n) worst", false, true, true, true, &buildQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Partitions"),
					AlgorithmMetricDescriptor("maximum_depth", "Maximum depth")
				}, MaxItemCount, "general permutation sort", "partition-sorts", "quick", "Partition Sorts",
				std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLLQuick, "arrayv.ll_quick_sort", "Quick Sort (Left-Left Pointers)", "O(n²) worst", "O(n) recursion", false, false, true, true, &buildArrayvLLQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 128,
				"Lomuto partition with a final-element pivot and left-side boundary; restricted to 128 items for stack safety",
				"partition-sorts", "left-left", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLRQuick, "arrayv.lr_quick_sort", "Quick Sort (Left-Right Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvLRQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"Hoare partition with inward left-right scans around the midpoint value",
				"partition-sorts", "left-right", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLRQuickParallel, "arrayv.lr_quick_sort_parallel", "Quick Sort (Left-Right Parallel Schedule)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvLRQuickParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"midpoint Hoare partition; disjoint recursive branches represented as deterministic phase groups",
				"partition-sorts", "left-right-parallel", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDualPivotQuick, "arrayv.dual_pivot_quick_sort", "Dual-Pivot Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("insertion_fallback_ranges", "Insertion fallback ranges") }, MaxItemCount,
				"two sampled pivots, three-way partitioning, adaptive divisor and insertion fallback for ranges of four items or fewer",
				"partition-sorts", "dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedDualPivotQuick, "arrayv.optimized_dual_pivot_quick_sort", "Optimized Dual-Pivot Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, false, true, true, &buildArrayvOptimizedDualPivotQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("insertion_fallback_ranges", "Insertion fallback ranges"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, MaxItemCount,
				"two sampled pivots, adaptive divisor, 27-item insertion fallback and an equal-key gathering pass for balanced duplicate-heavy partitions",
				"partition-sorts", "optimized-dual-pivot", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableQuick, "arrayv.stable_quick_sort", "Stable Quick Sort", "O(n²) worst", "O(n) auxiliary", true, false, true, true, &buildArrayvStableQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"stable first-pivot partition into ordered left and right reference lanes, followed by stable recursive passes",
				"partition-sorts", "stable-quick", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStableQuickParallel, "arrayv.stable_quick_sort_parallel", "Stable Quick Sort (Deterministic Phases)", "expected O(n log n)", "O(n) auxiliary", true, false, true, true, &buildArrayvStableQuickParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, MaxItemCount,
				"stable random-pivot partitions with a seeded pivot stream; disjoint recursive branches are grouped in deterministic phase spans",
				"partition-sorts", "stable-quick-parallel", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvForcedStableQuick, "arrayv.forced_stable_quick_sort", "Forced-Stable Quick Sort", "average O(n log n), worst O(n²)", "O(n) order workspace", true, false, true, true, &buildArrayvForcedStableQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("stable_tie_breaks", "Original-order tie checks") }, MaxItemCount,
				"median-of-three Hoare partition with an integer original-position workspace used as a stable tie-break key",
				"partition-sorts", "forced-stable-quick", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryLLQuick, "arrayv.ternary_ll_quick_sort", "Ternary Quick Sort (Left-Left Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, true, true, true, &buildArrayvTernaryLLQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, 128,
				"median-of-three left-left scans move equal keys to a trailing block before placing the pivot-equivalent region",
				"partition-sorts", "ternary-left-left", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryLRQuick, "arrayv.ternary_lr_quick_sort", "Ternary Quick Sort (Left-Right Pointers)", "average O(n log n), worst O(n²)", "O(log n) average recursion", false, true, true, true, &buildArrayvTernaryLRQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("partitions", "Partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth"), AlgorithmMetricDescriptor("equal_key_moves", "Equal-key moves") }, 128,
				"median-of-three inward scans collect equal keys at both ends, then rotate them beside the pivot",
				"partition-sorts", "ternary-left-right", "Partition Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvStablePermutation, "arrayv.stable_permutation_sort", "Stable Permutation Sort", "O(n! × n)", "O(n) index workspace", false, false, true, true, &buildArrayvStablePermutationRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("index_permutation_swaps", "Index permutation swaps"),
					AlgorithmMetricDescriptor("permutations_examined", "Permutations examined")
				}, 8, "factorial Heap-style index permutation enumeration with a hard eight-item safety limit",
				"permutation-sorts", "stable-permutation", "Permutation Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTable, "arrayv.table_sort", "Table Sort", "average O(n log n), worst O(n²)", "O(n) index table", true, false, true, true, &buildArrayvTableRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Table partitions"),
					AlgorithmMetricDescriptor("maximum_depth", "Maximum table depth"),
					AlgorithmMetricDescriptor("table_index_swaps", "Index table swaps"),
					AlgorithmMetricDescriptor("permutation_cycles", "Permutation cycles"),
					AlgorithmMetricDescriptor("permutation_cycle_moves", "Cycle moves")
				}, MaxItemCount, "stable indirect quicksort of an integer index table followed by cycle-based primary-array permutation",
				"indirect-sorts", "table", "Indirect Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Heap, "heap", "Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTernaryHeap, "arrayv.ternary_heap", "Ternary Heap Sort", "O(n log₃ n)", "O(1)", false, true, true, true, &buildArrayvTernaryHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "ternary", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBaseNHeap, "arrayv.base_n_heap", "Base-N Max Heap Sort", "O(n logᵦ n)", "O(1)", false, true, true, true, &buildArrayvBaseNHeapRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(4, "base_4", "Base 4", "heap_arity"),
					AlgorithmOptionDescriptor(2, "base_2", "Base 2", "heap_arity"),
					AlgorithmOptionDescriptor(3, "base_3", "Base 3", "heap_arity"),
					AlgorithmOptionDescriptor(8, "base_8", "Base 8", "heap_arity"),
					AlgorithmOptionDescriptor(16, "base_16", "Base 16", "heap_arity")
				}, std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "base-n", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMinHeap, "arrayv.min_heap", "Min Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvMinHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "min-heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFlippedMinHeap, "arrayv.flipped_min_heap", "Flipped Min Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvFlippedMinHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "flipped-min-heap", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBottomUpHeap, "arrayv.bottom_up_heap", "Bottom-Up Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvBottomUpHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "bottom-up", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinomialHeap, "arrayv.binomial_heap", "Binomial Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvBinomialHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "binomial", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLazyHeap, "arrayv.lazy_heap", "Lazy Heap Sort", "O(n√n)", "O(1)", false, true, true, true, &buildArrayvLazyHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "lazy", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMinMaxHeap, "arrayv.min_max_heap", "Min-Max Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvMinMaxHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "min-max", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOutOfPlaceHeap, "arrayv.out_of_place_heap", "Out-of-Place Heap Sort", "O(n log n)", "O(n)", false, false, true, true, &buildArrayvOutOfPlaceHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "out-of-place", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvTriangularHeap, "arrayv.triangular_heap", "Triangular Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvTriangularHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "triangular", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeakHeap, "arrayv.weak_heap", "Weak Heap Sort", "O(n log n)", "O(n) bits", false, true, true, true, &buildArrayvWeakHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "weak", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPoplarHeap, "arrayv.poplar_heap", "Poplar Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildArrayvPoplarHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "poplar", "Heap Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Counting, "counting", "Counting Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildCountingRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"distribution-sorts", "counting", "Distribution Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Radix, "radix", "Radix Sort", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildRadixRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("digit_passes", "Digit passes")
				}, MaxItemCount, "general permutation sort", "distribution-sorts", "radix", "Distribution Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Gnome, "gnome", "Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "gnome", "Exchange Sorts", std::vector<SemanticRole>(), true),
			AlgorithmDescriptor(SortAlgorithm::Cocktail, "cocktail", "Cocktail Shaker Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Comb, "comb", "Comb Sort", "O(n²) worst", "O(1)", false, true, true, true, &buildCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::OddEven, "odd_even", "Odd-Even Transposition Sort", "O(n²)", "O(1)", true, true, true, true, &buildOddEvenRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "odd-even", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Pancake, "pancake", "Pancake Sort", "O(n²)", "O(1)", false, true, true, true, &buildPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"selection-sorts", "pancake", "Selection Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBurntPancake, "arrayv.burnt_pancake", "Burnt Pancake Sort (ArrayV schedule)", "O(n²)", "O(1)", false, true, true, true, &buildArrayvBurntPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort with orientation changes",
				"pancake-sorts", "arrayv-burnt", "Pancake Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum, SemanticRole::CurrentMinimum }),
			AlgorithmDescriptor(SortAlgorithm::CanonicalBurntPancake, "burnt-pancake-canonical", "Burnt Pancake Sort (Canonical)", "O(n²)", "O(1)", false, true, true, true, &buildCanonicalBurntPancakeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort with orientation changes",
				"pancake-sorts", "burnt-canonical", "Pancake Sorts", std::vector<SemanticRole>{ SemanticRole::CurrentMaximum, SemanticRole::CurrentMinimum }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPancakeInsertion, "arrayv.pancake_insertion", "Pancake Insertion Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvPancakeInsertionRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"pancake-sorts", "pancake-insertion", "Pancake Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Stooge, "stooge", "Stooge Sort", "O(n^2.71)", "O(n)", false, true, true, true, &buildStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16, "novelty demonstration",
				"recursive-novelty", "stooge", "Recursive Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBufferedStooge, "arrayv.buffered_stooge_sort", "Buffered Stooge Sort", "superlinear recursive growth", "O(log n) stack, O(1) extra item workspace", false, true, true, true, &buildArrayvBufferedStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("buffered_stooge_calls", "Recursive calls"),
					AlgorithmMetricDescriptor("buffered_stooge_comparisons", "Comparisons"),
					AlgorithmMetricDescriptor("buffered_stooge_buffer_swaps", "Front-buffer swaps"),
					AlgorithmMetricDescriptor("buffered_stooge_cleanup_swaps", "Cleanup swaps")
				}, 8, "recursively sorts middle and right thirds, exchanges their merge into the front of the same array, then cleans up by recursive tail sorting and backward bubbling",
				"recursive-novelty", "buffered-stooge", "Recursive Novelty Sorts"),
		AlgorithmDescriptor(SortAlgorithm::Bogo, "bogo", "Bogo Sort", "O(n·n!) expected", "O(1)", false, true, true, true, &buildBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 8, "stochastic novelty demonstration",
				"stochastic-novelty", "bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBozo, "arrayv.bozo_sort", "Bozo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvBozoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random pair swaps") }, 8,
				"stochastic permutation sort using unconditional random pair swaps; seeded and limited to 8 items",
				"stochastic-novelty", "bozo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBubbleBogo, "arrayv.bubble_bogo_sort", "Bubble Bogo Sort", "O(n² log n) expected", "O(1)", false, true, true, true, &buildArrayvBubbleBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random adjacent probes") }, 8,
				"stochastic adjacent-exchange sort; seeded and limited to 8 items",
				"stochastic-novelty", "bubble-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvExchangeBogo, "arrayv.exchange_bogo_sort", "Exchange Bogo Sort", "O(n² log n) expected", "O(1)", false, true, true, true, &buildArrayvExchangeBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random pair probes") }, 8,
				"stochastic inversion-only random-pair exchange sort; seeded and limited to 8 items",
				"stochastic-novelty", "exchange-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDeterministicBogo, "arrayv.deterministic_bogo_sort", "Deterministic Bogo Sort", "O(n·n!)", "O(n) recursion", false, false, true, true, &buildArrayvDeterministicBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("permutation_checks", "Permutations checked") }, 5,
				"deterministic Heap-permutation enumeration; factorial demonstration limited to 5 items",
				"stochastic-novelty", "deterministic-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSelectionBogo, "arrayv.selection_bogo_sort", "Selection Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvSelectionBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_attempts", "Random minimum swaps") }, 8,
				"seeded random selection of a suffix minimum into each next output position; limited to 8 items",
				"stochastic-novelty", "selection-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvLessBogo, "arrayv.less_bogo_sort", "Less Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvLessBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Suffix shuffles") }, 8,
				"seeded Fisher-Yates suffix shuffle until each next output position contains a minimum; limited to 8 items",
				"stochastic-novelty", "less-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCocktailBogo, "arrayv.cocktail_bogo_sort", "Cocktail Bogo Sort", "unbounded expected", "O(1)", false, true, true, true, &buildArrayvCocktailBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Active-range shuffles") }, 8,
				"bidirectional Less Bogo variant that fixes an active range minimum or maximum before shuffling; seeded and limited to 8 items",
				"stochastic-novelty", "cocktail-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMedianQuickBogo, "arrayv.median_quick_bogo_sort", "Median Quick Bogo Sort", "unbounded expected", "O(log n) recursion", false, false, true, true, &buildArrayvMedianQuickBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Range shuffles"), AlgorithmMetricDescriptor("split_partitions", "Valid half splits"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 8,
				"seeded full-range shuffle until the midpoint halves are value-separated, then recurse; limited to 8 items",
				"stochastic-novelty", "median-quick-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuickBogo, "arrayv.quick_bogo_sort", "Quick Bogo Sort", "unbounded expected", "O(n) recursion", false, false, true, true, &buildArrayvQuickBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Range shuffles"), AlgorithmMetricDescriptor("partitions", "Valid partitions"), AlgorithmMetricDescriptor("maximum_depth", "Maximum depth") }, 5,
				"seeded Fisher-Yates shuffle until a tracked pivot separates its range, then recurse; limited to 5 items",
				"stochastic-novelty", "quick-bogo", "Stochastic Novelty Sorts", std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvRandomGuess, "arrayv.random_guess_sort", "Random Guess Sort", "unbounded expected", "O(n)", false, false, true, true, &buildArrayvRandomGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_guesses", "Random index guesses") }, 5,
				"seeded random index tuples checked for stable ordering, then materialized through an inspectable index workspace; limited to 5 items",
				"stochastic-novelty", "random-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedGuess, "arrayv.optimized_guess_sort", "Optimized Guess Sort", "O(n^n) worst", "O(n)", true, false, true, true, &buildArrayvOptimizedGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples") }, 5,
				"least-significant-index-first exhaustive tuple enumeration, stopping at the first stably sorted index sequence; limited to 5 items",
				"stochastic-novelty", "optimized-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmartGuess, "arrayv.smart_guess_sort", "Smart Guess Sort", "O(n^n) worst", "O(n)", true, false, true, true, &buildArrayvSmartGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples") }, 6,
				"reverse-scanned stable-order check with carry advancement from the detected inversion; limited to 6 items",
				"stochastic-novelty", "smart-guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvGuess, "arrayv.guess_sort", "Guess Sort", "O(n^(n+2)) worst", "O(n)", false, false, true, true, &buildArrayvGuessRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("candidate_tuples", "Candidate index tuples"), AlgorithmMetricDescriptor("index_pair_checks", "Index-pair uniqueness checks") }, 4,
				"exhaustive n-ary index tuples with all-pairs uniqueness and ordering checks; retains the latest valid stable permutation and is limited to 4 items",
				"stochastic-novelty", "guess", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBogoBogo, "arrayv.bogo_bogo_sort", "Bogo Bogo Sort", "unbounded nested expected", "O(n²) nested workspaces", false, false, true, true, &buildArrayvBogoBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("nested_checks", "Nested sortedness checks"), AlgorithmMetricDescriptor("random_shuffles", "Nested shuffles") }, 3,
				"recursively sorts copied prefixes, shuffles the final element into order, compares the candidate against the nested sorted copy, then shuffles the main range; trace-expansion ceiling is three items",
				"stochastic-novelty", "bogo-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeBogo, "arrayv.merge_bogo_sort", "Merge Bogo Sort", "unbounded random interleavings", "O(n) auxiliary", false, true, true, true, &buildArrayvMergeBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_weaves", "Random merge weaves") }, 8,
				"recursively sorts both halves, then samples a seeded stable weave of those runs until the merged range is ordered; limited to eight items",
				"stochastic-novelty", "merge-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSmartBogoBogo, "arrayv.smart_bogo_bogo_sort", "Smart Bogo Bogo Sort", "unbounded nested expected", "O(n) recursion", false, false, true, true, &buildArrayvSmartBogoBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("random_shuffles", "Prefix shuffles"), AlgorithmMetricDescriptor("recursive_prefix_retries", "Recursive prefix retries") }, 3,
				"recursively sorts each prefix; when its boundary exceeds the final item, it shuffles the full active prefix and repeats the recursive prefix sort; trace-expansion ceiling is three items",
				"stochastic-novelty", "smart-bogo-bogo", "Stochastic Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedBubble, "arrayv.optimized_bubble", "Optimized Bubble Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildArrayvOptimizedBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnoptimizedBubble, "arrayv.unoptimized_bubble", "Unoptimized Bubble Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvUnoptimizedBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "unoptimized-bubble", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSwaplessBubble, "arrayv.swapless_bubble", "Swapless Bubble Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvSwaplessBubbleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general stable permutation sort",
				"exchange-sorts", "swapless-bubble", "Exchange Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryGnome, "arrayv.binary_gnome", "Binary Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvBinaryGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "binary-gnome", "Exchange Sorts", std::vector<SemanticRole>{ SemanticRole::HeldKey }),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircleIterative, "arrayv.circle_iterative", "Circle Sort (Iterative)", "O(n log² n)", "O(1) auxiliary", false, true, true, true, &buildArrayvCircleIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "circle-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircleRecursive, "arrayv.circle_recursive", "Circle Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvCircleRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "circle-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroCircleIterative, "arrayv.intro_circle_sort_iterative", "Introspective Circle Sort (Iterative)", "circle passes with O(n²) binary-insertion fallback", "O(1)", false, true, true, true, &buildArrayvIntroCircleIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circle_passes", "Circle passes"), AlgorithmMetricDescriptor("binary_fallbacks", "Binary insertion fallbacks")
				}, MaxItemCount, "iterative mirrored-pair passes stop after the pinned half-logarithmic threshold and use binary insertion",
				"exchange-sorts", "intro-circle-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvIntroCircleRecursive, "arrayv.intro_circle_sort_recursive", "Introspective Circle Sort (Recursive)", "circle passes with O(n²) binary-insertion fallback", "O(log n) recursion", false, false, true, true, &buildArrayvIntroCircleRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("circle_passes", "Circle passes"), AlgorithmMetricDescriptor("binary_fallbacks", "Binary insertion fallbacks")
				}, MaxItemCount, "recursive mirrored-pair passes stop after the pinned half-logarithmic threshold and use binary insertion",
				"exchange-sorts", "intro-circle-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvHybridComb, "arrayv.hybrid_comb_sort", "Hybrid Comb Sort", "O(n²) worst; comb passes before insertion", "O(1)", false, true, true, true, &buildArrayvHybridCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Last gap"),
					AlgorithmMetricDescriptor("insertion_fallbacks", "Insertion fallbacks")
				}, MaxItemCount, "shrink-factor 1.3 comb passes switch to insertion sort once gap reaches min(8, N/32)",
				"exchange-sorts", "hybrid-comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvClassicThreeSmoothComb, "arrayv.classic_three_smooth_comb", "Classic 3-Smooth Comb Sort", "O(n²) worst", "O(1)", false, true, true, true, &buildArrayvClassicThreeSmoothCombRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "classic-three-smooth-comb", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombIterative, "arrayv.three_smooth_comb_iterative", "3-Smooth Comb Sort (Iterative)", "O(n²) worst", "O(1)", false, true, true, true, &buildArrayvThreeSmoothCombIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort; single pass per 3-smooth gap", "exchange-sorts", "three-smooth-iterative", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombRecursive, "arrayv.three_smooth_comb_recursive", "Recursive 3-Smooth Comb Sort", "O(n²) worst", "O(log n) stack", false, true, true, true, &buildArrayvThreeSmoothCombRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "three-smooth-recursive", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvThreeSmoothCombParallel, "arrayv.three_smooth_comb_parallel", "Parallel 3-Smooth Comb Sort (deterministic phases)", "O(n²) worst", "O(log n) scheduled depth", false, true, true, true, &buildArrayvThreeSmoothCombParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("gap_passes", "Gap passes"), AlgorithmMetricDescriptor("last_gap", "Current gap") },
				MaxItemCount, "general permutation sort", "exchange-sorts", "three-smooth-parallel", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicIterative, "arrayv.bitonic_sort_iterative", "Bitonic Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvBitonicIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bitonic-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicRecursive, "arrayv.bitonic_sort_recursive", "Bitonic Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvBitonicRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bitonic-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBitonicParallel, "arrayv.bitonic_sort_parallel", "Bitonic Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvBitonicParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "bitonic-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonIterative, "arrayv.bose_nelson_sort_iterative", "Bose-Nelson Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvBoseNelsonIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bose-nelson-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonRecursive, "arrayv.bose_nelson_sort_recursive", "Bose-Nelson Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvBoseNelsonRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "bose-nelson-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvBoseNelsonParallel, "arrayv.bose_nelson_sort_parallel", "Bose-Nelson Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvBoseNelsonParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "bose-nelson-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeIterative, "arrayv.odd_even_merge_sort_iterative", "Odd-Even Merge Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvOddEvenMergeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "odd-even-merge-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeRecursive, "arrayv.odd_even_merge_sort_recursive", "Odd-Even Merge Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvOddEvenMergeRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "odd-even-merge-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOddEvenMergeParallel, "arrayv.odd_even_merge_sort_parallel", "Odd-Even Merge Sort (Parallel schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvOddEvenMergeParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "odd-even-merge-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDiamondIterative, "arrayv.diamond_sort_iterative", "Diamond Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvDiamondIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "diamond-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvDiamondRecursive, "arrayv.diamond_sort_recursive", "Diamond Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvDiamondRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "diamond-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseIterative, "arrayv.pairwise_sort_iterative", "Pairwise Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvPairwiseIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseRecursive, "arrayv.pairwise_sort_recursive", "Pairwise Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvPairwiseRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCrease, "arrayv.crease_sort", "Crease Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvCreaseRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "crease", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFold, "arrayv.fold_sort", "Fold Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvFoldRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "fold", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMergeExchangeIterative, "arrayv.merge_exchange_iterative", "Merge-Exchange Sort", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvMergeExchangeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "merge-exchange-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseMergeIterative, "arrayv.pairwise_merge_iterative", "Pairwise Merge Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvPairwiseMergeIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-merge-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvPairwiseMergeRecursive, "arrayv.pairwise_merge_recursive", "Pairwise Merge Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvPairwiseMergeRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "pairwise-merge-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveIterative, "arrayv.weave_iterative", "Weave Sort (Iterative)", "O(n log² n)", "O(1)", false, true, true, true, &buildArrayvWeaveIterativeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "weave-iterative", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveParallel, "arrayv.weave_parallel", "Weave Sort (Parallel Schedule)", "O(n log² n)", "O(log n) scheduled depth", false, false, true, true, &buildArrayvWeaveParallelRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; deterministic parallel-phase schedule", "sorting-networks", "weave-parallel", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvWeaveRecursive, "arrayv.weave_recursive", "Weave Sort (Recursive)", "O(n log² n)", "O(log n) stack", false, false, true, true, &buildArrayvWeaveRecursiveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Network layers"), AlgorithmMetricDescriptor("network_comparators", "Network comparators") },
				MaxItemCount, "sorting network; general permutation sort", "sorting-networks", "weave-recursive", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvMatrix, "arrayv.matrix_sort", "Matrix Sort", "Data-dependent iterative matrix sorting", "O(n) auxiliary recursion", false, true, true, true, &buildArrayvMatrixRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("matrix_interleave_passes", "Matrix interleave passes") },
				128, "general permutation sort; matrix interleave and insertion phases", "sorting-networks", "matrix", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedStooge, "arrayv.optimized_stooge", "Optimized Stooge Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvOptimizedStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 128,
				"incomplete demonstration; endpoint exchange with forward and backward scans can leave an inversion", "stooge-sorts", "optimized", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedStoogeStudio, "arrayv.optimized_stooge_studio", "Optimized Stooge Sort (Studio)", "O(n²) worst, O(n) near-sorted", "O(n) recursion", true, true, true, true, &buildArrayvOptimizedStoogeStudioRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 64,
				"stable permutation sort; change-guided recursive Stooge network", "stooge-sorts", "studio-optimized", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvQuadStooge, "arrayv.quad_stooge", "Quad Stooge Sort", "O(n^2.58) approximate", "O(log n) stack", false, true, true, true, &buildArrayvQuadStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort; six overlapping half-range calls", "stooge-sorts", "quad", "Stooge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSlow, "arrayv.slow_sort", "Slow Sort", "super-polynomial practical growth", "O(log n) stack", false, true, true, true, &buildArrayvSlowRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort", "slow-sorts", "slow", "Slow Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCircloid, "arrayv.circloid", "Circloid Sort", "O(n log² n)", "O(log n) stack", false, true, true, true, &buildArrayvCircloidRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount,
				"recursive mirrored-comparison exchange sort with repeated whole-range passes", "exchange-sorts", "circloid", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSilly, "arrayv.silly_sort", "Silly Sort", "exponential practical growth", "O(n) stack", false, true, true, true, &buildArrayvSillyRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"recursive novelty permutation sort; equal minima are exchanged", "novelty-sorts", "silly", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSnuffle, "arrayv.snuffle_sort", "Snuffle Sort", "high recursive growth", "O(n) stack", false, true, true, true, &buildArrayvSnuffleRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16,
				"overlapping recursive endpoint-exchange demonstration", "novelty-sorts", "snuffle", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvCompleteGraph, "arrayv.complete_graph", "Complete Graph Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvCompleteGraphRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("network_layers", "Comparator layers"), AlgorithmMetricDescriptor("network_comparators", "Comparators") }, MaxItemCount,
				"deterministic complete-graph comparator schedule emitted in reversible network layers", "sorting-networks", "complete-graph", "Sorting Networks"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFun, "arrayv.fun_sort", "Fun Sort", "O(n² log n) worst", "O(1)", false, true, true, true, &buildArrayvFunRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 64,
				"incomplete demonstration: unordered binary-search exchanges can converge without globally sorting; bounded to 64 items", "novelty-sorts", "fun", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedCocktail, "arrayv.optimized_cocktail", "Optimized Cocktail Shaker Sort", "O(n²), best O(n)", "O(1)", true, true, true, true, &buildArrayvOptimizedCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvUnoptimizedCocktail, "arrayv.unoptimized_cocktail", "Unoptimized Cocktail Shaker Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvUnoptimizedCocktailRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "unoptimized-cocktail", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvOptimizedGnome, "arrayv.optimized_gnome", "Optimized Gnome Sort", "O(n²)", "O(1)", true, true, true, true, &buildArrayvOptimizedGnomeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "optimized-gnome", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvShove, "arrayv.shove", "Shove Sort", "O(n³) worst", "O(1)", false, true, true, true, &buildArrayvShoveRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 512, "general permutation sort",
				"exchange-sorts", "shove", "Exchange Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvSlope, "arrayv.slope", "Slope Sort", "O(n²)", "O(1)", false, true, true, true, &buildArrayvSlopeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"exchange-sorts", "slope", "Exchange Sorts"),
		AlgorithmDescriptor(SortAlgorithm::ArrayvAmericanFlag, "arrayv.american_flag", "American Flag Sort", "O(w(n + R))", "O(R + w)", false, true, false, true, &buildArrayvAmericanFlagRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") },
			MaxItemCount, "general integer permutation sort", "integer-radix", "american_flag", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryQuickIterative, "arrayv.binary_quick_iterative", "Binary Quick Sort (Iterative)", "O(w·n)", "O(n)", false, true, false, true, &buildArrayvBinaryQuickIterativeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "binary_quick_iterative", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvBinaryQuickRecursive, "arrayv.binary_quick_recursive", "Binary Quick Sort (Recursive)", "O(w·n)", "O(w)", false, true, false, true, &buildArrayvBinaryQuickRecursiveRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "binary_quick_recursive", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvClassicGravity, "arrayv.classic_gravity", "Classic Gravity Sort", "O(n·k)", "O(n·k)", true, false, false, true, &buildArrayvClassicGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 128, "general integer distribution sort", "gravity-sorts", "classic_gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvFlash, "arrayv.flash", "Flash Sort", "O(n + m)", "O(m)", false, true, false, true, &buildArrayvFlashRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "flash", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvGravity, "arrayv.gravity", "Gravity Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "gravity-sorts", "gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvInPlaceLsdRadix, "arrayv.in_place_lsd_radix", "In-place LSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvInPlaceLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "inplace_lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvIndex, "arrayv.index", "Index Sort", "O(n)", "O(1)", false, true, false, true, &buildArrayvIndexRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "distribution-sorts", "index", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvLsdRadix, "arrayv.lsd_radix", "LSD Radix Sort (base 4)", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildArrayvLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvMsdRadix, "arrayv.msd_radix", "MSD Radix Sort", "O(d(n + b))", "O(n + b)", true, false, false, true, &buildArrayvMsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "msd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvPigeonhole, "arrayv.pigeonhole", "Pigeonhole Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvPigeonholeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "pigeonhole", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvRotateLsdRadix, "arrayv.rotate_lsd_radix", "Rotate LSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvRotateLsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "rotate_lsd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvRotateMsdRadix, "arrayv.rotate_msd_radix", "Rotate MSD Radix Sort", "O(d·n²)", "O(1)", false, true, false, true, &buildArrayvRotateMsdRadixRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "integer-radix", "rotate_msd_radix", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvShatter, "arrayv.shatter", "Shatter Sort", "O(n log n)", "O(n + √n)", true, false, false, true, &buildArrayvShatterRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "shatter", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvSimpleShatter, "arrayv.simple_shatter", "Simple Shatter Sort", "O(w·n)", "O(1)", false, true, false, true, &buildArrayvSimpleShatterRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer distribution sort", "distribution-sorts", "simple_shatter", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvSimplisticGravity, "arrayv.simplistic_gravity", "Simplistic Gravity Sort", "O(n·k)", "O(k)", true, false, false, true, &buildArrayvSimplisticGravityRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "gravity-sorts", "simplistic_gravity", "Gravity Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStatic, "arrayv.static", "Static Sort", "O(n + k)", "O(n + k)", true, false, false, true, &buildArrayvStaticRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer distribution sort", "distribution-sorts", "static", "Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvTime, "arrayv.time", "Time Sort (deterministic simulation)", "O(n log n)", "O(n)", true, false, false, true, &buildArrayvTimeRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("maximum_deadline", "Maximum deadline") }, 128,
			"deterministic timing demonstration", "novelty-distribution", "time", "Novelty Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessAmericanFlag, "arrayv.stackless_american_flag", "Stackless American Flag Sort", "O(w(n + R))", "O(R)", false, true, false, true, &buildArrayvStacklessAmericanFlagRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{ AlgorithmMetricDescriptor("digit_passes", "Digit passes") }, MaxItemCount,
			"general integer permutation sort", "integer-radix", "stackless_american_flag", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation }),
		AlgorithmDescriptor(SortAlgorithm::ArrayvStacklessBinaryQuick, "arrayv.stackless_binary_quick", "Stackless Binary Quick Sort", "O(w·n)", "O(1)", false, true, false, true, &buildArrayvStacklessBinaryQuickRequest,
			std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general integer permutation sort", "integer-radix", "stackless_binary_quick", "Integer Distribution Sorts", std::vector<SemanticRole>(), false,
			std::vector<InputDomain>{ InputDomain::NonnegativeIntegerKeys, InputDomain::SignedIntegerKeys, InputDomain::IntegerPermutation })
		};
		return algorithms;
	}

	const AlgorithmDescriptor* algorithmDescriptor(SortAlgorithm algorithm)
	{
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i) if (catalog[i].id == algorithm) return &catalog[i];
		return nullptr;
	}

	const AlgorithmDescriptor* algorithmDescriptorById(const FamilyId& family, const VariantId& variant)
	{
		const std::vector<AlgorithmDescriptor>& catalog = algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			if (catalog[i].familyId == family && catalog[i].variantId == variant) return &catalog[i];
		// Settings v4 originally stored each algorithm ID as its family and "standard" as its variant.
		// Keep those pairs readable while normalizing loaded settings to the current grouped IDs.
		if (variant == "standard")
			for (std::size_t i = 0; i < catalog.size(); ++i)
				if (family == catalog[i].stableId) return &catalog[i];
		return nullptr;
	}

	bool algorithmAcceptsItemCount(SortAlgorithm algorithm, std::size_t itemCount)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor && itemCount <= descriptor->safeMaxItemCount;
	}

	const char* algorithmDisplayName(SortAlgorithm algorithm)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor ? descriptor->displayName : "Unknown Algorithm";
	}

	bool algorithmSupports(SortAlgorithm algorithm, InputProfile profile)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		return descriptor && (profile == InputProfile::NormalizedFloat
			? descriptor->supportsNormalizedFloat : descriptor->supportsIntegerKeys);
	}

	bool algorithmSupports(SortAlgorithm algorithm, InputDomain domain)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (!descriptor) return false;
		if (!descriptor->supportedDomains.empty())
			return std::find(descriptor->supportedDomains.begin(), descriptor->supportedDomains.end(), domain) != descriptor->supportedDomains.end();
		return algorithmSupports(algorithm, inputProfileForDomain(domain));
	}

	const char* inputDomainName(InputDomain domain)
	{
		switch (domain)
		{
		case InputDomain::NormalizedFloats: return "Normalized floats";
		case InputDomain::NonnegativeIntegerKeys: return "Nonnegative integer keys";
		case InputDomain::SignedIntegerKeys: return "Signed integer keys";
		case InputDomain::IntegerPermutation: return "Unique integer permutation";
		}
		return "Normalized floats";
	}

	const char* inputPatternName(InputPattern pattern)
	{
		switch (pattern)
		{
		case InputPattern::Random: return "Random";
		case InputPattern::Sorted: return "Sorted";
		case InputPattern::Reversed: return "Reversed";
		case InputPattern::NearlySorted: return "Nearly sorted";
		case InputPattern::FewUnique: return "Few unique";
		case InputPattern::OrganPipe: return "Organ pipe";
		case InputPattern::AndrutReference: return "Andrut reference (100 bars)";
		}
		return "Random";
	}

	bool inputPatternCompatible(InputDomain domain, InputPattern pattern)
	{
		if (pattern == InputPattern::AndrutReference)
			return domain == InputDomain::NormalizedFloats;
		if (domain == InputDomain::IntegerPermutation &&
			(pattern == InputPattern::FewUnique || pattern == InputPattern::OrganPipe)) return false;
		return static_cast<int>(domain) >= static_cast<int>(InputDomain::NormalizedFloats) &&
			static_cast<int>(domain) <= static_cast<int>(InputDomain::IntegerPermutation) &&
			static_cast<int>(pattern) >= static_cast<int>(InputPattern::Random) &&
			static_cast<int>(pattern) <= static_cast<int>(InputPattern::AndrutReference);
	}

	InputProfile inputProfileForDomain(InputDomain domain)
	{
		return domain == InputDomain::NormalizedFloats ? InputProfile::NormalizedFloat : InputProfile::IntegerKeys;
	}

	const char* shellGapSequenceName(ShellGapSequence sequence)
	{
		switch (sequence)
		{
		case ShellGapSequence::Halving: return "Halving";
		case ShellGapSequence::Knuth: return "Knuth";
		case ShellGapSequence::Ciura: return "Ciura";
		case ShellGapSequence::Tokuda: return "Tokuda";
		}
		return "Halving";
	}

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
		domain(InputDomain::NormalizedFloats), pattern(InputPattern::Random), shellGapSequence(ShellGapSequence::Halving),
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

	SortTrace SortTraceBuilder::build(const std::vector<SortItem>& input, SortDirection direction, std::uint32_t inputSeed)
	{
		TraceBuildRequest request;
		request.input = input;
		request.direction = direction;
		request.inputSeed = inputSeed;
		request.algorithm = SortAlgorithm::Insertion;
		request.profile = input.empty() || !input[0].hasIntegerKey ? InputProfile::NormalizedFloat : InputProfile::IntegerKeys;
		request.domain = request.profile == InputProfile::NormalizedFloat ? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys;
		return build(request);
	}

	SortTrace SortTraceBuilder::build(const TraceBuildRequest& request)
	{
		if (request.mergeWritebackMode != MergeWritebackMode::AnimatedWrites &&
			request.mergeWritebackMode != MergeWritebackMode::AtomicRangeWrite)
			throw std::invalid_argument("The requested Merge Sort writeback mode is invalid.");
		if (request.mergeExecutionMode != MergeExecutionMode::BottomUp &&
			request.mergeExecutionMode != MergeExecutionMode::TopDown)
			throw std::invalid_argument("The requested Merge Sort execution mode is invalid.");
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(request.algorithm);
		if (!descriptor || !descriptor->buildRequest) throw std::invalid_argument("The requested sorting algorithm is not registered.");
		if ((!request.familyId.empty() || !request.variantId.empty()) &&
			(request.familyId.empty() || request.variantId.empty() ||
			 algorithmDescriptorById(request.familyId, request.variantId) != descriptor))
			throw std::invalid_argument("The requested family and variant IDs do not match the registered algorithm.");
		const InputDomain domain = effectiveDomain(request);
		if (!inputPatternCompatible(domain, request.pattern))
			throw std::invalid_argument("The selected input pattern is incompatible with this data domain.");
		if (request.input.size() > descriptor->safeMaxItemCount)
			throw std::invalid_argument((std::string(descriptor->displayName) + " is limited to " +
				std::to_string(descriptor->safeMaxItemCount) + " items for safe trace generation.").c_str());
		if (!algorithmSupports(request.algorithm, domain)) throw std::invalid_argument("The selected algorithm does not support this data domain.");
		std::set<std::uint64_t> identities;
		std::set<std::int32_t> uniqueKeys;
		for (std::size_t i = 0; i < request.input.size(); ++i)
		{
			const SortItem& item = request.input[i];
			if (!std::isfinite(item.value)) throw std::invalid_argument("Sorting input values must be finite floats.");
			if (!identities.insert(item.stableId).second) throw std::invalid_argument("Sorting input stable IDs must be unique.");
			if (domain == InputDomain::NormalizedFloats && item.hasIntegerKey) throw std::invalid_argument("Float domain cannot contain integer-tagged items.");
			if (domain != InputDomain::NormalizedFloats && !item.hasIntegerKey) throw std::invalid_argument("Integer domains require integer keys.");
			if (domain == InputDomain::NonnegativeIntegerKeys &&
				(item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= request.input.size()))
				throw std::invalid_argument("Nonnegative integer keys must lie in the range [0, N-1].");
			if (domain == InputDomain::SignedIntegerKeys &&
				(std::abs(static_cast<std::int64_t>(item.integerKey)) >= static_cast<std::int64_t>(request.input.size())))
				throw std::invalid_argument("Signed integer keys must lie in the range [-(N-1), N-1].");
			if (domain == InputDomain::IntegerPermutation)
			{
				if (item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= request.input.size())
					throw std::invalid_argument("Permutation keys must lie in the range [0, N-1].");
				if (!uniqueKeys.insert(item.integerKey).second)
					throw std::invalid_argument("Permutation domain requires each integer key exactly once.");
			}
		}
		if (domain == InputDomain::IntegerPermutation && uniqueKeys.size() != request.input.size())
			throw std::invalid_argument("Permutation domain requires a complete unique key range.");
		return descriptor->buildRequest(request);
	}

	std::uint32_t Simulation::createFreshSeed()
	{
		std::random_device source;
		const std::uint32_t a = static_cast<std::uint32_t>(source()), b = static_cast<std::uint32_t>(source());
		return (a << 16) ^ b ^ static_cast<std::uint32_t>(source());
	}

	Simulation::Simulation(std::size_t initialItemCount, std::uint32_t startupSeed, InputProfile profile)
		: preparedInput_(), currentState_(), trace_(), itemCount_(initialItemCount), cursor_(0), inputSeed_(startupSeed),
		durationSeconds_(10.0f), elapsedSeconds_(0.0f), direction_(SortDirection::Ascending), algorithm_(SortAlgorithm::Insertion),
		profile_(profile), domain_(profile == InputProfile::NormalizedFloat ? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys),
		pattern_(InputPattern::Random), shellGapSequence_(ShellGapSequence::Halving),
		mergeWritebackMode_(MergeWritebackMode::AtomicRangeWrite), mergeExecutionMode_(MergeExecutionMode::BottomUp),
		mergeBufferVisualizationEnabled_(true),
		completionScanEnabled_(false),
		baselineKind_(BaselineKind::Shuffle),
		buildingTrace_(false), playing_(false), status_()
	{
		if (itemCount_ < MinItemCount || itemCount_ > MaxItemCount)
			throw std::invalid_argument("Initial item count must be between " + std::to_string(MinItemCount) +
				" and " + std::to_string(MaxItemCount) + ".");
		prepareItems(BaselineKind::Shuffle, startupSeed);
		status_ = "Ready. Input shuffle seed " + std::to_string(inputSeed_) + ".";
	}

	Simulation::Simulation(std::size_t initialItemCount, std::uint32_t startupSeed, InputDomain domain, InputPattern pattern)
		: Simulation(initialItemCount, startupSeed, inputProfileForDomain(domain))
	{
		if (!inputPatternCompatible(domain, pattern)) throw std::invalid_argument("The selected input pattern is incompatible with this data domain.");
		domain_ = domain;
		profile_ = inputProfileForDomain(domain);
		pattern_ = pattern;
		if (pattern_ == InputPattern::AndrutReference) itemCount_ = 100;
		prepareItems(BaselineKind::Shuffle, startupSeed);
	}

	Simulation::~Simulation() = default;

	CommandResult Simulation::checkPreparationAllowed() const
	{
		if (buildingTrace_) return CommandResult(false, "Cannot prepare input or start a run while the trace is being built.");
		if (playing_) return CommandResult(false, "Pause or reset playback before changing the input or run settings.");
		return CommandResult(true, "Ready.");
	}

	void Simulation::prepareItems(BaselineKind kind, std::uint32_t seed)
	{
		baselineKind_ = kind;
		inputSeed_ = kind == BaselineKind::Shuffle ? seed : 0;
		std::vector<SortItem> items;
		items.reserve(itemCount_);
		std::mt19937 engine(seed);
		static const int andrutRanks[100] = {
			78,89,75,81,31,39,93,6,86,27,56,20,42,98,96,95,60,62,77,15,
			2,49,37,46,63,64,82,45,32,12,28,76,94,3,57,59,30,50,90,72,
			38,70,36,53,34,44,40,43,4,84,29,7,25,33,8,41,66,14,67,48,
			35,61,79,88,85,0,92,87,97,19,18,26,58,1,91,10,23,54,55,69,
			73,22,24,5,13,74,71,99,9,16,65,83,51,80,11,52,68,47,21,17
		};
		const bool andrutFixture = kind == BaselineKind::Shuffle && pattern_ == InputPattern::AndrutReference;
		const auto assignKey = [this](SortItem& item, int key)
		{
			item.integerKey = key;
			item.hasIntegerKey = true;
			const float denominator = static_cast<float>(std::max<std::size_t>(1, itemCount_));
			item.value = domain_ == InputDomain::SignedIntegerKeys
				? static_cast<float>(key + static_cast<int>(itemCount_)) / (2.0f * denominator)
				: static_cast<float>(key + 1) / denominator;
		};
		for (std::size_t i = 0; i < itemCount_; ++i)
		{
			const float normalized = static_cast<float>(i + 1) / static_cast<float>(itemCount_);
			if (andrutFixture)
				items.push_back(SortItem(static_cast<std::uint64_t>(i), static_cast<float>(andrutRanks[i]) / 99.0f));
			else if (domain_ == InputDomain::NormalizedFloats)
				items.push_back(SortItem(static_cast<std::uint64_t>(i), normalized));
			else
			{
				int key = static_cast<int>(i);
				if (domain_ == InputDomain::NonnegativeIntegerKeys)
				{
					std::uniform_int_distribution<int> choose(0, static_cast<int>(itemCount_ - 1));
					key = choose(engine);
				}
				else if (domain_ == InputDomain::SignedIntegerKeys)
				{
					const int extent = static_cast<int>(itemCount_ - 1);
					std::uniform_int_distribution<int> choose(-extent, extent);
					key = choose(engine);
				}
				SortItem item(static_cast<std::uint64_t>(i), key, normalized);
				assignKey(item, key);
				items.push_back(item);
			}
		}
		const auto sortByDirection = [&items](SortDirection sortDirection)
		{
			std::stable_sort(items.begin(), items.end(), [sortDirection](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, sortDirection) == ComparisonResult::Less; });
		};
		if (kind == BaselineKind::Shuffle)
		{
			switch (pattern_)
			{
			case InputPattern::Random:
				std::shuffle(items.begin(), items.end(), engine);
				break;
			case InputPattern::Sorted:
				sortByDirection(SortDirection::Ascending);
				break;
			case InputPattern::Reversed:
				sortByDirection(SortDirection::Descending);
				break;
			case InputPattern::NearlySorted:
				sortByDirection(SortDirection::Ascending);
				if (items.size() > 1)
				{
					const std::size_t swaps = std::max<std::size_t>(1, items.size() / 20);
					std::uniform_int_distribution<std::size_t> choose(0, items.size() - 1);
					for (std::size_t i = 0; i < swaps; ++i) std::swap(items[choose(engine)], items[choose(engine)]);
				}
				break;
			case InputPattern::FewUnique:
			{
				const int uniqueCount = static_cast<int>(std::max<std::size_t>(1, std::min<std::size_t>(5, itemCount_)));
				std::uniform_int_distribution<int> choose(0, uniqueCount - 1);
				for (std::size_t i = 0; i < items.size(); ++i)
				{
					const int selected = choose(engine);
					if (domain_ == InputDomain::NormalizedFloats)
						items[i].value = static_cast<float>(selected + 1) / static_cast<float>(uniqueCount);
					else if (domain_ == InputDomain::SignedIntegerKeys)
						assignKey(items[i], selected - (uniqueCount - 1) / 2);
					else assignKey(items[i], selected);
				}
				std::shuffle(items.begin(), items.end(), engine);
				break;
			}
			case InputPattern::OrganPipe:
				for (std::size_t i = 0; i < items.size(); ++i)
				{
					const std::size_t rank = i <= items.size() / 2 ? i : items.size() - 1 - i;
					if (domain_ == InputDomain::NormalizedFloats)
						items[i].value = static_cast<float>(rank + 1) / static_cast<float>(itemCount_);
					else if (domain_ == InputDomain::SignedIntegerKeys)
						assignKey(items[i], static_cast<int>(rank) - static_cast<int>(itemCount_ / 2));
					else assignKey(items[i], static_cast<int>(rank));
				}
				break;
			case InputPattern::AndrutReference:
				// Installed from the fixture above; retain its exact left-to-right order.
				break;
			}
		}
		else sortByDirection(kind == BaselineKind::Descending ? SortDirection::Descending : SortDirection::Ascending);
		preparedInput_.swap(items);
		replaceTraceWithBaseline();
	}

	void Simulation::replaceTraceWithBaseline()
	{
		trace_.reset(); currentState_ = makeInitialState(preparedInput_); cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
	}

	CommandResult Simulation::prepareShuffle(std::uint32_t seed)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Shuffle, seed); status_ = "Prepared shuffled input with seed " + std::to_string(inputSeed_) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::prepareInstaSort()
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Ascending, 0); status_ = "Prepared the ascending baseline. Reset returns to this input."; return CommandResult(true, status_);
	}
	CommandResult Simulation::prepareInstaInvert()
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Descending, 0); status_ = "Prepared the reverse-sorted baseline. Reset returns to this input."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setItemCount(std::size_t count)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (count < MinItemCount || count > MaxItemCount)
		{
			status_ = "Bar count must be between " + std::to_string(MinItemCount) + " and " + std::to_string(MaxItemCount) + ".";
			return CommandResult(false, status_);
		}
		if (count == itemCount_) { status_ = "Bar count is already " + std::to_string(itemCount_) + "."; return CommandResult(true, status_); }
		itemCount_ = count;
		const bool leftAndrutPreset = pattern_ == InputPattern::AndrutReference && itemCount_ != 100;
		if (leftAndrutPreset) pattern_ = InputPattern::Random;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? createFreshSeed() : 0);
		status_ = leftAndrutPreset
			? "Bar count set to " + std::to_string(itemCount_) + "; input arrangement returned to Random because the Andrut reference contains exactly 100 bars."
			: "Bar count set to " + std::to_string(itemCount_) + "; prepared baseline refreshed.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setDirection(SortDirection direction)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (direction_ == direction) { status_ = "Direction is unchanged."; return CommandResult(true, status_); }
		direction_ = direction; replaceTraceWithBaseline(); status_ = "Direction changed; build a new trace."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setDuration(float seconds)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (!std::isfinite(seconds) || seconds < MinRunDurationSeconds || seconds > MaxRunDurationSeconds) { status_ = "Run duration must be between 1 and 120 seconds."; return CommandResult(false, status_); }
		durationSeconds_ = seconds;
		updateElapsedFromCursor();
		std::ostringstream formattedDuration;
		formattedDuration << std::setprecision(6) << seconds;
		status_ = "Run duration set to " + formattedDuration.str() + " seconds.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::selectAlgorithm(SortAlgorithm algorithm)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (!descriptor) { status_ = "The selected algorithm is not registered."; return CommandResult(false, status_); }
		InputDomain nextDomain = domain_;
		if (!algorithmSupports(algorithm, nextDomain))
		{
			if (!descriptor->supportedDomains.empty()) nextDomain = descriptor->supportedDomains.front();
			else if (descriptor->supportsNormalizedFloat) nextDomain = InputDomain::NormalizedFloats;
			else if (descriptor->supportsIntegerKeys) nextDomain = InputDomain::NonnegativeIntegerKeys;
			else { status_ = "The selected algorithm does not support an available input domain."; return CommandResult(false, status_); }
		}
		const bool domainChanged = nextDomain != domain_;
		const bool patternChanged = !inputPatternCompatible(nextDomain, pattern_);
		const bool algorithmChanged = algorithm_ != algorithm;
		if (!domainChanged && !patternChanged && !algorithmChanged)
		{
			status_ = "Algorithm selection is unchanged.";
			return CommandResult(true, status_);
		}
		domain_ = nextDomain;
		profile_ = inputProfileForDomain(nextDomain);
		if (patternChanged) pattern_ = InputPattern::Random;
		algorithm_ = algorithm;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = std::string("Algorithm set to ") + algorithmDisplayName(algorithm) + ".";
		if (domainChanged) status_ += std::string(" Input domain changed to ") + inputDomainName(domain_) + ".";
		if (patternChanged) status_ += " Incompatible input pattern changed to Random.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setInputProfile(InputProfile profile)
	{
		if (profile != InputProfile::NormalizedFloat && profile != InputProfile::IntegerKeys)
			return CommandResult(false, "Invalid legacy input profile.");
		return setInputDomain(profile == InputProfile::NormalizedFloat
			? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys);
	}

	CommandResult Simulation::setInputDomain(InputDomain domain)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (static_cast<int>(domain) < static_cast<int>(InputDomain::NormalizedFloats) ||
			static_cast<int>(domain) > static_cast<int>(InputDomain::IntegerPermutation))
			return CommandResult(false, "Invalid input domain.");
		if (!inputPatternCompatible(domain, pattern_))
			return CommandResult(false, std::string(inputPatternName(pattern_)) + " is unavailable for " + inputDomainName(domain) + ".");
		if (domain_ == domain) return CommandResult(true, "Input domain is unchanged.");
		domain_ = domain;
		profile_ = inputProfileForDomain(domain);
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = canRun() ? std::string("Input domain set to ") + inputDomainName(domain) + "; regenerated from the current seed."
			: std::string("Input domain set to ") + inputDomainName(domain) + "; selected algorithm does not support this domain.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setInputPattern(InputPattern pattern)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (pattern == InputPattern::AndrutReference)
		{
			if (!algorithmSupports(algorithm_, InputDomain::NormalizedFloats))
			{
				status_ = std::string(algorithmDisplayName(algorithm_)) + " does not support the Andrut reference input domain.";
				return CommandResult(false, status_);
			}
			domain_ = InputDomain::NormalizedFloats;
			profile_ = InputProfile::NormalizedFloat;
			itemCount_ = 100;
		}
		if (!inputPatternCompatible(domain_, pattern))
			return CommandResult(false, std::string(inputPatternName(pattern)) + " is unavailable for " + inputDomainName(domain_) + ".");
		if (pattern_ == pattern && (pattern != InputPattern::AndrutReference || itemCount_ == 100))
			return CommandResult(true, "Input pattern is unchanged.");
		pattern_ = pattern;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = pattern == InputPattern::AndrutReference
			? "Andrut's 100-bar reference arrangement loaded; item count set to 100."
			: std::string("Input pattern set to ") + inputPatternName(pattern) + "; regenerated from the current seed.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setShellGapSequence(ShellGapSequence sequence)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (sequence < ShellGapSequence::Halving || sequence > ShellGapSequence::Tokuda) return CommandResult(false, "Invalid Shell gap sequence.");
		if (shellGapSequence_ == sequence) return CommandResult(true, "Shell gap sequence is unchanged.");
		shellGapSequence_ = sequence; replaceTraceWithBaseline(); status_ = std::string("Shell gap sequence set to ") + shellGapSequenceName(sequence) + "."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeWritebackMode(MergeWritebackMode mode)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mode != MergeWritebackMode::AnimatedWrites && mode != MergeWritebackMode::AtomicRangeWrite)
			return CommandResult(false, "Invalid Merge Sort writeback mode.");
		if (mergeWritebackMode_ == mode) return CommandResult(true, "Merge Sort writeback mode is unchanged.");
		mergeWritebackMode_ = mode;
		replaceTraceWithBaseline();
		status_ = std::string("Merge Sort writeback set to ") +
			(mode == MergeWritebackMode::AtomicRangeWrite ? "comparison phase then atomic range write." : "animated item writes.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeExecutionMode(MergeExecutionMode mode)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mode != MergeExecutionMode::BottomUp && mode != MergeExecutionMode::TopDown)
			return CommandResult(false, "Invalid Merge Sort execution mode.");
		if (mergeExecutionMode_ == mode) return CommandResult(true, "Merge Sort execution mode is unchanged.");
		mergeExecutionMode_ = mode;
		replaceTraceWithBaseline();
		status_ = std::string("Merge Sort execution set to ") +
			(mode == MergeExecutionMode::BottomUp ? "bottom-up." : "top-down.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setCompletionScanEnabled(bool enabled)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (completionScanEnabled_ == enabled) return CommandResult(true, "Completion scan setting is unchanged.");
		completionScanEnabled_ = enabled;
		replaceTraceWithBaseline();
		status_ = std::string("Completion scan ") + (enabled ? "enabled." : "disabled.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeBufferVisualizationEnabled(bool enabled)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mergeBufferVisualizationEnabled_ == enabled)
			return CommandResult(true, "Merge buffer visualization setting is unchanged.");
		mergeBufferVisualizationEnabled_ = enabled;
		replaceTraceWithBaseline();
		status_ = std::string("Merge buffer visualization ") + (enabled ? "enabled." : "disabled.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::beginTraceBuild(TraceBuildRequest& request)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (!algorithmSupports(algorithm_, domain_)) { status_ = std::string(algorithmDisplayName(algorithm_)) + " does not support " + inputDomainName(domain_) + "."; return CommandResult(false, status_); }
		if (!inputPatternCompatible(domain_, pattern_)) { status_ = std::string(inputPatternName(pattern_)) + " is incompatible with " + inputDomainName(domain_) + "."; return CommandResult(false, status_); }
		if (!algorithmAcceptsItemCount(algorithm_, itemCount_))
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm_);
			status_ = std::string(algorithmDisplayName(algorithm_)) + " has a safe limit of " +
				std::to_string(descriptor ? descriptor->safeMaxItemCount : 0) + " items. Reduce the item count to run it.";
			return CommandResult(false, status_);
		}
		request.input = preparedInput_; request.algorithm = algorithm_; request.direction = direction_; request.inputSeed = inputSeed_;
		const AlgorithmDescriptor* selected = algorithmDescriptor(algorithm_);
		request.familyId = selected ? selected->familyId : FamilyId();
		request.variantId = selected ? selected->variantId : VariantId();
		request.profile = profile_; request.domain = domain_; request.pattern = pattern_; request.shellGapSequence = shellGapSequence_;
		request.mergeWritebackMode = mergeWritebackMode_;
		request.mergeExecutionMode = mergeExecutionMode_;
		request.completionScanEnabled = completionScanEnabled_;
		request.mergeBufferVisualizationEnabled = mergeBufferVisualizationEnabled_;
		trace_.reset(); currentState_ = makeInitialState(request.input); cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
		buildingTrace_ = true; status_ = "Building deterministic trace from a copy of the prepared input..."; return CommandResult(true, status_);
	}

	void Simulation::completeTraceBuild(TraceBuildResult completed)
	{
		if (!buildingTrace_) return;
		buildingTrace_ = false;
		if (!completed.succeeded) { status_ = "Trace build failed: " + (completed.error.empty() ? "Unknown error." : completed.error); return; }
		trace_ = std::make_shared<SortTrace>(std::move(completed.trace)); currentState_ = trace_->initialState;
		cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
		status_ = trace_->complete
			? "Trace ready: " + std::to_string(trace_->events.size()) + " events. Press Play."
			: "Partial trace ready: " + std::to_string(trace_->operationCount) + " operations. " + trace_->completionReason;
	}

	CommandResult Simulation::togglePlayback()
	{
		if (buildingTrace_) { status_ = "Trace is still being built; playback will be available when it is published."; return CommandResult(false, status_); }
		if (!trace_) { status_ = canRun() ? "Build a trace with Run before starting playback." : "Selected algorithm is incompatible with the current profile."; return CommandResult(false, status_); }
		if (playing_) { playing_ = false; status_ = "Playback paused."; return CommandResult(true, status_); }
		if (cursor_ >= trace_->events.size()) seekInternal(0);
		playing_ = true; status_ = "Playback running."; return CommandResult(true, status_);
	}
	CommandResult Simulation::stopPlayback() { playing_ = false; status_ = "Playback paused."; return CommandResult(true, status_); }
	CommandResult Simulation::reset()
	{
		playing_ = false; cursor_ = 0; elapsedSeconds_ = 0.0f;
		currentState_ = trace_ ? trace_->initialState : makeInitialState(preparedInput_);
		status_ = buildingTrace_ ? "Reset to the prepared baseline; trace generation continues." : "Reset to the prepared baseline at event zero.";
		return CommandResult(true, status_);
	}
	CommandResult Simulation::previousEvent()
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; stepping starts when ready." : "No trace is ready to step backward."; return CommandResult(false, status_); }
		playing_ = false; if (cursor_ > 0) seekInternal(cursor_ - 1); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::nextEvent()
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; stepping starts when ready." : "No trace is ready to step forward."; return CommandResult(false, status_); }
		playing_ = false; if (cursor_ < trace_->events.size()) seekInternal(cursor_ + 1); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::seekTo(std::size_t eventCursor)
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; seeking starts when ready." : "Build a trace before seeking."; return CommandResult(false, status_); }
		playing_ = false; seekInternal(eventCursor); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::seekToFraction(float fraction)
	{
		if (!std::isfinite(fraction)) { status_ = "Timeline position must be finite."; return CommandResult(false, status_); }
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; seeking starts when ready." : "Build a trace before using the timeline."; return CommandResult(false, status_); }
		fraction = std::max(0.0f, std::min(1.0f, fraction));
		const std::size_t target = fraction >= 1.0f ? trace_->events.size() : static_cast<std::size_t>(fraction * static_cast<double>(trace_->events.size()));
		playing_ = false; seekInternal(target); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	void Simulation::advance(float deltaSeconds)
	{
		if (!playing_ || !trace_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
		elapsedSeconds_ = std::min(durationSeconds_, elapsedSeconds_ + deltaSeconds);
		const float elapsed = elapsedSeconds_;
		const std::size_t count = trace_->events.size();
		const std::size_t target = elapsed >= durationSeconds_ || count == 0 ? count : static_cast<std::size_t>((static_cast<double>(elapsed) / durationSeconds_) * count);
		seekInternal(target); elapsedSeconds_ = elapsed;
		if (elapsed >= durationSeconds_)
		{
			playing_ = false; elapsedSeconds_ = durationSeconds_;
			status_ = trace_->complete ? "Playback complete." : "Partial playback complete. " + trace_->completionReason;
		}
	}

	void Simulation::synchronizePlaybackPosition(std::size_t eventCursor, double elapsedSeconds,
		double playbackDurationSeconds, bool playing)
	{
		if (!trace_ || buildingTrace_ || !std::isfinite(elapsedSeconds) ||
			!std::isfinite(playbackDurationSeconds) || playbackDurationSeconds < 0.0)
			return;
		seekInternal((std::min)(eventCursor, trace_->events.size()));
		const double duration = playbackDurationSeconds;
		elapsedSeconds_ = static_cast<float>((std::max)(0.0, (std::min)(duration, elapsedSeconds)));
		if (duration == 0.0 || elapsedSeconds >= duration)
		{
			seekInternal(trace_->events.size());
			elapsedSeconds_ = static_cast<float>(duration);
			playing_ = false;
			status_ = trace_->complete ? "Playback complete." : "Partial playback complete. " + trace_->completionReason;
		}
		else
			playing_ = playing;
	}

	void Simulation::seekInternal(std::size_t targetCursor)
	{
		if (!trace_) return;
		const std::size_t target = std::min(targetCursor, trace_->events.size());
		if (target != cursor_)
		{
			const std::size_t distance = target > cursor_ ? target - cursor_ : cursor_ - target;
			if (distance <= 256)
			{
				while (cursor_ > target) applyEventBackward(currentState_, trace_->events[--cursor_]);
				while (cursor_ < target) applyEventForward(currentState_, trace_->events[cursor_++]);
			}
			else
			{
				std::map<std::size_t, SortState>::const_iterator snapshot = trace_->snapshots.upper_bound(target);
				if (snapshot != trace_->snapshots.begin()) --snapshot;
				if (snapshot != trace_->snapshots.end()) { currentState_ = snapshot->second; cursor_ = snapshot->first; }
				else { currentState_ = trace_->initialState; cursor_ = 0; }
				while (cursor_ < target) applyEventForward(currentState_, trace_->events[cursor_++]);
			}
		}
		else cursor_ = target;
		updateElapsedFromCursor();
	}

	void Simulation::updateElapsedFromCursor()
	{
		if (!trace_ || trace_->events.empty()) elapsedSeconds_ = 0.0f;
		else elapsedSeconds_ = durationSeconds_ * (static_cast<float>(cursor_) / static_cast<float>(trace_->events.size()));
	}

	std::vector<ActiveRole> Simulation::rolesAtCursor() const
	{
		std::vector<ActiveRole> roles;
		if (!trace_ || cursor_ == 0) return roles;
		const SemanticEvent& event = trace_->events[cursor_ - 1];
		if (event.type == SemanticEventType::Compare)
		{
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex, event.firstLane));
			roles.push_back(ActiveRole(event.secondRole, event.secondItemId, event.secondIndex, event.secondLane));
		}
		else if (event.type == SemanticEventType::Probe)
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex, event.firstLane));
		else if (event.type == SemanticEventType::HoldKey)
		{
			roles.push_back(ActiveRole(SemanticRole::HeldKey, event.item.stableId, event.firstIndex));
			roles.push_back(ActiveRole(SemanticRole::Gap, NoItemId, event.firstIndex));
		}
		else if (event.type == SemanticEventType::ShiftItem)
		{
			roles.push_back(ActiveRole(SemanticRole::ShiftedItem, event.item.stableId, event.secondIndex));
			roles.push_back(ActiveRole(SemanticRole::Gap, NoItemId, event.firstIndex));
			roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.item.stableId, event.secondIndex));
		}
		else if (event.type == SemanticEventType::PlaceKey || event.type == SemanticEventType::WriteItem)
			roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.item.stableId, event.firstIndex, event.firstLane));
		else if (event.type == SemanticEventType::WriteRange)
			for (std::size_t i = 0; i < event.afterSlots.size(); ++i)
				if (event.afterSlots[i].occupied)
					roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.afterSlots[i].item.stableId, event.firstIndex + i, event.firstLane));
		else if (event.type == SemanticEventType::Swap)
		{
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex));
			roles.push_back(ActiveRole(event.secondRole, event.secondItemId, event.secondIndex));
		}
		else if (event.type == SemanticEventType::AuxiliaryItemWrite)
			roles.push_back(ActiveRole(SemanticRole::AuxiliaryCopy, event.item.stableId, event.firstIndex, event.workspaceName));
		else if (event.type == SemanticEventType::DiscardItem)
			roles.push_back(ActiveRole(SemanticRole::DiscardedItem, event.item.stableId, event.firstIndex, "discard"));
		return roles;
	}

	RenderSnapshot Simulation::snapshot() const
	{
		RenderSnapshot result;
		result.state = currentState_; result.metrics = currentState_.metrics; result.activeRoles = rolesAtCursor();
		result.direction = direction_; result.algorithm = algorithm_; result.profile = profile_; result.domain = domain_; result.pattern = pattern_;
		const AlgorithmDescriptor* selected = algorithmDescriptor(algorithm_);
		result.familyId = selected ? selected->familyId : FamilyId();
		result.variantId = selected ? selected->variantId : VariantId();
		result.shellGapSequence = shellGapSequence_; result.mergeWritebackMode = mergeWritebackMode_;
		result.mergeExecutionMode = mergeExecutionMode_;
		result.mergeBufferVisualizationEnabled = mergeBufferVisualizationEnabled_;
		result.cursor = cursor_; result.eventCount = trace_ ? trace_->events.size() : 0; result.itemCount = itemCount_; result.inputSeed = inputSeed_;
		result.audioEligibleEventCount = trace_ ? trace_->audioEventIndices.size() : 0;
		result.durationSeconds = durationSeconds_; result.elapsedSeconds = elapsedSeconds_; result.hasTrace = static_cast<bool>(trace_);
		result.buildingTrace = buildingTrace_; result.playing = playing_; result.complete = trace_ && cursor_ >= trace_->events.size();
		result.partial = trace_ && !trace_->complete;
		if (trace_) result.completionReason = trace_->completionReason;
		result.runCompatible = canRun();
		if (!inputPatternCompatible(domain_, pattern_))
			result.compatibilityMessage = std::string(inputPatternName(pattern_)) + " is incompatible with " + inputDomainName(domain_) + ".";
		else if (algorithmSupports(algorithm_, domain_) && !algorithmAcceptsItemCount(algorithm_, itemCount_))
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm_);
			result.compatibilityMessage = std::string(algorithmDisplayName(algorithm_)) + " is limited to " +
				std::to_string(descriptor ? descriptor->safeMaxItemCount : 0) + " items.";
		}
		else if (!result.runCompatible) result.compatibilityMessage = std::string(algorithmDisplayName(algorithm_)) + " does not support " + inputDomainName(domain_) + ".";
		result.status = status_;
		if (trace_)
			for (std::size_t i = 0; i < trace_->annotations.size(); ++i)
				if (trace_->annotations[i].beginCursor <= cursor_ && cursor_ < trace_->annotations[i].endCursor)
					result.activeRegions.push_back(trace_->annotations[i]);
		return result;
	}

	std::shared_ptr<const SortTrace> Simulation::currentTrace() const { return trace_; }
}
