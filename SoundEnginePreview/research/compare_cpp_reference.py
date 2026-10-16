"""Compare the C++ reconstructed renderer with the frozen Python reference.

The only time shift is the documented 128-frame C++ scheduling lead minus the
32-frame preroll used by the frozen Python export. No gain or filter fit is run.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "x64/Release/AudioAnalysis/packages"))
from scipy import signal
from scipy.io import wavfile
import numpy as np

from fit_note_model import render_notes
from analyze_andrut import comparisons


def verify(output_dir: Path):
    research = Path(__file__).parent
    profile = json.loads((research / "andrut_delayed_sine_bursts_v1.json").read_text())
    array = json.loads((research / "andrut_array.json").read_text())
    ranks = array["ranks"]
    assert sorted(ranks) == list(range(100))
    config = profile["configuration"]
    results = {}
    for name in ("Selection", "Insertion", "Gnome"):
        expected_ranks = comparisons(ranks, name)
        if profile["alignments"][name]["swapped_operands"]:
            expected_ranks = expected_ranks[:, ::-1]
        fixture = json.loads((output_dir / f"reference_{name.lower()}_andrut100_events.json").read_text())
        actual_events = [event for event in fixture["events"] if event["kind"] in (1, 2)]
        actual_pairs = np.array([[event["first"], event["second"]] for event in actual_events], dtype=np.float64)
        expected_pairs = expected_ranks.astype(np.float64) / 99.0
        if actual_pairs.shape != expected_pairs.shape:
            raise AssertionError(f"{name}: event sequence shape {actual_pairs.shape} != {expected_pairs.shape}")
        max_event_error = float(np.max(np.abs(actual_pairs - expected_pairs))) if actual_pairs.size else 0.0
        if max_event_error > 1.0e-7:
            raise AssertionError(f"{name}: C++ comparison fixture differs from the frozen ordered operands by {max_event_error}")

        source = render_notes(
            expected_ranks,
            rate=profile["sample_rate"],
            step=config["step"],
            base=config["base"],
            duration=config["duration"],
            mode="cycles",
            window="tukey",
            tukey=config["tukey"],
            delays=tuple(config["delays"]),
            clock_hz=profile["comparison_clock_hz"],
            preroll_frames=32,
        )
        expected = signal.convolve(source, np.asarray(profile["output_filter"]["coefficients_causal"]),
                                   mode="full", method="fft")
        path = output_dir / f"reference_{name.lower()}_andrut100_raw_float32.wav"
        rate, actual = wavfile.read(path)
        if rate != profile["sample_rate"] or actual.dtype != np.float32 or actual.ndim != 2 or actual.shape[1] != 2:
            raise AssertionError(f"{name}: unexpected C++ WAV format {rate} Hz, {actual.dtype}, shape {actual.shape}")
        actual = actual[:, 0].astype(np.float64)
        # C++ lead 128, frozen Python preroll 32; this is the sole alignment.
        aligned = actual[96:96 + len(expected)]
        if len(aligned) != len(expected):
            raise AssertionError(f"{name}: C++ render is short after the documented 96-frame alignment")
        difference = aligned - expected
        metrics = {
            "comparison_count": int(len(expected_ranks)),
            "maximum_operand_position_error": max_event_error,
            "aligned_frames": int(len(expected)),
            "alignment_frames": 96,
            "maximum_absolute_sample_error": float(np.max(np.abs(difference))),
            "rms_sample_error": float(np.sqrt(np.mean(difference * difference))),
            "cxx_peak": float(np.max(np.abs(aligned))),
            "python_peak": float(np.max(np.abs(expected))),
        }
        if metrics["maximum_absolute_sample_error"] > 2.0e-6:
            raise AssertionError(f"{name}: maximum sample error {metrics['maximum_absolute_sample_error']:.9g} exceeds 2e-6")
        results[name] = metrics
    report = {
        "reference_profile": profile["id"],
        "array_fixture_sha256": array["source_image_sha256"],
        "alignment": "C++ scheduling lead 128 minus frozen Python preroll 32 = 96 samples",
        "gain_or_filter_fitted": False,
        "maximum_sample_error_tolerance": 2e-6,
        "algorithms": results,
    }
    (output_dir / "reference_parity.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path, nargs="?", default=ROOT / "x64/Release/AudioPreviewReconstructedPack")
    verify(parser.parse_args().output)
