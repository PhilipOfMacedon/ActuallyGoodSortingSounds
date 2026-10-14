# Refactoring roadmap

> Historical record for the first rendering/simulation refactor. Its audio-future scope was superseded when the sound engine was implemented. Current roster work is tracked in [sorting-roster-progress.md](sorting-roster-progress.md).

This file is the working checklist for the first large cleanup wave. It records accepted product and architecture decisions, implementation order, and completion evidence. The audio engine remains future work.

## Accepted architecture

- Keep the SDL/OpenGL application as the executable and add one dependency-free `SortingLogic` static library. That library owns sorting contracts, deterministic trace generation, replay, and simulation state. A separate console test project links it without SDL/OpenGL.
- Keep the application coordinator small. It routes keyboard and mouse commands, polls trace-build completion, advances simulation time, and hands snapshots to the renderer.
- Keep all SDL, OpenGL, UI, and renderer resources on the application thread. Trace construction receives a copied input on a worker and publishes an immutable completed trace.
- Treat the ordered event trace and cursor as the future audio boundary. No sound synthesis or SDL audio setup is part of this wave.
- Insertion Sort is the only catalogued algorithm. Its ascending/descending behavior is one direction setting. Keep its shift-based form and explicitly represent the held key and vacant gap.
- Each event cursor describes an exact replayable state. Items carry stable IDs; comparisons retain their comparator result; mutations are reversible. Use inverse events for nearby rewind and state snapshots every 4,096 events for longer seeks.
- Map the complete trace linearly over the chosen total duration. Apply every due event and expose the latest event's semantic roles as the representative visual state for the frame.
- Render semantic roles through renderer-owned styles and per-quad descriptors (tint, texture/material, and pass). Keep custom behavior in renderer-level strategies/passes and batch bar draws by material.
- Use custom OpenGL controls with SDL mouse input. Preserve keyboard shortcuts by routing them through the same command dispatch. Show command feedback and reject conflicting preparation/new-run commands during trace generation or active playback; transport remains available.
- Defaults are 80 bars and 10 seconds; bounds are 2–800 bars and 1–120 seconds. Startup is shuffled from a recorded seed. Each explicit Shuffle records a fresh seed. Insta-sort and Insta-invert prepare ascending and descending baselines over the same stable item set. Reset restores the most recently prepared baseline and trace cursor zero.
- Keep the Visual Studio solution and its Win32/x64 Debug/Release configurations. Centralize SDL/GLEW/GLM paths in a shared property sheet. Do not migrate to CMake or a package manager.

## Work sequence and status

1. **Recovery point and roadmap — complete.** Initialize Git, ignore generated Visual Studio outputs, and commit the pre-refactor source. Baseline commit: `c576cff` (`Baseline existing SDL bar visualizer`).
2. **Headless core and test project — complete.** Added trace/event contracts, stable items, direction-aware Insertion Sort, reversible mutations, a worker-safe simulation API, and a dependency-free console test project. Removed empty algorithm stubs with the old sorter/worker.
3. **Simulation lifecycle and transport — complete.** Added copied-input background trace creation/publication, cursor playback, inverse stepping, long seeks with 4,096-event snapshots, duration mapping, baseline preparation/reset, seeded shuffling, and busy-command feedback.
4. **Rendering sweep and controls — implemented and visually checked.** Replaced fixed highlight slots with semantic roles, added quad descriptors for tint/texture/material/pass, grouped bars by material, and added custom OpenGL controls. Live inspection confirmed preset drawing, readable controls/status, mouse command dispatch, event stepping, seek, playback, pause, reset, and maximize/resizing. The inspection found and fixed two text-rendering defects: labels were drawn with the shader unbound, and their quad extended upward in a top-down projection.
5. **Project wiring and verification — mostly complete.** Confirmed the orphan `SortingLiveWindow.cpp` was outside the project and removed it; centralized SDL/GLEW/GLM paths; verified runtime DLL/font/shader copying; preserved Win32/x64 Debug/Release. All four solution configurations and test executables passed in the earlier verification. After the live UI fixes, x64 Debug rebuilt successfully and the test executable again passed 10,020 checks. Live verification reached 800 bars, generated a 311,368-event trace, exercised playback/pause/reset at the 10-second duration, and confirmed the dense render remains legible at 1920×1032. The user confirmed the app's R shortcut reshuffles bars; Computer Use keyboard injection did not reach the SDL app. The old ignored `SDL2BarGraphics\Debug` and `SDL2BarGraphics\x64` folders remain; removal was blocked by shell policy.

## Acceptance checklist

- [x] SortingLogic builds without SDL/OpenGL headers or libraries.
- [x] Ascending and descending stable Insertion Sort pass empty, one-item, duplicate, sorted, reverse-sorted, and randomized cases.
- [x] Seeded shuffles are repeatable permutations; prepared presets and Reset restore the documented baseline exactly.
- [x] Every trace cursor has unique item identities across occupied slots and the held key. Comparison results, gap/held transitions, final ordering, forward/backward stepping, and seeks are verified.
- [x] Playback applies all events due between frames, maps the complete trace to duration, and supports 800 bars and long seeks.
- [x] Conflicting commands report why they were rejected; transport commands continue to work.
- [x] Bars are batched by material and semantic roles are variable. Custom mouse/keyboard controls are implemented.
- [x] Visual Studio Win32/x64 Debug and Release builds succeed; all four standalone test executables pass; runtime dependencies and assets are copied to the app output directories.
- [x] Visually inspect Insta-sort and Insta-invert, shuffled input, trace generation, forward/backward stepping, held-key/gap roles, timeline seek, playback, pause, reset, and a maximized layout.
- [x] Visually inspect the 800-bar, 10-second view; generate its 311,368-event trace; verify playback, pause, and reset.
- [x] Confirm the app-side `R` shuffle shortcut manually. Computer Use keyboard injection did not reach the SDL app, while mouse automation worked.

## Scope boundary

Do not add audio synthesis, SDL audio initialization, trace serialization, or another sorting algorithm in this wave. The operation vocabulary should leave room for later algorithms, but only the events required by shift-based Insertion Sort are implemented now.
