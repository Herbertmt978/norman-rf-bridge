#include "learning_session.h"
#include "learned_panel.h"
#include "learned_relay_endpoint.h"
#include <iostream>

using namespace esphome::norman_rf_monitor;
int failures = 0;
void check(bool value, const char *message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
norman_rf::Frame frame(int action, uint8_t index, int panel = 0) {
  norman_rf::Frame f{}; f[0] = 24; f[4] = uint8_t(action); f[21] = uint8_t(panel);
  f[24] = index; f[25] = norman_rf::encode_rolling_index(index);
  const auto crc = norman_rf::crc16(f.data(), 28);
  f[28] = uint8_t(crc >> 8); f[29] = uint8_t(crc);
  return f;
}
int main() {
  const std::string token(32, 'a'), other(32, 'b');
  LearningSession s;
  check(!s.begin("short", false, 0), "token validated");
  check(s.begin(token, false, 0), "start new panel");
  check(!s.begin(other, false, 1), "second owner cannot steal session");
  check(!s.capture(other, 0, 1), "wrong session rejected");
  check(!s.capture(token, 1, 1), "close cannot precede open");
  check(s.capture(token, 0, 1), "open capture");
  s.observe(frame(0x38, 255), 2);
  for (int i = 0; i < 100; ++i) s.observe(frame(0x38, 255), 3);
  check(s.unique() == 1, "burst copies are not independent presses");
  s.observe(frame(0x38, 0), 4);
  s.observe(frame(0x38, 255), 5);
  check(s.unique() == 2 && s.accept(token, 6), "wrap accepted, late echo ignored");
  check(!s.ready(6), "open alone cannot control panel");
  check(!s.capture(token, 2, 6), "opposite close cannot precede preferred close");
  check(s.capture(token, 1, 7), "close capture");
  s.observe(frame(0xf8, 1), 8); s.observe(frame(0xf8, 2), 9);
  check(s.accept(token, 10) && s.ready(10), "complete pair ready");
  s.observe(frame(0x38, 3), 11);
  check(s.last_index() == 3, "observe fresh family counter while awaiting save");
  check(s.capture(token, 2, 12), "optional other close direction");
  s.observe(frame(0xe8, 4), 13); s.observe(frame(0xe8, 5), 14);
  check(s.accept(token, 15) && s.has_opposite(), "opposite endpoint ready");
  LearnedPanel p; p.setup(0);
  check(p.configure_target(s.frame(0), s.frame(1), s.last_index(), 37, 100,
                          "Bottom left", "Office", &s.frame(2)), "all endpoints persist in one record");
  LearnedPanel rebooted; rebooted.setup(0);
  check(rebooted.ready() && rebooted.supports(0) && rebooted.supports(100) && rebooted.last_index() == 5,
        "learned pair and opposite survive reboot");
  s.saved(0); check(!s.active(16) && s.saved_slot() == 0, "idempotent saved receipt");
  check(s.begin(token, true, 20) && s.capture(token, 0, 21), "relay-only session");
  s.observe(frame(0x11, 200), 22); s.observe(frame(0x11, 33), 23);
  check(s.accept(token, 24) && s.ready(24), "relay does not assume native room sequence model");
  check(!s.capture(token, 1, 25), "relay action never becomes direct panel");
  s.cancel(); check(!s.ready(26), "cancel grants no authority");
  s.begin(token, false, 30); s.capture(token, 0, 31);
  s.observe(frame(0x38, 1), 32); s.observe(frame(0x38, 2, 1), 33);
  check(!s.accept(token, 34) && s.error() == "mixed_actions", "mixed panel traffic refused");
  check(s.capture(token, 0, 35), "explicit retry clears ambiguous window");
  auto bad = frame(0x38, 1); bad[10] ^= 1; s.observe(bad, 36);
  check(s.unique() == 0, "bad CRC rejected");
  check(!s.accept(token, 37) && s.error() == "need_two_presses", "empty capture rejected");
  s.capture(token, 0, 40); s.observe(frame(0x38, 1), 41); s.observe(frame(0x38, 2), 42);
  check(!s.accept(token, 60040) && s.error() == "capture_expired", "capture has finite lifetime");
  check(!s.active(600030) && !s.ready(600030) && !s.capturing(), "abandoned flow releases radio");
  LearningSession boot;
  check(!boot.owns(token) && !boot.ready(0), "reboot drops uncommitted session");
  const auto identity = rebooted.profile_id();
  check(!rebooted.remove("wrong"), "stale removal identity rejected");
  check(rebooted.rename(identity, "New name", "Other room") && rebooted.profile_id() == identity &&
        rebooted.last_index() == 5 && rebooted.supports(0), "rename preserves identity, sequence and optional endpoint");
  check(rebooted.remove(identity) && !rebooted.ready(), "remove selected panel only");
  LearnedPanel deleted; deleted.setup(0);
  check(!deleted.ready() && deleted.remove(identity), "removal is durable and idempotent");
  check(deleted.configure_target(frame(0x38, 10, 2), frame(0xf8, 11, 2), 11, 37, 100, "Replacement", "Office"),
        "removed slot reusable without reviving legacy counter");
  check(deleted.profile_id() != identity && !deleted.remove(identity), "stale delete cannot remove replacement");
  LearnedRelayEndpoint endpoint; endpoint.setup(0);
  check(endpoint.configure(frame(0x11, 10), "Room Open"), "relay fixture");
  const auto relay_id = endpoint.profile_id();
  check(endpoint.rename(relay_id, "Office Open") && endpoint.profile_id() == relay_id, "relay rename stable");
  check(!endpoint.remove("wrong") && endpoint.remove(relay_id), "relay removal exact identity");
  LearnedRelayEndpoint empty; empty.setup(0);
  check(!empty.ready() && empty.remove(relay_id), "relay tombstone survives reboot");
  check(empty.configure(frame(0x22, 20), "Other action"), "relay slot reusable");
  esphome::fail_save = true;
  check(!empty.remove(empty.profile_id()) && !empty.ready(), "failed delete fails closed");
  esphome::fail_save = false;
  return failures ? 1 : 0;
}
