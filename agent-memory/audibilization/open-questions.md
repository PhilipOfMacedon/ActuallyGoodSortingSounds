# Open questions and next investigations

These are deliberately unresolved reference-audio or future-audio questions. They do not block the implemented sorting simulation. Do not turn them into requirements until evidence or an explicit product decision settles them.

## Reference behavior to identify

1. What is the exact bar-to-frequency mapping? Is it linear, logarithmic, table-based, or a fixed floor/ceiling with sample-count-dependent steps?
2. Which low-frequency ridges are fundamentals, persistent background tones, or analysis artifacts? Are high ridges true harmonics of event fundamentals?
3. What are the event attack/release and pitch-slew times? Inspect an isolated event with a short-window/time-domain method; the supplied 2048-sample spectrogram window spans 46.44 ms.
4. Does each comparison start a new voice, update a phase-continuous oscillator, or trigger a sound for only selected comparisons? How are simultaneous voices mixed and normalized?
5. Are the four quadratic algorithms driven by the exact same full input vector? The Insertion/Gnome untouched suffix strongly matches; Bubble and Selection still need appropriate early-section frame checks.
6. Which frequency bounds, bar counts, event delays, stereo behavior, and gain settings were used in each demo section?
7. Can the source audio be analyzed at higher quality than the provided MP4/AAC and MP3 encodes?

## Decisions already made for the simulation

- Initial defaults are 80 bars and 10 seconds; supported limits are 2–800 bars and 1–120 seconds. The 800-bar case is a stress limit, not the default demonstration size.
- The first implementation catalogs only shift-based Insertion Sort, with ascending/descending selected through one direction setting. Its trace records comparisons, held-key/gap transitions, shifts, placements, and completion.
- Startup and explicit Shuffle use recorded seeds. Insta-sort and Insta-invert prepare ascending and descending baselines over one stable item set. Reset restores the latest prepared baseline and cursor zero.

## Product decisions for a later audio implementation

- Which operations produce sound: every comparison, selected comparisons, mutations, checkpoints, or window summaries.
- What a window summary should preserve: candidate values, compared pair, extrema/quantiles, operation counts, or a configurable mix.
- Which semantic checkpoint kinds are common enough to standardize and which are optional annotations.
- Minimum visible/audio duration per high-priority checkpoint and policy if anchors exceed the presentation budget.
- Whether time scaling should preserve pitch by resynthesis/time-stretch, and what rewind should sound like.
- Whether to extend the current event schema with swaps, range writes, checkpoints, and batch commits when additional algorithms are introduced.
- Snapshot cadence and log compression beyond the current 4,096-event seek snapshots, based on measured trace size and latency.
- Whether the user-facing demonstration should expose the full event trace, deterministic seed, and exportable audio/video.

## Suggested investigation order

1. Confirm exact bar counts and segment boundaries from the source video.
2. Compare stable untouched suffixes near the beginning of Bubble and Selection sections against the other quadratic runs.
3. Track a few isolated ridge trajectories in a short-window spectrogram and in the decoded waveform to estimate pitches and envelopes.
4. Extend the event/checkpoint vocabulary only when another algorithm is selected; the current schema covers shift-based Insertion Sort.
5. Investigate the remaining audio decisions before adding synthesis to the existing trace boundary.

## Deferred fallback for the SoundEngine prototype

The current offline prototype uses a 60-band harmonic bank with exponential comparison tails. If listening review shows that this profile does not resemble the target effect closely enough, revisit a spectrogram-based reverse-engineering pass: estimate the frequency-bin intensity distribution over time/value, separate the comparison-focus peak from its quieter super-harmonic tail, and derive candidate per-bin weights. Keep the focus peak capped at 4.2 kHz while allowing the observed harmonic tail to extend through 17.1 kHz. This remains a fallback investigation, not a requirement for the current preview gate.

## Current Andrut similarity experiment

The latest offline pack is `x64/Debug/AudioPreviewModelComparisons`. It contains nine dry clips of the same 79-comparison ordered Insertion Sort sweep, crossing phase-aware versus magnitude-only bed comb shaping, `1/k` through `1/k^2` bed profiles, a 25% master reduction, quieter bed levels, and either fixed tau or a tau that grows from 0.1 to 0.5 bins across the value range. The generated `README.txt` records each setting.

The source Andrut WAV measured about 0.0759 RMS / 0.4867 peak overall. A previous local dry preview measured 0.3305 RMS / 0.8294 peak; the new reduced variants range roughly 0.153–0.196 RMS / 0.340–0.440 peak. That closes some loudness distance, but most variants remain about twice the reference RMS. These comparisons are not loudness-normalized. Coherent bed/comparison phases and the conservative headroom bound can make bed-level changes non-monotonic in total RMS, so master level is the reliable global trim. The remaining difference is not explained by gain alone, and none of these observations proves that Andrut used a comb filter or this particular envelope.
