// Checks the lock-screen pieces without the lock screen:
//   fgoverlaytest render <outdir> [backdrop.jpg]  render the real HUD code over a backdrop, 60 fps PNG frames
//   fgoverlaytest live [shot.png]                  real Overlay window: hidden until a key press, shown after it
//   fgoverlaytest dll <FaceGateCP.dll>             load the provider DLL like LogonUI does and create the provider
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
    auto find = [] { return FindWindowW(L"FaceGateOverlay", L"FaceGate"); };
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
    ov2.set_curtain_lifter([&] { ++lifts; inject_key(VK_F24); });   // real one sends Shift + a corner click
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
    check(SUCCEEDED(hr) && cf, "class factory for the FaceGate CLSID");
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
    else if (cmd == L"sound") { fgcp::play_unlock_sound(); rc = 0; }   // then exit at once, like LogonUI after sign-in
    else printf("usage: fgoverlaytest render <outdir> [backdrop] | live [shot.png] | dll <FaceGateCP.dll> | wav <file.wav>\n");
    GdiplusShutdown(tok);
    return rc;
}
