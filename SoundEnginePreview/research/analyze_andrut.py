"""Evidence gathering for the three known-array reference recordings.

Analysis dependencies are separate from the application. Run with the workspace
Python; packages from x64/Release/AudioAnalysis/packages are supported.
"""
import argparse
import hashlib
import json
import sys
import warnings
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "x64/Release/AudioAnalysis/packages"))
import numpy as np
from scipy import signal
from scipy.io import wavfile
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def comparisons(ranks, algorithm):
    a = list(ranks)
    pairs = []
    if algorithm == "Selection":
        for i in range(len(a) - 1):
            minimum = i
            for j in range(i + 1, len(a)):
                pairs.append((a[minimum], a[j]))
                if a[j] < a[minimum]:
                    minimum = j
            a[i], a[minimum] = a[minimum], a[i]
    elif algorithm == "Insertion":
        for i in range(1, len(a)):
            key, j = a[i], i - 1
            while j >= 0:
                pairs.append((a[j], key))
                if a[j] <= key:
                    break
                a[j + 1] = a[j]
                j -= 1
            a[j + 1] = key
    elif algorithm == "Gnome":
        i = 1
        while i < len(a):
            pairs.append((a[i - 1], a[i]))
            if a[i - 1] <= a[i]:
                i += 1
            else:
                a[i - 1], a[i] = a[i], a[i - 1]
                i = max(1, i - 1)
    else:
        raise ValueError(algorithm)
    assert a == sorted(ranks)
    return np.array(pairs, dtype=int)


def read_wave(path):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", wavfile.WavFileWarning)
        rate, x = wavfile.read(path)
    if np.issubdtype(x.dtype, np.integer):
        x = x.astype(float) / (np.iinfo(x.dtype).max + 1)
    else:
        x = x.astype(float)
    return rate, x.mean(axis=1) if x.ndim > 1 else x


def probe(reference_dir, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    ranks = json.loads((Path(__file__).parent / "andrut_array.json").read_text())["ranks"]
    assert sorted(ranks) == list(range(100))
    result = {"ranks": ranks, "recordings": {}}
    fig, axes = plt.subplots(3, 3, figsize=(17, 12), constrained_layout=True)
    for row, name in enumerate(("Selection", "Insertion", "Gnome")):
        path = reference_dir / f"Andrut_{name}_Sort.wav"
        fs, x = read_wave(path)
        pairs = comparisons(ranks, name)
        np.save(output_dir / f"{name.lower()}_comparison_values.npy", pairs)
        block = 147
        usable = x[:len(x) // block * block].reshape(-1, block)
        folded_power = np.mean(usable * usable, axis=0)
        folded_mean = usable.mean(axis=0)
        result["recordings"][name] = {
            "path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "sample_rate": fs, "frames": len(x), "seconds": len(x) / fs,
            "comparisons": len(pairs), "expected_seconds_at_300": len(pairs) / 300,
            "duration_excess_ms": (len(x) / fs - len(pairs) / 300) * 1000,
            "rms": float(np.sqrt(np.mean(x*x))), "peak": float(np.max(abs(x))),
            "folded_minimum_sample": int(np.argmin(folded_power)),
            "folded_maximum_sample": int(np.argmax(folded_power)),
            "periodic_mean_energy_fraction": float(np.mean(folded_mean**2) / np.mean(x*x)),
        }
        axes[row, 0].plot(np.arange(147) / fs * 1000, np.sqrt(folded_power), label="Folded RMS")
        axes[row, 0].plot(np.arange(147) / fs * 1000, folded_mean, label="Folded signed mean")
        axes[row, 0].set(title=f"{name}: 147-sample clock fold", xlabel="Clock phase (ms)")
        axes[row, 0].legend()
        start = int(.25 * fs)
        axes[row, 1].plot(np.arange(1200) / fs * 1000, x[start:start+1200], lw=.7)
        axes[row, 1].set(title="Waveform, 0.25 s onwards", xlabel="Time (ms)")
        f, t, z = signal.stft(x, fs, nperseg=1024, noverlap=768, boundary=None)
        axes[row, 2].pcolormesh(t, f, 20*np.log10(np.maximum(abs(z), 1e-7)), cmap="magma", vmin=-80, vmax=-20, shading="auto", rasterized=True)
        axes[row, 2].set(ylim=(0, 5000), title=f"{len(pairs)} comparisons, {len(x)/fs:.3f} s", xlabel="Time (s)", ylabel="Hz")
    fig.savefig(output_dir / "reference_probe.png", dpi=140)
    plt.close(fig)
    (output_dir / "reference_probe.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result["recordings"], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--references", type=Path, default=ROOT / "x64/Release/Andrut's Reference")
    parser.add_argument("--output", type=Path, default=ROOT / "x64/Release/AudioAnalysis/AndrutKnownArray")
    args = parser.parse_args()
    probe(args.references, args.output)
