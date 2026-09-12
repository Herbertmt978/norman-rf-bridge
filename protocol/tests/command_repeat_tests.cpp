#include "command_repeat.h"
#include <iostream>

using namespace esphome::norman_rf_monitor;
int failures = 0;
void check(bool value, const char *message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void crc(norman_rf::Frame &frame) {
  const auto value = norman_rf::crc16(frame.data(), 28);
  frame[28] = static_cast<uint8_t>(value >> 8); frame[29] = static_cast<uint8_t>(value);
}
int main() {
  std::array<LearnedPanel, 32> panels{};
  std::vector<int32_t> slots{0, 1}, positions{100, 100};
  std::vector<std::string> ids;
  for (uint8_t i = 0; i < 2; ++i) {
    panels[i].setup(i);
    norman_rf::Frame open{}; open[0] = 24; open[21] = i; crc(open);
    auto close = open; close[4] = 0xf8; crc(close);
    check(panels[i].configure_target(open, close, 255, 37, 100, "Panel", "Room"), "synthetic target");
    ids.push_back(panels[i].profile_id());
  }
  CommandRepeat repeat;
  BatchFrames original{}, output{};
  check(!repeat.take(slots, positions, ids, panels, 0, output), "boot never has repeat authority");
  check(prepare_target_batch(panels, slots, positions, ids, original), "reserve original batch once");
  const auto durable_before = esphome::persisted;
  const auto pending_before = esphome::pending;
  repeat.remember(slots, positions, ids, original, panels, 1000);
  check(!repeat.take({0}, {100}, {ids[0]}, panels, 1100, output), "subset rejected");
  check(!repeat.take({1,0}, positions, {ids[1],ids[0]}, panels, 1100, output), "order change rejected");
  check(!repeat.take(slots, {37,100}, ids, panels, 1100, output), "opposite command rejected");
  check(!repeat.take(slots, positions, {ids[0],"wrong"}, panels, 1100, output), "wrong identity rejected");
  repeat.observe(original[0]);
  auto unrelated = original[0]; unrelated[21] = 20; crc(unrelated); repeat.observe(unrelated);
  check(repeat.take(slots, positions, ids, panels, 2000, output), "first identical repeat accepted after own echo");
  check(output == original, "all thirty bytes including code and CRC unchanged");
  check(repeat.take(slots, positions, ids, panels, 3000, output), "second identical repeat accepted");
  check(!repeat.take(slots, positions, ids, panels, 4000, output), "third repeat refused");
  check(panels[0].last_index() == 0 && panels[1].last_index() == 0, "no new rolling reservation");
  check(esphome::persisted == durable_before && esphome::pending == pending_before,
        "repeat leaves durable and pending storage unchanged");
  repeat.remember(slots, positions, ids, original, panels, 1000);
  check(!repeat.take(slots, positions, ids, panels, 61000, output), "sixty second expiry");
  repeat.remember(slots, positions, ids, original, panels, UINT32_MAX - 2000);
  check(repeat.take(slots, positions, ids, panels, 500, output), "millis wrap supported");
  repeat.clear();
  check(!repeat.take(slots, positions, ids, panels, 600, output), "explicit clear invalidates");
  repeat.remember(slots, positions, ids, original, panels, 1000);
  repeat.observe(panels[0].next(37));
  check(!repeat.take(slots, positions, ids, panels, 2000, output), "conflicting observed endpoint cancels whole batch");
  repeat.remember(slots, positions, ids, original, panels, 1000);
  repeat.observe(panels[1].next(100));
  check(!repeat.take(slots, positions, ids, panels, 2000, output), "different observed code cancels same endpoint");

  // Deterministic background scheduling, sharing the manual cache and budget.
  size_t count = 0;
  CommandRepeat automatic;
  check(!automatic.take_due(panels, 21000, true, output, count), "boot has no automatic work");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  check(automatic.remaining(1000) == 2, "two repeats scheduled by original command");
  check(!automatic.take_due(panels, 20999, true, output, count), "not before twenty seconds");
  check(!automatic.take_due(panels, 21000, false, output, count), "busy or unsuccessful radio cannot repeat");
  check(automatic.take_due(panels, 21000, true, output, count), "first automatic repeat due");
  check(count == 2 && output == original, "automatic whole-batch bytes unchanged");
  check(!automatic.take_due(panels, 40999, true, output, count), "no compressed second burst");
  check(automatic.take_due(panels, 41000, true, output, count), "second automatic repeat due");
  check(automatic.remaining(41000) == 0, "automatic budget exhausted");
  check(!automatic.take(slots, positions, ids, panels, 42000, output), "manual cannot exceed automatic budget");
  check(!automatic.take_due(panels, 61000, true, output, count), "no third scheduled repeat");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  check(automatic.take(slots, positions, ids, panels, 15000, output), "manual uses shared budget");
  check(!automatic.take_due(panels, 34999, true, output, count), "manual postpones next automatic burst");
  check(automatic.take_due(panels, 35000, true, output, count), "automatic uses final shared allowance");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  check(!automatic.take_due(panels, 39000, false, output, count), "busy radio postpones without consuming");
  check(automatic.take_due(panels, 40000, true, output, count), "delayed first burst accepted");
  check(automatic.take_due(panels, 60000, true, output, count), "twenty seconds after delayed burst");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  check(!automatic.take_due(panels, 61000, false, output, count), "expiry clears even while ineligible");
  check(!automatic.take_due(panels, 21000, true, output, count), "expired work cannot revive on clock wrap");
  automatic.remember(slots, positions, ids, original, panels, UINT32_MAX - 10000);
  check(!automatic.take_due(panels, 9998, true, output, count), "automatic wrap delay boundary");
  check(automatic.take_due(panels, 9999, true, output, count), "automatic millis wrap supported");
  automatic.clear();
  check(!automatic.take_due(panels, 29999, true, output, count), "new command, disable or fault cancels pending work");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  automatic.clear();  // Policy setter also clears a manual-only cached command on enable.
  check(!automatic.take_due(panels, 21000, true, output, count), "enabling policy cannot revive an off-period command");
  automatic.remember(slots, positions, ids, original, panels, 1000);
  automatic.observe(panels[0].next(37));
  check(!automatic.take_due(panels, 21000, true, output, count), "observed conflicting command cancels automatic work");
  check(esphome::persisted == durable_before && esphome::pending == pending_before,
        "automatic and manual repeats never write learned counters");

  repeat.remember(slots, positions, ids, original, panels, 1000);
  check(panels[0].commit_transmit(panels[0].next(100)), "new command changes rolling state");
  check(!repeat.take(slots, positions, ids, panels, 2000, output), "counter advance independently prevents stale replay");
  CommandRepeat rebooted;
  check(!rebooted.take(slots, positions, ids, panels, 2000, output), "reboot cannot replay cached command");
  return failures ? 1 : 0;
}
