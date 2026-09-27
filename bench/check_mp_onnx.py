"""Compare the ONNX re-implementation against real MediaPipe on LFW photos (no camera needed).
Each photo is placed on a 1280x720 grey canvas at a webcam-like size, like a real frame."""
import time
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
import pyarrow.parquet as pq

from fg.mp_onnx import FaceMesh, pose
from fg.tracker import MODEL

ROOT = Path(__file__).resolve().parents[1]
vision = mp.tasks.vision
ref = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
    base_options=mp.tasks.BaseOptions(model_asset_path=str(MODEL)), num_faces=1,
    output_facial_transformation_matrixes=True))

imgs = pq.read_table(ROOT / "datasets" / "lfw_train.parquet").column("image").to_pylist()[:3000:15]
errs, yaws, pitches, ms, miss = [], [], [], [], 0
for im in imgs:
    face = cv2.imdecode(np.frombuffer(im["bytes"], np.uint8), cv2.IMREAD_COLOR)
    face = cv2.resize(face, (500, 500))
    frame = np.full((720, 1280, 3), 110, np.uint8)
    frame[110:610, 390:890] = face
    r = ref.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)))
    fm = FaceMesh()  # fresh = detector path, like the first frame on the lock screen
    t = time.perf_counter()
    pts, _ = fm.process(frame)
    pts2, _ = fm.process(frame)  # second call = tracking path
    ms.append((time.perf_counter() - t) * 1000 / 2)
    if not r.face_landmarks or pts2 is None:
        miss += bool(r.face_landmarks) != (pts2 is not None)
        continue
    rp = np.array([(p.x * 1280, p.y * 720) for p in r.face_landmarks[0]])
    w = rp[:, 0].max() - rp[:, 0].min()
    errs.append(np.median(np.linalg.norm(pts2[:, :2] - rp, axis=1)) / w)
    R = np.asarray(r.facial_transformation_matrixes[0])[:3, :3]
    my, mp_, _ = pose(pts2)
    yaws.append((my, np.degrees(np.arcsin(-np.clip(R[2, 0], -1, 1)))))
    pitches.append((mp_, np.degrees(np.arctan2(R[2, 1], R[2, 2]))))

dy, dp = np.array(yaws), np.array(pitches)
print(f"images {len(imgs)}, compared {len(errs)}, detection disagreements {miss}")
print(f"landmark error vs MediaPipe: median {np.median(errs) * 100:.2f}% of face width, p95 {np.percentile(errs, 95) * 100:.2f}%")
for n, d in (("yaw", dy), ("pitch", dp)):
    print(f"{n:5} mine vs mp: corr {np.corrcoef(d.T)[0, 1]:+.3f}, mean |diff| {np.abs(d[:, 0] - d[:, 1]).mean():.1f} deg, "
          f"ranges mine {d[:, 0].min():+.0f}..{d[:, 0].max():+.0f}  mp {d[:, 1].min():+.0f}..{d[:, 1].max():+.0f}")
print(f"my pipeline: {np.median(ms):.1f} ms/frame (Python)")
