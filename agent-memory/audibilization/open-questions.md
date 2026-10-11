# Open questions and next investigations

These are deliberately unresolved. Do not turn them into implementation requirements until evidence or an explicit product decision settles them.

## Reference behavior to identify

1. What is the exact bar-to-frequency mapping? Is it linear, logarithmic, table-based, or a fixed floor/ceiling with sample-count-dependent steps?
2. Which low-frequency ridges are fundamentals, persistent background tones, or analysis artifacts? Are high ridges true harmonics of event fundamentals?
3. What are the event attack/release and pitch-slew times? Inspect an isolated event with a short-window/time-domain method; the supplied 2048-sample spectrogram window spans 46.44 ms.
4. Does each comparison start a new voice, update a phase-continuous oscillator, or trigger a sound for only selected comparisons? How are simultaneous voices mixed and normalized?
5. Are the four quadratic algorithms driven by the exact same full input vector? The Insertion/Gnome untouched suffix strongly matches; Bubble and Selection still need appropriate early-section frame checks.
6. Which frequency bounds, bar counts, event delays, stereo behavior, and gain settings were used in each demo section?
7. Can the source audio be analyzed at higher quality than the provided MP4/AAC and MP3 encodes?

## Product decisions for a later implementation plan

- Target bar counts and default total demo duration; the 800-bar/five-second case is currently a stress scenario.
- Which operations produce sound: every comparison, selected comparisons, mutations, checkpoints, or window summaries.
- What a window summary should preserve: candidate values, compared pair, extrema/quantiles, operation counts, or a configurable mix.
- Which semantic checkpoint kinds are common enough to standardize and which are optional annotations.
- Minimum visible/audio duration per high-priority checkpoint and policy if anchors exceed the presentation budget.
- Whether time scaling should preserve pitch by resynthesis/time-stretch, and what rewind should sound like.
- Snapshot cadence and log compression based on measured event sizes and seek-latency targets.
- Whether the user-facing demonstration should expose the full event trace, deterministic seed, and exportable audio/video.

## Suggested investigation order

1. Confirm exact bar counts and segment boundaries from the source video.
2. Compare stable untouched suffixes near the beginning of Bubble and Selection sections against the other quadratic runs.
3. Track a few isolated ridge trajectories in a short-window spectrogram and in the decoded waveform to estimate pitches and envelopes.
4. Decide the smallest common event/checkpoint schema that can represent comparisons, swaps, writes, and batch commits across the intended algorithms.
5. Only then draft an implementation plan for the existing SDL2BarGraphics project.
