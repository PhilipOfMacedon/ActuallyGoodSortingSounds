"""Render the frozen analytic model and audit it against all three recordings.

--synthesis-only reads the profile and rank fixture, never the reference WAVs.
The reference audio is used only for measurements and explicitly labelled A/Bs.
"""
from fit_note_model import *
import shutil
import heapq

NAMES=("Selection","Insertion","Gnome")
PREROLL=32


def fade_excerpt(samples,rate):
    samples=samples.copy()
    n=round(.005*rate)
    ramp=.5-.5*np.cos(np.linspace(0,np.pi,n))
    samples[:n]*=ramp;samples[-n:]*=ramp[::-1]
    return samples


def metrics(target,model):
    gain=float(np.dot(target,model)/np.dot(model,model))
    energy=np.sum(target*target)
    return {
        "compared_frames":len(target),
        "correlation":float(np.corrcoef(target,model)[0,1]),
        "fixed_gain_energy_explained":float(1-np.sum((target-model)**2)/energy),
        "gain_fitted_energy_explained":float(1-np.sum((target-gain*model)**2)/energy),
        "optional_gain_correction":gain,
        "reference_rms":float(np.sqrt(np.mean(target**2))),
        "model_rms":float(np.sqrt(np.mean(model**2))),
        "residual_rms":float(np.sqrt(np.mean((target-model)**2)))
    }


def export_wave(path,mono,rate,pcm=False):
    assert np.isfinite(mono).all()
    assert np.max(abs(mono))<1.,f"Clipping would occur in {path.name}"
    stereo=np.repeat(mono[:,None],2,axis=1)
    data=np.rint(stereo*32767).astype(np.int16) if pcm else stereo.astype(np.float32)
    wavfile.write(path,rate,data)
    actual_rate,actual=wavfile.read(path)
    assert actual_rate==rate and np.array_equal(actual,data),"WAV round trip changed samples"
    return {"file":path.name,"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "frames":len(data),"channels":2,"format":"pcm16" if pcm else "float32",
            "peak":float(np.max(abs(mono))),"seconds":len(mono)/rate}


def make_pack(reference_dir,output_dir,profile_path,synthesis_only=False):
    output_dir.mkdir(parents=True,exist_ok=True)
    profile=json.loads(profile_path.read_text())
    array_path=Path(__file__).parent/"andrut_array.json"
    fixture=json.loads(array_path.read_text())
    ranks=fixture["ranks"]
    assert sorted(ranks)==list(range(100))
    cfg=profile["configuration"]
    fs=profile["sample_rate"]
    filt=np.array(profile["output_filter"]["coefficients_causal"])
    alignment_delay=PREROLL+profile["output_filter"]["group_alignment_frames"]
    manifest={"profile":profile,"array":fixture,"signals":[],"methodology":{
        "training":"Selection only; common source parameters and output filter frozen before validation",
        "validation":"Insertion and Gnome; only comparator operand order and recording alignment differ",
        "gain":"One common gain embedded in the FIR. No per-recording gain correction in exported audio or primary score.",
        "scoring_interval":"Omit first and last 5000 synthesis frames; reject prior-scene audio and source/recording cut boundaries",
        "synthesis_preroll_frames":PREROLL,
        "filter_alignment_delay_frames":profile["output_filter"]["group_alignment_frames"]}}
    figure_data=[]
    for name in NAMES:
        pairing=profile["alignments"][name]
        pairs=comparisons(ranks,name)
        if pairing["swapped_operands"]:
            pairs=pairs[:,::-1]
        source=render_notes(pairs,preroll_frames=PREROLL,**cfg)
        output=signal.convolve(source,filt,mode="full",method="fft")
        stem=name.lower()+"_delayed_sine_bursts"
        entry={"algorithm":name,"comparisons":len(pairs),"active_seconds":len(pairs)/300,
            "operand_order":"reversed from research comparison pair" if pairing["swapped_operands"] else "research comparison pair",
            "comparison_values":pairs.tolist(),
            "event_frames_late_operand":[PREROLL+147*i for i in range(len(pairs))],
            "exports":[export_wave(output_dir/(stem+"_raw_f32.wav"),output,fs),
                       export_wave(output_dir/(stem+"_listen_pcm16.wav"),output,fs,True)]}
        active=[];maximum=0
        for i,pair in enumerate(pairs):
            for lane,rank in enumerate(pair):
                start=PREROLL+i*147+cfg["delays"][lane]
                frequency=cfg["base"]+cfg["step"]*rank
                end=start+round(cfg["duration"]*fs/frequency)+1
                while active and active[0]<=start:heapq.heappop(active)
                heapq.heappush(active,end);maximum=max(maximum,len(active))
        entry["maximum_active_note_count"]=maximum
        if not synthesis_only:
            refpath=reference_dir/f"Andrut_{name}_Sort.wav"
            rate,reference=read_wave(refpath)
            assert rate==fs
            offset=pairing["offset_samples"]-alignment_delay
            first=alignment_delay+5000
            last=min(len(pairs)*147-5000+alignment_delay,len(reference)-offset)
            target=reference[first+offset:last+offset]
            candidate=output[first:last]
            entry["reference_sha256"]=hashlib.sha256(refpath.read_bytes()).hexdigest()
            entry["reference_alignment_samples"]=offset
            entry["metrics"]=metrics(target,candidate)
            assert entry["metrics"]["fixed_gain_energy_explained"]>.95,(name,entry["metrics"])
            # Excerpts are aligned to the same exact comparison cursors. A is
            # always reference; B is synthesis; no gain matching per excerpt.
            ab=[];timestamps=[];cursor=0
            for fraction in (.15,.5,.8):
                start=alignment_delay+int(fraction*len(pairs)*147)
                end=start+fs
                ref=fade_excerpt(reference[start+offset:end+offset],fs)
                ours=fade_excerpt(output[start:end],fs)
                assert len(ref)==len(ours)==fs
                for label,part in (("A: reference",ref),("gap",np.zeros(round(.15*fs))),
                                   ("B: reconstruction",ours),("gap",np.zeros(round(.35*fs)))):
                    timestamps.append({"start_seconds":cursor/fs,"label":label,
                        "comparison_fraction":fraction})
                    ab.append(part);cursor+=len(part)
            abname=name.lower()+"_AB_reference_then_reconstruction.wav"
            entry["exports"].append(export_wave(output_dir/abname,np.concatenate(ab),fs,True))
            entry["AB_timeline"]=timestamps
            # Perform mechanism ablations with all other frozen parameters.
            # Optional scalar matching avoids confusing a quieter result with
            # a structurally worse result; no shape/filter parameters refit.
            ablations={"same_onset":dict(cfg,delays=[0,0]),
                "four_period_duration_cap":dict(cfg,duration_cap_seconds=4/300),
                "gaussian_four_period_cap":dict(cfg,window="gaussian",duration_cap_seconds=4/300)}
            entry["ablations"]={}
            for label,configuration in ablations.items():
                absource=render_notes(pairs,preroll_frames=PREROLL,**configuration)
                aboutput=signal.convolve(absource,filt,mode="full",method="fft")
                limit=min(last,len(aboutput))
                entry["ablations"][label]=metrics(reference[first+offset:limit+offset],aboutput[first:limit])
            # Repeat the first-pair uncertainty check without altering the rest.
            alternate=ranks.copy();alternate[0],alternate[1]=alternate[1],alternate[0]
            pp=comparisons(alternate,name)
            if pairing["swapped_operands"]:pp=pp[:,::-1]
            yy=signal.convolve(render_notes(pp,preroll_frames=PREROLL,**cfg),filt,mode="full",method="fft")
            alternate_offset=offset-147 if name=="Gnome" else offset
            limit=min(last,len(yy),len(reference)-alternate_offset)
            entry["reversed_initial_pair_metrics"]=metrics(reference[first+alternate_offset:limit+alternate_offset],yy[first:limit])
            figure_data.append((name,reference,output,offset,alignment_delay,len(pairs)))
            print(name,json.dumps(entry["metrics"]),flush=True)
        manifest["signals"].append(entry)
    shutil.copyfile(profile_path,output_dir/profile_path.name)
    shutil.copyfile(array_path,output_dir/array_path.name)
    (output_dir/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    if not synthesis_only:
        plots(figure_data,fs,output_dir)
    lines=["# Andrut reference reconstruction: delayed overlapping sine bursts","",
        "One common fitted source and one common 31-tap output response are used for all three algorithms. "
        "The oscillator frequencies remain continuous; there is no harmonic-bank source, separate humming bed, or chorus.","",
        "## Listen","","A/B clips alternate one second of reference (A), a 0.15-second gap, one second of reconstruction (B), "
        "and a 0.35-second gap. This repeats for early, middle, and late portions of each run. "
        "Both excerpt types receive identical 5 ms edge fades to prevent cut-boundary clicks. "
        "Output uses the common fitted level, with no per-clip normalization.",""]
    for entry in manifest["signals"]:
        name=entry["algorithm"]
        if not synthesis_only:
            lines.append(f"- **{name}: [A/B comparison]({name.lower()}_AB_reference_then_reconstruction.wav)**")
        lines.append(f"- {name}: [full reconstruction]({name.lower()}_delayed_sine_bursts_listen_pcm16.wav) "
                     f"({entry['comparisons']} comparisons, {entry['active_seconds']:.3f} seconds before release)")
    lines.extend(["","## Common source controls","",f"```json\n{json.dumps(cfg,indent=2)}\n```","",
        "Frequency is `base + step * rank`. Duration is measured in carrier cycles. "
        "The Tukey fraction controls short cosine edges. The first operand leads the second by about ten samples at 44.1 kHz. "
        "Every note captures its pitch and continues to completion while subsequent notes overlap. "
        "The 300 Hz line family comes from the event clock.",""])
    if not synthesis_only:
        lines.extend(["## Agreement with the recordings","",
            "Source and output filter were fitted using Selection Sort. Insertion and Gnome validate that same profile. "
            "The primary score includes no per-recording gain correction. Recording offsets and comparator operand order were identified separately.","",
            "| Algorithm | Correlation | Waveform energy explained |","|---|---:|---:|"])
        for entry in manifest["signals"]:
            m=entry["metrics"]
            lines.append(f"| {entry['algorithm']} | {m['correlation']:.5f} | {100*m['fixed_gain_energy_explained']:.2f}% |")
        lines.extend(["","The first pair may have been swapped before the supplied image was captured. "
            "Both orders are measured in the manifest; the remaining 98 positions are fixed. "
            "Correlation and energy explained are waveform metrics, not percentages of perceptual similarity or proof of the creator's implementation.","",
            "![Reference and reconstruction](spectrogram_comparison.png)","",
            "![Aligned waveforms](waveform_comparison.png)"])
    lines.extend(["","Raw float32 and PCM16 WAVs carry the same level. Every export is read back and checked. "
        "The manifest contains exact comparisons, event frames, alignment, parameters, metrics, hashes, and ablations.",""])
    (output_dir/"README.md").write_text("\n".join(lines))
    print(f"Pack ready: {output_dir}",flush=True)


def plots(data,fs,output):
    fig,axs=plt.subplots(3,2,figsize=(16,11),constrained_layout=True)
    waves,waxs=plt.subplots(3,1,figsize=(14,8),constrained_layout=True)
    for row,(name,reference,model,offset,delay,count) in enumerate(data):
        first=delay+5000;last=min(count*147-5000+delay,len(reference)-offset)
        for col,(label,part) in enumerate((("Reference",reference[first+offset:last+offset]),("Reconstruction",model[first:last]))):
            f,t,z=signal.stft(part,fs,nperseg=2048,noverlap=1792,boundary=None,padded=False)
            axs[row,col].pcolormesh(t,f,20*np.log10(np.maximum(abs(z),1e-7)),shading="auto",cmap="magma",vmin=-80,vmax=-20,rasterized=True)
            axs[row,col].set(title=f"{name}: {label}",ylim=(0,5000),xlabel="seconds",ylabel="Hz")
        start=delay+int(.7*count*147);length=round(.02*fs)
        time=np.arange(length)/fs*1000
        waxs[row].plot(time,reference[start+offset:start+offset+length],label="Reference",lw=.9)
        waxs[row].plot(time,model[start:start+length],label="Reconstruction",lw=.8,alpha=.8,ls="--")
        waxs[row].set(title=f"{name}: same event cursor, same waveform scale",xlabel="ms",ylabel="sample amplitude")
        waxs[row].legend()
    fig.savefig(output/"spectrogram_comparison.png",dpi=140);plt.close(fig)
    waves.savefig(output/"waveform_comparison.png",dpi=140);plt.close(waves)
    # A pure delay's cancellation prediction is independent of learned FIR taps.
    profile=json.loads((output/"andrut_delayed_sine_bursts_v1.json").read_text()) if (output/"andrut_delayed_sine_bursts_v1.json").exists() else None
    if profile:
        D=-profile["configuration"]["delays"][0]
        freq=np.linspace(0,18000,10000)
        response=np.maximum(abs(np.cos(np.pi*freq*D/fs)),1e-5)
        fig,ax=plt.subplots(figsize=(13,4),constrained_layout=True)
        ax.plot(freq,20*np.log10(response));ax.set(ylim=(-55,3),xlabel="Hz",ylabel="dB relative to DC",title="Cancellation from two identical signals separated by the fitted operand delay")
        for k in range(4):
            notch=(2*k+1)*fs/(2*D)
            ax.axvline(notch,color="gray",ls=":");ax.text(notch+80,-48,f"{notch:.0f} Hz",rotation=90)
        fig.savefig(output/"delay_cancellation_prediction.png",dpi=140);plt.close(fig)


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioPreviewAndrutReconstruction")
    p.add_argument("--profile",type=Path,default=Path(__file__).parent/"andrut_delayed_sine_bursts_v1.json")
    p.add_argument("--synthesis-only",action="store_true")
    a=p.parse_args();make_pack(a.references,a.output,a.profile,a.synthesis_only)
