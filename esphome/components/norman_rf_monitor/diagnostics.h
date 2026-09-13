#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace esphome::norman_rf_monitor {

// Volatile evidence only. Never writes profiles or changes RF decisions.
class ActivityAge {
 public:
  void mark(uint64_t now) { seen_ = true; at_ = now; }
  float seconds(uint64_t now) const {
    return seen_ && now >= at_ ? float(now - at_) / 1000.0f : NAN;
  }
 private:
  bool seen_{false};
  uint64_t at_{0};
};

}  // namespace esphome::norman_rf_monitor
