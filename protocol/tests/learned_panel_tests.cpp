#include "learned_panel.h"
#include <array>
#include <iostream>

using esphome::norman_rf_monitor::LearnedPanel;
int failures = 0;
void check(bool result, const char *label) {
  if (!result) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
void crc(norman_rf::Frame &frame) {
  auto value = norman_rf::crc16(frame.data(), 28);
  frame[28] = static_cast<uint8_t>(value >> 8);
  frame[29] = static_cast<uint8_t>(value);
}
int main() {
  norman_rf::Frame open{};
  open[0] = 24; open[4] = 0x38; crc(open);
  auto close = open; close[4] = 0xf8; crc(close);
  std::array<int32_t,30> open_ints{}, close_ints{};
  std::copy(open.begin(), open.end(), open_ints.begin());
  std::copy(close.begin(), close.end(), close_ints.begin());
  LearnedPanel panel;
  panel.setup();
  check(!panel.ready() && !panel.commit_transmit(open), "factory state cannot send");
  check(panel.configure(open_ints.data(),30,close_ints.data(),30,255,37), "commission persisted");
  check(!panel.relay_enabled(), "relay factory off");
  check(panel.set_relay(true), "explicit relay commission");
  LearnedPanel rebooted;
  rebooted.setup();
  check(rebooted.ready() && rebooted.relay_enabled(), "USB reboot restores commissioned relay");
  auto command = rebooted.next(37);
  check(command[24] == 0 && rebooted.commit_transmit(command), "durable wrap reservation");
  check(!rebooted.commit_transmit(command), "same index cannot send twice");
  LearnedPanel after_command;
  after_command.setup();
  check(after_command.last_index() == 0, "counter survives reboot before RF");
  check(!after_command.supports(100) && !after_command.supports(50), "unlearned positions unavailable");
  auto alien = command; alien[21] ^= 1; crc(alien);
  check(!after_command.accepts(alien), "other target not allowed");
  auto advance = after_command.next(0);
  check(after_command.observe(advance), "observed forward sequence accepted");
  check(!after_command.observe(command), "old echo cannot roll counter back");
  check(after_command.persist(), "observed counter persisted");
  const auto identity = after_command.profile_id();
  auto close_up = close;
  close_up[2] = 0x0a; close_up[3] = 0x80;
  close_up[25] = norman_rf::encode_rolling_index(10); crc(close_up);
  std::array<int32_t,30> up_ints{};
  std::copy(close_up.begin(), close_up.end(), up_ints.begin());
  check(after_command.configure_close_up(up_ints.data(), 30), "captured multibyte direction persists");
  LearnedPanel with_up;
  with_up.setup();
  check(with_up.ready() && with_up.supports(100) && with_up.relay_enabled(), "all endpoints and relay survive reset");
  check(with_up.last_index() == 10, "fresh manual direction capture advances sequence");
  check(with_up.profile_id() == identity, "optional capability and sequence retain panel fingerprint");
  check(with_up.commit_transmit(with_up.next(100)), "upward-close command commits durably");
  esphome::fail_sync = true;
  check(!after_command.commit_transmit(after_command.next(37)), "storage failure blocks RF");
  check(!after_command.ready() && !after_command.commit_transmit(advance), "storage fault has no raw bypass");
  esphome::fail_sync = false;
  esphome::persisted[0x4e540100U][0] ^= 1;
  LearnedPanel corrupt;
  corrupt.setup();
  check(!corrupt.ready() && !corrupt.relay_enabled(), "corrupt state fails closed");
  check(!corrupt.configure(open_ints.data(),30,close_ints.data(),30,5,37), "corrupt target cannot silently recommission");

  esphome::persisted.clear(); esphome::pending.clear();
  norman_rf::LearnedProfile legacy;
  legacy.open = open; legacy.close = close; legacy.close_up = close_up;
  legacy.last_index = 150; legacy.has_close_up = 1; legacy.relay_enabled = 1;
  auto legacy_pref = esphome::preferences.make_preference<norman_rf::LearnedProfile>(0x4e524632U, true);
  legacy_pref.save(&legacy); esphome::preferences.sync();
  LearnedPanel migrated;
  migrated.setup();
  check(migrated.ready() && migrated.last_index() == 150 && migrated.supports(100) && migrated.relay_enabled(), "live 95-byte profile migrates without losing endpoints or counter");
  auto old_bytes = esphome::persisted[0x4e524632U];
  check(migrated.configure_target(open,close,1,37,0,"Bedroom lower left","Bedroom"), "migrated target can be labelled");
  check(migrated.last_index() == 150 && migrated.supports(100), "labelling never resets counter or optional endpoint");
  check(esphome::persisted[0x4e524632U] == old_bytes, "legacy backup untouched");
  for (uint8_t slot=1; slot<32; ++slot) {
    LearnedPanel target; target.setup(slot);
    auto o = open; o[21] = slot; crc(o);
    auto c = close; c[21] = slot; crc(c);
    check(!target.ready(), "new slot starts empty");
    check(target.configure_target(o,c,slot,37,100,"Panel","Room"), "independent upward-close target persists");
    check(target.supports(100) && !target.supports(0), "only actually learned physical direction exposed");
    check(target.next(100)[4] == c[4], "primary upward close selects exact template");
    check(target.commit_transmit(target.next(37)), "target counter reserved");
    LearnedPanel restored; restored.setup(slot);
    check(restored.last_index() == slot+1 && restored.name() == "Panel" && restored.room() == "Room", "metadata and independent counter survive reset");
  }
  LearnedPanel zero; zero.setup();
  check(zero.last_index() == 150 && zero.name() == "Bedroom lower left", "other31 targets never alter pilot");
  esphome::persisted.erase(0x4e540100U);
  LearnedPanel lost; lost.setup();
  check(!lost.ready(), "lost new state never falls back to stale legacy counter");
  norman_rf::Frame parsed{};
  check(!norman_rf::parse_frame_hex("xx", parsed), "bad hex rejected");
  check(!norman_rf::parse_frame_hex(std::string(60,'g'), parsed), "nonhex rejected");
  check(!norman_rf::parse_frame_hex(std::string(60,'0'), parsed), "invalid frame rejected");
  if (failures) return 1;
  std::cout << "Learned panel persistence and failure tests passed\n";
}
