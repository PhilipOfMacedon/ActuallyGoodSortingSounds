# Proposed engine design

This records the current architecture discussion. It is not an implementation plan or a commitment to a particular framework.

## 0. Agreed conceptual boundaries

Keep the application coordinator thin: it wires the subsystems together and routes user commands. Do not turn a central “core” object into a manager that owns algorithm rules, simulation state, rendering, and audio.

- **Sorting core:** defines the algorithms and their semantic operations. It has no SDL, OpenGL, renderer, or audio dependency.
- **Simulation:** owns the authoritative sorting state and progress. It applies algorithm operations and exposes the state needed for presentation.
- **Renderer:** consumes read-only presentation data and draws it. It does not infer sorting meaning by watching array mutations.
- **Audio:** consumes semantic operations or summaries from the same simulation progress. It does not infer events from rendered frames or mutate sorting state.
- **Application coordinator:** connects these parts and routes input; it contains no sorting or rendering policy.

The direction favored for a simulation-based system is for algorithms to expose logical operations and for the simulation to apply them. The exact operation-production interface remains open until the algorithm/event vocabulary and playback requirements are decided.

### Flexible rendering and visual state

Treat highlighting as presentation derived from semantic operation state, not as a fixed property of a bar or a two-index side channel. Operations may identify a variable number of participants and roles, such as compared items, a pivot, a destination, or a write target. The simulation can expose those roles in a read-only view; the renderer maps them to appearance.

Keep the quad/mesh primitive generic. A draw instance may select a render descriptor or material containing properties such as tint, texture, and shader/pass. This supports different looks without teaching a generic quad about sorting algorithms. Prefer data-driven material/style selection over an arbitrary per-quad drawing callback; custom drawing behavior belongs in renderer-level strategies or passes so resource ownership, ordering, and batching remain tractable.

During a renderer sweep, classify each assumption by where it belongs:

- Algorithm meaning (compare, swap, write, pivot, completion) belongs in the sorting operation vocabulary.
- Current algorithm progress and canonical values belong in simulation state.
- Role-to-color/texture/material choices belong in presentation styling or themes.
- Geometry, GPU resources, draw ordering, and render passes belong in the renderer.
- Input routing and subsystem construction belong in the application coordinator.

Do not blindly move every special case into the algorithm. Algorithm-specific semantics should originate with the algorithm, while their appearance remains a renderer policy. The sweep should remove assumptions that leak across these boundaries, including the current fixed two-highlight slots, single red highlight appearance, and sorting-specific behavior in generic drawing paths. The existing post-sort `uselessScan()` is a presentation flourish rather than a sorting operation; decide whether to retain it as an explicit presentation effect or remove it before it can be confused with algorithm/audio events.

Current code points to audit: `SDL2BarGraphics/SDL2BarGraphics.cpp` allocates two highlight slots; `SDL2BarGraphics/ArraySorter.cpp` writes those positions; `SDL2BarGraphics/ArrayRenderer.cpp` maps every highlighted position to one red rectangle; `SDL2BarGraphics/Rectangle.cpp` bakes a vertex color into the mesh; and `SDL2BarGraphics/Mesh.cpp` binds one texture and shader for each draw. These are observations about the current implementation, not requirements for the replacement.

## 1. Record a complete deterministic run once

Start from an immutable input vector with stable item identities. Record the sort's observable operations into an append-only trace. At minimum, the operation interface should support:

- `compare(i, j, result)`
- `swap(i, j)`
- `write(i, oldValue, newValue)`
- range/bulk writes with before/after data or equivalent per-cell deltas
- batch begin/commit boundaries
- optional semantic checkpoints

Record random seeds or actual random decisions, and the comparator result used. Replay should consume recorded results rather than invoke the comparator or random generator again. If the displayed model includes auxiliary arrays or buffers, include those mutations too.

An event sequence number defines total order, even when multiple events share one logical time. Store item IDs separately from values so duplicate-valued bars remain distinguishable and stability can be shown.

## 2. Separate algorithm time from presentation time

The complete trace is the ledger. The presentation timeline maps it to a user-selected total duration. At each video/audio control time, advance through every due event to compute the correct state, while showing or sonorizing only a sampled or summarized representation.

For 800-item Selection Sort:

- Comparisons: `800*799/2 = 319,600`.
- At a five-second total duration: about 63,920 comparisons/second, or **15.64 microseconds/comparison**.
- At a 10 ms presentation/audio-control interval (100 Hz): about **639 comparisons** per update.
- At 20 ms (50 Hz): about **1,278 comparisons** per update.
- At 60 visual frames/second: about **1,065 comparisons** per frame.

The 10–20 ms value is a control/update interval, not the PCM sample rate. The synthesizer still renders at 44.1/48 kHz and can interpolate or envelope controls sample by sample. A naive every-kth-operation sampler may miss salient patterns; deterministic per-window summaries can use representative comparison values, candidate values, mutation counts, and the exact final array state for that window.

Keep ordinary operation reduction independent of state correctness: every mutation in the trace is applied, even if its intermediate display/audio presentation is omitted.

## 3. Method-neutral key-turnpoint markers

Provide a shared checkpoint API that any algorithm can use, for example:

```text
ctx.checkpoint(scope=[lo, hi], kind="commit", salience=2)
```

The checkpoint records its exact trace index/state version, scope, kind, and priority. Merge Sort can mark a completed merged range; Counting Sort can mark a committed rewrite batch. The engine handles both as generic commit markers without needing algorithm-specific rendering branches.

Bulk mutation batches provide useful automatic checkpoints, while explicit markers can highlight higher-level milestones. Timeline fitting should protect high-priority anchors from downsampling, reserve a minimum presentation slot where feasible, and allocate ordinary work between anchors. If the number of required distinct anchors exceeds the number of legible frames/audio slots, combine lower-priority markers or lengthen the demonstration; a fixed short duration cannot make an unbounded number of landmarks individually perceptible.

## 4. State reconstruction, seek, and rewind

The canonical state at cursor `k` is the initial state with events `[0,k)` applied. Do not store a full array copy for each event.

- Keep one mutable working state plus periodic snapshots keyed by event index.
- Forward playback applies the next event range.
- A nearby rewind uses inverse mutations in reverse order: swaps are self-inverse; writes store old/new values; bulk writes retain enough old data to restore the prior range.
- A large seek restores the nearest preceding snapshot, replays forward into a scratch state, then atomically swaps that state into view at a frame boundary.
- Store periodic hashes/checksums to detect log/state divergence.
- Select snapshot intervals by replay cost and mutation volume, not only by raw event count.

Memory example for 800 32-bit values: one snapshot is about 3.2 KB. Ten thousand snapshots (one per 1,000 events in a 10-million-event trace) use about 32 MB of raw values, compared with about 32 GB for 10 million complete copies. Storing a 32-bit item ID alongside each value approximately doubles those raw figures; allocator/index overhead is additional.

Time-to-cursor conversion should use a stable rule such as “apply every event with timestamp <= playhead, ordered by sequence number.” Interpolated bar motion is a display overlay; the underlying state remains an exact discrete event state.

## 5. Audio rendering and playback controls

Generate sound from the same trace cursor or from deterministic summaries of trace windows. Keep the PCM render clock separate from event/control cadence. Candidate mechanisms to investigate include phase-continuous oscillators, short per-voice envelopes, bounded polyphony, conservative headroom/limiting, and deterministic phase/envelope restoration on seek.

Changing playback rate by replaying an already-rendered file changes pitch. If pitch must remain fixed, resynthesize/re-time events or use a suitable time-stretch method. Rewind should replay events backward or restore/replay state while generating normal forward envelopes; reversing the waveform itself is a different, usually undesirable effect.

## 6. Edge cases to guard

- Duplicate keys and stable-sort identity; empty/one-item/all-equal/already-sorted/reverse-sorted arrays.
- Zero-width value ranges and `N < 2` in pitch mapping.
- Inconsistent comparators, NaN/infinity policies, and operation limits.
- Out-of-place algorithms, auxiliary structures, range rewrites, and interrupted batches.
- Same-time event/marker ordering, too-dense checkpoints, and a final state that falls just beyond a sampled frame.
- Randomized pivots/seeds, event-log versioning, cancellation, and incomplete runs.
- Voice buildup/clipping, phase reset, seeks during fades, playback-rate changes, and audio-buffer underruns.
- Parallel operation logs: capture a deterministic total order or serialize the logical event schedule.

## Correctness invariant

For every event cursor `k`, the visible canonical array must equal the result of applying exactly the first `k` state mutations to the recorded initial state. Timeline sampling may skip intermediate presentations, but must not change this invariant.
