"""Fit frequencies without assuming an envelope, phase, or voice amplitude.

Each short comparison interior is projected onto the two predicted sine/cosine
pairs plus DC. Per-segment phases/gains are nuisance parameters, not synthesis
controls. This distinguishes frequency mapping from envelope/phase errors.
"""
import os
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("OMP_NUM_THREADS", "1")
from analyze_andrut import *
from scipy import optimize


def fit(reference_dir, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    ranks = json.loads((Path(__file__).parent / "andrut_array.json").read_text())["ranks"]
    results = {}
    for name, approximate_start in (("Selection", 6463), ("Insertion", 813), ("Gnome", 5521)):
        fs, x = read_wave(reference_dir / f"Andrut_{name}_Sort.wav")
        pairs = comparisons(ranks, name)
        ids = np.linspace(30, min(1500, len(pairs)-30), 250, dtype=int)
        local = np.arange(24, 123)
        t = local / fs
        chosen_pairs = pairs[ids]

        def cost(parameters, event_offset=0, return_details=False):
            step, base = parameters[:2]
            curve = parameters[2] if len(parameters) > 2 else 1.0
            freq = base + step*99*(chosen_pairs/99)**curve
            phase = 2*np.pi*freq[:,:,None]*t
            basis = np.stack((np.sin(phase[:,0]), np.cos(phase[:,0]),
                              np.sin(phase[:,1]), np.cos(phase[:,1]),
                              np.ones((len(ids), len(local)))), axis=-1)
            positions = approximate_start + event_offset*147 + ids[:,None]*147 + local
            target = x[positions]
            coeff = np.matmul(np.linalg.pinv(basis), target[:,:,None])
            estimate = np.matmul(basis, coeff)[:,:,0]
            residual = target-estimate
            score = np.sum(residual**2)/np.sum((target-target.mean(axis=1,keepdims=True))**2)
            return (score, freq, coeff, target, estimate) if return_details else score

        candidates=[]
        for off in range(-3,4):
            answer=optimize.differential_evolution(lambda p: cost(p,off), [(25,60),(0,350)], seed=57,
                                                 maxiter=40, popsize=7, polish=True, tol=.0001)
            candidates.append((float(answer.fun),off,answer.x.tolist()))
        candidates.sort()
        score,off,parameters=candidates[0]
        curved=optimize.minimize(lambda p:cost(p,off),parameters+[1.],method="Nelder-Mead",
                                options={"maxiter":300,"xatol":1e-7})
        detail=cost(curved.x,off,True)
        result={"linear_residual_fraction":score,"linear_parameters":{"step_hz":parameters[0],"base_hz":parameters[1]},
                "event_offset":off,"approximate_start_sample":approximate_start,
                "power_mapping_residual_fraction":float(curved.fun),
                "power_mapping_parameters":{"step_hz":float(curved.x[0]),"base_hz":float(curved.x[1]),"power":float(curved.x[2])},
                "candidates":candidates}
        results[name]=result
        fig,axs=plt.subplots(3,1,figsize=(12,8),constrained_layout=True)
        for ax,k in zip(axs,[20,80,180]):
            ax.plot(local,detail[3][k],label="Reference")
            ax.plot(local,detail[4][k],label="Best two-tone projection",ls="--")
            ax.set_title(f"{name}: comparison {ids[k]}, ranks {chosen_pairs[k].tolist()}")
            ax.legend()
        fig.savefig(output_dir/f"{name.lower()}_segment_fits.png",dpi=130)
        plt.close(fig)
        print(name,json.dumps(result),flush=True)
        (output_dir/"segment_fits.json").write_text(json.dumps(results,indent=2))


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray")
    a=p.parse_args()
    fit(a.references,a.output)
