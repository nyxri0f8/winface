#include "facemesh.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace fg {
namespace {

constexpr double kPi = 3.14159265358979323846;

double norm_angle(double a) {
    a = std::fmod(a + kPi, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    return a - kPi;
}

// rotation from the line a -> b (target angle 0), square long side, scaled 1.5
Roi roi_from(Pt a, Pt b, double x0, double y0, double x1, double y1) {
    double angle = norm_angle(-std::atan2(-(b.y - a.y), b.x - a.x));
    return {(x0 + x1) / 2, (y0 + y1) / 2, std::max(x1 - x0, y1 - y0) * 1.5, angle};
}

// image -> out x out crop of a rotated square (same matrix as mp_onnx.rect_affine)
Affine rect_affine(const Roi& r, int out) {
    double c = std::cos(r.angle), s = std::sin(r.angle), k = out / r.size;
    return {k * c, k * s, out / 2.0 - k * (c * r.cx + s * r.cy), -k * s, k * c, out / 2.0 - k * (-s * r.cx + c * r.cy)};
}

// area-average downscale (like cv::INTER_AREA) of the whole image to ow x oh
Image resize_area(const Image& src, int ow, int oh) {
    Image out(ow, oh, 3);
    double sx = double(src.w) / ow, sy = double(src.h) / oh;
    for (int y = 0; y < oh; ++y) {
        int ya = int(y * sy), yb = std::max(ya + 1, int((y + 1) * sy));
        for (int x = 0; x < ow; ++x) {
            int xa = int(x * sx), xb = std::max(xa + 1, int((x + 1) * sx));
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int yy = ya; yy < yb && yy < src.h; ++yy)
                for (int xx = xa; xx < xb && xx < src.w; ++xx, ++n)
                    for (int c = 0; c < 3; ++c) sum[c] += src.row(yy)[xx * 3 + c];
            for (int c = 0; c < 3; ++c) out.row(y)[x * 3 + c] = uint8_t((sum[c] + n / 2) / std::max(n, 1u));
        }
    }
    return out;
}

// Jacobi eigen-decomposition of a symmetric 4x4 matrix; returns the eigenvector of the largest eigenvalue.
std::array<double, 4> top_eigenvector(double A[4][4]) {
    double V[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int sweep = 0; sweep < 50; ++sweep) {
        double off = 0;
        for (int p = 0; p < 4; ++p) for (int q = p + 1; q < 4; ++q) off += A[p][q] * A[p][q];
        if (off < 1e-20) break;
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q) {
                if (std::fabs(A[p][q]) < 1e-30) continue;
                double th = (A[q][q] - A[p][p]) / (2 * A[p][q]);
                double t = (th >= 0 ? 1 : -1) / (std::fabs(th) + std::sqrt(th * th + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 4; ++k) {
                    double akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq;
                    A[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 4; ++k) {
                    double apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c * apk - s * aqk;
                    A[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 4; ++k) {
                    double vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
    }
    int best = 0;
    for (int i = 1; i < 4; ++i) if (A[i][i] > A[best][best]) best = i;
    return {V[0][best], V[1][best], V[2][best], V[3][best]};
}

}  // namespace

FaceMesh::FaceMesh(Ort::Env& env, const std::wstring& dir)
    : det_(env, dir + L"\\face_detector.onnx", 2), lmk_(env, dir + L"\\face_landmarks_detector.onnx", 2) {
    // SSD anchors: strides 8,16,16,16 with 2 anchors each; equal strides share one grid
    const int strides[] = {8, 16, 16, 16};
    for (int i = 0; i < 4;) {
        int s = strides[i], n = 0;
        while (i < 4 && strides[i] == s) { n += 2; ++i; }
        int g = 128 / s;
        for (int y = 0; y < g; ++y)
            for (int x = 0; x < g; ++x)
                for (int k = 0; k < n; ++k) anchors_.push_back({float((x + 0.5) / g), float((y + 0.5) / g)});
    }
    std::ifstream f(dir + L"\\canonical_face.bin", std::ios::binary);
    std::vector<float> raw(468 * 4);
    if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size() * sizeof(float))) throw std::runtime_error("canonical_face.bin");
    for (int i = 0; i < 468; ++i) {
        canon_.push_back({raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]});
        canon_w_.push_back(raw[468 * 3 + i]);
    }
}

std::optional<Roi> FaceMesh::detect(const Image& bgr) {
    double s = 128.0 / std::max(bgr.w, bgr.h);
    int nw = int(std::lround(bgr.w * s)), nh = int(std::lround(bgr.h * s));
    int px = (128 - nw) / 2, py = (128 - nh) / 2;
    Image small = resize_area(bgr, nw, nh);
    std::vector<float> in(128 * 128 * 3, -1.0f);  // letterbox padding = black = -1
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            const uint8_t* p = small.row(y) + x * 3;
            float* d = &in[((y + py) * 128 + (x + px)) * 3];
            d[0] = p[2] / 127.5f - 1; d[1] = p[1] / 127.5f - 1; d[2] = p[0] / 127.5f - 1;  // RGB
        }
    auto out = det_.run(in.data(), {1, 128, 128, 3});
    const auto& reg = out[0].data;
    const auto& cls = out[1].data;
    int best = -1;
    float best_score = 0.5f;
    for (int i = 0; i < 896; ++i) {
        float sc = 1.0f / (1.0f + std::exp(-std::clamp(cls[i], -100.0f, 100.0f)));
        if (sc > best_score) { best_score = sc; best = i; }
    }
    if (best < 0) return std::nullopt;
    const float* r = &reg[best * 16];
    Pt a = anchors_[best];
    auto un = [&](double xn, double yn) { return Pt{float((xn * 128 - px) / s), float((yn * 128 - py) / s)}; };
    double cx = r[0] / 128.0 + a.x, cy = r[1] / 128.0 + a.y, bw = r[2] / 128.0, bh = r[3] / 128.0;
    Pt p0 = un(cx - bw / 2, cy - bh / 2), p1 = un(cx + bw / 2, cy + bh / 2);
    Pt e0 = un(r[4] / 128.0 + a.x, r[5] / 128.0 + a.y), e1 = un(r[6] / 128.0 + a.x, r[7] / 128.0 + a.y);
    return roi_from(e0, e1, p0.x, p0.y, p1.x, p1.y);
}

std::optional<Face> FaceMesh::landmarks(const Image& bgr, const Roi& roi) {
    Affine A = rect_affine(roi, 256);
    Image crop = warp_affine(bgr, A, 256, 256, /*replicate=*/false);
    std::vector<float> in(256 * 256 * 3);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const uint8_t* p = crop.row(y) + x * 3;
            float* d = &in[(y * 256 + x) * 3];
            d[0] = p[2] / 255.0f; d[1] = p[1] / 255.0f; d[2] = p[0] / 255.0f;
        }
    auto out = lmk_.run(in.data(), {1, 256, 256, 3});
    Face f;
    f.presence = 1.0f / (1.0f + std::exp(-out[1].data[0]));
    if (f.presence < 0.5f) return std::nullopt;
    Affine Ai = invert(A);
    const auto& lm = out[0].data;
    f.pts.resize(478);
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (int i = 0; i < 478; ++i) {
        Pt q = xform(Ai, Pt{lm[i * 3], lm[i * 3 + 1]});
        f.pts[i] = {q.x, q.y, float(lm[i * 3 + 2] * roi.size / 256.0)};
        mnx = std::min(mnx, q.x); mny = std::min(mny, q.y); mxx = std::max(mxx, q.x); mxy = std::max(mxy, q.y);
    }
    f.x0 = int(mnx); f.y0 = int(mny); f.x1 = int(mxx); f.y1 = int(mxy);
    return f;
}

void FaceMesh::pose(Face& f) const {
    // weighted rigid fit canonical -> observed (x, -y, -z); Horn's quaternion method
    double ws = 0, ms[3] = {}, mo[3] = {};
    for (int i = 0; i < 468; ++i) {
        double w = canon_w_[i];
        if (w <= 0) continue;
        double o[3] = {f.pts[i][0], -f.pts[i][1], -f.pts[i][2]};
        for (int k = 0; k < 3; ++k) { ms[k] += w * canon_[i][k]; mo[k] += w * o[k]; }
        ws += w;
    }
    for (int k = 0; k < 3; ++k) { ms[k] /= ws; mo[k] /= ws; }
    double S[3][3] = {};
    for (int i = 0; i < 468; ++i) {
        double w = canon_w_[i];
        if (w <= 0) continue;
        double a[3] = {canon_[i][0] - ms[0], canon_[i][1] - ms[1], canon_[i][2] - ms[2]};
        double b[3] = {f.pts[i][0] - mo[0], -f.pts[i][1] - mo[1], -f.pts[i][2] - mo[2]};
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) S[r][c] += w * a[r] * b[c];
    }
    double N[4][4] = {
        {S[0][0] + S[1][1] + S[2][2], S[1][2] - S[2][1], S[2][0] - S[0][2], S[0][1] - S[1][0]},
        {S[1][2] - S[2][1], S[0][0] - S[1][1] - S[2][2], S[0][1] + S[1][0], S[2][0] + S[0][2]},
        {S[2][0] - S[0][2], S[0][1] + S[1][0], -S[0][0] + S[1][1] - S[2][2], S[1][2] + S[2][1]},
        {S[0][1] - S[1][0], S[2][0] + S[0][2], S[1][2] + S[2][1], -S[0][0] - S[1][1] + S[2][2]}};
    auto q = top_eigenvector(N);
    double w = q[0], x = q[1], y = q[2], z = q[3];
    double R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                      {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                      {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    const double deg = 180.0 / kPi;
    f.pitch = float(std::atan2(R[2][1], R[2][2]) * deg);
    f.yaw = float(std::asin(-std::clamp(R[2][0], -1.0, 1.0)) * deg);
    f.roll = float(std::atan2(R[1][0], R[0][0]) * deg);
}

std::optional<Face> FaceMesh::process(const Image& bgr) {
    if (!roi_) {
        roi_ = detect(bgr);
        if (!roi_) return std::nullopt;
    }
    auto f = landmarks(bgr, *roi_);
    if (!f) { roi_.reset(); return std::nullopt; }
    roi_ = roi_from(f->p(33), f->p(263), f->x0, f->y0, f->x1, f->y1);
    pose(*f);
    return f;
}

}  // namespace fg
