"""Recover visible bar ranks from a contiguous staircase silhouette."""
from analyze_andrut import *
from PIL import Image
import csv


def extract(path,output):
    rgb=np.array(Image.open(path).convert("RGB"))
    white=np.all(rgb>=245,axis=2)
    assert np.all(white.any(axis=0)),"Expected one white silhouette in every column"
    top=white.argmax(axis=0)
    # The resized screenshot varies by one pixel near some bar corners.
    boundaries=np.r_[0,np.flatnonzero(abs(np.diff(top))>=2)+1,rgb.shape[1]]
    heights=np.array([rgb.shape[0]-int(np.median(top[a:b])) for a,b in zip(boundaries[:-1],boundaries[1:])])
    assert len(set(heights))==len(heights),"Tied raster heights require manual review"
    ranks=np.argsort(np.argsort(heights))
    fixture=json.loads((Path(__file__).parent/"andrut_array.json").read_text())
    assert ranks.tolist()==fixture["ranks"],"Image order differs from the saved fixture"
    assert hashlib.sha256(path.read_bytes()).hexdigest()==fixture["source_image_sha256"]
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("w",newline="") as f:
        writer=csv.writer(f);writer.writerow(["position_1based","x_first","x_last","height_pixels","rank_0based"])
        for i,(a,b,h,r) in enumerate(zip(boundaries[:-1],boundaries[1:],heights,ranks)):
            writer.writerow([i+1,int(a),int(b-1),int(h),int(r)])
    print(f"Recovered {len(ranks)} distinct bars; all ranks match the fixture. First pair's original pre-frame order remains uncertain.")


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--image",type=Path,default=ROOT/"x64/Release/Andrut's Reference/video_small_array.png")
    p.add_argument("--output",type=Path,default=ROOT/"x64/Release/AudioAnalysis/AndrutKnownArray/extracted_array.csv")
    a=p.parse_args();extract(a.image,a.output)
