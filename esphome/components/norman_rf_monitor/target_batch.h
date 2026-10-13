#pragma once

#include <array>
#include <string>
#include <vector>
#include "learned_panel.h"

namespace esphome::norman_rf_monitor {

// Bound a round to eight 2ms radio-completion waits, below its55ms cadence.
inline constexpr size_t kBatchCapacity = 8;
using BatchFrames = std::array<norman_rf::Frame, kBatchCapacity>;

// rounds and count are validated nonzero before the scheduler starts. Each
// target owns one contiguous block of rounds; every copy in a round belongs to
// that target. Interleaving targets within a round (0.7-0.11.3) physically
// missed panels although every target's frames were on air. All Norman frames
// share one radio address; a duty-cycled motor plausibly samples another
// target's frame and sleeps. Sequential and hub sends never interleave.
// Contiguous blocks alone still missed a lounge panel on 2 of 2 camera-checked
// actions (13 Oct 2026); separate single-target commands moved 24 of 24, so
// the HA room covers now send sections one at a time.
// Total rounds and copies per round are unchanged; uneven splits differ by one round.
inline size_t batch_block_target(size_t round, size_t rounds, size_t count) {
  return round * count / rounds;
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
