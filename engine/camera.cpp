// FaceGate engine - Media Foundation camera. MJPG 720p from the device, decoded to RGB32 by MF,
// converted to mirrored BGR (same convention as the Python bench, so yaw signs match).
#include "camera.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace fg {
namespace {

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

std::wstring get_string(IMFActivate* a, const GUID& key) {
    WCHAR* s = nullptr;
    UINT32 n = 0;
    std::wstring out;
    if (SUCCEEDED(a->GetAllocatedString(key, &s, &n))) { out.assign(s, n); CoTaskMemFree(s); }
    return out;
}

}  // namespace

std::vector<CameraEntry> list_cameras() {
    std::vector<CameraEntry> out;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        IMFAttributes* attr = nullptr;
        MFCreateAttributes(&attr, 1);
        attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devs = nullptr;
        UINT32 n = 0;
        if (SUCCEEDED(MFEnumDeviceSources(attr, &devs, &n))) {
            for (UINT32 i = 0; i < n; ++i) {
                std::wstring link = lower(get_string(devs[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK));
                out.push_back({get_string(devs[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME), link, link.rfind(L"\\\\?\\usb#", 0) == 0});
                devs[i]->Release();
            }
            CoTaskMemFree(devs);
        }
        release(attr);
        MFShutdown();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return out;
}

Camera::Camera(const std::wstring& allowed_prefix) : allowed_(lower(allowed_prefix)) {}
Camera::~Camera() { close(); }

bool Camera::open(int width, int height, int fps) {
    const double t0 = now_ms();
    // MF needs COM on the calling thread (balanced per call; the capture thread keeps its own)
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct CoGuard { bool on; ~CoGuard() { if (on) CoUninitialize(); } } guard{SUCCEEDED(co)};
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { err_ = "MFStartup failed"; return false; }
    mf_started_ = true;

    // 1. find the whitelisted camera
    IMFAttributes* attr = nullptr;
    MFCreateAttributes(&attr, 1);
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attr, &devs, &count);
    release(attr);
    if (FAILED(hr)) { err_ = "MFEnumDeviceSources failed"; return false; }
    for (UINT32 i = 0; i < count; ++i) {
        std::wstring link = lower(get_string(devs[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK));
        // hardware USB cameras only: virtual cameras use swd#/root# style links
        bool hw = link.rfind(L"\\\\?\\usb#", 0) == 0;
        bool ok = hw && (allowed_.empty() || link.find(allowed_) != std::wstring::npos);
        if (ok && !source_ && SUCCEEDED(devs[i]->ActivateObject(IID_PPV_ARGS(&source_)))) {
            info_.name = get_string(devs[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            info_.symlink = link;
        }
        devs[i]->Release();
    }
    CoTaskMemFree(devs);
    if (!source_) { err_ = "whitelisted camera not found"; return false; }

    // 2. source reader with MF's built-in MJPG decoder + colour converter
    MFCreateAttributes(&attr, 2);
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    attr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
    hr = MFCreateSourceReaderFromMediaSource(source_, attr, &reader_);
    release(attr);
    if (FAILED(hr)) { err_ = "MFCreateSourceReaderFromMediaSource failed"; return false; }
    const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;

    // 3. pick the device format: MJPG at the requested size (fast USB path), else anything at that size
    IMFMediaType* best = nullptr;
    int best_rank = -1;
    for (DWORD i = 0;; ++i) {
        IMFMediaType* t = nullptr;
        if (FAILED(reader_->GetNativeMediaType(stream, i, &t))) break;
        GUID sub{};
        UINT32 w = 0, h = 0, num = 0, den = 1;
        t->GetGUID(MF_MT_SUBTYPE, &sub);
        MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
        int rank = -1;
        if ((int)w == width && (int)h == height && den && num / den >= (UINT32)fps)
            rank = (sub == MFVideoFormat_MJPG) ? 2 : 1;
        if (rank > best_rank) { release(best); best = t; best_rank = rank; } else t->Release();
    }
    if (!best) { err_ = "no usable camera format"; return false; }
    hr = reader_->SetCurrentMediaType(stream, nullptr, best);
    release(best);
    if (FAILED(hr)) { err_ = "set device format failed"; return false; }

    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, width, height);
    hr = reader_->SetCurrentMediaType(stream, nullptr, out);
    release(out);
    if (FAILED(hr)) { err_ = "set RGB32 output failed"; return false; }

    IMFMediaType* cur = nullptr;
    reader_->GetCurrentMediaType(stream, &cur);
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
    UINT32 st = 0;
    stride_ = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? (LONG)st : (LONG)w * 4;
    release(cur);
    w_ = (int)w;
    h_ = (int)h;

    run_ = true;
    th_ = std::thread(&Camera::loop, this);
    // wait for the first frame so open_ms() measures "camera ready"
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::seconds(3), [&] { return seq_ > 0 || !run_; });
    open_ms_ = now_ms() - t0;
    if (seq_ == 0) { err_ = "no frame within 3 s"; return false; }
    return true;
}

void Camera::loop() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct CoGuard { bool on; ~CoGuard() { if (on) CoUninitialize(); } } guard{SUCCEEDED(co)};
    const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    while (run_) {
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample* s = nullptr;
        HRESULT hr = reader_->ReadSample(stream, 0, &idx, &flags, &ts, &s);
        if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            err_ = "camera read failed";
            run_ = false;
            cv_.notify_all();
            break;
        }
        if (!s) continue;
        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(s->ConvertToContiguousBuffer(&buf))) {
            BYTE* p = nullptr;
            DWORD len = 0;
            IMF2DBuffer* b2 = nullptr;
            LONG pitch = stride_;
            bool locked2d = SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&p, &pitch));
            if (!locked2d && SUCCEEDED(buf->Lock(&p, nullptr, &len)) && stride_ < 0)
                p += size_t(-stride_) * (h_ - 1);  // bottom-up buffer: start at the last row
            if (p) {
                Image img(w_, h_, 3);
                for (int y = 0; y < h_; ++y) {
                    const BYTE* src = p + (ptrdiff_t)y * pitch;
                    uint8_t* dst = img.row(y);
                    for (int x = 0; x < w_; ++x) {   // BGRA -> BGR, mirrored horizontally
                        const BYTE* q = src + (size_t)(w_ - 1 - x) * 4;
                        dst[3 * x] = q[0]; dst[3 * x + 1] = q[1]; dst[3 * x + 2] = q[2];
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    frame_ = std::move(img);
                    ++seq_;
                    ts_ms_ = now_ms();
                }
                cv_.notify_all();
            }
            if (locked2d) b2->Unlock2D(); else if (p) buf->Unlock();
            release(b2);
            buf->Release();
        }
        s->Release();
    }
}

bool Camera::next(Image& out, uint64_t& seq, double& ts_ms, int timeout_ms) {
    std::unique_lock<std::mutex> lk(mu_);
    if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return seq_ > seq || !run_; })) return false;
    if (seq_ <= seq) return false;
    out = frame_;
    seq = seq_;
    ts_ms = ts_ms_;
    return true;
}

void Camera::close() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct CoGuard { bool on; ~CoGuard() { if (on) CoUninitialize(); } } guard{SUCCEEDED(co)};
    run_ = false;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    release(reader_);
    if (source_) { source_->Shutdown(); release(source_); }  // turns the camera (and its LED) off
    if (mf_started_) { MFShutdown(); mf_started_ = false; }
}

}  // namespace fg
