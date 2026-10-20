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

Baseline evidence: `ControlPanel` previously alternated `TextLabel` between legacy and themed font/DPI setters, while the profile recomputed paragraph wrapping during paint. That call path is no longer present.

Current status (verified 2026-10-20): `TextLabel::setContent` changes visible text without resetting font identity, raster size, or DPI. `ResolvedTextStyle` supplies one font/size/DPI/logical-height contract for measurement and drawing, and `TextBlockLayout` caches wrapped lines by content, width, and style. The app prepares `ControlPanel` once before chart rendering; `render` paints that prepared frame. The algorithm profile uses cached paragraph layout and retains scrolling/clipping behavior.

Remaining uncertainty: no texture-allocation counter or idle-frame benchmark was added, so the fixed invalidation path is verified from the call graph, while its CPU/GPU savings remain unmeasured. Preserve font fallback, raster identity, and scroll behavior when changing this code again.

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

Baseline evidence: `ControlPanel` formerly owned the appearance transaction, browser model, and all inline edit buffer/parser state alongside its main/settings roots, platform input, and renderer facade. Generic widgets also previously depended on the concrete controller.

Current status (verified 2026-10-20): `AppActions.h` holds the value-only app command contract, `UiOverlayState` owns modal transitions, and the browser, settings chrome/model, profile, and appearance editor have separate model/view boundaries. `AppearanceEditorView` owns its controls, session, layout, composition, synchronization, and pure edit actions, returning explicit host effects. `InlineEditModel` owns edit target, accepted-character rules, replacement/backspace/clear behavior, and strict integer/float/double parsing without SDL or renderer dependencies. `SettingsPanelControls` consumes UI-owned `SettingsPanelAlgorithmPresentation` values; `SettingsPanelCatalog` translates SortingLogic descriptors into option groups, visible-audio control IDs, pattern compatibility, and option labels. `SettingsPanelView` owns the retained settings widget root, construction and synchronization of settings values/actions/enabled state, captions, navigation/scroll model, panel/content bounds, responsive row placement, active-control visibility, and typed settings-tab/scroll behavior. It lays out the container only when the cached screen layout changes. `ControlPanel` remains the SDL/renderer adapter, paints specialized controls, owns the main widget root and native dialogs, and converts view effects to app actions.

Next: submit settings tab/content control paint through the existing `UiPaintCommand` path, then isolate the main-control root and view state. Keep focus, scroll, pointer capture, and modal dismissal behavior under test; do not create view classes that only relocate control flow.

Acceptance: actual runtime view composition and edit state are testable without SDL/GL; menu search/scrolling, Escape/back, focus, pointer capture, inline editing, and outside-click dismissal preserve their current behavior.

### 5. Part of the apparent UI test seam is not connected to runtime painting — high priority

Baseline evidence: early `UiPaintContext` coverage only checked a test recorder. That finding is stale for the extracted chrome views.

Current status (verified 2026-10-20): settings chrome and settings control order now submit through `UiPaintCommand`; each visible widget is represented by its stable UI ID, layer, clip, and popup state, while `ControlPanel` adapts those commands through the existing specialized widget renderers into `Renderer2D`. `AlgorithmBrowserView`, `AlgorithmProfileView`, and `AppearanceEditorView` also submit production composition through the same contract. Headless tests compose those runtime views and inspect emitted commands. Main controls, timeline, and some specialized text/control rendering still submit directly through `Renderer2D`. Settings geometry, visibility, state, and command ordering have headless coverage; live inspection confirmed the settings tabs and dropdown popup still render in the active palette.

Next: extend the existing command contract to main-control composition. Keep detailed widget styling and actual painter-order/material tests on the current renderer path; do not add a parallel paint abstraction.

### 6. Audio eligibility and semantic visibility are repeated across layers — high priority

Evidence at the audit baseline: `SortingLogic.cpp` populated audio-event indices, `AudioPresentationBridge.cpp` independently converted eligible events into a prepared timeline, and the live bridge update classified probes/movement/distribution again. Merge's hidden-buffer rule also appeared in the trace policy, bridge, `ArrayRenderer.cpp`, and main-array role projection in `UiArchitecture.cpp`.

These are decisions about which semantic operations are presented. Repeating them risks different reference-cadence duration, audible operations, UI controls, and lane visibility for the same run.

Current status (2026-10-20): the repeated decisions now live in `SortingLogic/TracePresentationPolicy.h/.cpp`. `TraceAudioPresentationPolicy` classifies comparisons, probes, main changes, auxiliary writes, distribution bookkeeping, and discards; derives the reference-cadence index; projects operand IDs, semantic roles, and configured operand order; and preserves the existing completion-scan and hidden-Merge rules. `AudioPresentationBridge` uses that policy for prepared timelines, live comparison state, and accents. Contextual audio controls query the same operation-category capabilities. `ArrayRenderer` and `UiArchitecture` share the policy's workspace visibility and role-lane projection. Gain inheritance and mute settings remain separate, so live gain changes do not change event timing.

The bridge still translates semantic roles to SoundEngine roles and normalized pitches at its adapter boundary. No PCM generation, DSP, envelope, gain, or mailbox code was changed.

Acceptance: existing Counting/Radix event eligibility, paired/single operands, pitch positions, gains, timing, and PCM fixtures remain unchanged, including the established `2e-6` PCM tolerance. Preserve the hidden-Merge behavior and all current reconstructed-audio formulas. This is a policy extraction, not sound redesign.

Verification (2026-10-20): x64 Debug/Release solution builds and `AppFrameworkTests` pass; `SortingLogicTests --core-contracts-only` passes with 39,535 checks in both configurations, including Counting/Radix audio signatures; standalone `AudioDeviceTransportTests` builds and passes in both configurations with the SDL dummy backend, including fixed/reference cadence and 80/800-item runs. `git diff --check` passes; only configured LF-to-CRLF notices remain.

### 7. Playback coordination is distributed across the main loop, controller, bridge, and device — high priority, higher risk

Evidence: the main loop still takes a snapshot, feeds it to playback coordination, and takes another snapshot after audio feedback may advance the visible cursor. The bridge tracks build/seek preparation and multiple generations/revisions; the callback installs and acknowledges the corresponding objects.

These mechanisms have real purposes. `PlaybackCoordinator` now owns queued-play intent, the audio-preparation gate, fallback clock state, and stale-playhead acceptance. It exposes explicit no-trace/building/preparing/queued/ready/playing/paused/completed states. `PlaybackFrameCoordinator` owns the app-thread bridge update, readiness gate, retirement acknowledgement, feedback handoff, and audio-status composition; the transport integration test uses this same path. `AppController` still executes simulation commands. The bridge remains the owner of its timeline worker and generation/revision counters; the callback keeps installing and acknowledging immutable snapshots.

Keep device health distinct from preparation readiness and preserve the fenced callback retirement protocol. `AudioTimelineAcknowledgement` now groups the callback's timeline generation and seek revision; `AudioPlaybackRevision` groups the active timeline generation and transport revision used to accept playhead feedback. The main loop passes these named values instead of loose integer pairs.

A remaining improvement is to measure the second snapshot and reduce any material main-loop coordination overhead, without moving callback ownership or destruction onto the audio thread. Snapshot cost is also tracked under item 9.

The reference timeline conversion part is now resolved by `PlaybackTimebase`: the 300-event cadence and 44.1 kHz reference sample rate, duration/cursor conversion, and audio-frame seconds conversion have one home. These remain reference-time units and are still distinct from device resampling. The SDL output uses the named 44.1 kHz reference when scaling the existing 128-frame crossfade.

Remaining work keeps the device health distinct from preparation readiness and preserves the callback's fenced retirement protocol. The main-loop and snapshot overhead remain candidates for measurement, not assumption.

Acceptance: pause/resume, seek, queued play, replacement, device loss/retry, drain/crossfade acknowledgement, and fallback clocks retain existing behavior. The callback must not gain blocking locks, allocation, file access, or ownership destruction. Verification (2026-10-20): x64 Debug/Release solution builds and `AppFrameworkTests` pass; standalone `AudioDeviceTransportTests` builds and passes in Debug and Release with SDL's dummy backend, including reference/fixed timing, pause/step/seek, and 80/800-item cases. Focused coordinator checks cover queued-start gating, lifecycle states, host-clock loss/recovery, and stale playhead rejection. Typed acknowledgement/revision wiring is exercised by seek/device recovery and transport integration tests. `git diff --check` passes with only configured LF-to-CRLF notices.

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
- Runtime code was unchanged when this audit was first written; implementation changes and their regression evidence are recorded in the execution checkpoints and `structural-refactor-progress.md`.
- The original text-cache invalidation path has been corrected, but its performance impact remains unmeasured. Snapshot/annotation/role-scan costs and compact-event savings remain candidates requiring measurement.
- No general proof was made that every duplicated algorithm kernel is equivalent, that every retained class is dead in every offline tool, or that accounted trace bytes equal peak heap use. Those checks belong to the relevant implementation slice.

## Structural refactor execution checkpoint — 2026-10-20

The appearance view now owns its transactional state, control synchronization, and pure edit actions. Inline input buffering and numeric parsing have been extracted to the SDL-independent `InlineEditModel` in the existing `AppFramework` library. `ControlPanel` maps edit targets to controls, translates SDL events, applies values to preferences or app commands, and handles renderer/native-dialog effects.

The full Visual Studio solution builds in x64/x86 Debug and Release; `AppFrameworkTests` passes in all four configurations. New inline-edit checks cover target-specific character filtering, first-character replacement, malformed/overflow parsing, and the ten-character input limit. `git diff --check` passes. No visual layout changed and no performance claim is made.

Next: continue extracting main/settings runtime view ownership through the existing paint and action contracts. The full architecture audit remains active; this checkpoint does not certify the overall refactor as complete.

## Settings catalog boundary checkpoint — 2026-10-20

`SettingsPanelControls` now receives a UI-owned catalog presentation containing stable family/variant IDs, option-group IDs, Merge-buffer visibility, and visible audio-control IDs. `SettingsPanelCatalog` is the only layer in this path that translates `AlgorithmDescriptor`, semantic audio capabilities, and supported role channels. This removes SortingLogic and settings/audio policy includes from the generic settings-control helper without changing which widgets appear or their order.

Verification: full Visual Studio solution builds and `AppFrameworkTests` passed in x64/x86 Debug and Release. New assertions cover Insertion, Merge, Counting, and missing-descriptor control visibility. `git diff --check` passes.

## Settings view ownership checkpoint — 2026-10-20

`SettingsPanelView` now owns the retained settings `UIRoot`, its session navigation/scroll model, and captions. It handles typed page/audio-group navigation and wheel-scroll policy; `ControlPanel` routes those actions to the view and no longer stores the settings root/model/caption map itself. The app still owns SDL translation, control construction/state synchronization, layout, renderer submission, and modal lifetime.

Verification: full Visual Studio solution builds passed in x64/x86 Debug and Release. `AppFrameworkTests` passed in all four configurations, including view-level navigation, scroll bounds, page-switch offset reset, and app-level modal ownership checks. `git diff --check` passes. No widget geometry or renderer path changed, and no performance improvement is claimed.

## Settings layout ownership checkpoint — 2026-10-20

Moved settings panel container and modal bounds, tab/audio-subpage geometry, row placement, clipped-scroll configuration, popup placement, and active-control visibility into `SettingsPanelView`. `ControlPanel` now supplies the selected algorithm's UI-owned active control IDs and the theme's scrollbar thickness. The view keeps its panel bounds and only rebuilds the container when the existing screen layout cache invalidates; repeated frame preparation no longer calls `layoutContainer` for the settings panel.

Verification: full solution builds and `AppFrameworkTests` passed in x64/x86 Debug and Release. New view checks cover page-specific content offsets, tab/footer and control visibility, and long-list wheel scrolling. Computer Use inspection confirmed the Appearance, Algorithm, General Audio Basic/Advanced layouts and scrolling to the final Advanced Audio control; tabs and footer remained fixed. `git diff --check` passes. The frame path was not benchmarked, so no performance claim is made.

## Settings control construction checkpoint — 2026-10-20

`SettingsPanelView::initializeControls` now constructs the settings widgets, catalog-derived algorithm option selectors, typed app actions, captions, and panel container from the supplied `UserPreferences`. `SettingsPanelCatalog` centralizes palette, preset, spacing, input-domain, input-pattern, and boolean option labels. Palette identity and display-name metadata moved into renderer-independent `UiAppearance`, removing the GLM dependency that initially prevented the headless `AppFramework` target from building the settings catalog. `ControlPanel` retains SDL/renderer label resources and settings value/enabled-state synchronization, but no longer builds the settings widget tree.

Verification: the full solution built in x64 Debug/Release and x86-mapped Win32 Debug/Release. `AppFrameworkTests` passed in all four configurations, including checks for initial preference values, typed actions, every catalog option group, palette label order, and view-owned construction. The first x64 Debug build exposed the GLM dependency leak; the pure appearance catalog extraction resolved it. `git diff --check` passes. No visual or performance change is claimed.

Next: move settings control painting through the existing `UiPaintCommand` contract before isolating main-control ownership.

## Settings control synchronization checkpoint — 2026-10-20

`SettingsPanelView` now synchronizes saved selections, catalog-derived option values/actions, simulation setup enablement, pattern compatibility, reconstructed-audio control values, and contextual per-variant audio levels/actions. `ControlPanel` no longer owns these settings-widget decisions. Main controls and settings now share `settingsInputPatternEnabled`, avoiding two copies of the domain/algorithm compatibility rule. Audio gain controls use 0.5 dB increments so stored half-decibel overrides remain visible rather than being rounded to whole decibels.

Verification: the four Visual Studio solution builds and `AppFrameworkTests` passed. `SoundEngineTests` passed with 417,376 assertions in x64 Debug. `SortingLogicTests` was not completed; duplicate runs were stopped because this settings-only change does not touch the sorting library. `git diff --check` passes. No geometry or render order changed; no performance gain is claimed.

Next: isolate main-control view state/composition through the same command path. The active architecture goal remains in progress.

## Settings paint composition checkpoint — 2026-10-20

`SettingsPanelView::compose` now emits an ordered `UiPaintCommand` stream for settings tabs, the active-tab marker, audio sub-tabs, visible content controls, modal buttons, and expanded combo popups. Commands carry widget IDs, painter layers, clipping, and popup state. `ControlPanel` dispatches them to the pre-existing themed widget and slider renderers, preserving theme-specific geometry and text resources while moving selection and paint order into the view. Headless checks cover Appearance ordering/clipping, active-tab paint, General Audio visibility, and popup routing. Live inspection of Algorithm settings and the Appearance palette dropdown showed the existing controls and popup still render.

Verification: full solution builds and `AppFrameworkTests` passed in Win32/x64 Debug and Release. Live Computer Use inspection showed the Algorithm settings and Appearance palette popup still render through the existing themed control path. `git diff --check` passes. No graphics layout or painter-order change is intended; no performance gain is claimed.
