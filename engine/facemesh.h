// WinFace engine - MediaPipe Face Landmarker re-implemented on ONNX (port of bench/fg/mp_onnx.py).
#pragma once
#include <array>
#include <optional>
#include <vector>

#include "geom.h"
#include "image.h"
#include "onnx.h"

namespace fg {

struct Face {
    std::vector<std::array<float, 3>> pts;  // 478 x (x, y, z) in image pixels
    float presence = 0;
    float yaw = 0, pitch = 0, roll = 0;      // degrees, same convention as the bench
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;      // landmark bounding box
    int width() const { return x1 - x0; }
    Pt p(int i) const { return {pts[i][0], pts[i][1]}; }
};

struct Roi { double cx, cy, size, angle; };

class FaceMesh {
public:
    FaceMesh(Ort::Env& env, const std::wstring& model_dir);
    std::optional<Face> process(const Image& bgr);  // detect or track, then landmarks + pose
    void reset() { roi_.reset(); }

private:
    std::optional<Roi> detect(const Image& bgr);
    std::optional<Face> landmarks(const Image& bgr, const Roi& roi);
    void pose(Face& f) const;

    Model det_, lmk_;
    std::vector<Pt> anchors_;
    std::vector<std::array<double, 3>> canon_;
    std::vector<double> canon_w_;
    std::optional<Roi> roi_;
};

}  // namespace fg
