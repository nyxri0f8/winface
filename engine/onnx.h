// FaceGate engine - thin ONNX Runtime wrapper (CPU only, for battery).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace fg {

struct Tensor {
    std::vector<float> data;
    std::vector<int64_t> shape;
};

class Model {
public:
    Model(Ort::Env& env, const std::wstring& path, int threads);
    // single float input -> all outputs
    std::vector<Tensor> run(const float* input, const std::vector<int64_t>& shape);

private:
    std::unique_ptr<Ort::Session> s_;
    std::string in_name_;
    std::vector<std::string> out_names_;
};

}  // namespace fg
