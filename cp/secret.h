// FaceGate - password vault.
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

// setup side (elevated): create/replace the TPM key, encrypt, write %ProgramData%\FaceGate\secret.tpm
bool tpm_store(const std::wstring& password, std::wstring& err);
// lock-screen side (SYSTEM)
bool tpm_load(SecurePassword& out, std::wstring& err);

// test mode (user context): %LOCALAPPDATA%\FaceGate\secret.dpapi
bool dpapi_store(const std::wstring& password, std::wstring& err);
bool dpapi_load(SecurePassword& out, std::wstring& err);

bool running_as_system();

}  // namespace fgcp
