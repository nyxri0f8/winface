"""CPU/battery optimisation: INT8-quantise the recognition model and measure cold start + speed + accuracy.

Cold start matters because FaceGate loads everything fresh every time the lock screen appears.
"""
import time
from pathlib import Path

import cv2
import numpy as np
import onnx
import onnxruntime as ort
from onnx import version_converter
from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static
from onnxruntime.quantization.shape_inference import quant_pre_process

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "models"
crops = np.load(Path(__file__).parent / "data" / "crops.npz")["crops"]


def prep(c):
    x = cv2.cvtColor(c, cv2.COLOR_BGR2RGB).astype(np.float32)
    return ((x - 127.5) / 127.5).transpose(2, 0, 1)[None]


class Reader(CalibrationDataReader):
    def __init__(self, name):
        self.it = iter([{name: prep(c)} for c in crops[::3]])

    def get_next(self):
        return next(self.it, None)


def session(path, threads):
    so = ort.SessionOptions()
    so.intra_op_num_threads = threads
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    t = time.perf_counter()
    s = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    return s, (time.perf_counter() - t) * 1000


def embed_all(s):
    n = s.get_inputs()[0].name
    v = np.concatenate([s.run(None, {n: prep(c)})[0] for c in crops])
    return v / np.linalg.norm(v, axis=1, keepdims=True)


for name, src in [("arcface", MODELS / "buffalo_l" / "w600k_r50.onnx"), ("adaface", MODELS / "adaface_ir101.onnx")]:
    pre = MODELS / f"{name}_pre.onnx"
    q = MODELS / f"{name}_int8.onnx"
    if not q.exists():
        m = onnx.load(str(src))
        if m.opset_import[0].version < 13:  # per-channel INT8 needs opset >= 13
            m = version_converter.convert_version(m, 13)
        onnx.save(m, str(pre))
        quant_pre_process(str(pre), str(pre))
        inp = ort.InferenceSession(str(src), providers=["CPUExecutionProvider"]).get_inputs()[0].name
        quantize_static(str(pre), str(q), Reader(inp), quant_format=QuantFormat.QDQ,
                        activation_type=QuantType.QUInt8, weight_type=QuantType.QInt8, per_channel=True)
        pre.unlink(missing_ok=True)
    ref = None
    for label, path in [("fp32", src), ("int8", q)]:
        for threads in (2, 4, 8):
            s, load_ms = session(path, threads)
            n = s.get_inputs()[0].name
            x = prep(crops[0])
            t = time.perf_counter()
            s.run(None, {n: x})
            first_ms = (time.perf_counter() - t) * 1000
            t = time.perf_counter()
            for _ in range(20):
                s.run(None, {n: x})
            run_ms = (time.perf_counter() - t) / 20 * 1000
            extra = ""
            if threads == 8:
                e = embed_all(s)
                if ref is None:
                    ref = e
                else:
                    agree = (e * ref).sum(1)
                    # does the quantised model still rank captures the same way?
                    c = ref.mean(0); c /= np.linalg.norm(c)
                    cq = e.mean(0); cq /= np.linalg.norm(cq)
                    extra = f"  fp32-vs-int8 cosine min {agree.min():.4f} mean {agree.mean():.4f}; score shift {np.abs(ref @ c - e @ cq).max():.4f}"
            print(f"{name:8} {label} threads={threads}: load {load_ms:5.0f} ms, first run {first_ms:5.0f} ms, "
                  f"steady {run_ms:5.1f} ms, file {path.stat().st_size / 1e6:4.0f} MB{extra}")
