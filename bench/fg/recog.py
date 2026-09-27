"""Face recognition embedder: ArcFace R50 INT8 on CPU (chosen in Phase 1 for speed + battery)."""
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

MODELS = Path(__file__).resolve().parents[2] / "models"
PATHS = {
    "arcface_int8": MODELS / "arcface_int8.onnx",
    "arcface": MODELS / "buffalo_l" / "w600k_r50.onnx",
    "adaface": MODELS / "adaface_ir101.onnx",
}


class Embedder:
    def __init__(self, name="arcface_int8", threads=4):
        self.name = name
        so = ort.SessionOptions()
        so.intra_op_num_threads = threads
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.sess = ort.InferenceSession(str(PATHS[name]), so, providers=["CPUExecutionProvider"])
        self.inp = self.sess.get_inputs()[0].name

    def embed(self, face112_bgr):
        """112x112 aligned BGR crop -> L2-normalised 512-d vector. Models take RGB in [-1, 1]."""
        x = cv2.cvtColor(face112_bgr, cv2.COLOR_BGR2RGB).astype(np.float32)
        x = ((x - 127.5) / 127.5).transpose(2, 0, 1)[None]
        v = self.sess.run(None, {self.inp: x})[0][0]
        return v / (np.linalg.norm(v) + 1e-9)
