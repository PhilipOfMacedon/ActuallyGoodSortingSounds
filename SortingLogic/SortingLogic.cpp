#include "SortingLogic.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
			return a.stableId == b.stableId && a.value == b.value && a.integerKey == b.integerKey && a.hasIntegerKey == b.hasIntegerKey;
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
				trace.inputSeed = request.inputSeed;
				trace.input = request.input;
				trace.initialState = state;
				trace.snapshots.insert(std::make_pair(static_cast<std::size_t>(0), state));
			}

			SortTrace trace;
			SortState state;
			std::size_t maxOperations;
			bool completionScanEnabled;
			int nextGroupId;

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
				 event.sequence = trace.events.size();
				if (event.type == SemanticEventType::Compare) trace.comparisonEventIndices.push_back(event.sequence);
				else if (event.type == SemanticEventType::Probe) trace.probeEventIndices.push_back(event.sequence);
				trace.events.push_back(event);
				applyEventForward(state, trace.events.back());
				if (event.type != SemanticEventType::Complete) ++trace.operationCount;
				const std::size_t cursor = trace.events.size();
				if (cursor % SnapshotInterval == 0) trace.snapshots[cursor] = state;
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

			void probe(std::size_t index, SemanticRole role = SemanticRole::ProbedItem, const std::string& lane = "main")
			{
				const SortItem item = at(index);
				SemanticEvent event;
				event.type = SemanticEventType::Probe;
				event.firstIndex = index;
				event.firstLane = lane;
				event.firstItemId = item.stableId;
				event.item = item;
				event.firstRole = role;
				append(event);
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
				event.beforeCounter = static_cast<int>(old);
				event.afterCounter = static_cast<int>(value);
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
				trace.snapshots[trace.events.size()] = state;
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
			r.resizeCounters("digits", 10);
			r.resizeAuxiliary("radix_output", n);
			r.probe(0, SemanticRole::ProcessedItem);
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 1; i < n; ++i)
			{
				r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey);
				maximum = std::max(maximum, r.at(i).integerKey);
			}
			const int transformedMaximum = maximum - minimum;
			int place = 1;
			do
			{
				std::size_t bucketBegin[10];
				for (std::size_t d = 0; d < 10; ++d) bucketBegin[d] = NoGapIndex;
				for (std::size_t d = 0; d < 10; ++d) r.writeCounter("digits", d, 0);
				const auto transformed = [&r, minimum, maximum](const SortItem& item)
					{ return r.trace.direction == SortDirection::Ascending ? item.integerKey - minimum : maximum - item.integerKey; };
				for (std::size_t i = 0; i < n; ++i)
				{
					r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = static_cast<std::size_t>((transformed(r.at(i)) / place) % 10);
					if (bucketBegin[digit] == NoGapIndex) bucketBegin[digit] = r.trace.events.size();
					r.writeCounter("digits", digit, r.counter("digits", digit) + 1);
				}
				for (std::size_t d = 1; d < 10; ++d)
					r.writeCounter("digits", d, r.counter("digits", d) + r.counter("digits", d - 1));
				for (std::size_t i = n; i > 0; --i)
				{
					r.probe(i - 1, SemanticRole::ProcessedItem);
					const SortItem item = r.at(i - 1);
					const std::size_t digit = static_cast<std::size_t>((transformed(item) / place) % 10);
					const int target = r.counter("digits", digit) - 1;
					r.writeAuxiliary("radix_output", static_cast<std::size_t>(target), item);
					r.writeCounter("digits", digit, target);
				}
				for (std::size_t i = 0; i < n; ++i) r.writeMain(i, r.auxiliaryAt("radix_output", i));
				r.addMetric("digit_passes");
				for (std::size_t d = 0; d < 10; ++d)
					if (bucketBegin[d] != NoGapIndex)
						r.annotate("digits", RegionKind::Bucket, d, d + 1, 1, static_cast<int>(d),
							"Digit " + std::to_string(d), bucketBegin[d]);
				if (transformedMaximum / place < 10) break;
				place *= 10;
			} while (place > 0);
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
			Recorder recorder(request);
			try
			{
				switch (algorithm)
				{
				case SortAlgorithm::Insertion: insertion(recorder); break;
				case SortAlgorithm::Bubble: bubble(recorder); break;
				case SortAlgorithm::Selection: selection(recorder); break;
				case SortAlgorithm::Shell: shell(recorder); break;
				case SortAlgorithm::Merge: mergeSort(recorder); break;
				case SortAlgorithm::Quick: quickSort(recorder); break;
				case SortAlgorithm::Heap: heapSort(recorder); break;
				case SortAlgorithm::Counting: counting(recorder); break;
			case SortAlgorithm::Radix: radix(recorder); break;
			case SortAlgorithm::Gnome: gnome(recorder); break;
			case SortAlgorithm::Cocktail: cocktail(recorder); break;
			case SortAlgorithm::Comb: comb(recorder); break;
			case SortAlgorithm::OddEven: oddEven(recorder); break;
			case SortAlgorithm::Pancake: pancake(recorder); break;
			case SortAlgorithm::Stooge: stooge(recorder); break;
			case SortAlgorithm::Bogo: bogo(recorder); break;
				default: throw std::invalid_argument("The requested sorting algorithm is not registered.");
				}
				recorder.finish();
			}
			catch (const OperationLimitReached&)
			{
				recorder.finishPartial("Global trace operation budget reached (" + std::to_string(request.maxOperations) + " operations).");
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
		SORT_REQUEST_BUILDER(buildShellRequest, Shell)
		SORT_REQUEST_BUILDER(buildMergeRequest, Merge)
		SORT_REQUEST_BUILDER(buildQuickRequest, Quick)
		SORT_REQUEST_BUILDER(buildHeapRequest, Heap)
		SORT_REQUEST_BUILDER(buildCountingRequest, Counting)
		SORT_REQUEST_BUILDER(buildRadixRequest, Radix)
		SORT_REQUEST_BUILDER(buildGnomeRequest, Gnome)
		SORT_REQUEST_BUILDER(buildCocktailRequest, Cocktail)
		SORT_REQUEST_BUILDER(buildCombRequest, Comb)
		SORT_REQUEST_BUILDER(buildOddEvenRequest, OddEven)
		SORT_REQUEST_BUILDER(buildPancakeRequest, Pancake)
		SORT_REQUEST_BUILDER(buildStoogeRequest, Stooge)
		SORT_REQUEST_BUILDER(buildBogoRequest, Bogo)
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
			AlgorithmDescriptor(SortAlgorithm::Merge, "merge", "Merge Sort", "O(n log n)", "O(n)", true, false, true, true, &buildMergeRequest,
				std::vector<AlgorithmOptionDescriptor>{
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::BottomUp), "bottom_up", "Bottom-up", "execution_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::TopDown), "top_down", "Top-down", "execution_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AnimatedWrites), "animated_writes", "Animated item writes", "writeback_mode"),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AtomicRangeWrite), "compare_then_write_range", "Compare, then write range", "writeback_mode")
				}, std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("merge_passes", "Merge passes")
				}, MaxItemCount, "general permutation sort", "merge-sort", "merge", "Merge Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Quick, "quick", "Quick Sort", "average O(n log n), worst O(n²)", "O(log n) average, O(n) worst", false, true, true, true, &buildQuickRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>{
					AlgorithmMetricDescriptor("partitions", "Partitions"),
					AlgorithmMetricDescriptor("maximum_depth", "Maximum depth")
				}, MaxItemCount, "general permutation sort", "partition-sorts", "quick", "Partition Sorts",
				std::vector<SemanticRole>{ SemanticRole::Pivot }),
			AlgorithmDescriptor(SortAlgorithm::Heap, "heap", "Heap Sort", "O(n log n)", "O(1)", false, true, true, true, &buildHeapRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), MaxItemCount, "general permutation sort",
				"heap-sorts", "heap", "Heap Sorts"),
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
			AlgorithmDescriptor(SortAlgorithm::Stooge, "stooge", "Stooge Sort", "O(n^2.71)", "O(n)", false, true, true, true, &buildStoogeRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 16, "novelty demonstration",
				"recursive-novelty", "stooge", "Recursive Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::Bogo, "bogo", "Bogo Sort", "O(n·n!) expected", "O(1)", false, true, true, true, &buildBogoRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 8, "stochastic novelty demonstration",
				"stochastic-novelty", "bogo", "Stochastic Novelty Sorts")
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
		firstItemId(NoItemId), secondItemId(NoItemId), item(), beforeItem(), beforeSecondItem(), beforeSlots(), afterSlots(), comparison(ComparisonResult::Equal),
		firstLane("main"), secondLane("main"), workspaceName(), firstRole(SemanticRole::ComparedItem),
		secondRole(SemanticRole::ComparedItem), completionScan(false), beforeOccupied(false), beforeSecondOccupied(false),
		beforeCounter(0), afterCounter(0), beforeSize(0), afterSize(0), metricName(), metricDelta(0), groupId(-1)
	{
	}

	SortTrace::SortTrace()
		: algorithm(SortAlgorithm::Insertion), familyId("insertion"), variantId("standard"),
		direction(SortDirection::Ascending), profile(InputProfile::NormalizedFloat),
		domain(InputDomain::NormalizedFloats), pattern(InputPattern::Random), shellGapSequence(ShellGapSequence::Halving),
		mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite), mergeExecutionMode(MergeExecutionMode::BottomUp),
		mergeBufferVisualizationEnabled(true),
		inputSeed(0), input(), initialState(), events(),
		comparisonEventIndices(), probeEventIndices(), audioEventIndices(), annotations(), snapshots(), operationCount(0), complete(true), completionReason()
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
		case SemanticEventType::Swap:
			require(event.firstIndex < state.slots.size() && event.secondIndex < state.slots.size(), "Swap index is out of range.");
			std::swap(state.slots[event.firstIndex], state.slots[event.secondIndex]); ++state.metrics.swaps;
			break;
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
			if (event.afterSize == 0) state.integerWorkspaces.erase(event.workspaceName);
			else state.integerWorkspaces[event.workspaceName].resize(event.afterSize, 0);
			state.metrics.peakAuxiliaryCapacity = std::max(state.metrics.peakAuxiliaryCapacity, totalAuxiliaryCapacity(state));
			break;
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
		case SemanticEventType::HoldKey:
			require(state.hasHeldItem && event.firstIndex < state.slots.size(), "Invalid reverse hold.");
			state.slots[event.firstIndex] = SortSlot(state.heldItem); clearHeldItem(state); break;
		case SemanticEventType::ShiftItem:
			require(state.hasHeldItem && event.firstIndex < state.slots.size() && event.secondIndex < state.slots.size(), "Invalid reverse shift.");
			state.slots[event.firstIndex] = state.slots[event.secondIndex]; state.slots[event.secondIndex].occupied = false; state.gapIndex = event.secondIndex; break;
		case SemanticEventType::PlaceKey:
			require(!state.hasHeldItem && event.firstIndex < state.slots.size() && state.slots[event.firstIndex].occupied, "Invalid reverse place.");
			state.heldItem = state.slots[event.firstIndex].item; state.slots[event.firstIndex].occupied = false; state.hasHeldItem = true; state.gapIndex = event.firstIndex; break;
		case SemanticEventType::Swap:
			std::swap(state.slots[event.firstIndex], state.slots[event.secondIndex]); if (state.metrics.swaps) --state.metrics.swaps; break;
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
				if (event.beforeCounter == 0) state.metrics.algorithm.erase(event.metricName);
				else state.metrics.algorithm[event.metricName] = static_cast<std::uint64_t>(event.beforeCounter);
			}
			else { state.integerWorkspaces[event.workspaceName][event.firstIndex] = event.beforeCounter; if (state.metrics.auxiliaryCounterUpdates) --state.metrics.auxiliaryCounterUpdates; }
			break;
		case SemanticEventType::ResizeAuxiliary:
			if (event.beforeSize == 0) state.auxiliaryItems.erase(event.workspaceName);
			else state.auxiliaryItems[event.workspaceName].resize(event.beforeSize);
			state.metrics.peakAuxiliaryCapacity = static_cast<std::size_t>(event.metricDelta); break;
		case SemanticEventType::ResizeCounterWorkspace:
			if (event.beforeSize == 0) state.integerWorkspaces.erase(event.workspaceName);
			else state.integerWorkspaces[event.workspaceName].resize(event.beforeSize);
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
		durationSeconds_ = seconds; updateElapsedFromCursor(); status_ = "Run duration set to " + std::to_string(static_cast<int>(seconds)) + " seconds."; return CommandResult(true, status_);
	}

	CommandResult Simulation::selectAlgorithm(SortAlgorithm algorithm)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (!algorithmDescriptor(algorithm)) { status_ = "The selected algorithm is not registered."; return CommandResult(false, status_); }
		if (!algorithmSupports(algorithm, domain_)) { status_ = "The selected algorithm does not support the current data domain."; return CommandResult(false, status_); }
		if (algorithm_ == algorithm) { status_ = "Algorithm selection is unchanged."; return CommandResult(true, status_); }
		algorithm_ = algorithm; replaceTraceWithBaseline(); status_ = std::string("Algorithm set to ") + algorithmDisplayName(algorithm) + "."; return CommandResult(true, status_);
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
