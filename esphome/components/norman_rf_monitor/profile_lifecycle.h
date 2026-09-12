#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace esphome::norman_rf_monitor {
inline bool valid_identity(const std::string &id) {
  if (id.size() != 16) return false;
  for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
// A complete, preference-CRC-protected tombstone in the existing record size.
// No namespace reset, global preferences erase, or stale legacy fallback.
template<class Profile> bool deleted_profile(const Profile &profile) {
  if (profile.version != 255 || profile.name[16] != 0) return false;
  const std::string id(profile.name.data(), 16);
  if (!valid_identity(id)) return false;
  Profile expected{};
  expected.version = 255;
  std::copy(id.begin(), id.end(), expected.name.begin());
  return std::memcmp(&expected, &profile, sizeof(Profile)) == 0;
}
}  // namespace esphome::norman_rf_monitor
