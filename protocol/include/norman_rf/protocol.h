#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string_view>

namespace norman_rf {

// The observed application frame is 30 bytes. The proof-of-concept RF24 setup
// may receive a 32-byte static radio payload with two trailing pad bytes; raw
// radio capture must establish that before calling validate_frame().
inline constexpr std::size_t kFrameSize = 30;
inline constexpr std::uint8_t kObservedBodyLength = 24;
inline constexpr std::size_t kRollingIndexOffset = 24;
inline constexpr std::size_t kRollingCodeOffset = 25;
inline constexpr std::uint16_t kCrcPolynomial = 0x0083;
inline constexpr std::uint16_t kCrcInitialValue = 0xacc8;
using Frame = std::array<std::uint8_t, kFrameSize>;

struct LearnedProfile {
  Frame open{};
  Frame close{};
  Frame close_up{};
  std::uint8_t last_index{0};
  std::uint8_t open_position{37};
  std::uint8_t has_close_up{0};
  std::uint8_t relay_enabled{0};
  std::uint8_t version{2};
};
static_assert(sizeof(LearnedProfile) == 95, "Persistent profile layout changed");

// Versioned target wrapper preserves the deployed 95-byte payload exactly.
// The primary close template can represent either endpoint; the optional third
// template represents the opposite one. Translation is confined to these helpers.
struct TargetProfile {
  LearnedProfile commands{};
  std::array<char, 48> name{};
  std::array<char, 48> room{};
  std::uint8_t close_position{0};
  std::uint8_t version{1};
};
static_assert(sizeof(TargetProfile) == 193, "Persistent target layout changed");
[[nodiscard]] bool valid_target(const TargetProfile& target);
[[nodiscard]] int target_position(const TargetProfile& target, int position);

enum class RelayDecision { eligible, duplicate, full };

// Exact application-frame cache; never evict a live entry to admit another.
// Refresh echoes so a circulating packet cannot become eligible at expiry.
class RelayCache {
 public:
  RelayDecision admit(const Frame& frame, std::uint32_t now);
 private:
  struct Entry { Frame frame{}; std::uint32_t seen{0}; bool used{false}; };
  // One open/close per32 commissioned targets fits without evicting live echoes.
  std::array<Entry, 64> entries_{};
};

enum class ValidationError {
  none,
  wrong_size,
  unsupported_length,
  bad_crc,
};

struct ValidationResult {
  ValidationError error;
  std::uint16_t expected_crc;
  std::uint16_t actual_crc;

  [[nodiscard]] constexpr bool valid() const {
    return error == ValidationError::none;
  }
};

[[nodiscard]] std::uint16_t crc16(const std::uint8_t* data,
                                  std::size_t size);
[[nodiscard]] std::uint8_t reverse_bits(std::uint8_t value);
[[nodiscard]] std::uint8_t encode_rolling_index(std::uint8_t index);
[[nodiscard]] std::uint8_t decode_rolling_code(std::uint8_t code);
[[nodiscard]] ValidationResult validate_frame(const std::uint8_t* data,
                                              std::size_t size);
// Experimental bench boundary, not a product command/rolling-state API.
[[nodiscard]] bool valid_tx_request(const std::uint8_t* data, std::size_t size,
                                    int channel, int copies);
[[nodiscard]] bool same_command_family(const Frame& left, const Frame& right);
// Exact learned endpoint, allowing only sequence/CRC variation. Both must validate.
[[nodiscard]] bool same_learned_command(const Frame& left, const Frame& right);
[[nodiscard]] bool valid_profile(const LearnedProfile& profile);
[[nodiscard]] const Frame* command_template(const LearnedProfile& profile, int position);
[[nodiscard]] bool learned_command(const LearnedProfile& profile, const Frame& frame);
[[nodiscard]] bool parse_frame_hex(std::string_view value, Frame& frame);
[[nodiscard]] Frame next_command(const LearnedProfile& profile, int position);

}  // namespace norman_rf
