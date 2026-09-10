#pragma once

#include "esphome/core/preferences.h"
#include "norman_rf/protocol.h"
#include <string>

namespace esphome::norman_rf_monitor {

inline constexpr size_t kRelayEndpointCapacity = 32;

// A receive allowlist entry, not a motor target. No sequence state or TX API.
struct RelayEndpointProfile {
  norman_rf::Frame command{};
  std::array<char, 48> name{};
  uint8_t version{1};
};
static_assert(sizeof(RelayEndpointProfile) == 79, "Relay endpoint layout changed");

class LearnedRelayEndpoint {
 public:
  void setup(uint8_t slot);
  bool configure(const norman_rf::Frame &command, const std::string &name);
  bool accepts(const norman_rf::Frame &frame) const {
    return ready_ && norman_rf::same_learned_command(profile_.command, frame);
  }
  bool ready() const { return ready_; }
  std::string name() const { return ready_ ? profile_.name.data() : ""; }

 private:
  static bool valid_(const RelayEndpointProfile &profile);
  ESPPreferenceObject preference_;
  ESPPreferenceObject marker_;
  RelayEndpointProfile profile_{};
  bool ready_{false};
  bool storage_fault_{true};  // setup must establish a valid storage namespace first.
};

}  // namespace esphome::norman_rf_monitor
