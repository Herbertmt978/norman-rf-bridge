#include "learned_relay_endpoint.h"
#include <algorithm>

namespace esphome::norman_rf_monitor {

bool LearnedRelayEndpoint::valid_(const RelayEndpointProfile &profile) {
  if (profile.version != 1 || profile.name.front() == 0 || profile.name.back() != 0 ||
      !norman_rf::validate_frame(profile.command.data(), profile.command.size()).valid()) return false;
  for (const char c : profile.name) if (c != 0 && (c < 32 || c > 126)) return false;
  return true;
}

void LearnedRelayEndpoint::setup(uint8_t slot) {
  ready_ = false;
  storage_fault_ = true;
  if (slot >= kRelayEndpointCapacity) { storage_fault_ = true; return; }
  preference_ = global_preferences->make_preference<RelayEndpointProfile>(0x4e570100U + slot, true);
  marker_ = global_preferences->make_preference<uint32_t>(0x4e580100U + slot, true);
  const bool loaded = preference_.load(&profile_);
  ready_ = loaded && valid_(profile_);
  if (ready_) { storage_fault_ = false; return; }
  uint32_t marker{};
  storage_fault_ = loaded || marker_.load(&marker);
}

bool LearnedRelayEndpoint::configure(const norman_rf::Frame &command, const std::string &name) {
  if (storage_fault_ || name.empty() || name.size() >= 48 ||
      (ready_ && !accepts(command))) return false;
  RelayEndpointProfile candidate{};
  candidate.command = command;
  std::copy(name.begin(), name.end(), candidate.name.begin());
  if (!valid_(candidate)) return false;
  const uint32_t marker = 0x52454c59U;
  // Persist the marker first: failed commissioning must not look like a fresh slot.
  if (!marker_.save(&marker) || !global_preferences->sync() ||
      !preference_.save(&candidate) || !global_preferences->sync()) {
    ready_ = false;
    storage_fault_ = true;
    return false;
  }
  profile_ = candidate;
  ready_ = true;
  return true;
}

}  // namespace esphome::norman_rf_monitor
