"""Phase 1 bench: enrol faces, then verify live with recognition + liveness signals.

  python facetest.py enroll <name>          guided 5-pose capture (bench data, NOT encrypted)
  python facetest.py verify --label L       L = genuine | photo | screen | other   (logged to logs/L_<time>.csv)
"""
import argparse
import csv
import time
from pathlib import Path

import cv2
import numpy as np

from fg.align import align
from fg.camera import Camera
from fg.liveness import Liveness
from fg.recog import Embedder
from fg.render import COLORS, N_TICKS, compose, draw_mesh, draw_ring, hud
from fg.tracker import Tracker

DATA = Path(__file__).parent / "data"
LOGS = Path(__file__).parent / "logs"
MODEL = "arcface_int8"
POSES = [  # (instruction, yaw range, pitch range)
    ("Look straight at the camera", (-8, 8), (-8, 8)),
    ("Slowly turn your head LEFT", (-35, -12), (-15, 15)),
    ("Slowly turn your head RIGHT", (12, 35), (-15, 15)),
    ("Tilt your head UP a little", (-15, 15), (10, 30)),
    ("Tilt your head DOWN a little", (-15, 15), (-30, -10)),
]
PER_POSE = 15
MIN_SHARPNESS = 60.0
WIN = "WinFace - bench"


def sharpness(crop):
    return cv2.Laplacian(cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY), cv2.CV_64F).var()


def frames(cam):
    last = -1
    while True:
        frame, ts, seq = cam.read()
        if seq == last:
            time.sleep(0.002)
            continue
        last = seq
        yield cv2.flip(frame, 1), ts


def window():
    cv2.namedWindow(WIN, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WIN, 1280, 720)


def show(img):
    cv2.imshow(WIN, img)
    k = cv2.waitKey(1) & 0xFF
    return k in (ord("q"), 27) or cv2.getWindowProperty(WIN, cv2.WND_PROP_VISIBLE) < 1


def enroll(name):
    tracker, cam = Tracker(), Camera()
    window()
    crops, lit, pose_i, got = [], np.zeros(N_TICKS, bool), 0, 0
    for frame, ts in frames(cam):
        t = time.perf_counter()
        faces = tracker.process(frame, ts * 1000)
        layer = np.zeros_like(frame)
        msg, (yr, pr) = POSES[pose_i][0], POSES[pose_i][1:]
        if len(faces) == 1:
            f = faces[0]
            draw_mesh(layer, f.pts, COLORS["scan"], t, f.bbox)
            x0, y0, x1, y1 = f.bbox
            if yr[0] <= f.yaw <= yr[1] and pr[0] <= f.pitch <= pr[1]:
                crop = align(frame, f.pts)
                if sharpness(crop) >= MIN_SHARPNESS:
                    crops.append(crop)
                    got += 1
                    lit[: int(N_TICKS * (pose_i * PER_POSE + got) / (len(POSES) * PER_POSE))] = True
                else:
                    msg += "  (hold still - blurry)"
            draw_ring(layer, ((x0 + x1) / 2, (y0 + y1) / 2), max(x1 - x0, y1 - y0) * 0.72, lit, COLORS["scan"], t)
            msg += f"   yaw {f.yaw:+.0f} pitch {f.pitch:+.0f}"
        elif len(faces) > 1:
            msg = "Only ONE face in view please"
        else:
            msg = "No face found - sit in front of the camera, good light"
        if got >= PER_POSE:
            pose_i, got = pose_i + 1, 0
            if pose_i == len(POSES):
                break
        out = compose(frame, layer)
        hud(out, [f"Enrolling '{name}'  step {pose_i + 1}/{len(POSES)}  ({got}/{PER_POSE})", msg, "q = cancel"])
        if show(out):
            print("cancelled")
            cam.close()
            return
    cam.close()
    cv2.destroyAllWindows()
    crops = np.stack(crops)
    emb = Embedder(MODEL)
    DATA.mkdir(exist_ok=True)
    np.savez_compressed(DATA / f"{name}.npz", crops=crops, **{MODEL: np.stack([emb.embed(c) for c in crops])})
    print(f"saved {len(crops)} captures -> {DATA / (name + '.npz')}")


def load_profiles(emb):
    profiles = {}
    for p in sorted(DATA.glob("*.npz")):
        d = dict(np.load(p))
        if MODEL not in d:  # older enrolment: compute templates with the current model
            d[MODEL] = np.stack([emb.embed(c) for c in d["crops"]])
            np.savez_compressed(p, **d)
        profiles[p.stem] = d[MODEL]
    return profiles


def score(templates, v):
    """Mean of the 3 best similarities against the enrolled captures (robust to pose)."""
    return float(np.sort(templates @ v)[-3:].mean())


def verify(label):
    emb = Embedder(MODEL)
    profiles = load_profiles(emb)
    if not profiles:
        print("no profiles - run: python facetest.py enroll <name>")
        return
    tracker, live, cam = Tracker(), Liveness(), Camera()
    LOGS.mkdir(exist_ok=True)
    log_path = LOGS / f"{label}_{int(time.time())}.csv"
    fh = open(log_path, "w", newline="")
    log = csv.writer(fh)
    log.writerow(["t", "label", "profile", "score", "texture", "nonplanar", "motion", "blink", "yaw", "pitch", "sharp", "width"])
    print(f"profiles: {list(profiles)}   logging -> {log_path}")
    window()
    for frame, ts in frames(cam):
        t = time.perf_counter()
        faces = tracker.process(frame, ts * 1000)
        layer = np.zeros_like(frame)
        lines = [f"label: {label}   profiles: {', '.join(profiles)}"]
        if not faces:
            live.reset()
        for f in sorted(faces, key=lambda f: f.bbox[0] - f.bbox[2])[:1]:  # largest (nearest) face
            crop = align(frame, f.pts)
            v = emb.embed(crop)
            best = max(profiles, key=lambda n: score(profiles[n], v))
            s = score(profiles[best], v)
            L = live.update(frame, f)
            np3 = L["nonplanar"]
            ok = s > 0.45 and L["texture"] > 0.5
            draw_mesh(layer, f.pts, COLORS["ok"] if ok else COLORS["fail"], t, f.bbox)
            lines += [
                f"match   {best:<10} {s:.3f}",
                f"texture (real prob) {L['texture']:.3f}",
                f"3D      non-planar {'-' if np3 is None else f'{np3 * 100:.2f}%'}   motion {L['motion'] * 100:.1f}%",
                f"blink   {'YES' if L['blink'] else 'no'}",
                f"yaw {f.yaw:+.0f} pitch {f.pitch:+.0f}   faces {len(faces)}   {(time.perf_counter() - t) * 1000:.0f} ms/frame",
            ]
            w = f.bbox[2] - f.bbox[0]
            log.writerow([f"{t:.3f}", label, best, f"{s:.4f}", f"{L['texture']:.4f}", "" if np3 is None else f"{np3:.5f}",
                          f"{L['motion']:.4f}", int(L["blink"]), f"{f.yaw:.1f}", f"{f.pitch:.1f}", f"{sharpness(crop):.0f}", w])
        out = compose(frame, layer)
        hud(out, lines + ["(green = match + texture pass; thresholds are placeholders)", "q = quit"])
        if show(out):
            break
    fh.close()
    cam.close()
    cv2.destroyAllWindows()
    print("log saved:", log_path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("enroll")
    e.add_argument("name")
    v = sub.add_parser("verify")
    v.add_argument("--label", default="genuine", choices=["genuine", "photo", "screen", "other"])
    a = ap.parse_args()
    enroll(a.name) if a.cmd == "enroll" else verify(a.label)
