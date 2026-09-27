// FaceGate engine - unlock decision state machine (1:1 port of bench/fg/decide.py).
#pragma once
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "recog.h"

namespace fg {

struct Params {
    float match = 0.42f;         // LFW calibration: best of 13k strangers scored 0.313
    int need = 3, window = 5;    // frames that must match, out of the last `window`
    float texture = 0.60f;       // median MiniFASNet real-probability
    int width_min = 170, width_max = 330;  // face width px at 1280x720
    float turn_deg = 12, wrong_way_deg = 15, frontal_deg = 8;
    double nonplanar_min = 0.025;
    double search_timeout_ms = 7000, challenge_timeout_ms = 3000;
};

enum class State { Search, Challenge, Unlock, Fail };

struct Status {
    State state = State::Search;
    std::string hint, reason, profile;
    int direction = 0;          // -1 = LEFT, +1 = RIGHT during the challenge
    float progress = 0;         // 0..1 for the ring animation
    float score = 0, texture = 0, texture_med = 0, yaw = 0, turn = 0;
    double nonplanar = 0;
    int width = 0;
};

class Engine {
public:
    Engine(Recognizer& rec, Texture& tex, std::map<std::string, std::vector<Embedding>> profiles, Params p = {});
    void reset(double now_ms);
    // one camera frame; `faces` sorted largest first (the engine only uses the nearest face)
    const Status& step(const Image& frame, const std::vector<Face>& faces, double now_ms);
    const Status& status() const { return st_; }

private:
    const Status& fail(const std::string& why);
    const Status& timeouts(double now);

    Recognizer& rec_;
    Texture& tex_;
    std::map<std::string, std::vector<Embedding>> profiles_;
    Params p_;
    Status st_;
    std::deque<bool> hits_;
    std::deque<float> texs_;
    double t_start_ = 0, t_challenge_ = 0;
    float yaw0_ = 0;
    std::vector<Pt> pts0_;
    double w0_ = 1;
    // diagnostics for "why didn't it match" (logged on timeout)
    int n_face_ = 0, n_in_ = 0, n_far_ = 0, n_close_ = 0, n_side_ = 0;
    float best_score_ = -1, tex_at_best_ = 0;
};

// indices used for the 3D parallax test (face oval + nose ridge + eyes + mouth corners)
const std::vector<int>& parallax_idx();

}  // namespace fg
