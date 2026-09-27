"""Extract MediaPipe's canonical 3D face model + Procrustes weights from the .binarypb (tiny protobuf reader).
Writes models/canonical_face.bin: 468 x (x,y,z) float32, then 468 float32 weights (0 = not used for pose).
"""
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "models" / "mp_task" / "geometry_pipeline_metadata_landmarks.binarypb"
OUT = ROOT / "models" / "canonical_face.bin"


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1
        r |= (c & 0x7F) << s; s += 7
        if c < 0x80:
            return r, i


def fields(b):
    i = 0
    while i < len(b):
        key, i = varint(b, i)
        fn, wt = key >> 3, key & 7
        if wt == 0:
            v, i = varint(b, i)
        elif wt == 1:
            v = b[i:i + 8]; i += 8
        elif wt == 2:
            n, i = varint(b, i); v = b[i:i + n]; i += n
        elif wt == 5:
            v = b[i:i + 4]; i += 4
        else:
            raise ValueError(wt)
        yield fn, wt, v


data = SRC.read_bytes()
import collections  # noqa: E402
print("top-level fields:", collections.Counter((fn, wt) for fn, wt, _ in fields(data)))
weights = {}
verts = []
for fn, wt, v in fields(data):
    if fn == 2:  # WeightedLandmarkRef
        lid, w = 0, 0.0
        for f2, w2, v2 in fields(v):
            if f2 == 1: lid = v2
            if f2 == 2: w = struct.unpack("<f", v2)[0]
        weights[lid] = w
    elif wt == 2 and fn != 2:  # canonical Mesh3d
        print(f"field {fn} subfields:", collections.Counter((f2, w2) for f2, w2, _ in fields(v)))
        for f2, w2, v2 in fields(v):
            if f2 == 3:
                if w2 == 2:
                    verts += list(struct.unpack(f"<{len(v2) // 4}f", v2))
                else:
                    verts.append(struct.unpack("<f", v2)[0])

vb = np.array(verts, np.float32).reshape(-1, 5)   # x, y, z, u, v
xyz = vb[:, :3]
w = np.zeros(len(xyz), np.float32)
for k, val in weights.items():
    w[k] = val
print(f"canonical vertices {len(xyz)}, procrustes landmarks {len(weights)}, weight sum {w.sum():.3f}")
OUT.write_bytes(xyz.tobytes() + w.tobytes())
print("wrote", OUT, OUT.stat().st_size, "bytes")
