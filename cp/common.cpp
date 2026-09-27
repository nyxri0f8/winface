#include "common.h"

#include <cstdarg>
#include <cstdio>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace fgcp {

LONG g_dll_refs = 0;

static DWORD reg_dword(HKEY k, const wchar_t* name, DWORD def) {
    DWORD v = 0, sz = sizeof v, type = 0;
    return RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&v, &sz) == ERROR_SUCCESS && type == REG_DWORD ? v : def;
}

Config Config::load() {
    Config c;
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFace", 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return c;
    c.enabled = reg_dword(k, L"Enabled", 0) == 1;
    c.scenarios = reg_dword(k, L"Scenarios", 1);
    c.test_mode = reg_dword(k, L"TestMode", 0) == 1;
    wchar_t sid[256] = {};
    DWORD sz = sizeof sid, type = 0;
    if (RegQueryValueExW(k, L"UserSid", nullptr, &type, (BYTE*)sid, &sz) == ERROR_SUCCESS && type == REG_SZ) c.user_sid = sid;
    wchar_t cam[200] = {};
    sz = sizeof cam - sizeof(wchar_t);
    if (RegQueryValueExW(k, L"Camera", nullptr, &type, (BYTE*)cam, &sz) == ERROR_SUCCESS && type == REG_SZ && cam[0]) c.camera = cam;
    auto clamp = [](DWORD v, DWORD lo, DWORD hi) { return v < lo ? lo : v > hi ? hi : v; };
    c.search_ms = clamp(reg_dword(k, L"SearchMs", c.search_ms), 3000, 15000);
    c.challenge_ms = clamp(reg_dword(k, L"ChallengeMs", c.challenge_ms), 2000, 6000);
    c.max_fails = clamp(reg_dword(k, L"MaxFails", c.max_fails), 1, 5);
    c.strictness = clamp(reg_dword(k, L"Strictness", 0), 0, 2);
    c.sounds = reg_dword(k, L"Sounds", 1) != 0;
    RegCloseKey(k);
    return c;
}

std::wstring module_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), p, MAX_PATH);
    PathRemoveFileSpecW(p);
    return p;
}

std::wstring data_dir() {
    wchar_t p[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", p, MAX_PATH);
    return (n && n < MAX_PATH ? std::wstring(p) : std::wstring(L"C:\\ProgramData")) + L"\\WinFace";
}

void log_event(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(msg, 512, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[640];
    StringCchPrintfW(line, 640, L"%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                     t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), msg);
    std::wstring path = data_dir() + L"\\log.txt";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1400];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof utf8, nullptr, nullptr);
    DWORD w;
    if (n > 1) WriteFile(h, utf8, n - 1, &w, nullptr);
    CloseHandle(h);
}

}  // namespace fgcp
