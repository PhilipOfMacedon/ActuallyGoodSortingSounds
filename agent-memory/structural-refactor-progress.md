# Structural Refactor Progress

Plan and architectural evidence: [structural-architecture-audit.md](structural-architecture-audit.md).

The refactor is being delivered in small slices. Preserve the observable trace, audio, settings, UI, and Visual Studio contracts listed in the audit. Do not treat a passing build as proof of performance improvement; measure performance claims separately.

## 2026-10-19 — text cache and frame preparation

Status: first implementation slice complete on `codex/structural-refactor-text-ui`.

- Added `TextLabel::setContent`, which updates visible text while retaining the resolved font identity, raster size, and DPI. It only rerasterizes when the text content actually changes; `drawText` remains responsible for resolving the active appearance font.
- Switched retained `ControlPanel` labels to content-only updates, removing the per-frame alternation between the legacy font key and the themed font/DPI key. Lane labels use the same content-only API.
- Removed the second `prepareFrame` call from `ControlPanel::render`. The app prepares layout and widget state once before chart rendering; `render` now paints the prepared frame.
- Sorting, trace generation, audio synthesis, preferences, and settings formats were not changed.

Verification:

- Visual Studio solution x64 Debug build: passed.
- Visual Studio solution x64 Release build: passed on a serial confirmation. The first parallel invocation reported LNK4020 PDB warnings while linking `AppFrameworkTests`; this remains an unisolated build warning and should be revisited with the build-structure work.
- x64 Debug `AppFrameworkTests`: passed.
- x64 Debug `SoundEngineTests`: passed, 417,371 assertions.
- Sorting tests were not run because no sorting implementation or trace contract changed. No runtime texture-allocation counter or benchmark has been added, so the improvement is based on the corrected call path rather than a measured performance claim.
- No screenshot-based GUI review was performed for this slice.

Next: resolve text styling and cache wrapped descriptions before the SortingLogic extraction.

## 2026-10-19 — resolved styles and cached wrapped text

Status: second UI text slice committed and pushed as `0d48b56`.

- Added `ResolvedTextStyle` so width measurement, rasterization, line height, and drawing use the same font, raster size, DPI, and logical height.
- Added renderer-independent `TextBlockLayout`, which caches wrapped lines and invalidates when content, available width, or any resolved style field changes. Its text-width callback keeps font measurement outside the layout value.
- Replaced the per-frame algorithm profile wrapping work with cached complexity and description layouts. Reused the resolved text style for algorithm popup width measurement and painting.
- The profile description remains a clipped scrollable paragraph. A visual check confirmed the restored Current preset, readable profile card, palette, and controls after switching presets; the short built-in profile text did not exercise long-content scrolling.
- No sorting trace, audio, or persistence code changed.

Verification:

- x64 Debug solution build: passed. x64 Debug `AppFrameworkTests`: passed.
- x64 Release solution build: passed. Release `AppFrameworkTests` and `SoundEngineTests`: passed; the sound suite reported 417,371 assertions.
- The Release `AppFrameworkTests` link continues to emit existing LNK4020 PDB type-record warnings for `SoundEngine.pdb` and `SortingLogic.pdb`; linking and execution succeed. The warning is not isolated by this slice.
- `git diff --check` passed. The current worktree had no changes to sorting, sound, or settings behavior in this slice.

## 2026-10-19 — isolate trace state and replay

Status: first `SortingLogic` extraction committed and pushed as `af1e2c3`.

- Moved `SemanticEvent`, `SortTrace`, and `RenderSnapshot` construction, initial-state creation, comparison, forward/backward event application, state validation, and Shell-gap generation from `SortingLogic.cpp` into `TraceState.cpp`.
- Added `SortingLogicInternal.h` for invariant helpers shared by the recorder and replay code. The public `SortingLogic.h` API remains unchanged.
- Verified the moved implementation block matches the pre-extraction source byte-for-byte after newline normalization.
- Added `SortingLogicTests.exe --core-contracts-only` to run trace, replay, partial-budget, annotation, playback, workspace, and Counting/Radix event/audio-eligibility fixture checks without running every expensive algorithm case.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed. Release `SoundEngineTests`: passed with 417,371 assertions; no sound code changed in this slice.
- Full `SortingLogicTests` was attempted in Debug and Release but stopped before completion after high CPU use; the Release process had grown to about 455 MiB. This resource-heavy path is not counted as a pass. Targeted core and coverage suites passed instead.
- Release app-framework linking continues to emit LNK4020 warnings about PDB type records. They do not prevent linking or test execution and remain unisolated.

Existing baseline coverage was verified before extraction: `testDistributionAudioBaselines` freezes Counting/Radix trace and eligible-audio event order; `SoundEngineTests` compares PCM bit-for-bit to independent `LegacySoundEngine`; settings tests cover migrations through v8; focused Lazierest tests include the reported 400-item seed. The full-run cost means the broad suite should be decomposed into reusable focused contracts before it is used as the only regression gate.

Next: extract Simulation and input-generation ownership behind the existing facade, then split catalog/build routing and algorithm family strategies. Keep baselines fixed and run focused tests for each moved boundary.

## 2026-10-19 — isolate simulation and prepared input

Status: Simulation/input extraction committed and pushed as `8d5368f`.

- Moved seed creation, prepared input generation, baseline presets, configuration commands, trace build lifecycle, playback/seek, active-role projection, and snapshots into `Simulation.cpp`.
- Kept the `Simulation` type and public API in `SortingLogic.h`. The move uses existing public `SortingLogic` contracts for replay, compatibility, and catalog metadata; no new cross-module public API was introduced.
- Verified the moved method block matches the pre-extraction source after newline normalization (with only the new file's closing whitespace outside the compared block).

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release LNK4020 PDB type-record warnings appeared again while linking `AppFrameworkTests`; linking and tests succeeded.
- No sound, settings, algorithms, event payloads, or trace order were changed.

Next: extract trace request validation, then establish the recorder boundary for family modules.

## 2026-10-19 — isolate trace request validation

Status: request-validation extraction committed and pushed as `6109fa2`.

- Moved both `SortTraceBuilder::build` entry points and request validation into `TraceBuilder.cpp`.
- Moved the legacy-profile-to-domain compatibility rule into the private `SortingLogicInternal.h`; the public header and request shape remain unchanged.
- The extracted validation block matches its prior implementation aside from whitespace-only lines.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release LNK4020 PDB type-record warnings remain present during `AppFrameworkTests` linking.

Next: establish the private recorder boundary, then extract the first family strategy.

## 2026-10-19 — define the private recorder boundary

Status: recorder boundary committed and pushed as `458fa02`.

- Moved `Recorder` into the non-public `TraceRecorder.h`, under `sortinglogic::detail`, so future family modules can record semantic operations without adding recorder internals to `SortingLogic.h`.
- Moved operation/workspace budget exceptions and the accounted-state/event byte helpers into `SortingLogicInternal.h`, shared by recording and failure handling.
- Verified the recorder class matches its original implementation apart from whitespace-only lines. The private helper header keeps one implementation of the invariant checks and trace accounting.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release `AppFrameworkTests` linking again emitted LNK4020 PDB type-record warnings; linking and execution succeeded.

Next: extract the first family strategy through the private recorder contract.

## 2026-10-19 — extract the first algorithm strategy

Status: Insertion Sort strategy extraction committed and pushed as `8babfe9`.

- Moved the shift-based Insertion Sort builder into `InsertionStrategy.cpp` and declared it through the private `AlgorithmStrategies.h` contract.
- Routed both the catalog dispatcher and Intro Sort's small-range fallback through that same strategy function. The strategy receives only the private recorder contract and uses the existing comparator and event operations.
- Added ascending and descending duplicate-key insertion cases plus an Intro Sort small-range fallback case to `--core-contracts-only`; each uses the shared exhaustive cursor, stability, and inverse-replay verifier.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,550 checks with 0 failures. These runs include the pinned catalog coverage check.
- Debug and Release `AppFrameworkTests`: passed.
- Release `AppFrameworkTests` linking emitted the same LNK4020 PDB type-record warnings; link and execution succeeded.
- No algorithm event definitions changed; the extracted function body was moved without algorithm edits.

Next: extract the remaining insertion/exchange strategies in coherent batches, then move the static builder registrations away from the large switch without changing their stable IDs or trace signatures.

## 2026-10-19 — extract elementary exchange and selection strategies

Status: committed and pushed as `0a5614b`.

- Moved Bubble, Selection, Bingo, Double Selection, Stable Selection, Gnome, Cocktail, Comb, Odd-Even, optimized/unoptimized Bubble and Cocktail, and Swapless Bubble into `ElementaryStrategies.cpp` behind the private `AlgorithmStrategies.h` interface.
- Kept catalog IDs and the central dispatch behavior unchanged; each dispatch now calls the extracted strategy. Intro Sort's existing Insertion fallback continues to call the shared Insertion strategy.
- Added ascending and descending duplicate-key cases for all 14 extracted methods to the focused core-contract runner. The cases also exercise final state, stability metadata, and exhaustive cursor replay/inversion through the shared verifier.
- Removed whitespace-only remnants left at former function boundaries.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 11,410 checks with 0 failures.
- Release catalog-coverage run: passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release `AppFrameworkTests` linking emitted the existing LNK4020 PDB type-record warnings; link and execution succeeded.
- No sound, settings, workspace contracts, stable IDs, or intended operation sequences were changed.

Next: continue extracting cohesive algorithm families, then replace the growing algorithm dispatch switch with static private registrations while preserving compatibility adapters and trace signatures.

## 2026-10-19 — isolate sorting-network strategies

Status: committed and pushed as `eeebd5f`.

- Moved the compare-exchange helpers, deterministic layer scheduling, recursive kernels, and network strategy bodies into `SortingNetworkStrategies.cpp`.
- Routed the 26 catalog entries for 3-smooth comb, Bitonic, Bose–Nelson, Odd-Even Merge, Diamond, Pairwise, Crease, Fold, Merge-Exchange, Pairwise Merge, Weave, and Matrix strategies through the private strategy interface.
- Kept helper implementations private to the translation unit. The pairwise-merge power-of-two padding stays the same; its setup now belongs with the algorithm family instead of the central dispatcher.
- Added the new module to the existing Visual Studio project; public `SortingLogic` API and stable catalog IDs remain unchanged.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 11,410 checks with 0 failures, including sorting-network schedules, phase reconstruction, trace replay, and distribution audio baselines.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release `AppFrameworkTests` linking emitted the existing LNK4020 PDB type-record warnings; link and execution succeeded.
- No sound synthesis, event semantics, catalog metadata, or settings behavior changed.

Next: continue migrating catalog entries to direct strategy registrations and reduce algorithm dispatch and catalog construction coupling.

## 2026-10-19 — register extracted strategies directly

Status: committed and pushed as `d8df4a4`.

- Added a strategy-function path to the common trace-build harness. It still owns recorder construction, budget failures, finalization, trace validation, and audio-event indexing.
- Pointed the extracted insertion, elementary exchange/selection, and sorting-network catalog builders directly at their strategy functions instead of redispatching through the algorithm switch. The switch remains the compatibility route for strategies not yet extracted.
- Moved network-specific variant adapters—recursive/parallel schedules and padded pairwise-merge setup—into `SortingNetworkStrategies.cpp`, so the central build path does not encode those algorithm choices.
- Added small exhaustive replay cases for the direct 3-Smooth and Matrix registrations, completing direct-path verification for the network group. Existing contracts cover the remaining network variants and elementary methods.
- Kept the catalog's stable IDs, descriptor ordering, public builder signatures, and request defaults unchanged.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 16,740 checks with 0 failures.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- The final Release rebuild completed its full link-time code generation after an IPDB/IOBJ cache mismatch; all targets linked. Earlier incremental builds have emitted LNK4020 PDB warnings, while the final rebuild did not.
- The direct strategy path did not change trace events, phase schedules, Counting/Radix baselines, audio eligibility, or settings behavior.

Next: continue extracting and directly registering cohesive algorithm groups, then move catalog metadata and run configuration behind smaller private modules.

## 2026-10-19 — isolate distribution strategies

Status: committed and pushed as `c683cfe`.

- Moved Counting, Radix, American Flag, binary quick variants, Flash, Pigeonhole, Index, in-place and rotate radix variants, Gravity variants, Shatter variants, Static, and Time Sort into `DistributionStrategies.cpp`.
- Kept key transforms, bucket helpers, reversible counter/workspace operations, and distribution-specific bounds in the same private module. Special variant selection is wrapped by named strategy entry points.
- Pointed all 21 distribution catalog entries directly at their strategy functions. The common trace harness still owns budgets, finalization, state validation, and audio-index generation.
- Added exhaustive small duplicate-key/permutation replay cases for every distribution strategy in ascending and descending order. Counting/Radix's existing event-order, audio-eligibility, and PCM reference fixtures remain separate regression gates.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,446 checks with 0 failures. This includes all distribution variants in both directions and the frozen Counting/Radix event/audio fixtures.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- No Counting/Radix event order, audio eligibility, gains, synthesis, or transport behavior changed.

Next: reduce configuration and catalog coupling, beginning with shared value semantics and a resolved run-configuration boundary.

## 2026-10-19 — centralize settings and audio equality

Status: committed and pushed as `1d2c92b`.

- Moved exact equality for synthesizer parameters and reconstructed-audio parameters onto their value types. `AudioPresentationBridge` now consumes those comparisons instead of keeping a second field list.
- Added value equality for algorithm audio levels and user preferences. `AppController` now uses the shared preference comparison for change detection and persistence gating.
- Kept comparison semantics identical to the removed helpers, including exact floating-point comparisons and excluding the settings schema version from change detection. No settings keys, migrations, normalization, synthesis, or timeline behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,446 checks with 0 failures.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Debug and Release `SoundEngineTests`: each passed 417,371 assertions.

Next: route stable catalog option selections through the sorting boundary and resolve them before trace generation, preserving the legacy numeric request fields as a compatibility adapter.

## 2026-10-19 — resolve stable algorithm options in SortingLogic

Status: committed and pushed as `6615885`.

- Added stable option selections to `TraceBuildRequest`, keyed by catalog group ID and carrying the catalog's stable option ID.
- `AppController` now forwards those stable selections instead of mapping group-name strings into overloaded numeric request fields.
- `SortTraceBuilder` validates each selection against the selected descriptor and resolves it before validation and strategy dispatch. It rejects unknown IDs and unbound option groups. The old numeric fields still work when no stable selection is provided, preserving source compatibility.
- Added checks that stable Merge choices override the compatibility fields, invalid option IDs are rejected, Shell options reach the trace, and heap/rotate/Grail options still reach their existing trace fields through the app path.
- This is a transition boundary: option group bindings and legacy named fields remain to be consolidated after all existing choices are represented in catalog metadata.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,448 checks with 0 failures.
- Debug and Release catalog-coverage runs: each passed 1,678 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.

Next: use explicit option targets and defaults as the catalog contract, then derive a single effective configuration without removing compatibility adapters prematurely.

## 2026-10-19 — declare option bindings and defaults in the catalog

Status: committed and pushed as `e45d3a7`.

- Added `AlgorithmOptionTarget` and an explicit default marker to each current choice descriptor. All registered option groups now declare exactly one default and a trace-configuration target.
- Replaced `TraceBuilder`'s group-name routing with target-based request resolution. App option dispatch and settings migration also use the declared target.
- Removed the Rotate Merge default-capacity special case from settings resolution. Its 64-item default now comes from catalog metadata; Shell and Merge retain their dedicated legacy fields as compatibility adapters.
- Added catalog checks for unique stable option IDs within each group, one default per group, and a bound target for every option. Existing settings migration and option-dispatch checks continue to cover effective values.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,496 checks with 0 failures.
- Debug and Release catalog-coverage runs: each passed 1,726 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.

Next: completed in the following entry.

## 2026-10-19 — add a resolved algorithm configuration value

Status: committed and pushed as `b0e24f5`.

- Added `AlgorithmConfiguration` to trace requests and immutable traces. Stable catalog options resolve into named fields, and strategies now read that configuration for Shell, Merge, Grail, Base-N heap, Rotate Merge, radix, distribution bucket counts, and Time Sort.
- Added explicit choice targets for heap arity and Grail buffer mode so they no longer travel internally as overloaded bucket counts. Existing scalar request and trace fields remain compatibility projections.
- Added `TraceBuildRequest::setConfiguration` to distinguish an intentional typed value (including a default) from an untouched request. Legacy callers still map their old fields; catalog option IDs override the matching typed or legacy setting.
- Validate explicit choice values against catalog options and verify every choice in an option group shares one configuration target. Added checks for typed-over-legacy precedence, typed Base-N execution, compatibility projection, and invalid typed choices.
- Kept event construction, replay mutations, and Counting/Radix audio presentation unchanged.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,514 checks with 0 failures.
- Debug and Release pinned catalog-coverage runs: each passed 1,741 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Debug and Release `SoundEngineTests`: passed 417,371 assertions each during this slice; no sound-engine code changed.

Next: move simulation and app request construction toward the resolved configuration value, keeping the old scalar fields at the public compatibility edge until their consumers are migrated.

## 2026-10-19 — consolidate Simulation's stored algorithm choices

Status: committed and pushed as `0806c10`.

- Replaced Simulation's three independent Shell/Merge option members with one `AlgorithmConfiguration` value. Existing accessors and snapshot fields remain stable views of that value.
- Trace request construction still projects those choices through the legacy scalar adapter while other external request fields are being migrated. The builder resolves that adapter into the immutable typed trace configuration.
- Added integration checks showing the Simulation-owned Shell sequence and independent Merge execution/writeback choices reach both the built trace configuration and the existing compatibility fields.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,520 checks with 0 failures.
- Debug and Release pinned catalog-coverage runs: each passed 1,741 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- SoundEngine was untouched in this slice; its 417,371-assertion Debug and Release runs passed in the preceding typed-configuration slice.

Next: completed in the following entry.

## 2026-10-19 — route application options through typed configuration

Status: implementation and focused verification complete.

- Added shared option-value accessors to `AlgorithmConfiguration`; catalog targets now have one read/write mapping instead of separate switches in trace validation, app dispatch, and simulation.
- Added `Simulation::setAlgorithmOption(group, stableId)`. It resolves against the active algorithm's catalog, enforces the existing build/play lock, updates the typed value, and clears an old trace only when the effective choice changes.
- Routed AppController option actions through that method. Stable family/variant/group/choice IDs remain the persisted representation, while Shell/Merge compatibility preference fields are updated as before.
- App trace requests now carry the Simulation's typed configuration alongside stable option IDs. Startup hydrates the active algorithm's stored choices; successful build publication copies the effective resolved configuration back into Simulation.
- Added an integration check that the effective Base-N arity reaches both the trace and the Simulation state after publication.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,520 checks with 0 failures.
- Debug and Release pinned catalog-coverage runs: each passed 1,741 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed, including option dispatch and settings persistence checks.
- No audio or rendering code changed; no unrelated sound suite was rerun.

Next: isolate settings compatibility and option resolution so the INI migration fields do not also serve as runtime configuration state.

## 2026-10-19 — make stable algorithm options authoritative in settings

Status: implementation and focused verification complete.

- Removed Shell gap and Merge execution/writeback duplicates from `SimulationPreferences`. The stable family.variant.group option map is now the only persisted in-memory preference for algorithm options; catalog metadata supplies defaults.
- Kept the v8 INI compatibility keys. Loading converts legacy Shell/Merge keys to stable option IDs before reading explicit `option.*` entries, allowing stable entries to override them. Saving projects the catalog selections back to the old keys in the lowercase format older settings files use.
- Removed AppController's synchronization between stable options and duplicate preference fields. Legacy typed actions still update Simulation and persist the equivalent stable catalog ID.
- Updated defaults, round-trip, v3/v5/v7 migration, and controller dispatch assertions to check effective catalog selections.
- A first verification run exposed a serializer mismatch between the display label `Ciura` and the legacy parser token `ciura`; the writer now emits the stable lowercase token.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 39,520 checks with 0 failures.
- Debug and Release pinned catalog-coverage runs: each passed 1,741 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed.

Next: extract view-level UI state and action binding from the `ControlPanel`/controller facade, starting with the smallest view seam that can share behavior between runtime and tests.

## 2026-10-19 — decouple generic UI widgets from the app controller

Status: implementation and focused verification complete.

- Moved `AppAction` and its enums into `AppActions.h`, a value-only contract that uses standard library types and a forward declaration for appearance edits.
- Changed `UiLayer.h` to include the action contract instead of `AppController.h`, removing its transitive dependency on settings persistence, simulation, and the trace-build service.
- Added direct SortingLogic and settings includes to `UiArchitecture.h`, which consumes those types. The first build exposed that it had also relied on the controller's accidental transitive includes; the header now declares its real dependencies.
- Listed the new contract in the Visual Studio project and filters. No action payload, widget event, or runtime dispatch behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed.

Next: extract one view's composition and state from `ControlPanel`, keeping event-to-action binding at the facade boundary and preserving the shared painting path.

## 2026-10-19 — extract algorithm browser state and catalog queries

Status: implementation and focused verification complete.

- Added `AlgorithmBrowserModel` to own the algorithm picker’s modal state, selected family and variant indices, search text, scroll positions, bounds, and scroll viewports.
- Moved family grouping and case-insensitive family/variant filtering out of `ControlPanel`. The model preserves catalog ordering and matches the former filtering rules; SDL event translation and painter submission remain in `ControlPanel` for now.
- Extracted shared ASCII case-insensitive text matching into `TextSearch.h`, used by both the browser and appearance-token search.
- Removed algorithm-menu fields from the general `UiSessionState`; browser state now has one owner. Added model tests for search equivalence, catalog order, open/close state, typing, and backspace behavior.
- No simulation, trace, audio, settings format, or rendering command behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git only reported the repository’s normal LF-to-CRLF working-tree notices.
- The first model test run exposed an overly narrow test expectation: “insertion sort” also matches other insertion-family variants. The test now checks case-insensitive result equivalence and preserved catalog order instead of assuming one result.

Next: separate the browser’s layout and event-to-action policy from `ControlPanel` behind a view-facing interface, keeping the common paint queue and validating scroll, keyboard, and modal behavior through the same production path.

## 2026-10-19 — move browser layout and input policy behind its model

Status: implementation and focused verification complete.

- Moved algorithm-browser modal geometry, pane bounds, viewport configuration, keyboard navigation, pointer hit-testing, search focus, busy rejection, Escape/back, and typed variant selection into `AlgorithmBrowserModel`.
- The model consumes the existing platform-neutral `UiInputEvent` and returns a small typed result. `ControlPanel` now translates SDL events, performs SDL text-input start/stop and modal dismissal, and adapts a selected stable family/variant ID to `AppAction`.
- Preserved the renderer’s existing direct ordered submissions. The browser still paints in `ControlPanel`; this slice isolates layout and interaction policy without adding a second paint pipeline.
- Added `UiKey::Backspace` so the generic event contract represents the browser’s existing search edit operation.
- Expanded headless checks to cover window-clamped layout, family and variant wheel scrolling, keyboard focus and navigation, variant selection, busy rejection, Escape/back, pointer selection, and outside-click dismissal. The scroll fixture uses the largest family and a compact logical window to guarantee list overflow.
- No sorting trace, audio, settings, or renderer command behavior changed. The existing UI scroll contract and variant catalog ordering remain unchanged.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only its configured LF-to-CRLF working-tree notices.
- A focused test run initially used the Insertion family as an overflow fixture, but it did not exceed the default visible row count. The test now derives the largest registered family and uses a shorter viewport, verifying actual overflow rather than assuming it.

Next: connect the browser’s real paint composition to the shared paint-command contract (or remove that unused parallel abstraction), so the same browser layout can be exercised by headless composition tests and consumed by the production renderer without duplicating widget geometry.

## 2026-10-19 — route algorithm browser through shared UI paint

Status: implementation and focused verification complete.

- Added `AlgorithmBrowserView`, a stateless composer that emits the browser's ordered rectangles and text through `UiPaintContext`. It consumes the existing browser model, catalog entries, viewport bounds, selection state, and theme palette.
- Connected the live `ControlPanel` renderer adapter to those same commands. The adapter retains the prior renderer materials, clip rectangles, paint layers, stable text-label cache IDs, and submission order, so runtime and headless tests now share actual browser composition rather than merely testing the recorder.
- Expanded paint-contract checks for dim/panel order, search labels, clipped family rows, variant labels, empty search results, and busy feedback. The fixture explicitly reports a missing catalog entry instead of silently skipping the checks.
- Visually inspected the running x64 Debug application and opened the algorithm browser. Its overlay, search field, family/variant panes, selected rows, and background dimming render as expected.
- No sorting, trace, audio, settings, or SDL input behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only configured LF-to-CRLF working-tree notices.

Next: extract the Appearance settings composition as the next sizeable view boundary, keeping setting edits as typed actions and using the same production/test paint-command path.
