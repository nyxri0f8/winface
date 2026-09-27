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
}

const Status& Engine::fail(const std::string& why) {
    st_.state = State::Fail;
    st_.reason = why;
    st_.hint = "Not recognised";
    return st_;
}

const Status& Engine::timeouts(double now) {
    if (st_.state == State::Search && now - t_start_ > p_.search_timeout_ms) {
        char b[240];
        snprintf(b, sizeof b, "no confident match in time (frames: face %d, in range %d, too far %d, too close %d, not frontal %d;"
                 " best score %.2f, texture there %.2f)", n_face_, n_in_, n_far_, n_close_, n_side_, best_score_, tex_at_best_);
        // never reached scanning distance -> not a failed attempt (doesn't use up one of the 3 tries)
        return fail(n_in_ == 0 ? std::string("idle: ") + b : std::string(b));
    }
    if (st_.state == State::Challenge && now - t_challenge_ > p_.challenge_timeout_ms) return fail("challenge not completed in time");
    return st_;
}

const Status& Engine::step(const Image& frame, const std::vector<Face>& faces, double now) {
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

    if (st_.state == State::Search) {
        st_.hint = "Scanning...";
        st_.progress = 0.5f * std::min(1.0f, float(nhits) / p_.need);
        bool ready = nhits >= p_.need && (int)texs_.size() >= p_.need && tex_med >= p_.texture;
        if (ready && std::fabs(f.yaw) > p_.frontal_deg) {
            st_.hint = "Look straight at the screen";
            ++n_side_;
        } else if (ready) {
            st_.state = State::Challenge;
            st_.direction = random_direction();
            t_challenge_ = now;
            yaw0_ = f.yaw;
            pts0_ = pick(f);
            w0_ = w;
            st_.nonplanar = 0;
            st_.hint = st_.direction < 0 ? "Turn your head slightly LEFT" : "Turn your head slightly RIGHT";
        }
        return timeouts(now);
    }

    // Challenge
    float d = f.yaw - yaw0_;
    float want = float(st_.direction);
    double np = nonplanarity(pts0_, pick(f), w0_);
    st_.nonplanar = std::max(st_.nonplanar, np);
    st_.turn = d;
    st_.progress = 0.5f + 0.5f * std::min(std::max(d * want, 0.0f) / p_.turn_deg, 1.0f);
    if (d * want <= -p_.wrong_way_deg) return fail("turned the wrong way");
    if (score < p_.match) return fail(fmt("identity lost during challenge (%.2f)", score));
    if (tx < 0.3f && tex_med < p_.texture) return fail(fmt("texture looks fake (%.2f)", tex_med));
    if (d * want >= p_.turn_deg) {
        if (st_.nonplanar < p_.nonplanar_min) return fail(fmt("turn looked flat (%.2f%%) - photo/screen?", st_.nonplanar * 100));
        st_.state = State::Unlock;
        st_.progress = 1;
        st_.hint = "Unlocked";
        st_.reason = best + fmt(": turn %+.0f deg, 3D %.1f%%, texture %.2f", d, st_.nonplanar * 100, tex_med);
    }
    return timeouts(now);
}

}  // namespace fg
