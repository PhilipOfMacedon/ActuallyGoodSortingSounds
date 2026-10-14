# Audibilization memory

Working memory for recreating and improving the sound of sorting-algorithm demonstrations. The current target is an independently reproducible system, not a claim that the reference creator used any particular internal technique.

## Notes

- [Research log](research-log.md): reference assets, spectrogram observations, measurements, and confidence boundaries.
- [Engine design](engine-design.md): deterministic event traces, key-turnpoint markers, sampled presentation, audio rendering, seeking, and state reconstruction.
- [Open questions](open-questions.md): unknowns and suggested investigations before implementation planning.

## Current design direction

Keep the application coordinator thin. A dependency-free `SortingLogic` static library owns the sorting core, deterministic event trace, and simulation; the app owns SDL/OpenGL rendering and controls. The simulation owns canonical state and progress; the renderer consumes copied read-only presentation snapshots, and a future audio engine can consume the same ordered semantic trace. Insertion Sort is the only algorithm in the first wave, with ascending/descending as a comparator setting.

Make rendering data-driven. Represent highlighted bars as participants with semantic roles rather than fixed index slots, then map roles to colors, textures, materials, or render passes in presentation styling. Keep quads/meshes generic and configurable by render descriptors; avoid per-quad arbitrary callbacks. Batch bars by material. During a renderer sweep, move each assumption to the layer that owns it: algorithm meaning to operation semantics, canonical state to simulation, appearance to presentation styling, GPU work to rendering, and input/wiring to the application coordinator.

For the eventual audibilization engine, run the sorting algorithm once against a fixed input and record its complete deterministic event trace. Keep that trace as the source of truth. Build a separate presentation timeline of a chosen total duration, preserving marked turnpoints while sampling or summarizing ordinary events between them. Derive visual state and sound from the same event cursor. Use periodic snapshots plus forward replay and inverse mutations for efficient, consistent seeking; do not store a full array copy for every event.

The first implementation defaults to 80 bars and 10 seconds, with 2–800 bars and 1–120 seconds. Trace generation uses copied input on a worker, while playback and rendering stay on the app thread. Seeded Shuffle, Insta-sort, Insta-invert, Reset-to-prepared-baseline, event stepping, seeking, and snapshots every 4,096 events were in scope for the architecture wave. Subsequent work added the separate offline `SoundEngine` prototype and deterministic WAV preview packs; SDL playback and settings integration remain unimplemented. See [the refactoring roadmap](../refactoring-roadmap.md) for the original architecture acceptance checks and `SoundEngine/README.md` for current audio experiments.
