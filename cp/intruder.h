// WinFace - intruder photos (off by default). After a counted failed attempt the lock screen stores one small
// snapshot of whoever tried, encrypted: AES-256-GCM with a random key per photo, the key sealed by a TPM RSA key that
// only SYSTEM and Administrators can use. Files live in C:\ProgramData\WinFace\intruders (SYSTEM + Administrators
// only); at most kMaxPhotos are kept, none older than kMaxDays.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "../engine/image.h"

namespace fgcp {

constexpr int kMaxPhotos = 20, kMaxDays = 30;

struct IntruderPhoto {
    std::wstring id;          // file name without extension, e.g. 20260928-101530-123
    ULONGLONG when = 0;       // FILETIME (UTC)
};

std::wstring intruder_dir();
// lock screen (SYSTEM): encode + encrypt + store, then prune old photos
bool intruder_save(const fg::Image& bgr, const std::string& reason, std::wstring& err);
// setup side (admin)
bool intruder_key_ensure(std::wstring& err);   // create the TPM key + the locked-down folder
std::vector<IntruderPhoto> intruder_list();    // newest first
bool intruder_open(const std::wstring& id, std::vector<BYTE>& jpeg, std::string& reason, ULONGLONG& when, std::wstring& err);
int intruder_delete(const std::wstring& id);   // id or L"all"; returns how many were deleted
void intruder_erase_all();                     // photos, folder and the TPM key

// the encryption itself, with any key storage provider (tests use a software key; the product uses the TPM)
bool seal_blob(const std::vector<BYTE>& plain, std::vector<BYTE>& out, const wchar_t* provider, const wchar_t* key, DWORD flags,
               std::wstring& err);
bool open_blob(const std::vector<BYTE>& in, std::vector<BYTE>& plain, ULONGLONG& when, const wchar_t* provider, const wchar_t* key,
               DWORD flags, std::wstring& err);

// JPEG helper (WIC), also used by tests
bool encode_jpeg(const fg::Image& bgr, std::vector<BYTE>& out, int max_width = 640);

}  // namespace fgcp
