#include "onnx.h"

namespace fg {

Model::Model(Ort::Env& env, const std::wstring& path, int threads) {
    Ort::SessionOptions so;
    so.SetIntraOpNumThreads(threads);
    so.SetInterOpNumThreads(1);
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    so.DisableMemPattern();  // lower peak RAM; inputs are tiny
    // battery: worker threads sleep between runs instead of busy-spinning at 100% CPU
    so.AddConfigEntry("session.intra_op.allow_spinning", "0");
    so.AddConfigEntry("session.inter_op.allow_spinning", "0");
    s_ = std::make_unique<Ort::Session>(env, path.c_str(), so);
    Ort::AllocatorWithDefaultOptions alloc;
    in_name_ = s_->GetInputNameAllocated(0, alloc).get();
    for (size_t i = 0; i < s_->GetOutputCount(); ++i) out_names_.push_back(s_->GetOutputNameAllocated(i, alloc).get());
}

std::vector<Tensor> Model::run(const float* input, const std::vector<int64_t>& shape) {
    size_t n = 1;
    for (auto d : shape) n *= size_t(d);
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value in = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(input), n, shape.data(), shape.size());
    const char* in_names[] = {in_name_.c_str()};
    std::vector<const char*> outs;
    for (auto& s : out_names_) outs.push_back(s.c_str());
    auto res = s_->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, outs.data(), outs.size());
    std::vector<Tensor> ret;
    for (auto& v : res) {
        auto info = v.GetTensorTypeAndShapeInfo();
        const float* p = v.GetTensorData<float>();
        ret.push_back({std::vector<float>(p, p + info.GetElementCount()), info.GetShape()});
    }
    return ret;
}

}  // namespace fg
