#pragma once

#include "norman_rf/protocol.h"
#include <array>
#include <string>

namespace esphome::norman_rf_monitor {

// Volatile, bounded capture. No RF or persistence authority; commit is explicit.
class LearningSession {
 public:
  bool active(uint32_t now) {
    if (active_ && uint32_t(now - started_) >= 600000) {
      active_ = false; capturing_ = false; error_ = "expired";
    }
    return active_;
  }
  bool owns(const std::string &token) const { return !token_.empty() && token == token_; }
  bool begin(const std::string &token, bool relay, uint32_t now) {
    if (active(now) || token.size() != 32) return false;
    for (char c : token) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    *this = LearningSession{};
    token_ = token; relay_ = relay; started_ = now; active_ = true;
    return true;
  }
  bool capture(const std::string &token, int endpoint, uint32_t now) {
    if (!owns(token) || !active(now) || endpoint < 0 || endpoint > 2 ||
        (relay_ && endpoint != 0) || (endpoint > 0 && !accepted_[0]) ||
        (endpoint == 2 && !accepted_[1])) return false;
    endpoint_ = endpoint; candidate_ = {}; unique_ = 0; packets_ = 0;
    capture_started_ = now; capturing_ = true; error_.clear();
    // Relearning Open invalidates dependent captures; no mixed old/new families.
    for (int i = endpoint; i < 3; ++i) accepted_[i] = false;
    return true;
  }
  void observe(const norman_rf::Frame &frame, uint32_t now) {
    if (!active(now)) return;
    if (!capturing_) {
      if (accepted_[0] && norman_rf::same_command_family(frames_[0], frame)) {
        const auto observed = norman_rf::decode_rolling_code(frame[25]);
        const uint8_t advance = uint8_t(observed - last_index_);
        if (advance > 0 && advance <= 127) last_index_ = observed;
      }
      return;
    }
    if (!error_.empty() ||
        uint32_t(now - capture_started_) >= 60000 ||
        !norman_rf::validate_frame(frame.data(), frame.size()).valid()) return;
    ++packets_;
    if (unique_ == 0) { candidate_ = frame; unique_ = 1; return; }
    if (!norman_rf::same_learned_command(candidate_, frame)) {
      error_ = "mixed_actions"; return;
    }
    if (candidate_ == frame) return;  // Hundreds of burst copies aren't new presses.
    if (!relay_) {
      const uint8_t advance = uint8_t(norman_rf::decode_rolling_code(frame[25]) -
                                      norman_rf::decode_rolling_code(candidate_[25]));
      if (advance == 0 || advance > 127) return;  // Late echoes cannot roll backwards.
    }
    candidate_ = frame;
    if (unique_ < 255) ++unique_;
  }
  bool accept(const std::string &token, uint32_t now) {
    if (!owns(token) || !active(now) || !capturing_) return false;
    if (!error_.empty()) return false;
    if (uint32_t(now - capture_started_) >= 60000) { error_ = "capture_expired"; return false; }
    if (unique_ < 2) { error_ = "need_two_presses"; return false; }
    if (endpoint_ > 0 && (!norman_rf::same_command_family(frames_[0], candidate_) ||
        norman_rf::same_learned_command(frames_[0], candidate_) ||
        (endpoint_ == 2 && norman_rf::same_learned_command(frames_[1], candidate_)))) {
      error_ = "wrong_endpoint"; return false;
    }
    frames_[endpoint_] = candidate_; accepted_[endpoint_] = true;
    last_index_ = norman_rf::decode_rolling_code(candidate_[25]);
    capturing_ = false;
    return true;
  }
  bool ready(uint32_t now) { return active(now) && !capturing_ && accepted_[0] && (relay_ || accepted_[1]); }
  void cancel() { active_ = false; capturing_ = false; error_ = "cancelled"; }
  void saved(int slot) { active_ = false; capturing_ = false; saved_slot_ = slot; error_.clear(); }
  bool relay() const { return relay_; }
  bool capturing() const { return capturing_; }
  int endpoint() const { return endpoint_; }
  uint8_t unique() const { return unique_; }
  uint32_t packets() const { return packets_; }
  int last_index() const { return last_index_; }
  int saved_slot() const { return saved_slot_; }
  bool has_opposite() const { return accepted_[2]; }
  const norman_rf::Frame &frame(int endpoint) const { return frames_[endpoint]; }
  const std::string &error() const { return error_; }

 private:
  std::string token_, error_;
  bool active_{false}, relay_{false}, capturing_{false};
  uint32_t started_{0}, capture_started_{0}, packets_{0};
  int endpoint_{0}, last_index_{0}, saved_slot_{-1};
  uint8_t unique_{0};
  norman_rf::Frame candidate_{};
  std::array<norman_rf::Frame, 3> frames_{};
  std::array<bool, 3> accepted_{};
};

}  // namespace esphome::norman_rf_monitor
