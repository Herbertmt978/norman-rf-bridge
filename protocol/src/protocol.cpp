#include "norman_rf/protocol.h"

namespace norman_rf {

bool parse_frame_hex(std::string_view value, Frame& frame) {
  if (value.size() != kFrameSize * 2) return false;
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  Frame candidate{};
  for (std::size_t i = 0; i < kFrameSize; ++i) {
    const int high = nibble(value[i * 2]);
    const int low = nibble(value[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    candidate[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (!validate_frame(candidate.data(), candidate.size()).valid()) return false;
  frame = candidate;
  return true;
}

std::uint16_t crc16(const std::uint8_t* data, const std::size_t size) {
  std::uint16_t crc = kCrcInitialValue;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= static_cast<std::uint16_t>(data[index]) << 8U;
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      const bool high_bit_set = (crc & 0x8000U) != 0;
      crc = static_cast<std::uint16_t>(crc << 1U);
      if (high_bit_set) {
        crc ^= kCrcPolynomial;
      }
    }
  }
  return crc;
}

std::uint8_t reverse_bits(std::uint8_t value) {
  std::uint8_t reversed = 0;
  for (std::uint8_t bit = 0; bit < 8; ++bit) {
    reversed = static_cast<std::uint8_t>((reversed << 1U) | (value & 1U));
    value = static_cast<std::uint8_t>(value >> 1U);
  }
  return reversed;
}

std::uint8_t encode_rolling_index(const std::uint8_t index) {
  return static_cast<std::uint8_t>(0xddU ^ reverse_bits(index));
}

std::uint8_t decode_rolling_code(const std::uint8_t code) {
  return reverse_bits(static_cast<std::uint8_t>(code ^ 0xddU));
}

ValidationResult validate_frame(const std::uint8_t* data,
                                const std::size_t size) {
  if (data == nullptr || size != kFrameSize) {
    return {ValidationError::wrong_size, 0, 0};
  }
  if (data[0] != kObservedBodyLength) {
    return {ValidationError::unsupported_length, 0, 0};
  }

  constexpr std::size_t crc_offset = kObservedBodyLength + 4U;
  static_assert(crc_offset + 2U == kFrameSize);
  const std::uint16_t expected = crc16(data, crc_offset);
  const std::uint16_t actual =
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[crc_offset])
                                 << 8U) |
      data[crc_offset + 1U];
  if (expected != actual) {
    return {ValidationError::bad_crc, expected, actual};
  }
  return {ValidationError::none, expected, actual};
}

bool valid_tx_request(const std::uint8_t* data, const std::size_t size,
                      const int channel, const int copies) {
  return (channel == 15 || channel == 39 || channel == 59) &&
         copies >= 1 && copies <= 100 && validate_frame(data, size).valid();
}

bool same_command_family(const Frame& left, const Frame& right) {
  if (!validate_frame(left.data(), left.size()).valid() ||
      !validate_frame(right.data(), right.size()).valid()) return false;
  for (std::size_t i = 0; i < 28; ++i) {
    // In the owner's correlated captures upward-close also changes bytes 2/3.
    // This is a commissioning consistency check, not a runtime command allowlist.
    if ((i >= 2 && i <= 4) || i == kRollingIndexOffset || i == kRollingCodeOffset) continue;
    if (left[i] != right[i]) return false;
  }
  return true;
}

bool same_learned_command(const Frame& left, const Frame& right) {
  if (!same_command_family(left, right)) return false;
  return left[2] == right[2] && left[3] == right[3] && left[4] == right[4];
}

bool valid_profile(const LearnedProfile& profile) {
  return profile.version == 2 && profile.open_position > 0 && profile.open_position < 100 &&
         profile.has_close_up <= 1 && profile.relay_enabled <= 1 &&
         !same_learned_command(profile.open, profile.close) && same_command_family(profile.open, profile.close) &&
         (!profile.has_close_up ||
          (same_command_family(profile.open, profile.close_up) &&
           !same_learned_command(profile.close_up, profile.open) &&
           !same_learned_command(profile.close_up, profile.close)));
}

const Frame* command_template(const LearnedProfile& profile, int position) {
  if (!valid_profile(profile)) return nullptr;
  if (position == profile.open_position) return &profile.open;
  if (position == 0) return &profile.close;
  if (position == 100 && profile.has_close_up) return &profile.close_up;
  return nullptr;
}

bool valid_target(const TargetProfile& target) {
  const auto label_ok = [](const auto& value) {
    if (value[0] == 0 || value.back() != 0) return false;
    for (const char c : value) if (c != 0 && (c < 32 || c > 126)) return false;
    return true;
  };
  return target.version == 1 && (target.close_position == 0 || target.close_position == 100) &&
         label_ok(target.name) && label_ok(target.room) && valid_profile(target.commands);
}

int target_position(const TargetProfile& target, int position) {
  if (!valid_target(target)) return -1;
  if (position == target.commands.open_position) return position;
  if (position == target.close_position) return 0;
  if (position == 100 - target.close_position && target.commands.has_close_up) return 100;
  return -1;
}

bool learned_command(const LearnedProfile& profile, const Frame& frame) {
  return valid_profile(profile) &&
         (same_learned_command(profile.open, frame) || same_learned_command(profile.close, frame) ||
          (profile.has_close_up && same_learned_command(profile.close_up, frame)));
}

Frame next_command(const LearnedProfile& profile, int position) {
  const Frame* selected = command_template(profile, position);
  if (selected == nullptr) return {};
  Frame frame = *selected;
  const auto index = static_cast<std::uint8_t>(profile.last_index + 1U);
  frame[kRollingIndexOffset] = index;
  frame[kRollingCodeOffset] = encode_rolling_index(index);
  const auto crc = crc16(frame.data(), 28);
  frame[28] = static_cast<std::uint8_t>(crc >> 8U);
  frame[29] = static_cast<std::uint8_t>(crc);
  return frame;
}

RelayDecision RelayCache::admit(const Frame& frame, const std::uint32_t now) {
  Entry* available = nullptr;
  for (auto& entry : entries_) {
    if (entry.used && entry.frame == frame) {
      const bool recent = static_cast<std::uint32_t>(now - entry.seen) < 60000;
      entry.seen = now;
      return recent ? RelayDecision::duplicate : RelayDecision::eligible;
    }
    if (!entry.used || static_cast<std::uint32_t>(now - entry.seen) >= 60000) available = &entry;
  }
  if (available == nullptr) return RelayDecision::full;
  available->frame = frame;
  available->seen = now;
  available->used = true;
  return RelayDecision::eligible;
}

}  // namespace norman_rf
