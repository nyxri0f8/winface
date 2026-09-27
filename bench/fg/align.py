"""Align a face to the standard 112x112 ArcFace crop using 5 MediaPipe landmarks."""
import cv2
import numpy as np

# Standard ArcFace 5-point template (image-left eye, image-right eye, nose, mouth left, mouth right)
TEMPLATE = np.array(
    [[38.2946, 51.6963], [73.5318, 51.5014], [56.0252, 71.7366], [41.5493, 92.3655], [70.7299, 92.2041]],
    dtype=np.float32,
)
# MediaPipe 478-point indices: iris centres 468/473, nose tip 1, mouth corners 61/291
MP_5 = [468, 473, 1, 61, 291]


def five_points(pts):
    return pts[MP_5, :2].astype(np.float32)


def align(bgr, pts, size=112):
    m, _ = cv2.estimateAffinePartial2D(five_points(pts), TEMPLATE, method=cv2.LMEDS)
    return cv2.warpAffine(bgr, m, (size, size), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
