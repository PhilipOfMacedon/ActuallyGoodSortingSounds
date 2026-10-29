#include "SortingLogic.h"
#include "AlgorithmRequestBuilders.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace
	{
		bool contains(const std::string& text, const char* token)
		{
			return token && text.find(token) != std::string::npos;
		}

		DemoComplexityClass classifyDemoComplexity(const AlgorithmDescriptor& algorithm)
		{
			const std::string stableId = algorithm.stableId ? algorithm.stableId : "";
			const std::string familyId = algorithm.familyId;
			std::string time = algorithm.timeComplexity ? algorithm.timeComplexity : "";
			for (std::size_t i = 0; i < time.size(); ++i)
				time[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(time[i])));
			if (!algorithm.supportsNormalizedFloat && algorithm.supportsIntegerKeys)
				return DemoComplexityClass::KeyDependent;
			if (algorithm.id == SortAlgorithm::Shell || contains(stableId, "shell") || contains(familyId, "shell"))
				return DemoComplexityClass::GapDependent;
			if (contains(stableId, "bogo") || contains(stableId, "guess") || contains(stableId, "stooge"))
				return DemoComplexityClass::Superpolynomial;
			if (algorithm.id == SortAlgorithm::Quick || contains(familyId, "quick"))
				return DemoComplexityClass::NLogN;

			if (contains(time, "2^n") || contains(time, "2ⁿ") || contains(time, "n!") ||
				contains(time, "n^n") || contains(time, "nⁿ") || contains(time, "factorial"))
				return DemoComplexityClass::Superpolynomial;
			if (contains(time, "n² log") || contains(time, "n^2 log") || contains(time, "n^2·log") ||
				contains(time, "n²·log")) return DemoComplexityClass::N2LogN;
			if (contains(time, "n log²") || contains(time, "n log^2") || contains(time, "n·log²") ||
				contains(time, "n·log^2")) return DemoComplexityClass::NLogSquaredN;
			if (contains(time, "n√n") || contains(time, "n·√n") || contains(time, "n sqrt n"))
				return DemoComplexityClass::NSqrtN;
			if (contains(time, "n²") || contains(time, "n^2")) return DemoComplexityClass::Quadratic;
			if (contains(time, "n³") || contains(time, "n^3") || contains(time, "n⁴") ||
				contains(time, "n^4") || contains(time, "n⁵") || contains(time, "n^5") ||
				contains(time, "n^k") || contains(time, "n^m")) return DemoComplexityClass::HigherPolynomial;
			if (contains(time, "n log n") || contains(time, "n·log n") || contains(time, "n log₂ n") ||
				contains(time, "n log2 n")) return DemoComplexityClass::NLogN;
			if (contains(time, "o(n)") || contains(time, "linear") || contains(time, "n + 1"))
				return DemoComplexityClass::Linear;
			return DemoComplexityClass::DemonstrationUnknown;
		}
	}

	const std::vector<AlgorithmDescriptor>& algorithmCatalog()
	{
		static const std::vector<AlgorithmDescriptor> algorithms = []()
		{
			std::vector<AlgorithmDescriptor> descriptors = {
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
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Halving), "halving", "Halving", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence, true),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Knuth), "knuth", "Knuth", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Ciura), "ciura", "Ciura", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence),
					AlgorithmOptionDescriptor(static_cast<int>(ShellGapSequence::Tokuda), "tokuda", "Tokuda", "gap_sequence",
						AlgorithmOptionTarget::ShellGapSequence)
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
					AlgorithmOptionDescriptor(16, "items_16", "16 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(32, "items_32", "32 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(64, "items_64", "64 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize, true),
					AlgorithmOptionDescriptor(128, "items_128", "128 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize),
					AlgorithmOptionDescriptor(256, "items_256", "256 items", "rotate_buffer_capacity", AlgorithmOptionTarget::RotateMergeBufferSize)
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
					AlgorithmOptionDescriptor(0, "in_place", "In-place", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode, true),
					AlgorithmOptionDescriptor(1, "static_buffer", "Static buffer (up to 512 items)", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode),
					AlgorithmOptionDescriptor(2, "dynamic_buffer", "Dynamic √n buffer", "grail_buffer_mode", AlgorithmOptionTarget::GrailBufferMode)
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
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::BottomUp), "bottom_up", "Bottom-up", "execution_mode",
						AlgorithmOptionTarget::MergeExecutionMode, true),
					AlgorithmOptionDescriptor(static_cast<int>(MergeExecutionMode::TopDown), "top_down", "Top-down", "execution_mode",
						AlgorithmOptionTarget::MergeExecutionMode),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AnimatedWrites), "animated_writes", "Animated item writes", "writeback_mode",
						AlgorithmOptionTarget::MergeWritebackMode),
					AlgorithmOptionDescriptor(static_cast<int>(MergeWritebackMode::AtomicRangeWrite), "compare_then_write_range", "Compare, then write range", "writeback_mode",
						AlgorithmOptionTarget::MergeWritebackMode, true)
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
					AlgorithmOptionDescriptor(4, "base_4", "Base 4", "heap_arity", AlgorithmOptionTarget::HeapArity, true),
					AlgorithmOptionDescriptor(2, "base_2", "Base 2", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(3, "base_3", "Base 3", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(8, "base_8", "Base 8", "heap_arity", AlgorithmOptionTarget::HeapArity),
					AlgorithmOptionDescriptor(16, "base_16", "Base 16", "heap_arity", AlgorithmOptionTarget::HeapArity)
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
				"binary-search exchanges use value then stable ID as a total order; duplicate-safe but unstable; bounded to 64 items", "novelty-sorts", "fun", "Novelty Sorts"),
			AlgorithmDescriptor(SortAlgorithm::ArrayvFunOriginal, "arrayv.fun_sort.upstream_original", "Fun Sort — upstream original", "O(n² log n) reported", "O(1)", false, true, true, true, &buildArrayvFunOriginalRequest,
				std::vector<AlgorithmOptionDescriptor>(), std::vector<AlgorithmMetricDescriptor>(), 64,
				"pinned upstream repeated binary-search exchanges with value-only equality; duplicate keys can prevent global ordering; bounded to 64 items", "novelty-sorts", "fun-upstream-original", "Novelty Sorts"),
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
			for (std::size_t i = 0; i < descriptors.size(); ++i)
			{
				descriptors[i].demoComplexity = classifyDemoComplexity(descriptors[i]);
				descriptors[i].preferredDemoDomain = descriptors[i].supportsNormalizedFloat
					? InputDomain::NormalizedFloats : InputDomain::IntegerPermutation;
			}
			return descriptors;
		}();
		return algorithms;
	}

	const char* demoComplexityClassName(DemoComplexityClass value)
	{
		switch (value)
		{
		case DemoComplexityClass::Linear: return "Linear";
		case DemoComplexityClass::NLogN: return "N log N";
		case DemoComplexityClass::NLogSquaredN: return "N log² N";
		case DemoComplexityClass::NSqrtN: return "N√N";
		case DemoComplexityClass::Quadratic: return "Quadratic";
		case DemoComplexityClass::N2LogN: return "N² log N";
		case DemoComplexityClass::HigherPolynomial: return "Higher polynomial";
		case DemoComplexityClass::GapDependent: return "Gap-dependent";
		case DemoComplexityClass::KeyDependent: return "Key-dependent";
		case DemoComplexityClass::Superpolynomial: return "Superpolynomial";
		case DemoComplexityClass::DemonstrationUnknown: return "Demonstration / unknown";
		}
		return "Demonstration / unknown";
	}

	bool demoComplexityClassFor(SortAlgorithm algorithm, DemoComplexityClass& value)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (!descriptor) return false;
		value = descriptor->demoComplexity;
		return true;
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

	}
