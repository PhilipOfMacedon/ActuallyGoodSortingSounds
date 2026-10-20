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

Next: freeze the behavioral baselines described in the audit before starting the mechanical `SortingLogic` extraction behind the stable facade.

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

Next: create the private recorder boundary needed to move algorithm families without turning internal mutation helpers into public API.

## 2026-10-19 — isolate trace request validation

Status: request-validation extraction implemented; pending commit.

- Moved both `SortTraceBuilder::build` entry points and request validation into `TraceBuilder.cpp`.
- Moved the legacy-profile-to-domain compatibility rule into the private `SortingLogicInternal.h`; the public header and request shape remain unchanged.
- The extracted validation block matches its prior implementation aside from whitespace-only lines.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release LNK4020 PDB type-record warnings remain present during `AppFrameworkTests` linking.

Next: move one family strategy behind the private recorder contract without altering the event sequence.

## 2026-10-19 — define the private recorder boundary

Status: recorder boundary implemented; pending commit.

- Moved `Recorder` into the non-public `TraceRecorder.h`, under `sortinglogic::detail`, so future family modules can record semantic operations without adding recorder internals to `SortingLogic.h`.
- Moved operation/workspace budget exceptions and the accounted-state/event byte helpers into `SortingLogicInternal.h`, shared by recording and failure handling.
- Verified the recorder class matches its original implementation apart from whitespace-only lines. The private helper header keeps one implementation of the invariant checks and trace accounting.

Verification:

- x64 Debug and Release solution builds: passed.
- Debug and Release core-contract runs: each passed 8,218 checks with 0 failures.
- Debug and Release `AppFrameworkTests`: passed.
- Release `AppFrameworkTests` linking again emitted LNK4020 PDB type-record warnings; linking and execution succeeded.

Next: move a small, isolated algorithm strategy into a family module, then expand the private strategy interface only as the actual family dependencies require.
