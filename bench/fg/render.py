"""Neon wireframe face mesh + Face ID-style tick ring, drawn with OpenCV (bench preview)."""
import math

import cv2
import mediapipe as mp
import numpy as np

_C = mp.tasks.vision.FaceLandmarksConnections
_idx = lambda conns: np.array([(c.start, c.end) for c in conns], dtype=np.int32)
TESS = _idx(_C.FACE_LANDMARKS_TESSELATION)
CONTOURS = _idx(_C.FACE_LANDMARKS_CONTOURS)
IRIS = _idx(list(_C.FACE_LANDMARKS_LEFT_IRIS) + list(_C.FACE_LANDMARKS_RIGHT_IRIS))

# BGR colours per state
COLORS = {
    "idle": (255, 200, 60),      # cyan-blue
    "scan": (255, 230, 90),      # bright cyan
    "ok": (120, 255, 90),        # green
    "fail": (70, 70, 255),       # red
}
SHIFT = 4                        # sub-pixel precision for anti-aliased lines
N_TICKS = 36


def _segs(pts, idx):
    p = np.round(pts[:, :2] * (1 << SHIFT)).astype(np.int32)
    return list(p[idx].reshape(-1, 2, 1, 2))


def draw_mesh(layer, pts, color, t, bbox):
    """Mesh on a black layer. A bright sweep band travels down the face."""
    dim = tuple(int(c * 0.45) for c in color)
    cv2.polylines(layer, _segs(pts, TESS), False, dim, 1, cv2.LINE_AA, SHIFT)
    cv2.polylines(layer, _segs(pts, CONTOURS), False, color, 1, cv2.LINE_AA, SHIFT)
    cv2.polylines(layer, _segs(pts, IRIS), True, (255, 255, 255), 1, cv2.LINE_AA, SHIFT)

    # scanning sweep: re-draw the mesh in white inside a moving horizontal band (face region only)
    H, W = layer.shape[:2]
    x0, y0, x1, y1 = max(bbox[0] - 2, 0), max(bbox[1] - 2, 0), min(bbox[2] + 3, W), min(bbox[3] + 3, H)
    if x1 <= x0 or y1 <= y0:
        return
    h = y1 - y0
    band_y = (t * 0.9 % 1.0) * h * 1.3 - 0.15 * h
    sweep = np.zeros((h, x1 - x0, 3), np.uint8)
    cv2.polylines(sweep, _segs(pts - np.float32([x0, y0, 0]), TESS), False, (255, 255, 255), 1, cv2.LINE_AA, SHIFT)
    ys = np.arange(h, dtype=np.float32)
    mask = np.exp(-((ys - band_y) / (0.06 * h)) ** 2)[:, None, None]
    roi = layer[y0:y1, x0:x1]
    np.maximum(roi, (sweep * mask).astype(np.uint8), out=roi)


def draw_ring(layer, center, radius, lit, color, t):
    """36 ticks around the face; `lit` is a bool array (Face ID enrolment style)."""
    cx, cy = center
    for i in range(N_TICKS):
        a = i / N_TICKS * 2 * math.pi - math.pi / 2
        on = lit[i]
        r0 = radius
        r1 = radius + (radius * 0.16 if on else radius * 0.09)
        c = color if on else (90, 90, 90)
        p0 = (int(cx + r0 * math.cos(a)), int(cy + r0 * math.sin(a)))
        p1 = (int(cx + r1 * math.cos(a)), int(cy + r1 * math.sin(a)))
        cv2.line(layer, p0, p1, c, 3 if on else 2, cv2.LINE_AA)
    # spinning arc while scanning
    start = (t * 240) % 360
    cv2.ellipse(layer, (int(cx), int(cy)), (int(radius * 0.93),) * 2, 0, start, start + 50, color, 2, cv2.LINE_AA)


def compose(frame, layer, dim=0.22):
    """Dim the camera image and add the glowing layer on top."""
    h, w = layer.shape[:2]
    small = cv2.resize(layer, (w // 2, h // 2), interpolation=cv2.INTER_AREA)
    glow = cv2.resize(cv2.GaussianBlur(small, (0, 0), 3.5), (w, h), interpolation=cv2.INTER_LINEAR)
    out = cv2.add(cv2.convertScaleAbs(frame, alpha=dim), cv2.convertScaleAbs(glow, alpha=1.6))
    return cv2.add(out, layer)


def hud(img, lines, org=(16, 28), color=(230, 230, 230)):
    x, y = org
    for s in lines:
        cv2.putText(img, s, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 3, cv2.LINE_AA)
        cv2.putText(img, s, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 1, cv2.LINE_AA)
        y += 24
