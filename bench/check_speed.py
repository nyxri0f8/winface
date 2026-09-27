"""Phase 0: compare CPU vs GPU for recognition, and camera backends / resolutions for startup time."""
import time
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

MODELS = Path(__file__).resolve().parents[1] / "models"


def bench(provider, runs=30):
    so = ort.SessionOptions()
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    sess = ort.InferenceSession(str(MODELS / "buffalo_l" / "w600k_r50.onnx"), so, providers=[provider])
    name = sess.get_inputs()[0].name
    x = np.random.rand(1, 3, 112, 112).astype(np.float32)
    for _ in range(3):
        sess.run(None, {name: x})
    t = time.perf_counter()
    for _ in range(runs):
        sess.run(None, {name: x})
    print(f"ArcFace R50 on {provider:<22} {(time.perf_counter() - t) / runs * 1000:6.1f} ms")


bench("CPUExecutionProvider")
bench("DmlExecutionProvider")

for backend, bname in [(cv2.CAP_DSHOW, "DSHOW"), (cv2.CAP_MSMF, "MSMF")]:
    for w, h in [(640, 480), (1280, 720)]:
        t = time.perf_counter()
        cap = cv2.VideoCapture(0, backend)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
        ok, f = cap.read()
        first = (time.perf_counter() - t) * 1000
        t2 = time.perf_counter()
        n = 0
        while time.perf_counter() - t2 < 1.5:
            ok, f = cap.read()
            n += ok
        fps = n / (time.perf_counter() - t2)
        print(f"{bname:<5} req {w}x{h}: got {None if f is None else f.shape[1::-1]}, first frame {first:6.0f} ms, {fps:4.1f} fps")
        cap.release()
