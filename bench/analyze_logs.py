"""Summarise verify logs: score distribution per label."""
import collections
import csv
import glob

import numpy as np

for f in sorted(glob.glob("logs/*.csv")):
    rows = list(csv.DictReader(open(f)))
    if not rows:
        print(f, "empty")
        continue
    a = np.array([float(r["arcface"]) for r in rows])
    sh = np.array([float(r["sharp"]) for r in rows])
    prof = dict(collections.Counter(r["profile"] for r in rows))
    hist = np.histogram(a, bins=[-1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1])[0]
    print(f"{f}: n={len(rows)} profiles={prof}")
    print(f"   arcface min {a.min():.2f}  p10 {np.percentile(a, 10):.2f}  median {np.median(a):.2f}  max {a.max():.2f}  sharp median {np.median(sh):.0f}")
    print("   bins <.2/.2-.3/.3-.4/.4-.5/.5-.6/.6-.7/.7-.8/>.8:", list(hist))
