# SoundEngine prototype

`SoundEngine` is a C++14 static library with no SDL, OpenGL, or `SortingLogic` dependency. Callers normalize values from the prepared input range, then send `AudioCommand`s in the same order as the visible trace cursor. The synthesizer applies each command immediately and retargets its band envelopes to the newest comparison; it does not retain a delayed event queue.

## Frequency bank and focus mapping

The bank contains 50, 100, and 150 Hz plus the 300 Hz harmonic series from 300 through 17,100 Hz (`300 × 1…57`), for 60 bands. Harmonics use a `1/k` base amplitude; the three low components start 12 dB below the 300 Hz fundamental. Linear-Hz, logarithmic-frequency, and equal-band-slot mappings all limit focus centers to 50–4,200 Hz. Bands above 4,200 Hz remain available to the continuous bed and to comparison-envelope tails. The 50/100/150 Hz bands remain in the comparison bank but are excluded from the continuous bed by default, so a low-valued comparison can bring them forward without making them part of the constant hum. Set `Parameters::includeSub300HzBed` to `true` only for experiments that explicitly want those components in the bed.

Comparison emphasis is an exponential amplitude envelope measured in band steps around the mapped focus location. The lower and upper sides retain separate `tauBelowBins` and `tauAboveBins` controls, while current defaults are symmetric at 0.5 bins. At an exact band center, the adjacent band receives about 13.5% of peak amplitude (`exp(-1 / 0.5)`). The selected preview profile defaults to master `0.50`, bed `0.15`, harmonic exponent `1.5`, tau `0.5/0.5`, and 8 ms attack/release. Chorus remains disabled by default.

## Simulator integration

The SDL application opens a stereo float audio device at 44.1 kHz when available. Audio initialization is non-fatal, and output remains silent until a trace is loaded. An app-side bridge maps stable item IDs from the trace's prepared input to normalized comparison positions, carrying the Insertion Sort held-key role. Reset keeps the continuous bed and clears comparison focus; completion also clears focus; clearing or replacing a trace fades the bed out.

The audio callback owns the `SoundEngine::Synthesizer`. The app publishes fixed-size snapshots through a triple-buffer latest-state mailbox, so a seek or dense frame replaces pending state rather than queuing stale comparisons. The callback does no allocation or waiting. Audio parameters live in version-2 `settings.ini` alongside appearance and simulation preferences; version-1 files keep their existing values and receive this profile's audio defaults. The Audio settings tab can change every synthesis parameter and all 60 band trims while playback continues.

## Listening previews

The preview executable uses the real `SortingLogic` Insertion Sort trace builder to create deterministic comparison patterns. It writes four dry clips: fixed low/high alternating comparisons and trace-derived comparisons at eight and 16 changes per second. The trace clips sample 40 or 80 comparison events evenly across a fixed seeded 80-item run and retain held-key roles. Every clip uses the symmetric 0.5-bin focus, 15% bed, and 8 ms attack/release so the faster event rates can be compared directly.

Build the x64 Debug preview target, then run:

```powershell
.\x64\Debug\SoundEnginePreview.exe .\x64\Debug\AudioPreviewFast
```

The output folder includes a `README.txt` index. The earlier refined pack remains available for comparison but is superseded by this focused timing and envelope pack. Spectrogram-based bin-weight extraction remains a fallback investigation if the exponential harmonic profile does not reproduce the desired target effect.

## Experimental comb-shaped harmonic bed

The bed's 300 Hz partials already use phase-locked `1/k` amplitudes, which is the magnitude profile of a truncated sawtooth Fourier series. The experiment adds a frequency-dependent magnitude response to those bed partials only. Comparison energy, including the low 50/100/150 Hz bands, remains separate and unfiltered.

`BedCombFilterParameters` models a normalized two-path comb response:

```text
H(f) = (1 - w/2) + (w/2) * exp(-j * (2*pi*f*tau + phase))
```

Its default experimental fit uses a 222.222 microsecond delay (4.5 kHz notch spacing), a 12 degree phase offset, 95% wet through 6.6 kHz, then a cubic fade to 90% at 15.6 kHz. This predicts deep nulls around 2.1, 6.6, and 11.1 kHz, and a softer null around 15.6 kHz. It does not independently explain the 15.3 kHz dip in the reference. The steady-state magnitude is evaluated once per bank partial and smoothed on changes, so the audio callback adds no delay-line processing or per-sample comb trigonometry. The selected preview profile enables phase-aware comb settings by default; the spectrogram resemblance does not prove Andrut used this circuit.

Generate four paired ordered-sweep clips (flat/comb bed at 0% and 20% envelope-weighted chorus):

```powershell
.\x64\Debug\SoundEnginePreview.exe --comb-bed-sweep .\x64\Debug\AudioPreviewCombBed
```

Each file uses the same two-second, 79-comparison sweep through Insertion Sort on an ordered 80-item input. The generated folder's `README.txt` documents the exact settings. No app-side playback or settings integration is part of this listening experiment.

## Ordered sweep chorus A/B

Run the preview executable with `--ordered-sweep` to create six two-second clips under `AudioPreviewOrderedSweepBed300`. They use an already-ordered 80-item input and the real Insertion Sort trace, yielding 79 adjacent-key comparisons swept evenly from the low to high end. The matrix crosses 0%, 10%, and 20% chorus mix with two `ChorusWeighting` modes: `Uniform` sends the complete harmonic sum through the existing modulated delay, while `ComparisonEnvelope` weights the delayed signal by the normalized comparison envelope across the frequency bands. The 50/100/150 Hz bands stay available to comparisons but are excluded from the continuous bed; the bed starts at 300 Hz. The 0% pair is identical; at nonzero mixes, the modes reveal whether concentrating chorus around the active comparison helps produce Andrut’s smoother sweep.

Example:

```powershell
.\x64\Debug\SoundEnginePreview.exe --ordered-sweep .\x64\Debug\AudioPreviewOrderedSweepBed300
```

These are offline listening experiments. The synthesizer defaults to uniform chorus weighting.

## Andrut similarity model sweep

Run `--model-sweep` to write nine dry, two-second ordered-sweep variants to `AudioPreviewModelComparisons`:

```powershell
.\x64\Debug\SoundEnginePreview.exe --model-sweep .\x64\Debug\AudioPreviewModelComparisons
```

All variants reuse the same 79 adjacent comparisons from the ordered 80-item Insertion Sort trace. The comparison focus travels from the lowest to highest values, with held-key roles preserved. The matrix varies these modeling choices:

- magnitude-only versus phase-aware comb shaping of the harmonic bed;
- harmonic-bed exponent `p=1`, `1.5`, or `2`, changing the bed partials from `1/k` to `1/k^p`;
- master level `0.50` versus `0.375` (a 25% output reduction), plus bed levels `0.15`, `0.1125`, and `0.0375`;
- fixed comparison tau (`0.1`, `0.25`, or `0.5` bins) versus a position-scaled tau that grows from `0.1` to `0.5` bins across the value range.

The comb magnitude stays identical between magnitude-only and phase-aware variants. Phase-aware rendering rotates each bed partial by the complex response phase; it does not comb-filter the comparison tones. Master gain is applied after headroom control, so reducing it remains an actual output-level change. These are listening hypotheses, not claims about Andrut’s implementation; engine defaults remain unchanged until a variant is selected for further work.

## Validation

`SoundEngineTests` covers the exact frequency bank, focus limits in all mapping modes, zero-width normalization, exponential tails, held-key gain, phase continuity, smooth setting changes, both chorus weighting modes, chorus sidebands, headroom, silence, and dense comparison retargeting.
