"""Fit a small analytic note model; retain other recordings as validation."""
from fit_note_model import *
import time


def optimize_model(reference_dir, output_dir):
    output_dir.mkdir(parents=True,exist_ok=True)
    ranks=json.loads((Path(__file__).parent/"andrut_array.json").read_text())["ranks"]
    fs,x=read_wave(reference_dir/"Andrut_Selection_Sort.wav")
    pairs=comparisons(ranks,"Selection")[:2000]
    count=0;best=1.;begin=time.monotonic()

    def objective(v):
        nonlocal count,best
        step,base,duration,power,alpha=v
        model=render_notes(pairs,step=step,base=base,duration=duration,mode="scaled",window="tukey",power=power,tukey=alpha)
        coeff,error=fit_filter(model,x,6463,taps=31,count=16000)
        count+=1
        if error<best:
            best=error
            state={"residual":error,"parameters":v.tolist(),"evaluations":count,"seconds":time.monotonic()-begin}
            (output_dir/"note_optimization_progress.json").write_text(json.dumps(state,indent=2))
        if count%150==0:
            print(f"{count} evaluations; best residual {best:.6f}; {time.monotonic()-begin:.1f} s",flush=True)
        return error

    bounds=[(38,43),(70,170),(4,16),(.5,1.5),(0,.7)]
    answer=optimize.differential_evolution(objective,bounds,seed=57,popsize=9,maxiter=55,tol=.0001,polish=False)
    parameters=answer.x
    step,base,duration,power,alpha=parameters
    config=dict(step=step,base=base,duration=duration,mode="scaled",window="tukey",power=power,tukey=alpha)
    model=render_notes(comparisons(ranks,"Selection"),**config)
    coeff,error=fit_filter(model,x,6463,taps=31)
    result={"configuration":config,"training_residual":error,"filter":coeff.tolist(),"validation":{}}
    for name,offset in (("Selection",6463),("Insertion",666),("Gnome",5521)):
        fs,ref=read_wave(reference_dir/f"Andrut_{name}_Sort.wav")
        err,correction,gain=evaluate(render_notes(comparisons(ranks,name),**config),ref,offset,coeff)
        result["validation"][name]={"residual_fraction":err,"alignment_correction_samples":correction,"gain":gain}
    (output_dir/"optimized_note_model.json").write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray")
    a=p.parse_args();optimize_model(a.references,a.output)
