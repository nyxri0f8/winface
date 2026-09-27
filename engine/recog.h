// FaceGate engine - ArcFace INT8 recognizer + MiniFASNet texture liveness.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "facemesh.h"

namespace fg {

using Embedding = std::array<float, 512>;

class Recognizer {
public:
    Recognizer(Ort::Env& env, const std::wstring& model_dir);
    Embedding embed(const Image& bgr, const Face& f);
    static Image align112(const Image& bgr, const Face& f);  // 5-point similarity to the ArcFace template

private:
    Model m_;
};

// mean of the 3 best similarities against a profile's enrolled templates (same as the bench)
float match_score(const std::vector<Embedding>& templates, const Embedding& v);

class Texture {
public:
    Texture(Ort::Env& env, const std::wstring& model_dir);
    float real_prob(const Image& bgr, const Face& f);  // 0..1, averaged over both MiniFASNet models

private:
    Model v2_, v1se_;
};

}  // namespace fg
