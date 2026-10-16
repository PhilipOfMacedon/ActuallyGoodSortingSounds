# Known-array Andrut reconstruction

This is offline analysis and synthesis tooling. It uses the three user-supplied
recordings and the extracted 100-element arrangement to distinguish synthesis
mechanisms using the same known comparison sequences. Python analysis packages
remain separate from application dependencies.

The effective model is **overlapping finite sine bursts, with a small relative
onset offset between comparison operands**. The clock produces the harmonic
family; no separate bed, fixed harmonic-bank oscillator, sawtooth generator,
or chorus is needed in this reconstruction.

## Reproduce the listening pack

From the workspace root, use the workspace Python executable:

```powershell
& 'C:\Users\kasin\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' SoundEnginePreview\research\create_listening_pack.py
```

Default reference directory: `x64/Release/Andrut's Reference`.
Default output directory: `x64/Release/AudioPreviewAndrutReconstruction`.
The index is `README.md` inside that directory.

The frozen `andrut_delayed_sine_bursts_v1.json` contains every source control,
the common 31-tap output FIR including gain, and recording alignments.
`andrut_array.json` records the permutation, source-image hash, and the initial
first-pair uncertainty. Synthesis uses these two files without reading recorded
PCM. To demonstrate that separation:

```powershell
python SoundEnginePreview/research/create_listening_pack.py --synthesis-only --references NonexistentDirectory --output x64/Release/AudioPreviewAndrutReconstructionRepro
```

The six synthesized WAV hashes must match the normal pack. Reference WAVs are
read only to measure agreement and produce explicitly labelled A/B excerpts.

Outputs include full float32 stereo and PCM16 stereo reconstructions of all
three algorithms, three A/B listening reels, waveform and spectrogram plots,
a cancellation prediction, and a manifest with complete event sequences.
The same source/filter/gain is used for all recordings. Each WAV is read back
and checked, and PCM clipping is rejected. A/B excerpts have identical 5 ms
edge fades; the full reconstructions do not receive those excerpt fades.

## Scripts

- `extract_array.py`: independently recover all 100 visible ranks from the PNG
  staircase and compare them with the fixture. Its first-pair uncertainty is
  temporal, not a tie in the image's measured heights.
- `analyze_andrut.py`: generate the three deterministic comparison schedules;
  inspect recording metadata, clock folding, and spectrograms.
- `fit_segments.py`: separate frequency-map error from unknown local phases and
  amplitudes by projecting comparison interiors onto two sine/cosine pairs.
- `identify_kernels.py`: diagnostic shared impulse-response identification.
  Good training fits alone were insufficient; cross-algorithm transfer exposed
  the need to investigate operand timing.
- `fit_note_model.py`: reusable analytic sine-note renderer and fitting helpers;
  compare duration and window hypotheses. Arbitrary frequencies are described
  by a continuous linear map; caching this input's 100 notes is an offline
  optimization, not a harmonic frequency bank.
- `optimize_notes.py`: earlier same-onset note experiment. Its weaker transfer
  is retained as research history.
- `refine_delayed_notes.py --wide`: fit the compact source on Selection, then
  validate the frozen source and filter on Insertion and Gnome.
- `freeze_profile.py`: freeze a fit into the standalone profile. Generation of
  an existing pack does not rerun fitting or need fitting result files.
- `create_listening_pack.py`: synthesis, export validation, measurements,
  mechanism ablations, first-pair uncertainty checks, and plots.

The local scientific packages are discovered at
`x64/Release/AudioAnalysis/packages`. This directory already contains NumPy,
SciPy, Matplotlib, and Pillow from the earlier audio analysis.

## Evidence and limits

See `agent-memory/audibilization/known-array-reconstruction.md` for the measured
result and mathematical interpretation. Selection is the fitting recording;
Insertion and Gnome are validation recordings. Timing offsets and comparator
operand order are identified separately, because the clips start at different
places and commutative comparison pairs do not preserve sound timing order.

The energy score is `1 - sum((reference-model)^2)/sum(reference^2)` over the
aligned comparison body, excluding the first/last 5,000 synthesis frames.
That excludes prior-scene audio and recording cuts. The primary score applies
no separate per-recording gain fit. Correlation and energy explained measure
waveform agreement; they do not establish perceptual identity or uniquely
identify the reference creator's internal implementation.

The reconstructed burst profile has since been implemented in the C++
`SoundEngine` and selected by the application. The reference JSON remains the
frozen source of truth; the C++ app path does not read Python modules or user
recordings at runtime.

## C++ reference rerenders and validation

The current C++ preview exporter builds the Selection, Insertion, and Gnome
100-item reference schedules from the known-array fixture, then renders them
through the same reconstructed renderer used by the application. It also
generates the 22-group cross-algorithm/array-size/timing listening matrix.

From the repository root:

```powershell
& '.\\x64\\Release\\SoundEnginePreview.exe' --reconstructed-pack '.\\x64\\Release\\AudioPreviewReconstructedPackFinal'
& 'C:\\Users\\kasin\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe' 'SoundEnginePreview\\research\\compare_cpp_reference.py' 'x64\\Release\\AudioPreviewReconstructedPackFinal'
```

The generated `README.md` is the listening index. `manifest.json` records
parameters, seeds, sample rate, excerpt frame, event cursor, DC, AC RMS, peak,
and listening gain. Per-scenario event fixtures make the synthesis inputs
auditable. Each scenario has both raw float32 and PCM16 listening files.

The parity script verifies event operands before comparing waveforms. Its only
alignment is 128 C++ scheduling-lead frames minus 32 Python preroll frames =
96 frames. It does not fit gain or filtering. The maximum absolute float32
sample error is below `4.5e-7` for each of Selection, Insertion, and Gnome,
comfortably inside the `2e-6` criterion.

The output directory is under ignored `x64/Release` build output. Generated
WAVs, manifests, and plots are reproducible artifacts, not source inputs.
