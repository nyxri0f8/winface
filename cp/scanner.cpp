#include "scanner.h"

#include <chrono>
#include <future>

#include "../engine/camera.h"
#include "../engine/profiles.h"
#include "common.h"

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

    fg::Params prm;
    prm.match = cfg_.match_threshold();
    prm.search_timeout_ms = cfg_.search_ms;
    prm.challenge_timeout_ms = cfg_.challenge_ms;
    fg::Engine eng(*rec_, *tex_, profiles, prm);
    eng.reset(now_ms());
    mesh_->reset();
    fg::Image frame;
    uint64_t seq = 0;
    double ts = 0;
    bool saw_face = false;
    std::string result;
    bool unlocked = false;
    while (!stop_) {
        if (!cam.next(frame, seq, ts, 300)) { result = "camera stopped delivering frames"; break; }
        std::vector<fg::Face> faces;
        if (auto f = mesh_->process(frame)) faces.push_back(std::move(*f));
        saw_face = saw_face || !faces.empty();
        eng.set_hold(hold_);
        const fg::Status& st = eng.step(frame, faces, now_ms());
        set([&](Snapshot& s) {
            s.state = st.state;
            s.hint = widen(st.hint);
            s.progress = st.progress;
            s.direction = st.state == fg::State::Challenge ? st.direction : 0;
            s.waiting = st.waiting;
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

}  // namespace fgcp
