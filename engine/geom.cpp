#include "geom.h"

#include <algorithm>
#include <cmath>

namespace fg {

Affine invert(const Affine& m) {
    double det = m[0] * m[4] - m[1] * m[3];
    if (std::fabs(det) < 1e-12) return {1, 0, 0, 0, 1, 0};
    double a = m[4] / det, b = -m[1] / det, d = -m[3] / det, e = m[0] / det;
    return {a, b, -(a * m[2] + b * m[5]), d, e, -(d * m[2] + e * m[5])};
}

Pt xform(const Affine& m, Pt p) {
    return {float(m[0] * p.x + m[1] * p.y + m[2]), float(m[3] * p.x + m[4] * p.y + m[5])};
}

Affine estimate_similarity(const Pt* s, const Pt* d, int n) {
    double sx = 0, sy = 0, dx = 0, dy = 0;
    for (int i = 0; i < n; ++i) { sx += s[i].x; sy += s[i].y; dx += d[i].x; dy += d[i].y; }
    sx /= n; sy /= n; dx /= n; dy /= n;
    // closed form for 2D: minimise sum |[a -b; b a] (s - sc) + dc - d|^2
    double num_a = 0, num_b = 0, den = 0;
    for (int i = 0; i < n; ++i) {
        double px = s[i].x - sx, py = s[i].y - sy, qx = d[i].x - dx, qy = d[i].y - dy;
        num_a += px * qx + py * qy;
        num_b += px * qy - py * qx;
        den += px * px + py * py;
    }
    double a = num_a / den, b = num_b / den;
    return {a, -b, dx - (a * sx - b * sy), b, a, dy - (b * sx + a * sy)};
}

Image warp_affine(const Image& src, const Affine& m, int ow, int oh, bool replicate) {
    Image out(ow, oh, src.ch);
    Affine inv = invert(m);
    const int C = src.ch;
    // sample with a virtual 1-px border: replicate edges, or black (constant 0)
    auto px = [&](int xx, int yy, int c) -> double {
        if (xx < 0 || yy < 0 || xx >= src.w || yy >= src.h) {
            if (!replicate) return 0.0;
            xx = std::clamp(xx, 0, src.w - 1);
            yy = std::clamp(yy, 0, src.h - 1);
        }
        return src.row(yy)[xx * C + c];
    };
    for (int y = 0; y < oh; ++y) {
        uint8_t* o = out.row(y);
        for (int x = 0; x < ow; ++x) {
            double fx = inv[0] * x + inv[1] * y + inv[2];
            double fy = inv[3] * x + inv[4] * y + inv[5];
            int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
            double ax = fx - x0, ay = fy - y0;
            bool inside = x0 >= 0 && y0 >= 0 && x0 + 1 < src.w && y0 + 1 < src.h;
            for (int c = 0; c < C; ++c) {
                double v;
                if (inside) {  // fast path
                    const uint8_t *r0 = src.row(y0) + x0 * C, *r1 = src.row(y0 + 1) + x0 * C;
                    v = (r0[c] * (1 - ax) + r0[C + c] * ax) * (1 - ay) + (r1[c] * (1 - ax) + r1[C + c] * ax) * ay;
                } else {
                    v = (px(x0, y0, c) * (1 - ax) + px(x0 + 1, y0, c) * ax) * (1 - ay) +
                        (px(x0, y0 + 1, c) * (1 - ax) + px(x0 + 1, y0 + 1, c) * ax) * ay;
                }
                o[x * C + c] = uint8_t(std::lround(std::clamp(v, 0.0, 255.0)));
            }
        }
    }
    return out;
}

namespace {

// Hartley normalisation: centroid to origin, mean distance sqrt(2)
std::array<double, 3> norm_params(const std::vector<Pt>& p) {
    double cx = 0, cy = 0;
    for (auto& q : p) { cx += q.x; cy += q.y; }
    cx /= p.size(); cy /= p.size();
    double md = 0;
    for (auto& q : p) md += std::hypot(q.x - cx, q.y - cy);
    md /= p.size();
    return {cx, cy, md > 1e-9 ? std::sqrt(2.0) / md : 1.0};
}

// Solve the 8x8 system M x = r with partial-pivot Gaussian elimination.
bool solve8(double M[8][9]) {
    for (int c = 0; c < 8; ++c) {
        int piv = c;
        for (int r = c + 1; r < 8; ++r) if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
        if (std::fabs(M[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 9; ++k) std::swap(M[c][k], M[piv][k]);
        for (int r = 0; r < 8; ++r) {
            if (r == c) continue;
            double f = M[r][c] / M[c][c];
            for (int k = c; k < 9; ++k) M[r][k] -= f * M[c][k];
        }
    }
    for (int r = 0; r < 8; ++r) M[r][8] /= M[r][r];
    return true;
}

}  // namespace

bool find_homography(const std::vector<Pt>& a, const std::vector<Pt>& b, Homog& h) {
    if (a.size() < 4 || a.size() != b.size()) return false;
    auto na = norm_params(a), nb = norm_params(b);
    // normal equations of the linear system with h33 = 1 (on normalised coordinates)
    double M[8][9] = {};
    for (size_t i = 0; i < a.size(); ++i) {
        double x = (a[i].x - na[0]) * na[2], y = (a[i].y - na[1]) * na[2];
        double u = (b[i].x - nb[0]) * nb[2], v = (b[i].y - nb[1]) * nb[2];
        double r1[9] = {x, y, 1, 0, 0, 0, -u * x, -u * y, u};
        double r2[9] = {0, 0, 0, x, y, 1, -v * x, -v * y, v};
        for (double* r : {r1, r2})
            for (int j = 0; j < 8; ++j)
                for (int k = 0; k < 9; ++k) M[j][k] += r[j] * r[k];
    }
    if (!solve8(M)) return false;
    double hn[9];
    for (int i = 0; i < 8; ++i) hn[i] = M[i][8];
    hn[8] = 1;
    // de-normalise: H = Tb^-1 * Hn * Ta
    double Ta[9] = {na[2], 0, -na[2] * na[0], 0, na[2], -na[2] * na[1], 0, 0, 1};
    double Tbi[9] = {1 / nb[2], 0, nb[0], 0, 1 / nb[2], nb[1], 0, 0, 1};
    double t[9] = {}, r[9] = {};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) t[i * 3 + j] += hn[i * 3 + k] * Ta[k * 3 + j];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) r[i * 3 + j] += Tbi[i * 3 + k] * t[k * 3 + j];
    for (int i = 0; i < 9; ++i) h[i] = r[i] / r[8];
    return true;
}

Pt xform(const Homog& h, Pt p) {
    double w = h[6] * p.x + h[7] * p.y + h[8];
    return {float((h[0] * p.x + h[1] * p.y + h[2]) / w), float((h[3] * p.x + h[4] * p.y + h[5]) / w)};
}

double nonplanarity(const std::vector<Pt>& a, const std::vector<Pt>& b, double width) {
    Homog h;
    if (!find_homography(a, b, h)) return -1;
    std::vector<double> e(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        Pt q = xform(h, a[i]);
        e[i] = std::hypot(q.x - b[i].x, q.y - b[i].y);
    }
    std::nth_element(e.begin(), e.begin() + e.size() / 2, e.end());
    return e[e.size() / 2] / width;
}

}  // namespace fg
