# Ordered Sweep 2: source-model reassessment

Analysis date: 2026-10-14 (America/Sao_Paulo).

## Assets and scope

Reference: `C:\Users\kasin\Music\MP3 Soundtracks\Andrut_Ordered_Sweep_2.wav`.
The user identifies the first sweep as the final Merge Sort merge, and the second as the final sequential scan. The supplied audio alone does not identify the compared item values or the exact event schedule.

The file is mono, 44,100 Hz, float32, 3.230385 s. The transition is approximately 2.0 s. This analysis does not change the SoundEngine or application.

Reproducible analysis script, numerical results, and plots are under ignored build output:

- `x64/Release/AudioAnalysis/analyze_sweep.py`
- `x64/Release/AudioAnalysis/initial_measurements.json`
- `x64/Release/AudioAnalysis/spectral_comparison.png`
- `x64/Release/AudioAnalysis/reference_full_spectrum.png`
- `x64/Release/AudioAnalysis/periodic_waveform_comparison.png`

Python analysis packages are isolated in that output directory; they are not application dependencies.

## Measurements

- Persistent spectral ridges lie near 300 Hz and its multiples. An 8,192-sample Hann analysis with zero padding places representative median-spectrum maxima near 300.1, 599.6, 899.7, 1199.8, 1499.9, and 1800.0 Hz. Zero padding refines peak sampling but does not improve the underlying window resolution. The high-frequency view retains the family into the upper audible range.
- The first sweep contains a strong moving inter-harmonic region. A provisional ridge tracker excluding harmonic-line cores gives approximately 172 Hz at 0.10 s, 711 Hz at 0.30 s, 1141 Hz at 0.50 s, 1550 Hz at 0.70 s, and 2153 Hz at 1.00 s. These are approximate moving-energy estimates, not recovered oscillator frequencies or a verified value-to-frequency formula.
- Both sweeps have strong repetition at 147 samples, equivalent to 3.333 ms / 300 Hz. Folding 26.667 ms windows into eight 147-sample periods and repeating their mean captures median local energy fractions of 0.814 in the first analysis interval (0.10–1.90 s) and 0.865 in the second (2.10–3.19 s). This is a projection onto periodic content including DC, not a separation of individual voices or bed energy. The moderate aggregate difference does not identify different synthesizers.
- Example folded waveforms contain sharp recurring features and sinusoidal portions. They are not identifiable as a raw sawtooth from their shape alone.
- Local RMS (26.667 ms windows) is not constant. The first interval's 5th/50th/95th percentiles are 0.0156 / 0.0451 / 0.1332. The second interval's are 0.0125 / 0.0418 / 0.1862. The visible widening therefore does not establish constant total signal energy; interference, bed contributions, processing, and source envelopes remain confounders.
- The reference and current chosen preview were plotted with the same FFT settings and fixed dBFS colour limits. Their durations and event schedules differ, so their RMS difference is not a calibrated gain recommendation.

## Interpretations and limits

1. A 300 Hz harmonic family establishes periodic structure; it does not establish a sawtooth oscillator. A repeating impulse, windowed sine packet, or any other periodic waveform can produce the same line spacing.
2. A fixed linear filter on a fixed 300 Hz sawtooth changes the amplitudes and phases of the existing harmonics. It cannot create a stationary freely selected 200 Hz or 1100 Hz tone. A changing filter can produce transients/sidebands; amplitude or phase modulation and additional voices can also generate inter-harmonic energy.
3. The current comparison model confines steady synthesis to predefined oscillator partials. A continuously moving focus need not be constrained to those frequencies. The low-band observations do not uniquely establish fixed 50/100/150 Hz components.
4. Gaussian amplitude allocation is plausible but unproven. A Gaussian time envelope produces a Gaussian spectral envelope. Shorter packets give wider spectral lobes; choosing duration inversely proportional to focus frequency produces approximately constant-Q width. Rectangular or sharply truncated packets instead produce sinc-like spectral lobes and longer tails. The spectrogram analysis window also broadens the displayed spectrum.
5. One voice during a scan and two during a comparison is plausible event semantics, but voice count and role-specific synthesis cannot be recovered from this magnitude image. Two identical voice generators with different values, phase histories, and event envelopes can explain different interference. Two sorted merge inputs do not imply the same pair sequence as a sequential scan.
6. Static harmonic notches and the earlier fitted comb response remain useful effective descriptions, not proof of a specific comb circuit. A focused contribution can fill notches in another contribution without changing the filter.

## Candidate mathematical model

Use a value-controlled short tone packet:

`g_v(t) = A(v) w_v(t) sin(2*pi*f(v)*t + phase)`.

Repeated at `T = 1/300`, a fixed packet gives

`s_v(t) = sum_m g_v(t - m*T)`

with Fourier coefficients `C_k(v) = G_v(k/T)/T`. The 300 Hz family is a consequence of repetition; the underlying packet carrier `f(v)` is continuous. With a sine packet,

`G_v(f) = [exp(j*phase)*W_v(f-f(v)) - exp(-j*phase)*W_v(f+f(v))] / (2*j)`.

This places a spectral envelope near the selected value while sampling its periodic component at the 300 Hz family. Changing packet values or timing breaks exact periodicity and produces additional spectral structure. A strictly periodic packet with frozen controls still has only integer-300-Hz lines and DC; a continuous voice or other nonperiodic mechanism may be needed if isolated held-value tests establish steady sub-300 or off-grid tones.

For Gaussian amplitude allocation `A(f) = G*exp(-(f-c)^2/(2*sigma^2))`, integrated spectral energy is proportional to `G^2*sigma`. An explicitly energy-normalized design therefore uses `G proportional to sigma^(-1/2)`. This is a controllable design choice, not an inferred property of the recording.

## Recommended next experiment

Build offline comparisons of: (a) a continuously pitched sine with finite envelopes, (b) 300 Hz clocked sine packets with adjustable duration/window and phase policy, and (c) a band-limited 300 Hz sawtooth through a moving resonant filter. Test one voice and two identical voices separately, using the same values and timing, before proposing asymmetric role-specific synthesis. Include a held off-grid pitch, a sweep below 300 Hz, and changes in packet clock to distinguish source pitch from repetition cadence. Keep the working app profile as a reference until listening and measurements select a replacement.

## Primary theory references

- Julius O. Smith, sinusoidal filter response: https://dsprelated.com/freebooks/filters/Finding_Frequency_Response.html
- Julius O. Smith, Gaussian window and transform: https://www.dsprelated.com/freebooks/sasp/Gaussian_Window_Transform.html
- Julius O. Smith, spectrum-analysis windows and sinc behaviour: https://www.dsprelated.com/freebooks/sasp/Spectrum_Analysis_Windows.html
- Julius O. Smith, modulation and sidebands: https://www.dsprelated.com/freebooks/mdft/Sinusoids_Exponentials.html
