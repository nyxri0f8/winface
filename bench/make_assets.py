"""Build runtime assets for the lock-screen component:
  models/runtime/mesh_edges.bin   uint16 pairs: tessellation edges, then contour edges (header: 2 x uint32 counts)
                                  (only for the bench previews - the lock-screen HUD no longer draws the mesh)
(assets/sfx_unlock.wav is a user-supplied clip and is not generated here)
"""
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

