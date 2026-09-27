#include "helpers.h"

#define SECURITY_WIN32
#include <security.h>   // NEGOSSP_NAME_A
#include <wincred.h>

#include <vector>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "credui.lib")
#pragma comment(lib, "advapi32.lib")

namespace fgcp {

HRESULT field_descriptor_copy(const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& src, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** out) {
    auto* d = (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));
    if (!d) return E_OUTOFMEMORY;
    *d = src;
    d->pszLabel = nullptr;
    if (src.pszLabel && FAILED(SHStrDupW(src.pszLabel, &d->pszLabel))) { CoTaskMemFree(d); return E_OUTOFMEMORY; }
    *out = d;
    return S_OK;
}

HRESULT negotiate_auth_package(ULONG* pkg) {
    HANDLE lsa;
    NTSTATUS st = LsaConnectUntrusted(&lsa);
    if (st != STATUS_SUCCESS) return HRESULT_FROM_NT(st);
    LSA_STRING name{(USHORT)strlen(NEGOSSP_NAME_A), (USHORT)(strlen(NEGOSSP_NAME_A) + 1), (PCHAR)NEGOSSP_NAME_A};
    st = LsaLookupAuthenticationPackage(lsa, &name, pkg);
    LsaDeregisterLogonProcess(lsa);
    return st == STATUS_SUCCESS ? S_OK : HRESULT_FROM_NT(st);
}

namespace {

// KERB_INTERACTIVE_UNLOCK_LOGON with the strings packed right after the struct (offsets as pointers)
HRESULT pack_kiul(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, const std::wstring& domain, const std::wstring& user,
                  const std::wstring& pw, BYTE** out, DWORD* cb) {
    auto bytes = [](const std::wstring& s) { return DWORD(s.size() * sizeof(wchar_t)); };
    DWORD total = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) + bytes(domain) + bytes(user) + bytes(pw);
    BYTE* buf = (BYTE*)CoTaskMemAlloc(total);
    if (!buf) return E_OUTOFMEMORY;
    ZeroMemory(buf, total);
    auto* k = (KERB_INTERACTIVE_UNLOCK_LOGON*)buf;
    KERB_INTERACTIVE_LOGON& kil = k->Logon;
    kil.MessageType = cpus == CPUS_UNLOCK_WORKSTATION ? KerbWorkstationUnlockLogon
                    : cpus == CPUS_CREDUI ? (KERB_LOGON_SUBMIT_TYPE)0 : KerbInteractiveLogon;
    BYTE* p = buf + sizeof(KERB_INTERACTIVE_UNLOCK_LOGON);
    auto put = [&](UNICODE_STRING& us, const std::wstring& s) {
        us.Length = us.MaximumLength = (USHORT)bytes(s);
        memcpy(p, s.data(), us.Length);
        us.Buffer = (PWSTR)(p - buf);  // offset, per the serialization contract
        p += us.Length;
    };
    put(kil.LogonDomainName, domain);
    put(kil.UserName, user);
    put(kil.Password, pw);
    *out = buf;
    *cb = total;
    return S_OK;
}

}  // namespace

HRESULT build_serialization(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, bool local_user, const std::wstring& qualified,
                            const std::wstring& password, CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* out) {
    ULONG pkg = 0;
    HRESULT hr = negotiate_auth_package(&pkg);
    if (FAILED(hr)) return hr;
    ZeroMemory(out, sizeof *out);
    if (local_user) {
        size_t slash = qualified.find(L'\\');
        std::wstring domain = slash == std::wstring::npos ? L"." : qualified.substr(0, slash);
        std::wstring user = slash == std::wstring::npos ? qualified : qualified.substr(slash + 1);
        // protect the password for LogonUI (not for CredUI), as the sample does
        std::wstring pw = password;
        if (cpus != CPUS_CREDUI) {
            DWORD n = 0;
            CRED_PROTECTION_TYPE pt;
            if (CredIsProtectedW((LPWSTR)password.c_str(), &pt) && pt == CredUnprotected) {
                CredProtectW(FALSE, (LPWSTR)password.c_str(), DWORD(password.size() + 1), nullptr, &n, nullptr);
                std::vector<wchar_t> prot(n);
                if (CredProtectW(FALSE, (LPWSTR)password.c_str(), DWORD(password.size() + 1), prot.data(), &n, nullptr))
                    pw.assign(prot.data());
                SecureZeroMemory(prot.data(), prot.size() * sizeof(wchar_t));
            }
        }
        hr = pack_kiul(cpus, domain, user, pw, &out->rgbSerialization, &out->cbSerialization);
        SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));
    } else {
        DWORD flags = CRED_PACK_PROTECTED_CREDENTIALS | CRED_PACK_ID_PROVIDER_CREDENTIALS;
        DWORD cb = 0;
        CredPackAuthenticationBufferW(flags, (LPWSTR)qualified.c_str(), (LPWSTR)password.c_str(), nullptr, &cb);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return HRESULT_FROM_WIN32(GetLastError());
        out->rgbSerialization = (BYTE*)CoTaskMemAlloc(cb);
        if (!out->rgbSerialization) return E_OUTOFMEMORY;
        if (!CredPackAuthenticationBufferW(flags, (LPWSTR)qualified.c_str(), (LPWSTR)password.c_str(), out->rgbSerialization, &cb)) {
            CoTaskMemFree(out->rgbSerialization);
            out->rgbSerialization = nullptr;
            return HRESULT_FROM_WIN32(GetLastError());
        }
        out->cbSerialization = cb;
        hr = S_OK;
    }
    if (SUCCEEDED(hr)) {
        out->ulAuthenticationPackage = pkg;
        out->clsidCredentialProvider = CLSID_FaceGateProvider;
    }
    return hr;
}

}  // namespace fgcp
