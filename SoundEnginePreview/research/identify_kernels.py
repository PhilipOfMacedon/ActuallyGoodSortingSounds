"""Identify shared per-value note responses, then validate on other algorithms.

The unknown response is represented in 147-sample polyphase blocks. Each
comparison excites both values. This is a diagnostic linear system model, not
the proposed implementation: a successful fit allows the recovered responses
to be replaced by a compact analytic generator/filter.
"""
import os
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"
from analyze_andrut import *


def event_matrix(pairs, periods):
    events = np.zeros((len(pairs), 100))
    for lane in range(2):
        np.add.at(events, (np.arange(len(pairs)), pairs[:, lane]), 1)
    matrix = np.zeros((len(pairs), 100*periods))
    for d in range(periods):
        matrix[d:, d*100:(d+1)*100] = events[:len(events)-d]
    return matrix


def identify(reference_dir, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    ranks=json.loads((Path(__file__).parent/"andrut_array.json").read_text())["ranks"]
    recordings={}
    for name,offset in (("Selection",6463),("Insertion",813),("Gnome",5521)):
        fs,x=read_wave(reference_dir/f"Andrut_{name}_Sort.wav")
        recordings[name]=(x,comparisons(ranks,name),offset)
    results=[]
    for periods in (1,2,4,8):
        x,pairs,start=recordings["Selection"]
        A=event_matrix(pairs,periods)
        ids=np.arange(30,min(len(pairs)-30,(len(x)-start)//147-30))
        Y=x[start+ids[:,None]*147+np.arange(147)]
        train=A[ids]
        gram=train.T@train
        ridge=.0001*np.trace(gram)/len(gram)
        H=np.linalg.solve(gram+ridge*np.eye(len(gram)),train.T@Y)
        np.save(output_dir/f"identified_note_kernels_{periods}period.npy",H.reshape(periods,100,147).transpose(1,0,2).reshape(100,-1))
        item={"periods":periods,"ridge":ridge,"recordings":{}}
        for name,(xx,pp,approx) in recordings.items():
            pred=(event_matrix(pp,periods)@H).ravel()
            # Align only the first third of the recording, leaving later sound
            # as a separate temporal validation region.
            n=min(len(pred)//3,fs*5)
            ref=xx[:min(len(xx),n+approx+3000)]
            corr=signal.correlate(ref,pred[:n],mode="full",method="fft")
            lags=signal.correlation_lags(len(ref),n)
            keep=np.flatnonzero(abs(lags-approx)<2205)
            best=keep[np.argmax(corr[keep])]
            offset=int(lags[best])
            first=max(147*30,-offset)
            last=min(len(pred)-147*30,len(xx)-offset)
            target=xx[offset+first:offset+last];model=pred[first:last]
            gain=float(np.dot(target,model)/np.dot(model,model))
            residual=target-gain*model
            segments={}
            for label,sl in (("early",slice(None,len(target)//3)),("late",slice(2*len(target)//3,None))):
                segments[label]=float(np.sum(residual[sl]**2)/np.sum(target[sl]**2))
            item["recordings"][name]={"offset_samples":offset,"gain":gain,
                "correlation":float(np.corrcoef(target,model)[0,1]),
                "residual_energy_fraction":float(np.sum(residual**2)/np.sum(target**2)),**segments}
        print(json.dumps(item),flush=True)
        results.append(item)
        (output_dir/"identified_kernel_results.json").write_text(json.dumps(results,indent=2))
    H=H.reshape(periods,100,147).transpose(1,0,2).reshape(100,-1)
    fig,axs=plt.subplots(4,2,figsize=(14,11),constrained_layout=True)
    for rank,ax in zip((0,2,6,12,20,40,60,99),axs.ravel()):
        ax.plot(np.arange(H.shape[1])/fs*1000,H[rank],lw=.7)
        ax.set(title=f"Rank {rank}: identified shared note response",xlabel="ms")
    fig.savefig(output_dir/"identified_note_kernels.png",dpi=140)
    plt.close(fig)


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--references",type=Path,default=ROOT/"x64/Release/Andrut's Reference")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray")
    a=p.parse_args()
    identify(a.references,a.output)
