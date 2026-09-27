"""Export test vectors for the C++ engine self-test + the enrolled templates in a C++-readable file.

testvec/frame_NN.bgr   raw 1280x720x3 BGR (LFW photo on a grey canvas)
testvec/expect_NN.bin  float32: 478*3 landmarks, yaw, pitch, roll, texture, 512 embedding
data/templates.bin     uint32 count, then per profile: uint32 name_len, name, uint32 n, n*512 float32
"""
from pathlib import Path

import cv2
import numpy as np
import pyarrow.parquet as pq

from fg.liveness import Liveness
from fg.mp_onnx import FaceMesh, pose
from fg.recog import Embedder

HERE = Path(__file__).parent
ROOT = HERE.parent
OUT = ROOT / "testvec"
OUT.mkdir(exist_ok=True)

emb, live = Embedder("arcface_int8"), Liveness()
imgs = pq.read_table(ROOT / "datasets" / "lfw_train.parquet").column("image").to_pylist()[5:4000:200]
n = 0
for im in imgs:
    face = cv2.resize(cv2.imdecode(np.frombuffer(im["bytes"], np.uint8), cv2.IMREAD_COLOR), (500, 500))
    frame = np.full((720, 1280, 3), 110, np.uint8)
    frame[110:610, 390:890] = face
    fm = FaceMesh()
    fm.process(frame)
    pts, _ = fm.process(frame)
    if pts is None:
        continue
    # same 5-point least-squares similarity as the C++ engine
    src = pts[[468, 473, 1, 61, 291], :2].astype(np.float64)
    dst = np.array([[38.2946, 51.6963], [73.5318, 51.5014], [56.0252, 71.7366], [41.5493, 92.3655], [70.7299, 92.2041]])
    sm, dm = src.mean(0), dst.mean(0)
    p, q = src - sm, dst - dm
    a = (p * q).sum() / (p ** 2).sum()
    b = (p[:, 0] * q[:, 1] - p[:, 1] * q[:, 0]).sum() / (p ** 2).sum()
    M = np.array([[a, -b, dm[0] - (a * sm[0] - b * sm[1])], [b, a, dm[1] - (b * sm[0] + a * sm[1])]])
    crop = cv2.warpAffine(frame, M, (112, 112), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    x0, y0 = pts[:, :2].min(0).astype(int)
    x1, y1 = pts[:, :2].max(0).astype(int)
    tex = live.texture(frame, (x0, y0, x1, y1))
    y, pi, r = pose(pts)
    frame.tofile(OUT / f"frame_{n:02d}.bgr")
    np.concatenate([pts.ravel(), [y, pi, r, tex], emb.embed(crop)]).astype(np.float32).tofile(OUT / f"expect_{n:02d}.bin")
    n += 1
print(f"wrote {n} test vectors to {OUT}")

with open(HERE / "data" / "templates.bin", "wb") as f:
    profiles = [p for p in sorted((HERE / "data").glob("*.npz"))]
    f.write(np.uint32(len(profiles)).tobytes())
    for p in profiles:
        t = np.load(p)["arcface_int8"].astype(np.float32)
        name = p.stem.encode()
        f.write(np.uint32(len(name)).tobytes() + name + np.uint32(len(t)).tobytes() + t.tobytes())
        print(f"template profile '{p.stem}': {len(t)} embeddings")
