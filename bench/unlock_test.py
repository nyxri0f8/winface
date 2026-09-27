"""Full unlock flow on the bench: scan -> random turn challenge -> UNLOCK / FAIL, repeated attempts.

  python unlock_test.py --label genuine|photo|video|other
Space = next attempt, q = quit. Every attempt + every frame's signals are logged to logs/attempts_<label>_<time>.csv
"""
import argparse
import csv
import time
from pathlib import Path

import cv2
import numpy as np

from facetest import WIN, frames, load_profiles, show, window
from fg.camera import Camera
from fg.decide import Engine
from fg.liveness import Liveness
from fg.recog import Embedder
from fg.render import COLORS, N_TICKS, compose, draw_mesh, draw_ring, hud
from fg.tracker import Tracker

LOGS = Path(__file__).parent / "logs"


def banner(img, st):
    """Face ID-style pill at the top centre: status text + direction arrow during the challenge."""
    h, w = img.shape[:2]
    cx, top = w // 2, 18
    color = {"UNLOCK": COLORS["ok"], "FAIL": COLORS["fail"]}.get(st.state, COLORS["scan"])
    cv2.rectangle(img, (cx - 230, top), (cx + 230, top + 70), (20, 20, 20), -1, cv2.LINE_AA)
    cv2.rectangle(img, (cx - 230, top), (cx + 230, top + 70), color, 2, cv2.LINE_AA)
    text = {"UNLOCK": "UNLOCKED", "FAIL": "NOT RECOGNISED"}.get(st.state, st.hint)
    size = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.8, 2)[0]
    cv2.putText(img, text, (cx - size[0] // 2, top + 45), cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2, cv2.LINE_AA)
    if st.state == "CHALLENGE":
        # a glowing target dot near the screen edge: people naturally turn toward it
        side = -1 if st.direction == "LEFT" else 1
        y, dot = top + 110, (cx + side * (w // 2 - 70), top + 110)
        pulse = 18 + 6 * np.sin(time.perf_counter() * 10)
        cv2.circle(img, dot, int(pulse + 14), tuple(c // 3 for c in color), -1, cv2.LINE_AA)
        cv2.circle(img, dot, int(pulse), color, -1, cv2.LINE_AA)
        cv2.arrowedLine(img, (cx - side * 60, y), (cx + side * 60, y), color, 6, cv2.LINE_AA, tipLength=0.4)


def main(label):
    emb = Embedder()
    profiles = load_profiles(emb)
    tracker, live, cam = Tracker(), Liveness(), Camera()
    eng = Engine(emb, live, profiles)
    LOGS.mkdir(exist_ok=True)
    path = LOGS / f"attempts_{label}_{int(time.time())}.csv"
    fh = open(path, "w", newline="")
    log = csv.writer(fh)
    log.writerow(["attempt", "t", "state", "direction", "score", "texture", "texture_med", "width", "yaw", "pitch",
                  "turn", "nonplanar_turn", "blink", "blink_raw", "result", "reason", "elapsed"])
    window()
    attempt, results, done_at = 1, [], None
    start_at = time.perf_counter() + 2.0  # countdown so a spoof can be positioned (and you can get out of view)
    for frame, ts in frames(cam):
        t = time.perf_counter()
        faces = tracker.process(frame, ts * 1000)
        if t < start_at:
            out = compose(frame, np.zeros_like(frame), dim=0.12)
            hud(out, [f"attempt {attempt} starts in {start_at - t:.1f}s - put ONLY the test subject in view"], org=(16, 40))
            cv2.imshow(WIN, out)
            if (cv2.waitKey(1) & 0xFF) in (ord("q"), 27):
                break
            eng.reset()
            continue
        st = eng.step(frame, faces) if done_at is None else eng.st
        s = st.signals
        elapsed = t - eng.t_start
        if done_at is None:
            log.writerow([attempt, f"{elapsed:.3f}", st.state, st.direction, *[
                "" if s.get(k) is None else f"{s[k]:.4f}" for k in
                ("score", "texture", "texture_med", "width", "yaw", "pitch", "turn", "nonplanar_turn")],
                int(bool(s.get("blink"))), f"{s.get('blink_raw', 0):.3f}", "", "", ""])
        if st.state in ("UNLOCK", "FAIL") and done_at is None:
            done_at = t
            results.append(st.state)
            log.writerow([attempt, f"{elapsed:.3f}", st.state, st.direction] + [""] * 10 + [st.state, st.reason, f"{elapsed:.2f}"])
            fh.flush()
            print(f"attempt {attempt}: {st.state:6}  {elapsed:4.2f}s  {st.reason}")

        layer = np.zeros_like(frame)
        color = {"UNLOCK": COLORS["ok"], "FAIL": COLORS["fail"]}.get(st.state, COLORS["scan"])
        if faces:
            f = faces[0]
            draw_mesh(layer, f.pts, color, t, f.bbox)
            x0, y0, x1, y1 = f.bbox
            lit = np.arange(N_TICKS) < int(st.progress * N_TICKS)
            draw_ring(layer, ((x0 + x1) / 2, (y0 + y1) / 2), max(x1 - x0, y1 - y0) * 0.72, lit, color, t)
        out = compose(frame, layer, dim=0.12)
        banner(out, st)
        info = [f"attempt {attempt}   label {label}   unlocked {results.count('UNLOCK')}/{len(results)}"]
        if s:
            npt = s.get("nonplanar_turn")
            info.append(f"match {s['score']:.2f}  texture {s['texture']:.2f} (med {s['texture_med']:.2f})  width {s['width']}  "
                        f"3D-turn {'-' if npt is None else f'{npt * 100:.1f}%'}  blink {s['blink_raw']:.2f}")
        if done_at is not None:
            info.append(f"{st.state}: {st.reason}  ({done_at - eng.t_start:.2f}s)   SPACE = next attempt")
        hud(out, info, org=(16, out.shape[0] - 20 - 24 * len(info) + 24))
        cv2.imshow(WIN, out)
        k = cv2.waitKey(1) & 0xFF
        if k in (ord("q"), 27) or cv2.getWindowProperty(WIN, cv2.WND_PROP_VISIBLE) < 1:
            break
        if k == ord(" ") and done_at is not None:
            attempt, done_at = attempt + 1, None
            start_at = time.perf_counter() + 2.0
            eng.reset()
    fh.close()
    cam.close()
    cv2.destroyAllWindows()
    print(f"{label}: unlocked {results.count('UNLOCK')}/{len(results)}   log: {path}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="genuine", choices=["genuine", "photo", "video", "other"])
    main(ap.parse_args().label)
