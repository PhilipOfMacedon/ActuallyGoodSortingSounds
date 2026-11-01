# Reconstruction coverage inventory

This inventory accompanies [`reconstruction-coverage.csv`](reconstruction-coverage.csv), which lists every selectable catalog variant and the trace/presentation contract currently registered for it.

## Current catalog summary

- 212 registered variants.
- 25 variants declare `IncrementalPublication`; their default `reconstruction_mode` is `detailed`, and `compact_reference` preserves the former grouping/scoring policy.
- 124 variants declare `DirectMainOperations`; they retain ordinary in-place operation semantics and do not acquire reconstruction notes merely because they write the primary array.
- 63 variants declare `DeclaredNonReconstruction`; they use auxiliary state, special outcomes, or demonstration strategies without a registered output-publication path.
- No catalog entry may remain `Unresolved`. `validateAlgorithmCatalog()` checks stable IDs, property defaults and values, workspace bindings, reconstruction properties, and classification consistency. `testPinnedCatalogCoverage()` exercises that validation.

The per-variant table is a checked-in snapshot of the static catalog. `AlgorithmCatalog.cpp` is authoritative; update the CSV whenever a variant or its coverage declaration changes. The `test_coverage` column points to the catalog-wide tests used for each classification. `verifyProfilesAndAlgorithms()` builds catalog entries in both directions and validates ordering, item preservation, exact forward/backward replay, snapshots, and reconstruction provenance/cadence. `testCompactReconstructionModes()` exercises both directions for every reconstruction-enabled variant in the legacy mode.

## Shared output path audit

All detailed placement events pass through `TraceRecorder.h`: `writeMain`, `writeMainFromAuxiliary`, `publishAuxiliaryToMain`, and `writeMainRange`. Local output vectors are surfaced as a replayable staging workspace before item-by-item placement; the staging event is bookkeeping, while each captured placement is a distinct reconstructive operation and cadence candidate.

Algorithm call sites that publish or materialize primary output are in:

- [`DistributionStrategies.cpp`](../SortingLogic/DistributionStrategies.cpp) — Counting, Radix, Pigeonhole, bucket/distribution, gravity, shatter, static, and deadline outputs.
- [`GrailStrategies.cpp`](../SortingLogic/GrailStrategies.cpp) and [`GrailTraceUtilities.cpp`](../SortingLogic/GrailTraceUtilities.cpp) — buffered and circular merge materialization.
- [`HeapStrategies.cpp`](../SortingLogic/HeapStrategies.cpp) and [`HanoiStrategy.cpp`](../SortingLogic/HanoiStrategy.cpp) — extracted output sequences.
- [`LibraryStrategies.cpp`](../SortingLogic/LibraryStrategies.cpp) — dense and sorted-library publication.
- [`MergeTraceUtilities.cpp`](../SortingLogic/MergeTraceUtilities.cpp) and [`MergeStrategies.cpp`](../SortingLogic/MergeStrategies.cpp) — merge range and variant output.
- [`PartitionStrategies.cpp`](../SortingLogic/PartitionStrategies.cpp) — partition output publication.
- [`PermutationTraceUtilities.cpp`](../SortingLogic/PermutationTraceUtilities.cpp) — permutation/cycle materialization.
- [`RunBasedStrategies.cpp`](../SortingLogic/RunBasedStrategies.cpp) and [`TournamentStrategies.cpp`](../SortingLogic/TournamentStrategies.cpp) — run merge and tournament output.
- [`StochasticStrategies.cpp`](../SortingLogic/StochasticStrategies.cpp) — novelty output that uses a range publication helper.

Presentation consumers resolve catalog metadata once into `ResolvedAlgorithmPresentation`. `TracePresentationPolicy` uses that resolved declaration for reconstruction eligibility, cadence, operand ordering, audio capabilities, and workspace visibility. The renderer uses registered workspace descriptors and the trace's active roles/regions; hiding a workspace does not alter the trace or its audio indices.

## Maintenance rule

When a builder adds an output path, classify the variant in the catalog, route each detailed placement through a provenance-bearing recorder operation, add replay/cadence coverage, and refresh this inventory. A new algorithm does not need an ID branch in renderer, audio, settings, or demo consumers.
