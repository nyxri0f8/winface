// WinFace engine - unlock decision state machine. Search + head turn are a 1:1 port of bench/fg/decide.py; the flash
// check and the blink/mouth action exist only here (the lock screen needs the display for the flash).
#pragma once
#include <array>
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
    // During the head turn the pose lowers ArcFace scores and one motion-blurred frame can dip further, so the turn
    // only has to stay above a floor (still well above the best stranger, 0.313), with one bad frame forgiven.
    // Identity itself was already proven frontally (3 of 5 frames >= match) before the challenge started.
    float challenge_floor_drop = 0.08f, challenge_floor_min = 0.34f;
    int challenge_lows_allowed = 1;
    double search_timeout_ms = 7000, challenge_timeout_ms = 3000;
    // second random action after the head turn: blink or open the mouth (a replay needs two random actions)
    bool action = true;
    float blink_drop = 0.60f;     // eyes count as closed below 60 % of their normal openness
    float mouth_open = 0.35f;     // inner-lip gap / mouth width
    // screen-flash check: two random colours are flashed; real skin must reflect each one at the right moment.
    // 0 off, 1 measure only (logged, never blocks), 2 enforce
    int flash_mode = 0;
    float flash_min = 0.015f;     // the flashed colour channel must rise >= 1.5 % more than the other two
};

// flash timeline (ms from Status::flash_t0): baseline, colour 1, gap, colour 2, tail for late frames
constexpr double kFlashBase = 300, kFlashOn = 400, kFlashGap = 300, kFlashEnd = 1550;
// the colour the screen must show at `now_ms` for this status (0xRRGGBB), or -1 for none
int flash_colour_at(double flash_t0, const int colours[2], double now_ms);

enum class State { Search, Challenge, Unlock, Fail };
// what the active challenge asks for (Status::stage)
enum Stage { kStageSearch = 0, kStageFlash = 1, kStageTurn = 2, kStageAction = 3 };
enum Action { kActionNone = 0, kActionBlink = 1, kActionMouth = 2 };

struct Status {
    State state = State::Search;
    std::string hint, reason, profile;
    int direction = 0;          // -1 = LEFT, +1 = RIGHT during the challenge
    float progress = 0;         // 0..1 overall scan progress (drives the HUD)
    bool waiting = false;       // held (prompt not visible) with an enrolled face already recognised
    float score = 0, texture = 0, texture_med = 0, yaw = 0, turn = 0;
    double nonplanar = 0;
    int width = 0;
    int stage = kStageSearch;
    int action = kActionNone;   // during kStageAction
    double flash_t0 = 0;        // during kStageFlash: timeline start (same clock as step's now_ms)
    int flash_rgb[2] = {0, 0};
    std::string flash_report;   // set once the flash was evaluated, e.g. "flash R +3.1% B +2.4% pass"
};

// measurements used by the challenges (exposed for tests)
float eye_openness(const Face& f);    // eye aspect ratio, both eyes averaged (~0.25-0.35 open, <0.15 closed)
float mouth_openness(const Face& f);  // inner-lip gap / mouth width (~0.0-0.1 closed, >0.4 open)
std::array<float, 3> skin_rgb(const Image& bgr, const Face& f);   // mean R, G, B of forehead + cheek patches
// did the skin reflect the flashed colour? rel = per-channel relative change (during / before - 1)
bool flash_response_ok(const std::array<float, 3>& before, const std::array<float, 3>& during, int rgb, float min_excess,
                       float* excess = nullptr);

class Engine {
public:
    Engine(Recognizer& rec, Texture& tex, std::map<std::string, std::vector<Embedding>> profiles, Params p = {});
    void reset(double now_ms);
    // one camera frame; `faces` sorted largest first (the engine only uses the nearest face)
    // frame_ts: when the camera delivered the frame (same clock); used to line frames up with the flash
    const Status& step(const Image& frame, const std::vector<Face>& faces, double now_ms, double frame_ts = -1);
    // hold: the user cannot see the prompt yet (lock-screen curtain) - recognise, but do not start the head turn
    void set_hold(bool hold) { hold_ = hold; }
    const Status& status() const { return st_; }

private:
    const Status& fail(const std::string& why);
    const Status& timeouts(double now);
    void start_turn(const Face& f, int w, double now);
    const Status& finish_flash(double now, const Face* f, int w);
    const Status& unlock(const std::string& how);

    double t_stage_ = 0;                 // when the current challenge stage started
    bool flash_done_ = false;
    struct Sample { double t; std::array<float, 3> rgb; };
    std::vector<Sample> flash_samples_;
    std::deque<float> ears_, mars_;      // recent openness, for the action baseline
    float ear0_ = 0.3f, mar0_ = 0.05f;
    bool closed_seen_ = false;
    std::string turn_desc_;

    Recognizer& rec_;
    Texture& tex_;
    std::map<std::string, std::vector<Embedding>> profiles_;
    Params p_;
    Status st_;
    std::deque<bool> hits_;
    std::deque<float> texs_;
    double t_start_ = 0, t_challenge_ = 0;
    float yaw0_ = 0;
    int lows_ = 0;       // challenge frames below the floor
    bool hold_ = false;
    std::vector<Pt> pts0_;
    double w0_ = 1;
    // diagnostics for "why didn't it match" (logged on timeout)
    int n_face_ = 0, n_in_ = 0, n_far_ = 0, n_close_ = 0, n_side_ = 0;
    float best_score_ = -1, tex_at_best_ = 0;
};

// indices used for the 3D parallax test (face oval + nose ridge + eyes + mouth corners)
const std::vector<int>& parallax_idx();

}  // namespace fg
