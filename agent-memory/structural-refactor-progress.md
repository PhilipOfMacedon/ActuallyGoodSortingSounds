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

Status: second UI text slice implemented; pending commit.

- Added `ResolvedTextStyle` so width measurement, rasterization, line height, and drawing use the same font, raster size, DPI, and logical height.
- Added renderer-independent `TextBlockLayout`, which caches wrapped lines and invalidates when content, available width, or any resolved style field changes. Its text-width callback keeps font measurement outside the layout value.
- Replaced the per-frame algorithm profile wrapping work with cached complexity and description layouts. Reused the resolved text style for algorithm popup width measurement and painting.
- The profile description remains a clipped scrollable paragraph. A visual check confirmed the restored Current preset, readable profile card, palette, and controls after switching presets; the short built-in profile text did not exercise long-content scrolling.
- No sorting trace, audio, or persistence code changed.

Verification:

- x64 Debug solution build: passed. x64 Debug `AppFrameworkTests`: passed.
- x64 Release solution build: passed. Release `AppFrameworkTests` and `SoundEngineTests`: passed; the sound suite reported 417,371 assertions.
- The Release `AppFrameworkTests` link continues to emit existing LNK4020 PDB type-record warnings for `SoundEngine.pdb` and `SortingLogic.pdb`; linking and execution succeed. The warning is not isolated by this slice.
- Debug `SortingLogicTests` and `SoundEngineTests` were started but stopped after several minutes of CPU-active execution without results. These suites are unrelated to the text-layout change; their incomplete runs are not counted as passes.
- `git diff --check` passed. The current worktree has no changes to sorting, sound, or settings behavior.

Next: capture fixed-seed trace signatures, Counting/Radix audio operands and PCM fixtures, settings migration/persistence fixtures, and baseline measurement counters. Only after those regression boundaries are recorded should the mechanical `SortingLogic` extraction begin.
