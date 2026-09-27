#include "profiles.h"

#include <cstdint>
#include <fstream>

namespace fg {

bool load_profiles(const std::wstring& path, Profiles& out) {
    std::ifstream f(path, std::ios::binary);
    uint32_t n = 0;
    if (!f.read((char*)&n, 4) || n > kMaxProfiles) return false;
    out.clear();
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t len = 0, cnt = 0;
        if (!f.read((char*)&len, 4) || len == 0 || len > 64) return false;
        std::string name(len, '\0');
        f.read(name.data(), len);
        if (!f.read((char*)&cnt, 4) || cnt == 0 || cnt > 1000) return false;
        std::vector<Embedding> t(cnt);
        if (!f.read((char*)t.data(), std::streamsize(cnt * sizeof(Embedding)))) return false;
        out[name] = std::move(t);
    }
    return !out.empty();
}

bool save_profiles(const std::wstring& path, const Profiles& p) {
    if (p.empty() || p.size() > kMaxProfiles) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    uint32_t n = (uint32_t)p.size();
    f.write((const char*)&n, 4);
    for (auto& [name, t] : p) {
        uint32_t len = (uint32_t)name.size(), cnt = (uint32_t)t.size();
        f.write((const char*)&len, 4);
        f.write(name.data(), len);
        f.write((const char*)&cnt, 4);
        f.write((const char*)t.data(), std::streamsize(cnt * sizeof(Embedding)));
    }
    return bool(f);
}

}  // namespace fg
