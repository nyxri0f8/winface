"""Impostor calibration: score 13k LFW stranger faces against each enrolled profile.
Gives the false-accept rate (FAR) per threshold, so the unlock threshold is set from data, not guesswork.
"""
import sys
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
import pyarrow.parquet as pq

from fg.align import align
from fg.recog import Embedder
from fg.tracker import MODEL as LANDMARK_MODEL

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(__file__).parent / "data"
CACHE = ROOT / "datasets" / "lfw_arcface_int8.npz"


def lfw_embeddings():
    if CACHE.exists():
        d = np.load(CACHE)
        return d["emb"], d["label"]
    vision = mp.tasks.vision
    lm = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(LANDMARK_MODEL)), num_faces=1))
    emb = Embedder("arcface_int8", threads=8)
    t = pq.read_table(ROOT / "datasets" / "lfw_train.parquet")
    labels, imgs = t.column("label").to_pylist(), t.column("image").to_pylist()
    out, lab = [], []
    for i, (l, im) in enumerate(zip(labels, imgs)):
        bgr = cv2.imdecode(np.frombuffer(im["bytes"], np.uint8), cv2.IMREAD_COLOR)
        h, w = bgr.shape[:2]
        res = lm.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)))
        if res.face_landmarks:
            pts = np.array([(p.x * w, p.y * h, 0) for p in res.face_landmarks[0]], np.float32)
            out.append(emb.embed(align(bgr, pts)))
            lab.append(l)
        if i % 2000 == 0:
            print(f"  {i}/{len(imgs)}", flush=True)
    emb_a, lab_a = np.stack(out), np.array(lab)
    np.savez_compressed(CACHE, emb=emb_a, label=lab_a)
    return emb_a, lab_a


def main():
    emb, lab = lfw_embeddings()
    print(f"LFW faces embedded: {len(emb)} ({len(set(lab))} people)")
    # sanity: same-person vs different-person scores inside LFW (model health check)
    rng = np.random.default_rng(0)
    same = [float(emb[i] @ emb[j]) for i, j in (rng.choice(np.where(lab == l)[0], 2, replace=False)
            for l in rng.choice([l for l in set(lab) if (lab == l).sum() > 1], 1500))]
    print(f"LFW same-person median {np.median(same):.3f} | ", end="")
    a, b = rng.integers(0, len(emb), (2, 20000))
    diff = (emb[a] * emb[b]).sum(1)[lab[a] != lab[b]]
    print(f"different-person median {np.median(diff):.3f}, p99.99 {np.percentile(diff, 99.99):.3f}")

    for p in sorted(DATA.glob("*.npz")):
        tmpl = np.load(p)["arcface_int8"]
        sims = np.sort(emb @ tmpl.T, axis=1)[:, -3:].mean(1)  # same scoring as unlock: mean of top-3
        print(f"\nProfile '{p.stem}' vs {len(sims)} strangers: max {sims.max():.3f}, p99.9 {np.percentile(sims, 99.9):.3f}, "
              f"mean {sims.mean():.3f}, std {sims.std():.3f}")
        mu, sd = sims.mean(), sims.std()
        for thr in (0.30, 0.35, 0.40, 0.45, 0.50, 0.55):
            far = (sims >= thr).mean()
            z = (thr - mu) / sd
            print(f"  threshold {thr:.2f}: strangers accepted {int((sims >= thr).sum()):5d} ({far:.2e})   "
                  f"normal-tail estimate {0.5 * __import__('math').erfc(z / 2 ** 0.5):.1e}")


if __name__ == "__main__":
    sys.exit(main())
