"""Refine a single common profile on Selection; validate on the two other files."""
from fit_note_model import *
import time


def refine(reference_dir,output_dir,wide=False):
    ranks=json.loads((Path(__file__).parent/"andrut_array.json").read_text())["ranks"]
    fs,x=read_wave(reference_dir/"Andrut_Selection_Sort.wav")
    pairs=comparisons(ranks,"Selection")
    evaluations=0;best=1.;begin=time.monotonic()
    def objective(p):
        nonlocal evaluations,best
        step,base,cycles,alpha,delay=p
        cfg=dict(step=step,base=base,duration=cycles,window="tukey",tukey=alpha,delays=[-delay,0])
        y=render_notes(pairs,**cfg)
        coeff,error=fit_filter(y,x,6463,count=20000)
        evaluations+=1
        if error<best:
            best=error
            state=dict(configuration=cfg,residual=error,evaluations=evaluations,seconds=time.monotonic()-begin)
            (output_dir/"delayed_note_refinement_progress.json").write_text(json.dumps(state,indent=2))
        if evaluations%200==0:
            print(f"{evaluations} evaluations; best residual {best:.7f}; {time.monotonic()-begin:.1f} s",flush=True)
        return error
    bounds=[(39.8,40.2),(90,140) if wide else (98,102),(8,12) if wide else (8.3,9.6),(0,.4),(9.5,10.5)]
    initial=[40.0346,102,9.3576,.0386,10.0319] if wide else [40,100,8.83,0,10]
    answer=optimize.differential_evolution(objective,bounds,x0=initial,seed=57,popsize=8,maxiter=45,polish=False,tol=1e-5)
    step,base,cycles,alpha,delay=answer.x
    cfg=dict(step=float(step),base=float(base),duration=float(cycles),window="tukey",tukey=float(alpha),delays=[float(-delay),0])
    y=render_notes(pairs,**cfg)
    coeff,error=fit_filter(y,x,6463,count=80000)
    result={"configuration":cfg,"training_residual":error,"filter":coeff.tolist(),"validation":{}}
    for name,swap,offset in (("Selection",False,6463),("Insertion",False,959),("Gnome",True,5532)):
        fs,ref=read_wave(reference_dir/f"Andrut_{name}_Sort.wav")
        pp=comparisons(ranks,name)
        if swap:pp=pp[:,::-1]
        error,correction,gain=evaluate(render_notes(pp,**cfg),ref,offset,coeff)
        result["validation"][name]={"residual_fraction":error,"offset_samples":offset+correction,"gain":gain,
                                    "swapped_operands":swap}
    filename="refined_delayed_note_model_wide.json" if wide else "refined_delayed_note_model.json"
    (output_dir/filename).write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray")
    p.add_argument("--wide",action="store_true")
    a=p.parse_args();refine(a.references,a.output,a.wide)
