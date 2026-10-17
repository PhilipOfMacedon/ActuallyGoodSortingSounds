# Audibilization research log

## Goal and provenance

Playback follow-up (2026-10-17): [pause/resume and single-step diagnosis](pause-resume-debug-2026-10-17.md).
The mid-run failure was a preparation/device-recovery feedback loop, with
callback acknowledgement and mailbox ownership fixes. The frozen acoustic
profile was not retuned. Portable packaging is deferred to the next major
milestone at the user's request.

Latest result (2026-10-16): [known-array reconstruction](known-array-reconstruction.md).
The supplied image has 100 bars; the first two initial values remain temporally
uncertain. A common delayed, overlapping sine-burst profile fitted on Selection
explains 97.4% of its waveform energy and 98.3% / 98.1% on the independent
Insertion / Gnome recordings. The frozen profile and reproducible offline
listening pack are documented in that note. Earlier 80-bar estimates and
standalone-bed interpretations below are historical hypotheses.

The user has been trying to mathematically recreate the audio of the sorting-algorithm video **“What different sorting algorithms sound like”** by Andrut. The aim is to reverse-engineer the observable behavior and build a better, reproducible implementation. The reference creator's private implementation is unknown and does not constrain the design.

Reference video: <https://www.youtube.com/watch?v=t8g-iYGHpEA>

The video's description says that comparisons mix sine waves whose frequencies are modulated by the compared values. This is useful evidence that comparison events and value-controlled pitches are involved, but it does not specify pitch mapping, envelopes, phase behavior, mixing, or whether rendering is live or offline.

### Local reference assets

- Video: `C:\Users\kasin\Downloads\iYGHpEA_What-different-sorting-algorithms-sound-like_001_480p.mp4`
- Audio: `C:\Users\kasin\Music\MP3 Soundtracks\What different sorting algorithms sound like.mp3`
- User-provided screenshots are in the conversation and show Audacity's spectrogram view/settings.

Previously inspected media metadata:

- MP4: about 89.792 seconds, 800×480 video at 30 fps, AAC stereo audio at 44.1 kHz.
- MP3: about 89.783 seconds, mono at 44.1 kHz, 192 kb/s.
- The visible section labels are Insertion Sort, Bubble Sort, Selection Sort, Merge Sort, and Gnome Sort. Insertion, Bubble, Selection, and Gnome are the four quadratic examples; Merge Sort is the faster comparison.

## User-provided run context

- The user initially confirmed that two inspected samples were produced with 80 bars.
- The user later clarified that the slow algorithms use 80 bars, while faster algorithms use at least four times as many to keep demo durations comparable.
- The user suspects the four O(n²) demonstrations reuse the same randomized input, analogous to restoring a saved state.
- A later hypothetical design target was 800 bars, Selection Sort, and a five-second total presentation. This is a stress example, not yet a committed product requirement.

## Spectrogram settings and interpretation limits

The supplied Audacity settings screenshot shows:

- Mel frequency scale, 1 Hz minimum and 20 kHz maximum.
- Gain 20 dB, range 80 dB, zero high-frequency boost.
- Frequencies algorithm, Hann window, 2048-sample window, zero-padding factor 2.
- The source audio is 44.1 kHz.

At 44.1 kHz, a 2048-sample window spans about **46.44 ms**. Its true FFT-bin spacing is about **21.53 Hz**. Two-times zero padding displays a denser grid (about **10.77 Hz** spacing) but does not double the underlying frequency resolution. The Mel scale also makes vertical pixel distances nonlinear in Hz. This setup is good for broad pitch structure but cannot establish a 10–30 ms attack/release envelope precisely.

## Observations and measurements

### Frequency structure

- The screenshots show persistent low-frequency ridges below 300 Hz, plus strong bands around approximately 300 Hz and its apparent multiples. This refutes the earlier overly-simple “300 Hz is the floor” interpretation.
- Other views show stepped energy extending to roughly 4.5 kHz and visible upper ridges approaching 18 kHz. The relation between those ridges and fundamentals has not been proven; they may be harmonics, overlapping event voices, or both.
- Exploratory FFT checks found components around 104 Hz and 140–148 Hz in a short late clip, alongside recurring energy near the 300 Hz family. This confirms sub-300 Hz content in that segment; it does not establish an exact denominator/frequency mapping.
- Another inspected segment had stepped energy near 600, 900, 1200 Hz and upward to roughly 3.9–4.5 kHz, with recurring energy near 300 Hz. This is consistent with changing pitches, but does not identify the oscillator or bar-to-frequency formula.
- A fixed frequency floor and ceiling with a rank step that changes with bar count is a plausible hypothesis raised by the user. A simple candidate would be a bounded mapping such as `f(v,N)=f_min+(f_max-f_min)*v/(N-1)`, but neither that formula nor its endpoints are verified.
- Spectrogram ridges alone do not prove harmonicity. To classify partials, track whether ridges remain integer multiples of the same fundamental with matching timing and envelope.

### Same-input evidence

Frames decoded from the reference video at approximately **0.2 s** (Insertion Sort) and **72.2 s** (Gnome Sort) show different processed prefixes but the same bar-height sequence in the untouched suffix, from about the 22nd bar through the right edge. The per-column bar-top sequence matches across that suffix (roughly the last 58 of 80 bars, given the display width/bar spacing); small pixel differences remain from lossy video encoding.

This is strong evidence that these two examples use the same underlying permutation, or at minimum the same long untouched suffix. It makes an independently randomized input for each section very unlikely. It does **not** by itself prove the complete initial vectors are identical or verify Bubble Sort and Selection Sort. The likely explanations include reloading the same vector or deterministically regenerating it with the same seed.

## Hypotheses: keep separate from facts

- **Per-event envelopes:** Short attack/release envelopes or pitch slews could make rapid comparison tones smoother and reduce clicks/spectral splatter. The current spectrogram window and overlapping audio do not establish an exact envelope or duration.
- **Offline/precomputed rendering:** A recorded event trace rendered afterward would naturally support careful smoothing, deterministic comparisons, and synchronized visuals. The clip alone cannot distinguish this from a live engine using the same trace or deterministic input.
- **Shared quadratic-sort input:** Strongly supported for the inspected Insertion/Gnome suffix; unverified for all four quadratic demonstrations.
- **Fixed pitch range across sample sizes:** Plausible based on the user's comparison of 80-bar and larger runs; requires direct measurement across known bar counts.

## Later source-model reassessment (2026-10-14)

The supplied `Andrut_Ordered_Sweep_2.wav` was measured directly and compared with the chosen engine preview. See [ordered-sweep-2-analysis.md](ordered-sweep-2-analysis.md) for reproducible output locations, measurements, and mathematical candidates. The 300 Hz spacing is verified as periodic structure, but identifying it as a sawtooth or a literal fixed-bin focus model is not justified. Repeating sine packets and changing source/filter parameters are priority hypotheses; constant energy and role-specific harmonic versus off-harmonic voices remain unproven.

## Terminology note: aliasing vs. clicks

Digital aliasing occurs when signal energy above the sampling system's Nyquist limit folds into lower frequencies. Speaker physics does not itself create digital aliasing. Abrupt amplitude/phase changes can create clicks and broadband spectral energy; a short envelope can reduce those transients. Abrupt frequency changes can create sidebands, while a phase-continuous oscillator avoids a phase-reset discontinuity. These are related sound-quality issues but not interchangeable explanations.

The reference's 44.1 kHz audio has a 22.05 kHz Nyquist limit. A 4.5 kHz sine fundamental and a few harmonics can remain below it, though higher harmonics or fast modulation can exceed it. The screenshots do not reveal the oscillator waveform or all generated partials.

## Current conclusions

1. Use the reference video as evidence, not as proof of its internal implementation.
2. Reuse a fixed input (or a fixed seed) across algorithms when comparing them.
3. Do not reduce the sound model to “one raw sine per bar value”; event overlap, envelope, phase, pitch mapping, and mixing need separate investigation.
4. A precomputed event trace is a promising design for reproducibility and controllable playback, regardless of whether the reference used one.

## Reconstructed playback completion wave (2026-10-16)

The application has switched from the experimental harmonic bank to the frozen
delayed-sine-burst profile described in `known-array-reconstruction.md`. The
profile JSON remains unchanged. Its C++ renderer matches the independent
Python reference after only the documented 96-frame relative alignment:

| Schedule | Comparisons | Maximum absolute PCM error | RMS error |
|---|---:|---:|---:|
| Selection, 100-item Andrut permutation | 4,950 | 3.44784e-7 | 6.05716e-8 |
| Insertion, 100-item Andrut permutation | 2,818 | 3.26363e-7 | 5.20083e-8 |
| Gnome, 100-item Andrut permutation | 5,542 | 4.42972e-7 | 4.99454e-8 |

The event fixtures are checked independently from PCM, including Gnome's
declared operand reversal. No extra gain or filter fit is used. The maximum
sample error is below the 2e-6 acceptance threshold.

Playback uses a stable sample-time anchor, separate from the advancing
playhead. Fixed-duration mode maps the full trace to the selected duration;
reference cadence places eligible events at 300 comparisons/second. A
128-frame scheduling lead is fixed and is not accumulated into elapsed time.
The callback receives immutable prepared timelines and bounded seek snapshots
through revisioned latest-state handoff, then renders preallocated notes. Seek
history uses the current settings and installs with a short crossfade. Missing
audio or failed preparation falls back to host-clock simulation.

Validation completed for x64 and Win32, Debug and Release: SoundEngine, App
Framework, and SortingLogic console suites passed. SoundEngine's resampler
checks include 22.05, 32, 44.1, 48, and 96 kHz. The generated pack contains 55
paired clips: three complete reference rerenders and 22 requested scenario
groups, with raw float32, PCM16, manifest, and event fixtures under ignored
`x64/Release/AudioPreviewReconstructedPackFinal`.

During the portable-package smoke run, a delayed playhead report from an older
transport command could cancel a newly started playback on the same trace.
Playhead feedback now carries a transport revision as well as a timeline
generation, and the app ignores reports unless both still match. An
AppFramework regression covers stale pause feedback, current playback
advancement, and normal completion. All four build configurations and suites
were rerun after this fix.

The x64 portable ZIP was extracted to a clean sibling folder and launched
without Visual Studio or source files. The extracted app built a trace and
advanced its event cursor during playback. The ZIP includes app-local SDL,
GLEW, and MSVC runtime files, font, shaders, and third-party notices. A
recipient-machine check remains useful for graphics-driver and audio-device
differences; this local smoke test cannot establish behavior on other Windows
hardware.

The current implementation does not establish operation-specific audio
semantics for every future algorithm. Before expanding the roster, decide
whether reads, writes, swaps, partition pivots, merge auxiliary comparisons,
and radix/counter operations should create sounds, then verify those mappings
against immutable traces.
