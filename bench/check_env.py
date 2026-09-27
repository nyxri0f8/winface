"""Phase 0 check: models load and run on the GPU (DirectML), camera opens."""
import time
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort
import mediapipe as mp

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "models"


def bench_onnx(path, shape, runs=20):
    sess = ort.InferenceSession(str(path), providers=["DmlExecutionProvider", "CPUExecutionProvider"])
    inp = sess.get_inputs()[0]
    x = np.random.rand(*shape).astype(np.float32)
    sess.run(None, {inp.name: x})  # warm-up
    t = time.perf_counter()
    for _ in range(runs):
        sess.run(None, {inp.name: x})
    ms = (time.perf_counter() - t) / runs * 1000
    print(f"  {path.name:<18} provider={sess.get_providers()[0]:<22} {ms:6.1f} ms/run")


print("ONNX Runtime", ort.__version__, "providers:", ort.get_available_providers())
bench_onnx(MODELS / "buffalo_l" / "w600k_r50.onnx", (1, 3, 112, 112))
bench_onnx(MODELS / "buffalo_l" / "det_10g.onnx", (1, 3, 640, 640))

print("MediaPipe", mp.__version__)
opts = mp.tasks.vision.FaceLandmarkerOptions(
    base_options=mp.tasks.BaseOptions(model_asset_path=str(MODELS / "face_landmarker.task")),
    output_face_blendshapes=True,
    num_faces=2,
)
landmarker = mp.tasks.vision.FaceLandmarker.create_from_options(opts)

cap = cv2.VideoCapture(0, cv2.CAP_MSMF)
t = time.perf_counter()
ok, frame = cap.read()
print(f"Camera open+first frame: {ok}, {(time.perf_counter() - t) * 1000:.0f} ms, shape={None if frame is None else frame.shape}")
if ok:
    for _ in range(10):
        ok, frame = cap.read()
    img = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
    t = time.perf_counter()
    res = landmarker.detect(img)
    ms = (time.perf_counter() - t) * 1000
    n = len(res.face_landmarks)
    print(f"  FaceLandmarker: {n} face(s), {len(res.face_landmarks[0]) if n else 0} points, {ms:.1f} ms")
cap.release()
