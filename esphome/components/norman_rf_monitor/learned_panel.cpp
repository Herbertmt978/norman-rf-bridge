#include "learned_panel.h"
#include <cstdio>
#include <algorithm>

namespace esphome::norman_rf_monitor {

std::string LearnedPanel::profile_id() const {
  if (!ready_) return "";
  uint64_t hash = 14695981039346656037ULL;
  for (size_t i = 0; i < 28; ++i) {
    if (i == 24 || i == 25) continue;
    hash = (hash ^ target_.commands.open[i]) * 1099511628211ULL;
  }
  hash = (hash ^ target_.commands.close[4]) * 1099511628211ULL;
  hash = (hash ^ target_.commands.open_position) * 1099511628211ULL;
  if (target_.close_position != 0) hash = (hash ^ target_.close_position) * 1099511628211ULL;
  char output[17]{};
  snprintf(output, sizeof(output), "%016llx", static_cast<unsigned long long>(hash));
  return output;
}

void LearnedPanel::setup(uint8_t slot) {
  preference_ = global_preferences->make_preference<norman_rf::TargetProfile>(0x4e540100U + slot, true);
  marker_ = global_preferences->make_preference<uint32_t>(0x4e550100U + slot, true);
  ready_ = preference_.load(&target_) && norman_rf::valid_target(target_);
  if (ready_) return;
  uint32_t migrated{};
  if (marker_.load(&migrated)) { storage_fault_ = true; return; }
  if (slot != 0) return;
  norman_rf::LearnedProfile legacy{};
  auto old = global_preferences->make_preference<norman_rf::LearnedProfile>(0x4e524632U, true);
  if (!old.load(&legacy) || !norman_rf::valid_profile(legacy)) return;
  target_.commands = legacy;
  std::copy_n("Learned panel", 14, target_.name.begin());
  std::copy_n("Unassigned", 11, target_.room.begin());
  persist();  // Import once; original bytes remain available for explicit recovery.
}

bool LearnedPanel::configure(const int32_t *open, size_t open_size, const int32_t *close,
                             size_t close_size, int last_index, int open_position) {
  if (open == nullptr || close == nullptr || open_size != 30 || close_size != 30) return false;
  norman_rf::Frame o{}, c{};
  for (size_t i = 0; i < 30; ++i) {
    if (open[i] < 0 || open[i] > 255 || close[i] < 0 || close[i] > 255) return false;
    o[i] = static_cast<uint8_t>(open[i]); c[i] = static_cast<uint8_t>(close[i]);
  }
  return configure_target(o, c, last_index, open_position, 0,
                          ready_ ? name() : "Learned panel", ready_ ? room() : "Unassigned");
}

bool LearnedPanel::configure_target(const norman_rf::Frame &open, const norman_rf::Frame &close,
                                    int last_index, int open_position, int close_position,
                                    const std::string &name, const std::string &room) {
  if (storage_fault_ || last_index < 0 || last_index > 255 || open_position <= 0 || open_position >= 100 ||
      (close_position != 0 && close_position != 100) || name.empty() || name.size() >= 48 ||
      room.empty() || room.size() >= 48) return false;
  auto candidate = target_;
  if (ready_) {
    // Existing slot identities/counters/endpoints are not replaced by commissioning.
    if (open_position != this->open_position() || close_position != this->close_position() ||
        !norman_rf::learned_command(target_.commands, open) ||
        !norman_rf::learned_command(target_.commands, close) ||
        open[2] != target_.commands.open[2] || open[3] != target_.commands.open[3] ||
        open[4] != target_.commands.open[4] || close[2] != target_.commands.close[2] ||
        close[3] != target_.commands.close[3] || close[4] != target_.commands.close[4]) return false;
  } else {
    candidate = {};
    candidate.commands.open = open; candidate.commands.close = close;
    candidate.commands.last_index = static_cast<uint8_t>(last_index);
    candidate.commands.open_position = static_cast<uint8_t>(open_position);
    candidate.close_position = static_cast<uint8_t>(close_position);
  }
  candidate.name.fill(0); candidate.room.fill(0);
  std::copy(name.begin(), name.end(), candidate.name.begin());
  std::copy(room.begin(), room.end(), candidate.room.begin());
  if (!norman_rf::valid_target(candidate)) return false;
  target_ = candidate;
  return persist();
}

bool LearnedPanel::configure_close_up(const int32_t *frame, size_t size) {
  if (frame == nullptr || size != 30) return false;
  norman_rf::Frame parsed{};
  for (size_t i = 0; i < 30; ++i) {
    if (frame[i] < 0 || frame[i] > 255) return false;
    parsed[i] = static_cast<uint8_t>(frame[i]);
  }
  return configure_endpoint(parsed, 100);
}

bool LearnedPanel::configure_endpoint(const norman_rf::Frame &frame, int position) {
  if (!ready_ || position != 100 - target_.close_position) return false;
  auto candidate = target_;
  candidate.commands.close_up = frame;
  candidate.commands.has_close_up = 1;
  if (!norman_rf::valid_target(candidate)) return false;
  // Do not silently replace a learned endpoint with a different command.
  if (target_.commands.has_close_up && !accepts(frame)) return false;
  target_ = candidate;
  observe(frame);
  return persist();
}

bool LearnedPanel::set_relay(bool enabled) {
  if (!ready_) return false;
  target_.commands.relay_enabled = enabled ? 1 : 0;
  return persist();
}

bool LearnedPanel::commit_transmit(const norman_rf::Frame &frame) {
  if (!accepts(frame)) return false;
  const auto next_index = static_cast<uint8_t>(target_.commands.last_index + 1U);
  if (frame[24] != next_index || frame[25] != norman_rf::encode_rolling_index(next_index)) return false;
  target_.commands.last_index = next_index;
  return persist();  // Durable before RF: a reset must not reuse an emitted index.
}

bool LearnedPanel::observe(const norman_rf::Frame &frame) {
  if (!accepts(frame)) return false;
  const auto observed = norman_rf::decode_rolling_code(frame[25]);
  const auto advance = static_cast<uint8_t>(observed - target_.commands.last_index);
  // Ignore equal/old burst echoes. Large gaps are ambiguous and need explicit
  // reprovisioning; the community three-code resync claim is not enabled here.
  if (advance == 0 || advance > 127) return false;
  target_.commands.last_index = observed;
  return true;
}

bool LearnedPanel::persist() {
  if (!norman_rf::valid_target(target_)) { ready_ = false; return false; }
  const uint32_t marker = 0x4d494752U;
  // Marker is durable first: interrupted migration requires explicit recovery,
  // never an automatic fallback to an older rolling code.
  if (!marker_.save(&marker) || !global_preferences->sync() ||
      !preference_.save(&target_) || !global_preferences->sync()) {
    ready_ = false;
    storage_fault_ = true;
    return false;
  }
  ready_ = true;
  return true;
}

}  // namespace esphome::norman_rf_monitor
