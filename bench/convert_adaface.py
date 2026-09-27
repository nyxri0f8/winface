"""One-time: convert AdaFace IR-101 (PyTorch safetensors) to ONNX and verify the outputs match."""
import sys
import types
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
from safetensors.torch import load_file

MODELS = Path(__file__).resolve().parents[1] / "models"
SRC = MODELS / "adaface_ir101"
OUT = MODELS / "adaface_ir101.onnx"

# model.py imports fvcore only for a FLOP counter we never call
sys.modules["fvcore"] = types.ModuleType("fvcore")
sys.modules["fvcore.nn"] = types.SimpleNamespace(flop_count=None)
sys.path.insert(0, str(SRC / "models" / "iresnet"))
from model import IR_101  # noqa: E402

net = IR_101(input_size=(112, 112), output_dim=512)
sd = load_file(str(SRC / "model.safetensors"))
prefix = next(p for p in ("model.net.", "net.", "") if any(k.startswith(p) for k in sd))
sd = {k[len(prefix):]: v for k, v in sd.items() if k.startswith(prefix)}
missing, unexpected = net.load_state_dict(sd, strict=True), None
net.eval()
print(f"loaded {len(sd)} tensors (prefix '{prefix}')")

x = torch.randn(1, 3, 112, 112)
torch.onnx.export(net, x, str(OUT), input_names=["input"], output_names=["embedding"],
                  opset_version=17, dynamic_axes={"input": {0: "n"}, "embedding": {0: "n"}}, dynamo=False)

with torch.no_grad():
    ref = net(x).numpy()
sess = ort.InferenceSession(str(OUT), providers=["CPUExecutionProvider"])
got = sess.run(None, {"input": x.numpy()})[0]
cos = float((ref * got).sum() / (np.linalg.norm(ref) * np.linalg.norm(got)))
print(f"saved {OUT.name} ({OUT.stat().st_size / 1e6:.0f} MB), torch vs onnx cosine = {cos:.6f}")
