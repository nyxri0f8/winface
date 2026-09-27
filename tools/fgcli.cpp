// WinFace test CLI.
//   fgcli selftest            compare the C++ engine with the Python reference (testvec/)
//   fgcli unlock              real cold-start unlock with the webcam, printed timeline
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <map>
#include <string>

#include "../engine/camera.h"
#include "../engine/decide.h"

using namespace fg;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static std::wstring root_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    for (int i = 0; i < 3; ++i) s = s.substr(0, s.find_last_of(L'\\'));  // build\Release\fgcli.exe -> repo root
    return s;
}

static std::map<std::string, std::vector<Embedding>> load_templates(const std::wstring& path) {
    std::map<std::string, std::vector<Embedding>> out;
    std::ifstream f(path, std::ios::binary);
    uint32_t n = 0;
    f.read((char*)&n, 4);
    for (uint32_t i = 0; i < n && f; ++i) {
        uint32_t len = 0, cnt = 0;
        f.read((char*)&len, 4);
        std::string name(len, '\0');
        f.read(name.data(), len);
        f.read((char*)&cnt, 4);
        std::vector<Embedding> t(cnt);
        f.read((char*)t.data(), cnt * sizeof(Embedding));
        out[name] = std::move(t);
    }
    return out;
}

static int selftest(const std::wstring& root) {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "fg");
    std::wstring md = root + L"\\models\\runtime";
    FaceMesh mesh(env, md);
    Recognizer rec(env, md);
    Texture tex(env, md);
    int n = 0, bad = 0;
    double worst_lm = 0, worst_yaw = 0, worst_tex = 0, worst_cos = 1, total_ms = 0, sum_cos = 0;
    double t_mesh = 0, t_emb = 0, t_tex = 0;
    for (int i = 0; i < 100; ++i) {
        wchar_t fp[64], ep[64];
        swprintf(fp, 64, L"\\testvec\\frame_%02d.bgr", i);
        swprintf(ep, 64, L"\\testvec\\expect_%02d.bin", i);
        std::ifstream ff(root + fp, std::ios::binary), fe(root + ep, std::ios::binary);
        if (!ff || !fe) break;
        Image img(1280, 720, 3);
        ff.read((char*)img.px.data(), img.px.size());
        std::vector<float> ex(478 * 3 + 4 + 512);
        fe.read((char*)ex.data(), ex.size() * 4);
        mesh.reset();
        mesh.process(img);
        double t = now_ms();
        auto f = mesh.process(img);
        double t1 = now_ms();
        if (!f) { printf("  vec %02d: C++ found no face\n", i); ++bad; continue; }
        Embedding e = rec.embed(img, *f);
        double t2 = now_ms();
        float tx = tex.real_prob(img, *f);
        double t3 = now_ms();
        total_ms += t3 - t;
        t_mesh += t1 - t; t_emb += t2 - t1; t_tex += t3 - t2;
        double lm = 0;
        for (int k = 0; k < 478; ++k) lm += std::hypot(f->pts[k][0] - ex[k * 3], f->pts[k][1] - ex[k * 3 + 1]);
        lm = lm / 478 / std::max(1, f->width());
        double dyaw = std::fabs(f->yaw - ex[1434]), dtex = std::fabs(tx - ex[1437]);
        double cs = 0;
        for (int k = 0; k < 512; ++k) cs += e[k] * ex[1438 + k];
        worst_lm = std::max(worst_lm, lm); worst_yaw = std::max(worst_yaw, dyaw);
        worst_tex = std::max(worst_tex, dtex); worst_cos = std::min(worst_cos, cs);
        sum_cos += cs;
        ++n;
    }
    if (n) printf("  stages: mesh %.1f ms, embedding %.1f ms, texture %.1f ms\n", t_mesh / n, t_emb / n, t_tex / n);
    printf("selftest: %d vectors, %d failures\n", n, bad);
    printf("  landmarks: worst mean error %.3f%% of face width\n", worst_lm * 100);
    printf("  yaw:       worst |diff| %.2f deg\n", worst_yaw);
    printf("  texture:   worst |diff| %.4f\n", worst_tex);
    printf("  embedding: worst cosine vs Python %.5f, mean %.5f\n", worst_cos, n ? sum_cos / n : 0);
    printf("  speed:     %.1f ms per frame (landmarks + embedding + texture)\n", n ? total_ms / n : 0);
    // INT8 ArcFace amplifies tiny bilinear rounding differences vs OpenCV's fixed-point warp; enrolment and
    // unlock both run in C++, so only a gross mismatch (bug) matters here.
    bool ok = bad == 0 && worst_lm < 0.01 && worst_yaw < 2 && worst_tex < 0.05 && worst_cos > 0.95 && sum_cos / n > 0.98;
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto sec = [](FILETIME f) { return (double(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 1e7; };
    printf("  CPU time: %.2f s total\n", sec(k) + sec(u));
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int unlock(const std::wstring& root) {
    const double t0 = now_ms();
    auto log = [&](const char* what) { printf("%7.0f ms  %s\n", now_ms() - t0, what); };
    // camera and model loading run in parallel, exactly like the lock-screen component will
    Camera cam(L"usb#vid_0408&pid_5496&mi_00");
    auto cam_ok = std::async(std::launch::async, [&] { return cam.open(); });
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "fg");
    std::wstring md = root + L"\\models\\runtime";
    FaceMesh mesh(env, md);
    Recognizer rec(env, md);
    Texture tex(env, md);
    auto profiles = load_templates(root + L"\\bench\\data\\templates.bin");
    log("models + templates loaded");
    if (!cam_ok.get()) { printf("camera error: %s\n", cam.error().c_str()); return 2; }
    char buf[200];
    snprintf(buf, sizeof buf, "camera ready (%ls, open %.0f ms)", cam.info().name.c_str(), cam.open_ms());
    log(buf);

    Engine eng(rec, tex, profiles);
    eng.reset(now_ms());
    Image frame;
    uint64_t seq = 0;
    double ts = 0;
    int frames = 0, last_dir = 0;
    State last = State::Search;
    while (cam.next(frame, seq, ts, 500)) {
        ++frames;
        std::vector<Face> faces;
        if (auto f = mesh.process(frame)) faces.push_back(*f);
        const Status& st = eng.step(frame, faces, now_ms());
        if (st.state == State::Challenge && last_dir == 0) {
            last_dir = st.direction;
            snprintf(buf, sizeof buf, ">>> CHALLENGE: turn your head slightly %s <<<", st.direction < 0 ? "LEFT" : "RIGHT");
            log(buf);
        }
        if (st.state != last && (st.state == State::Unlock || st.state == State::Fail)) {
            snprintf(buf, sizeof buf, "%s after %d frames: %s", st.state == State::Unlock ? "UNLOCK" : "FAIL", frames, st.reason.c_str());
            log(buf);
            break;
        }
        last = st.state;
    }
    cam.close();
    log("camera released");
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto sec = [](FILETIME f) { return (double(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 1e7; };
    printf("CPU time %.2f s, exiting\n", sec(k) + sec(u));
    return eng.status().state == State::Unlock ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Ort::InitApi();
    std::wstring root = root_dir();
    std::wstring cmd = argc > 1 ? argv[1] : L"";
    printf("root: %ls\n", root.c_str());
    try {
        if (cmd == L"selftest") return selftest(root);
        if (cmd == L"unlock") return unlock(root);
    } catch (const std::exception& e) {
        printf("error: %s\n", e.what());
        return 3;
    }
    printf("usage: fgcli selftest | unlock\n");
    return 1;
}
