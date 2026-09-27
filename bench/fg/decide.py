"""FaceGate unlock decision engine (bench version; will be ported 1:1 to C++).

SEARCH     face at the right distance, identity matches 3 of the last 5 frames, texture looks real
CHALLENGE  random "turn slightly LEFT/RIGHT": yaw must move >= TURN_DEG the asked way, the landmark
           motion during the turn must be 3D (non-planar), and texture must stay real
UNLOCK / FAIL
"""
import random
import time
from collections import deque
from dataclasses import dataclass, field

import numpy as np

from .align import align
from .liveness import PARALLAX_IDX, nonplanarity


@dataclass
class Params:
    match: float = 0.42          # from LFW calibration: max stranger 0.313
    need: int = 3                # of the last `window` frames
    window: int = 5
    texture: float = 0.60        # median MiniFASNet "real" probability over recent frames
    width_min: int = 170         # face width in px at 1280x720; outside this MiniFASNet is unreliable
    width_max: int = 330
    turn_deg: float = 12.0
    wrong_way_deg: float = 15.0   # only a clear turn the other way fails the challenge
    frontal_deg: float = 8.0      # challenge starts from a centred head, measured from there
    nonplanar_min: float = 0.025  # genuine turns measured 3.3%+ (18 attempts); flat media stays < 1%
    # during the turn the score only has to stay above a floor (still well above the best stranger, 0.313);
    # one motion-blurred frame below it is forgiven. Identity was already proven frontally before the turn.
    challenge_floor_drop: float = 0.08
    challenge_floor_min: float = 0.34
    challenge_lows_allowed: int = 1
    search_timeout: float = 5.0
    challenge_timeout: float = 2.5


@dataclass
class Status:
    state: str = "SEARCH"        # SEARCH | CHALLENGE | UNLOCK | FAIL
    hint: str = ""
    direction: str = ""          # LEFT / RIGHT during CHALLENGE
    profile: str = ""
    progress: float = 0.0        # 0..1 overall scan progress (drives the HUD / bench ring)
    reason: str = ""
    signals: dict = field(default_factory=dict)


class Engine:
    def __init__(self, embedder, liveness, profiles, params=None, rng=None):
        self.emb, self.live, self.profiles = embedder, liveness, profiles
        self.p = params or Params()
        self.rng = rng or random.SystemRandom()  # unpredictable challenge direction
        self.reset()

    def reset(self):
        self.st = Status()
        self.hits = deque(maxlen=self.p.window)
        self.tex = deque(maxlen=self.p.window)
        self.t_start = time.perf_counter()
        self.ch = None
        self.live.reset()

    def _identify(self, frame, face):
        v = self.emb.embed(align(frame, face.pts))
        best, best_s = "", -1.0
        for name, tmpl in self.profiles.items():
            s = float(np.sort(tmpl @ v)[-3:].mean())
            if s > best_s:
                best, best_s = name, s
        return best, best_s

    def step(self, frame, faces):
        st, p, now = self.st, self.p, time.perf_counter()
        if st.state in ("UNLOCK", "FAIL"):
            return st
        if not faces:
            st.hint = "Looking for you..."
            self.hits.append(False)
            return self._timeouts(now)
        # nearest face only; a second face of similar size nearby blocks the unlock
        faces = sorted(faces, key=lambda f: f.bbox[0] - f.bbox[2])
        f = faces[0]
        w = f.bbox[2] - f.bbox[0]
        if len(faces) > 1 and (faces[1].bbox[2] - faces[1].bbox[0]) > 0.6 * w:
            st.hint = "More than one person in view"
            self.hits.append(False)
            return self._timeouts(now)
        if w < p.width_min or w > p.width_max:
            st.hint = "Move closer" if w < p.width_min else "Move back a little"
            self.hits.append(False)
            return self._timeouts(now)

        name, score = self._identify(frame, f)
        L = self.live.update(frame, f)
        self.tex.append(L["texture"])
        self.hits.append(score >= p.match)
        tex_med = float(np.median(self.tex))
        st.signals = {"score": score, "texture": L["texture"], "texture_med": tex_med, "width": w,
                      "yaw": f.yaw, "pitch": f.pitch, "blink": L["blink"], "blink_raw": L["blink_raw"],
                      "nonplanar_turn": None}
        st.profile = name

        if st.state == "SEARCH":
            st.hint = "Scanning..."
            st.progress = 0.5 * sum(self.hits) / p.need
            ready = sum(self.hits) >= p.need and len(self.tex) >= p.need and tex_med >= p.texture
            if ready and abs(f.yaw) > p.frontal_deg:
                st.hint = "Look straight at the screen"  # start the turn from centre, not mid-motion
            elif ready:
                st.state, st.direction = "CHALLENGE", self.rng.choice(["LEFT", "RIGHT"])
                self.ch = {"t0": now, "yaw0": f.yaw, "pts0": f.pts[PARALLAX_IDX, :2].copy(), "w0": float(w), "peak_np": 0.0,
                           "lows": 0}
                st.hint = f"Turn your head slightly {st.direction}"
            return self._timeouts(now)

        # CHALLENGE
        c = self.ch
        d = f.yaw - c["yaw0"]
        want = -1 if st.direction == "LEFT" else 1
        npl = nonplanarity(c["pts0"], f.pts[PARALLAX_IDX, :2], c["w0"]) or 0.0
        c["peak_np"] = max(c["peak_np"], npl)
        st.signals["nonplanar_turn"] = c["peak_np"]
        st.signals["turn"] = d
        st.progress = 0.5 + 0.5 * min(max(d * want, 0) / p.turn_deg, 1.0)
        if d * want <= -p.wrong_way_deg:
            return self._fail("turned the wrong way")
        floor = min(p.match, max(p.match - p.challenge_floor_drop, p.challenge_floor_min))
        if score < floor:
            c["lows"] += 1
            if c["lows"] > p.challenge_lows_allowed:
                return self._fail(f"identity lost during challenge ({score:.2f})")
            return self._timeouts(now)  # forgiven once (motion blur), never on the frame that completes the turn
        if L["texture"] < 0.3 and tex_med < p.texture:
            return self._fail(f"texture looks fake ({tex_med:.2f})")
        if d * want >= p.turn_deg:
            if c["peak_np"] < p.nonplanar_min:
                return self._fail(f"turn looked flat ({c['peak_np'] * 100:.2f}%) - photo/screen?")
            st.state, st.progress, st.hint = "UNLOCK", 1.0, "Unlocked"
            st.reason = f"{name}: turn {d:+.0f} deg, 3D {c['peak_np'] * 100:.1f}%, texture {tex_med:.2f}"
        return self._timeouts(now)

    def _fail(self, why):
        self.st.state, self.st.reason, self.st.hint = "FAIL", why, "Not recognised"
        return self.st

    def _timeouts(self, now):
        st = self.st
        if st.state == "SEARCH" and now - self.t_start > self.p.search_timeout:
            return self._fail("no confident match in time")
        if st.state == "CHALLENGE" and now - self.ch["t0"] > self.p.challenge_timeout:
            return self._fail("challenge not completed in time")
        return st
