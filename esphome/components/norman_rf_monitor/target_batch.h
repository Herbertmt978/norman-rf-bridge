#pragma once

#include <array>
#include <string>
#include <vector>
#include "learned_panel.h"

namespace esphome::norman_rf_monitor {

inline constexpr size_t kBatchCapacity = 8;
using BatchFrames = std::array<norman_rf::Frame, kBatchCapacity>;

// Sequential room schedule. Each target receives its complete burst, one copy
// per 55 ms tick, followed by an idle gap before the next target; one target is
// the original single-command cadence. Interleaved (0.7-0.11.3) and contiguous
// four-per-tick (0.11.4) batches missed panels in camera trials although every
// target's frames were on air. Separate full single-target bursts about 0.6 s
// apart moved 24 of 24 lounge panels; this reproduces that pattern on the ESP.
inline constexpr size_t kTargetGapTicks = 11;  // About 0.6 s at 55 ms per tick.

// copies and count are validated nonzero before the scheduler starts.
inline size_t sequential_total_ticks(size_t copies, size_t count) {
  return copies * count + kTargetGapTicks * (count - 1);
}

// Selects the target for this tick; false means an idle gap tick.
inline bool sequential_target(size_t tick, size_t copies, size_t &target) {
  const size_t span = copies + kTargetGapTicks;
  target = tick / span;
  return tick % span < copies;
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
