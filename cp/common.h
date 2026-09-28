// WinFace credential provider - shared definitions.
#pragma once
#include <windows.h>
#include <credentialprovider.h>
#include <shlguid.h>   // CPFG_CREDENTIAL_PROVIDER_LOGO / _LABEL
#include <ntsecapi.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <string>

// the few NTSTATUS codes we need (ntstatus.h conflicts with windows.h)
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_LOGON_FAILURE
#define STATUS_LOGON_FAILURE ((NTSTATUS)0xC000006DL)
#endif
#ifndef STATUS_WRONG_PASSWORD
#define STATUS_WRONG_PASSWORD ((NTSTATUS)0xC000006AL)
#endif

// {C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}
DEFINE_GUID(CLSID_WinFaceProvider, 0xc188dc15, 0xe41e, 0x4ccf, 0x9d, 0xa9, 0x82, 0x38, 0xe1, 0xd0, 0xbb, 0xdf);

namespace fgcp {

enum FieldId { FI_TILEIMAGE = 0, FI_LABEL, FI_STATUS, FI_RETRY, FI_NUM_FIELDS };

// Settings live in HKLM\SOFTWARE\WinFace (admin-writable only). Missing key = provider disabled.
struct Config {
    bool enabled = false;          // master kill switch (Enabled=1 required)
    DWORD scenarios = 1;           // bit 0 = CredUI test prompt, bit 1 = lock screen unlock
    std::wstring user_sid;         // the one account WinFace may unlock
    bool test_mode = false;        // CredUI test: allow the user-scope DPAPI password blob
    // user settings (WinFace app -> Settings); every value is clamped to a safe range on load
    std::wstring camera;           // device-path fragment of the allowed camera; empty = automatic (first colour camera)
    DWORD search_ms = 7000;        // how long to look for a matching face     (3000..15000)
    DWORD challenge_ms = 3000;     // time for the head turn                   (2000..6000)
    DWORD max_fails = 3;           // failed attempts before face stops        (1..5)
    DWORD strictness = 0;          // 0 balanced (0.42), 1 strict (0.48), 2 relaxed (0.38)
    bool sounds = true;
    // extra liveness + lockout (WinFace app -> Security)
    bool action = true;            // random blink / open-mouth after the head turn   ("Action")
    DWORD flash = 1;               // screen-flash check: 0 off, 1 measure only, 2 enforce  ("FlashCheck")
    bool pin_after_restart = true; // face unlock only after one PIN/password sign-in since boot ("PinAfterRestart")
    DWORD pin_after_hours = 48;    // ... and when the PC was not unlocked for this long, 0 = never ("PinAfterHours")
    bool intruder_photos = false;  // encrypted snapshot after a failed attempt ("IntruderPhotos", off by default)
    // when the extra checks (flash + blink/mouth) run: 0 never, 1 randomly 2-3 times a day and always after 2 failed
    // attempts (default), 2 every unlock                                               ("ExtraChecks")
    DWORD extra_checks = 1;
    std::wstring camera_instance;  // the exact camera device that was enrolled ("CameraInstance")
    bool events_task = false;      // the sign-in events task is installed ("EventsTask"); needed by the PIN rules
    float match_threshold() const { return strictness == 1 ? 0.48f : strictness == 2 ? 0.38f : 0.42f; }
    static Config load();
};

// Lockout state, HKLM\SOFTWARE\WinFace\State - written only by SYSTEM (lock screen, sign-in events task) and admins.
// Times are FILETIMEs (UTC, 100 ns).
struct LockState {
    DWORD fails = 0;                  // consecutive failed face attempts
    ULONGLONG last_strong_auth = 0;   // last sign-in / unlock with the PIN or password
    ULONGLONG last_unlock = 0;        // last sign-in / unlock by any method
    ULONGLONG last_face_serve = 0;    // last time face unlock handed Windows the password
    std::wstring camera_changed;      // name of a different camera seen where the enrolled one was expected
    DWORD extra_day = 0;              // local date (yyyymmdd) of the extra-check plan below
    DWORD extra_plan = 0;             // how many random extra checks today (2 or 3)
    DWORD extra_done = 0;             // ... and how many already happened
    static LockState load();
    static void set_qword(const wchar_t* name, ULONGLONG v);
    static void set_dword(const wchar_t* name, DWORD v);
    static void set_string(const wchar_t* name, const std::wstring& v);
};
ULONGLONG filetime_now();
ULONGLONG boot_filetime();
// Why face unlock must wait for the PIN right now ("" = it may run). Pure function, so it can be tested.
std::wstring lockout_reason(const Config& c, const LockState& s, ULONGLONG now, ULONGLONG boot);

// Should this scan include the extra checks? Randomly 2-3 times a day (unpredictable: an attacker cannot know when),
// and always on the attempt after 2 failures. `rnd` is a fresh CSPRNG value, `today` yyyymmdd, `hour` 0-23 local.
// Updates the day's plan in `s` (the caller persists it). Pure apart from `s`, so it can be tested.
bool extra_checks_due(const Config& c, LockState& s, int fails, unsigned rnd, DWORD today, int hour);
unsigned secure_random();
DWORD local_yyyymmdd(int* hour = nullptr);

std::wstring module_dir();         // folder containing WinFaceCP.dll
std::wstring data_dir();           // C:\ProgramData\WinFace
void log_event(const wchar_t* fmt, ...);  // C:\ProgramData\WinFace\log.txt (never passwords)

extern LONG g_dll_refs;
inline void dll_addref() { InterlockedIncrement(&g_dll_refs); }
inline void dll_release() { InterlockedDecrement(&g_dll_refs); }

}  // namespace fgcp
