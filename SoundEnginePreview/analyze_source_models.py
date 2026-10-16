"""Validate and summarize the offline SoundEngine source model pack.

Usage:
  python analyze_source_models.py <AudioPreviewSourceModels directory>
"""
import json
import math
import pathlib
import struct
import sys
import warnings
from collections import Counter, defaultdict


def add_local_packages(pack_dir):
    package_dir = pack_dir.parent / "AudioAnalysis" / "packages"
    if package_dir.is_dir():
        sys.path.insert(0, str(package_dir))


def read_wave(path, expected_format, expected_rate):
    with path.open("rb") as stream:
        header = stream.read(44)
        if len(header) != 44 or header[:4] != b"RIFF" or header[8:12] != b"WAVE":
            raise ValueError(f"Bad RIFF/WAVE header: {path}")
        format_tag, channels = struct.unpack_from("<HH", header, 20)
        sample_rate = struct.unpack_from("<I", header, 24)[0]
        bits = struct.unpack_from("<H", header, 34)[0]
        data_size = struct.unpack_from("<I", header, 40)[0]
        if (format_tag, channels, sample_rate, bits) != (expected_format, 2, expected_rate, 32 if expected_format == 3 else 16):
            raise ValueError(f"Unexpected WAV format in {path}: {(format_tag, channels, sample_rate, bits)}")
        payload = stream.read(data_size)
        if len(payload) != data_size:
            raise ValueError(f"Truncated WAV data: {path}")
    return payload


def validate(pack_dir):
    import numpy as np

    manifest_path = pack_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    signals = manifest["signals"]
    errors = []
    if len(signals) != 129:
        errors.append(f"manifest has {len(signals)} signals, expected 129")
    if len(list(pack_dir.glob("*_raw_f32.wav"))) != 129:
        errors.append("raw float WAV count is not 129")
    if len(list(pack_dir.glob("*_listen_pcm16.wav"))) != 129:
        errors.append("PCM16 WAV count is not 129")

    scenario_counts = Counter(s["scenarioId"] for s in signals)
    expected_scenarios = {
        "ordered_scan": 36,
        "insertion_adjacent": 36,
        "selection_sort_64": 36,
        "trace_8changes_per_sec": 3,
        "trace_16changes_per_sec": 4,
        "pair_sweep_equal": 1,
        "pair_sweep_held6db": 4,
        "pair_sweep_bed": 3,
        "single_voice_probes": 3,
        "release_probe": 3,
    }
    for scenario, expected in expected_scenarios.items():
        if scenario_counts[scenario] != expected:
            errors.append(f"{scenario}: got {scenario_counts[scenario]}, expected {expected}")

    by_group = defaultdict(list)
    baseline_metrics = []
    target_peak = 10 ** (-1 / 20)
    expected_rms = 10 ** (-24 / 20)
    for signal in signals:
        if signal["sampleRate"] != 44100:
            errors.append(f"{signal['id']}: wrong sample rate")
        event_frames = signal["eventFrames"]
        if event_frames != sorted(event_frames) or len(event_frames) != len(signal["events"]):
            errors.append(f"{signal['id']}: event schedule is not reconstructible")
        for event, frame in zip(signal["events"], event_frames):
            if event["frame"] != frame or event["heldParticipant"] not in (-1, 0, 1):
                errors.append(f"{signal['id']}: event metadata does not match event frame list")
        if signal["scenarioId"] == "selection_sort_64":
            if len(event_frames) != 64 or signal["frameCount"] != 44100 * 5:
                errors.append(f"{signal['id']}: Selection Sort clip must be five seconds with 64 comparisons")
            expected_frames = [i * (44100 * 9 // 2) // 64 for i in range(64)]
            if event_frames != expected_frames:
                errors.append(f"{signal['id']}: Selection Sort events are not evenly sampled over 4.5 seconds")
            if any(not event["firstActive"] or not event["secondActive"] or event["heldParticipant"] != -1
                   for event in signal["events"]):
                errors.append(f"{signal['id']}: Selection Sort comparisons must use equal participant gain")
            if any(not (50 <= event["firstHz"] <= 4200 and 50 <= event["secondHz"] <= 4200)
                   for event in signal["events"]):
                errors.append(f"{signal['id']}: Selection Sort focus is outside the supported frequency range")
            spec = signal["renderSpec"]
            if spec["attackMs"] != 1 or spec["releaseMs"] != 2:
                errors.append(f"{signal['id']}: Selection Sort envelope must be 1/2 ms")
            envelope = signal["parameters"].get("scenarioEnvelope", {})
            if envelope.get("attackMs") != 1 or envelope.get("releaseMs") != 2:
                errors.append(f"{signal['id']}: manifest scenario envelope does not match its render spec")
        try:
            raw_bytes = read_wave(pack_dir / signal["rawFile"], 3, 44100)
            pcm_bytes = read_wave(pack_dir / signal["listeningFile"], 1, 44100)
        except (OSError, ValueError) as exc:
            errors.append(str(exc))
            continue
        raw = np.frombuffer(raw_bytes, dtype="<f4")
        pcm = np.frombuffer(pcm_bytes, dtype="<i2")
        if raw.size != signal["frameCount"] * 2 or pcm.size != signal["frameCount"] * 2:
            errors.append(f"{signal['id']}: frame count does not match files")
            continue
        dc = float(np.mean(raw, dtype=np.float64))
        rms = float(np.sqrt(np.mean((raw.astype(np.float64) - dc) ** 2)))
        peak = float(np.max(np.abs(raw))) if raw.size else 0.0
        if abs(dc - signal["dc"]) > 2e-6 or abs(rms - signal["acRms"]) > 2e-6 or abs(peak - signal["peak"]) > 2e-6:
            errors.append(f"{signal['id']}: manifest raw measurements do not match PCM")
        listening_peak = float(np.max(np.abs(pcm.astype(np.float32) / 32768.0))) if pcm.size else 0.0
        listening_rms = signal["acRms"] * signal["appliedListeningGain"]
        if listening_peak > target_peak + 0.0001:
            errors.append(f"{signal['id']}: matched PCM peak exceeds -1 dBFS")
        by_group[signal["group"]].append(listening_rms)
        if signal["id"] in {
            "legacy__trace_16changes_per_sec",
            "legacy__pair_sweep_equal",
            "sine_slew2_a8_r8__trace_16changes_per_sec",
            "packet_fixed_gaussian_4period_reset_peak__trace_16changes_per_sec",
            "saw_q4_lp10khz__trace_16changes_per_sec",
            "saw_q4_lp10khz__selection_sort_64",
        }:
            baseline_metrics.append({
                "id": signal["id"],
                "model": signal["model"],
                "scenario": signal["scenarioId"],
                "rawDc": dc,
                "rawAcRms": rms,
                "rawPeak": peak,
                "listeningGain": signal["appliedListeningGain"],
                "matchedAcRms": listening_rms,
                "matchedPcmPeak": listening_peak,
                "frameCount": int(signal["frameCount"]),
            })

    group_summary = {}
    for group, values in by_group.items():
        if not values:
            continue
        spread = max(values) - min(values)
        group_summary[group] = {"signalCount": len(values), "minimumAcRms": min(values), "maximumAcRms": max(values), "spread": spread}
        if spread > 2e-5:
            errors.append(f"{group}: listening RMS match spread is {spread:.6g}")
        if max(values) > expected_rms + 2e-5:
            errors.append(f"{group}: listening target exceeds -24 dBFS")

    profile_scenarios = ("ordered_scan", "insertion_adjacent", "selection_sort_64")
    unique_profiles = {s["profileId"] for s in signals if s["scenarioId"] in profile_scenarios}
    if len(unique_profiles) != 36:
        errors.append(f"model matrix includes {len(unique_profiles)} profiles, expected 36")
    category_counts = Counter(
        "sine" if profile.startswith("sine_") else
        "packet_fixed" if profile.startswith("packet_fixed_") else
        "packet_cycles" if profile.startswith("packet_cycles_") else
        "saw" if profile.startswith("saw_") else "other"
        for profile in unique_profiles
    )
    expected_categories = {"sine": 6, "packet_fixed": 8, "packet_cycles": 4, "saw": 18}
    if dict(category_counts) != expected_categories:
        errors.append(f"retained profile families are {dict(category_counts)}, expected {expected_categories}")
    for profile in unique_profiles:
        scenarios = {s["scenarioId"] for s in signals if s["profileId"] == profile}
        if scenarios.intersection(profile_scenarios) != set(profile_scenarios):
            errors.append(f"{profile}: missing a retained profile scenario")
    all_profiles = {s["profileId"] for s in signals}
    for profile in all_profiles:
        if profile.startswith("packet_clock_"):
            errors.append(f"{profile}: packet clock-rate sweep should have been removed")
        if profile.startswith("packet_fixed_") and "_4period_" not in profile:
            errors.append(f"{profile}: fixed packet duration is shorter than four periods")
        if profile.startswith("packet_cycles_") and "_8cycle_" not in profile:
            errors.append(f"{profile}: packet carrier-cycle duration is shorter than eight cycles")
    if any(s["id"] == "legacy__trace_8changes_per_sec" for s in signals):
        errors.append("legacy trace below 16 changes per second should have been removed")

    return manifest, errors, scenario_counts, group_summary, baseline_metrics


def make_plots(pack_dir, manifest, baseline_metrics):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    warnings.filterwarnings("ignore", message="divide by zero encountered in log10", category=RuntimeWarning)

    add_local_packages(pack_dir)
    chosen_ids = [
        "legacy__trace_16changes_per_sec",
        "sine_slew2_a8_r8__trace_16changes_per_sec",
        "packet_fixed_gaussian_4period_reset_peak__trace_16changes_per_sec",
        "saw_q4_lp10khz__trace_16changes_per_sec",
    ]
    lookup = {s["id"]: s for s in manifest["signals"]}
    fig, axes = plt.subplots(4, 1, figsize=(13, 9), constrained_layout=True)
    for ax, signal_id in zip(axes, chosen_ids):
        signal = lookup[signal_id]
        samples = np.frombuffer(read_wave(pack_dir / signal["rawFile"], 3, 44100), dtype="<f4")[::2]
        start, end = int(0.20 * 44100), int(0.24 * 44100)
        t = np.arange(start, end) / 44100
        ax.plot(t, samples[start:end], linewidth=0.7)
        ax.set_title(signal_id)
        ax.set_ylabel("Raw")
        ax.grid(alpha=0.2)
    axes[-1].set_xlabel("Time (s)")
    fig.savefig(pack_dir / "baseline_waveforms.png", dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(4, 1, figsize=(13, 11), constrained_layout=True)
    spectrogram_cmap = plt.get_cmap("magma").copy()
    spectrogram_cmap.set_bad("black")
    for ax, signal_id in zip(axes, chosen_ids):
        signal = lookup[signal_id]
        samples = np.frombuffer(read_wave(pack_dir / signal["rawFile"], 3, 44100), dtype="<f4")[::2]
        ax.set_facecolor("black")
        ax.specgram(samples, NFFT=4096, Fs=44100, noverlap=3072, cmap=spectrogram_cmap, scale="dB", vmin=-105, vmax=-25)
        ax.set_ylim(0, 8500)
        ax.set_title(signal_id)
        ax.set_ylabel("Hz")
    axes[-1].set_xlabel("Time (s)")
    fig.savefig(pack_dir / "baseline_spectrograms.png", dpi=150)
    plt.close(fig)

    (pack_dir / "summary_measurements.json").write_text(json.dumps({
        "baselineSignals": baseline_metrics,
        "scenarioCounts": dict(Counter(s["scenarioId"] for s in manifest["signals"])),
        "signalCount": len(manifest["signals"]),
    }, indent=2), encoding="utf-8")
    with (pack_dir / "baseline_measurements.csv").open("w", encoding="utf-8", newline="") as stream:
        stream.write("id,model,scenario,raw_dc,raw_ac_rms,raw_peak,listening_gain,matched_ac_rms,matched_pcm_peak,frame_count\n")
        for row in baseline_metrics:
            stream.write(",".join(str(row[key]) for key in (
                "id", "model", "scenario", "rawDc", "rawAcRms", "rawPeak",
                "listeningGain", "matchedAcRms", "matchedPcmPeak", "frameCount"
            )) + "\n")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze_source_models.py <AudioPreviewSourceModels directory>")
    pack_dir = pathlib.Path(sys.argv[1]).resolve()
    add_local_packages(pack_dir)
    manifest, errors, scenario_counts, groups, baseline = validate(pack_dir)
    make_plots(pack_dir, manifest, baseline)
    report = [
        f"Signals: {len(manifest['signals'])}",
        f"Scenario counts: {dict(scenario_counts)}",
        f"Matched groups: {len(groups)}",
        f"Baseline measurements: {len(baseline)}",
        "Plots: baseline_waveforms.png, baseline_spectrograms.png",
        "Result: " + ("PASS" if not errors else "FAIL"),
    ]
    if errors:
        report.extend("ERROR: " + error for error in errors)
    (pack_dir / "validation_report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
