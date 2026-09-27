"""Summarise unlock_test attempt logs: result per attempt, reasons, timing, and key signals."""
import csv
import glob
import sys

import numpy as np

files = sys.argv[1:] or sorted(glob.glob("logs/attempts_*.csv"))
for f in files:
    rows = list(csv.DictReader(open(f)))
    ends = [r for r in rows if r["result"]]
    print(f"\n== {f}: {len(ends)} attempts, UNLOCK {sum(r['result'] == 'UNLOCK' for r in ends)}")
    for e in ends:
        a = e["attempt"]
        fr = [r for r in rows if r["attempt"] == a and not r["result"] and r["score"]]
        ch = [r for r in fr if r["state"] == "CHALLENGE"]
        num = lambda rs, k: [float(r[k]) for r in rs if r[k]]
        sc, tx, npt, tu = num(fr, "score"), num(fr, "texture"), num(ch, "nonplanar_turn"), num(ch, "turn")
        br = num(fr, "blink_raw")
        print(f"  #{a:>2} {e['result']:6} {float(e['elapsed']):4.2f}s dir={e['direction'] or '-':5} "
              f"score med {np.median(sc) if sc else 0:.2f} | tex med {np.median(tx) if tx else 0:.2f} min {min(tx) if tx else 0:.2f} | "
              f"turn max {max(tu, key=abs) if tu else 0:+5.1f} | 3D peak {max(npt) * 100 if npt else 0:4.1f}% | "
              f"blink range {min(br) if br else 0:.2f}-{max(br) if br else 0:.2f} | {e['reason']}")
