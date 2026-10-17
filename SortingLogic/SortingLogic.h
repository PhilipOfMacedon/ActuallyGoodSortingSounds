#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sortinglogic
{
	static const std::size_t MinItemCount = 2;
	static const std::size_t MaxItemCount = 1024;
	static const std::size_t DefaultTraceOperationLimit = 1000000;
	static const float MinRunDurationSeconds = 1.0f;
	static const float MaxRunDurationSeconds = 120.0f;
	static const std::size_t SnapshotInterval = 4096;
	static const std::size_t NoGapIndex = static_cast<std::size_t>(-1);
	static const std::uint64_t NoItemId = static_cast<std::uint64_t>(-1);
	typedef std::string FamilyId;
	typedef std::string VariantId;

	enum class InputProfile { NormalizedFloat, IntegerKeys };
	enum class InputDomain { NormalizedFloats, NonnegativeIntegerKeys, SignedIntegerKeys, IntegerPermutation };
	// Persisted numeric values are stable; append new patterns without renumbering.
	enum class InputPattern { Random, Sorted, Reversed, NearlySorted, FewUnique, OrganPipe, AndrutReference = 6 };
	enum class SortDirection { Ascending, Descending };
	enum class ShellGapSequence { Halving, Knuth, Ciura, Tokuda };
	enum class MergeWritebackMode { AnimatedWrites, AtomicRangeWrite };
	enum class MergeExecutionMode { BottomUp, TopDown };

	// Values stay normalized for bar height. Integer profile items also carry their
	// exact key, so duplicate keys remain distinct logical items.
	struct SortItem
	{
		std::uint64_t stableId;
		float value;
		std::int32_t integerKey;
		bool hasIntegerKey;
		SortItem() : stableId(0), value(0.0f), integerKey(0), hasIntegerKey(false) {}
		SortItem(std::uint64_t id, float itemValue) : stableId(id), value(itemValue), integerKey(0), hasIntegerKey(false) {}
		SortItem(std::uint64_t id, std::int32_t key, float normalizedValue)
			: stableId(id), value(normalizedValue), integerKey(key), hasIntegerKey(true) {}
	};

	// Numeric values are stable IDs persisted in settings. Do not renumber them.
	enum class SortAlgorithm : int
	{
		Insertion = 1001, Bubble = 1002, Selection = 1003, Shell = 1004,
		Merge = 1005, Quick = 1006, Heap = 1007, Counting = 1008,
		Radix = 1009, Gnome = 1010, Cocktail = 1011, Comb = 1012,
		OddEven = 1013, Pancake = 1014, Stooge = 1015, Bogo = 1016
	};

	enum class SemanticEventType
	{
		Compare, Probe, HoldKey, ShiftItem, PlaceKey, Swap, WriteItem, DiscardItem,
		WriteRange, AuxiliaryItemWrite, CounterWrite, ResizeAuxiliary, ResizeCounterWorkspace,
		PhaseBegin, PhaseEnd, Complete
	};
	enum class ComparisonResult : int { Less = -1, Equal = 0, Greater = 1 };
	enum class SemanticRole
	{
		ComparedItem, HeldKey, Gap, ShiftedItem, WriteTarget, Pivot,
		CurrentMinimum, CurrentMaximum, AuxiliaryCopy, ProcessedItem, ProbedItem, DiscardedItem
	};
	enum class RegionKind { Contiguous, Strided, Bucket, Group };

	struct SortSlot
	{
		bool occupied;
		SortItem item;
		SortSlot() : occupied(false), item() {}
		explicit SortSlot(const SortItem& value) : occupied(true), item(value) {}
	};

	struct SortMetrics
	{
		std::uint64_t comparisons, swaps, primaryWrites, auxiliaryItemWrites, auxiliaryCounterUpdates, discards;
		std::size_t peakAuxiliaryCapacity;
		std::map<std::string, std::uint64_t> algorithm;
		SortMetrics() : comparisons(0), swaps(0), primaryWrites(0), auxiliaryItemWrites(0),
			auxiliaryCounterUpdates(0), discards(0), peakAuxiliaryCapacity(0), algorithm() {}
	};

	struct SortState
	{
		std::vector<SortSlot> slots;
		std::vector<SortItem> discardedItems;
		bool hasHeldItem;
		SortItem heldItem;
		std::size_t gapIndex;
		std::map<std::string, std::vector<SortSlot> > auxiliaryItems;
		std::map<std::string, std::vector<int> > integerWorkspaces;
		SortMetrics metrics;
		SortState() : discardedItems(), hasHeldItem(false), heldItem(), gapIndex(NoGapIndex), auxiliaryItems(), integerWorkspaces(), metrics() {}
	};

	struct SemanticEvent
	{
		std::size_t sequence;
		SemanticEventType type;
		std::size_t firstIndex, secondIndex;
		std::uint64_t firstItemId, secondItemId;
		SortItem item, beforeItem, beforeSecondItem;
		std::vector<SortSlot> beforeSlots, afterSlots;
		ComparisonResult comparison;
		std::string firstLane, secondLane, workspaceName;
		SemanticRole firstRole, secondRole;
		bool completionScan, beforeOccupied, beforeSecondOccupied;
		int beforeCounter, afterCounter;
		std::size_t beforeSize, afterSize;
		std::string metricName;
		std::uint64_t metricDelta;
		int groupId;
		SemanticEvent();
	};

	struct RegionAnnotation
	{
		std::string lane, label;
		RegionKind kind;
		std::size_t beginCursor, endCursor, first, last, stride;
		int group;
		RegionAnnotation() : lane("main"), label(), kind(RegionKind::Contiguous), beginCursor(0), endCursor(0),
			first(0), last(0), stride(1), group(-1) {}
	};

	struct SortTrace;
	struct TraceBuildRequest;
	typedef SortTrace (*AlgorithmTraceRequestBuilder)(const TraceBuildRequest&);

	struct AlgorithmOptionDescriptor
	{
		int value;
		const char* groupId;
		const char* stableId;
		const char* displayName;
		AlgorithmOptionDescriptor(int optionValue, const char* optionId, const char* name, const char* optionGroup = "primary")
			: value(optionValue), groupId(optionGroup), stableId(optionId), displayName(name) {}
	};

	struct AlgorithmMetricDescriptor
	{
		const char* key;
		const char* displayName;
		AlgorithmMetricDescriptor(const char* metricKey, const char* name) : key(metricKey), displayName(name) {}
	};

	struct AlgorithmDescriptor
	{
		SortAlgorithm id;
		const char* stableId;
		FamilyId familyId;
		VariantId variantId;
		const char* familyDisplayName;
		const char* displayName;
		const char* timeComplexity;
		const char* spaceComplexity;
		bool stable;
		bool inPlace;
		bool supportsNormalizedFloat;
		bool supportsIntegerKeys;
		std::size_t safeMaxItemCount;
		const char* behavior;
		AlgorithmTraceRequestBuilder buildRequest;
		std::vector<AlgorithmOptionDescriptor> supportedOptions;
		std::vector<AlgorithmMetricDescriptor> metrics;
		std::vector<SemanticRole> comparisonAudioRoles;
		bool reverseAudioOperandOrder;
		AlgorithmDescriptor(SortAlgorithm valueId, const char* key, const char* name, const char* time,
			const char* space, bool isStable, bool isInPlace, bool floats, bool integers,
			AlgorithmTraceRequestBuilder requestBuilder,
			const std::vector<AlgorithmOptionDescriptor>& options = std::vector<AlgorithmOptionDescriptor>(),
			const std::vector<AlgorithmMetricDescriptor>& applicableMetrics = std::vector<AlgorithmMetricDescriptor>(),
			std::size_t maximumItems = MaxItemCount, const char* behaviorLabel = "general permutation sort",
			const char* familyKey = nullptr, const char* methodKey = nullptr, const char* familyName = nullptr,
			const std::vector<SemanticRole>& audioRoles = std::vector<SemanticRole>(), bool reverseAudioOperands = false)
			: id(valueId), stableId(key), familyId(familyKey ? familyKey : key), variantId(methodKey ? methodKey : key),
			familyDisplayName(familyName ? familyName : name), displayName(name), timeComplexity(time), spaceComplexity(space), stable(isStable),
			inPlace(isInPlace), supportsNormalizedFloat(floats), supportsIntegerKeys(integers), safeMaxItemCount(maximumItems),
			behavior(behaviorLabel), buildRequest(requestBuilder),
			supportedOptions(options), metrics(applicableMetrics), comparisonAudioRoles(audioRoles), reverseAudioOperandOrder(reverseAudioOperands) {}
	};

	const std::vector<AlgorithmDescriptor>& algorithmCatalog();
	const AlgorithmDescriptor* algorithmDescriptor(SortAlgorithm algorithm);
	const AlgorithmDescriptor* algorithmDescriptorById(const FamilyId& family, const VariantId& variant);
	bool algorithmAcceptsItemCount(SortAlgorithm algorithm, std::size_t itemCount);
	const char* algorithmDisplayName(SortAlgorithm algorithm);
	bool algorithmSupports(SortAlgorithm algorithm, InputProfile profile);
	bool algorithmSupports(SortAlgorithm algorithm, InputDomain domain);
	const char* inputDomainName(InputDomain domain);
	const char* inputPatternName(InputPattern pattern);
	bool inputPatternCompatible(InputDomain domain, InputPattern pattern);
	InputProfile inputProfileForDomain(InputDomain domain);
	const char* shellGapSequenceName(ShellGapSequence sequence);

	struct SortTrace
	{
		SortAlgorithm algorithm;
		FamilyId familyId;
		VariantId variantId;
		SortDirection direction;
		InputProfile profile;
		InputDomain domain;
		InputPattern pattern;
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		bool mergeBufferVisualizationEnabled;
		std::uint32_t inputSeed;
		std::vector<SortItem> input;
		SortState initialState;
		std::vector<SemanticEvent> events;
		std::vector<std::size_t> comparisonEventIndices;
		std::vector<std::size_t> probeEventIndices;
		std::vector<std::size_t> audioEventIndices;
		std::vector<RegionAnnotation> annotations;
		std::map<std::size_t, SortState> snapshots;
		std::size_t operationCount;
		bool complete;
		std::string completionReason;
		SortTrace();
	};

	struct TraceBuildRequest
	{
		std::vector<SortItem> input;
		FamilyId familyId;
		VariantId variantId;
		SortAlgorithm algorithm;
		SortDirection direction;
		InputProfile profile;
		InputDomain domain;
		InputPattern pattern;
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		bool completionScanEnabled;
		bool mergeBufferVisualizationEnabled;
		std::uint32_t inputSeed;
		std::size_t maxOperations;
		TraceBuildRequest() : input(), familyId(), variantId(), algorithm(SortAlgorithm::Insertion), direction(SortDirection::Ascending),
			profile(InputProfile::NormalizedFloat), domain(InputDomain::NormalizedFloats), pattern(InputPattern::Random),
			shellGapSequence(ShellGapSequence::Halving), mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite),
			mergeExecutionMode(MergeExecutionMode::BottomUp), completionScanEnabled(false),
			mergeBufferVisualizationEnabled(true), inputSeed(0),
			maxOperations(DefaultTraceOperationLimit) {}
	};

	enum class TraceBuildStatus { Complete, Partial, Failed };

	struct TraceBuildResult
	{
		bool succeeded;
		TraceBuildStatus status;
		SortTrace trace;
		std::string error;
		TraceBuildResult() : succeeded(false), status(TraceBuildStatus::Failed), trace(), error() {}
	};

	struct ActiveRole
	{
		SemanticRole role;
		std::uint64_t itemId;
		std::size_t slotIndex;
		std::string lane;
		ActiveRole(SemanticRole valueRole, std::uint64_t valueId, std::size_t valueIndex, const std::string& valueLane = "main")
			: role(valueRole), itemId(valueId), slotIndex(valueIndex), lane(valueLane) {}
	};

	struct RenderSnapshot
	{
		SortState state;
		std::vector<ActiveRole> activeRoles;
		std::vector<RegionAnnotation> activeRegions;
		SortMetrics metrics;
		SortDirection direction;
		SortAlgorithm algorithm;
		FamilyId familyId;
		VariantId variantId;
		InputProfile profile;
		InputDomain domain;
		InputPattern pattern;
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		bool mergeBufferVisualizationEnabled;
		std::size_t cursor, eventCount, itemCount, audioEligibleEventCount;
		std::uint32_t inputSeed;
		float durationSeconds, elapsedSeconds;
		bool hasTrace, buildingTrace, playing, complete, partial, runCompatible;
		std::string compatibilityMessage, completionReason, status;
		RenderSnapshot();
	};

	struct CommandResult
	{
		bool accepted;
		std::string message;
		CommandResult(bool wasAccepted, const std::string& text) : accepted(wasAccepted), message(text) {}
	};

	SortState makeInitialState(const std::vector<SortItem>& items);
	ComparisonResult compareItems(const SortItem& left, const SortItem& right, SortDirection direction);
	void applyEventForward(SortState& state, const SemanticEvent& event);
	void applyEventBackward(SortState& state, const SemanticEvent& event);
	bool validateState(const SortState& state, std::size_t expectedItemCount, std::string* reason = nullptr);
	std::vector<std::size_t> shellGaps(std::size_t count, ShellGapSequence sequence);

	class SortTraceBuilder
	{
	public:
		static SortTrace build(const TraceBuildRequest& request);
		static SortTrace build(const std::vector<SortItem>& input, SortDirection direction, std::uint32_t inputSeed = 0);
	};

	class Simulation
	{
	public:
		explicit Simulation(std::size_t initialItemCount = 80, std::uint32_t startupSeed = 0,
			InputProfile profile = InputProfile::NormalizedFloat);
		Simulation(std::size_t initialItemCount, std::uint32_t startupSeed, InputDomain domain, InputPattern pattern = InputPattern::Random);
		~Simulation();
		Simulation(const Simulation&) = delete;
		Simulation& operator=(const Simulation&) = delete;
		CommandResult prepareShuffle(std::uint32_t seed);
		CommandResult prepareInstaSort();
		CommandResult prepareInstaInvert();
		CommandResult setItemCount(std::size_t count);
		CommandResult setDirection(SortDirection direction);
		CommandResult setDuration(float seconds);
		CommandResult selectAlgorithm(SortAlgorithm algorithm);
		CommandResult setInputProfile(InputProfile profile);
		CommandResult setInputDomain(InputDomain domain);
		CommandResult setInputPattern(InputPattern pattern);
		CommandResult setShellGapSequence(ShellGapSequence sequence);
		CommandResult setMergeWritebackMode(MergeWritebackMode mode);
		CommandResult setMergeExecutionMode(MergeExecutionMode mode);
		CommandResult setMergeBufferVisualizationEnabled(bool enabled);
		CommandResult setCompletionScanEnabled(bool enabled);
		CommandResult beginTraceBuild(TraceBuildRequest& request);
		void completeTraceBuild(TraceBuildResult result);
		CommandResult togglePlayback();
		CommandResult stopPlayback();
		CommandResult reset();
		CommandResult previousEvent();
		CommandResult nextEvent();
		CommandResult seekTo(std::size_t eventCursor);
		CommandResult seekToFraction(float fraction);
		void advance(float deltaSeconds);
		void synchronizePlaybackPosition(std::size_t eventCursor, double elapsedSeconds,
			double playbackDurationSeconds, bool playing);
		RenderSnapshot snapshot() const;
		std::shared_ptr<const SortTrace> currentTrace() const;
		const std::vector<SortItem>& preparedInput() const { return preparedInput_; }
		std::size_t itemCount() const { return itemCount_; }
		float durationSeconds() const { return durationSeconds_; }
		SortDirection direction() const { return direction_; }
		SortAlgorithm algorithm() const { return algorithm_; }
		InputProfile profile() const { return profile_; }
		InputDomain domain() const { return domain_; }
		InputPattern pattern() const { return pattern_; }
		ShellGapSequence shellGapSequence() const { return shellGapSequence_; }
		MergeWritebackMode mergeWritebackMode() const { return mergeWritebackMode_; }
		MergeExecutionMode mergeExecutionMode() const { return mergeExecutionMode_; }
		bool mergeBufferVisualizationEnabled() const { return mergeBufferVisualizationEnabled_; }
		bool completionScanEnabled() const { return completionScanEnabled_; }
		bool isBuildingTrace() const { return buildingTrace_; }
		bool isPlaying() const { return playing_; }
		bool canRun() const { return algorithmSupports(algorithm_, domain_) && algorithmAcceptsItemCount(algorithm_, itemCount_) &&
			inputPatternCompatible(domain_, pattern_); }
		static std::uint32_t createFreshSeed();
	private:
		enum class BaselineKind { Shuffle, Ascending, Descending };
		std::vector<SortItem> preparedInput_;
		SortState currentState_;
		std::shared_ptr<const SortTrace> trace_;
		std::size_t itemCount_, cursor_;
		std::uint32_t inputSeed_;
		float durationSeconds_, elapsedSeconds_;
		SortDirection direction_;
		SortAlgorithm algorithm_;
		InputProfile profile_;
		InputDomain domain_;
		InputPattern pattern_;
		ShellGapSequence shellGapSequence_;
		MergeWritebackMode mergeWritebackMode_;
		MergeExecutionMode mergeExecutionMode_;
		bool mergeBufferVisualizationEnabled_;
		bool completionScanEnabled_;
		BaselineKind baselineKind_;
		bool buildingTrace_, playing_;
		std::string status_;
		CommandResult checkPreparationAllowed() const;
		void prepareItems(BaselineKind kind, std::uint32_t seed);
		void replaceTraceWithBaseline();
		void seekInternal(std::size_t cursor);
		void updateElapsedFromCursor();
		std::vector<ActiveRole> rolesAtCursor() const;
	};
}
