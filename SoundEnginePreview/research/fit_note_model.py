"""Compare analytic overlap-add note models using cross-algorithm validation."""
import os
os.environ["OPENBLAS_NUM_THREADS"]="1"
os.environ["OMP_NUM_THREADS"]="1"
from analyze_andrut import *
from scipy import optimize


def render_notes(pairs, rate=44100, step=40., base=100., duration=8., mode="cycles", window="rect", power=1., tukey=.0, delays=(0,0), phase_frames=(0,0), preroll_frames=0, clock_hz=300., duration_cap_seconds=None):
    frequencies=base+step*np.arange(100)
    kernels=[]
    quadrature=[]
    for frequency in frequencies:
        seconds=duration/frequency if mode=="cycles" else duration*.001
        if mode=="scaled":
            seconds=duration*.001*(1000/frequency)**power
        if duration_cap_seconds is not None:
            seconds=min(seconds,duration_cap_seconds)
        length=max(2,round(seconds*rate))
        t=np.arange(length)/rate
        envelope=np.ones(length)
        if window=="hann":
            envelope=np.hanning(length)
        elif window=="tukey":
            envelope=signal.windows.tukey(length,tukey)
        elif window=="exponential":
            envelope=np.exp(-np.arange(length)/max(1,length/5))
        elif window=="gaussian":
            q=(np.arange(length)/(length-1)-.5)*8
            envelope=(np.exp(-.5*q*q)-np.exp(-8))/(1-np.exp(-8))
        kernels.append(np.sin(2*np.pi*frequency*t)*envelope)
        quadrature.append(np.cos(2*np.pi*frequency*t)*envelope)
    lane_kernels=[]
    starts=[]
    for lane in range(pairs.shape[1]):
        start=int(np.floor(delays[lane]))
        fraction=delays[lane]-start
        starts.append(start)
        shapes=[]
        for rank,frequency in enumerate(frequencies):
            phase=2*np.pi*frequency*phase_frames[lane]/rate
            h=kernels[rank]*np.cos(phase)+quadrature[rank]*np.sin(phase)
            if fraction:
                h=np.r_[h*(1-fraction),0]+np.r_[0,h*fraction]
            shapes.append(h)
        lane_kernels.append(shapes)
    out=np.zeros(preroll_frames+int(np.ceil(len(pairs)*rate/clock_hz))+max(map(len,kernels))+int(max(0,max(delays)))+1)
    for k,pair in enumerate(pairs):
        for lane,rank in enumerate(pair):
            h=lane_kernels[lane][rank]
            start=preroll_frames+int(np.floor(k*rate/clock_hz))+starts[lane]
            trim=max(0,-start)
            start=max(0,start)
            out[start:start+len(h)-trim]+=h[trim:]
    return out


def fit_filter(model, reference, offset, taps=31, count=35000):
    shifts=np.arange(-(taps//2),taps//2+1)
    first=max(5000,-offset+100)
    last=min(len(model)-5000,len(reference)-offset-100)
    ids=np.linspace(first,last-1,min(count,last-first),dtype=int)
    B=model[ids[:,None]+shifts]
    target=reference[ids+offset]
    gram=B.T@B
    coeff=np.linalg.solve(gram+np.eye(len(gram))*1e-8*np.trace(gram)/len(gram),B.T@target)
    pred=B@coeff
    error=float(np.sum((pred-target)**2)/np.sum(target**2))
    return coeff,error


def evaluate(model, reference, offset, coeff):
    filtered=signal.convolve(model,coeff[::-1],mode="same",method="fft")
    # Shared filter, plus a small independent alignment correction per file.
    first=max(5000,-offset)
    last=min(len(filtered)-5000,len(reference)-offset-100)
    pred=filtered[first:last]
    candidates=[]
    for correction in range(-15,16):
        target=reference[first+offset+correction:last+offset+correction]
        gain=np.dot(pred,target)/np.dot(pred,pred)
        error=np.sum((pred*gain-target)**2)/np.sum(target**2)
        candidates.append((float(error),correction,float(gain)))
    return min(candidates)


def run(reference_dir,output_dir):
    output_dir.mkdir(parents=True,exist_ok=True)
    ranks=json.loads((Path(__file__).parent/"andrut_array.json").read_text())["ranks"]
    data={name:(*read_wave(reference_dir/f"Andrut_{name}_Sort.wav"),comparisons(ranks,name),offset)
          for name,offset in (("Selection",6463),("Insertion",666),("Gnome",5521))}
    fs,x,pairs,start=data["Selection"]
    configurations=[]
    for mode,durations in (("cycles",[1,2,4,6,8,10,12,16]),("ms",[2,3.333333,5,8,10,15,20])):
        for duration in durations:
            for window in ("rect","hann","exponential"):
                configurations.append(dict(duration=duration,mode=mode,window=window))
    results=[]
    for config in configurations:
        model=render_notes(pairs,**config)
        coeff,error=fit_filter(model,x,start)
        item={"configuration":config,"training_residual_fraction":error}
        results.append(item)
    results.sort(key=lambda x:x["training_residual_fraction"])
    for item in results[:12]:
        config=item["configuration"]
        model=render_notes(pairs,**config)
        coeff,error=fit_filter(model,x,start)
        item["validation"]={}
        for name,(fs,ref,pp,offset) in data.items():
            err,corr,gain=evaluate(render_notes(pp,**config),ref,offset,coeff)
            item["validation"][name]={"residual_fraction":err,"alignment_correction_samples":corr,"gain":gain}
        print(json.dumps(item),flush=True)
    (output_dir/"analytic_note_models.json").write_text(json.dumps(results,indent=2))


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray")
    a=p.parse_args();run(a.references,a.output)
