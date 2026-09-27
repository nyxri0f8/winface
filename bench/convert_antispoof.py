"""One-time: convert the two MiniFASNet anti-spoof models (PyTorch) to ONNX and verify."""
import sys
from collections import OrderedDict
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch

SRC = Path(__file__).resolve().parents[1] / "models" / "antispoof"
sys.path.insert(0, str(SRC))
from MiniFASNet import MiniFASNetV1SE, MiniFASNetV2  # noqa: E402

for pth, cls, out in [("2.7_80x80_MiniFASNetV2.pth", MiniFASNetV2, "fas_v2_s2.7.onnx"),
                      ("4_0_0_80x80_MiniFASNetV1SE.pth", MiniFASNetV1SE, "fas_v1se_s4.0.onnx")]:
    net = cls(conv6_kernel=(5, 5))  # get_kernel(80, 80)
    sd = torch.load(SRC / pth, map_location="cpu")
    sd = OrderedDict((k[7:] if k.startswith("module.") else k, v) for k, v in sd.items())
    net.load_state_dict(sd)
    net.eval()
    x = torch.rand(1, 3, 80, 80) * 255  # original repo feeds raw 0-255 BGR (no /255)
    torch.onnx.export(net, x, str(SRC / out), input_names=["input"], output_names=["logits"], opset_version=13, dynamo=False)
    with torch.no_grad():
        ref = torch.softmax(net(x), 1).numpy()
    got = ort.InferenceSession(str(SRC / out), providers=["CPUExecutionProvider"]).run(None, {"input": x.numpy()})[0]
    e = np.exp(got - got.max(1, keepdims=True))
    got = e / e.sum(1, keepdims=True)
    print(f"{out}: max prob diff torch vs onnx = {np.abs(ref - got).max():.2e}")
