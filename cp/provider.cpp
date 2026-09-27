// WinFace CP - ICredentialProvider: decides where the tile appears and triggers auto-logon.
#include "credential.h"
#include "helpers.h"

#include <propkey.h>

namespace fgcp {

extern const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR kFields[FI_NUM_FIELDS] = {
    {FI_TILEIMAGE, CPFT_TILE_IMAGE, const_cast<PWSTR>(L"Image"), CPFG_CREDENTIAL_PROVIDER_LOGO},
    {FI_LABEL, CPFT_LARGE_TEXT, const_cast<PWSTR>(L"Face unlock"), CPFG_CREDENTIAL_PROVIDER_LABEL},
    {FI_STATUS, CPFT_SMALL_TEXT, const_cast<PWSTR>(L"Status")},
    {FI_RETRY, CPFT_COMMAND_LINK, const_cast<PWSTR>(L"Try again")},
};

class FaceProvider : public ICredentialProvider, public ICredentialProviderSetUserArray {
public:
    FaceProvider() { dll_addref(); }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ICredentialProvider)) *ppv = static_cast<ICredentialProvider*>(this);
        else if (riid == __uuidof(ICredentialProviderSetUserArray)) *ppv = static_cast<ICredentialProviderSetUserArray*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD) override {
        cfg_ = Config::load();
        // Lock-screen mode covers both the lock screen and the first sign-in (Windows 11 often uses CPUS_LOGON
        // for a plain lock too). Face is always just one option: PIN/password tiles are never hidden.
        bool screen = cpus == CPUS_UNLOCK_WORKSTATION || cpus == CPUS_LOGON;
        bool allowed = cfg_.enabled && !cfg_.user_sid.empty() &&
                       ((cpus == CPUS_CREDUI && (cfg_.scenarios & 1)) || (screen && (cfg_.scenarios & 2)));
        log_event(L"SetUsageScenario %d -> %s", (int)cpus, allowed ? L"active" : L"inactive");
        if (!allowed) return E_NOTIMPL;
        cpus_ = cpus;
        return S_OK;
    }
    IFACEMETHODIMP SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) override { return E_NOTIMPL; }

    IFACEMETHODIMP SetUserArray(ICredentialProviderUserArray* users) override {
        DWORD n = 0;
        users->GetCount(&n);
        for (DWORD i = 0; i < n && !cred_; ++i) {
            ICredentialProviderUser* u = nullptr;
            if (FAILED(users->GetAt(i, &u))) continue;
            PWSTR sid = nullptr, qn = nullptr;
            GUID prov{};
            if (SUCCEEDED(u->GetSid(&sid)) && cfg_.user_sid == sid && SUCCEEDED(u->GetStringValue(PKEY_Identity_QualifiedUserName, &qn))) {
                u->GetProviderID(&prov);
                bool local = prov == Identity_LocalUserProvider;
                FaceProvider* self = this;
                cred_ = new FaceCredential(cpus_, cfg_, sid, qn, local, [self] { self->unlocked(); });
                log_event(L"tile for %s (%s account)", qn, local ? L"local" : L"Microsoft/online");
            }
            CoTaskMemFree(sid);
            CoTaskMemFree(qn);
            u->Release();
        }
        if (!cred_) log_event(L"enrolled user not in the user array (%lu users)", n);
        return S_OK;
    }

    IFACEMETHODIMP Advise(ICredentialProviderEvents* ev, UINT_PTR ctx) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (events_) events_->Release();
        events_ = ev;
        if (events_) events_->AddRef();
        ctx_ = ctx;
        return S_OK;
    }
    IFACEMETHODIMP UnAdvise() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (events_) { events_->Release(); events_ = nullptr; }
        return S_OK;
    }

    IFACEMETHODIMP GetFieldDescriptorCount(DWORD* n) override { *n = FI_NUM_FIELDS; return S_OK; }
    IFACEMETHODIMP GetFieldDescriptorAt(DWORD i, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** d) override {
        if (i >= FI_NUM_FIELDS || !d) return E_INVALIDARG;
        return field_descriptor_copy(kFields[i], d);
    }

    IFACEMETHODIMP GetCredentialCount(DWORD* count, DWORD* def, BOOL* auto_logon) override {
        *count = cred_ ? 1 : 0;
        // Face is the default tile (it starts scanning straight away, like Windows Hello face);
        // the PIN stays one click away under "Sign-in options".
        *def = cred_ ? 0 : CREDENTIAL_PROVIDER_NO_DEFAULT;
        *auto_logon = FALSE;
        if (cred_ && pending_logon_.exchange(false)) {
            *def = 0;
            *auto_logon = TRUE;   // LogonUI will call GetSerialization right away
        }
        return S_OK;
    }
    IFACEMETHODIMP GetCredentialAt(DWORD i, ICredentialProviderCredential** c) override {
        if (i != 0 || !cred_ || !c) return E_INVALIDARG;
        return cred_->QueryInterface(__uuidof(ICredentialProviderCredential), (void**)c);
    }

private:
    ~FaceProvider() {
        if (cred_) cred_->Release();
        if (events_) events_->Release();
        dll_release();
    }
    void unlocked() {   // scanner thread: face verified -> ask LogonUI to re-enumerate and auto-logon
        pending_logon_ = true;
        std::lock_guard<std::mutex> lk(mu_);
        if (events_) events_->CredentialsChanged(ctx_);
    }

    LONG refs_ = 1;
    Config cfg_;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_ = CPUS_INVALID;
    FaceCredential* cred_ = nullptr;
    ICredentialProviderEvents* events_ = nullptr;
    UINT_PTR ctx_ = 0;
    std::mutex mu_;
    std::atomic<bool> pending_logon_{false};
};

HRESULT create_provider(REFIID riid, void** ppv) {
    auto* p = new (std::nothrow) FaceProvider();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

}  // namespace fgcp
