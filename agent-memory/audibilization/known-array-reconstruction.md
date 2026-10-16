# Known-array reconstruction of the three Andrut recordings

Analysis date: 2026-10-16 (America/Sao_Paulo).

## Inputs and uncertainty

Authoritative assets are in `x64/Release/Andrut's Reference`:

- `Andrut_Selection_Sort.wav`: mono float32, 44,100 Hz, 733,500 frames.
- `Andrut_Insertion_Sort.wav`: mono float32, 44,100 Hz, 416,388 frames.
- `Andrut_Gnome_Sort.wav`: mono float32, 44,100 Hz, 819,556 frames.
- `video_small_array.png`: 1,218 by 720 pixels; 100 distinct visible heights.

The three WAVs are byte-identical to the corresponding files under the user's
Music/MP3 Soundtracks directory. The reference directory is used for all
reproducible work. SHA-256 hashes are in the listening manifest.

The user confirms positions 3 through 100. The first two bars may already have
been sorted in the captured first video frame. The fixture therefore records
both permissible initial orders: `[78,89,...]` as visible, and `[89,78,...]`.
Rank values are evenly spaced 0 through 99; original numerical magnitudes are
unknown. The earlier 80-bar estimates are superseded for these recordings.

`SoundEnginePreview/research/extract_array.py` independently recovers all 100
visible ranks and checks the source-image hash. It finds no raster-height ties.

## Algorithm schedules

With the visible first pair, ordinary ascending implementations produce:

| Algorithm | Comparisons | Duration at 300 comparisons/s |
|---|---:|---:|
| Selection | 4,950 | 16.500000 s |
| Insertion | 2,818 | 9.393333 s |
| Gnome | 5,542 | 18.473333 s |

The array contains 2,724 inversions. The recordings contain small scene/cut
offsets, so file duration is not identical to comparison duration. Exactly
147 samples per comparison at 44.1 kHz aligns the waveform across each run.

The common source uses an early and a late operand. The effective order is
`(minimum, scanned)` for Selection, `(array[j], held key)` for Insertion, and
`(array[i], array[i-1])` for Gnome. This is an effective timing identification,
not proof of the original comparator source code. The final late-operand clock
offsets in the supplied files are 6,463, 959, and 5,532 samples, respectively.

## Effective synthesis model

For rank `r`, use a continuously pitched, finite sine note:

```text
f(r) = 104.0322699094767 + 39.90533652820316 * r  Hz
T(r) = 9.302650315454065 / f(r)  seconds
h_r(t) = Tukey(t/T(r), alpha=0.025666772580559438) * sin(2*pi*f(r)*t)
```

Each note starts at zero carrier phase and captures its frequency at birth.
Notes from earlier comparisons continue to completion and sum with newer ones.
Launch one note for each operand at every 300 Hz comparison clock tick, with
the late operand 9.95199933051814 samples behind the early operand at 44.1 kHz
(225.668919 microseconds). Apply one common 31-tap FIR response, including the
calibrated output gain, after mixing. Its exact causal coefficients are frozen
in `SoundEnginePreview/research/andrut_delayed_sine_bursts_v1.json`.

The fine decimals are an effective numerical fit, not uniquely identified
physical settings. The useful rounded recipe is roughly 100--4,060 Hz,
nine-cycle sine bursts with very short cosine edges, 300 comparison ticks per
second, and a ten-sample operand offset. The fitted profile spans about
104--4,055 Hz on this array; a 4,200 Hz spectral ridge is not itself a direct
measurement of an oscillator's maximum pitch.

No independent bed, chorus, fixed harmonic-bank focus, or sawtooth source is
used. The only source waveforms are these sine notes. Caching the 100 known
input pitches speeds the offline renderer; the mapping itself is continuous.

## Why the prior observations follow from this model

1. A periodic note train at 300 Hz yields a 300 Hz harmonic family even when
   the underlying note carrier lies between those harmonics. A persistent
   compared value produces a stable periodic field; changing values add
   nonperiodic structure and transitions. A sawtooth is therefore unnecessary
   to explain the harmonic line spacing.
2. Fixed carrier-cycle duration gives `T proportional to 1/f`. Its spectral
   envelope width grows approximately as `1/T`, hence proportional to `f`.
   Lower selections concentrate narrowly; higher selections spread more widely.
3. At fixed peak gain, single-note energy is approximately proportional to
   `T`, hence `1/f`; energy is not explicitly normalized. Coherent overlap
   changes total output energy further, particularly near the clock harmonics.
4. For equal or sufficiently similar source waveforms, the relative operand
   delay has response `1 + exp(-j*2*pi*f*delta)`. It predicts cancellation near
   2,216, 6,647, 11,078, and 15,509 Hz. These correspond closely to the user's
   observed muted regions around 2.1, 6.6, 11.1, and 15.3--15.6 kHz. Different
   compared values need not cancel exactly. This explains the apparent comb
   without identifying a dedicated comb-filter processor in the original.
5. The low note lasts about 89.4 ms, versus about 2.29 ms at the high end.
   Earlier packet experiments capped duration at four clock periods (13.33 ms),
   cutting away much of the low-note overlap. Hann/Gaussian windows also remove
   the relatively flat pulse body favored by this fit.

The old forum comment was traced to Sampo Syreeni on Timo Bingmann's page:
<https://panthema.net/2013/sound-of-sorting/>. It explicitly says "I might be
wrong" about its filter explanation. It remains a hypothesis; the mathematical
delay explanation above comes from the three recordings and does not rely on
that comment as proof. The page's own downloadable app uses a different sound
generator and is not Andrut's implementation.

## Fitting and independent validation

Source parameters and the output FIR were fitted using **Selection only**.
The frozen profile then generates Insertion and Gnome. Their recording offsets
and operand order differ, but source controls, filter, and output gain do not.

| Recording | Correlation | Energy explained, fixed common gain |
|---|---:|---:|
| Selection (fitting recording) | 0.98703 | 97.395% |
| Insertion (validation) | 0.99140 | 98.250% |
| Gnome (validation) | 0.99066 | 98.099% |

Energy explained is `1 - squared-error energy / reference energy` over the
aligned comparison body. First/last 5,000 source frames are excluded to reject
previous-scene sound and file cuts. The primary scores do not fit a separate
gain to each validation recording. Metrics are waveform agreement, not a
percentage of perceptual similarity or proof of the literal original method.
Residual differences remain visible in quieter spectral shoulders.

Mechanism ablations retain the same profile/output filter. Even after allowing
one scalar gain correction, removing the operand offset drops energy explained
to 23.3% / 53.3% / 37.6% (Selection / Insertion / Gnome). Capping durations to
four clock periods gives 86.4% / 81.1% / 84.1%; adding the earlier Gaussian
window to that cap gives 42.0% / 35.8% / 37.6%. These controls identify timing
and overlap as material features of this reconstruction.

Reversing the first pair gives about 95.75% agreement for Selection, versus
97.40% for the visible order. Insertion's later state is unchanged after the
first insertion; Gnome's extra initial comparison is absorbed by a one-event
recording alignment shift. Thus the latter two recordings do not uniquely
settle that first-pair uncertainty, and it is kept explicit.

## Artifacts and verification

Listening pack: `x64/Release/AudioPreviewAndrutReconstruction/README.md`.
It contains three full reconstructions in float32 and PCM16, three sequential
reference/reconstruction A/B reels, complete schedules and metrics in the
manifest, and common-scale waveform/spectrogram comparison plots.

- All nine WAVs are read back and verified; reconstructed peaks remain below
  0.632, with no clipping or non-finite samples.
- The six purely synthesized WAVs were regenerated in a separate process with
  an intentionally unavailable reference directory. All six hashes match.
- The image extraction independently reproduces the saved 100-element fixture.
- The complete profile and array are copied into the output pack.
- Current reference traces reach approximately 33 / 34 / 40 active finite notes.
  These are bounded overlap pools, not a delayed audio event backlog.

This wave adds offline research code and a frozen profile. The existing C++
application and synthesis path have not been modified. A real-time component
can use a fixed voice pool, persistent sample clock, explicit operand offset,
and a short output FIR. It should preserve finite exact-zero note endings and
the current allocation-free callback discipline.
