#pragma once

#include "esphome/core/preferences.h"
#include "norman_rf/protocol.h"
#include <string>

namespace esphome::norman_rf_monitor {

// One independently persisted target. No customer identifiers in the image.
// Owns persistence and rolling-state policy; the radio owner commits a counter
// before admitting a transmission. No automatic resynchronisation/replay loop.
class LearnedPanel {
 public:
  void setup(uint8_t slot = 0);
  bool configure_target(const norman_rf::Frame &open, const norman_rf::Frame &close,
                        int last_index, int open_position, int close_position,
                        const std::string &name, const std::string &room,
                        const norman_rf::Frame *opposite = nullptr);
  bool configure(const int32_t *open, size_t open_size, const int32_t *close,
                 size_t close_size, int last_index, int open_position);
  bool configure_close_up(const int32_t *frame, size_t size);
  bool configure_endpoint(const norman_rf::Frame &frame, int position);
  bool set_relay(bool enabled);
  bool relay_enabled() const { return ready_ && target_.commands.relay_enabled; }
  bool accepts(const norman_rf::Frame &frame) const { return ready_ && norman_rf::learned_command(target_.commands, frame); }
  bool same_target(const norman_rf::Frame &frame) const { return ready_ && norman_rf::same_command_family(target_.commands.open, frame); }
  bool supports(int position) const { return ready_ && norman_rf::target_position(target_, position) >= 0; }
  int open_position() const { return target_.commands.open_position; }
  int close_position() const { return target_.close_position; }
  int last_index() const { return target_.commands.last_index; }
  std::string name() const { return ready_ ? target_.name.data() : ""; }
  std::string room() const { return ready_ ? target_.room.data() : ""; }
  std::string profile_id() const;
  bool ready() const { return ready_; }
  norman_rf::Frame next(int position) const { return norman_rf::next_command(target_.commands, norman_rf::target_position(target_, position)); }
  bool commit_transmit(const norman_rf::Frame &frame);
  bool observe(const norman_rf::Frame &frame);
  bool persist();
  bool remove(const std::string &expected_id);
  bool rename(const std::string &expected_id, const std::string &name, const std::string &room);

 private:
  ESPPreferenceObject preference_;
  ESPPreferenceObject marker_;
  norman_rf::TargetProfile target_{};
  bool ready_{false};
  bool storage_fault_{false};
};

}  // namespace esphome::norman_rf_monitor
