#include "secret.h"

#include <windows.h>
#include <dpapi.h>
#include <ncrypt.h>
#include <sddl.h>
#include <shlobj.h>

#include <ctime>
#include <fstream>
#include <vector>

#include "common.h"

#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace fgcp {
namespace {

const wchar_t* kKeyName = L"WinFacePasswordKey";

std::wstring hr_msg(const wchar_t* what, SECURITY_STATUS s) {
    wchar_t b[128];
    StringCchPrintfW(b, 128, L"%s failed (0x%08X)", what, (unsigned)s);
    return b;
}

bool write_file(const std::wstring& path, const std::vector<BYTE>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char*)data.data(), data.size());
    return bool(f);
}

bool read_file(const std::wstring& path, std::vector<BYTE>& data) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    data.assign(std::istreambuf_iterator<char>(f), {});
    return !data.empty();
}

BCRYPT_OAEP_PADDING_INFO oaep() { return {BCRYPT_SHA256_ALGORITHM, nullptr, 0}; }

}  // namespace

void SecurePassword::wipe() {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

bool running_as_system() {
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    BYTE buf[256];
    DWORD n = 0;
    bool sys = false;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &n)) sys = IsWellKnownSid(((TOKEN_USER*)buf)->User.Sid, WinLocalSystemSid);
    CloseHandle(tok);
    return sys;
}

bool tpm_store(const std::wstring& pw, std::wstring& err) {
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    SECURITY_STATUS s = NCryptOpenStorageProvider(&prov, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (s != ERROR_SUCCESS) { err = hr_msg(L"open TPM provider", s); return false; }
    // a fresh key name every time: earlier keys are SYSTEM-only, so an admin cannot overwrite them
    wchar_t name[64];
    StringCchPrintfW(name, 64, L"%s-%llu", kKeyName, (unsigned long long)GetTickCount64() ^ (unsigned long long)time(nullptr));
    s = NCryptCreatePersistedKey(prov, &key, NCRYPT_RSA_ALGORITHM, name, 0, NCRYPT_MACHINE_KEY_FLAG);
    DWORD bits = 2048, usage = NCRYPT_ALLOW_DECRYPT_FLAG;
    if (s == ERROR_SUCCESS) s = NCryptSetProperty(key, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof bits, 0);
    if (s == ERROR_SUCCESS) s = NCryptSetProperty(key, NCRYPT_KEY_USAGE_PROPERTY, (BYTE*)&usage, sizeof usage, 0);
    if (s == ERROR_SUCCESS) s = NCryptFinalizeKey(key, 0);
    if (s != ERROR_SUCCESS) { err = hr_msg(L"create TPM key", s); if (key) NCryptFreeObject(key); NCryptFreeObject(prov); return false; }

    // encrypt (public-key operation) while we still have access
    BCRYPT_OAEP_PADDING_INFO pad = oaep();
    DWORD cb = 0;
    const BYTE* in = (const BYTE*)pw.c_str();
    DWORD in_len = DWORD(pw.size() * sizeof(wchar_t));
    s = NCryptEncrypt(key, (PBYTE)in, in_len, &pad, nullptr, 0, &cb, NCRYPT_PAD_OAEP_FLAG);
    std::vector<BYTE> blob(cb);
    if (s == ERROR_SUCCESS) s = NCryptEncrypt(key, (PBYTE)in, in_len, &pad, blob.data(), cb, &cb, NCRYPT_PAD_OAEP_FLAG);
    if (s != ERROR_SUCCESS) { err = hr_msg(L"TPM encrypt", s); NCryptFreeObject(key); NCryptFreeObject(prov); return false; }
    blob.resize(cb);

    // lock the key down: SYSTEM only (the lock screen). Admins/users can no longer use it.
    PSECURITY_DESCRIPTOR sd = nullptr;
    ULONG sd_len = 0;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)", SDDL_REVISION_1, &sd, &sd_len)) {
        s = NCryptSetProperty(key, NCRYPT_SECURITY_DESCR_PROPERTY, (BYTE*)sd, sd_len, DACL_SECURITY_INFORMATION);
        LocalFree(sd);
        if (s != ERROR_SUCCESS) { err = hr_msg(L"restrict TPM key", s); NCryptFreeObject(key); NCryptFreeObject(prov); return false; }
    }
    NCryptFreeObject(key);
    NCryptFreeObject(prov);
    // file: uint32 name length (chars), key name, encrypted blob
    std::vector<BYTE> file(4);
    uint32_t nlen = (uint32_t)wcslen(name);
    memcpy(file.data(), &nlen, 4);
    file.insert(file.end(), (BYTE*)name, (BYTE*)(name + nlen));
    file.insert(file.end(), blob.begin(), blob.end());
    CreateDirectoryW(data_dir().c_str(), nullptr);
    if (!write_file(data_dir() + L"\\secret.tpm", file)) { err = L"cannot write secret.tpm"; return false; }
    return true;
}

bool tpm_load(SecurePassword& out, std::wstring& err) {
    std::vector<BYTE> file;
    if (!read_file(data_dir() + L"\\secret.tpm", file) || file.size() < 8) { err = L"no secret.tpm"; return false; }
    uint32_t nlen = 0;
    memcpy(&nlen, file.data(), 4);
    if (nlen == 0 || nlen > 60 || file.size() < 4 + nlen * sizeof(wchar_t) + 16) { err = L"bad secret.tpm"; return false; }
    std::wstring name((const wchar_t*)(file.data() + 4), nlen);
    std::vector<BYTE> blob(file.begin() + 4 + nlen * sizeof(wchar_t), file.end());
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    SECURITY_STATUS s = NCryptOpenStorageProvider(&prov, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (s == ERROR_SUCCESS) s = NCryptOpenKey(prov, &key, name.c_str(), 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (s != ERROR_SUCCESS) { err = hr_msg(L"open TPM key", s); if (prov) NCryptFreeObject(prov); return false; }
    BCRYPT_OAEP_PADDING_INFO pad = oaep();
    DWORD cb = 0;
    s = NCryptDecrypt(key, blob.data(), (DWORD)blob.size(), &pad, nullptr, 0, &cb, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    std::vector<BYTE> plain(cb);
    if (s == ERROR_SUCCESS) s = NCryptDecrypt(key, blob.data(), (DWORD)blob.size(), &pad, plain.data(), cb, &cb, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    NCryptFreeObject(key);
    NCryptFreeObject(prov);
    if (s != ERROR_SUCCESS) { err = hr_msg(L"TPM decrypt", s); SecureZeroMemory(plain.data(), plain.size()); return false; }
    out.value.assign((const wchar_t*)plain.data(), cb / sizeof(wchar_t));
    SecureZeroMemory(plain.data(), plain.size());
    return true;
}

static std::wstring current_key_name() {
    std::vector<BYTE> file;
    if (!read_file(data_dir() + L"\\secret.tpm", file) || file.size() < 8) return L"";
    uint32_t nlen = 0;
    memcpy(&nlen, file.data(), 4);
    if (nlen == 0 || nlen > 60 || file.size() < 4 + nlen * sizeof(wchar_t)) return L"";
    return std::wstring((const wchar_t*)(file.data() + 4), nlen);
}

int tpm_delete_keys(bool keep_current, int* deleted) {
    std::wstring keep = keep_current ? current_key_name() : L"";
    NCRYPT_PROV_HANDLE prov = 0;
    if (NCryptOpenStorageProvider(&prov, MS_PLATFORM_CRYPTO_PROVIDER, 0) != ERROR_SUCCESS) return 0;
    std::vector<std::wstring> names;
    NCryptKeyName* kn = nullptr;
    PVOID state = nullptr;
    while (NCryptEnumKeys(prov, nullptr, &kn, &state, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG) == ERROR_SUCCESS) {
        // WinFace keys, plus keys saved before the product was renamed (FaceGate)
        bool ours = wcsncmp(kn->pszName, kKeyName, wcslen(kKeyName)) == 0 || wcsncmp(kn->pszName, L"FaceGatePasswordKey", 19) == 0;
        if (ours && keep != kn->pszName) names.push_back(kn->pszName);
        NCryptFreeBuffer(kn);
    }
    if (state) NCryptFreeBuffer(state);
    int gone = 0, left = 0;
    for (auto& n : names) {
        NCRYPT_KEY_HANDLE key = 0;
        bool ok = NCryptOpenKey(prov, &key, n.c_str(), 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG) == ERROR_SUCCESS &&
                  NCryptDeleteKey(key, 0) == ERROR_SUCCESS;   // frees the handle on success
        if (!ok && key) NCryptFreeObject(key);
        ++(ok ? gone : left);
    }
    NCryptFreeObject(prov);
    if (deleted) *deleted = gone;
    return left;
}

static std::wstring dpapi_path() {
    wchar_t* p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) { r = std::wstring(p) + L"\\WinFace"; CoTaskMemFree(p); }
    return r;
}

bool dpapi_store(const std::wstring& pw, std::wstring& err) {
    DATA_BLOB in{DWORD(pw.size() * sizeof(wchar_t)), (BYTE*)pw.c_str()}, outb{};
    if (!CryptProtectData(&in, L"WinFace test", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outb)) { err = L"CryptProtectData failed"; return false; }
    std::vector<BYTE> blob(outb.pbData, outb.pbData + outb.cbData);
    LocalFree(outb.pbData);
    std::wstring dir = dpapi_path();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!write_file(dir + L"\\secret.dpapi", blob)) { err = L"cannot write secret.dpapi"; return false; }
    return true;
}

bool dpapi_load(SecurePassword& out, std::wstring& err) {
    std::vector<BYTE> blob;
    if (!read_file(dpapi_path() + L"\\secret.dpapi", blob)) { err = L"no secret.dpapi"; return false; }
    DATA_BLOB in{(DWORD)blob.size(), blob.data()}, outb{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outb)) { err = L"CryptUnprotectData failed"; return false; }
    out.value.assign((const wchar_t*)outb.pbData, outb.cbData / sizeof(wchar_t));
    SecureZeroMemory(outb.pbData, outb.cbData);
    LocalFree(outb.pbData);
    return true;
}

void erase_password_files() {
    std::wstring t = data_dir() + L"\\secret.tpm", d = dpapi_path() + L"\\secret.dpapi";
    DeleteFileW(t.c_str());
    if (!dpapi_path().empty()) {
        DeleteFileW(d.c_str());
        RemoveDirectoryW(dpapi_path().c_str());
    }
}

}  // namespace fgcp
