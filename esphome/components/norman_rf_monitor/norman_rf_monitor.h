#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "learned_panel.h"
#include "received_command.h"
#include "target_batch.h"
#include "command_repeat.h"

namespace esphome::norman_rf_monitor {

class NormanRfMonitor : public Component,
                        public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                              spi::CLOCK_POLARITY_LOW,
                                              spi::CLOCK_PHASE_LEADING,
                                              spi::DATA_RATE_4MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_ce_pin(GPIOPin *ce_pin) { ce_pin_ = ce_pin; }

  bool radio_ready() const { return radio_ready_; }
  std::string hardware_status() const { return hardware_status_; }
  std::string mode_status() const { return mode_status_; }
  std::string transmit_status() const { return tx_status_; }
  static constexpr size_t kTargetCapacity = 32;
  const LearnedRelayEndpoint *relay_endpoint(int slot) const {
    return slot >= 0 && slot < static_cast<int>(kRelayEndpointCapacity) ? &relay_endpoints_[slot] : nullptr;
  }
  bool configure_relay_endpoint(int slot, const std::string &name, const std::string &frame);
  const LearnedPanel *target(int slot) const { return slot >= 0 && slot < 32 ? &panels_[slot] : nullptr; }
  bool configure_target(int slot, const std::string &name, const std::string &room,
                        const std::string &open, const std::string &close,
                        int last_index, int open_position, int close_position);
  bool configure_target_endpoint(int slot, int position, const std::string &frame);
  bool transmit_target(int slot, int position, const std::string &expected_profile);
  bool transmit_targets(const std::vector<int32_t> &slots, const std::vector<int32_t> &positions,
                        const std::vector<std::string> &identities);
  bool transmit_targets_json(const std::string &request, bool repeat = false);
  bool repeat_targets(const std::vector<int32_t> &slots, const std::vector<int32_t> &positions,
                      const std::vector<std::string> &identities);
  bool configure_panel(const std::string &open, const std::string &close, int last_index, int open_position);
  bool configure_close_up(const std::string &frame);
  bool transmit_command(int position);
  bool set_relay(bool enabled);
  void set_automatic_repeats(bool enabled);
  bool automatic_repeats() const { return automatic_repeats_; }
  uint8_t repeats_remaining() const;
  uint32_t automatic_repeat_count() const { return automatic_repeat_count_; }
  bool relay_enabled() const { return relay_policy_ready_ && relay_requested_ && !relay_fault_; }
  bool panel_ready() const { return panels_[0].ready(); }
  int rolling_index() const { return panels_[0].last_index(); }
  int open_position() const { return panels_[0].open_position(); }
  bool close_up_ready() const { return panels_[0].supports(100); }
  bool command_active() const { return tx_active_ && !tx_is_relay_; }
  bool command_success() const { return command_success_; }
  uint32_t relayed_count() const { return relayed_count_; }
  std::string panel_status() const;
  std::string profile_id() const { return panels_[0].profile_id(); }
  uint32_t transmitted_count() const { return transmitted_count_; }
  std::string register_snapshot() const { return register_snapshot_; }
  uint32_t received_count() const { return received_count_; }
  uint32_t valid_count() const { return valid_count_; }
  uint32_t invalid_count() const { return invalid_count_; }
  uint32_t duplicate_count() const { return duplicate_count_; }
  uint32_t dropped_count() const { return dropped_count_; }

 protected:
  static constexpr uint8_t kRegisterConfig = 0x00;
  static constexpr uint8_t kRegisterEnableAutoAck = 0x01;
  static constexpr uint8_t kRegisterEnableRxAddress = 0x02;
  static constexpr uint8_t kRegisterSetupAddressWidth = 0x03;
  static constexpr uint8_t kRegisterSetupRetransmit = 0x04;
  static constexpr uint8_t kRegisterRfChannel = 0x05;
  static constexpr uint8_t kRegisterRfSetup = 0x06;
  static constexpr uint8_t kRegisterStatus = 0x07;
  static constexpr uint8_t kRegisterRxAddressPipe0 = 0x0a;
  static constexpr uint8_t kRegisterRxPayloadWidthPipe0 = 0x11;
  static constexpr uint8_t kRegisterFifoStatus = 0x17;

  static constexpr uint8_t kCommandReadRegister = 0x00;
  static constexpr uint8_t kCommandWriteRegister = 0x20;
  static constexpr uint8_t kCommandReadRxPayload = 0x61;
  static constexpr uint8_t kCommandFlushRx = 0xe2;
  static constexpr uint8_t kCommandNoOperation = 0xff;

  static constexpr uint8_t kConfigPowerUp = 0x02;
  static constexpr uint8_t kConfigPrimaryRx = 0x01;
  static constexpr uint8_t kStatusRxDataReady = 0x40;
  static constexpr uint8_t kStatusClearAllInterrupts = 0x70;
  static constexpr uint8_t kFifoRxEmpty = 0x01;
  static constexpr std::array<uint8_t, 3> kCandidateChannels = {15, 39, 59};
  static constexpr uint32_t kChannelDwellMs = 20;
  static constexpr uint8_t kPayloadWidth = 32;
  static constexpr uint32_t kDuplicateWindowMs = 250;
  static constexpr uint32_t kLogWindowMs = 60000;
  // Bounded commissioning budget: include command/reply bursts for whole-room
  // learning without silently dropping later target commands in the same minute.
  static constexpr uint8_t kLogLimitPerWindow = 60;

  bool read_register_(uint8_t address, uint8_t *value);
  bool write_register_(uint8_t address, uint8_t value);
  uint8_t issue_command_(uint8_t command);
  bool configure_receive_only_();
  bool verify_configuration_();
  void hop_channel_();
  void poll_radio_();
  bool read_payload_(std::array<uint8_t, kPayloadWidth> *payload);
  bool is_duplicate_(const std::array<uint8_t, kPayloadWidth> &payload,
                     uint32_t now);
  void log_payload_if_budgeted_(const std::array<uint8_t, kPayloadWidth> &payload,
                                bool valid, uint32_t now);
  void set_fault_(const std::string &reason);
  void update_register_snapshot_();
  void send_test_copy_();
  void finish_test_(const char *status);
  bool start_burst_(const norman_rf::Frame &frame, int channel, int copies, bool relay);
  bool start_bursts_(const BatchFrames &frames, size_t count, int channel, int copies, bool relay);
  bool can_transmit_() const;
  bool persist_relay_();
  void setup_relay_();
  void select_receive_channel_();
  void handle_valid_frame_(const std::array<uint8_t, kPayloadWidth> &payload, uint32_t now);
  void repeat_if_due_();

  GPIOPin *ce_pin_{nullptr};
  bool radio_ready_{false};
  std::string hardware_status_{"Not fitted"};
  std::string mode_status_{"hardware_missing"};
  std::string register_snapshot_{"unread"};
  uint32_t received_count_{0};
  uint32_t valid_count_{0};
  uint32_t invalid_count_{0};
  uint32_t duplicate_count_{0};
  uint32_t dropped_count_{0};
  uint32_t last_frame_ms_{0};
  uint32_t log_window_start_ms_{0};
  uint8_t log_count_in_window_{0};
  bool has_last_payload_{false};
  uint8_t channel_index_{0};
  std::array<uint8_t, kPayloadWidth> last_payload_{};
  std::array<std::array<uint8_t, kPayloadWidth>, kBatchCapacity> tx_payloads_{};
  size_t tx_target_count_{0};
  bool tx_active_{false};
  uint16_t tx_remaining_{0};
  uint32_t tx_started_ms_{0};
  uint32_t tx_last_finished_ms_{0};
  bool tx_has_finished_{false};
  uint32_t transmitted_count_{0};
  std::string tx_status_{"idle"};
  std::array<LearnedPanel, 32> panels_{};
  std::array<LearnedRelayEndpoint, kRelayEndpointCapacity> relay_endpoints_{};
  ESPPreferenceObject relay_preference_;
  ESPPreferenceObject relay_marker_;
  bool relay_requested_{false};
  bool relay_policy_ready_{false};
  norman_rf::RelayCache relay_cache_;
  CommandRepeat command_repeat_;
  bool automatic_repeats_{false};  // Restored by the ESPHome policy switch at boot.
  uint32_t automatic_repeat_count_{0};
  bool relay_fault_{false};
  bool tx_is_relay_{false};
  bool command_success_{false};
  uint32_t relayed_count_{0};
};

}  // namespace esphome::norman_rf_monitor
