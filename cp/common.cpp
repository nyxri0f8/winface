#include "common.h"

#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

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
    c.action = reg_dword(k, L"Action", 1) != 0;
    c.flash = clamp(reg_dword(k, L"FlashCheck", 1), 0, 2);
    c.pin_after_restart = reg_dword(k, L"PinAfterRestart", 1) != 0;
    c.pin_after_hours = clamp(reg_dword(k, L"PinAfterHours", 48), 0, 720);
    c.intruder_photos = reg_dword(k, L"IntruderPhotos", 0) == 1;
    c.extra_checks = clamp(reg_dword(k, L"ExtraChecks", 1), 0, 2);
    c.events_task = reg_dword(k, L"EventsTask", 0) == 1;
    wchar_t inst[260] = {};
    sz = sizeof inst - sizeof(wchar_t);
    if (RegQueryValueExW(k, L"CameraInstance", nullptr, &type, (BYTE*)inst, &sz) == ERROR_SUCCESS && type == REG_SZ) c.camera_instance = inst;
    RegCloseKey(k);
    return c;
}

static const wchar_t* kStateKey = L"SOFTWARE\\WinFace\\State";

LockState LockState::load() {
    LockState s;
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kStateKey, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return s;
    auto q = [&](const wchar_t* n) {
        ULONGLONG v = 0;
        DWORD sz = sizeof v, type = 0;
        return RegQueryValueExW(k, n, nullptr, &type, (BYTE*)&v, &sz) == ERROR_SUCCESS && type == REG_QWORD ? v : 0ULL;
    };
    s.fails = reg_dword(k, L"Fails", 0);
    s.last_strong_auth = q(L"LastStrongAuth");
    s.last_unlock = q(L"LastUnlock");
    s.last_face_serve = q(L"LastFaceServe");
    s.extra_day = reg_dword(k, L"ExtraDay", 0);
    s.extra_plan = reg_dword(k, L"ExtraPlan", 0);
    s.extra_done = reg_dword(k, L"ExtraDone", 0);
    wchar_t cam[200] = {};
    DWORD sz = sizeof cam - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(k, L"CameraChanged", nullptr, &type, (BYTE*)cam, &sz) == ERROR_SUCCESS && type == REG_SZ) s.camera_changed = cam;
    RegCloseKey(k);
    return s;
}

static void set_state_value(const wchar_t* name, DWORD type, const void* data, DWORD size) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStateKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr)) return;
    if (type == REG_SZ && size <= sizeof(wchar_t)) RegDeleteValueW(k, name);   // empty string = remove
    else RegSetValueExW(k, name, 0, type, (const BYTE*)data, size);
    RegCloseKey(k);
}
void LockState::set_qword(const wchar_t* n, ULONGLONG v) { set_state_value(n, REG_QWORD, &v, sizeof v); }
void LockState::set_dword(const wchar_t* n, DWORD v) { set_state_value(n, REG_DWORD, &v, sizeof v); }
void LockState::set_string(const wchar_t* n, const std::wstring& v) {
    set_state_value(n, REG_SZ, v.c_str(), DWORD((v.size() + 1) * sizeof(wchar_t)));
}

ULONGLONG filetime_now() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

ULONGLONG boot_filetime() { return filetime_now() - GetTickCount64() * 10000ULL; }

unsigned secure_random() {
    unsigned v = 0;
    BCryptGenRandom(nullptr, (PUCHAR)&v, sizeof v, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v;
}

DWORD local_yyyymmdd(int* hour) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    if (hour) *hour = t.wHour;
    return t.wYear * 10000u + t.wMonth * 100u + t.wDay;
}

bool extra_checks_due(const Config& c, LockState& s, int fails, unsigned rnd, DWORD today, int hour) {
    if (c.extra_checks == 0 || (!c.action && c.flash == 0)) return false;
    if (c.extra_checks == 2 || fails >= 2) return true;   // every unlock, or the attempt after 2 failures
    if (s.extra_day != today) {                            // a new day: plan 2 or 3 random extra checks
        s.extra_day = today;
        s.extra_plan = 2 + (rnd >> 16) % 2;
        s.extra_done = 0;
    }
    if (s.extra_done >= s.extra_plan) return false;
    // spread them over the day: ~1 in 4 unlocks, and catch up in the evening so the day's checks still happen
    return (rnd & 0xFFFF) % 4 == 0 || hour >= 19;
}

std::wstring lockout_reason(const Config& c, const LockState& s, ULONGLONG now, ULONGLONG boot) {
    const ULONGLONG hour = 36000000000ULL;
    if (s.fails >= c.max_fails) return L"Face unlock is paused after too many failed attempts - sign in with your PIN";
    if (!s.camera_changed.empty()) return L"The camera changed - sign in with your PIN, then confirm it in the WinFace app";
    if (!c.events_task) return L"";   // without the sign-in events task the PIN rules below cannot be tracked
    if (c.pin_after_restart && s.last_strong_auth < boot) return L"Your PIN is required after a restart";
    if (c.pin_after_hours && (s.last_unlock == 0 || now - s.last_unlock > c.pin_after_hours * hour))
        return L"Your PIN is required because this PC was not unlocked for a while";
    return L"";
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
