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
    endpoint_ = endpoint; candidates_ = {}; candidate_count_ = 0; packets_ = 0;
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
    // Open establishes the target/controller identity. Another target cannot
    // provide Close evidence, but neither should it invalidate that evidence.
    // Conflicting actions from the selected identity still fail at accept.
    if (endpoint_ > 0 && !norman_rf::same_command_family(frames_[0], frame)) return;
    ++packets_;
    const uint16_t sample = relay_ ? uint16_t((uint16_t(frame[24]) << 8) | frame[25]) : frame[25];
    for (size_t i = 0; i < candidate_count_; ++i) {
      auto &candidate = candidates_[i];
      if (!norman_rf::same_learned_command(candidate.frame, frame)) continue;
      // Freeze this candidate's evidence, but keep listening for ambiguity.
      if (candidate.samples == 2) return;
      for (size_t j = 0; j < candidate.seen_count; ++j)
        if (candidate.seen[j] == sample) return;
      if (candidate.seen_count == candidate.seen.size()) { error_ = "mixed_actions"; return; }
      candidate.seen[candidate.seen_count++] = sample;
      if (uint32_t(now - candidate.first_at) < 8000) return;
      // Hub-originated codes need not advance within the serial-number half
      // range. A same-code/payload variant is still not an independent press.
      candidate.frame = frame; candidate.samples = 2;
      return;
    }
    // Never select the first/loudest candidate or evict ambiguity under load.
    if (candidate_count_ == candidates_.size()) { error_ = "mixed_actions"; return; }
    auto &candidate = candidates_[candidate_count_++];
    candidate.frame = frame; candidate.first_at = now; candidate.samples = 1;
    candidate.seen[0] = sample; candidate.seen_count = 1;
  }
  bool accept(const std::string &token, uint32_t now) {
    if (!owns(token) || !active(now) || !capturing_) return false;
    if (!error_.empty()) return false;
    if (uint32_t(now - capture_started_) >= 60000) { error_ = "capture_expired"; return false; }
    const Candidate *selected = nullptr;
    for (size_t i = 0; i < candidate_count_; ++i) {
      if (candidates_[i].samples != 2) continue;
      if (selected != nullptr) { error_ = "mixed_actions"; return false; }
      selected = &candidates_[i];
    }
    if (selected == nullptr) {
      error_ = candidate_count_ > 1 ? "mixed_actions" : "need_two_presses";
      return false;
    }
    for (size_t i = 0; i < candidate_count_; ++i) {
      if (&candidates_[i] != selected &&
          norman_rf::same_command_family(selected->frame, candidates_[i].frame)) {
        error_ = "mixed_actions"; return false;
      }
    }
    const auto &candidate = selected->frame;
    if (endpoint_ > 0 && (!norman_rf::same_command_family(frames_[0], candidate) ||
        norman_rf::same_learned_command(frames_[0], candidate) ||
        (endpoint_ == 2 && norman_rf::same_learned_command(frames_[1], candidate)))) {
      error_ = "wrong_endpoint"; return false;
    }
    frames_[endpoint_] = candidate; accepted_[endpoint_] = true;
    last_index_ = norman_rf::decode_rolling_code(candidate[25]);
    capturing_ = false;
    return true;
  }
  bool ready(uint32_t now) { return active(now) && !capturing_ && accepted_[0] && (relay_ || accepted_[1]); }
  void cancel() { active_ = false; capturing_ = false; error_ = "cancelled"; }
  void saved(int slot) { active_ = false; capturing_ = false; saved_slot_ = slot; error_.clear(); }
  bool relay() const { return relay_; }
  bool capturing() const { return capturing_; }
  bool capture_expired(uint32_t now) const { return capturing_ && uint32_t(now - capture_started_) >= 60000; }
  int endpoint() const { return endpoint_; }
  uint8_t unique() const {
    uint8_t samples = 0;
    for (size_t i = 0; i < candidate_count_; ++i)
      if (candidates_[i].samples > samples) samples = candidates_[i].samples;
    return samples;
  }
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
  struct Candidate {
    norman_rf::Frame frame{};
    uint32_t first_at{0};
    uint8_t samples{0};
    std::array<uint16_t, 16> seen{};
    size_t seen_count{0};
  };
  std::array<Candidate, 8> candidates_{};
  size_t candidate_count_{0};
  std::array<norman_rf::Frame, 3> frames_{};
  std::array<bool, 3> accepted_{};
};

}  // namespace esphome::norman_rf_monitor
