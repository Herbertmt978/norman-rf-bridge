#pragma once

#include "target_batch.h"

namespace esphome::norman_rf_monitor {

// Volatile, explicit retry of the last locally originated command. Never learns
// from RF, reserves another counter, survives reboot, or schedules itself.
class CommandRepeat {
 public:
  void clear() { remaining_ = 0; }

  void remember(const std::vector<int32_t> &slots, const std::vector<int32_t> &positions,
                const std::vector<std::string> &identities, const BatchFrames &frames,
                const std::array<LearnedPanel, 32> &panels, uint32_t now) {
    clear();
    if (slots.empty() || slots.size() > kBatchCapacity || positions.size() != slots.size() ||
        identities.size() != slots.size()) return;
    for (size_t i = 0; i < slots.size(); ++i) {
      if (slots[i] < 0 || slots[i] >= 32) return;
      indices_[i] = panels[slots[i]].last_index();
    }
    slots_ = slots;
    positions_ = positions;
    identities_ = identities;
    frames_ = frames;
    created_ms_ = now;
    remaining_ = 2;
  }

  bool take(const std::vector<int32_t> &slots, const std::vector<int32_t> &positions,
            const std::vector<std::string> &identities,
            const std::array<LearnedPanel, 32> &panels, uint32_t now, BatchFrames &frames) {
    if (remaining_ == 0 || static_cast<uint32_t>(now - created_ms_) >= 60000 ||
        slots != slots_ || positions != positions_ || identities != identities_) return false;
    for (size_t i = 0; i < slots_.size(); ++i) {
      const auto &panel = panels[slots_[i]];
      if (!panel.ready() || panel.profile_id() != identities_[i] ||
          panel.last_index() != indices_[i] || !panel.accepts(frames_[i])) {
        clear();
        return false;
      }
    }
    --remaining_;  // An attempted repeat consumes its budget, even on radio failure.
    frames = frames_;
    return true;
  }

  void observe(const norman_rf::Frame &frame) {
    if (remaining_ == 0) return;
    for (size_t i = 0; i < slots_.size(); ++i) {
      if (norman_rf::same_command_family(frames_[i], frame) && frames_[i] != frame) {
        clear();  // A conflicting observed command supersedes the whole batch.
        return;
      }
    }
  }

 private:
  std::vector<int32_t> slots_, positions_;
  std::vector<std::string> identities_;
  BatchFrames frames_{};
  std::array<int, kBatchCapacity> indices_{};
  uint32_t created_ms_{0};
  uint8_t remaining_{0};
};

}  // namespace esphome::norman_rf_monitor
