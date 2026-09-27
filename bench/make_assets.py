"""Build runtime assets for the lock-screen component:
  models/runtime/mesh_edges.bin   uint16 pairs: tessellation edges, then contour edges (header: 2 x uint32 counts)
  assets/sfx_scan.wav, sfx_unlock.wav, sfx_fail.wav   original synthesized sounds (no third-party audio)
"""
import wave
from pathlib import Path

import mediapipe as mp
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
C = mp.tasks.vision.FaceLandmarksConnections
tess = np.array([(c.start, c.end) for c in C.FACE_LANDMARKS_TESSELATION], np.uint16)
cont = np.array([(c.start, c.end) for c in C.FACE_LANDMARKS_CONTOURS], np.uint16)
with open(ROOT / "models" / "runtime" / "mesh_edges.bin", "wb") as f:
    f.write(np.array([len(tess), len(cont)], np.uint32).tobytes() + tess.tobytes() + cont.tobytes())
print(f"mesh edges: {len(tess)} tessellation, {len(cont)} contour")

SR = 44100
out = ROOT / "assets"
out.mkdir(exist_ok=True)


def env(n, a=0.005, r=0.08):
    t = np.arange(n) / SR
    e = np.minimum(1, t / a) * np.exp(-np.maximum(0, t - a) / r)
    return e


def save(name, x):
    x = x / (np.abs(x).max() + 1e-9) * 0.6
    fade = np.minimum(1, np.arange(len(x))[::-1] / (0.01 * SR))  # click-free tail
    pcm = (x * fade * 32767).astype(np.int16)
    with wave.open(str(out / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"{name}: {len(x) / SR * 1000:.0f} ms")


def tone(freq, dur, decay):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = freq if np.isscalar(freq) else np.linspace(freq[0], freq[1], n)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return (np.sin(ph) + 0.25 * np.sin(2 * ph) + 0.08 * np.sin(3 * ph)) * env(n, 0.004, decay)


# scan: soft rising "power-up" shimmer
n = int(0.35 * SR)
scan = tone((520, 880), 0.35, 0.2) * 0.5 + tone((1040, 1760), 0.35, 0.12) * 0.15
save("sfx_scan.wav", scan)

# unlock: bright two-note glassy chime (fifth interval) with a short airy whoosh
a = tone(1318.5, 0.5, 0.18)
b = np.concatenate([np.zeros(int(0.07 * SR)), tone(1975.5, 0.43, 0.2)])
noise = np.random.default_rng(1).standard_normal(len(a)) * env(len(a), 0.02, 0.05) * 0.08
save("sfx_unlock.wav", a + b + noise)

# fail: two short low "thunk" pulses, pitch dropping
f1 = tone((220, 160), 0.12, 0.05)
f2 = tone((200, 140), 0.14, 0.06)
save("sfx_fail.wav", np.concatenate([f1, np.zeros(int(0.05 * SR)), f2]))
