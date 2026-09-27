"""Simulate one lock-screen unlock from a COLD start, the way WinFace will run with no background process:
process starts -> camera + models load in parallel -> match -> release everything -> exit.

Prints a timeline and the CPU time used (battery cost).  Usage: python unlock_sim.py [threshold]
"""
import os
import sys
import threading
import time

T0 = time.perf_counter()
CPU0 = time.process_time()

import cv2  # noqa: E402
import numpy as np  # noqa: E402
import onnxruntime as ort  # noqa: E402
from pathlib import Path  # noqa: E402

THRESH = float(sys.argv[1]) if len(sys.argv) > 1 else 0.45
NEED, WINDOW = 3, 5          # 3 of the last 5 frames must match
TIMEOUT = 4.0
ROOT = Path(__file__).resolve().parents[1]
ms = lambda: (time.perf_counter() - T0) * 1000
events = []
mark = lambda s: events.append((ms(), s))
mark("process start + imports")

cam_box = {}


def open_cam():
    from fg.camera import Camera
    cam_box["cam"] = Camera()
    mark(f"camera first frame (open {cam_box['cam'].open_ms:.0f} ms)")


cam_thread = threading.Thread(target=open_cam)
cam_thread.start()

from fg.align import align  # noqa: E402
from fg.tracker import Tracker  # noqa: E402

tracker = Tracker()
mark("face tracker loaded")
so = ort.SessionOptions()
so.intra_op_num_threads = 4
so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
sess = ort.InferenceSession(str(ROOT / "models" / "arcface_int8.onnx"), so, providers=["CPUExecutionProvider"])
inp = sess.get_inputs()[0].name


def embed(c):
    x = cv2.cvtColor(c, cv2.COLOR_BGR2RGB).astype(np.float32)
    v = sess.run(None, {inp: ((x - 127.5) / 127.5).transpose(2, 0, 1)[None]})[0][0]
    return v / np.linalg.norm(v)


# templates must come from the same (int8) model that runs at unlock time; precomputed like the real product
cache = Path(__file__).parent / "data" / "templates_arcface_int8.npy"
if not cache.exists():
    np.save(cache, np.stack([embed(c) for c in np.load(cache.with_name("crops.npz"))["crops"]]))
    print("templates cached - run again for a real timing")
    sys.exit()
templates = np.load(cache)
mark(f"recognizer loaded + {len(templates)} templates")

cam_thread.join()
cam = cam_box["cam"]
hits, last_seq, frames, result = [], -1, 0, "TIMEOUT"
start = time.perf_counter()
while time.perf_counter() - start < TIMEOUT:
    frame, ts, seq = cam.read()
    if seq == last_seq:
        time.sleep(0.003)
        continue
    last_seq = seq
    frames += 1
    faces = tracker.process(frame, ts * 1000)
    if not faces:
        hits.append(False)
        continue
    f = max(faces, key=lambda f: f.bbox[2] - f.bbox[0])  # nearest (largest) face
    s = float(np.sort(templates @ embed(align(frame, f.pts)))[-3:].mean())
    hits.append(s >= THRESH)
    if frames == 1 or hits[-1]:
        mark(f"frame {frames}: score {s:.3f} {'MATCH' if hits[-1] else ''}")
    if sum(hits[-WINDOW:]) >= NEED:
        result = "UNLOCK"
        break
mark(f"decision: {result} after {frames} frames")
cam.close()
mark("camera released")
cpu = time.process_time() - CPU0
for t, s in events:
    print(f"{t:7.0f} ms  {s}")
print(f"CPU time used: {cpu:.2f} s (all cores) over {ms() / 1000:.2f} s wall  |  pid {os.getpid()} exiting now")
