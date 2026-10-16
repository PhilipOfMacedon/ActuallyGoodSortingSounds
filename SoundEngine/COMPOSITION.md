# Composable SoundEngine

## Integration boundary

The application continues to call SoundEngine::Synthesizer, which renders the approved complex-phase harmonic profile. Its sample summation order is a sound reference, so the original implementation is retained as the legacy profile. SoundEngineTests builds a frozen copy in the test executable and compares interleaved output bit for bit across parameter changes, roles, accents, enable/disable, reset, and sample rates.

The experimental path is SoundEngine::ComposableRenderer. It is SDL-free and is used by the offline preview tool. The app remains on the legacy path until listening review chooses a new model. Both paths live in the same static library; there is no external plugin loader and no extra dependency.

## Pipeline layout

~~~text
comparison voice A: source -> filter 1 -> ... -> filter 4 -> envelope and gain --\
comparison voice B: source -> filter 1 -> ... -> filter 4 -> envelope and gain ----> constant-power mix
optional bed:       source -> filter 1 -> ... -> filter 4 -> bed gain ----------------/
                                  -> ordered stereo output processors -> master
~~~

PipelineDescription selects component IDs and configures up to four mono filters on each lane and four shared stereo processors. Both comparison lanes can use the same source and filter chain. VoiceControl retargets frequency and gain and starts or releases a voice. Event changes take effect at the first sample of the next render call.

ComponentRegistry provides stable C++ component IDs, descriptions, and factories. Registration, factory lookup, topology construction, packet-window preparation, and allocations happen before rendering. Register components before preparing a pipeline. A prepared renderer processes any caller block length in reusable chunks of at most 256 frames; it does not allocate, lock, look up registry entries, access files, or rebuild topology while rendering. Control and rendering calls belong to one owner thread. Replacing component topology requires another prepare call.

Pitch, voice gain, attack/release, master gain, and source peak gain are smoothed in sample time. Band-pass center updates use a persistent 32-sample cadence and coefficient interpolation. Oscillator phase, packet launch phase, filter state, and chorus delay state persist across block boundaries. Reset clears component state and returns the sample clock to zero.

## Registered components

| Type | ID | Behavior |
|---|---|---|
| Source | continuous_sine | Phase-continuous oscillator with freely retargeted frequency. |
| Source | clocked_sine_packets | 300 Hz packet clock by default; Hann or endpoint-zero Gaussian windows; reset or continuous-reference carrier phase; peak or window-energy normalization. Eight fixed grain slots per lane cover durations up to four clock periods. |
| Source | bandlimited_saw_300 | 300 Hz Fourier sawtooth with 1/k partial amplitudes through 17.1 kHz, limited by Nyquist at lower sample rates. |
| Source | legacy_harmonic_bed | Independent coherent bed using the selected 300 Hz harmonic exponent and comb response. Sub-300 Hz components remain optional. |
| Filter | bypass | Identity. |
| Filter | tpt_bandpass | Peak-normalized topology-preserving state-variable band-pass; fixed bandwidth or constant Q, with Q bounded to 0.1–64. |
| Filter | butterworth_lowpass | Two-pole Butterworth low-pass. |
| Output | bypass | Identity stereo processor. |
| Output | stereo_chorus | Preallocated modulated fractional-delay stereo chorus. |

The experimental defaults are 44.1 kHz, master gain 0.50, source peak gain 0.25, 8 ms attack/release, 2 ms pitch slew, and continuous mapping from 50 to 4,200 Hz. The offline profile matrix overrides only the model parameters named by each profile. Experimental profiles start dry without event accents or a bed; separate A/B clips enable the legacy harmonic bed.

For carrier-cycle packet profiles, requested duration is limited to 0.5–4 periods of the packet clock so eight slots remain sufficient. At a low carrier frequency the upper duration bound can therefore shorten the requested carrier cycle count. Window-energy normalization normalizes the squared-window integral against the two-period baseline; it does not promise equal final energy after carrier phase, filtering, overlap, or mixing.

## Offline exploration pack

Build SoundEnginePreview in x64 Release and run:

~~~powershell
.\x64\Release\SoundEnginePreview.exe --source-models .\x64\Release\AudioPreviewSourceModels
~~~

The complete pack contains 70 model profiles. Each has an 80-value single-voice scan and the 79 adjacent comparisons from ordered Insertion Sort. Baseline references add the legacy profile and all three new models at eight and sixteen changes per second, equal-role and held-key legacy sweeps, bed and held-key A/Bs, six-frequency probes, and release probes. The generator enforces a total of 162 signals and writes two WAV files per signal:

- *_raw_f32.wav: unmodified IEEE float32 stereo renderer output.
- *_listen_pcm16.wav: PCM16 copy with per-scenario-group AC-RMS matching toward -24 dBFS. The group target is lowered as a whole as needed to keep every peak below -1 dBFS.

README.md lists baseline clips first. manifest.json records each profile, all render controls, event frames and held-key roles, seed, sample rate, raw DC/AC RMS/peak, applied listening gain, and file names. Media and analysis outputs belong under ignored build output.

An individual profile/scenario can be rerendered with:

~~~powershell
.\x64\Release\SoundEnginePreview.exe --source-models .\x64\Release\OnePreview --profile packet_fixed_gaussian_2period_reset_peak --scenario insertion_adjacent
~~~

Run the manifest, WAV-format, event-schedule, level-match, peak-limit, and baseline-plot audit with the workspace Python runtime:

~~~powershell
python .\SoundEnginePreview\analyze_source_models.py .\x64\Release\AudioPreviewSourceModels
~~~

The script writes validation_report.txt, baseline measurement JSON/CSV, waveform and spectrogram plots, and checks the 162-signal/324-WAV matrix. The app's live controls and persistence remain on the legacy synthesizer during this listening gate.
