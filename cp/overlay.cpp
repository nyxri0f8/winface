#include "overlay.h"

#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include "common.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

namespace fgcp {
namespace {

constexpr UINT WM_FG_SHOW = WM_APP + 1;
constexpr float kPi = 3.14159265f;

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

Color with_alpha(Color c, int a) { return Color(BYTE(std::clamp(a, 0, 255)), c.GetR(), c.GetG(), c.GetB()); }

const Color kCyan(255, 90, 220, 255), kGreen(255, 80, 255, 140), kRed(255, 255, 80, 90), kGrey(255, 90, 96, 110);

}  // namespace

void play_sound(const wchar_t* name) {
    std::wstring p = module_dir() + L"\\assets\\" + name + L".wav";
    PlaySoundW(p.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void Overlay::start(HWND parent, Source src, DisplayCb on_display) {
    if (th_.joinable()) return;
    src_ = std::move(src);
    on_display_ = std::move(on_display);
    // mesh edges (uint32 n_tess, n_cont, then uint16 pairs)
    std::ifstream f(module_dir() + L"\\models\\mesh_edges.bin", std::ios::binary);
    uint32_t nt = 0, nc = 0;
    if (f.read((char*)&nt, 4) && f.read((char*)&nc, 4) && nt < 5000 && nc < 1000) {
        tess_.resize(nt * 2);
        cont_.resize(nc * 2);
        f.read((char*)tess_.data(), nt * 4);
        f.read((char*)cont_.data(), nc * 4);
    }
    th_ = std::thread(&Overlay::run, this, parent);
}

void Overlay::stop() {
    if (DWORD t = tid_) PostThreadMessageW(t, WM_QUIT, 0, 0);
    if (th_.joinable()) th_.join();
}

void Overlay::result(bool unlocked, bool sound) {
    result_t_ = now_s();
    result_ = unlocked ? 1 : -1;
    if (sound) play_sound(unlocked ? L"sfx_unlock" : L"sfx_fail");
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
    } else if (m == WM_FG_SHOW) {
        ShowWindow(h, w ? SW_SHOWNOACTIVATE : SW_HIDE);
        return 0;
    } else if (m == WM_POWERBROADCAST && w == PBT_POWERSETTINGCHANGE && self) {
        auto* ps = (POWERBROADCAST_SETTING*)l;
        if (IsEqualGUID(ps->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && ps->DataLength == sizeof(DWORD)) {
            DWORD state = *(DWORD*)ps->Data;   // 0 off, 1 on, 2 dimmed
            if (self->on_display_) self->on_display_(state == 1);
        }
        return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
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
    w_ = int(420 * dpi_);
    h_ = int(300 * dpi_);
    x_ = (mi.rcMonitor.left + mi.rcMonitor.right - w_) / 2;
    y_ = mi.rcMonitor.top + int(36 * dpi_);

    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                             wc.lpszClassName, L"FaceGate", WS_POPUP, x_, y_, w_, h_, nullptr, nullptr, mod, this);
    hwnd_ = h;
    HPOWERNOTIFY pn = h ? RegisterPowerSettingNotification(h, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE) : nullptr;
    if (h) {
        t_start_ = now_s();
        paint();
        if (visible_) ShowWindow(h, SW_SHOWNOACTIVATE);
        SetTimer(h, 1, 33, nullptr);  // ~30 fps
    } else {
        log_event(L"overlay window failed (%lu)", GetLastError());
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
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
    if (s.has_face) last_ = s; else { last_.state = s.state; last_.hint = s.hint; last_.progress = s.progress; last_.direction = s.direction; }
    const double t = now_s();
    const int res = result_;
    const double rt = t - result_t_;

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
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));
        const float k = dpi_;

        // fade in on start; fade out 1.2 s after a result
        float alpha = float(std::min(1.0, (t - t_start_) / 0.25));
        if (res != 0 && rt > 0.9) alpha *= float(std::max(0.0, 1 - (rt - 0.9) / 0.3));
        Color accent = res == 1 ? kGreen : res == -1 ? kRed : kCyan;
        float shake = res == -1 && rt < 0.45 ? float(std::sin(rt * 60) * 10 * (1 - rt / 0.45)) * k : 0;

        // pill background
        GraphicsPath pill;
        float r = 28 * k, W = float(w_), H = float(h_);
        pill.AddArc(0.f, 0.f, 2 * r, 2 * r, 180, 90);
        pill.AddArc(W - 2 * r - 1, 0.f, 2 * r, 2 * r, 270, 90);
        pill.AddArc(W - 2 * r - 1, H - 2 * r - 1, 2 * r, 2 * r, 0, 90);
        pill.AddArc(0.f, H - 2 * r - 1, 2 * r, 2 * r, 90, 90);
        pill.CloseFigure();
        SolidBrush bg(Color(BYTE(215 * alpha), 10, 12, 18));
        g.FillPath(&bg, &pill);
        Pen border(with_alpha(accent, int(110 * alpha)), 1.5f * k);
        g.DrawPath(&border, &pill);

        const float cx = W / 2 + shake, cy = 128 * k, R = 104 * k;

        // Face ID tick ring
        int lit = int(std::round(std::clamp(last_.progress, 0.f, 1.f) * 36));
        if (res == 1) lit = 36;
        for (int i = 0; i < 36; ++i) {
            float a = i / 36.f * 2 * kPi - kPi / 2;
            bool on = i < lit;
            float r0 = R, r1 = R + (on ? 16 : 9) * k;
            Pen p(with_alpha(on ? accent : kGrey, int((on ? 255 : 150) * alpha)), (on ? 3.2f : 2.2f) * k);
            p.SetStartCap(LineCapRound);
            p.SetEndCap(LineCapRound);
            g.DrawLine(&p, cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a), cy + r1 * std::sin(a));
        }
        if (res == 0) {  // spinning scan arc
            Pen arc(with_alpha(accent, int(200 * alpha)), 2.2f * k);
            float st = float(std::fmod(t * 240, 360));
            g.DrawArc(&arc, cx - R * 0.9f, cy - R * 0.9f, R * 1.8f, R * 1.8f, st, 55);
        }

        // neon mesh (vector only)
        if (!last_.mesh.empty() && res != 1) {
            const float sc = 150 * k;
            auto P = [&](int i) { return PointF(cx + last_.mesh[i].x * sc, cy + last_.mesh[i].y * sc); };
            Pen dimp(with_alpha(accent, int(60 * alpha)), 1.0f * k);
            for (size_t e = 0; e + 1 < tess_.size(); e += 2) g.DrawLine(&dimp, P(tess_[e]), P(tess_[e + 1]));
            Pen glow(with_alpha(accent, int(45 * alpha)), 5.0f * k), bright(with_alpha(accent, int(235 * alpha)), 1.5f * k);
            for (size_t e = 0; e + 1 < cont_.size(); e += 2) g.DrawLine(&glow, P(cont_[e]), P(cont_[e + 1]));
            for (size_t e = 0; e + 1 < cont_.size(); e += 2) g.DrawLine(&bright, P(cont_[e]), P(cont_[e + 1]));
            // scan sweep line across the face
            if (res == 0) {
                float band = cy - 0.55f * sc + float(std::fmod(t * 0.9, 1.0)) * 1.1f * sc;
                LinearGradientBrush sweep(PointF(cx - 0.45f * sc, band), PointF(cx + 0.45f * sc, band),
                                          with_alpha(accent, 0), with_alpha(Color(255, 255, 255, 255), int(160 * alpha)));
                Color blend[3] = {with_alpha(accent, 0), Color(BYTE(170 * alpha), 255, 255, 255), with_alpha(accent, 0)};
                REAL pos[3] = {0.f, 0.5f, 1.f};
                sweep.SetInterpolationColors(blend, pos, 3);
                g.FillRectangle(&sweep, cx - 0.45f * sc, band - 1.2f * k, 0.9f * sc, 2.4f * k);
            }
        } else if (res == 0) {  // nobody yet: breathing circle
            float br = float(0.5 + 0.5 * std::sin(t * 3));
            Pen p(with_alpha(accent, int((70 + 80 * br) * alpha)), 2 * k);
            g.DrawEllipse(&p, cx - 46 * k, cy - 58 * k, 92 * k, 116 * k);
        }

        // success: check mark drawn in; failure: X
        if (res != 0) {
            float prog = float(std::clamp(rt / 0.35, 0.0, 1.0));
            Pen p(with_alpha(accent, int(255 * alpha)), 7 * k);
            p.SetStartCap(LineCapRound);
            p.SetEndCap(LineCapRound);
            p.SetLineJoin(LineJoinRound);
            if (res == 1) {
                PointF a(cx - 34 * k, cy + 2 * k), b(cx - 8 * k, cy + 28 * k), c(cx + 38 * k, cy - 26 * k);
                float p1 = std::min(prog * 2, 1.f), p2 = std::max(0.f, prog * 2 - 1);
                g.DrawLine(&p, a, PointF(a.X + (b.X - a.X) * p1, a.Y + (b.Y - a.Y) * p1));
                if (p2 > 0) g.DrawLine(&p, b, PointF(b.X + (c.X - b.X) * p2, b.Y + (c.Y - b.Y) * p2));
            } else {
                float d = 26 * k * prog;
                g.DrawLine(&p, cx - d, cy - d, cx + d, cy + d);
                g.DrawLine(&p, cx - d, cy + d, cx + d, cy - d);
            }
        }

        // challenge: pulsing target dot + arrow toward it
        if (res == 0 && last_.state == fg::State::Challenge && last_.direction != 0) {
            float side = float(last_.direction);
            float pulse = float(0.5 + 0.5 * std::sin(t * 10));
            PointF dot(cx + side * (W / 2 - 34 * k), cy);
            SolidBrush halo(with_alpha(accent, int(70 * alpha))), core(with_alpha(accent, int(255 * alpha)));
            float hr = (16 + 6 * pulse) * k, cr = 9 * k;
            g.FillEllipse(&halo, dot.X - hr, dot.Y - hr, 2 * hr, 2 * hr);
            g.FillEllipse(&core, dot.X - cr, dot.Y - cr, 2 * cr, 2 * cr);
            Pen ap(with_alpha(accent, int(230 * alpha)), 4 * k);
            AdjustableArrowCap cap(4, 4);
            ap.SetCustomEndCap(&cap);
            float ax = cx + side * (R + 26 * k);
            g.DrawLine(&ap, ax - side * 8 * k, cy, ax + side * 14 * k, cy);
        }

        // status text
        FontFamily ff(L"Segoe UI");
        Font font(&ff, 15 * k, FontStyleRegular, UnitPixel);
        SolidBrush tb(Color(BYTE(235 * alpha), 235, 240, 245));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        const wchar_t* text = res == 1 ? L"Unlocked" : res == -1 ? L"Not recognised" : last_.hint.c_str();
        g.DrawString(text, -1, &font, RectF(0, H - 42 * k, W, 30 * k), &sf, &tb);
    }
    POINT src{0, 0}, dst{x_, y_};
    SIZE sz{w_, h_};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(h, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (res != 0 && rt > 1.25 && IsWindowVisible(h)) ShowWindow(h, SW_HIDE);
}

}  // namespace fgcp
