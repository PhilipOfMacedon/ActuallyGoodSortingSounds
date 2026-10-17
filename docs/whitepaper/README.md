# Andrut reconstruction white paper

The 20-page paper describes the effective delayed sine-burst reconstruction,
its equations, the existing recording-agreement measurements and ablations,
the C++/Python parity report, and the recorded application validation.

`build_whitepaper.py` typesets existing evidence. It does not run application
tests, read reference audio, generate new audio, or refit the model. New plots
are explanatory equation plots or charts of saved numerical results. The
empirical spectrogram and waveform figures are copied from the existing study.

## Outputs

- `output/pdf/andrut_sorting_sonification_white_paper.pdf`
- `output/pdf/andrut_sorting_sonification_white_paper.md`
- `output/pdf/andrut_whitepaper_evidence.json`
- `output/pdf/andrut_whitepaper_figures/`

The study targets implementation commit
`494dacc7c8dc16351abc4a26d297d36faac36bc3`. Its checkpoint remains fixed even
when the script is run from a later working tree.

## Build

On Windows, use Python with `numpy`, `matplotlib`, `reportlab`, and `Pillow`.
The builder uses the installed Georgia, Calibri, and Consolas fonts. In the
project workspace, the existing offline analysis packages are preferred.

From the project root (or the extracted source supplement's root):

```powershell
python docs/whitepaper/build_whitepaper.py
```

Required inputs are the frozen profile and array under
`SoundEnginePreview/research`, the existing reconstruction manifest and two
empirical figures under `x64/Release/AudioPreviewAndrutReconstruction`, and the
final manifest and parity report under
`x64/Release/AudioPreviewReconstructedPackFinal`. The source supplement includes
those inputs without the supplied recordings or generated listening WAVs.

The Markdown file is the editable text. The PDF has section bookmarks,
numbered equations, figure captions, references, and the complete FIR/rank
appendix. Raster equations are rendered at 330 dpi; the Markdown retains their
LaTeX expressions.

## Evidence boundaries

Recording agreement, scalar-matched ablations, and implementation parity are
different measurements and are labelled separately. Build-suite assertion
counts are from the existing project completion report, not an execution
performed while authoring the paper. No claim of access to Andrut's original
implementation is made.
