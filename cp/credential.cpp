#include "credential.h"

#include <algorithm>


#include "helpers.h"
#include "secret.h"

namespace fgcp {

extern const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR kFields[FI_NUM_FIELDS];

FaceCredential::FaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, const Config& cfg, const std::wstring& sid,
                               const std::wstring& user, bool local, Unlocked on_unlocked)
    : cpus_(cpus), cfg_(cfg), sid_(sid), user_(user), local_(local), on_unlocked_(std::move(on_unlocked)) {
    persist_ = (cpus == CPUS_UNLOCK_WORKSTATION || cpus == CPUS_LOGON) && running_as_system();
    dll_addref();
}

FaceCredential::~FaceCredential() {
    UnAdvise();
    dll_release();
}

IFACEMETHODIMP_(ULONG) FaceCredential::Release() {
    LONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return r;
}

IFACEMETHODIMP FaceCredential::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ICredentialProviderCredential) || riid == __uuidof(ICredentialProviderCredential2)) {
        *ppv = static_cast<ICredentialProviderCredential2*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

void FaceCredential::set_status(const wchar_t* s) {
    std::lock_guard<std::mutex> lk(mu_);
    status_ = s;
}

IFACEMETHODIMP FaceCredential::Advise(ICredentialProviderCredentialEvents* ev) {
    if (events_) events_->Release();
    events_ = ev;
    if (events_) events_->AddRef();
    HWND parent = nullptr;
    if (events_) events_->OnCreatingWindow(&parent);
    if (!scanner_) scanner_ = std::make_unique<Scanner>([this](bool ok, const std::string& why) { on_scan_done(ok, why); }, cfg_);
    if (!overlay_) {
        overlay_ = std::make_unique<Overlay>();
        // Lock / sign-in screen: the camera starts straight away, but the overlay waits behind the Windows curtain
        // until a key press or click lifts it. CredUI has no curtain, so it shows at once.
        bool behind_curtain = cpus_ == CPUS_UNLOCK_WORKSTATION || cpus_ == CPUS_LOGON;
        overlay_->start(parent, [this] { return scanner_->snapshot(); }, [this](bool display_on) {
            // lid opened / screen woke: scan again; screen off: camera off
            if (display_on) restart_scan(L"display on"); else if (scanner_) scanner_->stop();
        }, behind_curtain, [this] {
            // curtain lifted: the waiting scan may now ask for the head turn; if it already ended, start a fresh one
            if (!scanner_) return;
            scanner_->set_hold(false);
            if (!scanner_->snapshot().running && verified_at_ == 0) restart_scan(L"curtain lifted");
        });
    }
    log_event(L"credential advised (scenario %d)", (int)cpus_);
    restart_scan(L"advise");   // start immediately: this is what makes "open lid -> unlocked" fast
    return S_OK;
}

IFACEMETHODIMP FaceCredential::UnAdvise() {
    // overlay first: its thread reads scanner snapshots; the scanner's callback only touches overlay atomics
    if (overlay_) overlay_->stop();
    if (scanner_) scanner_->stop();
    scanner_.reset();
    overlay_.reset();
    if (events_) { events_->Release(); events_ = nullptr; }
    return S_OK;
}

// Only the real lock / sign-in screen (SYSTEM) keeps lockout state; the CredUI test prompt runs as the user.
std::wstring FaceCredential::lockout() const {
    if (!persist_) return L"";
    return lockout_reason(cfg_, LockState::load(), filetime_now(), boot_filetime());
}

bool FaceCredential::locked() const { return fails_ >= (int)cfg_.max_fails || !lockout().empty(); }

void FaceCredential::restart_scan(const wchar_t* why) {
    if (!scanner_) return;
    // After a successful face check Windows is signing in: keep the camera OFF, otherwise it is still held
    // while LogonUI exits and the next lock screen waits seconds for it.
    ULONGLONG u = unlocked_at_;
    if (u != 0 && GetTickCount64() - u < 15000) { log_event(L"scan request '%s' ignored: signing in", why); return; }
    log_event(L"scan request: %s", why);
    if (fails_ >= (int)cfg_.max_fails) { set_status(L"Face not recognised - use your PIN (Sign-in options)"); return; }
    std::wstring locked = lockout();
    if (!locked.empty()) {   // PIN rules (after restart, unused for a while, too many failures, camera changed)
        log_event(L"face unlock paused: %s", locked.c_str());
        if (overlay_) overlay_->show(false);
        set_status(locked.c_str());
        return;
    }
    if (overlay_) { overlay_->reset(); }
    set_status(L"Look at the camera");
    // behind the curtain: recognise now, ask for the head turn only once the user can see the prompt
    scanner_->set_hold(overlay_ && !overlay_->revealed());
    // extra checks (flash + blink/mouth): not every time - randomly 2-3 times a day and after 2 failed attempts
    bool extras;
    if (persist_) {
        LockState s = LockState::load();
        DWORD day = s.extra_day, plan = s.extra_plan;
        int hour = 0;
        DWORD today = local_yyyymmdd(&hour);
        extras = extra_checks_due(cfg_, s, std::max(fails_.load(), (int)s.fails), secure_random(), today, hour);
        if (s.extra_day != day || s.extra_plan != plan) {
            LockState::set_dword(L"ExtraDay", s.extra_day);
            LockState::set_dword(L"ExtraPlan", s.extra_plan);
            LockState::set_dword(L"ExtraDone", s.extra_done);
        }
    } else {   // the CredUI test prompt keeps no state
        extras = cfg_.extra_checks == 2 || (cfg_.extra_checks == 1 && fails_ >= 2);
    }
    extras_active_ = extras;
    scanner_->set_extras(extras);
    if (extras) log_event(L"this scan includes the extra checks (flash + blink/mouth)");
    scanner_->start();
}

void FaceCredential::on_scan_done(bool unlocked, const std::string& reason) {
    // worker thread
    // one of today's random extra checks was used (only when it really ran: an unlock or a counted failure)
    auto count_extra = [&] { if (extras_active_ && persist_) LockState::set_dword(L"ExtraDone", LockState::load().extra_done + 1); };
    if (unlocked) {
        unlocked_at_ = GetTickCount64();   // before anything else can ask for a new scan
        count_extra();
        if (persist_) LockState::set_dword(L"Fails", 0);
        verified_at_ = GetTickCount64();
        if (overlay_) overlay_->result(true, cfg_.sounds);
        set_status(L"Unlocking...");
        if (on_unlocked_) on_unlocked_();
        return;
    }
    if (reason == "camera changed") {
        if (overlay_) overlay_->show(false);
        set_status(L"The camera changed - sign in with your PIN, then confirm it in the WinFace app");
        return;
    }
    if (reason.rfind("idle", 0) == 0 || reason.rfind("camera", 0) == 0 || reason.rfind("models", 0) == 0 ||
        reason == "no enrolled faces") {
        if (overlay_) overlay_->show(false);
        set_status(L"Click to use face unlock");
        return;
    }
    int n = ++fails_;
    count_extra();
    if (persist_) {   // counted across lock screens: after MaxFails only the PIN / password clears it
        DWORD total = LockState::load().fails + 1;
        LockState::set_dword(L"Fails", total);
        n = std::max(n, (int)total);
        if (cfg_.intruder_photos && scanner_) scanner_->save_intruder_photo(reason);
    }
    if (overlay_) overlay_->result(false, cfg_.sounds);
    set_status(n >= (int)cfg_.max_fails ? L"Face unlock paused after too many failed attempts - use your PIN" : L"Not recognised - click to try again");
}

bool FaceCredential::take_verified() {
    ULONGLONG t = verified_at_.exchange(0);
    return t != 0 && GetTickCount64() - t < 10000;   // proof expires after 10 s and is single-use
}

IFACEMETHODIMP FaceCredential::SetSelected(BOOL* auto_logon) {
    *auto_logon = verified_at_ != 0 ? TRUE : FALSE;
    if (!*auto_logon && scanner_ && !scanner_->snapshot().running) restart_scan(L"tile selected");
    return S_OK;
}

IFACEMETHODIMP FaceCredential::SetDeselected() { return S_OK; }

IFACEMETHODIMP FaceCredential::GetFieldState(DWORD id, CREDENTIAL_PROVIDER_FIELD_STATE* s, CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* is) {
    if (id >= FI_NUM_FIELDS) return E_INVALIDARG;
    *is = CPFIS_NONE;
    switch (id) {
        case FI_TILEIMAGE: case FI_LABEL: *s = CPFS_DISPLAY_IN_BOTH; break;
        default: *s = CPFS_DISPLAY_IN_SELECTED_TILE; break;
    }
    return S_OK;
}

IFACEMETHODIMP FaceCredential::GetStringValue(DWORD id, PWSTR* v) {
    std::lock_guard<std::mutex> lk(mu_);
    switch (id) {
        case FI_LABEL: return SHStrDupW(L"Face unlock", v);
        case FI_STATUS: return SHStrDupW(status_.c_str(), v);
        case FI_RETRY: return SHStrDupW(L"Try face unlock again", v);
        default: return E_INVALIDARG;
    }
}

IFACEMETHODIMP FaceCredential::GetBitmapValue(DWORD id, HBITMAP* bmp) {
    if (id != FI_TILEIMAGE) return E_INVALIDARG;
    std::wstring p = module_dir() + L"\\assets\\tile.bmp";
    *bmp = (HBITMAP)LoadImageW(nullptr, p.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    return *bmp ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

IFACEMETHODIMP FaceCredential::CommandLinkClicked(DWORD id) {
    if (id != FI_RETRY) return E_INVALIDARG;
    restart_scan(L"retry link");
    if (events_) {
        std::lock_guard<std::mutex> lk(mu_);
        events_->SetFieldString(this, FI_STATUS, status_.c_str());
    }
    return S_OK;
}

IFACEMETHODIMP FaceCredential::GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* r,
                                                CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* cs, PWSTR* status,
                                                CREDENTIAL_PROVIDER_STATUS_ICON* icon) {
    *r = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    *status = nullptr;
    *icon = CPSI_NONE;
    ZeroMemory(cs, sizeof *cs);
    if (!take_verified()) {
        restart_scan(L"serialization without face");
        SHStrDupW(L"Look at the camera to unlock", status);
        *icon = CPSI_WARNING;
        return S_OK;
    }
    SecurePassword pw;
    std::wstring err;
    bool ok = running_as_system() ? tpm_load(pw, err) : (cfg_.test_mode ? dpapi_load(pw, err) : (err = L"not SYSTEM and test mode off", false));
    if (!ok) {
        log_event(L"password unavailable: %s", err.c_str());
        SHStrDupW(L"Face unlock needs setup - use your PIN or password", status);
        *icon = CPSI_ERROR;
        return S_OK;
    }
    HRESULT hr = build_serialization(cpus_, local_, user_, pw.value, cs);
    pw.wipe();
    if (FAILED(hr)) {
        log_event(L"serialization failed 0x%08X", (unsigned)hr);
        return hr;
    }
    log_event(L"credential serialized for %s", user_.c_str());
    served_at_ = GetTickCount64();
    if (persist_) LockState::set_qword(L"LastFaceServe", filetime_now());   // lets the sign-in events task tell face from PIN
    *r = CPGSR_RETURN_CREDENTIAL_FINISHED;
    return S_OK;
}

IFACEMETHODIMP FaceCredential::ReportResult(NTSTATUS st, NTSTATUS sub, PWSTR* status, CREDENTIAL_PROVIDER_STATUS_ICON* icon) {
    *status = nullptr;
    *icon = CPSI_NONE;
    if (st != STATUS_SUCCESS) {
        served_at_ = 0; unlocked_at_ = 0;   // sign-in failed: allow scanning again
        log_event(L"logon result 0x%08X / 0x%08X", (unsigned)st, (unsigned)sub);
        if (st == STATUS_LOGON_FAILURE || sub == STATUS_LOGON_FAILURE || st == STATUS_WRONG_PASSWORD) {
            fails_ = (int)cfg_.max_fails;  // stored password is wrong (changed?) - stop until setup is re-run
            SHStrDupW(L"Your password changed - re-run WinFace setup. Use your PIN for now.", status);
            *icon = CPSI_ERROR;
        }
    }
    return S_OK;
}

IFACEMETHODIMP FaceCredential::GetUserSid(PWSTR* sid) { return SHStrDupW(sid_.c_str(), sid); }

}  // namespace fgcp
