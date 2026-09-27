"""Phase 1 live viewer: neon face mesh on the HP webcam with head pose, blink/smile and timings.

Keys: q / Esc = quit, m = toggle camera image, r = reset tick ring, s = save screenshot to bench/shots/
"""
import time
from pathlib import Path

import cv2
import numpy as np

from fg.camera import Camera
from fg.render import COLORS, N_TICKS, compose, draw_mesh, draw_ring, hud
from fg.tracker import Tracker

SHOTS = Path(__file__).parent / "shots"


def main():
    t0 = time.perf_counter()
    tracker = Tracker()
    cam = Camera()
    print(f"camera open: {cam.open_ms:.0f} ms, total startup {(time.perf_counter() - t0) * 1000:.0f} ms")

    show_cam, last_seq, fps, t_prev = True, -1, 0.0, time.perf_counter()
    lit = np.zeros(N_TICKS, bool)
    cv2.namedWindow("FaceGate - live", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("FaceGate - live", 1280, 720)

    while True:
        frame, ts, seq = cam.read()
        if seq == last_seq:
            time.sleep(0.002)
            continue
        last_seq = seq
        frame = cv2.flip(frame, 1)  # mirror, like a selfie camera

        t = time.perf_counter()
        faces = tracker.process(frame, ts * 1000)
        track_ms = (time.perf_counter() - t) * 1000

        layer = np.zeros_like(frame)
        lines = []
        for f in faces:
            color = COLORS["scan"]
            draw_mesh(layer, f.pts, color, t, f.bbox)
            x0, y0, x1, y1 = f.bbox
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            # light the tick in the direction the head is turned (enrolment preview)
            if abs(f.yaw) > 8 or abs(f.pitch) > 8:
                ang = np.arctan2(-f.pitch, f.yaw)  # screen angle of head direction
                lit[int(((ang + np.pi / 2) % (2 * np.pi)) / (2 * np.pi) * N_TICKS) % N_TICKS] = True
            draw_ring(layer, (cx, cy), max(x1 - x0, y1 - y0) * 0.72, lit, color, t)
            b = f.blend
            lines += [
                f"yaw {f.yaw:+5.1f}  pitch {f.pitch:+5.1f}  roll {f.roll:+5.1f}",
                f"blink L {b.get('eyeBlinkLeft', 0):.2f}  R {b.get('eyeBlinkRight', 0):.2f}   "
                f"smile {(b.get('mouthSmileLeft', 0) + b.get('mouthSmileRight', 0)) / 2:.2f}",
            ]

        now = time.perf_counter()
        fps = 0.9 * fps + 0.1 / max(now - t_prev, 1e-6)
        t_prev = now
        out = compose(frame if show_cam else np.zeros_like(frame), layer)
        hud(out, [f"FPS {fps:4.1f}   track {track_ms:4.1f} ms   faces {len(faces)}"] + lines
            + ["q quit | m camera on/off | r reset ring | s screenshot"])
        cv2.imshow("FaceGate - live", out)

        k = cv2.waitKey(1) & 0xFF
        if k in (ord("q"), 27) or cv2.getWindowProperty("FaceGate - live", cv2.WND_PROP_VISIBLE) < 1:
            break
        if k == ord("m"):
            show_cam = not show_cam
        if k == ord("r"):
            lit[:] = False
        if k == ord("s"):
            SHOTS.mkdir(exist_ok=True)
            p = SHOTS / f"live_{int(time.time())}.png"
            cv2.imwrite(str(p), out)
            print("saved", p)

    cam.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
