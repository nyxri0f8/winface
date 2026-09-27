// WinFace - password vault.
//  TPM:   RSA-2048 key inside the TPM (Microsoft Platform Crypto Provider), machine scope, usable only
//         by SYSTEM. The setup tool encrypts with it; only the lock screen (LogonUI = SYSTEM) can decrypt.
//         The blob is useless on any other machine and to any non-SYSTEM process.
//  DPAPI: TEST MODE ONLY - user-scope blob so the CredUI test prompt (runs as the user) can work.
#pragma once
#include <string>

namespace fgcp {

struct SecurePassword {           // wiped on destruction
    std::wstring value;
    ~SecurePassword() { wipe(); }
    void wipe();
};

// setup side (elevated): create/replace the TPM key, encrypt, write %ProgramData%\WinFace\secret.tpm
bool tpm_store(const std::wstring& password, std::wstring& err);
// lock-screen side (SYSTEM)
bool tpm_load(SecurePassword& out, std::wstring& err);

// test mode (user context): %LOCALAPPDATA%\WinFace\secret.dpapi
bool dpapi_store(const std::wstring& password, std::wstring& err);
bool dpapi_load(SecurePassword& out, std::wstring& err);

// Erase: the password files (TPM blob + this user's DPAPI copy). Without the blob the password is unrecoverable.
void erase_password_files();
// Delete WinFace's TPM keys except the one secret.tpm still uses (keep_current). The keys are SYSTEM-only, so this
// only succeeds when running as SYSTEM; returns how many keys remain.
int tpm_delete_keys(bool keep_current, int* deleted = nullptr);

bool running_as_system();

}  // namespace fgcp
