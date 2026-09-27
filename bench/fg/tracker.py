"""MediaPipe Face Landmarker wrapper: 478 3D points, 52 blendshapes, head pose."""
import math
from dataclasses import dataclass, field
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np

MODEL = Path(__file__).resolve().parents[2] / "models" / "face_landmarker.task"


@dataclass
class Face:
    pts: np.ndarray            # (478, 3) pixel x, y and relative depth z (pixels)
    blend: dict = field(default_factory=dict)
    yaw: float = 0.0           # degrees, + = turned to the camera's right
    pitch: float = 0.0         # degrees, + = looking up
    roll: float = 0.0

    @property
    def bbox(self):
        x0, y0 = self.pts[:, :2].min(0)
        x1, y1 = self.pts[:, :2].max(0)
        return int(x0), int(y0), int(x1), int(y1)


class Tracker:
    def __init__(self, max_faces=2):
        vision = mp.tasks.vision
        opts = vision.FaceLandmarkerOptions(
            base_options=mp.tasks.BaseOptions(model_asset_path=str(MODEL)),
            running_mode=vision.RunningMode.VIDEO,
            num_faces=max_faces,
            min_face_detection_confidence=0.6,
            min_face_presence_confidence=0.6,
            min_tracking_confidence=0.6,
            output_face_blendshapes=True,
            output_facial_transformation_matrixes=True,
        )
        self.lm = vision.FaceLandmarker.create_from_options(opts)
        self._last_ts = -1

    def process(self, bgr, ts_ms):
        ts_ms = max(int(ts_ms), self._last_ts + 1)  # VIDEO mode needs strictly increasing timestamps
        self._last_ts = ts_ms
        h, w = bgr.shape[:2]
        img = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
        res = self.lm.detect_for_video(img, ts_ms)
        faces = []
        for i, lms in enumerate(res.face_landmarks):
            pts = np.array([(p.x * w, p.y * h, p.z * w) for p in lms], dtype=np.float32)
            f = Face(pts=pts)
            if res.face_blendshapes:
                f.blend = {c.category_name: c.score for c in res.face_blendshapes[i]}
            if res.facial_transformation_matrixes:
                r = np.asarray(res.facial_transformation_matrixes[i])[:3, :3]
                f.pitch = math.degrees(math.atan2(r[2, 1], r[2, 2]))
                f.yaw = math.degrees(math.asin(-np.clip(r[2, 0], -1, 1)))
                f.roll = math.degrees(math.atan2(r[1, 0], r[0, 0]))
            faces.append(f)
        return faces
