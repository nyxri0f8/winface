#include "scanner.h"

#include <chrono>
#include <future>

#include "../engine/camera.h"
#include "../engine/profiles.h"
#include "common.h"
#include "intruder.h"
#include "secret.h"

namespace fgcp {
namespace {

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

Scanner::Scanner(Done on_done, const Config& cfg) : done_(std::move(on_done)), cfg_(cfg) {}
Scanner::~Scanner() { stop(); }

bool Scanner::ensure_models(std::string& err) {
    if (rec_) return true;
    try {
        std::wstring md = module_dir() + L"\\models";
        Ort::InitApi();  // ORT_API_MANUAL_INIT: first touch of onnxruntime.dll happens here, never in DllMain
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "winface");
        mesh_ = std::make_unique<fg::FaceMesh>(*env_, md);
        tex_ = std::make_unique<fg::Texture>(*env_, md);
        rec_ = std::make_unique<fg::Recognizer>(*env_, md);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        mesh_.reset(); tex_.reset(); rec_.reset(); env_.reset();
        return false;
    }
}

void Scanner::start() {
    if (running_) return;
    if (th_.joinable()) th_.join();
    stop_ = false;
    running_ = true;
    th_ = std::thread(&Scanner::run, this);
}

void Scanner::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
}

Snapshot Scanner::snapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

void Scanner::run() {
    auto set = [&](auto fn) { std::lock_guard<std::mutex> lk(mu_); fn(snap_); };
    set([](Snapshot& s) { s = Snapshot{}; s.running = true; });
    const double t0 = now_ms();

    // camera and models start in parallel (camera is the slow part)
    fg::Camera cam(cfg_.camera);  // virtual / non-USB cameras are always refused by fg::Camera
    auto cam_ok = std::async(std::launch::async, [&] { return cam.open(); });
    std::string err;
    bool models_ok = ensure_models(err);
    fg::Profiles profiles;
    bool prof_ok = fg::load_profiles(data_dir() + L"\\profiles.bin", profiles);
    bool camera_ok = cam_ok.get();
    if (!models_ok || !prof_ok || !camera_ok) {
        std::string why = !models_ok ? "models: " + err : !prof_ok ? "no enrolled faces" : "camera: " + cam.error();
        log_event(L"scan aborted: %hs", why.c_str());
        cam.close();
        set([&](Snapshot& s) { s.hint = L"Face unlock unavailable"; s.state = fg::State::Fail; s.running = false; });
        running_ = false;
        done_(false, why);
        return;
    }
    log_event(L"scan start: camera %.0f ms, ready after %.0f ms (%s, %hs)", cam.open_ms(), now_ms() - t0, cam.info().name.c_str(),
              cam.info().format.c_str());
    // the enrolled camera is pinned to its exact device: a different one (another port, a look-alike device) needs
    // the owner to confirm it in the WinFace app first
    std::wstring inst = fg::camera_instance(cam.info().symlink);
    if (!cfg_.camera_instance.empty() && inst != cfg_.camera_instance) {
        log_event(L"camera changed: expected %s, found %s (%s) - face unlock paused", cfg_.camera_instance.c_str(), inst.c_str(),
                  cam.info().name.c_str());
        if (running_as_system()) LockState::set_string(L"CameraChanged", cam.info().name.empty() ? inst : cam.info().name);
        cam.close();
        set([&](Snapshot& s) { s.hint = L"Camera changed"; s.state = fg::State::Fail; s.running = false; });
        running_ = false;
        done_(false, "camera changed");
        return;
    }

    fg::Params prm;
    prm.match = cfg_.match_threshold();
    prm.search_timeout_ms = cfg_.search_ms;
    prm.challenge_timeout_ms = cfg_.challenge_ms;
    // the extra checks run only on some scans (randomly 2-3 a day, after 2 failures) - see extra_checks_due
    prm.action = cfg_.action && extras_;
    prm.flash_mode = extras_ ? (int)cfg_.flash : 0;
    fg::Engine eng(*rec_, *tex_, profiles, prm);
    eng.reset(now_ms());
    mesh_->reset();
    fg::Image frame;
    uint64_t seq = 0;
    double ts = 0;
    bool saw_face = false, flash_logged = false;
    std::string result;
    bool unlocked = false;
    while (!stop_) {
        if (!cam.next(frame, seq, ts, 300)) { result = "camera stopped delivering frames"; break; }
        std::vector<fg::Face> faces;
        if (auto f = mesh_->process(frame)) faces.push_back(std::move(*f));
        saw_face = saw_face || !faces.empty();
        eng.set_hold(hold_);
        const fg::Status& st = eng.step(frame, faces, now_ms(), ts);
        if (!st.flash_report.empty() && !flash_logged) { log_event(L"%hs", st.flash_report.c_str()); flash_logged = true; }
        if (cfg_.intruder_photos && !faces.empty()) {
            std::lock_guard<std::mutex> lk(mu_);
            last_face_frame_ = frame;
        }
        set([&](Snapshot& s) {
            s.state = st.state;
            s.hint = widen(st.hint);
            s.progress = st.progress;
            s.direction = st.state == fg::State::Challenge ? st.direction : 0;
            s.waiting = st.waiting;
            s.stage = st.state == fg::State::Challenge ? st.stage : 0;
            s.action = st.stage == fg::kStageAction ? st.action : 0;
            s.flash_t0 = st.flash_t0;
            s.flash_rgb[0] = st.flash_rgb[0];
            s.flash_rgb[1] = st.flash_rgb[1];
            s.has_face = !faces.empty();
            if (s.has_face) {
                const fg::Face& f = faces[0];
                float cx = (f.x0 + f.x1) / 2.0f, cy = (f.y0 + f.y1) / 2.0f, h = float(std::max(1, f.y1 - f.y0));
                s.mesh.resize(f.pts.size());
                for (size_t i = 0; i < f.pts.size(); ++i) s.mesh[i] = {(f.pts[i][0] - cx) / h, (f.pts[i][1] - cy) / h};
            }
        });
        if (st.state == fg::State::Unlock) { unlocked = true; result = st.reason; break; }
        if (st.state == fg::State::Fail) { result = saw_face ? st.reason : "idle: nobody in front of the camera"; break; }
    }
    cam.close();  // camera + LED off immediately
    if (stop_ && result.empty()) result = "stopped";
    log_event(L"scan end after %.0f ms: %hs - %hs", now_ms() - t0, unlocked ? "UNLOCK" : "no unlock", result.c_str());
    set([&](Snapshot& s) { s.running = false; });
    running_ = false;
    if (!stop_) done_(unlocked, result);
}

void Scanner::save_intruder_photo(const std::string& reason) {
    fg::Image img;
    {
        std::lock_guard<std::mutex> lk(mu_);
        img = std::move(last_face_frame_);
        last_face_frame_ = fg::Image{};
    }
    if (img.w == 0) return;
    std::wstring err;
    if (intruder_save(img, reason, err)) log_event(L"intruder photo saved (encrypted)");
    else log_event(L"intruder photo not saved: %s", err.c_str());
}

}  // namespace fgcp
