// WinFace engine - small geometry toolkit (replaces the OpenCV calls used by the Python bench).
#pragma once
#include <array>
#include <vector>

#include "image.h"

namespace fg {

struct Pt { float x, y; };
using Affine = std::array<double, 6>;   // [a b c; d e f]: x' = a x + b y + c, y' = d x + e y + f
using Homog = std::array<double, 9>;    // row-major 3x3

Affine invert(const Affine& m);
Pt xform(const Affine& m, Pt p);

// Least-squares similarity (rotation + uniform scale + translation) mapping src -> dst (Umeyama).
Affine estimate_similarity(const Pt* src, const Pt* dst, int n);

// dst(x, y) = src(M^-1 (x, y)), bilinear. M maps src -> dst coordinates.
// replicate = true: edge pixels extend outward (ArcFace crop); false: black outside (landmark crop).
Image warp_affine(const Image& src, const Affine& m, int out_w, int out_h, bool replicate = true);

// Least-squares homography a -> b (normalised DLT, same idea as cv::findHomography(method=0)).
bool find_homography(const std::vector<Pt>& a, const std::vector<Pt>& b, Homog& h);
Pt xform(const Homog& h, Pt p);

// median of |H(a_i) - b_i| / width  -> ~0 for flat media, larger for a rotating real head
double nonplanarity(const std::vector<Pt>& a, const std::vector<Pt>& b, double width);

}  // namespace fg
