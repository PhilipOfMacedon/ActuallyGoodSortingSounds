# Audibilization memory

Working memory for recreating and improving the sound of sorting-algorithm demonstrations. The current target is an independently reproducible system, not a claim that the reference creator used any particular internal technique.

## Notes

- [Research log](research-log.md): reference assets, spectrogram observations, measurements, and confidence boundaries.
- [Engine design](engine-design.md): deterministic event traces, key-turnpoint markers, sampled presentation, audio rendering, seeking, and state reconstruction.
- [Open questions](open-questions.md): unknowns and suggested investigations before implementation planning.

## Current design direction

Keep the application coordinator thin and separate the sorting core, simulation, renderer, and audio engine. The simulation owns canonical state and progress; the renderer consumes read-only presentation data, and audio consumes semantic operations from the same progress. Algorithms should expose logical operations for the simulation to apply, though the exact interface remains undecided.

Make rendering data-driven. Represent highlighted bars as participants with semantic roles rather than fixed index slots, then map roles to colors, textures, materials, or render passes in presentation styling. Keep quads/meshes generic and configurable by render descriptors; avoid per-quad arbitrary callbacks. During a renderer sweep, move each assumption to the layer that owns it: algorithm meaning to operation semantics, canonical state to simulation, appearance to presentation styling, GPU work to rendering, and input/wiring to the application coordinator.

For the eventual audibilization engine, run the sorting algorithm once against a fixed input and record its complete deterministic event trace. Keep that trace as the source of truth. Build a separate presentation timeline of a chosen total duration, preserving marked turnpoints while sampling or summarizing ordinary events between them. Derive visual state and sound from the same event cursor. Use periodic snapshots plus forward replay and inverse mutations for efficient, consistent seeking; do not store a full array copy for every event.

The likely first design target is 800 bars with a user-selectable total simulation duration. A five-second Selection Sort is the stress example discussed so far, not a finalized requirement.
