// WinFace engine - webcam capture via Media Foundation (no OpenCV).
// Only the whitelisted built-in camera is accepted; virtual / software cameras are refused.
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "image.h"

struct IMFSourceReader;
struct IMFMediaSource;

namespace fg {

struct CameraInfo {
    std::wstring name;
    std::string format;     // e.g. "1280x720 @30 fps" or "1920x1080 @30 fps (scaled)"
    std::wstring symlink;   // e.g. \\?\usb#vid_0408&pid_5496&mi_00#...
};

// all video capture devices; `hardware` = a real camera on a hardware bus (virtual cameras are never usable),
// `infrared` = a Windows Hello IR sensor (grey-scale; cannot be used by the colour face models)
struct CameraEntry { std::wstring name, symlink; bool hardware, infrared; };
bool is_hardware_camera(const std::wstring& symlink);
bool is_infrared_camera(const std::wstring& friendly_name);
bool is_capture_device(const std::wstring& friendly_name);   // HDMI capture dongles etc. - never used
std::wstring camera_instance(const std::wstring& symlink);  // bus#ids#instance: identifies one physical camera
std::vector<CameraEntry> list_cameras();

class Camera {
public:
    // `allowed_prefix`: lower-case substring the device symbolic link must contain
    // (e.g. L"usb#vid_0408&pid_5496&mi_00"). Empty = automatic: the first real colour camera that works.
    explicit Camera(const std::wstring& allowed_prefix);
    ~Camera();

    bool open(int width = 1280, int height = 720, int fps = 30);
    void close();
    // Latest frame newer than `last_seq`; waits up to timeout_ms. Returns false on timeout/error.
    bool next(Image& out, uint64_t& seq, double& ts_ms, int timeout_ms = 200);

    const CameraInfo& info() const { return info_; }
    double open_ms() const { return open_ms_; }
    const std::string& error() const { return err_; }

private:
    void loop();

    std::wstring allowed_;
    CameraInfo info_;
    IMFMediaSource* source_ = nullptr;
    IMFSourceReader* reader_ = nullptr;
    int w_ = 0, h_ = 0;
    long stride_ = 0;
    double open_ms_ = 0;
    std::string err_;

    std::thread th_;
    std::atomic<bool> run_{false};
    std::mutex mu_;
    std::condition_variable cv_;
    Image frame_;
    uint64_t seq_ = 0;
    double ts_ms_ = 0;
    bool mf_started_ = false;
};

}  // namespace fg
