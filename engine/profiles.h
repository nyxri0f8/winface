// WinFace engine - enrolled face profiles file (up to 3 profiles).
// Format: uint32 count, then per profile: uint32 name_len, name (UTF-8), uint32 n, n * 512 float32
#pragma once
#include <map>
#include <string>
#include <vector>

#include "recog.h"

namespace fg {

using Profiles = std::map<std::string, std::vector<Embedding>>;
constexpr size_t kMaxProfiles = 3;

bool load_profiles(const std::wstring& path, Profiles& out);
bool save_profiles(const std::wstring& path, const Profiles& p);

}  // namespace fg
