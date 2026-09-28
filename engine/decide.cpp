#include "decide.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

namespace fg {
namespace {

float median(std::deque<float> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n == 0 ? 0 : (n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]));
}

// unpredictable value in [0, n) from the OS CSPRNG
int random_below(int n) {
    unsigned int v = 0;
    BCryptGenRandom(nullptr, (PUCHAR)&v, sizeof v, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return int(v % unsigned(n));
}

float dist(const Face& f, int a, int b) {
    float dx = f.pts[a][0] - f.pts[b][0], dy = f.pts[a][1] - f.pts[b][1];
    return std::sqrt(dx * dx + dy * dy);
}

float median_of(std::deque<float> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

// unpredictable challenge direction from the OS CSPRNG
int random_direction() {
    unsigned char b = 0;
    BCryptGenRandom(nullptr, &b, 1, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return (b & 1) ? 1 : -1;
}

std::vector<Pt> pick(const Face& f) {
    std::vector<Pt> out;
    for (int i : parallax_idx()) out.push_back(f.p(i));
    return out;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[160];
    snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

}  // namespace

const std::vector<int>& parallax_idx() {
    static const std::vector<int> idx = {
        10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152,
        148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
        1, 4, 5, 6, 168, 197, 195, 2, 33, 133, 362, 263, 61, 291, 13, 14};
    return idx;
}

// MediaPipe face mesh indices: eye corners + upper/lower lids, inner lips + mouth corners
float eye_openness(const Face& f) {
    if (f.pts.size() < 468) return 0;
    auto ear = [&](int c0, int c1, int u0, int l0, int u1, int l1) {
        float w = dist(f, c0, c1);
        return w > 1 ? (dist(f, u0, l0) + dist(f, u1, l1)) / (2 * w) : 0.f;
    };
    return 0.5f * (ear(33, 133, 160, 144, 158, 153) + ear(263, 362, 387, 373, 385, 380));
}

float mouth_openness(const Face& f) {
    if (f.pts.size() < 468) return 0;
    float w = dist(f, 78, 308);
    return w > 1 ? dist(f, 13, 14) / w : 0.f;
}

std::array<float, 3> skin_rgb(const Image& bgr, const Face& f) {
    std::array<float, 3> out{0, 0, 0};
    if (f.pts.size() < 468 || bgr.ch != 3) return out;
    const int r = std::max(2, f.width() / 30);   // small patches on skin that rarely moves or shadows
    double sum[3] = {0, 0, 0};
    long n = 0;
    for (int i : {151, 108, 337, 50, 280, 205, 425}) {
        int cx = int(f.pts[i][0]), cy = int(f.pts[i][1]);
        for (int y = std::max(0, cy - r); y <= std::min(bgr.h - 1, cy + r); ++y) {
            const uint8_t* row = bgr.row(y);
            for (int x = std::max(0, cx - r); x <= std::min(bgr.w - 1, cx + r); ++x) {
                sum[0] += row[x * 3 + 2]; sum[1] += row[x * 3 + 1]; sum[2] += row[x * 3];   // BGR -> RGB
                ++n;
            }
        }
    }
    if (n) for (int c = 0; c < 3; ++c) out[c] = float(sum[c] / n);
    return out;
}

bool flash_response_ok(const std::array<float, 3>& before, const std::array<float, 3>& during, int rgb, float min_excess,
                       float* excess) {
    int ch = (rgb >> 16 & 0xFF) ? 0 : (rgb >> 8 & 0xFF) ? 1 : 2;   // the flashes are pure red, green or blue
    float rel[3];
    for (int c = 0; c < 3; ++c) rel[c] = before[c] > 1 ? during[c] / before[c] - 1 : 0;
    float others = 0.5f * (rel[(ch + 1) % 3] + rel[(ch + 2) % 3]);
    float ex = rel[ch] - others;
    if (excess) *excess = ex;
    // a lit face gets brighter in the flashed colour than in the other two; a phone screen or a deepfake that does
    // not know the colour does not
    return ex >= min_excess && rel[ch] >= rel[(ch + 1) % 3] && rel[ch] >= rel[(ch + 2) % 3];
}

int flash_colour_at(double t0, const int colours[2], double now) {
    double t = now - t0;
    if (t >= kFlashBase && t < kFlashBase + kFlashOn) return colours[0];
    double t2 = kFlashBase + kFlashOn + kFlashGap;
    if (t >= t2 && t < t2 + kFlashOn) return colours[1];
    return -1;
}

Engine::Engine(Recognizer& rec, Texture& tex, std::map<std::string, std::vector<Embedding>> profiles, Params p)
    : rec_(rec), tex_(tex), profiles_(std::move(profiles)), p_(p) {}

void Engine::reset(double now) {
    st_ = Status{};
    hits_.clear();
    texs_.clear();
    t_start_ = now;
    n_face_ = n_in_ = n_far_ = n_close_ = n_side_ = 0;
    best_score_ = -1;
    tex_at_best_ = 0;
    t_stage_ = now;
    flash_done_ = false;
    flash_samples_.clear();
    ears_.clear();
    mars_.clear();
    closed_seen_ = false;
}

const Status& Engine::fail(const std::string& why) {
    st_.state = State::Fail;
    st_.reason = why;
    st_.hint = "Not recognised";
    return st_;
}

const Status& Engine::timeouts(double now) {
    if (st_.state == State::Search && now - t_start_ > p_.search_timeout_ms && hold_) {
        // nobody lifted the curtain: not an attempt, the scan simply ends (a key press starts a fresh one)
        return fail("idle: waited behind the lock screen, no key pressed");
    }
    if (st_.state == State::Search && now - t_start_ > p_.search_timeout_ms) {
        char b[240];
        snprintf(b, sizeof b, "no confident match in time (frames: face %d, in range %d, too far %d, too close %d, not frontal %d;"
                 " best score %.2f, texture there %.2f)", n_face_, n_in_, n_far_, n_close_, n_side_, best_score_, tex_at_best_);
        // never reached scanning distance -> not a failed attempt (doesn't use up one of the 3 tries)
        return fail(n_in_ == 0 ? std::string("idle: ") + b : std::string(b));
    }
    if (st_.state == State::Challenge && st_.stage == kStageFlash && now - st_.flash_t0 > kFlashEnd + 1500)
        return fail("hold still during the screen flash");
    if (st_.state == State::Challenge && st_.stage == kStageTurn && now - t_stage_ > p_.challenge_timeout_ms)
        return fail("challenge not completed in time");
    if (st_.state == State::Challenge && st_.stage == kStageAction && now - t_stage_ > p_.challenge_timeout_ms)
        return fail(st_.action == kActionBlink ? "blink not seen in time" : "mouth not opened in time");
    return st_;
}

const Status& Engine::step(const Image& frame, const std::vector<Face>& faces, double now, double frame_ts) {
    if (frame_ts < 0) frame_ts = now;
    // the flash timeline runs on its own clock, even while no face is in view
    if (st_.state == State::Challenge && st_.stage == kStageFlash && now - st_.flash_t0 >= kFlashEnd)
        return finish_flash(now, faces.empty() ? nullptr : &faces[0], faces.empty() ? 0 : faces[0].width());
    auto push_hit = [&](bool h) { hits_.push_back(h); if ((int)hits_.size() > p_.window) hits_.pop_front(); };
    if (st_.state == State::Unlock || st_.state == State::Fail) return st_;
    if (faces.empty()) { st_.hint = "Looking for you..."; push_hit(false); return timeouts(now); }
    const Face& f = faces[0];
    int w = f.width();
    ++n_face_;
    if (faces.size() > 1 && faces[1].width() > 0.6 * w) { st_.hint = "More than one person in view"; push_hit(false); return timeouts(now); }
    if (w < p_.width_min || w > p_.width_max) {
        st_.hint = w < p_.width_min ? "Move closer" : "Move back a little";
        ++(w < p_.width_min ? n_far_ : n_close_);
        push_hit(false);
        return timeouts(now);
    }
    ++n_in_;

    Embedding v = rec_.embed(frame, f);
    std::string best;
    float score = -1;
    for (auto& [name, t] : profiles_) {
        float s = match_score(t, v);
        if (s > score) { score = s; best = name; }
    }
    float tx = tex_.real_prob(frame, f);
    texs_.push_back(tx);
    if ((int)texs_.size() > p_.window) texs_.pop_front();
    push_hit(score >= p_.match);
    float tex_med = median(texs_);
    int nhits = int(std::count(hits_.begin(), hits_.end(), true));
    st_.score = score; st_.texture = tx; st_.texture_med = tex_med; st_.width = w; st_.yaw = f.yaw; st_.profile = best;
    if (score > best_score_) { best_score_ = score; tex_at_best_ = tex_med; }

    // openness history while the face is steady (baseline for the blink / mouth action)
    if (st_.stage != kStageAction) {
        ears_.push_back(eye_openness(f));
        mars_.push_back(mouth_openness(f));
        if (ears_.size() > 12) { ears_.pop_front(); mars_.pop_front(); }
    }

    if (st_.state == State::Search) {
        st_.hint = "Scanning...";
        st_.progress = 0.5f * std::min(1.0f, float(nhits) / p_.need);
        bool ready = nhits >= p_.need && (int)texs_.size() >= p_.need && tex_med >= p_.texture;
        st_.waiting = ready && hold_;
        if (ready && hold_) {
            st_.hint = "Face found";   // hidden: wait for the curtain to lift before asking for anything
        } else if (ready && std::fabs(f.yaw) > p_.frontal_deg) {
            st_.hint = "Look straight at the screen";
            ++n_side_;
        } else if (ready && p_.flash_mode > 0 && !flash_done_) {
            // two different random colours (red / green / blue), unpredictable to a replay or a live deepfake
            static const int kColours[3] = {0xFF0000, 0x00FF00, 0x0000FF};
            int a = random_below(3), b = (a + 1 + random_below(2)) % 3;
            st_.state = State::Challenge;
            st_.stage = kStageFlash;
            st_.flash_rgb[0] = kColours[a];
            st_.flash_rgb[1] = kColours[b];
            st_.flash_t0 = now + 50;   // give the display a moment
            t_stage_ = now;
            lows_ = 0;
            flash_samples_.clear();
            st_.hint = "Hold still";
        } else if (ready) {
            start_turn(f, w, now);
        }
        return timeouts(now);
    }

    // every challenge stage: the same person must stay in front of the camera
    const float keep = std::min(p_.match, std::max(p_.match - p_.challenge_floor_drop, p_.challenge_floor_min));
    const int lows_allowed = p_.challenge_lows_allowed + (st_.stage == kStageAction ? 1 : 0);   // closed eyes / open mouth
    if (score < keep) {
        if (++lows_ > lows_allowed) return fail(fmt("identity lost during challenge (%.2f)", score));
        return timeouts(now);   // forgiven once (motion blur) - but never the frame that completes a challenge
    }
    if (tx < 0.3f && tex_med < p_.texture) return fail(fmt("texture looks fake (%.2f)", tex_med));

    if (st_.stage == kStageFlash) {
        st_.progress = 0.5f + 0.1f * float(std::min(1.0, (now - st_.flash_t0) / kFlashEnd));
        flash_samples_.push_back({frame_ts, skin_rgb(frame, f)});
        return timeouts(now);
    }

    if (st_.stage == kStageTurn) {
        float d = f.yaw - yaw0_;
        float want = float(st_.direction);
        double np = nonplanarity(pts0_, pick(f), w0_);
        st_.nonplanar = std::max(st_.nonplanar, np);
        st_.turn = d;
        st_.progress = 0.6f + 0.25f * std::min(std::max(d * want, 0.0f) / p_.turn_deg, 1.0f);
        if (d * want <= -p_.wrong_way_deg) return fail("turned the wrong way");
        if (d * want >= p_.turn_deg) {
            if (st_.nonplanar < p_.nonplanar_min) return fail(fmt("turn looked flat (%.2f%%) - photo/screen?", st_.nonplanar * 100));
            turn_desc_ = fmt("turn %+.0f deg, 3D %.1f%%, texture %.2f", d, st_.nonplanar * 100, tex_med);
            if (!p_.action) return unlock(turn_desc_);
            // second random action; baselines from the frames before it
            st_.stage = kStageAction;
            st_.direction = 0;
            st_.action = random_below(2) ? kActionBlink : kActionMouth;
            ear0_ = std::max(0.12f, median_of(ears_));
            mar0_ = median_of(mars_);
            closed_seen_ = false;
            lows_ = 0;
            t_stage_ = now;
            st_.hint = st_.action == kActionBlink ? "Now blink slowly" : "Now open your mouth";
        }
        return timeouts(now);
    }

    // kStageAction
    st_.progress = 0.85f;
    if (st_.action == kActionBlink) {
        float e = eye_openness(f);
        if (e < ear0_ * p_.blink_drop) closed_seen_ = true;
        if (closed_seen_) st_.progress = 0.93f;
        if (closed_seen_ && e > ear0_ * 0.8f) return unlock(turn_desc_ + fmt(", blink %.2f->closed", ear0_));
    } else {
        float m = mouth_openness(f);
        if (m > std::max(p_.mouth_open, mar0_ + 0.2f)) return unlock(turn_desc_ + fmt(", mouth %.2f->%.2f", mar0_, m));
    }
    return timeouts(now);
}

void Engine::start_turn(const Face& f, int w, double now) {
    st_.state = State::Challenge;
    st_.stage = kStageTurn;
    st_.direction = random_direction();
    t_stage_ = t_challenge_ = now;
    yaw0_ = f.yaw;
    pts0_ = pick(f);
    w0_ = w;
    lows_ = 0;
    st_.nonplanar = 0;
    st_.hint = st_.direction < 0 ? "Turn your head slightly LEFT" : "Turn your head slightly RIGHT";
}

// Compare the skin colour before the flashes with the colour while each one was on screen. Frames reach us a
// little after they were captured, so each window starts 120 ms after the colour appeared.
const Status& Engine::finish_flash(double now, const Face* f, int w) {
    flash_done_ = true;
    auto mean = [&](double a, double b, int& n) {
        std::array<float, 3> m{0, 0, 0};
        n = 0;
        for (auto& s : flash_samples_)
            if (s.t >= a && s.t < b) { for (int c = 0; c < 3; ++c) m[c] += s.rgb[c]; ++n; }
        if (n) for (auto& v : m) v /= float(n);
        return m;
    };
    const double t0 = st_.flash_t0, on1 = t0 + kFlashBase, on2 = on1 + kFlashOn + kFlashGap;
    int nb = 0, n1 = 0, n2 = 0;
    auto base = mean(t0 - 400, on1 + 60, nb);
    auto d1 = mean(on1 + 120, on1 + kFlashOn + 80, n1);
    auto d2 = mean(on2 + 120, on2 + kFlashOn + 80, n2);
    auto name = [](int rgb) { return rgb == 0xFF0000 ? "R" : rgb == 0x00FF00 ? "G" : "B"; };
    bool ok = false;
    if (nb && n1 && n2) {
        float e1 = 0, e2 = 0;
        bool ok1 = flash_response_ok(base, d1, st_.flash_rgb[0], p_.flash_min, &e1);
        bool ok2 = flash_response_ok(base, d2, st_.flash_rgb[1], p_.flash_min, &e2);
        ok = ok1 && ok2;
        char b[160];
        snprintf(b, sizeof b, "flash %s %+.1f%% %s %+.1f%% %s (frames %d/%d/%d)", name(st_.flash_rgb[0]), e1 * 100,
                 name(st_.flash_rgb[1]), e2 * 100, ok ? "pass" : "FAIL", nb, n1, n2);
        st_.flash_report = b;
    } else {
        st_.flash_report = fmt("flash: too few frames (%.0f/%.0f/%.0f)", nb, n1, n2);
    }
    if (p_.flash_mode == 2 && !ok) return fail("screen reflection did not match a real face - photo or screen?");
    if (!f) { st_.state = State::Search; st_.stage = kStageSearch; st_.hint = "Looking for you..."; return st_; }
    start_turn(*f, w, now);
    return st_;
}

const Status& Engine::unlock(const std::string& how) {
    st_.state = State::Unlock;
    st_.progress = 1;
    st_.hint = "Unlocked";
    st_.reason = st_.profile + ": " + how + (st_.flash_report.empty() ? "" : ", " + st_.flash_report);
    return st_;
}

}  // namespace fg
