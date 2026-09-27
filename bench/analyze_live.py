"""Compare liveness signals across labelled runs (new-format logs only)."""
import csv
import glob

import numpy as np

pct = lambda a, q: np.percentile(a, q) if len(a) else float("nan")

for f in sorted(glob.glob("logs/*.csv")):
    rows = list(csv.DictReader(open(f)))
    if not rows or "texture" not in rows[0]:
        continue
    g = lambda k: np.array([float(r[k]) for r in rows if r[k] != ""])
    s, tx, mo = g("score"), g("texture"), g("motion")
    np3 = np.array([float(r["nonplanar"]) for r in rows if r["nonplanar"] != ""])
    mo3 = np.array([float(r["motion"]) for r in rows if r["nonplanar"] != ""])
    moving = np3[mo3 > 0.03]  # only frames with real movement (>3% of face width)
    bl = g("blink")
    print(f"\n{f}  n={len(rows)}  duration {float(rows[-1]['t']) - float(rows[0]['t']):.0f}s")
    print(f"  match    p5 {pct(s, 5):.2f}  median {np.median(s):.2f}  max {s.max():.2f}   frames >=0.42: {(s >= 0.42).mean():.0%}")
    print(f"  texture  p5 {pct(tx, 5):.3f}  p25 {pct(tx, 25):.3f}  median {np.median(tx):.3f}  max {tx.max():.3f}   frames >0.5: {(tx > 0.5).mean():.0%}  >0.8: {(tx > 0.8).mean():.0%}")
    print(f"  3D       moving frames {len(moving)}/{len(np3)}   non-planar p10 {pct(moving, 10) * 100:.2f}%  median {pct(moving, 50) * 100:.2f}%  p90 {pct(moving, 90) * 100:.2f}%"
          f"   | still frames median {pct(np3[mo3 <= 0.03], 50) * 100:.2f}%")
    print(f"  motion   median {np.median(mo) * 100:.1f}%   blink frames {bl.mean():.0%}")
    # per-second timeline of texture to see if spoof passes in bursts
    t = np.array([float(r["t"]) for r in rows]); t -= t[0]
    sec = [f"{np.median(tx[(t >= i) & (t < i + 2)]):.2f}" for i in range(0, int(t[-1]), 2) if ((t >= i) & (t < i + 2)).any()]
    print("  texture per 2s:", " ".join(sec))
