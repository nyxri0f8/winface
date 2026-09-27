// FaceGate CP - Face ID-style overlay at the top of the lock screen (vector mesh only, never camera pixels).
// Also watches the display power state so the scan restarts when the lid opens / screen wakes.
#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "scanner.h"

namespace fgcp {

class Overlay {
public:
    using Source = std::function<Snapshot()>;
    using DisplayCb = std::function<void(bool on)>;

    ~Overlay() { stop(); }
    void start(HWND parent, Source src, DisplayCb on_display);
    void stop();
    void result(bool unlocked, bool sound = true);   // success/fail animation + sound (thread-safe)
    void show(bool visible);      // thread-safe
    void reset();                 // new scan: clear result, fade in again (thread-safe)

private:
    void run(HWND parent);
    void paint();
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);

    Source src_;
    DisplayCb on_display_;
    std::thread th_;
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<DWORD> tid_{0};
    std::atomic<int> result_{0};      // 0 none, 1 unlocked, -1 failed
    std::atomic<double> result_t_{0};
    std::atomic<bool> visible_{true};
    int w_ = 0, h_ = 0, x_ = 0, y_ = 0;
    float dpi_ = 1;
    std::vector<unsigned short> tess_, cont_;
    Snapshot last_;
    std::atomic<double> t_start_{0};
};

void play_sound(const wchar_t* name);  // "sfx_scan" | "sfx_unlock" | "sfx_fail"

}  // namespace fgcp
