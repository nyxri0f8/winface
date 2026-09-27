#include "overlay.h"

#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "common.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

namespace fgcp {
namespace {

constexpr UINT WM_FG_SHOW = WM_APP + 1;

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

float clamp01(double v) { return float(std::clamp(v, 0.0, 1.0)); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float ease_out(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }                              // cubic
float ease_in_out(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }  // cubic
Color white(float a) { return Color(BYTE(std::lround(std::clamp(a, 0.f, 1.f) * 255)), 255, 255, 255); }

// One Face ID corner bracket: leg along the horizontal edge, rounded corner, leg down the vertical edge.
// (sx, sy) = which corner (+-1). h = half size, r = corner radius, l = leg length beyond the arc.
void add_corner(GraphicsPath& p, float cx, float cy, float h, float r, float l, int sx, int sy) {
    const float ax = cx + sx * (h - r), ay = cy + sy * (h - r);       // arc centre
    const float a0 = sy < 0 ? 270.f : 90.f, a1 = sx < 0 ? 180.f : 0.f;  // horizontal-edge point -> vertical-edge point
    float sweep = a1 - a0;
    if (sweep > 180) sweep -= 360;
    if (sweep < -180) sweep += 360;
    p.StartFigure();
    if (l > 0.25f) p.AddLine(ax - sx * l, cy + sy * h, ax, cy + sy * h);
    p.AddArc(ax - r, ay - r, 2 * r, 2 * r, a0, sweep);
    if (l > 0.25f) p.AddLine(cx + sx * h, ay, cx + sx * h, ay - sy * l);
}

// draw the first `frac` (0..1, by length) of a polyline
void draw_partial(Graphics& g, Pen& pen, const PointF* pts, int n, float frac) {
    float total = 0;
    for (int i = 1; i < n; ++i) total += std::hypot(pts[i].X - pts[i - 1].X, pts[i].Y - pts[i - 1].Y);
    float left = total * frac;
    GraphicsPath path;
    for (int i = 1; i < n && left > 0; ++i) {
        float seg = std::hypot(pts[i].X - pts[i - 1].X, pts[i].Y - pts[i - 1].Y);
        float u = std::min(1.f, left / seg);
        path.AddLine(pts[i - 1], PointF(pts[i - 1].X + (pts[i].X - pts[i - 1].X) * u, pts[i - 1].Y + (pts[i].Y - pts[i - 1].Y) * u));
        left -= seg;
    }
    g.DrawPath(&pen, &path);
}

// Lift the lock-screen curtain the way a person would. Runs on the overlay thread, which is attached to the
// sign-in (Winlogon) desktop, so the input goes there. Shift types nothing; the click lands in the empty top-left
// corner, which has no control on the sign-in page either.
void lift_curtain_by_input() {
    INPUT in[4] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_SHIFT;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].type = in[3].type = INPUT_MOUSE;
    in[2].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
    in[3].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP;
    UINT n = SendInput(4, in, sizeof(INPUT));
    if (n != 4) log_event(L"curtain lift: SendInput sent %u of 4 (%lu)", n, GetLastError());
}

}  // namespace

void play_unlock_sound() {
    std::wstring exe = module_dir() + L"\\fgsound.exe";
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return;
    }
    log_event(L"fgsound.exe failed to start (%lu): playing in-process", GetLastError());
    std::wstring wav = module_dir() + L"\\assets\\sfx_unlock.wav";
    PlaySoundW(wav.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

double hud_opacity(const HudFrame& f) {
    double a = clamp01(f.age / 0.22);                                          // appear
    if (f.result != 0 && f.since_result > 0.95) a *= 1 - clamp01((f.since_result - 0.95) / 0.3);  // leave
    return a;
}

void render_hud(Graphics& g, float k, const HudFrame& f) {
    const float W = kHudW * k, H = kHudH * k;
    const int res = f.result;
    const float rt = float(f.since_result);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    const float A = float(hud_opacity(f));
    if (A <= 0) return;
    // the whole HUD settles in from 94 % and eases out to 96 % as it leaves
    float s = 0.94f + 0.06f * ease_out(clamp01(f.age / 0.3));
    if (res != 0 && rt > 0.95f) s *= 1 - 0.04f * clamp01((rt - 0.95) / 0.3);
    g.TranslateTransform(W / 2, H / 2);
    g.ScaleTransform(s, s);
    g.TranslateTransform(-W / 2, -H / 2);

    // panel: dark, quiet, no border glow
    {
        const float r = 44 * k, i = 0.5f;
        GraphicsPath panel;
        panel.AddArc(i, i, 2 * r, 2 * r, 180, 90);
        panel.AddArc(W - 2 * r - i, i, 2 * r, 2 * r, 270, 90);
        panel.AddArc(W - 2 * r - i, H - 2 * r - i, 2 * r, 2 * r, 0, 90);
        panel.AddArc(i, H - 2 * r - i, 2 * r, 2 * r, 90, 90);
        panel.CloseFigure();
        SolidBrush bg(Color(BYTE(std::lround(A * 0.9 * 255)), 18, 18, 20));
        g.FillPath(&bg, &panel);
        Pen hair(white(A * 0.07f), 1.0f * k);
        g.DrawPath(&hair, &panel);
    }

    // failure: damped horizontal shake of the glyph
    const float shake = res == -1 && rt < 0.55f ? float(std::sin(rt * 42) * 11 * std::exp(-rt * 6.5)) * k : 0;
    const float cx = W / 2 + shake, cy = 108 * k;
    const float sw = 3.6f * k;

    // --- brackets -> circle ---------------------------------------------------------------------------
    // scanning: legs grow a little with progress; success: corners round out until the brackets become a circle
    const float morph = res == 1 ? ease_in_out(clamp01(rt / 0.42)) : 0.f;
    const float breathe = res == 0 ? float(std::sin(f.t * 2.4)) * 0.022f * (1 - f.face) : 0.f;
    const float h = 50 * k * (1 + breathe);
    const float r0 = 0.30f * h, r = lerp(r0, h, morph);
    const float l_scan = lerp(0.20f * h, h - r0, 0.55f * ease_out(std::clamp(f.progress, 0.f, 1.f)));
    const float l = lerp(l_scan, h - r, morph);
    const float bracket_a = res == 0 ? lerp(0.5f, 1.f, f.face) : 1.f;
    {
        Pen pen(white(A * bracket_a), sw);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        GraphicsPath p;
        add_corner(p, cx, cy, h, r, l, -1, -1);
        add_corner(p, cx, cy, h, r, l, +1, -1);
        add_corner(p, cx, cy, h, r, l, +1, +1);
        add_corner(p, cx, cy, h, r, l, -1, +1);
        g.DrawPath(&pen, &p);
    }

    // --- the face: eyes, nose, mouth (follows the user's head a little) -----------------------------------
    float face_a = lerp(0.35f, 1.f, f.face);
    if (res == 1) face_a *= 1 - clamp01(rt / 0.16);
    if (face_a > 0.01f) {
        Pen pen(white(A * face_a), sw);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        const float lk = std::clamp(f.look, -1.f, 1.f);
        const float fx = cx + lk * 7 * k;        // features slide
        const float nx = fx + lk * 3 * k;        // the nose a little further: reads as a turn, not a slide
        const float ey = cy - 13 * k;
        g.DrawLine(&pen, fx - 17 * k, ey - 5 * k, fx - 17 * k, ey + 4 * k);
        g.DrawLine(&pen, fx + 17 * k, ey - 5 * k, fx + 17 * k, ey + 4 * k);
        PointF nose[3] = {PointF(nx + 1 * k, ey - 5 * k), PointF(nx + 1 * k, cy + 7 * k), PointF(nx - 4 * k, cy + 7 * k)};
        g.DrawLines(&pen, nose, 3);
        const float smile = res == -1 ? lerp(8, 0, clamp01(rt / 0.2)) * k : 8 * k;   // not recognised: flat mouth
        const float my = cy + 19 * k;
        g.DrawBezier(&pen, PointF(fx - 16 * k, my), PointF(fx - 7 * k, my + smile), PointF(fx + 7 * k, my + smile),
                     PointF(fx + 16 * k, my));
    }

    // --- success: check mark drawn inside the circle ---------------------------------------------------
    if (res == 1) {
        const float p = ease_out(clamp01((rt - 0.30) / 0.30));
        if (p > 0) {
            Pen pen(white(A), sw * 1.15f);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            pen.SetLineJoin(LineJoinRound);
            PointF tick[3] = {PointF(cx - 0.42f * h, cy + 0.02f * h), PointF(cx - 0.12f * h, cy + 0.32f * h),
                              PointF(cx + 0.44f * h, cy - 0.30f * h)};
            draw_partial(g, pen, tick, 3, p);
        }
    }

    // --- head-turn challenge: two quiet chevrons beside the glyph, pulsing outward ----------------------
    if (res == 0 && f.state == fg::State::Challenge && f.direction != 0) {
        const float side = float(f.direction > 0 ? 1 : -1);
        for (int i = 0; i < 2; ++i) {
            float ph = float(std::fmod(f.t * 1.6 - i * 0.18, 1.0));
            float a = 0.25f + 0.75f * std::max(0.f, std::sin(ph * 3.14159265f));
            float x = cx + side * (h + 22 * k + i * 11 * k);
            Pen pen(white(A * a), 3.0f * k);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            pen.SetLineJoin(LineJoinRound);
            PointF ch[3] = {PointF(x - side * 4 * k, cy - 9 * k), PointF(x + side * 4 * k, cy), PointF(x - side * 4 * k, cy + 9 * k)};
            g.DrawLines(&pen, ch, 3);
        }
    }

    // --- one line of text --------------------------------------------------------------------------------
    {
        FontFamily semibold(L"Segoe UI Semibold"), regular(L"Segoe UI");
        FontFamily& ff = semibold.GetLastStatus() == Ok ? semibold : regular;
        Font font(&ff, 15 * k, FontStyleRegular, UnitPixel);
        SolidBrush tb(white(A * (res == 0 ? 0.72f : 0.92f)));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        const wchar_t* text = res == 1 ? L"Unlocked" : res == -1 ? L"Not recognised" : f.hint;
        g.DrawString(text, -1, &font, RectF(18 * k, 184 * k, W - 36 * k, 46 * k), &sf, &tb);
    }
}

void Overlay::start(HWND parent, Source src, DisplayCb on_display, bool wait_for_input, RevealCb on_reveal) {
    if (th_.joinable()) return;
    src_ = std::move(src);
    on_display_ = std::move(on_display);
    on_reveal_ = std::move(on_reveal);
    wait_for_input_ = wait_for_input;
    revealed_ = !wait_for_input;
    th_ = std::thread(&Overlay::run, this, parent);
}

void Overlay::stop() {
    if (DWORD t = tid_) PostThreadMessageW(t, WM_QUIT, 0, 0);
    if (th_.joinable()) th_.join();
}

void Overlay::result(bool unlocked, bool sound) {
    result_t_ = now_s();
    result_ = unlocked ? 1 : -1;
    if (sound && unlocked) play_unlock_sound();   // the only sound: no scan-start or failure sounds
}

void Overlay::reset() {
    result_ = 0;
    t_start_ = now_s();
    show(true);
}

void Overlay::show(bool v) {
    visible_ = v;
    if (HWND h = hwnd_) PostMessageW(h, WM_FG_SHOW, v, 0);
}

LRESULT CALLBACK Overlay::wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
    } else if (m == WM_TIMER && self) {
        self->paint();
        return 0;
    } else if (m == WM_FG_SHOW && self) {
        self->apply_visibility();
        return 0;
    } else if (m == WM_INPUT && self) {
        self->on_input((HRAWINPUT)l);
        return DefWindowProcW(h, m, w, l);   // required so the system frees the raw input buffer
    } else if (m == WM_POWERBROADCAST && w == PBT_POWERSETTINGCHANGE && self) {
        auto* ps = (POWERBROADCAST_SETTING*)l;
        if (IsEqualGUID(ps->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && ps->DataLength == sizeof(DWORD)) {
            DWORD state = *(DWORD*)ps->Data;   // 0 off, 1 on, 2 dimmed
            if (state == 0 && self->wait_for_input_) {   // screen off: the curtain comes back down on wake
                self->revealed_ = false;
                self->lift_tried_ = false;
                self->apply_visibility();
            }
            if (self->on_display_) self->on_display_(state == 1);
        }
        return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}

void Overlay::apply_visibility() {
    HWND h = hwnd_;
    if (!h) return;
    bool want = visible_ && revealed_;
    if (want != bool(IsWindowVisible(h))) ShowWindow(h, want ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void Overlay::on_input(HRAWINPUT ri) {
    if (revealed_) return;
    RAWINPUT in;
    UINT sz = sizeof in;
    if (GetRawInputData(ri, RID_INPUT, &in, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
    bool press = false;
    if (in.header.dwType == RIM_TYPEKEYBOARD) {
        press = !(in.data.keyboard.Flags & RI_KEY_BREAK);   // any key down (Up, Enter, Space, ...)
    } else if (in.header.dwType == RIM_TYPEMOUSE) {
        const USHORT down = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN |
                            RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_WHEEL;
        press = (in.data.mouse.usButtonFlags & down) != 0;   // a click / scroll, not just a mouse nudge
    }
    if (press) reveal(L"user input");
}

void Overlay::reveal(const wchar_t* why) {
    if (revealed_) return;
    revealed_ = true;
    log_event(L"overlay revealed by %s", why);
    apply_visibility();
    if (on_reveal_) on_reveal_();
}

void Overlay::run(HWND parent) {
    tid_ = GetCurrentThreadId();
    // draw on the same desktop as the sign-in UI (the secure Winlogon desktop on the lock screen)
    DWORD ptid = parent ? GetWindowThreadProcessId(parent, nullptr) : 0;
    if (HDESK d = ptid ? GetThreadDesktop(ptid) : nullptr) SetThreadDesktop(d);

    ULONG_PTR gdip = 0;
    GdiplusStartupInput gsi;
    GdiplusStartup(&gdip, &gsi, nullptr);

    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Overlay::wndproc, &mod);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = mod;
    wc.lpszClassName = L"FaceGateOverlay";
    RegisterClassExW(&wc);

    HMONITOR mon = MonitorFromWindow(parent, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpi = parent ? GetDpiForWindow(parent) : 96;
    dpi_ = (dpi ? dpi : 96) / 96.0f;
    w_ = int(kHudW * dpi_);
    h_ = int(kHudH * dpi_);
    x_ = (mi.rcMonitor.left + mi.rcMonitor.right - w_) / 2;
    y_ = mi.rcMonitor.top + int(40 * dpi_);

    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                             wc.lpszClassName, L"FaceGate", WS_POPUP, x_, y_, w_, h_, nullptr, nullptr, mod, this);
    hwnd_ = h;
    HPOWERNOTIFY pn = h ? RegisterPowerSettingNotification(h, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE) : nullptr;
    if (h && wait_for_input_) {
        // keyboard + mouse, delivered even though this window never has focus (RIDEV_INPUTSINK)
        RAWINPUTDEVICE rid[2] = {{0x01, 0x06, RIDEV_INPUTSINK, h}, {0x01, 0x02, RIDEV_INPUTSINK, h}};
        if (!RegisterRawInputDevices(rid, 2, sizeof rid[0])) {
            log_event(L"raw input unavailable (%lu): overlay shown immediately", GetLastError());
            revealed_ = true;
        }
    }
    if (h) {
        t_start_ = now_s();
        paint();
        apply_visibility();
        SetTimer(h, 1, 16, nullptr);  // ~60 fps: the morph and check mark need smooth motion
    } else {
        log_event(L"overlay window failed (%lu)", GetLastError());
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (h && wait_for_input_) {
        RAWINPUTDEVICE rid[2] = {{0x01, 0x06, RIDEV_REMOVE, nullptr}, {0x01, 0x02, RIDEV_REMOVE, nullptr}};
        RegisterRawInputDevices(rid, 2, sizeof rid[0]);
    }
    if (pn) UnregisterPowerSettingNotification(pn);
    if (h) DestroyWindow(h);
    hwnd_ = nullptr;
    UnregisterClassW(wc.lpszClassName, mod);
    GdiplusShutdown(gdip);
    tid_ = 0;
}

void Overlay::paint() {
    HWND h = hwnd_;
    if (!h || !src_) return;
    Snapshot s = src_();
    // behind the curtain and the camera already knows you: lift the curtain, like Windows Hello does
    if (wait_for_input_ && !revealed_ && s.waiting && !lift_tried_) {
        lift_tried_ = true;
        if (lift_) lift_(); else lift_curtain_by_input();
        reveal(L"face recognised behind the lock screen");
    }
    if (s.has_face) last_ = s; else { last_.state = s.state; last_.hint = s.hint; last_.progress = s.progress; last_.direction = s.direction; }

    // smooth the glyph's inputs so it never jitters with the landmarks
    const float want_face = s.has_face ? 1.f : 0.f;
    // mesh is centred on the face box, face height = 1: the nose tip (landmark 1) drifts sideways as the head turns.
    // Negated so the glyph moves like a mirror.
    const float want_look = s.has_face && s.mesh.size() > 1 ? std::clamp(-s.mesh[1].x * 6.f, -1.f, 1.f) : 0.f;
    face_ += (want_face - face_) * 0.12f;
    look_ += (want_look - look_) * 0.15f;

    HudFrame f;
    f.t = now_s();
    f.age = f.t - t_start_;
    f.result = result_;
    f.since_result = f.t - result_t_;
    f.state = last_.state;
    f.progress = last_.progress;
    f.direction = last_.direction;
    f.face = face_;
    f.look = look_;
    f.hint = last_.hint.c_str();

    // 32-bit premultiplied DIB for UpdateLayeredWindow
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w_, -h_, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, dib);
    {
        Bitmap canvas(w_, h_, w_ * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&canvas);
        render_hud(g, dpi_, f);
    }
    POINT src{0, 0}, dst{x_, y_};
    SIZE sz{w_, h_};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(h, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (f.result != 0 && hud_opacity(f) <= 0 && IsWindowVisible(h)) ShowWindow(h, SW_HIDE);
}

}  // namespace fgcp
