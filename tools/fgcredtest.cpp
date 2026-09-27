// Opens the standard "Windows Security" credential prompt. With `fgsetup mode test`, the WinFace tile appears
// here (and ONLY here), so the whole face -> password -> Windows logon path can be tested without touching
// the lock screen. The password is verified with LogonUser and never printed.
#include <windows.h>
#include <wincred.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "credui.lib")

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CREDUI_INFOW ui{sizeof ui};
    ui.pszCaptionText = L"WinFace test";
    ui.pszMessageText = L"Choose 'Face unlock' and look at the camera.";
    ULONG pkg = 0;
    void* out = nullptr;
    ULONG out_len = 0;
    BOOL save = FALSE;
    DWORD t0 = GetTickCount();
    DWORD r = CredUIPromptForWindowsCredentialsW(&ui, 0, &pkg, nullptr, 0, &out, &out_len, &save, CREDUIWIN_ENUMERATE_CURRENT_USER);
    if (r != ERROR_SUCCESS) { printf("prompt closed (%lu)\n", r); return 1; }
    printf("credential returned after %.1f s (package %lu, %lu bytes)\n", (GetTickCount() - t0) / 1000.0, pkg, out_len);

    // ask for the needed sizes first (Microsoft-account buffers are large)
    DWORD nu = 0, nd = 0, np = 0;
    CredUnPackAuthenticationBufferW(CRED_PACK_PROTECTED_CREDENTIALS, out, out_len, nullptr, &nu, nullptr, &nd, nullptr, &np);
    std::wstring user(nu + 1, L'\0'), domain(nd + 1, L'\0'), pw(np + 1, L'\0');
    bool unpacked = CredUnPackAuthenticationBufferW(CRED_PACK_PROTECTED_CREDENTIALS, out, out_len, user.data(), &nu,
                                                    domain.data(), &nd, pw.data(), &np);
    SecureZeroMemory(out, out_len);
    CoTaskMemFree(out);
    if (!unpacked) {
        DWORD e = GetLastError();
        printf("could not unpack (%lu) - probably a PIN / Windows Hello credential, which this tool cannot verify\n", e);
        return 2;
    }
    user.resize(wcslen(user.c_str()));
    domain.resize(wcslen(domain.c_str()));
    // unprotect if needed, then verify against Windows
    DWORD npl = 0;
    CredUnprotectW(FALSE, pw.data(), np, nullptr, &npl);
    std::wstring plain(npl + 1, L'\0');
    const wchar_t* pass = pw.c_str();
    if (npl && CredUnprotectW(FALSE, pw.data(), np, plain.data(), &npl)) pass = plain.c_str();
    std::wstring u = user, d = domain;
    // Microsoft-account credentials carry a *marshaled* user name ("@@..."); decode it to the real name
    if (u.rfind(L"@@", 0) == 0) {
        CRED_MARSHAL_TYPE type;
        PVOID info = nullptr;
        if (CredUnmarshalCredentialW(u.c_str(), &type, &info)) {
            // both types start with the user-name pointer (type 4 = UsernameForPackedCredentials)
            if (type == UsernameTargetCredential || (int)type == 4) u = ((USERNAME_TARGET_CREDENTIAL_INFO*)info)->UserName;
            CredFree(info);
        }
    }
    printf("unpacked: user '%ls' domain '%ls'\n", u.c_str(), d.c_str());
    if (d.empty() && u.find(L'\\') != std::wstring::npos) { d = u.substr(0, u.find(L'\\')); u = u.substr(u.find(L'\\') + 1); }
    printf("account: %ls%s%ls\n", d.c_str(), d.empty() ? "" : "\\", u.c_str());
    // try the forms Windows accepts for a Microsoft account: MicrosoftAccount\email, then the plain email
    HANDLE tok = nullptr;
    BOOL ok = FALSE;
    DWORD err = 0;
    std::wstring email = u.find(L'\\') != std::wstring::npos ? u.substr(u.find(L'\\') + 1) : u;
    const std::pair<std::wstring, std::wstring> tries[] = {
        {u, d}, {email, L"MicrosoftAccount"}, {L"MicrosoftAccount\\" + email, L""}, {email, L""}};
    for (auto& [tu, td] : tries) {
        ok = LogonUserW(tu.c_str(), td.empty() ? nullptr : td.c_str(), pass, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &tok);
        err = GetLastError();
        printf("  check as %ls%s%ls: %s\n", td.c_str(), td.empty() ? "" : "\\", tu.c_str(), ok ? "OK" : "no");
        if (ok) break;
    }
    SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));
    SecureZeroMemory(plain.data(), plain.size() * sizeof(wchar_t));
    if (ok) { CloseHandle(tok); printf("RESULT: Windows accepted the credential - face unlock path works.\n"); return 0; }
    printf("RESULT: Windows rejected the credential (error %lu)%s\n", err,
           err == ERROR_LOGON_FAILURE ? " - stored password wrong? re-run: fgsetup password" : "");
    return 3;
}
