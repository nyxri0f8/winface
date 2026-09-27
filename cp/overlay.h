// FaceGate CP - minimal Face ID-style HUD at the top of the lock screen (a vector glyph only, never camera pixels).
// Also watches the display power state so the scan restarts when the lid opens / screen wakes.
// On the lock/sign-in screen it stays hidden behind the Windows curtain (the camera still scans). The curtain lifts
// when the user presses a key / clicks, or - like Windows Hello - by itself as soon as the camera recognises an
// enrolled face; either way the HUD appears together with the sign-in page.
#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>

#include "scanner.h"

namespace Gdiplus { class Graphics; }

namespace fgcp {

// HUD size in DIPs (multiplied by the DPI scale)
constexpr int kHudW = 248, kHudH = 248;

// Everything one HUD frame depends on. render_hud is a pure function of this, so tools/fgoverlaytest can render
// exactly what the lock screen shows.
struct HudFrame {
    double t = 0;             // clock (s), drives looping motion
    double age = 1;           // seconds since this scan started (appear animation)
    int result = 0;           // 0 scanning, 1 unlocked, -1 not recognised
    double since_result = 0;  // seconds since the result
    fg::State state = fg::State::Search;
    float progress = 0;       // 0..1
    int direction = 0;        // head-turn challenge: -1 left, +1 right, 0 none
    float face = 0;           // 0..1 smoothed "a face is in view"
    float look = 0;           // -1..1 smoothed head offset (the glyph's face follows the user)
    const wchar_t* hint = L"";
};
void render_hud(Gdiplus::Graphics& g, float scale, const HudFrame& f);   // g covers kHudW x kHudH * scale
double hud_opacity(const HudFrame& f);                                    // 0 once the result has faded out

class Overlay {
public:
    using Source = std::function<Snapshot()>;
    using DisplayCb = std::function<void(bool on)>;
    using RevealCb = std::function<void()>;

    ~Overlay() { stop(); }
    // wait_for_input: keep the window hidden until a key press / mouse click (lock-screen curtain), then call on_reveal
    void start(HWND parent, Source src, DisplayCb on_display, bool wait_for_input = false, RevealCb on_reveal = nullptr);
    void stop();
    void result(bool unlocked, bool sound = true);   // success/fail animation; sound on success only (thread-safe)
    void show(bool visible);      // thread-safe
    void reset();                 // new scan: clear result, fade in again (thread-safe)
    bool revealed() const { return revealed_; }   // false while waiting behind the lock-screen curtain
    // how the curtain is lifted when a face is recognised (default: a Shift tap + a click in the empty top-left
    // corner, sent from the sign-in desktop). Replaceable for tests.
    void set_curtain_lifter(std::function<void()> fn) { lift_ = std::move(fn); }

private:
    void run(HWND parent);
    void paint();
    void apply_visibility();      // overlay thread only
    void on_input(HRAWINPUT ri);  // overlay thread only
    void reveal(const wchar_t* why);  // overlay thread only
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);

    Source src_;
    DisplayCb on_display_;
    RevealCb on_reveal_;
    std::function<void()> lift_;
    bool lift_tried_ = false;         // one automatic lift per curtain (overlay thread only)
    std::thread th_;
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<DWORD> tid_{0};
    std::atomic<int> result_{0};      // 0 none, 1 unlocked, -1 failed
    std::atomic<double> result_t_{0};
    std::atomic<bool> visible_{true};
    std::atomic<bool> revealed_{true};   // false while the lock-screen curtain is (probably) still down
    bool wait_for_input_ = false;
    int w_ = 0, h_ = 0, x_ = 0, y_ = 0;
    float dpi_ = 1;
    float face_ = 0, look_ = 0;       // smoothed glyph inputs (overlay thread only)
    Snapshot last_;
    std::atomic<double> t_start_{0};
};

// plays assets\sfx_unlock.wav to the end - in fgsound.exe, which outlives LogonUI (it exits right after sign-in)
void play_unlock_sound();

}  // namespace fgcp
