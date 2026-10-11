# Audibilization research log

## Goal and provenance

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

## Terminology note: aliasing vs. clicks

Digital aliasing occurs when signal energy above the sampling system's Nyquist limit folds into lower frequencies. Speaker physics does not itself create digital aliasing. Abrupt amplitude/phase changes can create clicks and broadband spectral energy; a short envelope can reduce those transients. Abrupt frequency changes can create sidebands, while a phase-continuous oscillator avoids a phase-reset discontinuity. These are related sound-quality issues but not interchangeable explanations.

The reference's 44.1 kHz audio has a 22.05 kHz Nyquist limit. A 4.5 kHz sine fundamental and a few harmonics can remain below it, though higher harmonics or fast modulation can exceed it. The screenshots do not reveal the oscillator waveform or all generated partials.

## Current conclusions

1. Use the reference video as evidence, not as proof of its internal implementation.
2. Reuse a fixed input (or a fixed seed) across algorithms when comparing them.
3. Do not reduce the sound model to “one raw sine per bar value”; event overlap, envelope, phase, pitch mapping, and mixing need separate investigation.
4. A precomputed event trace is a promising design for reproducibility and controllable playback, regardless of whether the reference used one.
