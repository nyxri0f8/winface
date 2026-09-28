// WinFace CP - the "Face unlock" tile (one per enrolled Windows account).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "common.h"
#include "overlay.h"
#include "scanner.h"

namespace fgcp {

class FaceCredential : public ICredentialProviderCredential2 {
public:
    using Unlocked = std::function<void()>;   // tells the provider to trigger auto-logon
    FaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, const Config& cfg, const std::wstring& sid,
                   const std::wstring& qualified_user, bool local_user, Unlocked on_unlocked);

    // IUnknown
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override;
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    // ICredentialProviderCredential
    IFACEMETHODIMP Advise(ICredentialProviderCredentialEvents* ev) override;
    IFACEMETHODIMP UnAdvise() override;
    IFACEMETHODIMP SetSelected(BOOL* auto_logon) override;
    IFACEMETHODIMP SetDeselected() override;
    IFACEMETHODIMP GetFieldState(DWORD id, CREDENTIAL_PROVIDER_FIELD_STATE* s, CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* is) override;
    IFACEMETHODIMP GetStringValue(DWORD id, PWSTR* v) override;
    IFACEMETHODIMP GetBitmapValue(DWORD id, HBITMAP* bmp) override;
    IFACEMETHODIMP GetCheckboxValue(DWORD, BOOL*, PWSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetSubmitButtonValue(DWORD, DWORD*) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetComboBoxValueCount(DWORD, DWORD*, DWORD*) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetComboBoxValueAt(DWORD, DWORD, PWSTR*) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetStringValue(DWORD, PCWSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetCheckboxValue(DWORD, BOOL) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetComboBoxSelectedValue(DWORD, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP CommandLinkClicked(DWORD id) override;
    IFACEMETHODIMP GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* r,
                                    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* cs, PWSTR* status,
                                    CREDENTIAL_PROVIDER_STATUS_ICON* icon) override;
    IFACEMETHODIMP ReportResult(NTSTATUS st, NTSTATUS sub, PWSTR* status, CREDENTIAL_PROVIDER_STATUS_ICON* icon) override;
    // ICredentialProviderCredential2
    IFACEMETHODIMP GetUserSid(PWSTR* sid) override;

    bool take_verified();          // one-shot, short-lived proof that the face check passed
    bool locked() const;           // PIN required right now (then face is not the default tile)
    void restart_scan(const wchar_t* why);

private:
    ~FaceCredential();
    void on_scan_done(bool unlocked, const std::string& reason);
    void set_status(const wchar_t* s);
    std::wstring lockout() const;

    LONG refs_ = 1;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_;
    Config cfg_;
    std::wstring sid_, user_;
    bool local_;
    bool persist_ = false;
    std::atomic<bool> extras_active_{false};   // the running scan includes the extra checks         // lock / sign-in screen as SYSTEM: lockout state is read and written
    Unlocked on_unlocked_;
    ICredentialProviderCredentialEvents* events_ = nullptr;
    std::unique_ptr<Scanner> scanner_;
    std::unique_ptr<Overlay> overlay_;
    std::mutex mu_;
    std::wstring status_ = L"Look at the camera";
    std::atomic<ULONGLONG> verified_at_{0};   // GetTickCount64 of a successful face check, 0 = none
    std::atomic<ULONGLONG> served_at_{0};     // credential handed to Windows: no rescans while it signs in
    std::atomic<ULONGLONG> unlocked_at_{0};   // face verified: camera stays off until sign-in fails or 15 s pass
    std::atomic<int> fails_{0};
};

}  // namespace fgcp
