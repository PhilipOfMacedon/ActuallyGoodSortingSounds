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

## 2026-10-19 — isolate appearance editor layout

Status: implementation and focused verification complete.

- Added a pure `calculateAppearanceEditorLayout` function for the appearance editor's centered panel, title, description/status, color swatch, content clip, scale, and 25 named widget bounds. It uses logical window dimensions and has no renderer or platform dependencies beyond the shared UI geometry contract.
- `ControlPanel` now applies that one result to its retained widgets and modal hit bounds, then paints from the same title, description, status, clip, panel, swatch, and scale values. This removes the duplicate panel/swatch state and avoids recomputing editor geometry during paint.
- When the existing main layout key invalidates on resize/DPI/preset changes, the appearance layout now refreshes with it. Popup-above decisions still use each widget's current visible option count, which remains input/layout state rather than fixed geometry.
- Added checks for the established 1920×1080 layout, compact 320×240 scaling, bottom action containment, empty dimensions, and the complete widget-bound set. Visually inspected the editor with all settings controls visible; the modal remains centered and the controls fit the panel.
- No settings values, edit actions, token behavior, renderer materials, or text rasterization changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only configured LF-to-CRLF working-tree notices.

Next: move the appearance editor's token filtering and draft-selection state behind a small view model, keeping file/color dialogs and app-action dispatch in the platform/facade layer.

## 2026-10-19 — move appearance token browsing into a view model

Status: implementation and focused verification complete.

- Added platform-neutral `AppearanceEditorModel` to own the editing preset, category filter, bounded search string, filtered stable token IDs/labels, and selected token. Search remains case-insensitive, catalog order is unchanged, and a vanished selection falls back to the first result (or clears when there are no results).
- `ControlPanel` now translates SDL text and widget selection into model calls, then mirrors the model's token options into the retained combo box. Draft appearance values, Win32 color/file dialogs, text-entry focus, and `ApplyAppearance` dispatch stay at the facade boundary.
- Removed the second appearance-preset field and the duplicated token-filter/search/selection containers from `ControlPanel`. Import, cancel, and preset selection still follow their existing draft behavior; category selection remains remembered across editor sessions, while search clears on reopen.
- Added headless checks for case-insensitive description search, stable IDs, category filtering, selection/fallback, preset switching, search limits, backspace, and clear. No appearance setting or serialized key changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only configured LF-to-CRLF working-tree notices.

## 2026-10-19 — route settings chrome through shared UI paint commands

Status: implementation and focused verification complete.

- Added `SettingsPanelView` to compose the dimmed backdrop, panel, top accent, title, content surface, and context-sensitive footer through the same `UiPaintContext` contract used by the algorithm browser.
- Routed both browser and settings paint commands through one `ControlPanel::drawUiPaintCommand` adapter, preserving renderer materials, layer mapping, clipping, cached label ownership, and painter order. Settings widgets remain on their existing widget-specific path, preserving hover, focus, slider, and popup behavior.
- Kept the existing footer content and bounds for Appearance/Algorithm versus General Audio. The view receives already-resolved text and colors; it does not own settings data or update the application.
- Added headless checks for command order, full-window dim bounds, panel/accent/content geometry, title/footer text layers, muted footer color, and omitted empty footer. Visually inspected the runtime settings overlay in the Debug build; the frame, tabs, controls, and footer render in the expected order and positions.
- No interaction, settings persistence, sorting, trace, or audio behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only its configured LF-to-CRLF working-tree notices.

## 2026-10-19 — isolate settings navigation and viewport state

Status: implementation and focused verification complete.

- Added `SettingsPanelModel` with typed Appearance, Algorithm, and General Audio pages plus Basic/Advanced audio groups. It owns the settings scroll viewport and the existing session-only audio-band selection, while saved preferences remain in `UserSettings`.
- Replaced the loose `UiSessionState` fields in `ControlPanel` with the model. Page changes and audio-group changes preserve the existing scroll-reset rules; opening General Audio directly preserves the last selected audio group. Wheel scrolling and row layout now consume the model's single viewport instance.
- Removed the no-op preset-change callback. The audio-band selection clamp remains at the UI adapter boundary; no persisted setting or visible control was added.
- Added checks for defaults, typed page navigation, scroll reset, audio-group preservation, finite/nonnegative scroll offsets, and audio-band bounds.
- No settings serialization, widget action, sorting, trace, or audio behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only its configured LF-to-CRLF working-tree notices.

## 2026-10-19 — extract the settings page control catalog

Status: implementation and focused verification complete.

- Added `SettingsPanelControls` for stable algorithm-option widget IDs and captions, plus the ordered control IDs shown by each settings page and General Audio group.
- Moved the existing Merge-only control, algorithm option grouping, per-operation audio visibility, and role-emphasis visibility decisions out of `ControlPanel::layoutSettingsControls`. The method now asks the helper for the page's active controls and applies geometry/visibility to the retained widgets.
- Preserved the exact control ordering and legacy visibility rules for Insertion, Merge, Counting/Radix, and discard behavior. Role-channel metadata is resolved once per algorithm layout rather than reconstructed inside the channel loop.
- Added headless checks for Appearance, Insertion, Merge, Counting, and both General Audio groups, including stable option IDs and operation-channel visibility.
- No widget geometry, settings values, persisted keys, action routing, sorting, trace, or sound behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only its configured LF-to-CRLF working-tree notices.

## 2026-10-19 — extract settings panel geometry

Status: implementation and focused verification complete.

- Added pure `calculateSettingsPanelGeometry` and `calculateSettingsWidgetGeometry` helpers. They return tab/content/footer rectangles, row bounds, radio hit segments, DOS scrollbar subregions, and the legacy band-selector popup direction.
- `ControlPanel::layoutSettingsControls` now applies those results to retained widgets. It no longer recalculates per-widget placement or pointer subregions itself; page control selection and geometry are separate helpers.
- Added checks for Appearance versus General Audio content offsets, compact-window minimum height, tab/footer placement, combo/button/radio geometry, scrollbar hit regions, and popup-above placement.
- The existing formulas and modal layout are unchanged. No settings, action, sorting, trace, or audio behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git emitted only its configured LF-to-CRLF working-tree notices.

Next: replace the independent overlay booleans with explicit overlay ownership and transitions, then centralize view event-to-action routing.

## 2026-10-19 — centralize modal overlay ownership

Status: implementation and focused verification complete.

- Added `UiOverlayState` as the single authority for the active settings panel, algorithm browser, and appearance editor. It enforces modal exclusivity and records the appearance editor's return destination when opened from Settings.
- Removed the settings, appearance-editor, and saved-return booleans from `ControlPanel`; input capture, event routing, drawing, and modal queries now derive from the overlay state. Input capture is synchronized whenever an overlay transition occurs.
- Removed the algorithm browser model's independent visibility flag. The model now owns only search, selection, and scroll interaction state; `ControlPanel` owns whether its view is active.
- Added headless transition checks for initial state, Settings-to-Appearance return, modal exclusivity, browser ownership, and return to the main view.
- Preserved the existing routes and appearance workflow. Opening the appearance editor from the algorithm browser now closes that browser first, preventing overlapping modal state.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Release `SoundEngineTests`: passed (417,371 assertions); no sound code changed.
- An attempted Debug `SortingLogicTests` rerun was stopped after more than eight CPU minutes because this UI-only slice does not touch SortingLogic; no SortingLogic result is claimed for this slice.

Next: centralize view event-to-action routing so the retained UI tree emits one typed action path instead of mixing widget events and direct view commands inside `ControlPanel`.

## 2026-10-19 — extract SDL input translation

Status: implementation and focused verification complete.

- Added `SdlUiEventAdapter` to translate pointer, wheel, keyboard, text, and focus events into the retained UI's `UiInputEvent` contract. The adapter is deterministic: the ControlPanel facade supplies SDL's current pointer position for wheel events and current modifiers for mouse-down events.
- Replaced the separate conversion blocks in the main panel and appearance editor, and the algorithm browser's narrower converter, with the shared adapter. `Controls` and `Search` key profiles preserve their previous Space/Backspace differences.
- Added framework checks for supported/unsupported keys, modifier propagation, pointer coordinates, wheel context, text input, focus loss, and ignored window events. The test project only needs SDL headers; this adapter does not link or call SDL runtime functions.
- No widget geometry, action mapping, keyboard shortcut, simulation, trace, or audio behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Debug and Release `SoundEngineTests`: passed (417,371 assertions each).
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: extract the main-menu and widget-event to typed-command mapping from `ControlPanel::processAction`, preserving the existing `AppAction` payloads and menu selection behavior.

## 2026-10-19 — extract main-menu action mapping

Status: implementation and focused verification complete.

- Added `mainMenuSelectionAction`, a pure mapping from the existing stable menu control IDs and option indices to typed `AppAction` values or view commands.
- `ControlPanel` still resets the selected menu option and applies the mapped action, but no longer embeds File/View/Tools selection policy or recursively handles one Tools-menu case by hand.
- Added checks for every current menu entry, invalid indices, and unknown menu IDs. The mapped actions match the existing menu behavior.
- No menu labels, indices, shortcuts, simulation commands, settings, or persistence behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: extract catalog-backed AppAction normalization for algorithm selection, option selection, and Shell sequence values from `ControlPanel::processAction`, then continue separating settings/audio dispatch from widget interpretation.

## 2026-10-19 — normalize catalog-backed UI actions

Status: implementation and focused verification complete.

- Added `normalizeUiCatalogAction` to resolve legacy numeric algorithm choices into stable family/variant IDs and resolve algorithm option indices or stable option IDs into catalog-owned values.
- Moved Shell gap sequence selection and its established Halving fallback out of `ControlPanel` and into the same catalog adapter.
- `ControlPanel::processAction` now delegates these catalog-facing action kinds as a group; the application controller continues to receive the same stable action payloads.
- Added checks for numeric and stable algorithm identity, invalid algorithm IDs, valid/fallback Shell sequences, grouped Merge option indices, stable option IDs, and invalid option selections.
- No algorithm behavior, catalog order, menu option values, settings persistence, or controller dispatch rules changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: separate audio/settings command handling from widget interpretation and reduce the remaining event-ID string routing in `ControlPanel::processAction`.

## 2026-10-19 — extract UI action routing

Status: implementation and focused verification complete.

- Added `UiActionRouting` as the dedicated home for main-menu mapping, catalog-backed action normalization, and the bar/time activation mapping.
- `ControlPanel::processAction` now delegates those policies instead of containing its own bar/time arithmetic and importing the general `UiArchitecture` module for action routing.
- Kept the established step sizes exactly: Ctrl divides by ten, Shift multiplies by ten, and pressing both or neither preserves the base step. Unknown controls and non-activation events remain no-ops.
- Added mapping checks for bar and duration actions, modifier-scaled changes, and unrelated control IDs. Existing menu and catalog normalization checks now use the extracted module.
- No menu behavior, command payload, catalog choice, settings value, simulation, trace, or sound behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: separate settings and audio widget interpretation from `ControlPanel::processAction`, then continue with controller/persistence boundaries.

## 2026-10-19 — route settings navigation through typed intents

Status: implementation and focused verification complete.

- Added a typed `SettingsNavigation` result that maps view commands to a settings page or audio subgroup. The mapping now lives with the other UI action routing rules instead of being reconstructed in `ControlPanel` conditionals.
- Preserved Appearance, Algorithm, and General Audio page selection; both legacy and current General Audio commands still resolve to the same page. Basic and Advanced remain separate audio groups.
- `ControlPanel` still owns the effects of navigation: updating its session model, closing a popup, and recalculating settings layout. The router only classifies the command and does not mutate view state.
- Added checks for each page/group mapping, the legacy audio alias, and unrelated modal commands.
- No settings content, selected preference, action payload, persistence, simulation, trace, or audio behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: isolate per-variant/general audio widget dispatch from app preference dispatch, then continue with the duplicated settings equality and normalization paths.

## 2026-10-19 — extract audio settings actions and normalization

Status: implementation and focused verification complete.

- Moved comparison and reconstructed audio action-to-field mapping out of `AppController` into the value-only `AudioSettingsActions` adapter. Mismatched action kinds are ignored.
- Extracted pure SoundEngine parameter normalizers and made both runtime setters use the same normalization functions. App settings no longer construct `Synthesizer` or `ReconstructedSynthesizer` objects merely to clamp a value.
- Preserved the two existing reconstructed frequency fallback contexts: the runtime setter falls back to its prior normalized values, while the application action adapter matches its former fresh-validator factory-default fallback.
- Added tests for clamping, NaN/infinity fallbacks, invalid enum values, normalizer idempotence, runtime-setter equivalence, invalid channel indices, and the reconstructed frequency fallback.
- Rechecked the equality finding in the audit against the current source. `UserPreferences`, `SoundEngine::Parameters`, and `ReconstructedParameters` already centralize their equality operators; the audit's claimed duplicate helpers in the controller and bridge were stale. Updated the audit to distinguish that from the real remaining settings-copy and codec/storage boundaries.
- No synthesizer formula, timeline scheduling, trace, UI control, saved key, or settings version changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Debug `SoundEngineTests`: passed (417,376 assertions).
- Release `SoundEngineTests`: passed (417,376 assertions).
- `git diff --check`: passed; Git reported only its configured LF-to-CRLF working-tree notices.

Next: classify actions that can change saved values and reduce full preference copies for transport-only commands.

## 2026-10-19 — narrow dispatch preference snapshots

Status: implementation and focused verification complete.

- Added value equality for `VisualPreferences` and `SimulationPreferences`, and made `UserPreferences` equality compose those value contracts.
- `AppController::dispatch` now snapshots simulation configuration for every action because accepted commands can reconcile saved input, but only actions that can change non-simulation preferences take a full `UserPreferences` snapshot. Play, pause, step, seek, reset, fullscreen, audio-band selection, and device retry avoid copying appearance/audio/options maps.
- Unknown future action kinds conservatively use the full snapshot until explicitly classified. All current `AppActionKind` values are listed.
- Actual preference changes still save immediately; unchanged values and transport-only actions still produce no writes.
- Added direct tests for preference value equality. Existing controller persistence tests continue to cover no-op edits, transport actions, and one-save-per-change behavior.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: isolate family/variant gain inheritance and effective SoundEngine parameter mapping from the settings schema and storage.

## 2026-10-19 — isolate algorithm audio preference policy

Status: implementation and focused verification complete.

- Moved family defaults, variant offsets, effective SoundEngine parameter construction, supported semantic-role channels, and display labels from `UserSettings.cpp` to `AlgorithmAudioPreferences.cpp`.
- Preserved the helper declarations in `UserSettings.h`; settings loading, saved keys, gain inheritance, clamps, routing, and audio behavior remain unchanged.
- Added the new source to the app, framework test, and standalone audio transport test project. The standalone project also lacked the already-required `AudioSettingsActions.cpp`; added it so the project links independently instead of failing before its tests.
- No settings schema, migration, DSP normalization, synthesizer, timeline, or callback behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` x64 Debug and Release builds: passed.
- Standalone transport integration tests passed in both configurations with the SDL dummy backend, including 80- and 800-item scenarios.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: separate INI schema/migrations from Win32 storage while keeping the saved format stable.

## 2026-10-20 — isolate settings file storage

Status: implementation and focused verification complete.

- Moved LocalAppData path resolution, the 1 MiB bounded read, temporary-file creation and flush, pre-migration backup, and atomic replacement into `SettingsFileStorage.cpp`.
- `UserSettings.cpp` no longer includes Win32 headers or performs file I/O; it retains the current INI schema, migrations, preference validation, and serialization decisions.
- Preserved the existing settings path, error messages, backup suffix, read limit, write-through replacement, and fallback behavior.
- Added the storage source to the app, framework tests, and standalone audio transport test project.
- No settings version, key, migration, value, or save-on-change behavior changed.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed, including settings round-trip, migration, malformed-file, and backup checks.
- Standalone `AudioDeviceTransportTests` x64 Debug and Release builds: passed.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: move INI encoding, decoding, and migrations behind a pure codec boundary. Keep the current settings version and immediate save behavior.

## 2026-10-20 — avoid render snapshots for narrow simulation queries

Status: implementation and focused verification complete.

- Added read-only `Simulation::hasTrace()` and `Simulation::cursor()` accessors.
- Replaced AppController snapshots used only to inspect trace existence or cursor position, including reference-cadence synchronization. Full snapshots remain where audio presentation or UI actually consumes the render state.
- Extended the simulation worker/playback test to compare the narrow getters with the exact trace cursor before build, after publication, at each seek position, and after reset.
- Updated the audit's frame-cost finding to mark duplicate panel preparation and narrow controller queries resolved, while retaining snapshot copies, region scans, and per-item role lookup as measured candidates rather than assumed bottlenecks.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Debug and Release `SortingLogicTests --core-contracts-only`: passed (39,522 checks each).
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

## 2026-10-20 — isolate settings validation and INI codec

Status: implementation and focused verification complete.

- Moved settings range/enum invariants into `UserSettingsValidation.cpp`, pure INI parsing, serialization, and v1–v8 migrations into `SettingsIniCodec.cpp`, while `SettingsFileStorage.cpp` remains the only module using Win32 file APIs.
- Reduced `UserSettings.cpp` to preference value semantics and `SettingsStore` delegation. Added a single shared current-version constant; settings remain at version 8.
- Kept stable keys, migration fallback rules, defaults, format precision, and serializer field order by moving the existing codec logic without redesigning it.
- Existing migration, malformed-value, round-trip, and backup tests exercise the codec through `SettingsStore`; no new settings format or key was introduced.
- Added codec and validation translation units to the app, framework tests, and standalone audio transport test project.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed, including v1–v8 migration, malformed settings, audio fallback, round-trip, and migration backup checks.
- Standalone `AudioDeviceTransportTests` x64 Debug and Release builds: passed.
- Standalone transport integration tests passed in both configurations with the SDL dummy backend, including both timing modes and 800-item cases.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

The next boundary identified after the settings extraction was deterministic semantic presentation policy; its implementation and verification are recorded below.

## 2026-10-20 — centralize trace presentation policy

Status: implementation and focused verification complete.

- Added `TraceAudioPresentationPolicy` as the shared semantic-to-presentation decision point. It classifies comparison/probe events, operation accents, distribution bookkeeping, discards, and hidden Merge-buffer writes; the core now uses it to populate the existing reference-cadence index.
- The policy also projects trace operand IDs, semantic roles, and configured operand order for the bridge. Prepared timelines, live comparison state, and frame accents now consume the same classification and projection rules.
- Moved operation-category visibility out of `SettingsPanelControls` into the shared policy. Chart rendering and main-array role projection now use the same Merge workspace visibility rule.
- Preserved the existing behavior: Comparison and Probe timeline eligibility, auxiliary-write cadence, hidden Merge buffer behavior, role IDs, operand reversal, completion scans, and operation accent categories. No synthesis, PCM, envelope, gain, or mailbox implementation changed.
- Added focused policy checks for event categories, cadence indices, single/paired operands, role projection, discard settings, completion-scan behavior, and shared Merge-lane visibility.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `SortingLogicTests --core-contracts-only`: passed (39,535 checks each), including Counting/Radix audio signature baselines.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` x64 Debug and Release builds and runs: passed with SDL dummy audio, including fixed/reference cadence, pause/step/seek, and 80/800-item scenarios.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: isolate playback orchestration state transitions from the main loop, controller, audio bridge, and device adapter without changing callback ownership or generation/revision fencing.

## 2026-10-20 — centralize playback timebase conversions

Status: implementation and focused verification complete.

- Added `PlaybackTimebase` as the single owner of the reference timeline's 300-event cadence and 44.1 kHz sample rate, including duration, cursor/elapsed, and frame/seconds conversions.
- Routed controller fallback playback, bridge timeline preparation, control-panel duration labels and scrubbing, UI duration captions, and SDL seek-crossfade scaling through the named constants and helpers.
- Kept the reference timeline distinct from the actual SDL device rate; device resampling and the sound synthesizer remain unchanged.
- Added focused conversion tests for eligible-event cursor mapping, exact trace completion, and reference-frame conversion.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` Debug and Release runs with SDL's dummy backend: passed, including fixed/reference timing and 80/800-item cases.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: extract app-thread playback orchestration transitions into a coordinator while preserving the existing callback ownership and generation/revision fencing.

## 2026-10-20 — extract app-thread playback coordination state

Status: implementation and focused verification complete.

- Added `PlaybackCoordinator` to own queued-play intent, the audio-preparation gate, audio-clock availability transitions, and the host reference-clock value. `AppController` no longer stores these related flags and timing fields independently.
- Added explicit lifecycle states for no trace, trace building, audio preparation, queued, ready, playing, paused, and completed. The controller exposes the state and uses it to preserve the existing queued-status message.
- Centralized stale audio-playhead acceptance in the coordinator using the existing timeline-generation and transport-revision checks. The controller still issues `Simulation` commands; the bridge still owns timeline construction, and the SDL callback still owns its existing fenced installation/acknowledgement protocol.
- Kept device availability and timeline readiness as separate inputs, and preserved fallback-clock rebasing when the audio clock is lost.
- Added direct transition tests and controller integration checks for preparation/readiness states.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` Debug and Release builds and runs with SDL's dummy backend: passed, including pause/step/seek, reference/fixed timing, and 80/800-item cases.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: narrow the main loop's playback wiring and introduce a named timeline-generation/transport-revision fence value while keeping timeline-worker and callback ownership in their current modules.

## 2026-10-20 — name audio acknowledgement and playhead revision fences

Status: implementation and focused verification complete.

- Added `AudioTimelineAcknowledgement` for the callback's timeline-generation and seek-revision acknowledgement, and `AudioPlaybackRevision` for the active timeline generation and transport revision.
- Updated `SdlAudioOutput`, `AudioPresentationBridge`, `AppController`, the main loop, and transport tests to pass these named values. Removed the separate public acknowledgement accessors and multi-integer readiness/playhead interfaces so callers cannot silently swap same-typed counters.
- Kept the callback's atomic counters, acknowledgement order, immutable timeline installation, and retirement protocol unchanged.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` Debug and Release builds and runs with SDL's dummy backend: passed, including device recovery, seek acknowledgement, pause/step, fixed/reference timing, and 80/800-item cases.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: extract the main loop's snapshot, bridge-update, readiness, feedback, and retirement sequence behind a small app-thread frame coordinator while preserving its current order.

## 2026-10-20 — extract audio frame handoff coordination

Status: implementation and focused verification complete.

- Added `PlaybackFrameCoordinator` for the app-thread sequence that checks audio-clock readiness, publishes the current simulation snapshot to the bridge, updates the preparation gate, retires acknowledged timelines, applies current playhead feedback, and builds audio status text.
- Updated the main loop to delegate this handoff while retaining input processing, diagnostic-capture service, rendering, and the second post-feedback snapshot.
- Updated `AudioDeviceTransportTests` to use the production frame coordinator instead of reproducing its bridge/readiness/feedback wiring.
- Kept `AudioPresentationBridge`'s single timeline worker and all SDL callback installation, atomic acknowledgement, and retirement fencing in their existing owners.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release `AppFrameworkTests`: passed.
- Standalone `AudioDeviceTransportTests` Debug and Release builds and runs with SDL's dummy backend: passed, including device/seek readiness, pause/step/resume, fixed/reference timing, and 80/800-item cases.
- `git diff --check`: passed; Git reported only configured LF-to-CRLF working-tree notices.

Next: measure the remaining full-snapshot and role/region projection costs from audit item 9 before adding caches or changing frame snapshot ownership.

## 2026-10-20 — profile and index active render roles

Status: measured implementation complete; snapshot work remains open.

- Added `ActiveRoleLookup` for per-frame role sets of 12 or more entries. It sorts `(lane, stableId)` keys, compacts duplicate keys to the dominant role while preserving the linear scan's first-role tie behavior, and resolves each chart item with binary search. Smaller sets retain the scan path.
- Wired the chart's main-array and workspace role projections through reusable lookup buffers. The renderer drops borrowed vector references at the end of the frame while retaining index capacity for the next frame.
- Added equivalence checks against the original linear role selector across lanes, missing items, duplicate roles, priority order, and ties. Added a deterministic profile mode to `AppFrameworkTests` using an 800-item Merge trace with atomic range writes.
- Median CPU role projection per 800 items, including index rebuild: x64 Debug 5.611 ms scan vs. 2.653 ms indexed; x64 Release 0.897 ms scan vs. 0.142 ms indexed. The profile measures CPU projection rather than GPU submission or complete frame time.
- At the largest range-write cursor, snapshot medians measured 2,521.968 us in Debug and 36.909 us in Release. Snapshot copying/annotation work is the next optimization candidate; do not add cached snapshots until ownership and invalidation are measured.

Verification:

- x64 Debug and Release Visual Studio solution builds passed.
- x64 Debug and Release `AppFrameworkTests` passed.
- `AppFrameworkTests --render-cost-profile` completed in both configurations and reported the figures above.
- `git diff --check` passed; Git reported only the configured LF-to-CRLF working-tree notices.

## 2026-10-20 — keep audio coordination on compact playback state

Status: implementation and focused verification complete.

- Added `SimulationPlaybackState` with only cursor, duration, trace-present, complete, and playing fields. `AudioPresentationBridge` now accepts this compact value instead of a full `RenderSnapshot`.
- `PlaybackFrameCoordinator` obtains the small playback state directly. The main loop now builds one full render snapshot after audio playhead feedback, rather than copying the complete workspace once before and once after the handoff. The ordering of audio feedback and rendered state remains the same.
- Updated the offline reconstructed-audio preview and transport tests to use the compact contract. The preview no longer copies the sorting workspace while preparing audio timelines.
- Added parity checks for no-trace, trace-start, stepped, playing, and complete states against the corresponding full snapshot fields.
- Based on the prior deterministic 800-item profile, this removes one full snapshot copy from the frame; at the largest range-write cursor, one snapshot measured 2,521.968 us in x64 Debug and 36.909 us in x64 Release. These are per-snapshot medians, not a full-frame benchmark.

Verification:

- x64 Debug and Release Visual Studio solution builds passed, including `SoundEnginePreview`.
- x64 Debug and Release `AppFrameworkTests` passed.
- Separate x64 Debug and Release `AudioDeviceTransportTests` builds and SDL dummy-device runs passed, including fixed/reference timing, pause/resume, stepping, seeking, and 80/800-item scenarios.
- `git diff --check` passed; Git reported only the configured LF-to-CRLF working-tree notices.

Next: profile the remaining state copy, role construction, and region filtering within the single render snapshot before considering snapshot caching.

## 2026-10-20 — reuse render snapshot storage

Status: measured implementation complete; annotation indexing is not justified by this profile.

- Added `Simulation::snapshotInto` and `AppController::snapshotInto`, retaining the existing value-returning snapshot API for callers that need ownership. The main loop now keeps one `RenderSnapshot` and fills it each frame.
- Snapshot generation clears and refills the destination's active-role and region vectors, preserving their capacity. It overwrites status and compatibility strings so trace replacement cannot leave stale presentation data. `rolesAtCursor` now fills a caller-owned vector instead of constructing and moving a new vector each frame.
- Added parity and stale-data checks for no-trace, initial, stepped, playing, complete, and replacement states. Added a profile path comparing fresh snapshots with reuse of a warmed destination.
- Deterministic 800-item Merge / atomic-range-write profile: x64 Debug fresh snapshot median 2,390.155 us vs. reused 1,091.750 us; x64 Release 41.030 us vs. 15.426 us. These are snapshot-only medians, not full-frame times.
- Isolated filtering of 799 region annotations measured 19.470/26.807 us in Debug at the comparison/range cursors and 0.780/0.938 us in Release. Leave the scan linear; this evidence does not justify an interval index.

Verification:

- x64 Debug and Release solution builds passed, including the app and `SoundEnginePreview`.
- x64 Debug and Release `AppFrameworkTests` passed; the role/snapshot profile ran in both configurations.
- `SortingLogicTests --core-contracts-only` passed in both configurations with 39,535 checks and 0 failures.
- Standalone x64 Debug and Release `AudioDeviceTransportTests` builds and SDL dummy-device runs passed, including 80/800-item pause, step, seek, and playback cases.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Next: continue with the audit's remaining UI view/action ownership and measured text/raster invalidation concerns; do not add annotation caches absent a larger representative trace showing material cost.

## 2026-10-20 — remove inactive rendering and audio objects

Status: implementation and regression verification complete.

- Whole-tree call-site search confirmed `Mesh`, `Rectangle`, and their `Texture` wrapper were referenced only by their own legacy implementation. Removed those six files from the application and removed their Visual Studio project/filter entries.
- Removed the unused `SoundEngine::Synthesizer` member from `SdlAudioOutput`. It was constructed but never read or written; the callback uses the two reconstructed synthesizers and the immutable timeline/seek state. The reusable synthesizer remains in `SoundEngine` and the independent audio tests/previews.
- Kept `Renderer2D`, `Shader`, and `TextLabel` resources, which do have live callers. UI rectangle paint commands are unrelated to the deleted `Rectangle` mesh class.

Verification:

- x64 Debug and Release solution builds passed.
- `AppFrameworkTests` passed in both configurations.
- `SortingLogicTests --core-contracts-only` passed in both configurations with 39,535 checks and 0 failures.
- `SoundEngineTests` passed in both configurations with 417,376 assertions.
- Standalone `AudioDeviceTransportTests` built and passed in both configurations with SDL dummy audio, including 80/800-item runs.
- `git diff --check` passed; configured line-ending notices only.

Follow-up: the next section records the chart-input narrowing and its verification.

## 2026-10-20 — narrow chart renderer preferences

Status: implementation and regression verification complete.

- Changed `ArrayRenderer::render` to accept `BarSpacingMode` instead of the full persisted `UserSettings`. The frame coordinator now passes `app.preferences().visual.spacing` explicitly.
- Extracted `BarSpacingMode` into `VisualSettingsTypes.h`. `LayoutMetrics.h` now depends on that small visual type rather than including the full settings model, reducing transitive settings, sound, and simulation dependencies in layout code.
- Preserved all layout calculations and the existing spacing enum values; this changes the ownership boundary only.

Verification:

- x64 Debug and Release Visual Studio solution builds passed.
- `AppFrameworkTests` passed in both configurations.
- `SortingLogicTests --core-contracts-only` passed in both configurations with 39,535 checks and 0 failures.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Next: continue the asset/source-ownership audit, starting with executable-relative shader/font lookup and explicit asset-root ownership.

## 2026-10-20 — resolve bundled assets beside the executable

Status: implementation and launch verification complete.

- Added `applicationAssetPath` backed by `SDL_GetBasePath`, resolving bundled assets beside the executable rather than relative to the caller's current working directory.
- Updated shader file reads to use `SDL_RWFromFile`, which accepts the UTF-8 path returned by SDL. Updated bundled font loading and measurement to use the same executable-relative path.
- Kept the current runtime-copy target and asset folder layout. No asset contents or rendering behavior changed.

Verification:

- x64 Debug and Release Visual Studio solution builds passed.
- Launching the Debug app with `%TEMP%` as its working directory remained active after three seconds and produced no shader/font errors.
- `AppFrameworkTests` passed in both configurations.
- `SortingLogicTests --core-contracts-only` passed in both configurations with 39,535 checks and 0 failures.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Follow-up: the next section records the shared app-framework target and test ownership change.

## 2026-10-20 — share app framework sources between app and tests

Status: implementation and four-configuration regression verification complete.

- Added one C++14 `AppFramework` static library for reusable application coordination, settings, UI models/presentation, audio presentation, and layout code. The app and `AppFrameworkTests` now consume the same compiled objects.
- Removed 27 duplicated application source entries from `AppFrameworkTests.vcxproj`; it now compiles its test harness and the two platform adapters it directly tests. The SDL/OpenGL application target no longer recompiles the shared framework sources.
- Kept SDL event translation and DirectWrite font enumeration with the application/test hosts. `AppFramework` imports no SDL/GLEW/GLM property sheet and requires none of those headers. SortingLogic and SoundEngine remain explicit project dependencies and are linked by each final executable.
- Preserved the four existing solution configurations and moved the shared source filter entries to the framework project.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and Win32 Debug/Release.
- `AppFrameworkTests` passed in all four configurations.
- `SortingLogicTests --core-contracts-only` passed in all four configurations with 39,535 checks and 0 failures.
- The x64 Debug app launched from `%TEMP%`, remained active after three seconds, and emitted no asset errors.
- Release links emit `LNK4020` warnings about type records in static-library PDBs; linking succeeds and the resulting app/test executables pass. Debug-symbol type data remains a separate build-quality issue.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Follow-up: the next section records the external library root override and its build verification.

## 2026-10-20 — allow local external library roots

Status: implementation and build verification complete.

- Made `ExternalLibsRoot` conditional in `ExternalDependencies.props`. A Visual Studio user property sheet, environment/property injection, or `/p:ExternalLibsRoot=...` can now select the local dependency directory.
- Kept the existing project-relative `External Libs` directory as the unchanged fallback, so current machines and solutions need no migration.

Verification:

- Built the full solution in x64 Debug/Release and Win32 Debug/Release with `ExternalLibsRoot` explicitly supplied on the MSBuild command line.
- The supplied path pointed to the current dependency folder and all four configurations succeeded.
- Release links still report `LNK4020` static-library PDB type-record warnings; executables link successfully. This issue is tracked separately from dependency path resolution.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Follow-up: the next section records the Release PDB warning fix and profile comparison.

## 2026-10-20 — narrow ControlPanel facade dependencies

Status: implementation and four-configuration build verification complete.

- Removed direct SDL event, `Renderer2D`, `Window`, and full `SortingLogic` includes from the `ControlPanel` public header. Its API now forward-declares the referenced host and trace types; `ControlPanel.cpp` explicitly includes the concrete implementations it uses.
- Kept `RenderingTypes.h` in the header because the facade's private renderer helper signatures use its value types. This is a narrow dependency cleanup, not a claim that the panel itself is now renderer-independent.
- Reconciled two stale audit findings against the current tree: `AppAction` is already in `AppActions.h`, and `AlgorithmBrowserView`/`SettingsPanelView` already use the shared runtime/test `UiPaintContext`. The remaining UI debt is concentrated in `ControlPanel` ownership and in the interactive controls that still submit directly to `Renderer2D`.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and Win32 Debug/Release.
- `AppFrameworkTests` passed in all four configurations.
- x64 Debug and Release `SortingLogicTests --core-contracts-only` each passed 39,535 checks with 0 failures.
- `git diff --check` passed; Git reported only its configured LF-to-CRLF notices.

Next: continue the UI boundary work from the corrected current-tree audit. Prefer extracting one cohesive owner from `ControlPanel` (appearance editing or main profile composition) rather than moving rendering code without ownership changes.

## 2026-10-20 — move native appearance dialogs behind an adapter

Status: implementation and four-configuration build/test verification complete.

- Added `AppearanceDialogAdapter` to the Windows/SDL application host. It now owns the native color chooser, appearance-profile file pickers, bounded UTF-8 file reads/writes, and calls to the existing profile codec.
- `ControlPanel` handles only the returned status/profile/error and applies successful edits to its current draft. The existing dialog filters, default export filename, 1 MiB import limit, cancellation behavior, profile messages, and color alpha are preserved.
- Removed the Win32 dialog and file APIs from `ControlPanel.cpp`; no appearance serialization format or preset/token behavior changed.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and Win32 Debug/Release.
- `AppFrameworkTests` passed in all four configurations, including existing appearance-profile codec checks.
- `git diff --check` passed; only configured LF-to-CRLF notices were reported.

Next: extract one remaining `ControlPanel` state/composition owner. The platform dialog, SDL event, and font-enumeration adapters are now explicit; avoid merely moving drawing statements into a second facade.

## 2026-10-20 — isolate algorithm browser catalog from SortingLogic

Status: implementation and four-configuration regression verification complete.

- Added `AlgorithmBrowserCatalog` as the only adapter from SortingLogic algorithm descriptors to UI-owned family and variant records. The browser model owns the presentation catalog it receives; its model and paint view contain no SortingLogic descriptor, catalog, or numeric algorithm-ID dependency.
- Browser selection and selected-row styling now use the stable `(familyId, variantId)` pair. Catalog order, display names, filtering, scrolling, focus, busy rejection, and selection actions remain unchanged.
- Added a contract check mapping every registered descriptor to its stable family/variant IDs and display name in the browser catalog.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and Win32 Debug/Release.
- `AppFrameworkTests` passed in all four configurations, including browser search/input/paint checks and the new complete catalog mapping assertion.
- x64 Debug `AppFrameworkTests` also passed on the first focused build before the four-configuration sweep.
- `git diff --check` passed; only configured LF-to-CRLF notices were reported.

Next: continue decomposing `ControlPanel`'s session state and main/settings composition, keeping platform services and algorithm catalog data behind their current adapters.

## 2026-10-20 — isolate simulation-to-screen presentation adapter

Status: implementation and four-configuration build/test verification complete.

- Removed the SortingLogic and UserSettings includes from `UiArchitecture.h`. General layout, overlay, and paint consumers now see only UI-owned presentation types instead of inheriting the sorting engine's full public contract.
- Moved `buildUiScreenModel` and main-array role projection into `UiScreenModelAdapter`, which is the explicit boundary that translates snapshots and saved preferences into view data.
- Kept the same model fields, labels, role projection, duration calculation, and transport states. Existing screen-model and chart role checks continue to exercise the adapter.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and x86 Debug/Release (project configuration `Win32`).
- `AppFrameworkTests` passed in all four configurations.
- x64 Debug `SortingLogicTests --core-contracts-only` passed 39,535 checks with 0 failures.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Next: continue moving screen/session ownership out of `ControlPanel`; prefer an owner that carries state and action handling, not just another rendering wrapper.

## 2026-10-20 — extract initial selection strategies

Status: implementation complete; full-catalog regression run and four-configuration builds passed.

- Moved Bad Sort, Cycle Sort, and Stable Cycle Sort unchanged into `SelectionStrategies.cpp`, where they use the same internal `Recorder` interface as the existing strategy modules.
- Registered all three with direct strategy builders and removed their now-dead cases from the fallback algorithm switch. Their descriptor facts, IDs, event order, metrics, and trace finalization remain unchanged.
- Updated the ignored structural audit with the current SortingLogic boundaries and corrected the stale 2026-10-19 source locations.

Verification:

- Full x64 Debug `SortingLogicTests` passed: 16,270,491 checks, 0 failures, including catalog-wide replay reconstruction.
- After removing the dead switch cases, x64 Debug solution build passed; focused Bad Sort and Stable Cycle suites passed 322,101 and 249,474 checks respectively, with 0 failures.
- Full Visual Studio solution builds passed in x64 Debug/Release and x86 Debug/Release (`Win32` project configuration).
- `git diff --check` passed; configured LF-to-CRLF notices only.

Next: extract the remaining selection and heap kernels behind the same recorder contract, keeping each family batch independently reviewable; then continue through the fallback dispatch and catalog boundary.

## 2026-10-20 — remove Release framework PDB warnings

Status: implementation and warning/performance verification complete.

- Disabled whole-program/LTCG compilation only for the `AppFramework` Release static library. Release compiler optimization remains enabled; the app's own translation units and the sorting/audio libraries retain their existing optimization settings.
- This prevents the final linker from consuming the framework's LTCG PDB type records, which triggered repeated `LNK4020` warnings. x64 Release and Win32 Release now link without those warnings.
- The tradeoff is no cross-module LTCG optimization across the AppFramework boundary. The existing 800-item x64 Release render-cost profile showed no measurable regression: fresh snapshot median 41.030 -> 36.268 us; reused snapshot 15.426 -> 15.440 us; role scan/index medians 0.897/0.142 -> 0.686/0.103 ms per 800 items.

Verification:

- x64 Release and Win32 Release solution builds passed without `LNK4020` output.
- `AppFrameworkTests` and `SortingLogicTests --core-contracts-only` passed in both Release configurations; SortingLogic reported 39,535 checks and 0 failures.
- `AppFrameworkTests --render-cost-profile` completed successfully in x64 Release.
- `git diff --check` passed; configured LF-to-CRLF notices only.

Next: continue the remaining UI/rendering ownership items, then revisit any performance boundary only when a representative profile shows meaningful cost.


## 2026-10-20 — extract Improved Block Selection Sort

Status: implementation and focused verification complete; ready for review.

- Moved `improvedBlockSelectionSort` unchanged from `SortingLogic.cpp` into `SelectionStrategies.cpp`, keeping the same `Recorder` operations, metrics, phases, comparisons, and mutation order.
- Routed its stable existing algorithm ID through the direct strategy request builder and removed the dead fallback-switch implementation. Catalog metadata and user-visible behavior are unchanged.
- This is an ownership and source-organization change only; it makes no runtime or performance claim.

Verification:

- Full Visual Studio solution builds passed for x86 Debug, x86 Release, x64 Debug, and x64 Release (32-bit project outputs are under `Win32`).
- Focused Improved Block Selection suite passed 534,724 checks with 0 failures.
- SortingLogic core contracts passed 39,535 checks with 0 failures.
- `git diff --check` passed.

Next: continue extracting cohesive recorder-only algorithm families from `SortingLogic.cpp`, first checking helper dependencies and existing focused tests so shared kernels move together rather than being copied into new silos.

## 2026-10-20 — extract pancake strategy family

Status: implementation and focused verification complete; ready for review.

- Moved Pancake Sort, ArrayV Burnt Pancake, Canonical Burnt Pancake, Pancake Insertion, and their shared flip/search helpers into `PancakeStrategies.cpp`.
- Routed the four stable algorithm IDs through direct strategy builders and removed their fallback-switch cases. Recorder operation order, phase labels, orientation mutations, annotations, and catalog metadata were retained.
- Added a focused regression command for the pancake family covering empty, singleton, duplicate, all-equal, sorted, reversed, and randomized inputs in both directions; burnt variants also verify upright orientation and exact rewind.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused Pancake family tests passed 7,087 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. Each reported 0 failures.
- `git diff --check` passed.

Next: continue removing cohesive algorithm families from the central implementation file; inspect shared helper ownership before moving the remaining exchange and heap kernels.

## 2026-10-20 — extract elementary exchange variants

Status: implementation and focused verification complete; ready for review.

- Moved Binary Gnome, Optimized Gnome, Shove, Slope, and Hybrid Comb Sort into the existing `ElementaryStrategies.cpp` module.
- Routed their stable IDs through direct strategy builders and removed their fallback-switch cases. Hybrid Comb continues to call the shared Insertion strategy; the gap schedule, metrics, annotations, and operation sequence are unchanged.
- Added a focused suite for empty, singleton, duplicate, all-equal, ordered, and randomized inputs in both directions, plus the existing Hybrid Comb and Intro Circle checks.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused elementary exchange tests passed 37,445 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. All reported 0 failures.
- `git diff --check` passed.

Next: address the Circle/Intro Circle dependency on the shared binary insertion range before moving that family; then continue with remaining heap and tree ownership boundaries.

## 2026-10-20 — extract shared binary insertion kernel

Status: implementation and focused verification complete; ready for review.

- Moved Binary Insertion Sort and its reusable `binaryInsertionRange` kernel into `InsertionStrategy.cpp`.
- Routed the algorithm's stable ID through the strategy builder. Qualified all 40 remaining call sites in `SortingLogic.cpp` through the strategy boundary; the helper body and each caller's parameters are unchanged.
- This gives Intro Circle and the many merge/hybrid strategies one shared insertion kernel rather than a hidden dependency on the monolithic translation unit.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused Binary Insertion tests passed 2,639 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. All reported 0 failures.
- `git diff --check` passed.

Next: move Circle and Intro Circle with their shared recursion helper now that their binary-insertion dependency is an explicit strategy API; then continue the remaining exchange and heap boundaries.

## 2026-10-20 — extract Circle Sort variants

Status: implementation and focused verification complete; ready for review.

- Moved the shared power-of-two and recursive-pass helpers, iterative/recursive Circle Sort, and both Intro Circle variants into `CircleStrategies.cpp`.
- Added thin strategy entry points for the four catalog variants, routed their stable IDs directly, and removed their fallback cases. Intro Circle uses the shared Binary Insertion range kernel through the new strategy API.
- Kept the existing schedules, comparison order, swap order, metrics, and fallback threshold unchanged.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused Circle family tests passed 37,093 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. All reported 0 failures.
- `git diff --check` passed.

Next: continue extracting recorder-only algorithm groups while preserving their shared helpers and direct registration boundaries; the central implementation still contains substantial heap, tree, merge, and partition code.

## 2026-10-20 — extract heap strategy family

Status: implementation and focused verification complete; ready for review.

- Moved the 13 heap catalog variants and their private heap helpers into `HeapStrategies.cpp`.
- Added thin entry points for ternary, configurable Base-N, min-heap, and flipped min-heap modes. Base-N reads `heapArity` from the replay trace configuration, preserving the existing typed-option behavior.
- Routed every heap stable ID through direct strategy builders and removed the heap cases from the central fallback switch. Auxiliary lanes, metrics, comparison order, and mutation behavior were retained.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused Heap family tests passed 14,105 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. All reported 0 failures; core contracts include all five Base-N arities.
- `git diff --check` passed.

Next: continue extracting tree and partition families; keep shared fallbacks explicit and avoid turning the strategy header into a dumping ground for unrelated implementation details.

## 2026-10-20 — extract tree strategies and shared permutation emission

Status: implementation and focused verification complete; ready for review.

- Moved unbalanced, AA, AVL, Splay, and Red-Black tree variants plus their private node/link/rotation helpers into `TreeStrategies.cpp`.
- Moved the shared sorted-index-to-replayable-permutation emitter into `PermutationTraceUtilities` because both tree extraction and Strand Sort use it. Tree/Strand event order, lane operations, and mutation behavior remain unchanged.
- Routed the six tree catalog IDs through direct strategy builders and removed their fallback cases. Strand Sort keeps its existing registration and now calls the shared detail utility.

Verification:

- Full Visual Studio solution builds passed for x86 Debug/Release and x64 Debug/Release.
- Focused tree/Strand tests passed 22,786 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks. All reported 0 failures.
- `git diff --check` passed.

Next: continue with partition and merge ownership boundaries; keep the generic recorder helper surface narrow and split the strategy declarations by family if the shared header begins to obscure dependencies.

## 2026-10-20 — extract Quick partition strategies

Status: implementation and focused verification complete; ready for review.

- Moved the standard midpoint Quick, left-left, left-right, dual-pivot, stable, forced-stable, and ternary Quick variants into `PartitionStrategies.cpp`.
- Added thin unary strategy entry points for configuration choices that were previously represented by boolean parameters. Eleven stable algorithm IDs now dispatch directly to their extracted strategy builders; their catalog descriptors, options, seeds, traces, and fallback behavior are unchanged.
- Added `--partition-quick-only` to compose the existing pointer, dual-pivot, stable, and ternary Quick regressions.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- Focused Quick-family tests passed 106,359 checks; pinned catalog coverage passed 1,741 checks; SortingLogic core contracts passed 39,535 checks in both Debug and Release. All reported 0 failures.
- `git diff --check` passed. `SortingLogic.cpp` is now 8,694 physical lines, down 502 lines from the tree extraction checkpoint. This records source relocation, not runtime speed.

Next: continue with the remaining partition hybrids and merge strategies. Keep shared insertion/heap kernels explicit, and extract merge helpers only with the strategies that own their trace conventions.
## 2026-10-20 — extract stackless partition strategies

Status: implementation and focused verification complete; ready for review.

- Moved Stackless Hybrid Quick Sort and Stackless Dual-Pivot Quick Sort, including their local scan, partition, binary-insertion, and duplicate-skip helpers, into `StacklessPartitionStrategies.cpp`.
- Added direct strategy entry points and removed both algorithms from the central fallback switch. Their catalog IDs, operation streams, metrics, and phase annotations are unchanged.
- Added `--stackless-partition-only` to run the existing focused correctness suites together.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- Focused stackless partition tests passed 668,471 checks in Debug and Release; catalog coverage passed 1,741 checks and core contracts passed 39,535 checks in both configurations. All reported 0 failures.
- `git diff --check` passed. `SortingLogic.cpp` is now 8,391 physical lines, down 303 lines from the preceding Quick partition slice. This measures code relocation, not runtime speed.

Next: move shared IntroSort/PDQ partition kernels behind a sorting-internal boundary, then continue extracting the larger merge implementations with their actual shared buffer kernels.
## 2026-10-20 — extract shared partition kernels, IntroSort, and PDQ

Status: implementation and bounded regression verification complete; the full 800-item PDQ stress fixture remains unverified in this slice.

- Moved median-of-three selection, the traced Hoare partition, and heap-sort range fallback into `PartitionTraceUtilities.cpp/.h` under `sortinglogic::detail`. The helper bodies and recorder operations were moved without algorithmic edits; `siftDown` has internal linkage.
- Moved IntroSort and both PDQ variants into `PartitionStrategies.cpp`, added direct strategy registration, and removed their fallback-switch cases. Drop Merge, Median Merge, and Buffer Partition Merge now call the same shared utility boundary explicitly.
- Added `--pdq-smoke-only`, which runs the existing boundary/duplicate matrix while omitting only the two 800-item patterns. The original `--pdq-only` full suite remains unchanged by default.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: IntroSort 38,165 checks; PDQ boundary smoke 88,015; Drop Merge 3,341; Median Merge 2,873; Buffer Partition Merge 114,897; catalog coverage 1,741; core contracts 39,535. All completed suites reported 0 failures.
- The full `--pdq-only` Debug suite was stopped after memory rose to approximately 780 MiB while validating its two 800-item trace/replay cases. That stress result is not reported as passing; the bounded PDQ cases and catalog coverage passed.
- `git diff --check` and a final x64 Debug rebuild passed. `SortingLogic.cpp` is now 8,151 physical lines, down 240 from the preceding stackless partition slice. This is source relocation, not a runtime speed claim.

Next: continue extracting merge families around explicit shared buffer/rotation kernels, then return to the configuration and UI ownership boundaries in the audit.
## 2026-10-20 — isolate the baseline Merge strategy and shared merge-range writer

Status: implementation and focused verification complete; ready for review.

- Moved the configurable bottom-up/top-down Merge Sort implementation into `MergeStrategies.cpp` and registered the stable Merge ID directly.
- Moved the shared merge-range recorder operation into `MergeTraceUtilities.cpp/.h`. Binary/iterative merge variants, ECTA, Median Merge, Buffer Partition Merge, and the other existing users now call the same `detail::traceMergeRange` helper.
- Preserved the configured atomic-range versus animated-item writeback behavior and the existing merge annotations, comparisons, metrics, and event ordering. The implementation body changed only its helper qualification after the move.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: core contracts 39,535 checks; merge-family variants 44,531; Iterative Top-down Merge 228,782; ECTA 120,305; Median Merge 2,873; Buffer Partition Merge 114,897; catalog coverage 1,741. All reported 0 failures.
- `git diff --check` passed. `SortingLogic.cpp` is now 8,085 physical lines, down 66 lines from the preceding partition-kernel slice. This is a source ownership change, not a runtime speed claim.

Next: continue extracting cohesive merge implementations and their shared buffer/rotation kernels, then resume the configuration and view-ownership items in the audit.

## 2026-10-20 — extract Binary and Iterative Top-down Merge strategies

Status: implementation and focused verification complete; ready for review.

- Moved Binary Merge Sort and Iterative Top-down Merge Sort into `SortingLogic/MergeStrategies.cpp` and registered both through direct strategy builders.
- Binary Merge continues to call the shared Binary Insertion range strategy and `detail::traceMergeRange`; Iterative Top-down Merge retains its proportional subranges, merge-layer phases, work charges, and metrics.
- Removed both implementations from the central fallback switch. Stable IDs, catalog descriptors, workspace usage, event order, and rewind behavior are unchanged.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: core contracts passed 39,535 checks; merge-family tests passed 44,531; Iterative Top-down Merge passed 228,782; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: extract the in-place merge strategy, then continue through merge kernels with dedicated existing replay suites.

## 2026-10-20 — extract In-Place Merge Sort

Status: implementation and focused verification complete; ready for review.

- Moved In-Place Merge Sort and its recursive range worker into `SortingLogic/MergeStrategies.cpp`, registered through a direct strategy builder, and removed its fallback-switch case.
- Kept the existing rotation-by-swapping procedure, comparison roles, work charges, phase annotations, metrics, and event sequence intact. The catalog ID and its in-place/unstable metadata remain unchanged.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: In-Place Merge passed 39,872 checks; core contracts passed 39,535; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: extract the parallel and rotation-based merge strategies with their existing replay suites, then resume the configuration and view-ownership items in the audit.

## 2026-10-20 — extract Parallel Merge Sort

Status: implementation and focused verification complete; ready for review.

- Moved Parallel Merge Sort and its recursive task-phase worker into `SortingLogic/MergeStrategies.cpp`; its catalog request now uses a direct strategy builder and its fallback-switch case is removed.
- Preserved the deterministic sibling-task phases, merge-range writer calls, work charges, task/pass/depth metrics, and event ordering. “Parallel” remains a trace presentation schedule; this move does not add worker threads to the simulation.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: Parallel Merge passed 66,197 checks; core contracts passed 39,535; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: inspect and extract the rotation-based merge strategies as a cohesive module, then resume the configuration and view-ownership items in the audit.

## 2026-10-20 — extract rotation-based Merge strategies

Status: implementation and focused verification complete; ready for review.

- Moved the shared Rotate Merge implementation into `SortingLogic/RotationMergeStrategies.cpp`. The ordinary and deterministic-parallel catalog entries now use separate direct strategy wrappers over the same body.
- Preserved binary-search comparisons, block-swap rotation, work accounting, phase schedules, metrics, annotations, stable IDs, and event order. No change was made to simulation threading or run behavior.
- Added the module to the existing Visual Studio static library project; the central fallback switch no longer dispatches either variant.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: Rotate Merge passed 478,954 checks; Parallel Rotate Merge passed 151,845; core contracts passed 39,535; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: extract Stackless Rotate Merge with its dedicated suite, then review remaining merge modules and return to the UI/configuration ownership findings.

## 2026-10-20 — extract Stackless Rotate Merge

Status: implementation and focused verification complete; ready for review.

- Moved Stackless Rotate Merge and its partition search, block rotation, and pass workers into `SortingLogic/RotationMergeStrategies.cpp` alongside the related rotate variants.
- Registered its stable catalog ID directly and removed the fallback-switch case. The implementation remains separate from recursive Rotate Merge and retains its own partition trace, work accounting, metrics, and annotation schedule.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: Stackless Rotate Merge passed 386,074 checks; core contracts passed 39,535; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: audit remaining merge implementations for the next cohesive extraction, then return to settings/configuration and UI ownership work from the structural audit.

## 2026-10-20 — extract Optimized Bottom-up Merge

Status: implementation and focused verification complete; ready for review.

- Moved Optimized Bottom-up Merge and its alternating-buffer pass helper into `SortingLogic/MergeStrategies.cpp`; the catalog request now uses a direct strategy builder and the fallback-switch case is removed.
- Preserved 16-item binary-insertion run formation, alternating main/auxiliary passes, lane names, comparison/write order, metrics, and annotations. The shared binary-insertion kernel remains owned by the insertion strategy module.

Verification:

- Full Visual Studio solution builds passed for x86/x64 Debug and Release.
- In each configuration: merge-family tests passed 44,531 checks; core contracts passed 39,535; pinned catalog coverage passed 1,741. All reported 0 failures.
- `git diff --check` passed. This is a source ownership change, not a runtime speed claim.

Next: continue auditing remaining cross-family merge helpers, then resume the settings/configuration, UI ownership, and measured rendering items in the structural audit.

## 2026-10-20 — extract ECTA and shared stable k-way merge

ECTA Sort now lives in `SortingLogic/MergeStrategies.cpp` and is registered through its direct strategy builder. The stable k-way merge recorder moved into `MergeTraceUtilities` and is shared by ECTA, Remi, Kota, Chalice, Fifth, and Quad merge paths. The fallback switch no longer owns ECTA dispatch.

The extraction preserves the existing cursor-counter lane, comparison/probe roles, work charges, phase markers, annotations, metrics, workspace writes, and event order. It is an ownership refactor, not a claim of faster execution.

Verification:

- Full Visual Studio solution builds passed in x86/x64 Debug and Release.
- In every configuration, focused ECTA tests passed 120,305 checks; Remi passed 3,789; Fifth/Quad multiway merge passed 189,394; merge-roster tail passed 17,048; core contracts passed 39,535; and pinned-catalog coverage passed 1,741. All suites reported 0 failures.
- `git diff --check` passed.

Next: continue with the remaining sorting-core concentration only where a cohesive ownership boundary exists; otherwise move to the audit's settings/configuration and UI ownership findings rather than splitting files mechanically.

## 2026-10-20 — extract the algorithm profile view

Moved the responsive algorithm profile card, wrapped text layout, viewport state, wheel handling, and scrollbar capture/drag into `SDL2BarGraphics/AlgorithmProfileView`. It consumes `UiAlgorithmCapabilities` and logical geometry, emits shared `UiPaintCommand`s, and has no SDL or SortingLogic dependency. `ControlPanel` now delegates profile composition and pointer/wheel behavior to the view.

Extended the paint command contract with text alignment and overflow policy so the profile's left-aligned, clipped paragraphs retain their existing appearance. The shared command adapter maps the added main-control/text layers and timeline materials to the existing renderer pipeline; no new renderer or material state was introduced.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and x86 Debug/Release (`x86` is the solution platform label for Win32).
- `AppFrameworkTests` passed in all four configurations. New headless checks cover responsive visibility, paint layers, clipped/left-aligned body text, wheel hit testing, scrollbar page/drag behavior, and resetting scroll on algorithm changes.
- `git diff --check` passed. No live-window screenshot review was run for this slice.

Next: continue extracting actual view/session ownership from `ControlPanel`, starting with the appearance editor, while preserving the current modal and draft/apply/cancel behavior.

## 2026-10-20 — consolidate appearance editor session state

Moved the appearance editor's original preferences, editable draft, token-selection model, and feedback string into one `AppearanceEditorSession`. `ControlPanel` now begins a session on open, restores its baseline on Cancel, and advances the baseline only after the app reports a successful Apply. SDL event handling, rendering, and native profile dialogs remain in `ControlPanel` for a later view extraction.

Verification:

- Full Visual Studio solution builds passed in x64 Debug/Release and x86 Debug/Release.
- `AppFrameworkTests` passed in all four configurations. New coverage checks session begin, draft token editing, cancel restoration, error clearing, and Apply baseline advancement.
- `git diff --check` passed. No live-window screenshot review was run because this slice changes state ownership only.

Next: extract appearance editor layout, paint composition, and widget action handling behind a dedicated view while keeping native file dialogs in the platform adapter.

## 2026-10-20 — extract the appearance editor view boundary

Added `AppearanceEditorView` to own the retained editor controls, `AppearanceEditorSession`, responsive layout result, description text, and painter-ordered modal composition. `ControlPanel` delegates editor creation, layout, and modal chrome/swatch/status composition to the view. It remains the SDL and renderer adapter for pointer/key translation, specialized widget painting, native profile dialogs, and app action dispatch; those responsibilities are the next extraction boundary rather than something this slice pretends has vanished.

Added the shared `SettingsControls` paint layer so the view can order its swatch alongside controls. New headless checks cover control/layout ownership, modal paint order and clipping, status/error styling, and outside-click input capture. Removed an unused palette field from the view contract.

Verification:

- Full x64 Debug/Release and x86 Debug/Release solution builds and `AppFrameworkTests` passed for this slice. After removing the unused palette field, the full x64 Debug solution build and `AppFrameworkTests` were rerun and passed.
- Visually inspected the live appearance editor and settings window; the modal, controls, swatch, description, and status were visible and correctly layered.
- `git diff --check` passed. This is an ownership refactor; no runtime performance claim is made.

Next: move appearance-editor widget-state synchronization and semantic action handling behind the view. Keep SDL event translation, GPU submission, and native file dialogs at the platform adapter boundary.

## 2026-10-20 — move appearance control synchronization into the view

`AppearanceEditorView::synchronizeControls` now maps its token model and draft into the retained controls: filtered token options, selected preset/palette/category, search label, type-specific visibility, values, descriptions, and font-family/style options. The platform adapter supplies installed-font choices through callbacks and retains only the renderer-owned `TextLabel` cache and resize/layout call. This removes the duplicated widget-state synchronization block from `ControlPanel` without moving DirectWrite or SDL responsibilities into the view.

An empty token search now keeps its empty-result message and skips type-specific value handling when no token descriptor exists. The test exercises each token type available across the built-in presets and the no-results case.

Verification:

- Full solution builds and `AppFrameworkTests` passed in x64 Debug/Release and x86 Debug/Release.
- Visually inspected the live appearance editor after synchronization moved; the default color token, swatch, sliders, labels, and modal layout render correctly.
- `git diff --check` passed. No settings persistence, appearance-edit semantics, or renderer behavior was changed.

Next: move pure appearance-token edit actions into the view and make its boundary return explicit host effects for inline text entry, color/profile dialogs, Apply, and Cancel. SDL event translation, native dialogs, and app-controller dispatch remain in the host adapter.

## 2026-10-20 — move appearance edit actions into the view

`AppearanceEditorView::handleAction` now owns preset, palette, category, token, scalar, color, boolean, choice, font, reset, import/export, and Apply/Cancel action semantics. It returns explicit synchronization requirements and host effects; `ControlPanel` handles only inline text entry, native color/profile dialogs, modal closing, and `AppAction` dispatch. Font enumeration remains supplied by the host. Empty-search token reset remains a no-op, matching the prior behavior.

Verification:

- Full Visual Studio solution builds passed for x64/x86 Debug and Release. The x64/x86 Debug/Release configuration labels were used as defined by the solution.
- `AppFrameworkTests` passed in all four configurations, including the new action-dispatch coverage and empty-search reset case.
- `git diff --check` passed. No live-window screenshot review was run because the widget layout and painting are unchanged; the action semantics are exercised headlessly.
- This is an ownership refactor. No performance improvement is claimed.

Next: extract the remaining inline appearance editor input state/commit logic into a small platform-neutral model, or move to the audit's settings/configuration ownership boundary if the inline editor proves inseparable from SDL text input. Keep the host responsible for SDL event translation and native dialogs.

## 2026-10-20 — extract inline edit state and parsing

Added `InlineEditModel` to the existing `AppFramework` library. It owns the active edit target, accepted-character rules, first-character replacement, the ten-character limit, backspace/clear behavior, and strict integer/float/double parsing. `ControlPanel` now maps targets to widget IDs, translates SDL text/key events, applies values to appearance preferences or `AppAction`s, and restores labels on cancel. This keeps SDL, renderer, and settings ownership out of the model while removing four pieces of mutable input state and parser logic from the facade.

Verification:

- Full Visual Studio solution builds passed in x64/x86 Debug and Release.
- `AppFrameworkTests` passed in all four configurations. New checks cover target-specific character filtering, replacement behavior, overflow and malformed numeric input, finite parsing, and the ten-character limit.
- `git diff --check` passed. No visual layout changed, so no screenshot review was run. No performance improvement is claimed.

Next: extract main/settings view ownership and assess whether their runtime paint and action paths can use the existing shared paint/action contracts without introducing a second abstraction.

## 2026-10-20 — isolate settings catalog translation

`SettingsPanelControls` no longer accepts `SortingLogic::AlgorithmDescriptor` or imports sorting/audio policy. It consumes `SettingsPanelAlgorithmPresentation`, a UI-owned value with stable family/variant IDs, option groups, Merge-buffer visibility, and visible audio-control IDs. The new `SettingsPanelCatalog` adapter performs the one-way mapping from the algorithm registry and semantic audio capabilities. `ControlPanel` supplies that presentation when laying out the selected algorithm's settings.

Verification:

- Full Visual Studio solution builds passed in x64/x86 Debug and Release.
- `AppFrameworkTests` passed in all four configurations. Settings catalog assertions cover Insertion, Merge, Counting, unknown metadata, and both General Audio groups.
- `git diff --check` passed. Control IDs, filtering order, and visible options are unchanged; no runtime performance claim is made.

Next: extract ownership of the main/settings widget roots and their layout/action state through the existing paint/action contracts, keeping renderer and SDL adapters at the boundary.

## 2026-10-20 — move settings root and navigation ownership into the view

`SettingsPanelView` now owns the retained settings `UIRoot`, settings page/audio-group and scroll model, and settings captions. It handles typed navigation commands and settings-wheel scrolling. `ControlPanel` routes those events to the view and no longer stores the settings root, navigation model, or caption map; it remains responsible for SDL translation, control construction and state synchronization, responsive placement, renderer submission, and modal lifetime.

Added view-level checks for initial page behavior, typed navigation, audio subpage preservation, inside/outside scroll hit testing, scroll reset on page changes, and leaving modal opening to the overlay coordinator.

Verification:

- Full Visual Studio solution builds passed in x64/x86 Debug and Release.
- `AppFrameworkTests` passed in all four configurations.
- `git diff --check` passed. Control geometry and paint order did not change; no performance improvement is claimed.

Next: move settings control construction, page layout/visibility, state synchronization, and shared-command paint composition into the view using prepared catalog data; then isolate the main-control root.

## 2026-10-20 — move settings layout and visibility into the view

`SettingsPanelView` now owns settings-panel/content bounds, container and modal bounds, tab/subpage geometry, responsive row placement, active-control visibility, popup placement, and clipped scroll configuration. `ControlPanel` supplies only the active settings control IDs derived from the UI-owned algorithm presentation and the theme scrollbar thickness. The panel container is laid out on screen-layout cache invalidation; cached-frame preparation only updates page control geometry and no longer reissues the container layout.

Added view-level layout assertions for active/inactive widgets, Appearance and General Audio content offsets, audio subpage/retry visibility, and long-list scrolling. Live Computer Use inspection covered Appearance, Algorithm, General Audio Basic and Advanced, and scrolling down to the final Advanced Audio control; the modal tabs and footer stayed fixed.

Verification:

- Full Visual Studio solution builds passed in x64/x86 Debug and Release.
- `AppFrameworkTests` passed in all four configurations.
- `git diff --check` passed. No performance improvement is claimed; frame/layout cost has not been benchmarked.

Next: move settings-control value/enabled-state synchronization behind `SettingsPanelView`, then submit settings tab/content paint through the existing `UiPaintCommand` path.

## 2026-10-20 — move settings control construction into the view

`SettingsPanelView::initializeControls` now creates the settings tabs, appearance and simulation selectors, catalog-derived algorithm option controls, reconstructed-audio controls, per-variant audio controls, captions, and retained panel container from `UserPreferences`. `ControlPanel::createWidgets` now initializes the view rather than constructing its widget tree. Renderer-owned `TextLabel` resources remain in the host adapter and are created from the completed view tree.

Centralized palette, preset, spacing, domain, pattern, and boolean option labels in `SettingsPanelCatalog`. Palette names and order now come from renderer-independent `UiAppearance` metadata, keeping GLM out of the headless settings catalog and avoiding a rendering-header dependency for UI choices.

Added headless checks for initialized selection values, typed action payloads, algorithm option coverage, and palette label order. The full Visual Studio solution built in x64 Debug/Release and x86-mapped Win32 Debug/Release. `AppFrameworkTests` passed in all four configurations. The initial x64 Debug compile caught the GLM dependency leak; the pure appearance catalog extraction fixed it. `git diff --check` passes. No visual or performance change is claimed.

Next: move settings control painting through the existing `UiPaintCommand` contract, then isolate main-control ownership.

## 2026-10-20 — move settings state synchronization into the view

`SettingsPanelView::synchronizeControls` now owns saved selector values, catalog-derived algorithm option selections/actions and busy gating, pattern compatibility, and reconstructed-audio/per-variant gain control synchronization. `ControlPanel` no longer decides those widget values or enabled states. The main panel and settings panel now call the same `settingsInputPatternEnabled` compatibility rule. Per-variant audio sliders use 0.5 dB steps to preserve stored half-decibel overrides.

Added headless checks for busy-state enablement, compatibility-disabled patterns, option selection, reconstructed-audio dependent controls, inherited family levels, variant offsets, and selected-algorithm action IDs. The first run caught whole-decibel slider quantization of a stored 3.5 dB value; the step was corrected and the checks now pass.

Verification: all four Visual Studio solution builds and `AppFrameworkTests` passed. `SoundEngineTests` passed with 417,376 assertions in x64 Debug. `SortingLogicTests` was not completed; duplicate runs were stopped because this settings-only change does not touch the sorting library. `git diff --check` passes. No layout or painter-order changes and no performance claim.

Next: rerun the four-configuration matrix for the combined settings-view change, then move main-control state and composition behind its view boundary.

## 2026-10-20 — compose settings controls through the shared paint contract

`SettingsPanelView::compose` now emits ordered control commands for tabs, the active marker, audio sub-tabs, visible rows, modal buttons, and expanded dropdowns. Commands carry stable widget IDs, render layer, clip bounds, and popup state. `ControlPanel` adapts each command through the existing preset-aware button, slider, combo, and popup rendering routines, so renderer-owned labels and theme-specific drawing remain intact. The view now determines which controls are painted and in what order.

Added view composition checks for Appearance control order/clipping and active-tab state, popup routing, and General Audio control visibility. Full solution builds and `AppFrameworkTests` passed in Win32/x64 Debug and Release. Live inspection confirmed the Algorithm settings controls and Appearance palette popup still render. No layout or painter-order change is intended and no performance gain is claimed.

Next: extract main-control state/composition behind the same view and command boundaries.

## 2026-10-20 — move main widget construction into a view

Added `MainControlsView` as the owner of the retained main-control `UIRoot`, initial controls, typed app actions, and semantic widget roles. `ControlPanel` now initializes that view and still owns renderer `TextLabel` resources, SDL event translation, layout, and painting. A temporary non-owning `mainUi_` alias keeps the existing input/layout/paint paths stable while those responsibilities move incrementally.

Added headless checks for selected algorithm label/action, direction and input selections, typed domain/pattern commands, shuffle routing, build-trace role, and disabled menu heading. `AppFrameworkTests` passed in x64 Debug and Release. The full x64 Release solution build passed. The x64 Debug solution compiled all targets but the final app link hit LNK1168 because the currently running app held the normal output executable open; rebuilding the app project into a separate verification directory linked successfully. `git diff --check` passed. No visible behavior or performance change is claimed.

Next: move main-control state synchronization and visibility behind `MainControlsView`, then submit its ordered control composition through `UiPaintCommand`.

## 2026-10-20 — move main-control synchronization into the view

`MainControlsView::synchronize` now applies the prepared screen model and render snapshot to the main controls: setup busy gating, transport labels, current algorithm/input/count/seed/duration values, collapsed start-overlay state, reference-cadence duration locks, preset-specific step text, menu enablement, and input-pattern compatibility. `ControlPanel` still creates the screen model and coordinates settings-panel synchronization, layout, and inline-edit labels.

Added checks for busy setup versus available transport, selected algorithm/direction and run readouts, reference-cadence timing controls, and the collapsed Classic Windows 95 start/step presentation. `AppFrameworkTests` passed in x64 Debug and Release; the app linked in x64 Debug to an isolated output and the full x64 Release solution built. `git diff --check` passed. The state-only move does not change geometry or painter order; no performance gain is claimed.

Next: move main-control paint ordering through `UiPaintCommand`, preserving `ControlPanel`'s theme-specific widget renderer and text resources.

## 2026-10-20 — route main control composition through paint commands

`MainControlsView::compose` now emits control commands in explicit header-button, menu, expanded-control, collapsed-start, and popup sections. Each command carries a stable widget ID, screen clip, semantic UI layer, and popup flag. `ControlPanel` adapts these commands through its existing `drawWidget` path, preserving preset-specific style, label resources, and the existing chart/timeline/profile draw order. Main dropdowns use a distinct shared-paint layer mapped to the established renderer dropdown layer.

Headless checks cover section ordering, visibility filtering, clipped popup commands, and dropdown layer selection. `AppFrameworkTests` passed in x64 Debug and Release; the app linked in x64 Debug to a separate output, and the full x64 Release solution built. A live Release smoke view showed the main panel and chart rendering without an obvious layering regression. `git diff --check` passed. This is a testability/ownership change, not a measured render optimization.

Next: move the remaining main-panel composition decisions (title/telemetry/profile/timeline section metadata where useful) behind the view without relocating renderer-specific drawing, then continue the structural audit's sorting and configuration boundaries.

## 2026-10-20 — move main-control layout state into the view

`MainControlsView::applyLayout` now owns main-control focus order, widget bounds, direction and editable-label presentation, collapsed/expanded visibility, preset-specific menu/run visibility, wide-layout input controls, and the panel-toggle caption. `ControlPanel` still owns settings-panel geometry and screen-level chart/timeline placement, and adapts the resulting view tree to existing renderers.

Headless checks cover current, Flat VGA DOS, and Classic Windows 95 layouts in expanded and collapsed states. `AppFrameworkTests` passed in x64 Debug and Release; the x64 Debug app linked to an isolated output directory and the full x64 Release solution built in an isolated output directory. `git diff --check` passed. The user closed the earlier smoke window; no fresh visual claim is made for this layout-only move.

Next: move remaining main-panel composition metadata (title, telemetry, profile, and timeline where useful) behind the view without relocating renderer-specific drawing, then continue the sorting-core and configuration boundaries. The goal remains active; no performance improvement is claimed.

## 2026-10-20 — move main text presentation into the view

`MainControlsView` now formats the title, algorithm/input facts, trace timing, live statistics, status, and collapsed progress summary. It owns the existing 200 ms live-text throttle. `ControlPanel` adapts the prepared strings to its renderer-owned `TextLabel` objects; text synchronization now happens in `prepareFrame`, while `render` only paints the prepared frame.

Added headless checks for current-preset content, collapsed progress, live update throttling, and immediate title changes across presets. `AppFrameworkTests` passed in x64 Debug and Release; full x64 Debug and Release solution builds passed. `git diff --check` passed. No font-raster or whole-frame performance claim is made.

Next: move remaining main-panel composition metadata (profile, timeline, and section ordering where useful) behind view contracts while keeping GPU-specific drawing in the host; then continue the sorting-core and configuration boundaries.
