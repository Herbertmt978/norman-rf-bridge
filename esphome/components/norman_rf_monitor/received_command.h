#pragma once

#include "learned_panel.h"
#include "learned_relay_endpoint.h"

namespace esphome::norman_rf_monitor {

struct ReceivedCommandMatch {
  int panel_slot{-1};
  int relay_slot{-1};
  bool authorized() const { return panel_slot >= 0 || relay_slot >= 0; }
};

// One classifier owns ambiguity across direct targets and receive-only entries.
// A relay-only match never masquerades as a panel or advances its counter.
inline ReceivedCommandMatch match_received_command(
    const std::array<LearnedPanel, 32> &panels,
    const std::array<LearnedRelayEndpoint, kRelayEndpointCapacity> &endpoints,
    const norman_rf::Frame &frame) {
  ReceivedCommandMatch result;
  for (size_t i = 0; i < panels.size(); ++i) if (panels[i].accepts(frame)) {
    if (result.authorized()) return {};
    result.panel_slot = static_cast<int>(i);
  }
  for (size_t i = 0; i < endpoints.size(); ++i) if (endpoints[i].accepts(frame)) {
    if (result.authorized()) return {};
    result.relay_slot = static_cast<int>(i);
  }
  return result;
}

}  // namespace esphome::norman_rf_monitor
