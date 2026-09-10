#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>

namespace esphome {
inline std::map<uint32_t, std::vector<uint8_t>> persisted;
inline std::map<uint32_t, std::vector<uint8_t>> pending;
inline bool fail_save = false;
inline bool fail_sync = false;
inline int syncs_before_failure = -1;
class ESPPreferenceObject {
 public:
  explicit ESPPreferenceObject(uint32_t key = 0) : key_(key) {}
  template<typename T> bool load(T *value) {
    if (persisted[key_].size() != sizeof(T)) return false;
    std::memcpy(value, persisted[key_].data(), sizeof(T));
    return true;
  }
  template<typename T> bool save(const T *value) {
    if (fail_save) return false;
    const auto *data = reinterpret_cast<const uint8_t *>(value);
    pending[key_].assign(data, data + sizeof(T));
    return true;
  }
 private:
  uint32_t key_;
};
class Preferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key, bool) { return ESPPreferenceObject(key); }
  bool sync() {
    if (fail_sync || syncs_before_failure == 0) return false;
    if (syncs_before_failure > 0) --syncs_before_failure;
    for (auto &entry : pending) persisted[entry.first] = entry.second;
    pending.clear();
    return true;
  }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
}  // namespace esphome
