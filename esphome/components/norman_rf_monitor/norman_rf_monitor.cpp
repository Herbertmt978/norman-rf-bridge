#include "norman_rf_monitor.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/json/json_util.h"
#include "norman_rf/protocol.h"

namespace esphome::norman_rf_monitor {

static const char *const TAG = "norman_rf_monitor";

void NormanRfMonitor::setup() {
  for (uint8_t slot = 0; slot < panels_.size(); ++slot) panels_[slot].setup(slot);
  for (uint8_t slot = 0; slot < relay_endpoints_.size(); ++slot) relay_endpoints_[slot].setup(slot);
  setup_relay_();
  if (this->ce_pin_ == nullptr) {
    this->set_fault_("ce_pin_missing");
    return;
  }

  this->ce_pin_->setup();
  this->ce_pin_->digital_write(false);
  this->spi_setup();

  if (!this->configure_receive_only_() || !this->verify_configuration_()) {
    this->set_fault_("register_readback_failed");
    return;
  }

  this->radio_ready_ = true;
  this->hardware_status_ = "nRF24L01+ detected";
  this->select_receive_channel_();
  this->update_register_snapshot_();
  this->ce_pin_->digital_write(true);
  this->set_interval("poll_radio", 1, [this]() { this->poll_radio_(); });
  this->set_interval("hop_channel", kChannelDwellMs, [this]() { this->hop_channel_(); });
  ESP_LOGI(TAG, "Radio verified; learned panel=%s; autonomous relay=%s", YESNO(panels_[0].ready()), YESNO(relay_enabled()));
}

void NormanRfMonitor::dump_config() {
  ESP_LOGCONFIG(TAG, "Norman RF Monitor:");
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  ESP_LOGCONFIG(TAG, "  Mode: %s; learned commands only; relay 15 -> 39 -> 59", mode_status_.c_str());
  ESP_LOGCONFIG(TAG, "  Radio status: %s", this->hardware_status_.c_str());
  ESP_LOGCONFIG(TAG, "  Registers: %s", this->register_snapshot_.c_str());
}

bool NormanRfMonitor::read_register_(uint8_t address, uint8_t *value) {
  this->enable();
  this->transfer_byte(kCommandReadRegister | (address & 0x1f));
  *value = this->transfer_byte(kCommandNoOperation);
  this->disable();
  return true;
}

bool NormanRfMonitor::write_register_(uint8_t address, uint8_t value) {
  this->enable();
  this->transfer_byte(kCommandWriteRegister | (address & 0x1f));
  this->transfer_byte(value);
  this->disable();
  return true;
}

uint8_t NormanRfMonitor::issue_command_(uint8_t command) {
  this->enable();
  const uint8_t status = this->transfer_byte(command);
  this->disable();
  return status;
}

bool NormanRfMonitor::configure_receive_only_() {
  this->ce_pin_->digital_write(false);

  // RX_ADDR_P0 is written least-significant byte first. This matches RF24's
  // openReadingPipe(0, uint64_t{0xdc5c9c1c05}) in the published Norman sketch.
  const std::array<uint8_t, 5> address = {0x05, 0x1c, 0x9c, 0x5c, 0xdc};
  this->write_register_(kRegisterEnableAutoAck, 0x00);
  this->write_register_(kRegisterEnableRxAddress, 0x01);
  this->write_register_(kRegisterSetupAddressWidth, 0x03);
  this->write_register_(kRegisterSetupRetransmit, 0x00);
  this->write_register_(kRegisterRfChannel, kCandidateChannels[channel_index_]);
  this->write_register_(kRegisterRfSetup, 0x00);
  this->write_register_(kRegisterRxPayloadWidthPipe0, kPayloadWidth);

  this->enable();
  this->transfer_byte(kCommandWriteRegister | kRegisterRxAddressPipe0);
  for (const uint8_t byte : address) {
    this->transfer_byte(byte);
  }
  this->disable();

  this->issue_command_(kCommandFlushRx);
  this->write_register_(kRegisterStatus, kStatusClearAllInterrupts);
  this->write_register_(kRegisterConfig, kConfigPowerUp | kConfigPrimaryRx);
  delay(5);
  return true;
}

bool NormanRfMonitor::verify_configuration_() {
  uint8_t config{};
  uint8_t auto_ack{};
  uint8_t enabled_rx{};
  uint8_t address_width{};
  uint8_t retransmit{};
  uint8_t channel{};
  uint8_t setup{};
  uint8_t payload_width{};
  this->read_register_(kRegisterConfig, &config);
  this->read_register_(kRegisterEnableAutoAck, &auto_ack);
  this->read_register_(kRegisterEnableRxAddress, &enabled_rx);
  this->read_register_(kRegisterSetupAddressWidth, &address_width);
  this->read_register_(kRegisterSetupRetransmit, &retransmit);
  this->read_register_(kRegisterRfChannel, &channel);
  this->read_register_(kRegisterRfSetup, &setup);
  this->read_register_(kRegisterRxPayloadWidthPipe0, &payload_width);

  return config == (kConfigPowerUp | kConfigPrimaryRx) && auto_ack == 0x00 &&
         enabled_rx == 0x01 && address_width == 0x03 && retransmit == 0x00 &&
         channel == kCandidateChannels[channel_index_] && setup == 0x00 &&
         payload_width == kPayloadWidth;
}

void NormanRfMonitor::hop_channel_() {
  if (!this->radio_ready_ || this->tx_active_) {
    return;
  }

  // The nRF24L01+ can only retune from standby-I. CE is low during every
  // retune, which preserves the receive-only boundary while RF_CH changes.
  this->ce_pin_->digital_write(false);
  // Drain packets on the channel that received them before changing RF_CH.
  // A matched packet may start a relay, in which case that burst owns the radio.
  this->poll_radio_();
  if (this->tx_active_) return;
  const size_t channel_count = this->relay_enabled() ? 2 : kCandidateChannels.size();
  this->channel_index_ = (this->channel_index_ + 1) % channel_count;
  this->write_register_(kRegisterRfChannel, kCandidateChannels[channel_index_]);
  this->ce_pin_->digital_write(true);
}

void NormanRfMonitor::poll_radio_() {
  if (!this->radio_ready_ || this->tx_active_) {
    return;
  }

  // RX_DR is an event flag, not FIFO occupancy: clearing it after one read
  // must not strand the other two hardware FIFO entries.
  for (uint8_t reads = 0; reads < 3; ++reads) {
    std::array<uint8_t, kPayloadWidth> payload{};
    if (!this->read_payload_(&payload)) {
      break;
    }

    ++this->received_count_;
    const uint32_t now = millis();
    const auto validation = norman_rf::validate_frame(payload.data(), norman_rf::kFrameSize);
    const bool valid = validation.valid();
    const bool duplicate = valid && this->is_duplicate_(payload, now);
    if (duplicate) {
      ++this->duplicate_count_;
    } else if (valid) {
      ++this->valid_count_;
    } else {
      ++this->invalid_count_;
    }
    if (valid && !duplicate) {
      this->log_payload_if_budgeted_(payload, true, now);
    }
    this->write_register_(kRegisterStatus, kStatusClearAllInterrupts);
    if (valid) this->handle_valid_frame_(payload, now);
    if (this->tx_active_) return;
  }
}

bool NormanRfMonitor::read_payload_(std::array<uint8_t, kPayloadWidth> *payload) {
  uint8_t fifo_status{};
  this->read_register_(kRegisterFifoStatus, &fifo_status);
  if ((fifo_status & kFifoRxEmpty) != 0) {
    return false;
  }

  this->enable();
  this->transfer_byte(kCommandReadRxPayload);
  for (uint8_t &byte : *payload) {
    byte = this->transfer_byte(kCommandNoOperation);
  }
  this->disable();
  return true;
}

bool NormanRfMonitor::is_duplicate_(const std::array<uint8_t, kPayloadWidth> &payload,
                                    uint32_t now) {
  const bool duplicate = this->has_last_payload_ &&
                         std::equal(payload.begin(), payload.begin() + norman_rf::kFrameSize,
                                    this->last_payload_.begin()) &&
                         static_cast<uint32_t>(now - this->last_frame_ms_) <= kDuplicateWindowMs;
  this->last_payload_ = payload;
  this->last_frame_ms_ = now;
  this->has_last_payload_ = true;
  return duplicate;
}

void NormanRfMonitor::log_payload_if_budgeted_(
    const std::array<uint8_t, kPayloadWidth> &payload, bool valid, uint32_t now) {
  if (static_cast<uint32_t>(now - this->log_window_start_ms_) >= kLogWindowMs) {
    this->log_window_start_ms_ = now;
    this->log_count_in_window_ = 0;
  }
  if (this->log_count_in_window_ >= kLogLimitPerWindow) {
    return;
  }
  ++this->log_count_in_window_;

  char line[kPayloadWidth * 3 + 1]{};
  size_t offset = 0;
  for (const uint8_t byte : payload) {
    offset += snprintf(line + offset, sizeof(line) - offset, "%02x ", byte);
  }
  ESP_LOGI(TAG, "RX %s ms=%lu ch=%u payload: %s", valid ? "valid" : "invalid",
           static_cast<unsigned long>(now), kCandidateChannels[channel_index_], line);
}

void NormanRfMonitor::set_fault_(const std::string &reason) {
  if (this->ce_pin_ != nullptr) {
    this->ce_pin_->digital_write(false);
  }
  this->radio_ready_ = false;
  this->hardware_status_ = "fault";
  this->mode_status_ = reason;
  this->register_snapshot_ = "unavailable";
  ESP_LOGE(TAG, "Receive-only monitor fault: %s", reason.c_str());
}

void NormanRfMonitor::update_register_snapshot_() {
  uint8_t config{};
  uint8_t channel{};
  uint8_t setup{};
  uint8_t status{};
  this->read_register_(kRegisterConfig, &config);
  this->read_register_(kRegisterRfChannel, &channel);
  this->read_register_(kRegisterRfSetup, &setup);
  status = this->issue_command_(kCommandNoOperation);

  char snapshot[64]{};
  snprintf(snapshot, sizeof(snapshot), "CONFIG=0x%02x RF_CH=0x%02x RF_SETUP=0x%02x STATUS=0x%02x HOP=15,39,59",
           config, channel, setup, status);
  this->register_snapshot_ = snapshot;
}

bool NormanRfMonitor::can_transmit_() const {
  return radio_ready_ && !tx_active_ &&
         (!tx_has_finished_ || static_cast<uint32_t>(millis() - tx_last_finished_ms_) >= 250);
}


bool NormanRfMonitor::configure_target(int slot, const std::string &name, const std::string &room,
                                       const std::string &open, const std::string &close,
                                       int last_index, int open_position, int close_position) {
  if (tx_active_ || slot < 0 || slot >= 32) return false;
  norman_rf::Frame o{}, c{};
  if (!norman_rf::parse_frame_hex(open, o) || !norman_rf::parse_frame_hex(close, c)) return false;
  for (const auto &endpoint : relay_endpoints_) if (endpoint.accepts(o) || endpoint.accepts(c)) return false;
  for (int i = 0; i < 32; ++i) if (i != slot && panels_[i].same_target(o)) return false;
  return panels_[slot].configure_target(o, c, last_index, open_position, close_position, name, room);
}

bool NormanRfMonitor::configure_target_endpoint(int slot, int position, const std::string &frame) {
  if (tx_active_ || slot < 0 || slot >= 32) return false;
  norman_rf::Frame parsed{};
  if (!norman_rf::parse_frame_hex(frame, parsed)) return false;
  for (const auto &endpoint : relay_endpoints_) if (endpoint.accepts(parsed)) return false;
  return panels_[slot].configure_endpoint(parsed, position);
}

bool NormanRfMonitor::configure_relay_endpoint(int slot, const std::string &name, const std::string &frame) {
  if (tx_active_ || slot < 0 || slot >= static_cast<int>(relay_endpoints_.size())) return false;
  norman_rf::Frame parsed{};
  if (!norman_rf::parse_frame_hex(frame, parsed)) return false;
  for (const auto &panel : panels_) if (panel.accepts(parsed)) return false;
  for (size_t i = 0; i < relay_endpoints_.size(); ++i)
    if (static_cast<int>(i) != slot && relay_endpoints_[i].accepts(parsed)) return false;
  // Learning grants receive/forward eligibility only; it cannot start a burst.
  return relay_endpoints_[slot].configure(parsed, name);
}

bool NormanRfMonitor::persist_relay_() {
  const uint32_t marker = 0x4d494752U;
  const uint32_t policy = 0x52460000U | (relay_requested_ ? 1U : 0U);
  relay_policy_ready_ = relay_marker_.save(&marker) && global_preferences->sync() &&
                        relay_preference_.save(&policy) && global_preferences->sync();
  return relay_policy_ready_;
}

void NormanRfMonitor::setup_relay_() {
  relay_preference_ = global_preferences->make_preference<uint32_t>(0x4e560100U, true);
  relay_marker_ = global_preferences->make_preference<uint32_t>(0x4e560101U, true);
  uint32_t policy{};
  if (relay_preference_.load(&policy)) {
    relay_policy_ready_ = (policy & 0xfffffffeU) == 0x52460000U;
    relay_requested_ = relay_policy_ready_ && (policy & 1U);
    return;
  }
  uint32_t marker{};
  if (relay_marker_.load(&marker)) return;
  relay_requested_ = panels_[0].relay_enabled();  // Import deployed single-profile policy once.
  persist_relay_();
}

bool NormanRfMonitor::configure_panel(const std::string &open, const std::string &close,
                                      int last_index, int open_position) {
  return configure_target(0, panels_[0].ready() ? panels_[0].name() : "Learned panel",
                          panels_[0].ready() ? panels_[0].room() : "Unassigned",
                          open, close, last_index, open_position, 0);
}

bool NormanRfMonitor::configure_close_up(const std::string &frame) {
  return configure_target_endpoint(0, 100, frame);
}

std::string NormanRfMonitor::panel_status() const {
  if (!panels_[0].ready()) return "uncommissioned_or_storage_fault";
  if (relay_fault_) return "relay_fault_rearm_required";
  return panels_[0].supports(100) ? "open_close_down_close_up_learned" : "open_close_down_learned";
}

bool NormanRfMonitor::transmit_command(int position) {
  return transmit_target(0, position, panels_[0].profile_id());
}

bool NormanRfMonitor::transmit_target(int slot, int position, const std::string &expected_profile) {
  return transmit_targets({slot}, {position}, {expected_profile});
}

bool NormanRfMonitor::set_relay(bool enabled) {
  if (tx_active_ || !radio_ready_ || !relay_policy_ready_) return false;
  if (enabled && std::none_of(panels_.begin(), panels_.end(), [](const auto &p) { return p.ready(); }) &&
      std::none_of(relay_endpoints_.begin(), relay_endpoints_.end(), [](const auto &p) { return p.ready(); })) return false;
  relay_requested_ = enabled;
  if (!persist_relay_()) return false;
  // Overflow requires an explicit disable/enable cycle, not incoming RF.
  if (!enabled) relay_fault_ = false;
  select_receive_channel_();
  return true;
}

void NormanRfMonitor::select_receive_channel_() {
  if (!radio_ready_ || tx_active_) return;
  ce_pin_->digital_write(false);
  if (relay_enabled()) channel_index_ = 0;  // Scan both direct and first-hop traffic.
  write_register_(kRegisterRfChannel, kCandidateChannels[channel_index_]);
  mode_status_ = relay_enabled() ? "learned_relay_15_39_to_39_59" : "monitor_hop_15_39_59";
  ce_pin_->digital_write(true);
}

void NormanRfMonitor::handle_valid_frame_(const std::array<uint8_t, kPayloadWidth> &payload, uint32_t now) {
  norman_rf::Frame frame{};
  std::copy_n(payload.begin(), frame.size(), frame.begin());
  const auto match = match_received_command(panels_, relay_endpoints_, frame);
  if (!match.authorized()) return;
  const int slot = match.panel_slot;
  if (slot >= 0 && panels_[slot].observe(frame)) {
    set_timeout("save_counter_" + std::to_string(slot), 1000, [this, slot]() {
      if (!panels_[slot].persist()) select_receive_channel_();
    });
  }
  const int input_channel = kCandidateChannels[channel_index_];
  const int output_channel = norman_rf::relay_output_channel(input_channel);
  if (!relay_enabled() || output_channel < 0) return;
  // Do not consume the deduplication entry during cooldown: a subsequent copy
  // in the incoming burst must remain eligible once the transmitter is ready.
  if (!can_transmit_()) return;
  const auto decision = relay_cache_.admit(frame, now);
  if (decision == norman_rf::RelayDecision::duplicate) return;
  if (decision == norman_rf::RelayDecision::full) {
    relay_fault_ = true;
    ++dropped_count_;
    select_receive_channel_();
    ESP_LOGE(TAG, "Relay cache full; relay stopped until explicitly rearmed");
    return;
  }
  if (!start_burst_(frame, output_channel, 20, true)) ++dropped_count_;
  else {
    ++relayed_count_;
    ESP_LOGI(TAG, "Relay accepted panel_slot=%d relay_slot=%d frame_crc=%02x%02x; unchanged %d -> %d",
             slot, match.relay_slot, frame[28], frame[29], input_channel, output_channel);
  }
}

bool NormanRfMonitor::start_burst_(const norman_rf::Frame &frame, int channel, int copies, bool relay) {
  BatchFrames frames{};
  frames[0] = frame;
  return start_bursts_(frames, 1, channel, copies, relay);
}

bool NormanRfMonitor::transmit_targets(const std::vector<int32_t> &slots,
                                     const std::vector<int32_t> &positions,
                                     const std::vector<std::string> &identities) {
  if (!can_transmit_()) return false;
  BatchFrames frames{};
  if (!prepare_target_batch(panels_, slots, positions, identities, frames)) return false;
  return start_bursts_(frames, slots.size(), 15, 100, false);
}

bool NormanRfMonitor::start_bursts_(const BatchFrames &frames, size_t count, int channel, int copies, bool relay) {
  const uint32_t now = millis();
  if (!can_transmit_() || count == 0 || count > kBatchCapacity) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!norman_rf::valid_tx_request(frames[i].data(), frames[i].size(), channel, copies)) return false;
  }
  if (!relay) for (size_t i = 0; i < count; ++i) {
    if (relay_cache_.admit(frames[i], now) == norman_rf::RelayDecision::full) relay_fault_ = true;
  }
  for (size_t i = 0; i < count; ++i) {
    tx_payloads_[i].fill(0);
    std::copy(frames[i].begin(), frames[i].end(), tx_payloads_[i].begin());
  }
  tx_target_count_ = count;
  this->tx_remaining_ = static_cast<uint16_t>(copies);
  this->tx_active_ = true;
  this->tx_is_relay_ = relay;
  if (!relay) this->command_success_ = false;
  this->tx_started_ms_ = now;
  this->tx_status_ = relay ? "relay_active" : "command_active";
  this->ce_pin_->digital_write(false);
  this->issue_command_(0xe1);  // FLUSH_TX: every trial starts with an empty FIFO.
  this->write_register_(kRegisterStatus, kStatusClearAllInterrupts);
  this->write_register_(kRegisterRfChannel, static_cast<uint8_t>(channel));
  this->write_register_(kRegisterRfSetup, 0x00);  // Lowest nRF power setting, 1 Mbps.
  this->enable();
  this->transfer_byte(kCommandWriteRegister | 0x10);  // TX_ADDR, LSB first.
  for (const uint8_t byte : {0x05, 0x1c, 0x9c, 0x5c, 0xdc}) this->transfer_byte(byte);
  this->disable();
  this->write_register_(kRegisterConfig, kConfigPowerUp);  // PTX, no radio CRC/ACK.
  this->set_interval("bounded_tx", 55, [this]() { this->send_test_copy_(); });
  ESP_LOGI(TAG, "TX armed ms=%lu channel=%d targets=%u copies_each=%d round=55ms width=32 min_power",
           static_cast<unsigned long>(now), channel, static_cast<unsigned>(count), copies);
  return true;
}

bool NormanRfMonitor::transmit_targets_json(const std::string &request) {
  if (request.size() > 2048 || !can_transmit_()) return false;
  return json::parse_json(request, [this](JsonObject root) -> bool {
    if (root.size() != 3 || !root["slots"].is<JsonArray>() || !root["positions"].is<JsonArray>() ||
        !root["profile_ids"].is<JsonArray>()) return false;
    const auto slots = root["slots"].as<JsonArray>();
    const auto positions = root["positions"].as<JsonArray>();
    const auto identities = root["profile_ids"].as<JsonArray>();
    if (slots.size() == 0 || slots.size() > kBatchCapacity || positions.size() != slots.size() ||
        identities.size() != slots.size()) return false;
    std::vector<int32_t> s, p;
    std::vector<std::string> ids;
    for (size_t i = 0; i < slots.size(); ++i) {
      if (!slots[i].is<int32_t>() || !positions[i].is<int32_t>() || !identities[i].is<const char *>()) return false;
      s.push_back(slots[i].as<int32_t>()); p.push_back(positions[i].as<int32_t>());
      ids.emplace_back(identities[i].as<const char *>());
    }
    return transmit_targets(s, p, ids);
  });
}

void NormanRfMonitor::send_test_copy_() {
  if (!this->tx_active_) return;
  if (static_cast<uint32_t>(millis() - this->tx_started_ms_) > 6500) {
    this->finish_test_("test_timeout");
    return;
  }
  const uint32_t round_started = micros();
  for (size_t offset = 0; offset < tx_target_count_; ++offset) {
  const size_t target = batch_target_index(100 - tx_remaining_, offset, tx_target_count_);
  this->write_register_(kRegisterStatus, kStatusClearAllInterrupts);
  this->enable();
  this->transfer_byte(0xa0);  // W_TX_PAYLOAD, 30-byte frame plus two zero bytes.
  for (const uint8_t byte : this->tx_payloads_[target]) this->transfer_byte(byte);
  this->disable();
  this->ce_pin_->digital_write(true);
  delay_microseconds_safe(15);
  this->ce_pin_->digital_write(false);
  const uint32_t started = micros();
  uint8_t status = 0;
  do {
    status = this->issue_command_(kCommandNoOperation);
    if (status & 0x30) break;  // TX_DS or MAX_RT.
    delay_microseconds_safe(25);
  } while (static_cast<uint32_t>(micros() - started) < 2000);
  if (!(status & 0x20) || (status & 0x10)) {
    this->finish_test_("radio_tx_failed");
    return;
  }
  ++this->transmitted_count_;
  }
  if (tx_remaining_ == 100 && tx_target_count_ > 1)
    ESP_LOGI(TAG, "First round: %u targets completed in %lu us",
             static_cast<unsigned>(tx_target_count_), static_cast<unsigned long>(micros() - round_started));
  if (--this->tx_remaining_ == 0) this->finish_test_("complete_not_acknowledged");
}

void NormanRfMonitor::finish_test_(const char *status) {
  this->cancel_interval("bounded_tx");
  this->ce_pin_->digital_write(false);
  this->issue_command_(0xe1);
  this->write_register_(kRegisterStatus, kStatusClearAllInterrupts);
  this->write_register_(kRegisterRfChannel, kCandidateChannels[this->channel_index_]);
  this->write_register_(kRegisterConfig, kConfigPowerUp | kConfigPrimaryRx);
  this->ce_pin_->digital_write(true);
  this->tx_active_ = false;
  if (!tx_is_relay_) command_success_ = std::string(status) == "complete_not_acknowledged";
  if (std::string(status) != "complete_not_acknowledged") relay_fault_ = true;
  this->tx_has_finished_ = true;
  this->tx_last_finished_ms_ = millis();
  this->tx_status_ = status;
  this->select_receive_channel_();
  ESP_LOGI(TAG, "TX finished: %s; total=%lu; monitor resumed", status,
           static_cast<unsigned long>(this->transmitted_count_));
}

}  // namespace esphome::norman_rf_monitor
