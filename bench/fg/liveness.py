"""Liveness (anti-spoof) signals for an RGB webcam. Each is independent; the decision combines them.

1. texture  - MiniFASNet V2 + V1SE: is the face region a real face or a print / screen?
2. depth3d  - parallax test: a photo or screen is FLAT, so when it moves, all 478 landmarks move
              together by one planar homography. A real head turning breaks that (nose vs cheeks).
              Reported as non-planarity = homography residual / face width, only when there is enough motion.
3. blink    - a real blink (both eyes close then open) seen in the recent frames.
"""
from collections import deque
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

AS = Path(__file__).resolve().parents[2] / "models" / "antispoof"
FAS_MODELS = [("fas_v2_s2.7.onnx", 2.7), ("fas_v1se_s4.0.onnx", 4.0)]

# 3D-parallax uses a stable subset: face oval + nose ridge + eyes + mouth corners
_OVAL = [10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152,
         148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109]
_CENTER = [1, 4, 5, 6, 168, 197, 195, 2, 33, 133, 362, 263, 61, 291, 13, 14]
PARALLAX_IDX = np.array(_OVAL + _CENTER)


def _crop(frame, bbox, scale, size=80):
    """Same crop as the original Silent-Face repo: square-ish box scaled around the face, clamped to image."""
    H, W = frame.shape[:2]
    x0, y0, x1, y1 = bbox
    bw, bh = x1 - x0, y1 - y0
    scale = min((H - 1) / bh, (W - 1) / bw, scale)
    nw, nh = bw * scale, bh * scale
    cx, cy = x0 + bw / 2, y0 + bh / 2
    l, t, r, b = cx - nw / 2, cy - nh / 2, cx + nw / 2, cy + nh / 2
    if l < 0: r -= l; l = 0
    if t < 0: b -= t; t = 0
    if r > W - 1: l -= r - W + 1; r = W - 1
    if b > H - 1: t -= b - H + 1; b = H - 1
    return cv2.resize(frame[int(t):int(b) + 1, int(l):int(r) + 1], (size, size))


def nonplanarity(a, b, width):
    """Homography residual between two landmark sets (PARALLAX_IDX points), as a fraction of face width.
    ~0 for a flat photo/screen however it moves; grows when a real head rotates."""
    H, _ = cv2.findHomography(a, b, 0)
    if H is None:
        return None
    proj = cv2.perspectiveTransform(a[None], H)[0]
    return float(np.median(np.linalg.norm(proj - b, axis=1)) / width)


class Liveness:
    def __init__(self, history=12):
        so = ort.SessionOptions()
        so.intra_op_num_threads = 2
        self.fas = [(ort.InferenceSession(str(AS / f), so, providers=["CPUExecutionProvider"]), s) for f, s in FAS_MODELS]
        self.hist = deque(maxlen=history)   # (pts2d, face_width)
        self.blinks = deque(maxlen=45)      # ~1.5 s of eye-closure scores

    def texture(self, frame, bbox):
        """Probability the face is real (0..1), averaged over both models."""
        p = 0.0
        for sess, scale in self.fas:
            x = _crop(frame, bbox, scale).astype(np.float32).transpose(2, 0, 1)[None]  # raw BGR 0-255
            z = sess.run(None, {"input": x})[0][0]
            e = np.exp(z - z.max())
            p += (e / e.sum())[1]           # class 1 = real
        return float(p / len(self.fas))

    def depth3d(self):
        """(non_planarity, motion) between the oldest and newest frame in history, both as fraction of face width."""
        if len(self.hist) < self.hist.maxlen:
            return None, 0.0
        (a, w), (b, _) = self.hist[0], self.hist[-1]
        motion = float(np.median(np.linalg.norm(b - a, axis=1)) / w)
        return nonplanarity(a, b, w), motion

    def blinked(self):
        """A blink = eye-closure score rises well above this person's open-eye level, then drops back.
        Relative, because MediaPipe's absolute blink score on webcams often peaks well below 0.5."""
        s = np.array(self.blinks)
        if len(s) < 8:
            return False
        base = np.percentile(s, 20)          # open-eye level
        peak_i = int(np.argmax(s))
        rise = s[peak_i] - base
        after = s[peak_i:]
        return bool(rise > 0.25 and peak_i > 0 and len(after) > 1 and after.min() < base + rise * 0.4)

    def update(self, frame, face):
        x0, y0, x1, y1 = face.bbox
        w = max(x1 - x0, 1)
        self.hist.append((face.pts[PARALLAX_IDX, :2].astype(np.float32), float(w)))
        self.blinks.append((face.blend.get("eyeBlinkLeft", 0) + face.blend.get("eyeBlinkRight", 0)) / 2)
        np3d, motion = self.depth3d()
        return {"texture": self.texture(frame, face.bbox), "nonplanar": np3d, "motion": motion,
                "blink": self.blinked(), "blink_raw": self.blinks[-1]}

    def reset(self):
        self.hist.clear()
        self.blinks.clear()
