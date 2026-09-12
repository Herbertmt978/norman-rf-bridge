#include "norman_rf/protocol.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

// Synthetic frame: length byte 0x18, ascending test data, and a separately
// precomputed Norman application CRC. It intentionally contains no captured
// installation identifiers or commands.
constexpr std::array<std::uint8_t, norman_rf::kFrameSize> kSyntheticFrame = {
    0x18, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x9a, 0xd5,
};

int failures = 0;

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_known_crc_vector_and_synthetic_frame_validate() {
  constexpr std::array<std::uint8_t, 9> kCheckText = {
      '1', '2', '3', '4', '5', '6', '7', '8', '9',
  };
  expect(norman_rf::crc16(kCheckText.data(), kCheckText.size()) == 0xf473,
         "CRC of the standard check text should be 0xf473");

  const auto result = norman_rf::validate_frame(kSyntheticFrame.data(),
                                                 kSyntheticFrame.size());
  expect(result.valid(), "synthetic frame should validate");
  expect(result.actual_crc == 0x9ad5,
         "synthetic frame CRC should be 0x9ad5");
}

void test_invalid_frames_are_rejected() {
  auto corrupt = kSyntheticFrame;
  corrupt[4] ^= 0x01;
  expect(norman_rf::validate_frame(corrupt.data(), corrupt.size()).error ==
             norman_rf::ValidationError::bad_crc,
         "a changed command byte should fail CRC validation");

  auto unsupported = kSyntheticFrame;
  unsupported[0] = 23;
  expect(norman_rf::validate_frame(unsupported.data(), unsupported.size()).error ==
             norman_rf::ValidationError::unsupported_length,
         "an unproven frame length should be rejected");

  expect(norman_rf::validate_frame(kSyntheticFrame.data(),
                                   kSyntheticFrame.size() - 1U)
             .error == norman_rf::ValidationError::wrong_size,
         "a truncated radio payload should be rejected");
  expect(norman_rf::validate_frame(nullptr, norman_rf::kFrameSize).error ==
             norman_rf::ValidationError::wrong_size,
         "a null frame should be rejected");
}

void test_rolling_mapping_round_trips() {
  expect(norman_rf::encode_rolling_index(0x00) == 0xdd,
         "rolling index 0 should map to 0xdd");
  expect(norman_rf::encode_rolling_index(0x01) == 0x5d,
         "rolling index 1 should map to 0x5d");
  expect(norman_rf::encode_rolling_index(0x0f) == 0x2d,
         "rolling index 15 should map to 0x2d");
  expect(norman_rf::encode_rolling_index(0x10) == 0xd5,
         "rolling index 16 should map to 0xd5");
  expect(norman_rf::encode_rolling_index(0xff) == 0x22,
         "rolling index 255 should map to 0x22");

  for (std::uint16_t index = 0; index <= 0xff; ++index) {
    const auto value = static_cast<std::uint8_t>(index);
    const auto encoded = norman_rf::encode_rolling_index(value);
    expect(norman_rf::decode_rolling_code(encoded) == value,
           "every rolling code should invert to its original index");
  }
}

void test_transmit_boundary() {
  for (const int channel : {15, 39, 59}) {
    expect(norman_rf::valid_tx_request(kSyntheticFrame.data(), 30, channel, 1),
           "one supported-channel CRC-valid frame is allowed");
    expect(norman_rf::valid_tx_request(kSyntheticFrame.data(), 30, channel, 100),
           "bounded maximum copies is allowed");
  }
  for (const int channel : {-1, 0, 14, 16, 40, 60, 125, 256}) {
    expect(!norman_rf::valid_tx_request(kSyntheticFrame.data(), 30, channel, 1),
           "unsupported channel is rejected");
  }
  for (const int copies : {-1, 0, 101, 10000}) {
    expect(!norman_rf::valid_tx_request(kSyntheticFrame.data(), 30, 15, copies),
           "unbounded or empty burst is rejected");
  }
  auto corrupt = kSyntheticFrame;
  corrupt[4] ^= 1;
  expect(!norman_rf::valid_tx_request(corrupt.data(), 30, 15, 10), "bad CRC rejected for TX");
  expect(!norman_rf::valid_tx_request(nullptr, 30, 15, 10), "null rejected for TX");
  expect(!norman_rf::valid_tx_request(kSyntheticFrame.data(), 29, 15, 10), "short frame rejected for TX");
}

void test_learned_profile() {
  norman_rf::LearnedProfile profile;
  profile.open = kSyntheticFrame;
  profile.close = kSyntheticFrame;
  profile.close[4] ^= 0x80;
  auto crc = norman_rf::crc16(profile.close.data(), 28);
  profile.close[28] = static_cast<uint8_t>(crc >> 8);
  profile.close[29] = static_cast<uint8_t>(crc);
  expect(norman_rf::valid_profile(profile), "matching learned pair validates");
  profile.last_index = 255;
  const auto command = norman_rf::next_command(profile, 37);
  expect(command[24] == 0 && command[25] == 0xdd, "counter wraps once without reuse");
  expect(norman_rf::validate_frame(command.data(), command.size()).valid(), "generated CRC validates");
  expect(norman_rf::same_command_family(command, profile.open), "rolling fields retain identity");
  auto different = profile.close;
  different[21] ^= 1;
  crc = norman_rf::crc16(different.data(), 28);
  different[28] = static_cast<uint8_t>(crc >> 8);
  different[29] = static_cast<uint8_t>(crc);
  expect(!norman_rf::same_command_family(different, profile.open), "other panel selector rejected");
  expect(norman_rf::command_template(profile, 100) == nullptr, "unlearned upward close rejected");
  expect(norman_rf::command_template(profile, 50) == nullptr, "unlearned intermediate position rejected");
  expect(norman_rf::learned_command(profile, command), "known opcode allowed");
  profile.close_up = profile.close;
  profile.close_up[2] ^= 0x0a;
  profile.close_up[3] ^= 0x80;
  crc = norman_rf::crc16(profile.close_up.data(), 28);
  profile.close_up[28] = static_cast<uint8_t>(crc >> 8);
  profile.close_up[29] = static_cast<uint8_t>(crc);
  expect(!norman_rf::learned_command(profile, profile.close_up), "uncommissioned direction not allowed");
  profile.has_close_up = 1;
  expect(norman_rf::valid_profile(profile), "multi-byte upward close can be explicitly commissioned");
  expect(norman_rf::learned_command(profile, profile.close_up), "exact upward-close template allowed");
  auto unlearned = profile.close_up;
  unlearned[2] ^= 1;
  crc = norman_rf::crc16(unlearned.data(), 28);
  unlearned[28] = static_cast<uint8_t>(crc >> 8);
  unlearned[29] = static_cast<uint8_t>(crc);
  expect(!norman_rf::learned_command(profile, unlearned), "same family is not an arbitrary command permit");
  expect(norman_rf::learned_command(profile, norman_rf::next_command(profile, 100)), "rolling upward-close remains exact");
  profile.close_up = profile.close;
  expect(!norman_rf::valid_profile(profile), "duplicate close direction rejected");
  profile.has_close_up = 0;
  profile.version = 3;
  expect(!norman_rf::valid_profile(profile), "unknown persistent format rejected");
  profile.version = 2;
  profile.close[5] ^= 1;
  expect(!norman_rf::valid_profile(profile), "corrupt saved profile rejected");
}

void test_relay_cache() {
  norman_rf::RelayCache cache;
  expect(cache.admit(kSyntheticFrame, 0) == norman_rf::RelayDecision::eligible, "first frame eligible");
  expect(cache.admit(kSyntheticFrame, 59000) == norman_rf::RelayDecision::duplicate, "echo suppressed");
  expect(cache.admit(kSyntheticFrame, 118000) == norman_rf::RelayDecision::duplicate, "echo extends suppression");
  for (uint8_t i = 1; i < 64; ++i) {
    auto frame = kSyntheticFrame;
    frame[24] = static_cast<uint8_t>(kSyntheticFrame[24] + i);
    expect(cache.admit(frame, 118000) == norman_rf::RelayDecision::eligible, "bounded cache fill");
  }
  auto extra = kSyntheticFrame;
  extra[24] = 99;
  expect(cache.admit(extra, 118001) == norman_rf::RelayDecision::full, "overflow cannot evict live identity");
  expect(cache.admit(extra, 178001) == norman_rf::RelayDecision::eligible, "expired entry reusable");
  norman_rf::RelayCache wrap;
  expect(wrap.admit(extra, 0xfffffff0U) == norman_rf::RelayDecision::eligible, "clock wrap seed");
  expect(wrap.admit(extra, 100U) == norman_rf::RelayDecision::duplicate, "clock wrap duplicate");
}

void test_forwarding_path() {
  expect(norman_rf::relay_output_channel(15) == 39, "direct ESP/hub traffic has a first hop");
  expect(norman_rf::relay_output_channel(39) == 59, "original repeater traffic retains final hop");
  for (int channel = -1; channel <= 126; ++channel) {
    if (channel == 15 || channel == 39) continue;
    expect(norman_rf::relay_output_channel(channel) == -1, "unsupported/terminal channel never forwarded");
  }
  int channel = 15;
  for (int hop = 0; hop < 3; ++hop) channel = norman_rf::relay_output_channel(channel);
  expect(channel == -1, "chain terminates after two hops");
  norman_rf::RelayCache source, first, second;
  expect(source.admit(kSyntheticFrame, 0) == norman_rf::RelayDecision::eligible, "origin reserves its own frame");
  expect(first.admit(kSyntheticFrame, 10) == norman_rf::RelayDecision::eligible, "first bridge forwards unchanged frame");
  expect(second.admit(kSyntheticFrame, 20) == norman_rf::RelayDecision::eligible, "second bridge forwards unchanged frame");
  expect(source.admit(kSyntheticFrame, 30) == norman_rf::RelayDecision::duplicate, "origin suppresses returning echo");
  expect(first.admit(kSyntheticFrame, 40) == norman_rf::RelayDecision::duplicate, "cross-channel echo suppressed");
}

}  // namespace

int main() {
  test_known_crc_vector_and_synthetic_frame_validate();
  test_invalid_frames_are_rejected();
  test_rolling_mapping_round_trips();
  test_transmit_boundary();
  test_learned_profile();
  test_relay_cache();
  test_forwarding_path();
  if (failures != 0) {
    std::cerr << failures << " protocol test(s) failed\n";
    return 1;
  }
  std::cout << "All Norman RF protocol tests passed\n";
  return 0;
}
