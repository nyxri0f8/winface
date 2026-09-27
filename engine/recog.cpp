#include "recog.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace fg {
namespace {

const Pt kTemplate[5] = {{38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
                         {41.5493f, 92.3655f}, {70.7299f, 92.2041f}};
const int kMp5[5] = {468, 473, 1, 61, 291};  // iris centres, nose tip, mouth corners

// Crop like Silent-Face's CropImage (scaled box around the face, shifted inside the image),
// then resize to 80x80 with bilinear half-pixel sampling (like cv::resize INTER_LINEAR).
Image fas_crop(const Image& img, const Face& f, double scale) {
    double bw = f.x1 - f.x0, bh = f.y1 - f.y0;
    scale = std::min({(img.h - 1) / bh, (img.w - 1) / bw, scale});
    double nw = bw * scale, nh = bh * scale, cx = f.x0 + bw / 2, cy = f.y0 + bh / 2;
    double l = cx - nw / 2, t = cy - nh / 2, r = cx + nw / 2, b = cy + nh / 2;
    if (l < 0) { r -= l; l = 0; }
    if (t < 0) { b -= t; t = 0; }
    if (r > img.w - 1) { l -= r - img.w + 1; r = img.w - 1; }
    if (b > img.h - 1) { t -= b - img.h + 1; b = img.h - 1; }
    int il = int(l), it = int(t), cw = int(r) + 1 - il, ch = int(b) + 1 - it;
    double sx = double(cw) / 80, sy = double(ch) / 80;
    // src = il + (dst + 0.5) * sx - 0.5  ->  dst = (src - il + 0.5) / sx - 0.5
    Affine m = {1 / sx, 0, (0.5 - il) / sx - 0.5, 0, 1 / sy, (0.5 - it) / sy - 0.5};
    return warp_affine(img, m, 80, 80, true);
}

float fas_real(Model& m, const Image& crop) {
    std::vector<float> in(3 * 80 * 80);
    for (int c = 0; c < 3; ++c)  // raw BGR 0..255, NCHW (as the original repo feeds it)
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 80; ++x) in[(c * 80 + y) * 80 + x] = crop.row(y)[x * 3 + c];
    auto z = m.run(in.data(), {1, 3, 80, 80})[0].data;
    float mx = *std::max_element(z.begin(), z.end()), sum = 0;
    for (auto& v : z) sum += (v = std::exp(v - mx));
    return z[1] / sum;  // class 1 = real
}

}  // namespace

// arcface_int8.onnx: locally quantized dev model (not redistributable). w600k_r50.onnx: the official InsightFace
// buffalo_l model the WinFace installer downloads after the user accepts its non-commercial licence.
static std::wstring arcface_path(const std::wstring& dir) {
    std::wstring q = dir + L"\\arcface_int8.onnx";
    return GetFileAttributesW(q.c_str()) != INVALID_FILE_ATTRIBUTES ? q : dir + L"\\w600k_r50.onnx";
}

Recognizer::Recognizer(Ort::Env& env, const std::wstring& dir) : m_(env, arcface_path(dir), 4) {}

Image Recognizer::align112(const Image& bgr, const Face& f) {
    Pt src[5];
    for (int i = 0; i < 5; ++i) src[i] = f.p(kMp5[i]);
    return warp_affine(bgr, estimate_similarity(src, kTemplate, 5), 112, 112, true);
}

Embedding Recognizer::embed(const Image& bgr, const Face& f) {
    Image c = align112(bgr, f);
    std::vector<float> in(3 * 112 * 112);
    for (int y = 0; y < 112; ++y)
        for (int x = 0; x < 112; ++x)
            for (int k = 0; k < 3; ++k)  // RGB planes, [-1, 1]
                in[(k * 112 + y) * 112 + x] = (c.row(y)[x * 3 + (2 - k)] - 127.5f) / 127.5f;
    auto out = m_.run(in.data(), {1, 3, 112, 112})[0].data;
    Embedding e{};
    double n = 0;
    for (int i = 0; i < 512; ++i) n += double(out[i]) * out[i];
    n = std::sqrt(n) + 1e-9;
    for (int i = 0; i < 512; ++i) e[i] = float(out[i] / n);
    return e;
}

float match_score(const std::vector<Embedding>& t, const Embedding& v) {
    float best[3] = {-2, -2, -2};
    for (auto& e : t) {
        float s = 0;
        for (int i = 0; i < 512; ++i) s += e[i] * v[i];
        if (s > best[0]) { best[0] = s; std::sort(best, best + 3); }
    }
    int n = int(std::min<size_t>(3, t.size()));
    float sum = 0;
    for (int i = 3 - n; i < 3; ++i) sum += best[i];
    return n ? sum / n : -1;
}

Texture::Texture(Ort::Env& env, const std::wstring& dir)
    : v2_(env, dir + L"\\fas_v2_s2.7.onnx", 2), v1se_(env, dir + L"\\fas_v1se_s4.0.onnx", 2) {}

float Texture::real_prob(const Image& bgr, const Face& f) {
    return 0.5f * (fas_real(v2_, fas_crop(bgr, f, 2.7)) + fas_real(v1se_, fas_crop(bgr, f, 4.0)));
}

}  // namespace fg
