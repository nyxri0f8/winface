#include "intruder.h"

#include <bcrypt.h>
#include <ncrypt.h>
#include <sddl.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

#include "common.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace fgcp {
namespace {

const wchar_t* kKey = L"WinFaceIntruderKey";
const char kMagic[4] = {'W', 'F', 'I', '1'};

std::wstring hr_text(const wchar_t* what, LONG s) {
    wchar_t b[96];
    swprintf_s(b, L"%s failed (0x%08X)", what, (unsigned)s);
    return b;
}

struct Sd {   // a security descriptor from SDDL, for CreateFile / CreateDirectory
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    explicit Sd(const wchar_t* sddl) { ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr); }
    ~Sd() { if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor); }
};
const wchar_t* kDirSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";   // SYSTEM + Administrators, nobody else
const wchar_t* kFileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";

BCRYPT_OAEP_PADDING_INFO oaep() { return {BCRYPT_SHA256_ALGORITHM, nullptr, 0}; }

// AES-256-GCM in place of `data`; key, nonce (12) and tag (16) are outputs/inputs
bool aes_gcm(bool encrypt, const BYTE key[32], const BYTE nonce[12], BYTE tag[16], std::vector<BYTE>& data, std::wstring& err) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE k = nullptr;
    NTSTATUS s = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (s == 0) s = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (s == 0) s = BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key, 32, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = (PUCHAR)nonce;
    info.cbNonce = 12;
    info.pbTag = tag;
    info.cbTag = 16;
    ULONG out = 0;
    std::vector<BYTE> res(data.size());
    if (s == 0)
        s = encrypt ? BCryptEncrypt(k, data.data(), (ULONG)data.size(), &info, nullptr, 0, res.data(), (ULONG)res.size(), &out, 0)
                    : BCryptDecrypt(k, data.data(), (ULONG)data.size(), &info, nullptr, 0, res.data(), (ULONG)res.size(), &out, 0);
    if (k) BCryptDestroyKey(k);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    SecureZeroMemory(data.data(), data.size());
    if (s != 0) { err = hr_text(encrypt ? L"encrypt" : L"decrypt (file damaged or tampered with)", s); return false; }
    data.swap(res);
    return true;
}

bool open_key(const wchar_t* provider, const wchar_t* name, DWORD flags, NCRYPT_PROV_HANDLE& prov, NCRYPT_KEY_HANDLE& key,
              std::wstring& err) {
    prov = 0;
    key = 0;
    SECURITY_STATUS s = NCryptOpenStorageProvider(&prov, provider, 0);
    if (s == ERROR_SUCCESS) s = NCryptOpenKey(prov, &key, name, 0, flags | NCRYPT_SILENT_FLAG);
    if (s != ERROR_SUCCESS) {
        err = hr_text(L"open the intruder-photo key (turn intruder photos on in WinFace)", s);
        if (prov) NCryptFreeObject(prov);
        prov = 0;
        return false;
    }
    return true;
}

}  // namespace

bool seal_blob(const std::vector<BYTE>& plain, std::vector<BYTE>& out, const wchar_t* provider, const wchar_t* name, DWORD flags,
               std::wstring& err) {
    BYTE key[32], nonce[12], tag[16];
    BCryptGenRandom(nullptr, key, sizeof key, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(nullptr, nonce, sizeof nonce, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::vector<BYTE> body = plain;
    bool ok = aes_gcm(true, key, nonce, tag, body, err);
    NCRYPT_PROV_HANDLE prov;
    NCRYPT_KEY_HANDLE k;
    DWORD wl = 0;
    std::vector<BYTE> wrapped;
    if (ok && (ok = open_key(provider, name, flags, prov, k, err))) {
        auto pad = oaep();
        SECURITY_STATUS s = NCryptEncrypt(k, key, sizeof key, &pad, nullptr, 0, &wl, NCRYPT_PAD_OAEP_FLAG);
        wrapped.resize(wl);
        if (s == ERROR_SUCCESS) s = NCryptEncrypt(k, key, sizeof key, &pad, wrapped.data(), wl, &wl, NCRYPT_PAD_OAEP_FLAG);
        if (s != ERROR_SUCCESS) { err = hr_text(L"seal the photo key", s); ok = false; }
        NCryptFreeObject(k);
        NCryptFreeObject(prov);
    }
    SecureZeroMemory(key, sizeof key);
    if (!ok) return false;
    ULONGLONG when = filetime_now();
    uint32_t wlen = wl;
    out.clear();
    out.insert(out.end(), kMagic, kMagic + 4);
    out.insert(out.end(), (BYTE*)&when, (BYTE*)&when + 8);
    out.insert(out.end(), (BYTE*)&wlen, (BYTE*)&wlen + 4);
    out.insert(out.end(), wrapped.begin(), wrapped.begin() + wl);
    out.insert(out.end(), nonce, nonce + 12);
    out.insert(out.end(), tag, tag + 16);
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

bool open_blob(const std::vector<BYTE>& in, std::vector<BYTE>& plain, ULONGLONG& when, const wchar_t* provider, const wchar_t* name,
               DWORD flags, std::wstring& err) {
    if (in.size() < 4 + 8 + 4 + 12 + 16 || memcmp(in.data(), kMagic, 4) != 0) { err = L"not a WinFace photo"; return false; }
    memcpy(&when, in.data() + 4, 8);
    uint32_t wlen = 0;
    memcpy(&wlen, in.data() + 12, 4);
    if (wlen > 1024 || in.size() < 16 + size_t(wlen) + 28) { err = L"damaged photo file"; return false; }
    const BYTE* wrapped = in.data() + 16;
    const BYTE* nonce = wrapped + wlen;
    BYTE tag[16];
    memcpy(tag, nonce + 12, 16);
    NCRYPT_PROV_HANDLE prov;
    NCRYPT_KEY_HANDLE k;
    if (!open_key(provider, name, flags, prov, k, err)) return false;
    BYTE key[32];
    DWORD kl = 0;
    auto pad = oaep();
    SECURITY_STATUS s = NCryptDecrypt(k, (PBYTE)wrapped, wlen, &pad, key, sizeof key, &kl, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    NCryptFreeObject(k);
    NCryptFreeObject(prov);
    if (s != ERROR_SUCCESS || kl != 32) { err = hr_text(L"unseal the photo key", s); return false; }
    plain.assign(nonce + 28, in.data() + in.size());
    bool ok = aes_gcm(false, key, nonce, tag, plain, err);
    SecureZeroMemory(key, sizeof key);
    return ok;
}

bool encode_jpeg(const fg::Image& bgr, std::vector<BYTE>& out, int max_width) {
    if (bgr.empty() || bgr.ch != 3) return false;
    // box-downscale by an integer factor to at most max_width
    int f = std::max(1, (bgr.w + max_width - 1) / max_width);
    int w = bgr.w / f, h = bgr.h / f;
    std::vector<BYTE> px(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int dy = 0; dy < f; ++dy) for (int dx = 0; dx < f; ++dx) sum += bgr.row(y * f + dy)[(x * f + dx) * 3 + c];
                px[(size_t(y) * w + x) * 3 + c] = BYTE(sum / (f * f));
            }
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* fac = nullptr;
    IStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (SUCCEEDED(hr)) hr = fac->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 3, (UINT)px.size(), px.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    if (SUCCEEDED(hr)) {
        HGLOBAL g = nullptr;
        GetHGlobalFromStream(stream, &g);
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        const BYTE* p = (const BYTE*)GlobalLock(g);
        out.assign(p, p + st.cbSize.QuadPart);
        GlobalUnlock(g);
    }
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (fac) fac->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    SecureZeroMemory(px.data(), px.size());
    return SUCCEEDED(hr) && !out.empty();
}

std::wstring intruder_dir() { return data_dir() + L"\\intruders"; }

bool intruder_save(const fg::Image& bgr, const std::string& reason, std::wstring& err) {
    std::vector<BYTE> jpeg;
    if (!encode_jpeg(bgr, jpeg)) { err = L"JPEG encoding failed"; return false; }
    std::vector<BYTE> plain(4);
    uint32_t rl = (uint32_t)std::min<size_t>(reason.size(), 200);
    memcpy(plain.data(), &rl, 4);
    plain.insert(plain.end(), reason.begin(), reason.begin() + rl);
    plain.insert(plain.end(), jpeg.begin(), jpeg.end());
    SecureZeroMemory(jpeg.data(), jpeg.size());
    std::vector<BYTE> blob;
    bool ok = seal_blob(plain, blob, MS_PLATFORM_CRYPTO_PROVIDER, kKey, NCRYPT_MACHINE_KEY_FLAG, err);
    SecureZeroMemory(plain.data(), plain.size());
    if (!ok) return false;
    Sd dsd(kDirSddl), fsd(kFileSddl);
    CreateDirectoryW(intruder_dir().c_str(), &dsd.sa);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"%04d%02d%02d-%02d%02d%02d-%03d.wfi", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    HANDLE h = CreateFileW((intruder_dir() + L"\\" + name).c_str(), GENERIC_WRITE, 0, &fsd.sa, CREATE_NEW, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = hr_text(L"write photo", (LONG)GetLastError()); return false; }
    DWORD wr = 0;
    ok = WriteFile(h, blob.data(), (DWORD)blob.size(), &wr, nullptr) && wr == blob.size();
    CloseHandle(h);
    // keep the newest kMaxPhotos, none older than kMaxDays
    auto list = intruder_list();
    const ULONGLONG day = 864000000000ULL, now = filetime_now();
    for (size_t i = 0; i < list.size(); ++i)
        if (i >= (size_t)kMaxPhotos || now - list[i].when > kMaxDays * day) intruder_delete(list[i].id);
    return ok;
}

bool intruder_key_ensure(std::wstring& err) {
    Sd dsd(kDirSddl);
    CreateDirectoryW(intruder_dir().c_str(), &dsd.sa);
    // an existing folder (e.g. created by hand) is locked down too
    SetFileSecurityW(intruder_dir().c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, dsd.sa.lpSecurityDescriptor);
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    SECURITY_STATUS s = NCryptOpenStorageProvider(&prov, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (s != ERROR_SUCCESS) { err = hr_text(L"open the TPM", s); return false; }
    if (NCryptOpenKey(prov, &key, kKey, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG) == ERROR_SUCCESS) {
        NCryptFreeObject(key);
        NCryptFreeObject(prov);
        return true;
    }
    s = NCryptCreatePersistedKey(prov, &key, NCRYPT_RSA_ALGORITHM, kKey, 0, NCRYPT_MACHINE_KEY_FLAG);
    DWORD bits = 2048, usage = NCRYPT_ALLOW_DECRYPT_FLAG;
    if (s == ERROR_SUCCESS) s = NCryptSetProperty(key, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof bits, 0);
    if (s == ERROR_SUCCESS) s = NCryptSetProperty(key, NCRYPT_KEY_USAGE_PROPERTY, (BYTE*)&usage, sizeof usage, 0);
    if (s == ERROR_SUCCESS) s = NCryptFinalizeKey(key, 0);
    if (s == ERROR_SUCCESS) {   // usable by the lock screen (SYSTEM) and administrators (the app) only
        PSECURITY_DESCRIPTOR sd = nullptr;
        ULONG len = 0;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd, &len)) {
            s = NCryptSetProperty(key, NCRYPT_SECURITY_DESCR_PROPERTY, (BYTE*)sd, len, DACL_SECURITY_INFORMATION);
            LocalFree(sd);
        }
    }
    if (s != ERROR_SUCCESS) err = hr_text(L"create the intruder-photo TPM key", s);
    if (key) NCryptFreeObject(key);
    NCryptFreeObject(prov);
    return s == ERROR_SUCCESS;
}

std::vector<IntruderPhoto> intruder_list() {
    std::vector<IntruderPhoto> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((intruder_dir() + L"\\*.wfi").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring id = fd.cFileName;
        id = id.substr(0, id.size() - 4);
        IntruderPhoto p{id, 0};
        HANDLE f = CreateFileW((intruder_dir() + L"\\" + fd.cFileName).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            BYTE head[12];
            DWORD rd = 0;
            if (ReadFile(f, head, 12, &rd, nullptr) && rd == 12 && memcmp(head, kMagic, 4) == 0) memcpy(&p.when, head + 4, 8);
            CloseHandle(f);
        }
        if (p.when == 0) p.when = (ULONGLONG(fd.ftCreationTime.dwHighDateTime) << 32) | fd.ftCreationTime.dwLowDateTime;
        out.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const IntruderPhoto& a, const IntruderPhoto& b) { return a.when > b.when; });
    return out;
}

bool intruder_open(const std::wstring& id, std::vector<BYTE>& jpeg, std::string& reason, ULONGLONG& when, std::wstring& err) {
    if (id.find_first_of(L"\\/:.") != std::wstring::npos) { err = L"bad photo id"; return false; }
    HANDLE f = CreateFileW((intruder_dir() + L"\\" + id + L".wfi").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = L"photo not found"; return false; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<BYTE> in((size_t)std::min<LONGLONG>(sz.QuadPart, 16 << 20));
    DWORD rd = 0;
    bool ok = ReadFile(f, in.data(), (DWORD)in.size(), &rd, nullptr) && rd == in.size();
    CloseHandle(f);
    if (!ok) { err = L"cannot read photo"; return false; }
    std::vector<BYTE> plain;
    if (!open_blob(in, plain, when, MS_PLATFORM_CRYPTO_PROVIDER, kKey, NCRYPT_MACHINE_KEY_FLAG, err)) return false;
    uint32_t rl = 0;
    if (plain.size() < 4) { err = L"damaged photo"; return false; }
    memcpy(&rl, plain.data(), 4);
    if (plain.size() < 4 + size_t(rl)) { err = L"damaged photo"; return false; }
    reason.assign((const char*)plain.data() + 4, rl);
    jpeg.assign(plain.begin() + 4 + rl, plain.end());
    SecureZeroMemory(plain.data(), plain.size());
    return true;
}

int intruder_delete(const std::wstring& id) {
    int n = 0;
    for (auto& p : intruder_list())
        if ((id == L"all" || p.id == id) && DeleteFileW((intruder_dir() + L"\\" + p.id + L".wfi").c_str())) ++n;
    return n;
}

void intruder_erase_all() {
    intruder_delete(L"all");
    RemoveDirectoryW(intruder_dir().c_str());
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    if (NCryptOpenStorageProvider(&prov, MS_PLATFORM_CRYPTO_PROVIDER, 0) != ERROR_SUCCESS) return;
    if (NCryptOpenKey(prov, &key, kKey, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG) == ERROR_SUCCESS && NCryptDeleteKey(key, 0) != ERROR_SUCCESS)
        NCryptFreeObject(key);
    NCryptFreeObject(prov);
}

}  // namespace fgcp
