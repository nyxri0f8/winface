// FaceGate credential provider - shared definitions.
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
DEFINE_GUID(CLSID_FaceGateProvider, 0xc188dc15, 0xe41e, 0x4ccf, 0x9d, 0xa9, 0x82, 0x38, 0xe1, 0xd0, 0xbb, 0xdf);

namespace fgcp {

enum FieldId { FI_TILEIMAGE = 0, FI_LABEL, FI_STATUS, FI_RETRY, FI_NUM_FIELDS };

// Settings live in HKLM\SOFTWARE\FaceGate (admin-writable only). Missing key = provider disabled.
struct Config {
    bool enabled = false;          // master kill switch (Enabled=1 required)
    DWORD scenarios = 1;           // bit 0 = CredUI test prompt, bit 1 = lock screen unlock
    std::wstring user_sid;         // the one account FaceGate may unlock
    bool test_mode = false;        // CredUI test: allow the user-scope DPAPI password blob
    // user settings (WinFace app -> Settings); every value is clamped to a safe range on load
    std::wstring camera = L"usb#vid_0408&pid_5496&mi_00";  // device-path fragment of the allowed camera
    DWORD search_ms = 7000;        // how long to look for a matching face     (3000..15000)
    DWORD challenge_ms = 3000;     // time for the head turn                   (2000..6000)
    DWORD max_fails = 3;           // failed attempts before face stops        (1..5)
    DWORD strictness = 0;          // 0 balanced (0.42), 1 strict (0.48), 2 relaxed (0.38)
    bool sounds = true;
    float match_threshold() const { return strictness == 1 ? 0.48f : strictness == 2 ? 0.38f : 0.42f; }
    static Config load();
};

std::wstring module_dir();         // folder containing FaceGateCP.dll
std::wstring data_dir();           // C:\ProgramData\FaceGate
void log_event(const wchar_t* fmt, ...);  // C:\ProgramData\FaceGate\log.txt (never passwords)

extern LONG g_dll_refs;
inline void dll_addref() { InterlockedIncrement(&g_dll_refs); }
inline void dll_release() { InterlockedDecrement(&g_dll_refs); }

}  // namespace fgcp
