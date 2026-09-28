// Checks the lock-screen pieces without the lock screen:
//   fgoverlaytest render <outdir> [backdrop.jpg]  render the real HUD code over a backdrop, 60 fps PNG frames
//   fgoverlaytest live [shot.png]                  real Overlay window: hidden until a key press, shown after it
//   fgoverlaytest dll <WinFaceCP.dll>             load the provider DLL like LogonUI does and create the provider
//   fgoverlaytest wav <file.wav>                   parse the sound and ask the audio driver if it can play it (silent)
//   fgoverlaytest sound                            play the unlock sound the way the provider does, then exit at once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <credentialprovider.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>

#include "../cp/overlay.h"
#include "../engine/camera.h"
#include "../engine/decide.h"
#include "../cp/common.h"
#include "../cp/intruder.h"

#include <ncrypt.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

bool png_clsid(CLSID* id) {
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    std::vector<BYTE> buf(sz);
    auto* enc = (ImageCodecInfo*)buf.data();
    GetImageEncoders(n, sz, enc);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(enc[i].MimeType, L"image/png") == 0) { *id = enc[i].Clsid; return true; }
    return false;
}

// A scripted scan: look for a face, find it, head-turn challenge, unlock; then a second scan that is not recognised.
struct Script {
    float face = 0, look = 0;   // smoothed exactly like Overlay::paint (it runs at 60 fps too)
    fgcp::HudFrame at(double t) {
        fgcp::HudFrame f;
        f.t = t;
        bool has_face = false;
        float look_raw = 0;
        if (t < 6.0) {                       // scan 1 -> unlock
            f.age = t;
            if (t < 1.0) { f.hint = L"Looking for you..."; }
            else if (t < 2.6) { has_face = true; f.hint = L"Scanning..."; f.progress = float((t - 1.0) / 1.6 * 0.5); look_raw = float(0.35 * std::sin((t - 1.0) * 3)); }
            else if (t < 4.4) { has_face = true; f.state = fg::State::Challenge; f.direction = 1; f.hint = L"Turn your head slightly RIGHT";
                                f.progress = float(0.5 + (t - 2.6) / 1.8 * 0.5); look_raw = t < 3.3 ? 0.f : 0.8f; }
            else { has_face = true; f.progress = 1; f.result = 1; f.since_result = t - 4.4; f.hint = L"Unlocked"; }
        } else {                             // scan 2 -> not recognised
            f.age = t - 6.0;
            has_face = true;
            if (t < 7.3) { f.hint = L"Scanning..."; f.progress = float((t - 6.0) / 1.3 * 0.3); look_raw = -0.2f; }
            else { f.result = -1; f.since_result = t - 7.3; f.hint = L"Not recognised"; }
        }
        face += ((has_face ? 1.f : 0.f) - face) * 0.12f;
        look += (look_raw - look) * 0.15f;
        f.face = face;
        f.look = look;
        return f;
    }
    static constexpr double kEnd = 8.9;
};

int cmd_render(const wchar_t* dir, const wchar_t* backdrop) {
    CLSID png;
    if (!png_clsid(&png)) { printf("no PNG encoder\n"); return 1; }
    const int W = 1280, H = 720;
    const float k = 1.25f;   // 120 dpi, a common laptop scale
    std::unique_ptr<Bitmap> bg;
    if (backdrop && *backdrop) bg.reset(Bitmap::FromFile(backdrop));
    Bitmap hud(int(fgcp::kHudW * k), int(fgcp::kHudH * k), PixelFormat32bppPARGB);
    Bitmap frame(W, H, PixelFormat32bppARGB);
    Script sc;
    int n = 0;
    for (double t = 0; t < Script::kEnd; t += 1 / 60.0, ++n) {
        fgcp::HudFrame f = sc.at(t);
        {
            Graphics g(&hud);
            fgcp::render_hud(g, k, f);
        }
        Graphics g(&frame);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        if (bg && bg->GetLastStatus() == Ok) g.DrawImage(bg.get(), 0, 0, W, H);
        else { LinearGradientBrush b(Point(0, 0), Point(W, H), Color(255, 24, 38, 64), Color(255, 70, 40, 60)); g.FillRectangle(&b, 0, 0, W, H); }
        g.DrawImage(&hud, (W - int(hud.GetWidth())) / 2, int(40 * k));
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\f%04d.png", dir, n);
        if (frame.Save(path, &png) != Ok) { wprintf(L"cannot write %s\n", path); return 1; }
    }
    printf("%d frames\n", n);
    return 0;
}

bool save_screen(const wchar_t* path, RECT r) {
    CLSID png;
    if (!png_clsid(&png)) return false;
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC s = GetDC(nullptr), m = CreateCompatibleDC(s);
    HBITMAP b = CreateCompatibleBitmap(s, w, h);
    HGDIOBJ o = SelectObject(m, b);
    BitBlt(m, 0, 0, w, h, s, r.left, r.top, SRCCOPY | CAPTUREBLT);
    SelectObject(m, o);
    bool ok = false;
    {
        Bitmap bmp(b, nullptr);
        ok = bmp.Save(path, &png) == Ok;
    }
    DeleteObject(b);
    DeleteDC(m);
    ReleaseDC(nullptr, s);
    return ok;
}

void inject_key(WORD vk) {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void inject_mouse_nudge() {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dx = 1; in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    in[1].mi.dx = -1; in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(2, in, sizeof(INPUT));
}

int cmd_live(const wchar_t* shot) {
    printf("live overlay (lock-screen mode: wait for input)\n");
    std::atomic<bool> revealed{false};
    const double t0 = GetTickCount64() / 1000.0;
    fgcp::Overlay ov;
    ov.start(nullptr, [&] {
        fgcp::Snapshot s;
        double t = GetTickCount64() / 1000.0 - t0;
        s.running = true;
        s.has_face = t > 0.5;
        s.hint = s.has_face ? L"Scanning..." : L"Looking for you...";
        s.progress = float(std::min(1.0, t / 4));
        return s;
    }, [](bool) {}, true, [&] { revealed = true; });
    auto find = [] { return FindWindowW(L"WinFaceOverlay", L"WinFace"); };
    for (int i = 0; i < 50 && !find(); ++i) Sleep(20);
    HWND h = find();
    check(h != nullptr, "overlay window created");
    if (!h) return 1;
    Sleep(800);
    check(!IsWindowVisible(h), "hidden while the curtain is down (camera would already be scanning)");
    inject_mouse_nudge();
    Sleep(300);
    check(!IsWindowVisible(h) && !revealed, "a mouse nudge alone does not reveal it");
    inject_key(VK_F24);   // any key works (Up, Enter...); F24 has no side effects on this PC
    Sleep(300);
    check(IsWindowVisible(h) && revealed, "key press reveals it and asks for a fresh scan");
    Sleep(1200);
    RECT r;
    GetWindowRect(h, &r);
    if (shot && *shot) check(save_screen(shot, r), "screenshot of the real window saved");
    ov.result(true, false);   // success animation, sound off for the test
    Sleep(1600);
    check(!IsWindowVisible(h), "hides itself after the unlock animation");
    ov.reset();
    Sleep(300);
    check(IsWindowVisible(h), "a new scan shows it again (curtain already lifted)");
    ov.stop();

    printf("live overlay: face recognised behind the curtain (no key press)\n");
    std::atomic<int> lifts{0};
    std::atomic<bool> revealed2{false}, recognised{false};
    fgcp::Overlay ov2;
    ov2.set_curtain_lifter([&] { ++lifts; inject_key(VK_F24); return true; });   // real one sends Shift + a corner click
    ov2.start(nullptr, [&] {
        fgcp::Snapshot s;
        s.running = true;
        s.has_face = true;
        s.waiting = recognised;
        s.hint = L"Face found";
        return s;
    }, [](bool) {}, true, [&] { revealed2 = true; });
    for (int i = 0; i < 50 && !find(); ++i) Sleep(20);
    h = find();
    Sleep(600);
    check(h && !IsWindowVisible(h) && lifts == 0, "face in view but not yet recognised: still hidden, curtain untouched");
    recognised = true;
    Sleep(400);
    check(h && IsWindowVisible(h) && revealed2, "recognised: shows itself with no key press and releases the head turn");
    check(lifts == 1, "lifts the curtain exactly once");
    Sleep(400);
    check(lifts == 1, "does not keep sending input afterwards");
    ov2.stop();

    printf("curtain lift when Windows refuses the input at first\n");
    {
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = VK_F24;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        check(fgcp::send_input_on_input_desktop(in, 2), "input sent from a fresh thread on the input desktop");
    }
    auto run_lift = [&](int fail_first, int wait_ms, int& tries) {
        std::atomic<bool> rev{false};
        std::atomic<int> n{0};
        fgcp::Overlay o;
        o.set_curtain_lifter([&] { return ++n > fail_first; });
        o.start(nullptr, [&] { fgcp::Snapshot s; s.running = s.has_face = s.waiting = true; return s; }, [](bool) {}, true,
                [&] { rev = true; });
        Sleep(wait_ms);
        HWND w = find();
        bool shown = w && IsWindowVisible(w);
        o.stop();
        tries = n;
        return rev && shown;
    };
    int tries = 0;
    check(run_lift(2, 1500, tries) && tries == 3, "refused twice, third try works: shown only then");
    check(!run_lift(100, 2800, tries) && tries == 7, "refused every time: stays hidden (key press still works), gives up after 7 tries");
    return g_fail ? 1 : 0;
}

int cmd_dll(const wchar_t* path) {
    printf("provider DLL\n");
    HMODULE m = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(m != nullptr, "DLL loads (dependencies resolve)");
    if (!m) { printf("    error %lu\n", GetLastError()); return 1; }
    using GetCO = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
    using CanUnload = HRESULT(__stdcall*)();
    auto get = (GetCO)GetProcAddress(m, "DllGetClassObject");
    auto can = (CanUnload)GetProcAddress(m, "DllCanUnloadNow");
    check(get && can, "exports DllGetClassObject / DllCanUnloadNow");
    if (!get) return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const CLSID clsid = {0xc188dc15, 0xe41e, 0x4ccf, {0x9d, 0xa9, 0x82, 0x38, 0xe1, 0xd0, 0xbb, 0xdf}};
    IClassFactory* cf = nullptr;
    HRESULT hr = get(clsid, IID_IClassFactory, (void**)&cf);
    check(SUCCEEDED(hr) && cf, "class factory for the WinFace CLSID");
    if (cf) {
        ICredentialProvider* p = nullptr;
        hr = cf->CreateInstance(nullptr, IID_ICredentialProvider, (void**)&p);
        check(SUCCEEDED(hr) && p, "creates ICredentialProvider");
        if (p) {
            DWORD n = 0;
            p->GetFieldDescriptorCount(&n);
            check(n == 4, "4 tile fields");
            p->Release();
        }
        cf->Release();
    }
    check(!can || can() == S_OK, "unloads cleanly (no leaked references)");
    FreeLibrary(m);
    CoUninitialize();
    return g_fail ? 1 : 0;
}

int cmd_camera() {
    printf("camera classification\n");
    struct { const wchar_t* name; bool ir; } names[] = {
        {L"HP FHD Camera", false}, {L"Integrated IR Camera", true}, {L"IR Camera", true}, {L"HP IR Camera", true},
        {L"Infrared Camera", true}, {L"Intel(R) RealSense(TM) Infrared", true}, {L"TOSHIBA Web Camera - HD", false},
        {L"TrueVision HD", false}, {L"Integrated Camera", false}, {L"Iriun Webcam", false}, {L"HD User Facing", false}};
    for (auto& n : names) {
        char b[160];
        snprintf(b, sizeof b, "%-34ls -> %s", n.name, n.ir ? "infrared (skipped)" : "colour");
        check(fg::is_infrared_camera(n.name) == n.ir, b);
    }
    struct { const wchar_t* link; bool hw; } links[] = {
        {L"\\\\?\\usb#vid_0408&pid_5496&mi_00#8&5d2c#{e5323777}", true}, {L"\\\\?\\display#int3474#4&1a2b#{e5323777}", true},
        {L"\\\\?\\acpi#int33be#1#{e5323777}", true}, {L"\\\\?\\pci#ven_8086&dev_a75d#3&11#{e5323777}", true},
        {L"\\\\?\\swd#vcamdevapi#aab2#{e5323777}", false}, {L"\\\\?\\root#image#0000#{e5323777}", false}};
    for (auto& l : links) {
        char b[160];
        snprintf(b, sizeof b, "%-44.44ls -> %s", l.link, l.hw ? "real camera" : "virtual (refused)");
        check(fg::is_hardware_camera(l.link) == l.hw, b);
    }

    printf("open the camera automatically (like a new PC with no camera chosen)\n");
    fg::Camera cam(L"");
    bool ok = cam.open();
    check(ok, ok ? "opened" : ("open failed: " + cam.error()).c_str());
    if (!ok) return 1;
    printf("    %ls, %s, first frame after %.0f ms\n", cam.info().name.c_str(), cam.info().format.c_str(), cam.open_ms());
    fg::Image img;
    uint64_t seq = 0;
    double ts = 0, t0 = 0, t1 = 0;
    int frames = 0;
    for (int i = 0; i < 40 && frames < 30; ++i)
        if (cam.next(img, seq, ts, 500)) { if (!frames) t0 = ts; t1 = ts; ++frames; }
    cam.close();
    double fps = frames > 1 && t1 > t0 ? (frames - 1) * 1000.0 / (t1 - t0) : 0;
    char b[120];
    snprintf(b, sizeof b, "frames are 1280x720 colour (%dx%d, %d frames, %.0f fps)", img.w, img.h, frames, fps);
    check(img.w == 1280 && img.h == 720 && frames >= 20, b);

    printf("a size this camera does not offer (the path a camera without 1280x720 takes)\n");
    fg::Camera cam2(L"");
    ok = cam2.open(960, 540, 30);
    check(ok, ok ? "opened" : ("open failed: " + cam2.error()).c_str());
    if (ok) {
        printf("    device format %s\n", cam2.info().format.c_str());
        frames = 0;
        for (int i = 0; i < 20 && frames < 10; ++i) if (cam2.next(img, seq = 0, ts, 500)) ++frames;
        snprintf(b, sizeof b, "scaled frames arrive at the requested size (%dx%d)", img.w, img.h);
        check(img.w == 960 && img.h == 540 && frames >= 5, b);
    }
    cam2.close();
    return g_fail ? 1 : 0;
}

int cmd_security() {
    using namespace fgcp;
    printf("lockout rules\n");
    const ULONGLONG hour = 36000000000ULL, now = 1000000 * hour, boot = now - 5 * hour;
    Config c;
    c.max_fails = 3;
    c.events_task = true;
    LockState s;
    s.last_strong_auth = now - 1 * hour;   // PIN used after this boot
    s.last_unlock = now - 1 * hour;
    check(lockout_reason(c, s, now, boot).empty(), "PIN used since boot, unlocked an hour ago: face allowed");
    LockState f = s; f.fails = 3;
    check(!lockout_reason(c, f, now, boot).empty(), "3 failed attempts: PIN required");
    f.fails = 2;
    check(lockout_reason(c, f, now, boot).empty(), "2 failed attempts: face still allowed");
    LockState r = s; r.last_strong_auth = boot - hour;
    check(lockout_reason(c, r, now, boot).find(L"restart") != std::wstring::npos, "no PIN since the restart: PIN required");
    Config c2 = c; c2.pin_after_restart = false;
    check(lockout_reason(c2, r, now, boot).empty(), "...unless 'PIN after restart' is off");
    LockState u = s; u.last_unlock = now - 49 * hour; u.last_strong_auth = now - 49 * hour;
    check(!lockout_reason(c, u, now, now - 100 * hour).empty(), "not unlocked for 49 h: PIN required");
    Config c3 = c; c3.pin_after_hours = 0;
    check(lockout_reason(c3, u, now, now - 100 * hour).empty(), "...unless that rule is off");
    LockState cam = s; cam.camera_changed = L"USB Video";
    check(!lockout_reason(c, cam, now, boot).empty(), "camera changed: PIN required");
    Config c4 = c; c4.events_task = false;
    check(lockout_reason(c4, r, now, boot).empty(), "without the sign-in events task the PIN-time rules stay off");

    printf("extra checks: randomly 2-3 times a day + after 2 failures\n");
    {
        Config e;
        LockState st;
        e.extra_checks = 0;
        check(!extra_checks_due(e, st, 5, 0, 20260928, 10), "'Never': no extra checks, even after failures");
        e.extra_checks = 2;
        check(extra_checks_due(e, st, 0, 1, 20260928, 10), "'Every unlock': always");
        e.extra_checks = 1;
        check(extra_checks_due(e, st, 2, 1, 20260928, 10), "third attempt after 2 failures: always");
        LockState d;
        extra_checks_due(e, d, 0, 1, 20260928, 10);
        check(d.extra_day == 20260928 && (d.extra_plan == 2 || d.extra_plan == 3) && d.extra_done == 0, "a new day plans 2 or 3");
        d.extra_done = d.extra_plan;
        check(!extra_checks_due(e, d, 0, 0, 20260928, 21), "day's plan used up: no more (even in the evening)");
        // 1000 simulated days, 10 unlocks each between 08:00 and 22:00
        int days = 1000, total = 0, min_day = 99, max_day = 0;
        char b0[160];
        LockState sim;
        for (int day = 0; day < days; ++day) {
            int today_count = 0;
            for (int k = 0; k < 10; ++k) {
                int hr = 8 + k * 14 / 10;
                if (extra_checks_due(e, sim, 0, secure_random(), 20000000 + day, hr)) { ++sim.extra_done; ++today_count; }
            }
            total += today_count;
            min_day = std::min(min_day, today_count);
            max_day = std::max(max_day, today_count);
        }
        snprintf(b0, sizeof b0, "1000 days x 10 unlocks: %.2f extra checks a day on average (min %d, max %d)", double(total) / days, min_day, max_day);
        check(min_day >= 2 && max_day <= 3, b0);
    }

    printf("blink / mouth measurement\n");
    fg::Face face;
    face.pts.assign(478, {0.f, 0.f, 0.f});
    auto eye = [&](int c0, int c1, int u0, int l0, int u1, int l1, float x, float open) {
        face.pts[c0] = {x, 100, 0}; face.pts[c1] = {x + 30, 100, 0};
        face.pts[u0] = {x + 10, 100 - open / 2, 0}; face.pts[l0] = {x + 10, 100 + open / 2, 0};
        face.pts[u1] = {x + 20, 100 - open / 2, 0}; face.pts[l1] = {x + 20, 100 + open / 2, 0};
    };
    auto eyes = [&](float open) { eye(33, 133, 160, 144, 158, 153, 60, open); eye(263, 362, 387, 373, 385, 380, 140, open); };
    eyes(9);
    float e_open = fg::eye_openness(face);
    eyes(2);
    float e_closed = fg::eye_openness(face);
    char b[160];
    snprintf(b, sizeof b, "eye openness: open %.2f, closed %.2f (closed < 60 %% of open)", e_open, e_closed);
    check(e_open > 0.25f && e_closed < e_open * 0.6f, b);
    face.pts[78] = {90, 180, 0}; face.pts[308] = {150, 180, 0}; face.pts[13] = {120, 178, 0}; face.pts[14] = {120, 182, 0};
    float m_closed = fg::mouth_openness(face);
    face.pts[13] = {120, 165, 0}; face.pts[14] = {120, 195, 0};
    float m_open = fg::mouth_openness(face);
    snprintf(b, sizeof b, "mouth openness: closed %.2f, open %.2f (open > 0.35)", m_closed, m_open);
    check(m_closed < 0.1f && m_open > 0.35f, b);

    printf("screen flash\n");
    const int cols[2] = {0xFF0000, 0x0000FF};
    check(fg::flash_colour_at(1000, cols, 1000 + 100) == -1, "baseline: no colour");
    check(fg::flash_colour_at(1000, cols, 1000 + 400) == 0xFF0000, "first colour on time");
    check(fg::flash_colour_at(1000, cols, 1000 + 850) == -1, "gap between the colours");
    check(fg::flash_colour_at(1000, cols, 1000 + 1200) == 0x0000FF, "second colour on time");
    check(fg::flash_colour_at(1000, cols, 1000 + 1500) == -1, "off afterwards");
    std::array<float, 3> base{150, 110, 90};
    check(fg::flash_response_ok(base, {157, 111, 90}, 0xFF0000, 0.015f), "skin turning redder under a red flash: pass");
    check(!fg::flash_response_ok(base, {150, 110, 95}, 0xFF0000, 0.015f), "turning bluer under a red flash: fail");
    check(!fg::flash_response_ok(base, {151, 111, 91}, 0xFF0000, 0.015f), "no clear change (screen / deepfake): fail");
    check(!fg::flash_response_ok(base, {160, 118, 96}, 0xFF0000, 0.015f), "everything just brighter (not the colour): fail");

    printf("camera identity\n");
    check(fg::camera_instance(L"\\\\?\\usb#vid_0408&pid_5496&mi_00#8&5d2c72a&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global") ==
              L"usb#vid_0408&pid_5496&mi_00#8&5d2c72a&0&0000", "exact device instance extracted");
    check(fg::is_capture_device(L"Elgato Cam Link 4K") && fg::is_capture_device(L"USB3 Video") && fg::is_capture_device(L"OBS Virtual Camera"),
          "HDMI capture dongles / OBS refused");
    check(!fg::is_capture_device(L"HP FHD Camera") && !fg::is_capture_device(L"Integrated Webcam") && !fg::is_capture_device(L"Jobs Camera"),
          "normal webcams allowed");

    printf("intruder photo encryption (software test key; the product uses the TPM)\n");
    fg::Image img(1280, 720);
    for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) { auto* p = img.row(y) + x * 3; p[0] = BYTE(x); p[1] = BYTE(y); p[2] = 128; }
    std::vector<BYTE> jpeg;
    check(encode_jpeg(img, jpeg) && jpeg.size() > 1000 && jpeg[0] == 0xFF && jpeg[1] == 0xD8, "JPEG encoded (640 px wide)");
    const wchar_t* kTestKey = L"WinFaceSelfTestKey";
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0);
    if (NCryptCreatePersistedKey(prov, &key, NCRYPT_RSA_ALGORITHM, kTestKey, 0, NCRYPT_OVERWRITE_KEY_FLAG) == ERROR_SUCCESS) {
        DWORD bits = 2048;
        NCryptSetProperty(key, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof bits, 0);
        NCryptFinalizeKey(key, 0);
    }
    std::vector<BYTE> sealed, back;
    std::wstring err;
    ULONGLONG when = 0;
    bool sealed_ok = seal_blob(jpeg, sealed, MS_KEY_STORAGE_PROVIDER, kTestKey, 0, err);
    check(sealed_ok, "encrypted");
    if (!sealed_ok) wprintf(L"    %s\n", err.c_str());
    check(sealed_ok && std::search(sealed.begin(), sealed.end(), jpeg.begin(), jpeg.begin() + 64) == sealed.end(),
          "no readable image data inside the file");
    check(open_blob(sealed, back, when, MS_KEY_STORAGE_PROVIDER, kTestKey, 0, err) && back == jpeg, "decrypts back to the same photo");
    std::vector<BYTE> tampered = sealed;
    if (!tampered.empty()) tampered[tampered.size() / 2] ^= 0x01;
    check(!open_blob(tampered, back, when, MS_KEY_STORAGE_PROVIDER, kTestKey, 0, err), "a tampered file is rejected");
    if (key) NCryptDeleteKey(key, 0);
    NCryptFreeObject(prov);
    return g_fail ? 1 : 0;
}

int cmd_wav(const wchar_t* path) {
    printf("sound file\n");
    HMMIO h = mmioOpenW(const_cast<LPWSTR>(path), nullptr, MMIO_READ | MMIO_ALLOCBUF);
    check(h != nullptr, "opens");
    if (!h) return 1;
    MMCKINFO riff{}, fmt{}, data{};
    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    bool ok = mmioDescend(h, &riff, nullptr, MMIO_FINDRIFF) == 0;
    fmt.ckid = mmioFOURCC('f', 'm', 't', ' ');
    ok = ok && mmioDescend(h, &fmt, &riff, MMIO_FINDCHUNK) == 0;
    WAVEFORMATEX wf{};
    ok = ok && mmioRead(h, (HPSTR)&wf, std::min<LONG>(sizeof wf, fmt.cksize)) > 0;
    check(ok, "RIFF/WAVE with a fmt chunk");
    mmioAscend(h, &fmt, 0);
    data.ckid = mmioFOURCC('d', 'a', 't', 'a');
    bool has_data = ok && mmioDescend(h, &data, &riff, MMIO_FINDCHUNK) == 0;
    mmioClose(h, 0);
    check(has_data && data.cksize > 0, "has audio data");
    check(wf.wFormatTag == WAVE_FORMAT_PCM, "plain PCM (what PlaySound needs)");
    if (ok && has_data && wf.nAvgBytesPerSec)
        printf("    %u Hz, %u ch, %u bit, %.2f s\n", wf.nSamplesPerSec, wf.nChannels, wf.wBitsPerSample, double(data.cksize) / wf.nAvgBytesPerSec);
    wf.cbSize = 0;
    MMRESULT q = waveOutOpen(nullptr, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY);
    check(q == MMSYSERR_NOERROR, "audio driver accepts the format (queried, nothing played)");
    return g_fail ? 1 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ULONG_PTR tok = 0;
    GdiplusStartupInput gsi;
    GdiplusStartup(&tok, &gsi, nullptr);
    std::wstring cmd = argc > 1 ? argv[1] : L"";
    int rc = 2;
    if (cmd == L"render" && argc > 2) rc = cmd_render(argv[2], argc > 3 ? argv[3] : nullptr);
    else if (cmd == L"live") rc = cmd_live(argc > 2 ? argv[2] : nullptr);
    else if (cmd == L"dll" && argc > 2) rc = cmd_dll(argv[2]);
    else if (cmd == L"wav" && argc > 2) rc = cmd_wav(argv[2]);
    else if (cmd == L"camera") rc = cmd_camera();
    else if (cmd == L"security") rc = cmd_security();
    else if (cmd == L"sound") { fgcp::play_unlock_sound(); rc = 0; }   // then exit at once, like LogonUI after sign-in
    else printf("usage: fgoverlaytest render <outdir> [backdrop] | live [shot.png] | dll <WinFaceCP.dll> | wav <file.wav>\n");
    GdiplusShutdown(tok);
    return rc;
}
