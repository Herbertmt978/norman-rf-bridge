#pragma once

#include <array>
#include <string>
#include <vector>
#include "learned_panel.h"

namespace esphome::norman_rf_monitor {

// Bound a round to eight 2ms radio-completion waits, below its55ms cadence.
inline constexpr size_t kBatchCapacity = 8;
using BatchFrames = std::array<norman_rf::Frame, kBatchCapacity>;

// count is validated nonzero before the scheduler starts. Rotate first place
// so a half-duplex external repeater cannot always favour the same target.
inline size_t batch_target_index(size_t round, size_t offset, size_t count) {
  return (round + offset) % count;
}

inline bool prepare_target_batch(std::array<LearnedPanel, 32> &panels,
                                 const std::vector<int32_t> &slots,
                                 const std::vector<int32_t> &positions,
                                 const std::vector<std::string> &identities,
                                 BatchFrames &frames) {
  if (slots.empty() || slots.size() > kBatchCapacity || positions.size() != slots.size() ||
      identities.size() != slots.size()) return false;
  std::array<bool, 32> seen{};
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto slot = slots[i];
    if (slot < 0 || slot >= 32 || seen[slot]) return false;
    seen[slot] = true;
    const auto &panel = panels[slot];
    if (!panel.ready() || panel.profile_id() != identities[i] || !panel.supports(positions[i])) return false;
    frames[i] = panel.next(positions[i]);
  }
  // Complete preflight before reserving any code. Reserve ALL before first RF.
  // A storage failure may consume earlier reservations but never emits a packet
  // or rewinds them. The caller must not retry an uncertain batch automatically.
  for (size_t i = 0; i < slots.size(); ++i)
    if (!panels[slots[i]].commit_transmit(frames[i])) return false;
  return true;
}

}  // namespace esphome::norman_rf_monitor
