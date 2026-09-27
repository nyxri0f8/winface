"""Per-3-second timeline of a log: texture vs face size, pose, sharpness, motion."""
import csv
import sys

import numpy as np

for f in sys.argv[1:]:
    rows = list(csv.DictReader(open(f)))
    t0 = float(rows[0]["t"])
    print("==", f)
    for i in range(0, int(float(rows[-1]["t"]) - t0) + 1, 3):
        seg = [x for x in rows if i <= float(x["t"]) - t0 < i + 3]
        if not seg:
            continue
        m = lambda k: np.median([float(x[k]) for x in seg])
        print(f"  {i:2d}s tex {m('texture'):.2f} score {m('score'):.2f} width {m('width'):4.0f} yaw {m('yaw'):+4.0f} "
              f"pitch {m('pitch'):+4.0f} sharp {m('sharp'):4.0f} motion {m('motion') * 100:4.1f}%")
