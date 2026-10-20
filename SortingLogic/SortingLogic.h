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
	static const std::size_t DefaultTraceWorkLimit = 8000000;
	static const std::size_t DefaultTraceByteLimit = 128u * 1024u * 1024u;
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
	enum class GrailBufferMode { InPlace, StaticBuffer, DynamicBuffer };

	// Values stay normalized for bar height. Integer profile items also carry their
	// exact key, so duplicate keys remain distinct logical items.
	struct SortItem
	{
		std::uint64_t stableId;
		float value;
		std::int32_t integerKey;
		bool hasIntegerKey;
		bool orientationReversed;
		SortItem() : stableId(0), value(0.0f), integerKey(0), hasIntegerKey(false), orientationReversed(false) {}
		SortItem(std::uint64_t id, float itemValue) : stableId(id), value(itemValue), integerKey(0), hasIntegerKey(false), orientationReversed(false) {}
		SortItem(std::uint64_t id, std::int32_t key, float normalizedValue)
			: stableId(id), value(normalizedValue), integerKey(key), hasIntegerKey(true), orientationReversed(false) {}
	};

	// Numeric values are stable IDs persisted in settings. Do not renumber them.
	enum class SortAlgorithm : int
	{
		Insertion = 1001, Bubble = 1002, Selection = 1003, Shell = 1004,
		Merge = 1005, Quick = 1006, Heap = 1007, Counting = 1008,
		Radix = 1009, Gnome = 1010, Cocktail = 1011, Comb = 1012,
		OddEven = 1013, Pancake = 1014, Stooge = 1015, Bogo = 1016,
		ArrayvAmericanFlag = 1017, ArrayvBinaryQuickIterative = 1018,
		ArrayvBinaryQuickRecursive = 1019, ArrayvClassicGravity = 1020,
		ArrayvFlash = 1021, ArrayvGravity = 1022, ArrayvInPlaceLsdRadix = 1023,
		ArrayvIndex = 1024, ArrayvLsdRadix = 1025, ArrayvMsdRadix = 1026,
		ArrayvPigeonhole = 1027, ArrayvRotateLsdRadix = 1028,
		ArrayvRotateMsdRadix = 1029, ArrayvShatter = 1030,
		ArrayvSimpleShatter = 1031, ArrayvSimplisticGravity = 1032,
		ArrayvStatic = 1033, ArrayvTime = 1034, ArrayvStacklessAmericanFlag = 1035,
		ArrayvStacklessBinaryQuick = 1036, ArrayvOptimizedBubble = 1037,
		ArrayvUnoptimizedBubble = 1038, ArrayvBinaryGnome = 1039,
		ArrayvCircleIterative = 1040, ArrayvCircleRecursive = 1041,
		ArrayvClassicThreeSmoothComb = 1042, ArrayvOptimizedCocktail = 1043,
		ArrayvUnoptimizedCocktail = 1044, ArrayvOptimizedGnome = 1045,
		ArrayvShove = 1046, ArrayvSlope = 1047, ArrayvBinaryInsertion = 1048,
		ArrayvRecursiveShell = 1049, ArrayvBingo = 1050,
		ArrayvDoubleSelection = 1051, ArrayvStableSelection = 1052,
		ArrayvCycle = 1053, ArrayvTernaryHeap = 1054,
		ArrayvBaseNHeap = 1055, ArrayvMinHeap = 1056,
		ArrayvFlippedMinHeap = 1057, ArrayvBottomUpHeap = 1058,
		ArrayvBinomialHeap = 1059, ArrayvLazyHeap = 1060,
		ArrayvMinMaxHeap = 1061, ArrayvOutOfPlaceHeap = 1062,
		ArrayvTriangularHeap = 1063, ArrayvWeakHeap = 1064,
		ArrayvPoplarHeap = 1065, ArrayvBurntPancake = 1066,
		CanonicalBurntPancake = 1067, ArrayvPancakeInsertion = 1068,
		ArrayvSwaplessBubble = 1069, ArrayvThreeSmoothCombRecursive = 1070,
		ArrayvThreeSmoothCombParallel = 1071,
		ArrayvBitonicIterative = 1072, ArrayvBitonicRecursive = 1073,
		ArrayvBitonicParallel = 1074, ArrayvBoseNelsonIterative = 1075,
		ArrayvBoseNelsonRecursive = 1076, ArrayvBoseNelsonParallel = 1077,
		ArrayvOddEvenMergeIterative = 1078, ArrayvOddEvenMergeRecursive = 1079,
		ArrayvOddEvenMergeParallel = 1080, ArrayvDiamondIterative = 1081,
		ArrayvDiamondRecursive = 1082, ArrayvPairwiseIterative = 1083,
		ArrayvPairwiseRecursive = 1084, ArrayvCrease = 1085, ArrayvFold = 1086,
		ArrayvMergeExchangeIterative = 1087, ArrayvPairwiseMergeIterative = 1088,
		ArrayvPairwiseMergeRecursive = 1089, ArrayvWeaveIterative = 1090,
		ArrayvWeaveParallel = 1091, ArrayvWeaveRecursive = 1092,
		ArrayvMatrix = 1093, ArrayvOptimizedStooge = 1094,
		ArrayvOptimizedStoogeStudio = 1095, ArrayvQuadStooge = 1096,
		ArrayvSlow = 1097, ArrayvThreeSmoothCombIterative = 1098,
		ArrayvCircloid = 1099, ArrayvSilly = 1100, ArrayvSnuffle = 1101,
		ArrayvCompleteGraph = 1102, ArrayvFun = 1103,
		ArrayvBozo = 1104, ArrayvBubbleBogo = 1105, ArrayvExchangeBogo = 1106,
		ArrayvDeterministicBogo = 1107, ArrayvSelectionBogo = 1108, ArrayvLessBogo = 1109,
		ArrayvLLQuick = 1110, ArrayvLRQuick = 1111, ArrayvLRQuickParallel = 1112,
		ArrayvCocktailBogo = 1113, ArrayvMedianQuickBogo = 1114, ArrayvQuickBogo = 1115,
		ArrayvRandomGuess = 1116, ArrayvOptimizedGuess = 1117,
		ArrayvSmartGuess = 1118, ArrayvGuess = 1119,
		ArrayvBogoBogo = 1120, ArrayvMergeBogo = 1121, ArrayvSmartBogoBogo = 1122,
		ArrayvDualPivotQuick = 1123, ArrayvOptimizedDualPivotQuick = 1124,
		ArrayvStableQuick = 1125, ArrayvStableQuickParallel = 1126, ArrayvForcedStableQuick = 1127,
		ArrayvBinaryMerge = 1128, ArrayvOptimizedBottomUpMerge = 1129,
		ArrayvTernaryLLQuick = 1130, ArrayvTernaryLRQuick = 1131,
		ArrayvStablePermutation = 1132, ArrayvTable = 1133,
		ArrayvHybridComb = 1134, ArrayvIntroCircleIterative = 1135, ArrayvIntroCircleRecursive = 1136,
		ArrayvDoubleInsertion = 1137, ArrayvBinaryDoubleInsertion = 1138, ArrayvIntroSort = 1139,
		ArrayvPatience = 1140, ArrayvShellParallel = 1141, ArrayvMergeInsertion = 1142,
		ArrayvStalinOverwrite = 1143, CanonicalStalinFilter = 1144,
		ArrayvClassicTree = 1145, ArrayvTree = 1146, ArrayvStrand = 1147,
		ArrayvAATree = 1148, ArrayvAVLTree = 1149, ArrayvSplayTree = 1150,
		ArrayvRedBlackTree = 1151, ArrayvIterativeTopDownMerge = 1152,
		ArrayvInPlaceMerge = 1153, ArrayvParallelMerge = 1154,
		ArrayvRotateMerge = 1155, ArrayvRotateMergeParallel = 1156,
		ArrayvStacklessRotateMerge = 1157, ArrayvBlockSwapMerge = 1158,
		ArrayvWeavedMerge = 1159, ArrayvTwinMerge = 1160,
		ArrayvTim = 1161, ArrayvCocktailMerge = 1162,
		ArrayvImprovedInPlaceMerge = 1163, ArrayvBlockInsertion = 1164,
		ArrayvBufferedStooge = 1165, ArrayvBadSort = 1166,
		ArrayvAsynchronous = 1167, ArrayvPDMerge = 1168,
		ArrayvStableCycle = 1169, ArrayvClassicTournament = 1170,
		ArrayvTournament = 1171, ArrayvSmoothSort = 1172,
		ArrayvBinomialSmoothSort = 1173, ArrayvImprovedBlockSelection = 1174,
		ArrayvStacklessHybridQuick = 1175, ArrayvStacklessDualPivotQuick = 1176,
		ArrayvHanoi = 1177, ArrayvLibrary = 1178, ArrayvSimplifiedLibrary = 1179,
		ArrayvWeaveMerge = 1180, ArrayvLaziest = 1181, ArrayvLazierest = 1182,
		ArrayvLazyStable = 1183, ArrayvOptimizedLazyStable = 1184,
		ArrayvFifthMerge = 1185, ArrayvQuadMerge = 1186,
		ArrayvBufferPartitionMerge = 1187, ArrayvParallelBlockMerge = 1188,
		ArrayvOptimizedRotateMerge = 1189, ArrayvGrail = 1190,
		ArrayvAdaptiveGrail = 1191, ArrayvPDQBranched = 1192,
		ArrayvPDQBranchless = 1193, ArrayvSqrt = 1194,
		ArrayvSynchronousSqrt = 1195, ArrayvEcta = 1196,
		ArrayvCircularGrail = 1197, ArrayvUnstableGrail = 1198,
		ArrayvDropMerge = 1199, ArrayvParallelGrail = 1200, ArrayvMedianMerge = 1201,
		ArrayvRemi = 1202, ArrayvChalice = 1203, ArrayvFlan = 1204,
		ArrayvFlux = 1205, ArrayvKota = 1206, ArrayvOptimizedWeaveMerge = 1207,
		ArrayvWiki = 1208, ArrayvYujisBufferedMerge2 = 1209,
		ArrayvAndrey = 1210, ArrayvNewShuffleMerge = 1211
	};

	enum class SemanticEventType
	{
		Compare, Probe, HoldKey, ShiftItem, PlaceKey, Swap, WriteItem, DiscardItem,
		WriteRange, AuxiliaryItemWrite, CounterWrite, ResizeAuxiliary, ResizeCounterWorkspace,
		PhaseBegin, PhaseEnd, SetOrientation, ReplaceScalar, CompactRange, Complete, CarryWrite
	};
	enum class AlgorithmOutcome
	{
		SortedPermutation, SortedStablePermutation, SortedValueRewrite, SortedWithDiscards,
		IncompleteDemonstration, DemonstrationOnly
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
		std::vector<int> beforeCounters;
		ComparisonResult comparison;
		std::string firstLane, secondLane, workspaceName;
		SemanticRole firstRole, secondRole;
		bool completionScan, beforeOccupied, beforeSecondOccupied;
		bool beforeBoolean, afterBoolean;
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

	enum class AlgorithmOptionTarget
	{
		Unbound,
		ShellGapSequence,
		MergeExecutionMode,
		MergeWritebackMode,
		BucketCount,
		HeapArity,
		GrailBufferMode,
		RadixBase,
		TimeMagnitude,
		RotateMergeBufferSize
	};

	struct AlgorithmConfiguration
	{
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		GrailBufferMode grailBufferMode;
		int radixBase;
		int bucketCount;
		int heapArity;
		int timeMagnitude;
		int rotateMergeBufferSize;
		AlgorithmConfiguration()
			: shellGapSequence(ShellGapSequence::Halving), mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite),
			mergeExecutionMode(MergeExecutionMode::BottomUp), grailBufferMode(GrailBufferMode::InPlace),
			radixBase(0), bucketCount(0), heapArity(4), timeMagnitude(10), rotateMergeBufferSize(64) {}
		bool optionValue(AlgorithmOptionTarget target, int& value) const
		{
			switch (target)
			{
			case AlgorithmOptionTarget::ShellGapSequence: value = static_cast<int>(shellGapSequence); return true;
			case AlgorithmOptionTarget::MergeExecutionMode: value = static_cast<int>(mergeExecutionMode); return true;
			case AlgorithmOptionTarget::MergeWritebackMode: value = static_cast<int>(mergeWritebackMode); return true;
			case AlgorithmOptionTarget::BucketCount: value = bucketCount; return true;
			case AlgorithmOptionTarget::HeapArity: value = heapArity; return true;
			case AlgorithmOptionTarget::GrailBufferMode: value = static_cast<int>(grailBufferMode); return true;
			case AlgorithmOptionTarget::RadixBase: value = radixBase; return true;
			case AlgorithmOptionTarget::TimeMagnitude: value = timeMagnitude; return true;
			case AlgorithmOptionTarget::RotateMergeBufferSize: value = rotateMergeBufferSize; return true;
			case AlgorithmOptionTarget::Unbound: return false;
			}
			return false;
		}
		bool setOptionValue(AlgorithmOptionTarget target, int value)
		{
			switch (target)
			{
			case AlgorithmOptionTarget::ShellGapSequence: shellGapSequence = static_cast<ShellGapSequence>(value); return true;
			case AlgorithmOptionTarget::MergeExecutionMode: mergeExecutionMode = static_cast<MergeExecutionMode>(value); return true;
			case AlgorithmOptionTarget::MergeWritebackMode: mergeWritebackMode = static_cast<MergeWritebackMode>(value); return true;
			case AlgorithmOptionTarget::BucketCount: bucketCount = value; return true;
			case AlgorithmOptionTarget::HeapArity: heapArity = value; return true;
			case AlgorithmOptionTarget::GrailBufferMode: grailBufferMode = static_cast<GrailBufferMode>(value); return true;
			case AlgorithmOptionTarget::RadixBase: radixBase = value; return true;
			case AlgorithmOptionTarget::TimeMagnitude: timeMagnitude = value; return true;
			case AlgorithmOptionTarget::RotateMergeBufferSize: rotateMergeBufferSize = value; return true;
			case AlgorithmOptionTarget::Unbound: return false;
			}
			return false;
		}
	};

	struct AlgorithmOptionDescriptor
	{
		int value;
		const char* groupId;
		const char* stableId;
		const char* displayName;
		AlgorithmOptionTarget target;
		bool isDefault;
		AlgorithmOptionDescriptor(int optionValue, const char* optionId, const char* name, const char* optionGroup = "primary",
			AlgorithmOptionTarget requestTarget = AlgorithmOptionTarget::Unbound, bool defaultOption = false)
			: value(optionValue), groupId(optionGroup), stableId(optionId), displayName(name), target(requestTarget), isDefault(defaultOption) {}
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
		std::vector<InputDomain> supportedDomains;
		AlgorithmDescriptor(SortAlgorithm valueId, const char* key, const char* name, const char* time,
			const char* space, bool isStable, bool isInPlace, bool floats, bool integers,
			AlgorithmTraceRequestBuilder requestBuilder,
			const std::vector<AlgorithmOptionDescriptor>& options = std::vector<AlgorithmOptionDescriptor>(),
			const std::vector<AlgorithmMetricDescriptor>& applicableMetrics = std::vector<AlgorithmMetricDescriptor>(),
			std::size_t maximumItems = MaxItemCount, const char* behaviorLabel = "general permutation sort",
			const char* familyKey = nullptr, const char* methodKey = nullptr, const char* familyName = nullptr,
			const std::vector<SemanticRole>& audioRoles = std::vector<SemanticRole>(), bool reverseAudioOperands = false,
			const std::vector<InputDomain>& domains = std::vector<InputDomain>())
			: id(valueId), stableId(key), familyId(familyKey ? familyKey : key), variantId(methodKey ? methodKey : key),
			familyDisplayName(familyName ? familyName : name), displayName(name), timeComplexity(time), spaceComplexity(space), stable(isStable),
			inPlace(isInPlace), supportsNormalizedFloat(floats), supportsIntegerKeys(integers), safeMaxItemCount(maximumItems),
			behavior(behaviorLabel), buildRequest(requestBuilder),
			supportedOptions(options), metrics(applicableMetrics), comparisonAudioRoles(audioRoles), reverseAudioOperandOrder(reverseAudioOperands),
			supportedDomains(domains) {}
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
		AlgorithmConfiguration configuration;
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		bool mergeBufferVisualizationEnabled;
		int radixBase, bucketCount, timeMagnitude, rotateMergeBufferSize;
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
		std::size_t workSteps;
		std::size_t accountedBytes;
		bool complete;
		AlgorithmOutcome outcome;
		std::string completionReason;
		SortTrace();
	};

	struct TraceBuildRequest
	{
		std::vector<SortItem> input;
		// Stable catalog option IDs keyed by option group. When present, these are
		// resolved by SortingLogic and override matching explicit or legacy values.
		std::map<std::string, std::string> optionSelections;
		FamilyId familyId;
		VariantId variantId;
		SortAlgorithm algorithm;
		SortDirection direction;
		InputProfile profile;
		InputDomain domain;
		InputPattern pattern;
		AlgorithmConfiguration configuration;
		bool hasExplicitConfiguration;
		ShellGapSequence shellGapSequence;
		MergeWritebackMode mergeWritebackMode;
		MergeExecutionMode mergeExecutionMode;
		bool completionScanEnabled;
		bool mergeBufferVisualizationEnabled;
		std::uint32_t inputSeed;
		std::size_t maxOperations;
		std::size_t maxWorkSteps;
		std::size_t maxTraceBytes;
		int radixBase;
		int bucketCount;
		int timeMagnitude;
		int rotateMergeBufferSize;
		TraceBuildRequest() : input(), optionSelections(), familyId(), variantId(), algorithm(SortAlgorithm::Insertion), direction(SortDirection::Ascending),
			profile(InputProfile::NormalizedFloat), domain(InputDomain::NormalizedFloats), pattern(InputPattern::Random), configuration(),
			hasExplicitConfiguration(false),
			shellGapSequence(ShellGapSequence::Halving), mergeWritebackMode(MergeWritebackMode::AtomicRangeWrite),
			mergeExecutionMode(MergeExecutionMode::BottomUp), completionScanEnabled(false),
			mergeBufferVisualizationEnabled(true), inputSeed(0),
			maxOperations(DefaultTraceOperationLimit), maxWorkSteps(DefaultTraceWorkLimit),
			maxTraceBytes(DefaultTraceByteLimit), radixBase(0), bucketCount(0), timeMagnitude(10), rotateMergeBufferSize(64) {}
		void setConfiguration(const AlgorithmConfiguration& value)
		{
			configuration = value;
			hasExplicitConfiguration = true;
		}
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
		CommandResult setAlgorithmOption(const std::string& groupId, const std::string& stableId);
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
		std::size_t cursor() const { return cursor_; }
		SortDirection direction() const { return direction_; }
		SortAlgorithm algorithm() const { return algorithm_; }
		InputProfile profile() const { return profile_; }
		InputDomain domain() const { return domain_; }
		InputPattern pattern() const { return pattern_; }
		ShellGapSequence shellGapSequence() const { return algorithmConfiguration_.shellGapSequence; }
		MergeWritebackMode mergeWritebackMode() const { return algorithmConfiguration_.mergeWritebackMode; }
		MergeExecutionMode mergeExecutionMode() const { return algorithmConfiguration_.mergeExecutionMode; }
		AlgorithmConfiguration algorithmConfiguration() const { return algorithmConfiguration_; }
		bool mergeBufferVisualizationEnabled() const { return mergeBufferVisualizationEnabled_; }
		bool completionScanEnabled() const { return completionScanEnabled_; }
		bool isBuildingTrace() const { return buildingTrace_; }
		bool isPlaying() const { return playing_; }
		bool hasTrace() const { return static_cast<bool>(trace_); }
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
		AlgorithmConfiguration algorithmConfiguration_;
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
