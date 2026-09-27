// WinFace setup back-end. The WinFace app drives it with --json (one JSON object per line on stdout);
// it also works by hand from an ADMINISTRATOR terminal.
//   status | profiles | cameras | logs                    (read-only)
//   enroll <name> [--json]     capture a face profile with the C++ engine (max 3)
//   remove <name>
//   password [--stdin]         Windows password -> TPM (lock screen) + DPAPI (test); verified with Windows first
//   verify                     check the saved password with Windows
//   mode off|test|lock
//   set <Setting> <value>      Camera | SearchMs | ChallengeMs | MaxFails | Strictness | Sounds
//   test [--json]              full face check (scan + liveness + head turn) without signing in
// Privacy: never prints the password; face data stays in %ProgramData%\FaceGate (admin-only).
#include <windows.h>
#include <conio.h>
#include <sddl.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "../cp/common.h"
#include "../cp/secret.h"
#include "../engine/camera.h"
#include "../engine/decide.h"
#include "../engine/profiles.h"

using namespace fg;

static bool g_json = false;

// ---------- small helpers ----------
static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::string jstr(const std::string& s) {   // JSON string literal
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
static std::string jstr(const std::wstring& s) { return jstr(utf8(s)); }
static void emit(const std::string& obj) { fputs(obj.c_str(), stdout); fputc('\n', stdout); fflush(stdout); }
static void say(const char* human, const std::string& json_obj) { if (g_json) emit(json_obj); else printf("%s\n", human); }
static int fail(const std::string& msg, int code) {
    if (g_json) emit("{\"t\":\"error\",\"msg\":" + jstr(msg) + "}"); else printf("error: %s\n", msg.c_str());
    return code;
}

static std::wstring install_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    return s.substr(0, s.find_last_of(L'\\'));
}

static bool is_admin() {
    BOOL admin = FALSE;
    PSID grp = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &grp)) {
        CheckTokenMembership(nullptr, grp, &admin);
        FreeSid(grp);
    }
    return admin;
}

static std::wstring current_sid() {
    HANDLE tok;
    std::wstring r;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return r;
    BYTE buf[256];
    DWORD n = 0;
    LPWSTR s = nullptr;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &n) && ConvertSidToStringSidW(((TOKEN_USER*)buf)->User.Sid, &s)) {
        r = s;
        LocalFree(s);
    }
    CloseHandle(tok);
    return r;
}

static bool set_reg(const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\FaceGate", 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr)) return false;
    bool ok = RegSetValueExW(k, name, 0, REG_DWORD, (BYTE*)&v, sizeof v) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

static bool set_reg_sz(const wchar_t* name, const std::wstring& v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\FaceGate", 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr)) return false;
    bool ok = RegSetValueExW(k, name, 0, REG_SZ, (BYTE*)v.c_str(), DWORD((v.size() + 1) * 2)) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

static std::wstring read_hidden(const wchar_t* prompt) {
    wprintf(L"%s", prompt);
    std::wstring s;
    for (;;) {
        wint_t c = _getwch();
        if (c == L'\r' || c == L'\n') break;
        if (c == 8) { if (!s.empty()) s.pop_back(); continue; }
        if (c == 3) { s.clear(); break; }
        s.push_back((wchar_t)c);
    }
    wprintf(L"\n");
    return s;
}

// one line of UTF-8 from the app's private stdin pipe (never from the command line)
static std::wstring read_stdin_line() {
    std::string b;
    char c;
    DWORD n;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    while (ReadFile(in, &c, 1, &n, nullptr) && n == 1 && c != '\n') if (c != '\r') b.push_back(c);
    int wn = MultiByteToWideChar(CP_UTF8, 0, b.c_str(), (int)b.size(), nullptr, 0);
    std::wstring w(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, b.c_str(), (int)b.size(), w.data(), wn);
    SecureZeroMemory(b.data(), b.size());
    return w;
}

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static double sharpness(const Image& c) {
    double sum = 0, sq = 0;
    int n = 0;
    auto g = [&](int x, int y) { const uint8_t* p = c.row(y) + x * 3; return 0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2]; };
    for (int y = 1; y < c.h - 1; ++y)
        for (int x = 1; x < c.w - 1; ++x) {
            double l = g(x - 1, y) + g(x + 1, y) + g(x, y - 1) + g(x, y + 1) - 4 * g(x, y);
            sum += l; sq += l * l; ++n;
        }
    double m = sum / n;
    return sq / n - m * m;
}

// 478 landmarks, centred on the face and scaled by face height (for the app's live mesh)
static std::string mesh_json(const Face& f) {
    std::string s = "[";
    float cx = (f.x0 + f.x1) / 2.0f, cy = (f.y0 + f.y1) / 2.0f, h = float(std::max(1, f.y1 - f.y0));
    char b[32];
    for (size_t i = 0; i < f.pts.size(); ++i) {
        snprintf(b, sizeof b, "%s%.3f,%.3f", i ? "," : "", (f.pts[i][0] - cx) / h, (f.pts[i][1] - cy) / h);
        s += b;
    }
    return s + "]";
}

struct Engine3 {   // the three models, loaded once
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "winface"};
    FaceMesh mesh;
    Recognizer rec;
    Texture tex;
    explicit Engine3(const std::wstring& md) : mesh(env, md), rec(env, md), tex(env, md) {}
};

// ---------- commands ----------
static int enroll(const std::string& name) {
    if (name.empty() || name.size() > 32) return fail("name must be 1-32 characters", 1);
    std::wstring prof_path = fgcp::data_dir() + L"\\profiles.bin";
    Profiles profiles;
    load_profiles(prof_path, profiles);
    if (!profiles.count(name) && profiles.size() >= kMaxProfiles) return fail("already 3 faces - remove one first", 1);
    Ort::InitApi();
    Engine3 e(install_dir() + L"\\models");
    auto cfg = fgcp::Config::load();
    Camera cam(cfg.camera);
    if (!cam.open()) return fail("camera: " + cam.error(), 2);

    struct Pose { const char* say; float y0, y1, p0, p1; };
    const Pose poses[] = {{"Look straight at the camera", -8, 8, -8, 8}, {"Slowly turn your head LEFT", -35, -12, -15, 15},
                          {"Slowly turn your head RIGHT", 12, 35, -15, 15}, {"Tilt your head UP a little", -15, 15, 10, 30},
                          {"Tilt your head DOWN a little", -15, 15, -30, -10}};
    const int per_pose = 15, n_poses = 5;
    std::vector<Embedding> embs;
    Image frame;
    uint64_t seq = 0;
    double ts = 0, t_last = 0;
    for (int pi = 0; pi < n_poses; ++pi) {
        const Pose& ps = poses[pi];
        int got = 0;
        if (!g_json) printf("\n  >> %s\n", ps.say);
        while (got < per_pose) {
            if (!cam.next(frame, seq, ts, 1000)) return fail("camera stopped", 2);
            auto f = e.mesh.process(frame);
            const char* msg = "No face - sit in front of the camera";
            if (f) {
                int w = f->width();
                if (w < 170) msg = "Move closer";
                else if (w > 330) msg = "Move back a little";
                else if (f->yaw < ps.y0 || f->yaw > ps.y1 || f->pitch < ps.p0 || f->pitch > ps.p1) msg = ps.say;
                else if (sharpness(Recognizer::align112(frame, *f)) < 60) msg = "Hold still";
                else { embs.push_back(e.rec.embed(frame, *f)); ++got; msg = "Capturing"; }
            }
            if (now_ms() - t_last > 60) {
                t_last = now_ms();
                if (g_json) {
                    char b[256];
                    snprintf(b, sizeof b, "{\"t\":\"frame\",\"pose\":%d,\"poses\":%d,\"got\":%d,\"per\":%d,\"say\":%s,\"msg\":%s,\"yaw\":%.1f,\"pitch\":%.1f,\"mesh\":",
                             pi, n_poses, got, per_pose, jstr(std::string(ps.say)).c_str(), jstr(std::string(msg)).c_str(),
                             f ? f->yaw : 0.f, f ? f->pitch : 0.f);
                    emit(std::string(b) + (f ? mesh_json(*f) : "[]") + "}");
                } else {
                    printf("\r     %2d/%d  %-40s", got, per_pose, msg);
                }
            }
        }
    }
    cam.close();
    Embedding c{};
    for (auto& v : embs) for (int i = 0; i < 512; ++i) c[i] += v[i];
    double n = 0;
    for (float v : c) n += double(v) * v;
    for (float& v : c) v = float(v / std::sqrt(n));
    std::string similar;
    for (auto& [other, t] : profiles)
        if (other != name && match_score(t, c) > 0.42f) similar = other;
    profiles[name] = embs;
    CreateDirectoryW(fgcp::data_dir().c_str(), nullptr);
    if (!save_profiles(prof_path, profiles)) return fail("cannot save faces (run as administrator)", 3);
    char b[200];
    snprintf(b, sizeof b, "{\"t\":\"done\",\"ok\":true,\"name\":%s,\"captures\":%zu,\"similar_to\":%s}", jstr(name).c_str(),
             embs.size(), jstr(similar).c_str());
    say(("Saved face '" + name + "'" + (similar.empty() ? "" : " (warning: looks like '" + similar + "')")).c_str(), b);
    return 0;
}

static int remove_profile(const std::string& name) {
    std::wstring p = fgcp::data_dir() + L"\\profiles.bin";
    Profiles profiles;
    load_profiles(p, profiles);
    if (!profiles.erase(name)) return fail("no face named '" + name + "'", 1);
    if (profiles.empty()) DeleteFileW(p.c_str()); else save_profiles(p, profiles);
    say(("removed '" + name + "'").c_str(), "{\"t\":\"done\",\"ok\":true}");
    return 0;
}

static std::vector<std::wstring> msa_names() {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\IdentityCRL\\UserExtendedProperties", 0, KEY_READ, &k)) return out;
    wchar_t nm[256];
    for (DWORD i = 0;; ++i) {
        DWORD n = 256;
        if (RegEnumKeyExW(k, i, nm, &n, nullptr, nullptr, nullptr, nullptr)) break;
        out.push_back(nm);
    }
    RegCloseKey(k);
    return out;
}

// same check `runas` does; local accounts too. Returns the account that accepted the password, or empty.
static std::wstring check_password(const std::wstring& pw) {
    HANDLE tok = nullptr;
    for (auto& email : msa_names())
        if (LogonUserW(email.c_str(), L"MicrosoftAccount", pw.c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &tok)) {
            CloseHandle(tok);
            return L"MicrosoftAccount\\" + email;
        }
    wchar_t user[256];
    DWORD n = 256;
    if (GetUserNameW(user, &n) && LogonUserW(user, L".", pw.c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &tok)) {
        CloseHandle(tok);
        return std::wstring(L".\\") + user;
    }
    return L"";
}

static int verify() {
    fgcp::SecurePassword pw;
    std::wstring err;
    if (!fgcp::dpapi_load(pw, err)) return fail("no saved password", 2);
    std::wstring who = check_password(pw.value);
    say(who.empty() ? "Windows REJECTED the saved password" : "Windows ACCEPTED the saved password",
        "{\"t\":\"done\",\"ok\":" + std::string(who.empty() ? "false" : "true") + ",\"account\":" + jstr(who) + "}");
    return who.empty() ? 1 : 0;
}

static int password(bool from_stdin) {
    std::wstring a, b;
    if (from_stdin) {
        a = read_stdin_line();
        b = a;
    } else {
        printf("Your Windows (Microsoft account) password is stored encrypted by the TPM, readable only by the lock screen.\n"
               "It is never shown or logged. Your PIN will NOT work here - it must be the account password.\n\n");
        a = read_hidden(L"Password: ");
        b = read_hidden(L"Again:    ");
    }
    auto wipe = [&] { SecureZeroMemory(a.data(), a.size() * 2); SecureZeroMemory(b.data(), b.size() * 2); };
    if (a.empty() || a != b) { wipe(); return fail("passwords empty or different - nothing saved", 1); }
    std::wstring who = check_password(a);
    if (who.empty()) { wipe(); return fail("Windows did not accept this password - nothing saved", 1); }
    std::wstring err, err2;
    bool ok_tpm = fgcp::tpm_store(a, err);
    bool ok_dp = fgcp::dpapi_store(a, err2);
    wipe();
    if (!ok_tpm) return fail("TPM: " + utf8(err), 2);
    set_reg_sz(L"UserSid", current_sid());
    say("Password saved (TPM) and linked to this account.",
        "{\"t\":\"done\",\"ok\":true,\"account\":" + jstr(who) + ",\"test_copy\":" + (ok_dp ? "true" : "false") + "}");
    return 0;
}

static int mode(const std::wstring& m) {
    if (m == L"off") { set_reg(L"Enabled", 0); set_reg(L"TestMode", 0); }
    else if (m == L"test") { set_reg(L"Enabled", 1); set_reg(L"Scenarios", 1); set_reg(L"TestMode", 1); }
    else if (m == L"lock") { set_reg(L"Enabled", 1); set_reg(L"Scenarios", 2); set_reg(L"TestMode", 0); }
    else return fail("mode must be off | test | lock", 1);
    say(("mode " + utf8(m)).c_str(), "{\"t\":\"done\",\"ok\":true}");
    return 0;
}

static int set_setting(const std::wstring& name, const std::wstring& val) {
    if (name == L"Camera") {
        // stored as the stable device-path fragment, e.g. usb#vid_0408&pid_5496&mi_00
        std::wstring v = val;
        if (v.rfind(L"\\\\?\\", 0) == 0) v = v.substr(4);
        size_t h1 = v.find(L'#'), h2 = h1 == std::wstring::npos ? h1 : v.find(L'#', h1 + 1);
        if (h2 != std::wstring::npos) v = v.substr(0, h2);
        if (v.rfind(L"usb#", 0) != 0) return fail("only real USB cameras can be used", 1);
        set_reg_sz(L"Camera", v);
    } else if (name == L"SearchMs" || name == L"ChallengeMs" || name == L"MaxFails" || name == L"Strictness" || name == L"Sounds") {
        set_reg(name.c_str(), (DWORD)_wtoi(val.c_str()));   // the lock screen clamps every value to a safe range
    } else {
        return fail("unknown setting", 1);
    }
    say("saved", "{\"t\":\"done\",\"ok\":true}");
    return 0;
}

static int status() {
    auto c = fgcp::Config::load();
    Profiles p;
    load_profiles(fgcp::data_dir() + L"\\profiles.bin", p);
    bool pw = GetFileAttributesW((fgcp::data_dir() + L"\\secret.tpm").c_str()) != INVALID_FILE_ATTRIBUTES;
    const char* m = !c.enabled ? "off" : (c.scenarios & 2) ? "lock" : "test";
    if (g_json) {
        std::string faces = "[";
        for (auto& [n, t] : p) faces += (faces.size() > 1 ? "," : "") + std::string("{\"name\":") + jstr(n) + ",\"captures\":" + std::to_string(t.size()) + "}";
        char b[400];
        snprintf(b, sizeof b, "{\"t\":\"status\",\"mode\":\"%s\",\"linked\":%s,\"password\":%s,\"camera\":%s,\"search_ms\":%lu,"
                 "\"challenge_ms\":%lu,\"max_fails\":%lu,\"strictness\":%lu,\"sounds\":%s,\"faces\":",
                 m, c.user_sid.empty() ? "false" : "true", pw ? "true" : "false", jstr(c.camera).c_str(), c.search_ms,
                 c.challenge_ms, c.max_fails, c.strictness, c.sounds ? "true" : "false");
        emit(std::string(b) + faces + "]}");
    } else {
        printf("mode: %s  faces: %zu/%zu  password: %s  camera: %ls\n", m, p.size(), kMaxProfiles, pw ? "saved" : "none", c.camera.c_str());
        printf("search %lu ms, head turn %lu ms, max fails %lu, strictness %lu, sounds %d\n", c.search_ms, c.challenge_ms,
               c.max_fails, c.strictness, c.sounds);
    }
    return 0;
}

static int cameras() {
    std::string a = "[";
    for (auto& c : list_cameras()) {
        a += (a.size() > 1 ? "," : "") + std::string("{\"name\":") + jstr(c.name) + ",\"id\":" + jstr(c.symlink) +
             ",\"usable\":" + (c.hardware ? "true" : "false") + "}";
        if (!g_json) printf("%s %ls\n   %ls\n", c.hardware ? "[usable] " : "[virtual]", c.name.c_str(), c.symlink.c_str());
    }
    if (g_json) emit("{\"t\":\"cameras\",\"list\":" + a + "]}");
    return 0;
}

// full face check exactly like the lock screen, but it never signs anyone in
static int test() {
    auto cfg = fgcp::Config::load();
    Profiles profiles;
    if (!load_profiles(fgcp::data_dir() + L"\\profiles.bin", profiles)) return fail("no faces added yet", 1);
    Ort::InitApi();
    const double t0 = now_ms();
    Engine3 e(install_dir() + L"\\models");
    Camera cam(cfg.camera);
    if (!cam.open()) return fail("camera: " + cam.error(), 2);
    Params prm;
    prm.match = cfg.match_threshold();
    prm.search_timeout_ms = cfg.search_ms;
    prm.challenge_timeout_ms = cfg.challenge_ms;
    Engine eng(e.rec, e.tex, profiles, prm);
    eng.reset(now_ms());
    Image frame;
    uint64_t seq = 0;
    double ts = 0, t_last = 0;
    const char* names[] = {"search", "challenge", "unlock", "fail"};
    for (;;) {
        if (!cam.next(frame, seq, ts, 1000)) { cam.close(); return fail("camera stopped", 2); }
        std::vector<Face> faces;
        if (auto f = e.mesh.process(frame)) faces.push_back(*f);
        const Status& st = eng.step(frame, faces, now_ms());
        bool end = st.state == State::Unlock || st.state == State::Fail;
        if (g_json && (end || now_ms() - t_last > 50)) {
            t_last = now_ms();
            char b[400];
            snprintf(b, sizeof b, "{\"t\":\"frame\",\"state\":\"%s\",\"hint\":%s,\"progress\":%.2f,\"direction\":%d,\"score\":%.2f,\"texture\":%.2f,\"mesh\":",
                     names[(int)st.state], jstr(st.hint).c_str(), st.progress, st.state == State::Challenge ? st.direction : 0,
                     st.score, st.texture);
            emit(std::string(b) + (faces.empty() ? "[]" : mesh_json(faces[0])) + "}");
        }
        if (end) {
            cam.close();
            char b[400];
            snprintf(b, sizeof b, "{\"t\":\"done\",\"ok\":%s,\"ms\":%.0f,\"camera_ms\":%.0f,\"reason\":%s}",
                     st.state == State::Unlock ? "true" : "false", now_ms() - t0, cam.open_ms(), jstr(st.reason).c_str());
            say((std::string(st.state == State::Unlock ? "PASS: " : "FAIL: ") + st.reason).c_str(), b);
            return st.state == State::Unlock ? 0 : 1;
        }
    }
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--json") g_json = true; else args.push_back(a);
    }
    std::wstring cmd = args.empty() ? L"" : args[0];
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::wstring(); };
    auto narrow = [](const std::wstring& w) { return utf8(w); };
    try {
        if (cmd == L"status") return status();
        if (cmd == L"cameras") return cameras();
        if (cmd == L"verify") return verify();
        if (!is_admin()) return fail("run as administrator", 5);
        if (cmd == L"enroll" && args.size() > 1) return enroll(narrow(arg(1)));
        if (cmd == L"remove" && args.size() > 1) return remove_profile(narrow(arg(1)));
        if (cmd == L"password") return password(arg(1) == L"--stdin");
        if (cmd == L"mode" && args.size() > 1) return mode(arg(1));
        if (cmd == L"set" && args.size() > 2) return set_setting(arg(1), arg(2));
        if (cmd == L"test") return test();
    } catch (const std::exception& e) {
        return fail(e.what(), 3);
    }
    printf("usage: fgsetup status|cameras|verify|enroll <name>|remove <name>|password [--stdin]|mode off|test|lock|"
           "set <Setting> <value>|test   [--json]\n");
    return 1;
}
