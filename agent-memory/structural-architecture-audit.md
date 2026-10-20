# Structural architecture audit and refactoring proposal

Date: 2026-10-19. Runtime baseline: `1b0a601` on `development`.

This is a static architecture review of the current C++14/Visual Studio application. It examines ownership, dependencies, duplicated decisions, UI composition, trace storage, build structure, and regression boundaries. It is not a new certification of every sorting implementation or a measured performance benchmark. This change set changes guidance/documentation and ignore rules only.

## Assessment

The existing separation into `SortingLogic`, `SoundEngine`, and the SDL/OpenGL application is worth keeping. The expensive structural debt is inside those boundaries: sorting is concentrated in one translation unit, the UI facade owns multiple screens and rendering details, and configuration/presentation rules are repeated across layers.

A behavior-preserving wave should first make the existing contracts explicit, then extract implementations behind them. Preserving final sorted output alone is insufficient: operation order determines animation, statistics, duration, and sound.

### Size and concentration

Counts include blank lines and were measured from tracked source at the runtime baseline. Production here means top-level `.cpp`/`.h` files in the three main projects, including retained sound experiments.

| Source | Physical lines | Responsibilities concentrated there |
| --- | ---: | --- |
| `SortingLogic/SortingLogic.cpp` | 14,989 | Recorder, algorithm implementations, dispatch, catalog, replay, input generation, simulation |
| `SDL2BarGraphics/ControlPanel.cpp` | 3,609 | Widget creation, input routing, main/settings/browser/editor views, layout, text, painting, Windows dialogs |
| `SDL2BarGraphics/UserSettings.cpp` | 1,162 | INI parsing/writing, migration, defaults, validity, audio-level policy, option resolution, Win32 storage |
| `SDL2BarGraphics/AppController.cpp` | 829 | Actions, settings edits, trace jobs, playback readiness, host/audio clock coordination |
| `SDL2BarGraphics/AudioPresentationBridge.cpp` | 722 | Parameter comparison, semantic/audio conversion, timeline worker, seek preparation, publication and retirement |
| `SortingLogicTests/SortingLogicTests.cpp` | 5,510 | Catalog cases, algorithm-specific behavior, replay, profiles, budgets, worker tests, manual test dispatch |
| `AppFrameworkTests/AppFrameworkTests.cpp` | 1,874 | UI geometry, controller, settings, presentation and mailbox checks |
| `SoundEngineTests/SoundEngineTests.cpp` | 1,713 | Live/reference DSP, timeline, experimental composition, regression checks |

There are 60 production source/header files totaling 32,115 physical lines. The first two files contain 18,598 lines, about 58% of that total. Size is evidence of concentration, not a correctness verdict. Splitting files without separating decisions would leave the central problem intact.

The manifest has 208 source rows, all marked `implemented_and_tested`; the current catalog has 211 descriptor initializers. The upstream coverage inventory and the application variant catalog are different inventories and must stay distinct.

## Existing boundaries to preserve

- `SortingLogic` does not include SDL/OpenGL, application, or sound headers. Algorithms produce a reversible semantic trace; simulation playback stays on the app thread.
- `TraceBuildService` owns a single trace job, publication, and shutdown joining. Its small, concrete service is appropriate; it does not need a general task scheduler.
- `SoundEngine` has no dependency on sorting or SDL. The app-side bridge translates sorting semantics into engine contracts.
- Prepared timelines and seek states are built outside the callback. The mailbox is bounded; generation/revision acknowledgements protect object retirement. Preserve these lifetime guarantees when extracting code.
- `Renderer2D` already owns a shared queue, reusable buffers, explicit material state, clipping, and painter ordering. This is a useful foundation, not a reason to introduce a render graph.
- `UiScrollViewport`, pure layout calculations, `ISettingsPersistence`, and stable saved family/variant identifiers are reusable seams.
- Existing legacy sound code in the tests is an independent reference. Its intentional duplication should not be removed merely to improve a duplication metric.

## Findings and recommended changes

### 1. The label cache can be invalidated twice for unchanged text — first priority

Evidence: `ControlPanel.cpp:1168` calls legacy `TextLabel::setText(..., "Courier New Bold", size, ...)` while synchronizing labels. `ControlPanel.cpp:2399` calls the themed font/DPI overload while drawing the same labels. `TextLabel.cpp:102`, `:107`, and `:132` use different font identities and DPI values, and rebuild when these change.

For labels synchronized and drawn every frame, those paths alternate between the legacy and resolved font keys. The cache's unchanged-content guard cannot prevent texture recreation. This is a concrete call-path finding; its CPU/GPU cost has not yet been measured.

Extract a text-content setter that does not modify font state, and resolve one `TextStyle` per label/frame revision. Introduce reusable wrapped-text layout for the algorithm description and other long paragraphs. Measurement, line spacing, rasterization, clipping, and painting should consume the same resolved style. Use explicit title/body/caption roles rather than choosing typography by numeric target-height thresholds.

Cache wrapped lines by content, width, font, DPI, and style revision. Currently the profile recomputes wrapping during painting. Font enumeration/fallback behavior must remain consistent between measurement and rendering.

Acceptance: unchanged visible labels do not allocate a new GL text texture on successive idle frames; description wrapping remains readable across presets, width, font changes, and DPI. Preserve all current text and scroll behavior.

### 2. One sorting translation unit owns almost the entire subsystem — high priority

Evidence: `SortingLogic.cpp:94` defines the recorder; implementations occupy most of the file; `:12179` builds through a large algorithm switch; `:12454` generates request wrappers; `:12670` starts catalog metadata; `:13999`/`:14154` implement replay; `:14411` onward implements simulation/input generation.

Extract logical modules inside the existing `SortingLogic` library:

- Contracts: item/domain types, trace state/events, catalog descriptors, request/result types.
- Recording: recorder and centralized operation/work/memory limits.
- Replay: forward/inverse application, validation, checkpoint seeking.
- Simulation/input: prepared input, generation, cursor and playback lifecycle.
- Catalog: registrations, compatibility and options.
- Algorithms: family modules for insertion, exchange, distribution, selection/heaps/trees, partition, merge/hybrids, networks, and novelty methods. Split large families further by coherent kernels when useful.

Keep `SortingLogic.h` as a temporary forwarding facade so extraction can be incremental. Give builders access to a concrete internal recording API and read-only state where possible; avoid making mutable recorder internals an informal public contract.

The static descriptors currently lead through generated wrappers back into the central switch. Replace that second dispatch with direct registered strategy functions, wrapped by one common build/limit/finalization path. Keep static registration; no runtime plugins or one class/project per algorithm.

Acceptance: each extracted batch produces the same event/annotation/metric signatures, partial-stop state, ordering, stability, and seeded behavior as the frozen baseline. Keep IDs and catalog order where current UI/migration adapters still depend on them.

### 3. Configuration has several sources of truth — high priority

Evidence: `SortingLogic.h` carries numeric algorithm IDs plus family/variant strings, both `InputProfile` and `InputDomain`, named Shell/Merge fields, and more generic numeric option fields. `UserSettings.h` also stores dedicated Shell/Merge fields and a stable option map. `AppController.cpp:209` translates option-group strings into request fields; `heap_arity`, `bucket_count`, and `grail_buffer_mode` reuse `bucketCount`.

The catalog's option descriptors currently represent choice entries. They are not yet the complete typed configuration schema described in the earlier roster plan. Adding a choice still requires understanding app-specific string translations and legacy fields.

Introduce a resolved run configuration in the sorting layer: descriptor identity, domain/pattern, direction, seed, budgets, and validated option values. Option metadata owns defaults, types, bounds, and valid combinations. The app submits stable selections; sorting resolves them into algorithm-specific settings. Preserve current defaults and behavior while replacing overloaded fields.

Keep numeric IDs, old profile fields, and old dedicated options in explicit migration/compatibility adapters during the transition. Move toward one authoritative representation at runtime instead of synchronizing every representation indefinitely. Use C++14 tagged values/named structs; this does not require a language upgrade or a new dependency.

Acceptance: current v1–v8 settings load identically, saved keys and effective choices survive, and all existing option configurations produce unchanged traces. A structural wave alone does not require a settings-version bump.

### 4. The UI has retained widgets, but screen ownership is still centralized — high priority

Evidence: `ControlPanel.h` owns main/settings/appearance roots, several overlay booleans, menu selections, scroll viewports, inline-edit state, theme drafts, labels, and dialog methods. Its implementation contains the main pane, algorithm browser, settings pages, appearance editor, profile card, text renderer, and input adapter. `UiLayer.h:10` includes `AppController.h`, pulling the controller, trace worker, settings, and audio contracts into generic widgets.

Extract small view objects: main controls/profile, algorithm browser, settings host, appearance editor, and inline editor. Each owns its state and UI composition. Keep `ControlPanel` as their facade initially. Move SDL-event conversion, color/file dialogs, and platform font lookup behind app/platform adapters.

Move `AppAction` contracts out of `AppController.h`. Generic UI input and widgets should not require the concrete app controller or worker headers. Bind generic control events to app actions at the view/coordinator boundary. Named action factories or typed payloads can prevent invalid combinations of the current catch-all `value/selection/index/string` fields without introducing a heap-allocated command object for every click.

Use a single active-overlay state with explicit transitions instead of independent booleans that must remain mutually consistent. Keep scroll offsets and keyboard focus per view; preserve the existing shared viewport geometry and modal input rules.

Acceptance: actual runtime view composition is testable without SDL/GL; menu search/scrolling, Escape/back, focus, pointer capture, inline editing, and outside-click dismissal preserve their current behavior.

### 5. Part of the apparent UI test seam is not connected to runtime painting — high priority

Evidence: `UiPaintContext`/`UiPaintCommand` are declared in `UiArchitecture.h:159`; the only use found outside declarations is the recording-context check in `AppFrameworkTests.cpp:374`. Runtime `ControlPanel` paints directly through `Renderer2D`.

The existing recording-context check proves storage behavior of a test recorder, not the commands produced by real menus or settings views. Either connect view composition to this interface with a real renderer adapter, or remove the unused parallel abstraction. Prefer one paint-command contract consumed by both production rendering and headless composition checks.

Acceptance: a layout/paint regression in a real settings pane affects a meaningful test; renderer painter-order checks continue to test the actual batch-key policy.

### 6. Audio eligibility and semantic visibility are repeated across layers — high priority

Evidence at the audit baseline: `SortingLogic.cpp` populated audio-event indices, `AudioPresentationBridge.cpp` independently converted eligible events into a prepared timeline, and the live bridge update classified probes/movement/distribution again. Merge's hidden-buffer rule also appeared in the trace policy, bridge, `ArrayRenderer.cpp`, and main-array role projection in `UiArchitecture.cpp`.

These are decisions about which semantic operations are presented. Repeating them risks different reference-cadence duration, audible operations, UI controls, and lane visibility for the same run.

Current status (2026-10-20): the repeated decisions now live in `SortingLogic/TracePresentationPolicy.h/.cpp`. `TraceAudioPresentationPolicy` classifies comparisons, probes, main changes, auxiliary writes, distribution bookkeeping, and discards; derives the reference-cadence index; projects operand IDs, semantic roles, and configured operand order; and preserves the existing completion-scan and hidden-Merge rules. `AudioPresentationBridge` uses that policy for prepared timelines, live comparison state, and accents. Contextual audio controls query the same operation-category capabilities. `ArrayRenderer` and `UiArchitecture` share the policy's workspace visibility and role-lane projection. Gain inheritance and mute settings remain separate, so live gain changes do not change event timing.

The bridge still translates semantic roles to SoundEngine roles and normalized pitches at its adapter boundary. No PCM generation, DSP, envelope, gain, or mailbox code was changed.

Acceptance: existing Counting/Radix event eligibility, paired/single operands, pitch positions, gains, timing, and PCM fixtures remain unchanged, including the established `2e-6` PCM tolerance. Preserve the hidden-Merge behavior and all current reconstructed-audio formulas. This is a policy extraction, not sound redesign.

Verification (2026-10-20): x64 Debug/Release solution builds and `AppFrameworkTests` pass; `SortingLogicTests --core-contracts-only` passes with 39,535 checks in both configurations, including Counting/Radix audio signatures; standalone `AudioDeviceTransportTests` builds and passes in both configurations with the SDL dummy backend, including fixed/reference cadence and 80/800-item runs. `git diff --check` passes; only configured LF-to-CRLF notices remain.

### 7. Playback coordination is distributed across the main loop, controller, bridge, and device — high priority, higher risk

Evidence: `SDL2BarGraphics.cpp:157` takes a snapshot, updates the bridge, feeds readiness to the controller, retires objects, receives the audio playhead, and takes another snapshot. `AppController.cpp:697` also owns fallback timing and preparation gates. The bridge tracks build/seek preparation and multiple generations/revisions; the callback installs and acknowledges the corresponding objects.

These mechanisms have real purposes. Consolidate the app-thread transitions into a playback coordinator with explicit preparing/ready/queued/playing/paused/completed states and named generation/revision values. Keep device health distinct from preparation readiness, and keep the existing fenced callback retirement protocol.

Centralize reference-frame/cadence conversions instead of repeating `300.0` and `44100.0` in UI/controller/transport calculations. The 44.1 kHz reference timeline is intentional and distinct from device resampling; it should be represented clearly, not blindly replaced by the output device rate.

Acceptance: pause/resume, seek, queued play, replacement, device loss/retry, drain/crossfade acknowledgement, and fallback clocks retain existing behavior. The callback must not gain blocking locks, allocation, file access, or ownership destruction. Implement this after lower-risk extractions.

### 8. Settings equality, normalization, storage, and audio policy are mixed — medium/high priority

Current-tree correction (verified during the refactor on 2026-10-19): equality is already centralized on `UserPreferences`, `SoundEngine::Parameters`, and `SoundEngine::ReconstructedParameters`; the original attribution to duplicated field-by-field comparison helpers in `AppController` and `AudioPresentationBridge` is stale for this tree. The bridge still has focused comparators for timeline gain and event-presentation structs, while the controller uses the value-type operators. Do not merge those distinct comparisons without evidence they represent the same value contract.

Current status (verified 2026-10-20): this boundary is resolved. `UserSettings.cpp` now owns preference value semantics and the `SettingsStore` orchestration. `UserSettingsValidation.cpp` owns range and enum invariants, `SettingsIniCodec.cpp` owns INI encoding/decoding and v1–v8 migration, `SettingsFileStorage.cpp` owns LocalAppData and atomic Windows file operations, and `AlgorithmAudioPreferences.cpp` owns family/variant gain inheritance and effective SoundEngine mapping. `AudioSettingsActions` and pure SoundEngine normalizers keep UI setting updates out of DSP object construction. AppController snapshots full preferences only for actions that can change non-simulation preferences; transport commands compare the smaller simulation value.

The settings version, keys, defaults, malformed-value fallbacks, stable algorithm IDs, v1–v8 migrations, immediate save-on-change behavior, backup suffix, bounded reads, write-through replacement, and normalization rules are unchanged. Verification: x64 Debug/Release solution builds and `AppFrameworkTests` pass; the standalone `AudioDeviceTransportTests` project builds and passes in both configurations, including reference/fixed timing and 800-item scenarios. No background saving or debouncing was introduced.

Next: centralize deterministic semantic presentation eligibility, operand/role projection, and lane visibility so the trace cadence index, audio adapter, and UI controls consume one policy without changing Counting/Radix or Merge behavior.

Acceptance: zero writes for transport/scrubbing/no-op edits, one effective save for an actual preference change, identical migrations/fallbacks, unchanged atomic temporary-file replacement/backup behavior, and no divergence between pure normalization and runtime parameter setters.

### 9. The frame prepares more work than it needs — medium priority, measure impact

Evidence at the audit baseline: the main loop prepared the control panel twice, and controller queries requested full snapshots to inspect a trace flag or cursor. Both are now resolved: `prepareFrame` runs once for the composed UI frame, and controller flow uses narrow `Simulation::hasTrace()` / `cursor()` accessors. Remaining cost centers are intentional full presentation snapshots—`Simulation::snapshot` copies state/metrics and scans active region annotations—and `ArrayRenderer::descriptorForItem` scans active roles once per item. The app currently takes snapshots for audio presentation and again for UI after possible playhead feedback.

The control panel now prepares once and paints the same frame snapshot used by the chart. AppController no longer creates full snapshots for trace-existence and cursor-only checks. Next, measure snapshot copy/annotation scans and role lookup cost before adding more cache state. Candidate changes are a monotonic active-region cursor/index with seek rebuild and a per-frame `(lane,itemId)` role lookup for large role sets; keep them contingent on measurement.

Retain snapshot ownership safety: do not expose worker-mutated state or return dangling borrowed snapshots. Measure state-copy time, annotation counts, text-raster counts, allocations/frame, batches, and draw uploads before choosing more complex caching.

Acceptance: identical frame state at each trace cursor; chart and controls use the same prepared viewport; no stale menu/focus/style state after preset, resize, DPI, seek, or algorithm changes.

### 10. Event storage is general but expensive, and its byte budget is an accounting model — medium priority, later change

Evidence: every `SemanticEvent` (`SortingLogic.h:194`) contains several item fields, three vectors, four strings, and counters/flags for all event kinds. Ordinary comparisons pay the containing-object cost of all these fields. Byte accounting (`SortingLogic.cpp:60`, `:73`) counts selected payloads; annotations are appended separately at `:542`. Container capacity, index arrays, annotation storage, and allocator/map-node overhead are not comprehensively represented by those functions.

Treat the 128 MiB limit as accounted trace payload, not a proven bound on total process memory. First make the accounting definition explicit and measure representative traces. Then consider interned lane/metric IDs and compact typed payload storage with separate pools for range writes, while retaining stable cursor semantics and snapshots every 4,096 events.

Keep forward/inverse behavior paired and covered. A compact tagged representation in C++14 is possible, but it is a higher-risk format refactor and should not be mixed with algorithm extraction.

Acceptance: equivalent per-cursor state and metrics, exact rewind/seek across checkpoint boundaries, valid partial traces at every budget stop, and checks before budgeted allocation/mutation. Compare memory high-water marks and build time with the frozen baseline.

### 11. Rendering retains an unused older path and application semantics in generic interfaces — medium priority

Evidence: `Mesh`, `Rectangle`, and their `Texture` path are still compiled into the app, while the live chart/UI use `Renderer2D`. Source searches find the mesh classes referencing one another but no live view creating them. `SdlAudioOutput.h:71` declares a legacy `Synthesizer` member with constructor initialization but no other app usage. `ArrayRenderer` includes `UiArchitecture.h` for sorting-role projection and receives the entire settings object. `MaterialId` includes semantic bar labels whose GPU state is identical.

Verify project/entry-point usage, then remove unused live-path objects and obsolete source entries. Preserve sound experiment/preview libraries and independent regression references, which do have callers.

Give chart views narrow visual preferences plus a semantic lane/role presentation model. Move shared rectangles/clips into a geometry contract that does not include the UI/controller. Keep semantic style selection in the chart/presentation layer and GPU materials in the renderer.

The current adjacent batching and stable painter order are correct constraints. Only merge additional batches when their effective GPU state, layer, texture, and clip agree; never globally sort overlapping UI by texture. A glyph atlas or different upload strategy is optional future measured work, not the first response to cache invalidation.

### 12. Build, asset lookup, test organization, and project memory need consolidation — medium priority

Evidence: shader/font paths depend on process working directory (`Renderer2D.cpp:23`, `TextLabel.cpp:104`); recent live launches required the output directory as CWD. `ExternalDependencies.props` assigns a machine-layout-relative external library root. Output directories use `$(SolutionDir)`, so building a project directly versus through the solution has produced different output trees. `AppFrameworkTests.vcxproj:33` explicitly recompiles application `.cpp` files. Large tests use manually duplicated dispatch lists and `verifyAlgorithmCase` stores a complete state at every cursor (`SortingLogicTests.cpp:254`). The memory index still described the roster as in progress before this audit.

Resolve bundled assets from an explicit executable-relative asset root and pass it to shader/font owners. Keep Visual Studio and existing configurations; make shared paths deterministic and dependency roots locally overridable. Maintain a single source-ownership definition for reusable app-core code; a focused static AppCore target can be evaluated after platform dependencies are removed, without creating a project per UI view.

Split test sources by contracts/families while keeping shared small-trace replay helpers. Keep exhaustive cursor states for small cases; use bounded checkpoints/signatures plus sampled reconstruction for larger stress tests. Register test names once for focused/full execution. Assertion totals are useful diagnostics, not evidence that two methods have equivalent behavior or that all seeds are correct.

Acceptance: all current Win32/x64 Debug/Release configurations and suites remain available; asset launch works from a different CWD; source lists agree between app/tests; source coverage manifest and current documentation are consistent.

## Duplication that must not be collapsed blindly

- Different algorithms can have similar search/rotation code while intentionally using different bounds, tie handling, traversal, metrics, or emitted roles. The Lazierest correction at the baseline is an example of why boundaries matter. Share only after specifying event/behavior equivalence, not because functions look similar.
- Replacing several merge implementations with one correct merge would change the showcase and sonorization. Preserve their schedules.
- Independent reference DSP implementations and deterministic fixtures should not call the production implementation they are supposed to verify.
- UI preset differences belong in data/strategies where appropriate, but visible differences in layout, input regions, and text styling are product behavior.

## Proposed target dependency shape

```text
Platform loop/adapters (SDL input/window/audio, Win32 files/dialogs/fonts, GL resources)
  -> App coordination (actions, preferences, presentation policy, playback state)
       -> SortingLogic (catalog/config, input, recorder, strategies, replay, simulation, trace worker)
       -> SoundEngine (timeline contracts, prepared seek/DSP, existing reconstructed synthesis)
  -> UI views + chart views (prepared presentation data and narrow visual preferences)
       -> shared geometry/text/paint commands
       -> Renderer2D adapter (ordered queue, materials, batching)
```

Dependency rules: sorting never depends on UI/audio; sound never depends on sorting/SDL; generic widgets never include the concrete controller; rendering consumes presentation rather than interrogating algorithms; cross-thread publication remains immutable and explicitly owned.

## Implementation sequence

Each row should be a coherent, independently reviewable change set, not one repository-wide replacement commit.

| Order | Work | Regression gate |
| --- | --- | --- |
| 0 | Freeze baseline traces/audio/settings/visual behavior and record measurement counters | Representative event signatures, seeded inputs including the reported Lazierest case, existing Counting/Radix PCM and transport fixtures; no automatic baseline regeneration |
| 1 | Repair text-style identity ownership; reusable measured text blocks; prepare UI once per frame | Text-raster counters and actual view layout/scroll checks across presets/DPI; meaningful existing app tests |
| 2 | Split sorting contracts/recorder/replay/input/simulation mechanically; retain facade | Broad sorting suite, exact small-trace replay and partial-stop checks |
| 3 | Move families in batches and point static registrations directly at strategies | Registry/208-row manifest checks plus family behavior/configuration and event-signature checks |
| 4 | Resolve typed run options and isolate compatibility adapters | Existing settings migration matrix and identical effective configurations/traces |
| 5 | Extract real UI views, generic action binding, and one runtime/test paint path | Focus, modal capture, scrolling, menu selection, inline edits, preset geometry and visual comparison |
| 6 | Centralize presentation policy, parameter values, settings codec/storage | Exact existing audio operands/cadence/PCM, migration/fallback and persistence-write checks |
| 7 | Extract playback coordination with explicit readiness/lifetime transitions | Current transport/mailbox/device tests, sample-position continuity, device/fallback/seek scenarios |
| 8 | Optimize snapshot/annotation/role work; then consider compact events | Recorded memory/CPU comparison plus exhaustive small-state replay and snapshot-boundary stress |
| 9 | Remove proven unused runtime code; consolidate build/source/asset ownership and test dispatch | All four solution configurations, all relevant suites, portable launch from another CWD |

Start with orders 0–2. They give a concrete performance win and the seams needed to make the larger family/UI work safe. Run the broader catalog/audio suites when their contracts are touched; do not repeatedly run unrelated expensive suites for a documentation edit.

## Behavior that the wave must preserve

- Algorithm identities, source/canonical distinctions, current options/defaults, safe item limits, work/operation limits, and declared non-permutation outcomes.
- Exact prepared inputs for fixed seed/domain/pattern, compatible-input selection rules, and Reset behavior.
- Trace events, comparison operand order/roles, grouped phases, regions, metrics, replay, rewind, checkpoint seeks, and playable partial results.
- Reconstructed sound formulas, Counting/Radix profiles, live gains, cadence, transport semantics, low-latency publication, and callback lifetime safety.
- Settings v1–v8 migration/fallback, stable keys, save-on-change, zero transport writes, atomic replacement, and appearance customization.
- Current menus, keyboard shortcuts, edit gestures, clipping/scrolling/focus behavior, UI presets, renderer painter order, resizing, and DPI coordinates.
- C++14, Visual Studio solution/configurations, current dependency set, and shared-window rendering.

## Audit verification and remaining uncertainty

- Inspected the named runtime contracts, project references/source lists, recorder/replay paths, main frame/transport path, UI/text pipeline, settings codec, and representative test helpers.
- Measured physical source size and inspected manifest status; no new full-catalog correctness claim is made.
- Confirmed the supplied coding guide was copied verbatim by SHA-256 equality. Preserved its sibling workflow locally and added discoverability through root `AGENTS.md`.
- Runtime code is unchanged in this audit. No application build or algorithm/audio suite rerun is needed to validate these documentation/ignore changes.
- Text-cache churn is supported by the inspected call sequence; its time/texture-allocation cost still needs instrumentation. Snapshot/annotation/role-scan costs and compact-event savings are candidates requiring measurement.
- No general proof was made that every duplicated algorithm kernel is equivalent, that every retained class is dead in every offline tool, or that accounted trace bytes equal peak heap use. Those checks belong to the relevant implementation slice.
