"""Reference re-implementation of MediaPipe Face Landmarker on the converted ONNX models.
This is the exact algorithm the C++ engine ports; it is checked against real MediaPipe.

detector (BlazeFace short range, 128x128, letterboxed) -> rotated square ROI (scale 1.5)
-> landmark model (256x256) -> 478 points in image pixels -> head pose (weighted Procrustes vs canonical face)
Tracking: after the first detection the ROI comes from the previous frame's landmarks (no detector).
"""
import math
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

M = Path(__file__).resolve().parents[2] / "models"


def _anchors():
    """SSD anchors for face_detection_short_range: strides 8,16,16,16, 2 anchors per layer, fixed size."""
    out = []
    strides = [8, 16, 16, 16]
    i = 0
    while i < len(strides):
        s, n = strides[i], 0
        while i < len(strides) and strides[i] == s:
            n += 2
            i += 1
        g = 128 // s
        for y in range(g):
            for x in range(g):
                for _ in range(n):
                    out.append(((x + 0.5) / g, (y + 0.5) / g))
    return np.array(out, np.float32)  # (896, 2)


ANCHORS = _anchors()


def _session(name, threads=2):
    so = ort.SessionOptions()
    so.intra_op_num_threads = threads
    return ort.InferenceSession(str(M / name), so, providers=["CPUExecutionProvider"])


def _canonical():
    raw = np.fromfile(M / "canonical_face.bin", np.float32)
    xyz = raw[: 468 * 3].reshape(468, 3)
    w = raw[468 * 3:]
    return xyz, w


CANON, CANON_W = _canonical()


def rect_affine(cx, cy, size, angle, out):
    """Affine mapping image -> out x out crop of a square of `size` px centred at (cx,cy), rotated by `angle` rad."""
    c, s = math.cos(angle), math.sin(angle)
    k = out / size
    # dst = k * R(-angle) (p - c) + out/2
    return np.array([[k * c, k * s, out / 2 - k * (c * cx + s * cy)],
                     [-k * s, k * c, out / 2 - k * (-s * cx + c * cy)]], np.float64)


def _roi_from_points(p_a, p_b, x0, y0, x1, y1):
    """Rotation from the line p_a -> p_b (target angle 0), square long side, scaled 1.5."""
    angle = -math.atan2(-(p_b[1] - p_a[1]), p_b[0] - p_a[0])
    angle = (angle + math.pi) % (2 * math.pi) - math.pi
    w, h = x1 - x0, y1 - y0
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return cx, cy, max(w, h) * 1.5, angle


def pose(pts):
    """yaw/pitch/roll (deg) from weighted rigid fit of the canonical face to the landmarks (same formula as tracker.py)."""
    obs = np.stack([pts[:468, 0], -pts[:468, 1], -pts[:468, 2]], 1).astype(np.float64)
    src = CANON.astype(np.float64)
    w = CANON_W.astype(np.float64)
    ws = w / w.sum()
    ms, mo = (ws[:, None] * src).sum(0), (ws[:, None] * obs).sum(0)
    A, B = src - ms, obs - mo
    C = (B * ws[:, None]).T @ A
    U, _, Vt = np.linalg.svd(C)
    d = np.sign(np.linalg.det(U @ Vt))
    r = U @ np.diag([1, 1, d]) @ Vt
    pitch = math.degrees(math.atan2(r[2, 1], r[2, 2]))
    yaw = math.degrees(math.asin(-np.clip(r[2, 0], -1, 1)))
    roll = math.degrees(math.atan2(r[1, 0], r[0, 0]))
    return yaw, pitch, roll


class FaceMesh:
    def __init__(self):
        self.det = _session("face_detector.onnx")
        self.lmk = _session("face_landmarks_detector.onnx")
        self.roi = None  # tracking ROI from the previous frame

    def detect(self, bgr):
        h, w = bgr.shape[:2]
        s = 128 / max(w, h)
        nw, nh = round(w * s), round(h * s)
        px, py = (128 - nw) // 2, (128 - nh) // 2
        img = np.zeros((128, 128, 3), np.uint8)
        img[py:py + nh, px:px + nw] = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_AREA)
        x = (cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 127.5 - 1.0)[None]
        reg, cls = self.det.run(None, {"input": x})
        score = 1 / (1 + np.exp(-np.clip(cls[0, :, 0], -100, 100)))
        i = int(np.argmax(score))
        if score[i] < 0.5:
            return None
        r = reg[0, i] / 128.0
        ax, ay = ANCHORS[i]
        cx, cy, bw, bh = r[0] + ax, r[1] + ay, r[2], r[3]
        kp = np.array([(r[4 + 2 * k] + ax, r[5 + 2 * k] + ay) for k in range(6)])

        def unletter(xn, yn):  # 128-letterbox normalised -> image pixels
            return (xn * 128 - px) / s, (yn * 128 - py) / s
        x0, y0 = unletter(cx - bw / 2, cy - bh / 2)
        x1, y1 = unletter(cx + bw / 2, cy + bh / 2)
        e0, e1 = unletter(*kp[0]), unletter(*kp[1])
        return _roi_from_points(e0, e1, x0, y0, x1, y1), float(score[i])

    def landmarks(self, bgr, roi):
        cx, cy, size, angle = roi
        A = rect_affine(cx, cy, size, angle, 256)
        crop = cv2.warpAffine(bgr, A, (256, 256), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
        x = (cv2.cvtColor(crop, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0)[None]
        out = self.lmk.run(None, {self.lmk.get_inputs()[0].name: x})
        lm = out[0].reshape(478, 3).astype(np.float64)
        presence = 1 / (1 + math.exp(-float(np.ravel(out[1])[0])))
        Ai = cv2.invertAffineTransform(A)
        xy = lm[:, :2] @ Ai[:, :2].T + Ai[:, 2]
        z = lm[:, 2] * (size / 256)
        return np.column_stack([xy, z]).astype(np.float32), presence

    def process(self, bgr):
        """-> (pts (478,3) or None, presence)"""
        if self.roi is None:
            d = self.detect(bgr)
            if d is None:
                return None, 0.0
            self.roi = d[0]
        pts, presence = self.landmarks(bgr, self.roi)
        if presence < 0.5:
            self.roi = None
            return None, presence
        x0, y0 = pts[:, :2].min(0)
        x1, y1 = pts[:, :2].max(0)
        self.roi = _roi_from_points(pts[33], pts[263], x0, y0, x1, y1)
        return pts, presence
