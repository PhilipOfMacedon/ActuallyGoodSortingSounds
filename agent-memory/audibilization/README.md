# Audibilization memory

Working memory for recreating and improving the sound of sorting-algorithm demonstrations. The current target is an independently reproducible system, not a claim that the reference creator used any particular internal technique.

## Notes

- [Known-array reconstruction](known-array-reconstruction.md): the current three-recording sine-burst fit, first-pair uncertainty, mathematical explanation, and listening pack.
- [Research log](research-log.md): reference assets, spectrogram observations, measurements, and confidence boundaries.
- [Engine design](engine-design.md): deterministic event traces, key-turnpoint markers, sampled presentation, audio rendering, seeking, and state reconstruction.
- [Open questions](open-questions.md): unknowns and suggested investigations before implementation planning.

## Current architecture and status

`SortingLogic` owns the sorting algorithms, deterministic event trace, and simulation state. The app owns SDL/OpenGL, retained controls, settings persistence, and a thin action coordinator. `SoundEngine` is a separate SDL- and sorting-independent static library. The app-side bridge translates traces into generic immutable audio timelines; neither library imports the other's domain types.

The application uses the reconstructed delayed sine-burst profile in `SoundEnginePreview/research/andrut_delayed_sine_bursts_v1.json`. It captures pitch at each note's birth, schedules two equal-gain operands with the calibrated relative delay, overlaps notes without normalization, then applies the fixed reference FIR. The app renders at 44.1 kHz internally and resamples to the device. There is no separate idle hum in this profile. Older harmonic-bank, comb, chorus, and modular source-model work remains offline research and regression support.

Audio timelines support fixed-duration event mapping and 300-comparison-per-second reference cadence. Sample time drives presentation when audio and a prepared timeline are available; otherwise the app uses a host clock. Prepared timelines and bounded seek snapshots are built off the callback thread with generation/revision checks. The callback renders preallocated state, consumes latest-state updates, and never allocates or blocks. Pause/step/seek obey Hold or Release selection policy; reset/completion/replacement stop new births and let active notes drain.

Settings version 6 persists appearance, algorithm choices, and reconstructed audio controls. Restore Reconstructed Defaults resets audio/timbre and inherited algorithm gains while preserving visual and run setup preferences. Algorithm audio metadata handles operand order and comparison/probe selection for the current roster; remaining work before adding many more algorithms is to define audio-role semantics and validate each new algorithm's event stream against its visual behavior.

Current evidence and delivery status are documented in [the research log](research-log.md), [the known-array reconstruction](known-array-reconstruction.md), and [`SoundEngine/README.md`](../../SoundEngine/README.md). The reconstruction parity comparison is below the 2e-6 PCM tolerance for Selection, Insertion, and Gnome. x64 and Win32 Debug/Release builds and the SoundEngine, AppFramework, and SortingLogic console suites have passed for this coding wave. Remaining checks are listed in the research log; notably, interactive device recovery and cross-machine execution depend on a recipient machine and cannot be proven by the source workstation alone.
