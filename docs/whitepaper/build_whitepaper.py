"""Typeset the white paper from existing research artifacts; runs no tests or fits.

Uses ReportLab from the Codex runtime and Matplotlib from the project's existing
offline analysis environment. New figures are explanatory equation plots or
visualizations of saved measurements. Reference audio is never read here.
"""
from pathlib import Path
from io import BytesIO
import hashlib
import json
import math
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "x64/Release/AudioAnalysis/packages"))
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import mathtext
from matplotlib.font_manager import FontProperties
from PIL import Image as PILImage
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle,
    PageBreak, KeepTogether, Preformatted,
)

OUT = ROOT / "output/pdf"
FIG = OUT / "andrut_whitepaper_figures"
OUT.mkdir(parents=True, exist_ok=True)
FIG.mkdir(parents=True, exist_ok=True)
RESEARCH = ROOT / "SoundEnginePreview/research"
PACK = ROOT / "x64/Release/AudioPreviewAndrutReconstruction"
FINAL = ROOT / "x64/Release/AudioPreviewReconstructedPackFinal"
profile = json.loads((RESEARCH / "andrut_delayed_sine_bursts_v1.json").read_text())
array = json.loads((RESEARCH / "andrut_array.json").read_text())
manifest = json.loads((PACK / "manifest.json").read_text())
parity = json.loads((FINAL / "reference_parity.json").read_text())
pack_final = json.loads((FINAL / "manifest.json").read_text())
cfg = profile["configuration"]
entries = manifest["signals"]
names = [e["algorithm"] for e in entries]
fs = profile["sample_rate"]
clock = profile["comparison_clock_hz"]
delay_frames = -cfg["delays"][0]
delta = delay_frames / fs
fmin = cfg["base"]
fmax = fmin + 99 * cfg["step"]
cycles = cfg["duration"]
alpha = cfg["tukey"]
taps = np.asarray(profile["output_filter"]["coefficients_causal"])
# Freeze the study's implementation target, independent of a future checkout.
commit = "494dacc7c8dc16351abc4a26d297d36faac36bc3"

TEAL = "#087e8b"
NAVY = "#142e46"
GOLD = "#c08a2b"
INK = "#263746"
GRAY = "#647583"
PALE = "#edf5f7"
COLORS = [TEAL, "#c2832e", "#6d64a5"]
plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 10,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.labelcolor": INK, "text.color": INK, "axes.edgecolor": "#a2b1ba",
    "xtick.color": GRAY, "ytick.color": GRAY, "grid.color": "#dfe7eb",
    "axes.titleweight": "bold", "figure.facecolor": "white",
    "savefig.facecolor": "white", "legend.frameon": False,
})


def savefig(fig, name):
    fig.savefig(FIG / name, dpi=230, bbox_inches="tight", pad_inches=.12)
    plt.close(fig)


def make_figures():
    # Existing empirical figures are reused without reanalysis of recordings.
    for name in ("spectrogram_comparison.png", "waveform_comparison.png"):
        shutil.copyfile(PACK / name, FIG / name)

    fig, ax = plt.subplots(figsize=(9.2, 2.45), constrained_layout=True)
    x = np.arange(1, 101)
    ax.bar(x, array["ranks"], width=.87, color=TEAL)
    ax.bar(x[:2], array["ranks"][:2], width=.87, color=GOLD)
    ax.set(xlabel="Initial array position (1-based)", ylabel="Height rank (0-99)", xlim=(0, 101), ylim=(0, 103))
    ax.text(.02, .94, "First two positions retain temporal uncertainty", transform=ax.transAxes, va="top", fontsize=9, color=GRAY)
    savefig(fig, "01_known_array.png")

    fig, axes = plt.subplots(1, 3, figsize=(9.2, 2.4), constrained_layout=True)
    for ax, freq, color in zip(axes, (fmin, 1150., fmax), COLORS):
        length = max(2, round(cycles * fs / freq))
        u = np.arange(length) / (length - 1)
        win = np.ones(length)
        left = u < alpha / 2
        right = u > 1 - alpha / 2
        win[left] = .5 * (1 - np.cos(2 * np.pi * u[left] / alpha))
        win[right] = .5 * (1 - np.cos(2 * np.pi * (1 - u[right]) / alpha))
        t = np.arange(length) / fs
        ax.plot(t * 1000, win * np.sin(2 * np.pi * freq * t), color=color, lw=.9)
        ax.plot(t * 1000, win, color=GRAY, ls="--", lw=.7)
        ax.set(title=f"{freq:,.1f} Hz", xlabel="Age (ms)", ylim=(-1.08, 1.08))
        if ax is axes[0]: ax.set_ylabel("Unit source amplitude")
    savefig(fig, "02_finite_notes.png")

    fig, ax = plt.subplots(figsize=(9.2, 2.8), constrained_layout=True)
    freq = np.linspace(0, 2800, 7000)
    carrier = 1150.
    duration = cycles / carrier
    response = np.abs(np.sinc(duration * (freq - carrier)))
    ax.plot(freq, response, color=TEAL, lw=1.4, label="Continuous packet-envelope approximation")
    line_f = np.arange(0, 2801, 300)
    line_a = np.abs(np.sinc(duration * (line_f - carrier)))
    ax.vlines(line_f, 0, line_a, color=GOLD, lw=2.5, label="300 Hz periodic-train line samples")
    ax.scatter(line_f, line_a, color=GOLD, s=16, zorder=4)
    ax.axvline(carrier, color=GRAY, lw=.8, ls="--")
    ax.set(xlabel="Analysis frequency (Hz)", ylabel="Relative magnitude", xlim=(0, 2800), ylim=(0, 1.07))
    ax.legend(loc="upper right", fontsize=8.5)
    savefig(fig, "03_clock_family.png")

    freq = np.linspace(fmin, fmax, 600)
    fig, axes = plt.subplots(1, 2, figsize=(9.2, 2.65), constrained_layout=True)
    axes[0].plot(freq, 1000 * cycles / freq, color=TEAL, lw=2)
    axes[0].set(xlabel="Carrier frequency (Hz)", ylabel="Note duration (ms)")
    axes[1].plot(freq, freq / cycles, color=GOLD, lw=2)
    axes[1].set(xlabel="Carrier frequency (Hz)", ylabel="Characteristic width 1/T (Hz)")
    for ax in axes: ax.grid(alpha=.6)
    savefig(fig, "04_duration_bandwidth.png")

    freq = np.linspace(0, 18000, 9000)
    ideal = np.abs(np.cos(np.pi * freq * delta))
    # Exact fractional-shift kernel used in the frozen Python reference.
    start = math.floor(-delay_frames)
    frac = -delay_frames - start
    omega = 2 * np.pi * freq / fs
    early = np.exp(-1j * omega * start) * ((1 - frac) + frac * np.exp(-1j * omega))
    discrete = np.abs(1 + early) / 2
    fig, axes = plt.subplots(2, 1, figsize=(9.2, 4.5), constrained_layout=True)
    axes[0].plot(freq, 20 * np.log10(np.maximum(ideal, 1e-5)), color=GRAY, ls="--", lw=1, label="Ideal relative delay")
    axes[0].plot(freq, 20 * np.log10(np.maximum(discrete, 1e-5)), color=TEAL, lw=1.2, label="Frozen linear fractional-delay model")
    axes[0].set(xlabel="Frequency (Hz)", ylabel="Pair gain / DC (dB)", ylim=(-52, 2), xlim=(0, 18000))
    for k in range(4):
        notch = (2 * k + 1) / (2 * delta)
        axes[0].axvline(notch, color=GOLD, lw=.7, alpha=.7)
        axes[0].text(notch + 80, -47, f"{notch:,.0f}", rotation=90, fontsize=8)
    axes[0].legend(fontsize=8, loc="upper right")
    fir = np.exp(-1j * omega[:, None] * np.arange(len(taps))) @ taps
    axes[1].plot(freq, 20 * np.log10(np.maximum(np.abs(fir), 1e-8)), color=GOLD, lw=1.3)
    axes[1].set(xlabel="Frequency (Hz)", ylabel="FIR magnitude (dB)", xlim=(0, 18000))
    axes[1].text(.02, .08, "Absolute transfer gain; calibration is included", transform=axes[1].transAxes, fontsize=8, color=GRAY)
    savefig(fig, "05_delay_and_fir.png")

    fig, ax = plt.subplots(figsize=(9.2, 2.5), constrained_layout=True)
    scores = [100 * e["metrics"]["fixed_gain_energy_explained"] for e in entries]
    bars = ax.bar(names, scores, color=COLORS, width=.5)
    ax.set(ylabel="Waveform energy explained (%)", ylim=(0, 105))
    for bar, v in zip(bars, scores): ax.text(bar.get_x() + bar.get_width()/2, v + 1, f"{v:.3f}%", ha="center", fontsize=10)
    ax.grid(axis="y", alpha=.6); ax.set_axisbelow(True)
    savefig(fig, "06_validation_scores.png")

    fig, ax = plt.subplots(figsize=(9.2, 3.15), constrained_layout=True)
    kinds = ["full", "same_onset", "four_period_duration_cap", "gaussian_four_period_cap"]
    labels = ["Frozen model", "No operand delay", "4-period duration cap", "Gaussian + 4-period cap"]
    xp = np.arange(4)
    for i, e in enumerate(entries):
        vals = [100 * (e["metrics"]["gain_fitted_energy_explained"] if k == "full" else e["ablations"][k]["gain_fitted_energy_explained"]) for k in kinds]
        bars = ax.bar(xp + (i - 1) * .23, vals, width=.22, color=COLORS[i], label=e["algorithm"])
        for bar, value in zip(bars, vals): ax.text(bar.get_x()+bar.get_width()/2, value+1, f"{value:.1f}", ha="center", fontsize=7.5)
    ax.set(xticks=xp, xticklabels=labels, ylabel="Energy explained after scalar matching (%)", ylim=(0, 112))
    ax.tick_params(axis="x", labelsize=8)
    ax.legend(ncol=3, loc="upper right", fontsize=8); ax.grid(axis="y", alpha=.5); ax.set_axisbelow(True)
    savefig(fig, "07_saved_ablations.png")

    fig, ax = plt.subplots(figsize=(9.2, 2.5), constrained_layout=True)
    errs = [parity["algorithms"][n]["maximum_absolute_sample_error"] for n in names]
    ax.bar(names, errs, width=.5, color=COLORS)
    ax.axhline(2e-6, color=GOLD, ls="--", label="Acceptance limit: 2e-6")
    ax.set(yscale="log", ylabel="Maximum absolute PCM sample error", ylim=(1e-7, 4e-6))
    ax.legend(fontsize=8, loc="upper right")
    for i, e in enumerate(errs): ax.text(i, e*1.12, f"{e:.3g}", ha="center", fontsize=9)
    savefig(fig, "08_cpp_parity.png")


def register_fonts():
    fonts = Path("C:/Windows/Fonts")
    for name, file in [("Body", "georgia.ttf"), ("Body-Bold", "georgiab.ttf"), ("Body-Italic", "georgiai.ttf"),
                       ("Sans", "calibri.ttf"), ("Sans-Bold", "calibrib.ttf"), ("Sans-Italic", "calibrii.ttf"),
                       ("Mono", "consola.ttf")]:
        pdfmetrics.registerFont(TTFont(name, str(fonts/file)))
    pdfmetrics.registerFontFamily("Body", normal="Body", bold="Body-Bold", italic="Body-Italic", boldItalic="Body-Bold")
    pdfmetrics.registerFontFamily("Sans", normal="Sans", bold="Sans-Bold", italic="Sans-Italic", boldItalic="Sans-Bold")


register_fonts()
styles = {
    "body": ParagraphStyle("body", fontName="Body", fontSize=10, leading=15, spaceAfter=9, textColor=colors.HexColor(INK)),
    "small": ParagraphStyle("small", fontName="Sans", fontSize=8.4, leading=11.4, spaceAfter=7, textColor=colors.HexColor(GRAY)),
    "heading": ParagraphStyle("heading", fontName="Sans-Bold", fontSize=22, leading=26, spaceAfter=15, textColor=colors.HexColor(NAVY)),
    "sub": ParagraphStyle("sub", fontName="Sans-Bold", fontSize=11.8, leading=15, spaceBefore=8, spaceAfter=7, textColor=colors.HexColor(TEAL)),
    "kicker": ParagraphStyle("kicker", fontName="Sans-Bold", fontSize=8.2, leading=10, spaceAfter=8, textColor=colors.HexColor(TEAL), tracking=1),
    "caption": ParagraphStyle("caption", fontName="Sans", fontSize=8.25, leading=11, spaceAfter=12, textColor=colors.HexColor(GRAY)),
    "table": ParagraphStyle("table", fontName="Sans", fontSize=8.6, leading=11.4, textColor=colors.HexColor(INK)),
    "thead": ParagraphStyle("thead", fontName="Sans-Bold", fontSize=8.5, leading=11, textColor=colors.white),
    "code": ParagraphStyle("code", fontName="Mono", fontSize=7.5, leading=10, textColor=colors.HexColor(INK)),
}
W, H = A4
WIDTH = W - 100
story = []
md = []
equation_counter = 0
figure_counter = 0


def p(text, kind="body"):
    story.append(Paragraph(text, styles[kind]))
    md.append(text + "\n")


def section(title, kicker, new=True):
    if new: story.append(PageBreak())
    story.append(Paragraph(kicker.upper(), styles["kicker"]))
    story.append(Paragraph(title, styles["heading"]))
    md.append("\n# " + title + "\n")


def sub(title):
    story.append(Paragraph(title, styles["sub"]))
    md.append("\n## " + title + "\n")


def table(headers, rows, widths=None, fontsize=None):
    data = [[Paragraph(str(x), styles["thead"]) for x in headers]]
    data += [[Paragraph(str(x), styles["table"]) for x in row] for row in rows]
    obj = Table(data, colWidths=widths, hAlign="LEFT", repeatRows=1)
    obj.setStyle(TableStyle([
        ("BACKGROUND", (0,0), (-1,0), colors.HexColor(NAVY)),
        ("VALIGN", (0,0), (-1,-1), "TOP"),
        ("LEFTPADDING", (0,0), (-1,-1), 8), ("RIGHTPADDING", (0,0), (-1,-1), 8),
        ("TOPPADDING", (0,0), (-1,-1), 6), ("BOTTOMPADDING", (0,0), (-1,-1), 6),
        ("ROWBACKGROUNDS", (0,1), (-1,-1), [colors.white, colors.HexColor(PALE)]),
        ("LINEBELOW", (0,-1), (-1,-1), .5, colors.HexColor("#c5d6de")),
    ]))
    story.append(obj); story.append(Spacer(1, 10))
    md.append("| " + " | ".join(headers) + " |")
    md.append("| " + " | ".join(["---"]*len(headers)) + " |")
    for row in rows: md.append("| " + " | ".join(str(x).replace("\n", " ") for x in row) + " |")
    md.append("")


def eq(latex):
    global equation_counter
    equation_counter += 1
    path = FIG / f"equation_{equation_counter:02}.png"
    mathtext.math_to_image("$"+latex+"$", str(path), prop=FontProperties(size=12), dpi=330, color=INK)
    iw, ih = PILImage.open(path).size
    ww, hh = iw*72/330, ih*72/330
    if ww > WIDTH-37:
        hh *= (WIDTH-37)/ww; ww = WIDTH-37
    cell = Image(str(path), width=ww, height=hh)
    t = Table([[cell, Paragraph(f"({equation_counter})", styles["small"])]], colWidths=[WIDTH-32, 32])
    t.setStyle(TableStyle([("ALIGN",(0,0),(0,0),"CENTER"),("VALIGN",(0,0),(-1,-1),"MIDDLE"),
                           ("TOPPADDING",(0,0),(-1,-1),5),("BOTTOMPADDING",(0,0),(-1,-1),9)]))
    story.append(t)
    md.append(f"\n$$\n{latex}\n$$\n\n({equation_counter})\n")


def fig(name, caption, width=WIDTH, maxheight=None):
    global figure_counter
    figure_counter += 1
    path = FIG/name
    iw, ih = PILImage.open(path).size
    height = width*ih/iw
    if maxheight and height > maxheight: width *= maxheight/height; height=maxheight
    block = [Image(str(path), width=width, height=height), Spacer(1,5),
             Paragraph(f"<b>Figure {figure_counter}.</b> {caption}", styles["caption"])]
    story.append(KeepTogether(block))
    md.append(f"\n![Figure {figure_counter}. {re.sub('<[^>]+>', '', caption)}](andrut_whitepaper_figures/{name})\n")


def callout(title, text):
    obj=Table([[Paragraph(title, styles["sub"])],[Paragraph(text, styles["body"])]],colWidths=[WIDTH])
    obj.setStyle(TableStyle([("BACKGROUND",(0,0),(-1,-1),colors.HexColor(PALE)),
        ("LINEBEFORE",(0,0),(0,-1),3,colors.HexColor(TEAL)),("LEFTPADDING",(0,0),(-1,-1),12),
        ("RIGHTPADDING",(0,0),(-1,-1),12),("TOPPADDING",(0,0),(-1,-1),4),("BOTTOMPADDING",(0,0),(-1,-1),5)]))
    story.append(obj); story.append(Spacer(1,10))
    md.append(f"\n> **{title}** {text}\n")


def code(text):
    story.append(Preformatted(text, styles["code"]))
    story.append(Spacer(1,8)); md.append("\n```text\n"+text+"\n```\n")


def content():
    # 1. Cover and executive abstract.
    story.append(Spacer(1,28))
    p("ACTUALLY GOOD SORTING SOUNDS  /  RESEARCH PAPER 01", "kicker")
    title_style=ParagraphStyle("title",fontName="Sans-Bold",fontSize=34,leading=37,textColor=colors.HexColor(NAVY),spaceAfter=16)
    story.append(Paragraph("Reconstructing Andrut's<br/>Sorting Sonification",title_style))
    md.append("# Reconstructing Andrut's Sorting Sonification\n")
    p("A delayed sine-burst model, its mathematical explanation,<br/>and validation across three sorting algorithms", "sub")
    p("17 October 2026  |  Version 1.0<br/>Evidence and implementation through 16 October 2026", "small")
    story.append(Spacer(1,14))
    sub("Abstract")
    p("Andrut's sorting demonstration combines moving pitches with a persistent 300 Hz harmonic family and conspicuous spectral cancellation regions. Those observations initially suggested a harmonic oscillator bank, chorus, or a filtered sawtooth. A known 100-item input permutation made a more discriminating reconstruction possible: reproduce the exact comparisons, then identify the sound produced by their ordered operands.")
    p("The resulting effective model launches phase-reset sine bursts on a 300 Hz clock. Each burst lasts a fixed number of carrier cycles, captures its pitch at birth, and overlaps earlier notes. The two operands have equal gain and a relative onset offset of approximately 225.67 microseconds. One common calibrated 31-tap FIR processes their sum. The model requires no independent humming bed or chorus.")
    p("Parameters and the FIR were fitted on Selection Sort. With that common source, filter, and gain, saved validation results explain 97.395% of Selection's waveform energy, 98.250% of Insertion's, and 98.099% of Gnome's. A subsequent C++ implementation agrees with the frozen Python synthesis to a maximum float32 sample error below 4.5 x 10<super>-7</super>. These are separate claims: agreement with the recordings supports the reconstruction; agreement between implementations verifies the port.")
    callout("Scope of the claim", "This paper describes an evidence-supported reconstruction of Andrut's observable audio behavior. The creator's source code is unavailable to this study. Numerical agreement does not uniquely identify the original implementation, analog circuit, filter design, or intended parameter values.")
    p("Prepared for the Actually Good Sorting Sounds project from user-supplied reference media, saved research artifacts, and existing validation records. No new audio experiments, model fitting, or test-suite executions were performed for this paper. New explanatory plots evaluate the documented equations; empirical plots and numerical scores reuse saved artifacts.", "small")
    p("<b>Reading guide.</b> Source and spectral theory: pp. 3-7. Identification and recording evidence: pp. 8-12. C++ parity and runtime: pp. 13-15. Configuration and interpretation: pp. 16-17. Exact constants and provenance: pp. 18-20. Section bookmarks are included in the PDF.", "small")

    # 2. Evidence.
    section("Evidence and the recovered input", "01 / Data and provenance")
    p("The primary dataset consists of three supplied mono float32 WAV recordings at 44,100 samples/s and a 1,218 x 720 raster image of the initial bars. The image provides 100 distinct height ranks; it does not reveal the source program's original numerical magnitudes. Ranking the recovered heights therefore preserves the order and equal spacing without pretending that pixels are the original data. [R1-R3]")
    table(["Recording", "Source frames", "File duration", "Role in identification"],[
        ["Selection", "733,500", "16.6327 s", "Source and common FIR fitting"],
        ["Insertion", "416,388", "9.4420 s", "Cross-algorithm validation"],
        ["Gnome", "819,556", "18.5840 s", "Cross-algorithm validation"],
    ],[91,86,90,WIDTH-267])
    fig("01_known_array.png", "Recovered 100-item rank permutation. Gold marks the only uncertain positions: the first two bars may already have exchanged places before the captured frame. The remaining 98 positions are user-confirmed. This is a visualization of the saved array fixture, not a new image-extraction run.")
    p("The visible order begins [78, 89, 75, 81, 31, ...]. The alternative allowed first pair is [89, 78, ...]. The array contains 2,724 inversions. The reconstruction retains this temporal uncertainty even though the raster heights themselves have no ties. Earlier 80-bar estimates in the research history are superseded for these three recordings.")
    table(["Schedule", "Comparisons", "Active duration at 300/s", "Effective early / late operand"],[
        ["Selection", "4,950", "16.500000 s", "Current minimum / scanned item"],
        ["Insertion", "2,818", "9.393333 s", "array[j] / held key"],
        ["Gnome", "5,542", "18.473333 s", "array[i] / array[i-1]"],
    ],[73,76,123,WIDTH-272])
    p("Recording durations include scene boundaries and cuts. They need not equal comparison-count duration. Exactly 147 source samples per comparison at 44.1 kHz supplies the common 300 Hz timing grid. Operand order is an effective audio-timing identification; it is not proof of the creator's comparator syntax.", "small")

    # 3. Overview.
    section("The reconstructed instrument", "02 / Effective signal model")
    callout("Signal path", "Ordered comparison operands -> normalized values -> two captured-pitch sine-burst lanes -> relative operand delay -> direct overlap sum -> common calibrated FIR -> output. The experimental application adds optional controls and device conversion after this reference path.")
    table(["Quantity", "Frozen value", "Meaning"],[
        ["Internal sample rate", "44,100 Hz", "Reference discrete-time clock"],
        ["Excitation / comparison clock", "300 Hz", "147 samples between comparison ticks"],
        ["Minimum carrier pitch", "104.0322699094767 Hz", "Rank 0 in the recovered input"],
        ["Pitch step for 100 ranks", "39.90533652820316 Hz", "Continuous linear mapping"],
        ["Maximum carrier pitch", "4,054.6605862015895 Hz", "Rank 99"],
        ["Carrier cycles per note", "9.302650315454065", "Duration C/f; no clock-period cap"],
        ["Tukey taper fraction", "0.025666772580559438", "Short cosine edges and flat interior"],
        ["Relative operand delay", "9.95199933051814 samples", "225.668919 microseconds"],
        ["Carrier phase at birth", "0 radians", "Each note restarts its own sine"],
        ["Operand gains", "Equal; 0 dB role offsets", "Natural overlap; no active-note normalization"],
        ["Common output response", "31 causal FIR coefficients", "Includes the fitted reference gain"],
        ["Independent bed / chorus", "None / off", "Periodic overlap produces the hum"],
    ],[123,163,WIDTH-286])
    p("For this input, rank r maps to f<sub>r</sub> = f<sub>0</sub> + s r. The application generalizes this to normalized input values using the prepared array's fixed extrema. Sorting and rearrangement never change an item's assigned pitch. Equal-value arrays map to the pitch-range midpoint. [R3, R4, R7]")
    eq(r"u(v)=\frac{v-v_{\min}}{v_{\max}-v_{\min}},\qquad f(v)=f_{\min}+(f_{\max}-f_{\min})u(v)")
    p("The many decimal places preserve a numerical fit and deterministic rerendering. They do not establish that Andrut deliberately chose those exact physical settings. A useful verbal recipe is approximately 100-4,060 Hz, nine-cycle sine notes, 300 comparison ticks/s, short cosine edges, and a ten-sample operand offset.")

    # 4. Discrete source.
    section("Finite notes, phase, and overlap", "03 / Source equations")
    p("Let F<sub>s</sub> be the sample rate, C the carrier-cycle count, alpha the taper fraction, and f the captured carrier frequency. The reference rounds duration to the nearest integer sample with ties to even. This detail, the endpoint convention, and fractional delay matter when comparing individual PCM samples. [R3, R5, R7]")
    eq(r"L_f=\max\left(2,\operatorname{roundEven}\left(\frac{CF_s}{f}\right)\right),\quad x_q=\frac{q}{L_f-1}")
    eq(r"h_f[q]=w_\alpha(x_q)\sin\left(\frac{2\pi f q}{F_s}\right),\qquad 0\leq q<L_f")
    p("For 0 &lt; alpha &lt;= 1, the Tukey window is defined by the following three intervals. Outside the note support, h is exactly zero. At the fitted positive taper, both window endpoints are zero.")
    table(["Normalized age x", "Window amplitude"],[
        ["0 &lt;= x &lt; alpha/2", "[1 - cos(2 pi x / alpha)] / 2"],
        ["alpha/2 &lt;= x &lt;= 1 - alpha/2", "1"],
        ["1 - alpha/2 &lt; x &lt;= 1", "[1 - cos(2 pi (1 - x) / alpha)] / 2"],
    ],[190,WIDTH-190])
    fig("02_finite_notes.png", "Analytic illustrations of individual unit-gain notes using the frozen duration and taper. Each panel contains about 9.3 carrier cycles, while its physical duration changes strongly with pitch. These curves illustrate the formula; they are not new recording measurements.")
    p("Every comparison births a new note for each operand. Its frequency, gain, taper, and duration stay captured until that note ends. Older notes continue while later comparisons change. The overlap sum therefore contains a history of recent comparisons, even though event handling never waits behind an audio queue.")
    eq(r"s[n]=\sum_k\sum_{a=1}^{2}g_{ka}\,\widetilde h_{f_{ka}}[n-n_k-m_a],\qquad n_k=\left\lfloor\frac{kF_s}{F_c}\right\rfloor")
    p("Here m<sub>a</sub> is the integer part of the lane's relative onset, and the tilde includes its fractional-sample interpolation. At the reference rate n<sub>k</sub> = 147k. All active terms sum directly, with no division by the number of sounding notes.")
    p("This separates two notions often conflated in early experiments: the 300 Hz clock determines when notes are born; the value-controlled carrier determines what each finite note contains. A birth clock is not an oscillator pitch grid.")

    # 5. Frequency derivation.
    section("Why a sine source produces 300 Hz lines", "04 / Periodicity and spectral structure")
    p("Consider a value held fixed while its finite note h<sub>f</sub>(t) is launched every P = 1/F<sub>c</sub> seconds. Away from the beginning and end of the run, the sum is periodic. Its Fourier-series coefficients sample the continuous note spectrum H<sub>f</sub> at multiples of the launch clock.")
    eq(r"s_f(t)=\sum_{k\in\mathbb{Z}}h_f(t-kP),\qquad c_m=F_c H_f(mF_c)")
    eq(r"S_f(\nu)=F_c\sum_{m\in\mathbb{Z}}H_f(mF_c)\,\delta(\nu-mF_c)")
    p("Multiplying a sine by a finite time window shifts that window's transform to the positive and negative carrier frequencies. Its envelope remains centered near a continuous value-controlled pitch, while periodic repetition samples that envelope on the 300 Hz family.")
    eq(r"H_f(\nu)=\frac{W_T(\nu-f)-W_T(\nu+f)}{2j}")
    fig("03_clock_family.png", "Explanatory rectangular-window limit near a 1,150 Hz carrier: the sinc envelope is continuous, but a stationary periodic train has discrete lines at multiples of 300 Hz. This schematic omits the negative-frequency lobe, the fitted Tukey edges, the second lane, and the FIR. It is not a measured spectrum.")
    p("Changing operand values disrupt exact periodicity and adds transient and inter-harmonic structure. Thus a moving diagonal ridge and a fixed 300 Hz line family can coexist. Strictly stationary, infinitely repeated notes have only clock harmonics; a low carrier does not imply a separate persistent oscillator at that exact off-grid frequency.")
    p("This explains why the original fixed-bin interpretation was too restrictive. A bank could mimic some spectra, but the observed line spacing alone never selected a bank or a sawtooth. Repeated sine notes provide another mechanism and, with the known schedules, transfer much more accurately across the saved recordings.")

    # 6. Bandwidth.
    section("The widening focus and apparent energy trade", "05 / Duration, bandwidth, and coherence")
    p("A fixed cycle count makes note duration inversely proportional to carrier pitch. The characteristic spectral width of a time-limited note is proportional to the inverse of its duration. Higher selections therefore spread across a broader frequency interval without requiring a value-dependent tau control or a separately moving Gaussian filter.")
    eq(r"T(f)=\frac{C}{f},\qquad \Delta\nu\sim\frac{1}{T}=\frac{f}{C},\qquad F_cT=\frac{F_cC}{f}")
    fig("04_duration_bandwidth.png", "Equation-derived scaling under the frozen profile. Duration falls from approximately 89.42 ms to 2.29 ms, while characteristic width increases from about 11.18 Hz to 435.86 Hz. Width here is the 1/T scale, not a measured half-power bandwidth.")
    p("The same relation predicts roughly 26.83 clock periods of overlap for a repeated minimum-pitch note, versus 0.69 periods at the maximum. The previous four-period packet cap allowed only 13.33 ms and removed much of the low-pitch history. That was a structural change, not merely a gentler envelope setting.")
    p("At fixed peak amplitude, an isolated note's energy generally falls as its duration shrinks. Ignoring the finite-carrier cross term, the squared integral of a Tukey window gives the following approximation:")
    eq(r"E_h(f)\approx\frac{A^2T(f)}{2}\left(1-\frac{5\alpha}{8}\right)\propto\frac{1}{f}")
    p("The complete signal is a coherent sum, so cross terms between overlapping notes can reinforce or cancel. Total output energy is not the sum of isolated-note energies. No automatic conservation rule, Gaussian energy budget, or active-note gain normalization is needed by the fitted model.")
    p("The earlier ordered-sweep analysis independently recorded strongly varying local RMS: its first interval had 5th/50th/95th percentiles 0.0156/0.0451/0.1332, and the later scan 0.0125/0.0418/0.1862. Those saved observations do not establish constant energy. They also do not identify different generators for the two sweep regions. Their exact operand schedules were not recovered. [R10]")

    # 7. Delay and FIR.
    section("The apparent comb and the shared response", "06 / Interference and output filtering")
    p("For two identical signals separated by delta seconds, their sum has an interference response 1 + exp(-j 2 pi nu delta). After dividing by its DC gain, the ideal magnitude and cancellation frequencies are:")
    eq(r"\frac{|1+e^{-j2\pi\nu\delta}|}{2}=|\cos(\pi\nu\delta)|,\qquad \nu_k=\frac{2k+1}{2\delta}")
    p("The fitted delay predicts minima near 2,216, 6,647, 11,078, and 15,509 Hz. These track the muted regions noticed in the spectrograms. Exact cancellation requires matching signals and coherent timing. Different operand values can fill the minima; the discrete interpolation used by the reconstruction also makes the ideal zero-depth prediction approximate.")
    fig("05_delay_and_fir.png", "Top: ideal pair interference and the exact linear fractional-shift kernel from the frozen model, normalized to the pair's DC gain. Bottom: the frozen FIR's absolute frequency response, including calibration gain. Both are calculated from saved parameters, not refitted to these plots.", maxheight=263)
    eq(r"y[n]=\sum_{\ell=0}^{30} b[\ell]\,s[n-\ell]")
    p("The coefficients are an effective common response learned from Selection, not an identified original analog filter. It can absorb source coloration, recording response, and other unresolved linear effects. The coefficients are not symmetric: the saved 15-frame alignment convention must not be described as a proven constant group delay.")
    p("A forum comment suggested a toothy source and a possible band-reject filter while explicitly expressing uncertainty. That historical suggestion motivated useful hypotheses, but it is not evidence of Andrut's implementation. Timo Bingmann's separate Sound of Sorting program documents a triangular, enveloped source; its implementation should not be attributed to Andrut. [R11]")

    # 8. Estimation.
    section("Identification and alignment methodology", "07 / How the model was obtained")
    p("Knowing the array allows each deterministic algorithm to regenerate ordered operand sequences before fitting any audio. That removes a major ambiguity: otherwise a poor match could come from the wrong comparison history rather than the wrong synthesizer. The source controls are theta = (base pitch, rank step, cycles, taper, operand delay). [R4, R5]")
    sub("A compact nonlinear source and a linear output fit")
    p("The saved refinement script searches the compact source controls on Selection with seeded differential evolution. For each candidate, a 31-column shifted-source matrix B is compared with aligned reference samples. A small ridge term stabilizes the common FIR least-squares estimate. Candidate evaluations use up to 20,000 selected frames; the final response is fitted using up to 80,000. These are fitting-sample counts, not independent recordings.")
    eq(r"\widehat{\mathbf{b}}=(B^TB+\lambda I)^{-1}B^T\mathbf{x},\qquad \lambda=10^{-8}\frac{\operatorname{tr}(B^TB)}{31}")
    p("Source parameters and output FIR are then frozen. Insertion and Gnome reuse them without a new source, filter, or primary output gain. Recording offsets and effective operand order are identified for each clip. This is cross-algorithm transfer, not a preregistered blind experiment: the research process inspected these recordings while developing hypotheses.")
    table(["Clip", "Late-operand offset in source file", "Pack waveform alignment offset"],[
        ["Selection", "6,463 samples", "6,416 samples"],
        ["Insertion", "959 samples", "912 samples"],
        ["Gnome", "5,532 samples", "5,485 samples"],
    ],[83,200,WIDTH-283])
    p("The pack adds 32 preroll frames and uses a 15-frame FIR alignment convention: offset<sub>pack</sub> = offset<sub>late</sub> - 47. The first lane is earlier by the fitted 9.952 samples. The C++ scheduling lead is a different implementation offset and is treated separately in Section 11.")
    sub("Primary scoring interval")
    p("The scoring script omits the first and last 5,000 synthesis frames (about 113.38 ms at each edge), clips to available reference data, and compares the aligned body. This suppresses previous-scene sound and cut-boundary effects. It does not claim that excluded transients are matched, nor that the residual within the scored region is purely encoding noise.")

    # 9. Primary validation.
    section("Agreement with the supplied recordings", "08 / Existing primary measurements")
    p("The primary score is an uncentered relative squared-error measure. Its denominator is reference energy, not mean-subtracted variance. Pearson correlation separately measures mean-centered waveform correlation. Neither quantity is a percentage of perceptual identity.")
    eq(r"Q_E=1-\frac{\sum_n(x[n]-y[n])^2}{\sum_n x[n]^2},\qquad \rho=\frac{\langle x-\bar{x},y-\bar{y}\rangle}{\|x-\bar{x}\|\,\|y-\bar{y}\|}")
    table(["Algorithm", "Scored frames", "Correlation", "Energy explained", "Residual RMS"],[
        [e["algorithm"], f"{e['metrics']['compared_frames']:,}", f"{e['metrics']['correlation']:.5f}", f"{100*e['metrics']['fixed_gain_energy_explained']:.3f}%", f"{e['metrics']['residual_rms']:.6f}"] for e in entries
    ],[76,90,83,119,WIDTH-368])
    fig("06_validation_scores.png", "Saved recording-agreement scores with the frozen common gain. The axis begins at zero; the narrow differences near 98% should not obscure the larger fact that the same model transfers across distinct comparison histories.")
    p("Selection is the fitting recording, so its score is not an independent prediction. Insertion and Gnome support transfer of the source mechanism. Their primary scores use the common FIR gain. The optional best scalar gains in the saved diagnostics are approximately 0.99991, 1.01175, and 1.01211; those corrections were not applied to the primary scores or exported reconstruction audio. [R2]")
    p("The full Python reconstructions peak at approximately 0.59772, 0.63155, and 0.61480. The nine exported WAV files were previously read back and checked. Six purely synthesized files were also regenerated in a separate process with the reference directory intentionally unavailable; their hashes matched. This demonstrates that synthesis uses the profile and event sequence rather than replaying the reference samples.")
    p("The remaining 1.75-2.60% error energy has not been uniquely attributed. It may include residual source-model mismatch, alignment details, recording processing, and encoding effects. A high score does not license assigning all remaining error to any one of those causes.")

    # 10. Saved visual evidence.
    section("Spectral agreement across complete runs", "09 / Reused empirical figure")
    fig("spectrogram_comparison.png", "Existing reference/reconstruction spectrograms from the original listening-pack analysis. Rows are Selection, Insertion, and Gnome; columns are supplied reference and reconstruction. All panels use 44.1 kHz data, a 2,048-sample STFT, 1,792-sample overlap, the same -80 to -20 dB magnitude color limits, and a 0-5 kHz display range. The horizontal origin is the beginning of the scored body, not the start of each source file.", maxheight=405)
    p("The correspondence is algorithm-dependent: persistent operands, moving operands, and different revisit patterns generate distinct structures. Matching those structures with the same instrument is stronger evidence than matching a generic ordered sweep. The plot still cannot prove a unique generator: it displays magnitude and loses phase information.")
    p("A 2,048-sample analysis window spans about 46.44 ms and has bin spacing about 21.53 Hz. Window smoothing broadens narrow features; zero padding, if used for another display, only densifies frequency samples. Neither a spectrogram's bright ridge nor a visual upper edge directly measures a source oscillator endpoint.")
    callout("How to read the image", "Repeated lines reveal periodic structure; moving intensity reveals changing packet spectra and interference. These are compatible observations. Treating every bright line as a separately synthesized oscillator discards the event timing that makes the reconstruction work.")

    # 11. Waveforms and ablation.
    section("Waveform phase and mechanism ablations", "10 / What the saved controls rule out")
    fig("waveform_comparison.png", "Existing aligned 20 ms waveform excerpts at the same 70%-of-run event position for each algorithm. Solid and dashed traces share amplitude scale. These examples complement the full-body numerical scores; they are selected excerpts, not additional validation recordings.", maxheight=260)
    table(["Saved model modification", "Selection", "Insertion", "Gnome"],[
        ["Frozen model", *[f"{100*e['metrics']['gain_fitted_energy_explained']:.2f}%" for e in entries]],
        ["Remove operand offset", *[f"{100*e['ablations']['same_onset']['gain_fitted_energy_explained']:.2f}%" for e in entries]],
        ["Cap notes at four clock periods", *[f"{100*e['ablations']['four_period_duration_cap']['gain_fitted_energy_explained']:.2f}%" for e in entries]],
        ["Gaussian window + four-period cap", *[f"{100*e['ablations']['gaussian_four_period_cap']['gain_fitted_energy_explained']:.2f}%" for e in entries]],
    ],[248,82,82,WIDTH-412])
    p("These ablation scores allow one best scalar gain for every variant, including the full model, while retaining the frozen output-filter shape. That concession prevents a quieter variant from losing solely on level. It does not re-optimize source controls or learn a replacement filter. The large no-delay collapse supports the importance of operand timing within this model family. The duration cap is also materially harmful.")
    p("The combined Gaussian-plus-cap condition changes two factors; it does not isolate the window's independent causal effect. More generally, these controls reject these specific substitutions under the frozen response. They do not prove that every alternative model with a separately optimized response must fail.")

    # 12. Ablation chart and first pair.
    section("What remains uncertain in the reconstruction", "10 / Sensitivity and identifiability")
    fig("07_saved_ablations.png", "Saved mechanism controls, visualized from the manifest. All bars use optional scalar-matched energy explained. Values therefore differ slightly from the fixed-common-gain primary table. No fitting was rerun for this paper.")
    sub("The first two array elements")
    p("Selection's fixed-gain score falls from 97.395% to 95.747% when the first pair is reversed. That favors the visible order within this reconstruction. Insertion's later state becomes the same after its first insertion, so the scored body is unchanged. Gnome's extra initial comparison is absorbed by a one-event alignment shift. Those two clips cannot independently resolve the initial-pair uncertainty.")
    sub("Source and filter are jointly identifiable only in an effective sense")
    p("The source controls and short FIR can trade some spectral details. A finite collection of output recordings cannot determine the creator's intent, hidden processing order, exact numeric representation, or whether an equivalent signal was produced offline. Replacing the compact source with a sufficiently flexible system could recreate the same PCM by other means. The preferred model is useful because it is compact, reproducible, interpretable, and transfers across schedules.")
    sub("A 4.2 kHz ridge is not the fitted endpoint")
    p("The saved fit ends at 4,054.66 Hz for rank 99. Finite-note spectral width, operand interference, the output response, and the STFT can place visible energy above that carrier. The earlier 4.2 kHz ceiling was a practical exploratory constraint rather than a final measured oscillator setting. Upper spectral shoulders likewise do not require 17.1 kHz carrier bins.")
    p("The data support delayed sine bursts as a successful reconstruction of these three recordings. They do not establish universality across all Andrut demonstrations, exact behavior on duplicate/constant arrays in the original program, or whether the original source used the same numerical constants.")

    # 13. Port parity.
    section("Independent C++ implementation agreement", "11 / Implementation verification")
    p("The application uses a C++14 implementation of the frozen burst model. Its x64 Release exporter produced the saved ordered comparison fixtures and raw float32 output. The parity tool first checks normalized operand pairs, including Gnome's declared reversal, then compares C++ PCM against Python synthesis from the frozen profile. It does not read the reference recordings or fit another response. [R6, R7]")
    table(["Algorithm", "Aligned frames", "Max abs. error", "RMS error"],[
        [n,f"{parity['algorithms'][n]['aligned_frames']:,}",f"{parity['algorithms'][n]['maximum_absolute_sample_error']:.6g}",f"{parity['algorithms'][n]['rms_sample_error']:.6g}"] for n in names
    ],[91,115,145,WIDTH-351])
    fig("08_cpp_parity.png", "Existing C++/Python float32 agreement. The acceptance threshold is 2 x 10<super>-6</super>; the largest saved error is 4.42972 x 10<super>-7</super>. This comparison verifies port behavior, independently of the larger model-to-recording error.")
    p("The only shift is the documented relative scheduling offset: 128 C++ lead frames minus 32 Python preroll frames = 96 samples, or approximately 2.177 ms. No optimized gain, filter, or recording-specific time shift is introduced. Maximum normalized-operand error is 2.95013 x 10<super>-8</super>, consistent with the float representation of normalized rank values.")
    p("C++ uses double precision for captured note evaluation and mixing, with float32 device/export samples. Duration rounding matches Python ties-to-even. The Tukey coordinate uses age/(length - 1). A fractional delay d = m + beta is represented by a weighted current/previous sample pair, equivalent to z<super>-m</super>[(1 - beta) + beta z<super>-1</super>] for nonnegative causal placement. The Python fixture expresses the first operand as a negative relative offset; the app supplies causal scheduling lead.")
    eq(r"\widetilde h[q]=(1-\beta)h[q]+\beta h[q-1],\qquad 0\leq\beta<1")
    p("The saved pack has three complete reference rerenders plus 22 listening scenario groups. Excerpting longer runs produces 55 raw/listening clip pairs. Its maximum raw peak is 0.640878 and its documented common listening gain is 1.0. It is an integration delivery, not an additional independent reference dataset.")

    # 14. Runtime.
    section("From reconstruction to deterministic playback", "12 / Application architecture")
    p("The runtime separates the sorting trace, prepared audio timeline, transport state, and output device. SoundEngine receives normalized participants and roles without importing SortingLogic or SDL types. The app prepares immutable timeline data on a worker, and the callback consumes preallocated state. [R7-R9]")
    table(["Stage", "Ownership and contract"],[
        ["Trace and simulation", "Every source event is preserved; visuals reconstruct exact cursor state."],
        ["Timeline preparation", "App-owned worker maps events and coalesces requests by generation."],
        ["Seek preparation", "Bounded history rebuilt using current settings; stale revisions discarded."],
        ["Audio callback", "Two lanes of 256 note slots, captured parameters, shared FIR; no queued event backlog."],
        ["Device output", "44.1 kHz internal model; output-rate conversion and final safety clamp."],
        ["Feedback", "Timeline generation and transport revision fence the visible playhead."],
    ],[128,WIDTH-128])
    sub("Two distinct timing modes")
    p("Reference cadence presents eligible comparisons or fallback probes consecutively at 300/s. Fixed duration maps the full trace to the selected run duration; each excitation tick selects the latest eligible event. Fast comparisons can therefore lack separate audible presentation, while slow comparisons are repeated. The complete simulation trace remains intact in either case.")
    eq(r"t_{\mathrm{play}}=t_{\mathrm{anchor}}+\frac{n_{\mathrm{source}}-n_{\mathrm{anchor}}}{44100}")
    p("The playback anchor is immutable for a transport run. Feeding the advancing playhead back into this equation would compound elapsed time. The 128-frame synthesis lead is kept outside simulation elapsed time. Audio drives playback only when a valid device and timeline are ready; a monotonic host clock keeps simulation available during preparation or device failure.")
    p("Pause, step, and seek either hold excitation of the inspected selection or apply the chosen release fade to new births. Existing notes retain their captured envelopes. Completion, Reset, and trace replacement stop births and let notes drain. Seeking prepares the notes and filter history appropriate to the current settings and canonical clock phase, then installs through a 128-source-frame crossfade. It does not replay every event from trace start.")
    p("The final portable smoke run exposed delayed old pause feedback on the same timeline. Adding transport-revision fencing prevented that stale report from cancelling a newer Play. This is an application synchronization result, not a change in the reconstructed instrument.")

    # 15. Engineering evidence.
    section("Recorded engineering validation", "13 / Existing tests and practical limits")
    table(["Configuration", "SoundEngine", "AppFramework", "SortingLogic"],[
        ["x64 Debug", "Passed", "Passed", "Passed"],
        ["x64 Release", "Passed", "Passed", "Passed"],
        ["Win32 Debug", "Passed", "Passed", "Passed"],
        ["Win32 Release", "Passed", "Passed", "Passed"],
    ],[147,116,116,WIDTH-379])
    p("The completion record reports 417,371 SoundEngine assertions and 106,832 SortingLogic checks with zero failures, with AppFramework passing in all four configurations. These are software assertions, not independent scientific observations. The paper reuses that recorded execution; it does not rerun the suites. Test source and the project research log provide the audit trail. [R8, R9]")
    table(["Area", "Existing checks and recorded evidence"],[
        ["Burst / transport DSP", "Captured pitch, rounding, endpoint behavior, overlap, intermediate playheads, completion, and irregular block partitions."],
        ["Seek / release", "Prepared snapshots compared with uninterrupted output; release durations 0/250/2,000 ms; revision identities and no callback allocation."],
        ["Output conversion", "22.05, 32, 44.1, 48, and 96 kHz: finite output, clock advancement, continuity, attenuation checks, and overload behavior."],
        ["Legacy preservation", "Frozen legacy PCM parity, exact-zero comparison-envelope regression, and old composable-source tests retained."],
        ["Application contracts", "Settings migration, restoration scope, family/variant levels, timeline mapping, mailbox behavior, and stale-playhead regression."],
    ],[120,WIDTH-120])
    p("The three known-reference schedules recorded maximum simultaneous finite-note counts of 33, 34, and 40. The application reserves 256 slots per operand; the default examples therefore have substantial margin. That observation is not a universal occupancy proof for every possible control combination. Slot exhaustion and output overload remain explicit diagnostics rather than silent truncation or automatic gain normalization.")
    p("Earlier idle stutter was traced during implementation to extremely small residual envelope tails and real-time numerical cost. The legacy exact-zero regression remains relevant, while the reconstructed finite notes explicitly release their slots at their endpoint. No new callback latency distribution or comparative CPU benchmark is claimed here.")
    p("The extracted portable x64 ZIP launched, built a trace, and advanced playback in a live local smoke check. Other computers' graphics drivers and audio devices were not available for this study. The saved evidence does not justify claiming a fully exhaustive live hardware matrix or subjective listening study.")

    # 16. Controls.
    section("Preserving the signature while allowing control", "14 / Generalization and instrument design")
    p("The reconstructed signature depends on relationships among pitch, launch cadence, finite duration, phase, operand timing, and common processing. A volume control can preserve those relationships. Changing a clock or duration changes the instrument itself, even if the new setting remains musically useful.")
    table(["Control family", "Reference-preserving use", "Effect of customization"],[
        ["Master / mute", "Uniform scaling before clipping", "Changes level; safety-clamp activation changes waveform."],
        ["Value normalization", "Fixed prepared-input min/max", "Retains pitch meaning across reorderings; different value distributions alter statistics."],
        ["Speed and event sampling", "Keep excitation at 300 Hz", "Fixed-duration mode repeats/skips audible selections; musical phrase changes with speed."],
        ["Cycles / taper", "Frozen values", "Changes overlap, width, high-frequency shoulders, and phase interference."],
        ["Operand delay / order", "Fitted relative delay and metadata", "Moves cancellations and changes coherent summation."],
        ["Role gains / tone", "Equal effective gains; optional tone off", "Colors the spectrum and can weaken cancellations."],
    ],[110,168,WIDTH-278])
    p("Changing array size while preserving pitch endpoints keeps the instrument's range and duration law. It changes the available value density and comparison history. It cannot guarantee the same waveform, rhythm, loudness distribution, or algorithmic phrase. Descending direction likewise preserves pitch assignments while changing event order.")
    p("The useful dimensionless controls include C, alpha, the overlap ratio F<sub>c</sub>C/f, and the delay phase 2 pi f delta. A time-scale transformation would have to scale several of these together to preserve normalized structure. Holding the audio instrument fixed while changing simulation speed preserves its timbral vocabulary more directly, but deliberately changes which events it articulates.")
    p("Basic controls therefore cover level, enable state, idle policy, release duration, and restoring defaults. Advanced controls expose pitch endpoints/mapping, excitation clock, cycles, taper, operand delay, and optional tone filtering. The default FIR and phase-reset behavior stay fixed in the application interface. Family and variant gains are editable but return to 0 dB with reconstructed defaults.")
    callout("Practical interpretation", "The default profile is a reproducible reference instrument. Customized profiles can be valuable musical instruments without remaining reconstructions of the supplied recording. The UI's reference/customized status makes that distinction visible instead of silently renaming every knob position 'authentic'.")

    # 17. Interpretation.
    section("Interpretation and boundaries of the result", "15 / What can responsibly be concluded")
    p("The strongest result is cross-schedule waveform transfer: a compact delayed-burst source and a common short output response explain nearly all scored energy in three supplied sorting recordings. Recovering the input made it possible to distinguish source behavior from comparison-history errors, and the independent port check verifies that the application implements the frozen mathematical model with small numerical error.")
    p("The apparent sawtooth hum, expanding focus, and comb-shaped dark regions have coherent explanations within that model. Periodic launch timing creates the harmonic grid; fixed carrier cycles make bandwidth grow with pitch; coherent operand delay produces conditional cancellation; finite, nearly rectangular notes create broad shoulders. These explanations jointly fit more evidence than assigning one separate processor to each visual symptom.")
    table(["Evidence class", "Supported statement", "Unresolved statement"],[
        ["Measured", "Saved waveform scores and exact schedule counts", "Perceptual identity for all listeners"],
        ["Fitted", "One effective source plus calibrated short FIR", "Creator's literal code and intended constants"],
        ["Derived", "Clock lines, inverse duration, conditional delay comb", "A dedicated original analog-like comb or reject circuit"],
        ["Implemented", "Sample-timed C++ transport and reproducible port", "Original video used the same live architecture"],
    ],[83,206,WIDTH-289])
    p("The earlier two-sweep image suggested one versus two voices, different harmonic families, or different synthesis methods. Those remain unverified for that particular unidentified schedule. The validated quadratic-algorithm recordings show that equal source rules with different operand histories can already produce markedly different spectral textures. A final scan and a merge are not the same event sequence merely because both involve ordered values.")
    p("This study has no confidence intervals for fitted controls, no held-out collection of additional creators or recording chains, and no formal perceptual protocol. It cannot isolate how much of the FIR belongs to the original source versus the media path. Its fine numerical fit should be treated as a frozen engineering reference, with explicit uncertainty about physical uniqueness.")
    p("For further algorithm expansion, the most consequential design choice is event semantics: which reads, comparisons, writes, or probes should articulate the instrument. That choice belongs to the simulator's trace-to-audio mapping. The reconstruction supplies a stable sound-producing mechanism; it does not automatically prescribe the musical meaning of every future sorting operation.")

    # 18. Exact appendix.
    section("Exact reproducibility constants", "Appendix A / Frozen numerical specification")
    p("The complete rank permutation follows, left to right. Zero is the shortest bar. Only the first two entries carry the initial-frame uncertainty described in Section 1. [R4]", "small")
    ranks=array["ranks"]
    code("\n".join(" ".join(f"{v:2d}" for v in ranks[i:i+20]) for i in range(0,100,20)))
    p("The causal FIR coefficients below include the calibrated reference gain. They are listed in convolution order b[0] through b[30], not the centered/reversed fitting-matrix order. Use the frozen JSON for full decimal serialization. [R3]", "small")
    rows=[]
    for i in range(16):
        j=i+16
        rows.append([str(i),f"{taps[i]:.17g}",str(j) if j<31 else "",f"{taps[j]:.17g}" if j<31 else ""])
    table(["Index", "b[index]", "Index", "b[index]"],rows,[40,208,40,WIDTH-288])
    p("Core controls: base = 104.0322699094767 Hz; rank step = 39.90533652820316 Hz; cycles = 9.302650315454065; alpha = 0.025666772580559438; early/late relative delays = [-9.95199933051814, 0] source samples; sample rate = 44,100 Hz; launch clock = 300 Hz. FIR alignment bookkeeping = 15 frames. These values are copied from the frozen profile without refitting.", "small")

    # 19. Audit trail.
    section("Artifact provenance and reproduction", "Appendix B / Audit trail")
    p(f"Implementation checkpoint: <b>{commit}</b>. The paper was authored from the existing artifacts at that checkpoint. Reference measurements were produced in the earlier reconstruction study; application validation was recorded in the completion wave. No tests or fitting were run to typeset this document.")
    sub("Input identity (SHA-256)")
    hash_rows=[("Source image",array["source_image_sha256"])] + [(e["algorithm"]+" reference WAV",e["reference_sha256"]) for e in entries]
    for label, digest in hash_rows:
        p("<b>"+label+"</b>","small"); code(digest)
    sub("Existing artifact locations")
    code("SoundEnginePreview/research/andrut_delayed_sine_bursts_v1.json\nSoundEnginePreview/research/andrut_array.json\nx64/Release/AudioPreviewAndrutReconstruction/manifest.json\nx64/Release/AudioPreviewReconstructedPackFinal/reference_parity.json")
    p("The first manifest includes exact comparison values, source-event frames, recording hashes, primary metrics, ablations, reversed-pair checks, export hashes, and A/B segment positions. The final pack manifest includes 22 scenario groups plus three complete reference rerenders, excerpt positions, raw statistics, and common listening gain. Generated media remain under ignored build output.")
    sub("Reproduce existing results when desired")
    p("These commands document the established workflow; they were not executed for this paper. Run with the project's offline Python environment and available supplied reference assets. The C++ preview exporter must be built separately.","small")
    code("python SoundEnginePreview/research/create_listening_pack.py\nSoundEnginePreview.exe --reconstructed-pack <output-directory>\npython SoundEnginePreview/research/compare_cpp_reference.py <output-directory>\npython docs/whitepaper/build_whitepaper.py")
    p("The white-paper builder reads saved measurements and copies existing empirical figures. It plots explanatory equations and emits PDF, editable Markdown, a compact evidence snapshot, and figure assets. It never reads recorded PCM, renders new listening samples, optimizes the fit, or invokes application test suites.")

    # 20. Sources.
    section("Sources and evidence map", "References / Existing study materials")
    refs=[
      ("R1", "Andrut. What different sorting algorithms sound like. Original reference video, identified in the study's provenance record. Supplied local extracts are the primary dataset; the video's page could not be retrieved during paper preparation.", "https://www.youtube.com/watch?v=t8g-iYGHpEA"),
      ("R2", "Existing reconstruction manifest and common-scale waveform/spectrogram plots. Primary fixed-gain scores, scalar-matched ablations, first-pair controls, export hashes, and note-occupancy counts.", "x64/Release/AudioPreviewAndrutReconstruction/manifest.json"),
      ("R3", "Frozen effective source and calibrated causal FIR. Authoritative numerical profile; its earlier alignment diagnostics include optional gains and are not the primary fixed-gain scores.", "SoundEnginePreview/research/andrut_delayed_sine_bursts_v1.json"),
      ("R4", "Known-array fixture and extraction procedure. Source-image hash, rank permutation, and first-pair temporal uncertainty.", "SoundEnginePreview/research/andrut_array.json; extract_array.py"),
      ("R5", "Python source renderer, fitting procedure, and scoring/export tool. See render_notes, fit_filter, refine, metrics, and make_pack.", "SoundEnginePreview/research/fit_note_model.py; refine_delayed_notes.py; create_listening_pack.py"),
      ("R6", "C++/Python parity report and comparison tool. Ordered operands checked separately; fixed 96-frame relative alignment, no gain/filter fit.", "x64/Release/AudioPreviewReconstructedPackFinal/reference_parity.json; SoundEnginePreview/research/compare_cpp_reference.py"),
      ("R7", "C++ burst source, FIR, prepared timeline, runtime state, and application integration.", "SoundEngine/ReconstructedDsp.cpp; ReconstructedSound.cpp; AudioTimeline.cpp; SDL2BarGraphics/AudioPresentationBridge.cpp; SdlAudioOutput.cpp"),
      ("R8", "Existing software regression suites. Source describes asserted invariants; execution status and counts are from the completion record.", "SoundEngineTests/SoundEngineTests.cpp; AppFrameworkTests/AppFrameworkTests.cpp; SortingLogicTests/SortingLogicTests.cpp"),
      ("R9", "Research log and recorded project completion report: build matrix, assertion/check counts, parity, transport-revision fix, and local portable-package smoke run. Execution counts are from the completion report, not a new run for this paper.", "agent-memory/audibilization/research-log.md; known-array-reconstruction.md; SoundEngine/README.md"),
      ("R10", "Earlier ordered-sweep reassessment. Saved ridge estimates, periodic projection, RMS percentiles, and explicit limits on source identification.", "agent-memory/audibilization/ordered-sweep-2-analysis.md"),
      ("R11", "Timo Bingmann. The Sound of Sorting (2013; page updated 2014). Distinguishes his implementation from Andrut's earlier demo and contains Sampo Syreeni's explicitly tentative filter comment. Page checked 17 October 2026.", "https://panthema.net/2013/sound-of-sorting/"),
    ]
    for rid, description, location in refs:
        p(f"<b>[{rid}]</b> {description}","small")
        # Narrow path text remains readable and wraps at explicit path separators.
        loc=location.replace("/", "/\u200b").replace("; ", ";<br/>")
        if location.startswith("https://"):
            loc=f'<link href="{location}" color="{TEAL}">{loc}</link>'
        p(loc,"small")
    p("Equations in Sections 3-6 and 8 are direct derivations under the stated reconstruction assumptions. Explanatory figures are labelled separately from empirical plots. No external filter hypothesis is promoted to a confirmed fact about the original implementation.","small")


def footer(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#d3dfe6"))
    canvas.setLineWidth(.5)
    canvas.line(50, 42, W-50, 42)
    canvas.setFillColor(colors.HexColor(GRAY))
    canvas.setFont("Sans",7.5)
    canvas.drawString(50, 29, "ACTUALLY GOOD SORTING SOUNDS  |  ANDRUT RECONSTRUCTION  |  v1.0")
    canvas.drawRightString(W-50,29,str(doc.page))
    if doc.page>1:
        canvas.setFillColor(colors.HexColor(TEAL));canvas.rect(50,H-31,24,2,fill=1,stroke=0)
    canvas.restoreState()


class PaperDoc(SimpleDocTemplate):
    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph) and flowable.style.name == "heading":
            title = flowable.getPlainText()
            key = "section_" + str(self.page)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title, key, level=0, closed=False)


def main():
    make_figures()
    content()
    pdf=OUT/"andrut_sorting_sonification_white_paper.pdf"
    doc=PaperDoc(str(pdf),pagesize=A4,rightMargin=50,leftMargin=50,topMargin=48,bottomMargin=57,
        title="Reconstructing Andrut's Sorting Sonification",author="Actually Good Sorting Sounds project",
        subject="An evidence-based reconstruction using delayed overlapping sine bursts",pageCompression=1)
    doc.build(story,onFirstPage=footer,onLaterPages=footer)
    markdown="\n".join(md)
    (OUT/"andrut_sorting_sonification_white_paper.md").write_text(markdown,encoding="utf-8")
    evidence={"date":"2026-10-17","implementation_commit":commit,"new_tests_or_fits_run":False,
        "profile":profile,"array":array,"recording_metrics":[{k:v for k,v in e.items() if k not in
          ("comparison_values","event_frames_late_operand","AB_timeline")} for e in entries],
        "cpp_python_parity":parity,"final_pack_summary":{k:v for k,v in pack_final.items() if k!="clips"},
        "engineering_validation":{"provenance":"Existing project completion report and research log; not rerun for this paper",
            "configurations":["x64 Debug","x64 Release","Win32 Debug","Win32 Release"],
            "sound_engine_assertions":417371,"sorting_logic_checks":106832,
            "all_three_suites_passed":True,"cross_machine_hardware_matrix_verified":False},
        "source_artifacts":[str(p.relative_to(ROOT)) for p in [PACK/"manifest.json",FINAL/"reference_parity.json"]]}
    (OUT/"andrut_whitepaper_evidence.json").write_text(json.dumps(evidence,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"pdf":str(pdf),"equations":equation_counter,"figures":figure_counter,"sha256":hashlib.sha256(pdf.read_bytes()).hexdigest()},indent=2))


if __name__=="__main__":
    main()
