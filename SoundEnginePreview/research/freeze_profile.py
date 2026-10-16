"""Freeze numerical fit results into a standalone reproducible sound profile."""
from analyze_andrut import *


def freeze(fit_path,profile_path):
    fitted=json.loads(fit_path.read_text())
    profile={
        "version":1,
        "id":"andrut_delayed_sine_bursts_v1",
        "description":"Effective reconstruction fitted on Selection Sort; Insertion and Gnome are validation recordings. Not a claim of the creator's original implementation.",
        "sample_rate":44100,
        "comparison_clock_hz":300,
        "configuration":fitted["configuration"],
        "source_semantics":{
            "duration_unit":"carrier cycles",
            "phase":"reset at each note's birth; captured frequency persists for the whole note",
            "window":"Tukey with short cosine edges",
            "overlap":"sum all active notes, including notes from earlier comparisons",
            "independent_bed":False,
            "chorus_mix":0,
            "duration_clamped_to_clock_periods":False,
            "frequency_formula":"base + step * rank; equivalently a continuous linear mapping of normalized values"
        },
        "output_filter":{
            "purpose":"One common short FIR effective response learned from Selection, including output gain",
            "coefficients_causal":list(reversed(fitted["filter"])),
            "group_alignment_frames":len(fitted["filter"])//2
        },
        "alignments":fitted["validation"],
        "fitting_recording":"Andrut_Selection_Sort.wav",
        "validation_recordings":["Andrut_Insertion_Sort.wav","Andrut_Gnome_Sort.wav"],
        "notes":[
            "Alignment and comparator operand order were identified separately for each recording.",
            "The first two initial values may be reversed; the remaining 98 values are confirmed.",
            "Fine decimal values describe this effective fit and are not uniquely identified physical parameters."
        ]
    }
    profile_path.write_text(json.dumps(profile,indent=2)+"\n")
    print(profile_path)


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--fit",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray/refined_delayed_note_model_wide.json")
    p.add_argument("--profile",type=Path,default=Path(__file__).parent/"andrut_delayed_sine_bursts_v1.json")
    a=p.parse_args();freeze(a.fit,a.profile)
