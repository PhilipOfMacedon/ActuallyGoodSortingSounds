# SoundEngine

`SoundEngine` is a C++14 static library with no SDL, OpenGL, or `SortingLogic` dependency. It contains the reconstructed application synthesizer, transport-independent audio timeline contracts, and older synthesis components retained for offline experiments and regression coverage.

## Application sound profile

The application currently selects `andrut_delayed_sine_bursts_v1`, a deterministic model of overlapping, finite sine bursts. This is an evidence-based reconstruction of the supplied recordings; it is not a claim about the reference creator's private implementation. The unchanged profile fixture is [`research/andrut_delayed_sine_bursts_v1.json`](../SoundEnginePreview/research/andrut_delayed_sine_bursts_v1.json).

The reference profile uses a continuous linear pitch map from 104.0322699094767 to 4,054.6605862015895 Hz, a 300 Hz excitation clock, 9.302650315454065 carrier cycles per note, phase reset at note birth, a Tukey window with taper fraction 0.025666772580559438, and two equal-gain operands. The second operand begins 225.668919 microseconds after the first. Notes keep their birth frequency and overlap by direct summation. A frozen calibrated 31-tap FIR shapes the mixed output. There is no independent bed, chorus, pitch slew, or extra comb filter in this application profile.

Normalized pitches are assigned from the prepared input's fixed minimum and maximum, so sorting, direction changes, and array permutations do not retune an item. Equal-value inputs map to the middle of the pitch range. Algorithm metadata declares operand ordering; Gnome's audio order is reversed to match the validated reference alignment. Other algorithms keep their recorded comparison order.

The application renders the reference model at 44.1 kHz internally. The app-side output adapter resamples to the device rate, then applies a final safety clamp and increments an overload counter if required. Raw synthesis and preview exports stay linear and unclamped. The fixed 128-frame scheduling lead is separate from simulation elapsed time. The FIR and streaming resampler delays are reported independently in diagnostics.

## Timeline, playback, and seek contract

`AudioTimeline.h` defines generic audio events without importing sorting types. The application bridge translates trace records into normalized operands, semantic roles, source cursors, and timing. It prepares immutable timelines off the callback thread.

- **Fixed duration** maps source events linearly to the selected duration. The 300 Hz excitation clock chooses the latest eligible event at each tick; fast events may be skipped as separate audible presentations and slow events can persist across ticks.
- **Reference cadence** schedules eligible comparisons or fallback probes consecutively at 300 per second. Simulation events are still preserved in source order, including multiple events at the same presentation time.
- The audio sample clock drives the visible playhead only when a device and prepared timeline are ready. A monotonic host clock keeps simulation usable without audio or during preparation failures.
- Playhead feedback is fenced by both timeline generation and transport revision, so a delayed pause/completion report cannot overwrite a newer Play or seek command on the same trace.
- Pause, step, and seek either retain the inspected selection or release new excitation according to the selected idle policy. Completion, reset, trace replacement, and clearing stop births while existing notes drain naturally.
- Seeking reconstructs bounded note and filter history off-thread with the current settings. Revisioned results are coalesced; stale work is discarded. The callback installs prepared state through preallocated buffers and never allocates, locks, looks up registry entries, or deletes timeline storage.

Each operand has 256 preallocated note slots. A slot is retained until the complete note window and its output have reached exact zero; notes are not truncated to make room. The callback consumes latest state rather than a delayed command queue. If preparation or device startup fails, the simulation remains available and reports audio status.

The app persists reconstructed audio settings in version 6 of `%LOCALAPPDATA%\\ActuallyGoodSortingSounds\\settings.ini`. Existing version-5 and older settings migrate through the established chain. The reconstructed profile is the default; Basic and Advanced audio controls expose supported edits, and Restore Reconstructed Defaults restores the profile and algorithm gain defaults without changing appearance or run setup.

## Reference validation

`SoundEnginePreview.exe --reconstructed-pack <directory>` exports three complete 100-item reference rerenders plus the 22-group listening matrix. Long runs include labelled two-second beginning, middle, and ending excerpts. The output includes raw float32 WAVs, PCM16 listening copies, event fixtures, a machine-readable manifest, and a listening index.

The C++ renderer has been checked against the frozen Python reconstruction for Selection, Insertion, and Gnome. The comparison verifies ordered operands separately, then compares PCM after only the documented 96-frame alignment (128-frame C++ lead minus 32-frame Python preroll). No extra gain or filter is fitted. Maximum absolute sample errors were 3.45e-7, 3.27e-7, and 4.43e-7 respectively, each below the 2e-6 limit.

Reproduce the checks from the repository root:

```powershell
& '.\\x64\\Release\\SoundEnginePreview.exe' --reconstructed-pack '.\\x64\\Release\\AudioPreviewReconstructedPackFinal'
& 'C:\\Users\\kasin\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe' 'SoundEnginePreview\\research\\compare_cpp_reference.py' 'x64\\Release\\AudioPreviewReconstructedPackFinal'
```

The most recent build and all three console test suites passed for x64 and Win32, in Debug and Release. The resampler tests cover 22.05, 32, 44.1, 48, and 96 kHz. Run the test executables from their configuration output directory after building the solution.

## Historical offline synthesis work

The frequency-bank harmonic bed, comb-shaped bed, chorus variants, and composable sine/packet/sawtooth source models are retained as research and regression material. They are not selected by the application's audio path. Earlier descriptions and WAVs under ignored build output document those experiments; the current app profile is the reconstructed delayed-burst model above. Selecting a different synthesis model for the app requires a separate listening and evidence review.

`ComposableRenderer` and its registered offline components are described in [`COMPOSITION.md`](COMPOSITION.md). They remain useful for future experiments, but their profiles are independent of the application's persisted reconstructed settings.
