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
  s.observe(frame(0x38, 0), 8002);
  s.observe(frame(0x38, 255), 8003);
  check(s.unique() == 2 && s.accept(token, 8004), "wrap accepted, late echo ignored");
  check(!s.ready(8004), "open alone cannot control panel");
  check(!s.capture(token, 2, 8005), "opposite close cannot precede preferred close");
  check(s.capture(token, 1, 9000), "close capture");
  s.observe(frame(0xf8, 1), 9001); s.observe(frame(0xf8, 2), 17001);
  check(s.accept(token, 17002) && s.ready(17002), "complete pair ready");
  s.observe(frame(0x38, 3), 17003);
  check(s.last_index() == 3, "observe fresh family counter while awaiting save");
  check(s.capture(token, 2, 18000), "optional other close direction");
  s.observe(frame(0xe8, 4), 18001); s.observe(frame(0xe8, 5), 26001);
  check(s.accept(token, 26002) && s.has_opposite(), "opposite endpoint ready");
  LearnedPanel p; p.setup(0);
  check(p.configure_target(s.frame(0), s.frame(1), s.last_index(), 37, 100,
                          "Bottom left", "Office", &s.frame(2)), "all endpoints persist in one record");
  LearnedPanel rebooted; rebooted.setup(0);
  check(rebooted.ready() && rebooted.supports(0) && rebooted.supports(100) && rebooted.last_index() == 5,
        "learned pair and opposite survive reboot");
  s.saved(0); check(!s.active(26003) && s.saved_slot() == 0, "idempotent saved receipt");
  check(s.begin(token, true, 27000) && s.capture(token, 0, 27001), "relay-only session");
  s.observe(frame(0x11, 200), 27002); s.observe(frame(0x11, 33), 35002);
  check(s.accept(token, 35003) && s.ready(35003), "relay does not assume native room sequence model");
  check(!s.capture(token, 1, 35004), "relay action never becomes direct panel");
  s.cancel(); check(!s.ready(35005), "cancel grants no authority");
  s = LearningSession{};  // Independent negative cases use a fresh clock origin.
  s.begin(token, false, 30); s.capture(token, 0, 31);
  s.observe(frame(0x38, 1), 32); s.observe(frame(0x38, 2, 1), 33);
  check(!s.accept(token, 34) && s.error() == "mixed_actions", "mixed panel traffic refused");
  check(s.capture(token, 0, 35), "explicit retry clears ambiguous window");
  auto bad = frame(0x38, 1); bad[10] ^= 1; s.observe(bad, 36);
  check(s.unique() == 0, "bad CRC rejected");
  check(!s.accept(token, 37) && s.error() == "need_two_presses", "empty capture rejected");
  s.capture(token, 0, 40); s.observe(frame(0x38, 1), 41); s.observe(frame(0x38, 2), 8041);
  check(!s.accept(token, 60040) && s.error() == "capture_expired", "capture has finite lifetime");
  check(!s.active(600030) && !s.ready(600030) && !s.capturing(), "abandoned flow releases radio");
  LearningSession boot;
  check(!boot.owns(token) && !boot.ready(0), "reboot drops uncommitted session");

  LearningSession diagnostic;
  diagnostic.begin(token, false, 0); diagnostic.capture(token, 0, 1);
  check(!diagnostic.accept(token, 2) && diagnostic.error() == "need_two_presses",
        "early acceptance caches a diagnostic error");
  check(!diagnostic.capture_expired(60000) && diagnostic.capture_expired(60001),
        "capture expiry boundary remains observable despite cached error");
  check(diagnostic.error() == "need_two_presses",
        "expiry projection does not mutate the learner's validation error");
  diagnostic.cancel();
  check(!diagnostic.capture_expired(60002), "cancel clears capture-expired projection");

  // Hub presses can jump outside the forward half range. Timing and distinct
  // samples, not the assumed ordering of their codes, establish this capture.
  LearningSession hub;
  check(hub.begin(token, false, 0) && hub.capture(token, 0, 1), "hub capture begins");
  hub.observe(frame(0x38, 221), 100);
  hub.observe(frame(0x38, 111), 8099);
  check(hub.unique() == 1, "less than eight seconds cannot count twice");
  hub.observe(frame(0x38, 221), 8100);
  check(hub.unique() == 1, "late identical echo cannot count twice");
  hub.observe(frame(0x38, 111), 8100);
  check(hub.unique() == 1, "early variant cannot become independent evidence as a late echo");
  auto same_code = frame(0x38, 221);
  same_code[24] ^= 1;
  const auto same_code_crc = norman_rf::crc16(same_code.data(), 28);
  same_code[28] = uint8_t(same_code_crc >> 8); same_code[29] = uint8_t(same_code_crc);
  hub.observe(same_code, 8100);
  check(hub.unique() == 1, "same code with changed timestamp cannot count twice");
  hub.observe(frame(0x38, 110), 8100);
  check(hub.unique() == 2, "nonmonotonic hub pair accepted at eight-second boundary");
  hub.observe(frame(0xe8, 115, 7), 9000);
  check(hub.accept(token, 9002) && hub.frame(0) == frame(0x38, 110),
        "candidate evidence freezes without rejecting a foreign one-off");
  check(hub.capture(token, 1, 10000), "close uses established target identity");
  hub.observe(frame(0xf8, 7, 1), 10001);
  hub.observe(frame(0xf8, 8, 1), 18001);
  check(hub.unique() == 0 && !hub.accept(token, 18002),
        "two actions for wrong panel cannot provide Close evidence");
  check(hub.capture(token, 1, 19000), "retry close after wrong target");
  hub.observe(frame(0xf8, 220), 19001);
  hub.observe(frame(0x38, 2, 4), 20000);
  hub.observe(frame(0xf8, 109), 27001);
  check(hub.accept(token, 27002) && hub.ready(27002) && hub.last_index() == 109,
        "foreign traffic excluded from locked close, last distinct code retained");
  check(hub.capture(token, 1, 28000), "explicit recapture invalidates old close");
  hub.observe(frame(0xf8, 10), 28001); hub.observe(frame(0xe8, 11), 36001);
  check(!hub.accept(token, 36002) && hub.error() == "mixed_actions",
        "conflicting actions for selected panel remain rejected");
  check(hub.capture(token, 1, 37000), "retry endpoint after conflict");
  hub.observe(frame(0x38, 12), 37001); hub.observe(frame(0x38, 13), 45001);
  check(!hub.accept(token, 45002) && hub.error() == "wrong_endpoint",
        "Open cannot masquerade as Close for selected identity");
  check(hub.capture(token, 0, 46000), "relearning Open clears previous identity lock");
  hub.observe(frame(0x38, 14), 46001); hub.observe(frame(0x38, 15, 1), 54001);
  check(!hub.accept(token, 54002) && hub.error() == "mixed_actions" && !hub.ready(54002),
        "initial Open still rejects mixed identities");

  LearningSession wrapped;
  const uint32_t near_wrap = UINT32_MAX - 4000;
  wrapped.begin(token, false, near_wrap); wrapped.capture(token, 0, near_wrap);
  wrapped.observe(frame(0x38, 200), near_wrap);
  wrapped.observe(frame(0x38, 20), uint32_t(near_wrap + 8000));
  check(wrapped.accept(token, uint32_t(near_wrap + 8001)), "sample gap handles millis wrap");

  // A foreign one-off may arrive before either requested sample. Candidates
  // must qualify independently; arrival order and packet counts grant nothing.
  for (bool foreign_first : {false, true}) {
    LearningSession crowded;
    crowded.begin(token, false, 0); crowded.capture(token, 0, 1);
    if (foreign_first) crowded.observe(frame(0xf8, 17, 1), 10);
    crowded.observe(frame(0x38, 221), 100);
    for (int i = 0; i < 200; ++i) crowded.observe(frame(0xf8, 17, 1), 200 + i);
    crowded.observe(frame(0x38, 110), 12100);
    check(crowded.accept(token, 12101) && crowded.frame(0) == frame(0x38, 110),
          "unique qualifying target wins over noisy foreign one-off, in either order");
  }
  LearningSession ambiguous;
  ambiguous.begin(token, false, 0); ambiguous.capture(token, 0, 1);
  ambiguous.observe(frame(0x38, 1), 100);
  ambiguous.observe(frame(0x38, 2, 1), 200);
  ambiguous.observe(frame(0x38, 3), 8100);
  ambiguous.observe(frame(0x38, 4, 1), 8200);
  check(!ambiguous.accept(token, 8201) && ambiguous.error() == "mixed_actions",
        "second qualifying family invalidates first even after it completed");
  check(ambiguous.capture(token, 0, 9000), "retry clears candidate pool");
  ambiguous.observe(frame(0x38, 1), 9001);
  ambiguous.observe(frame(0x38, 2), 17001);
  ambiguous.observe(frame(0xf8, 3), 17002);
  check(!ambiguous.accept(token, 17003) && ambiguous.error() == "mixed_actions",
        "single conflicting action for qualifying target still rejects capture");
  check(ambiguous.capture(token, 0, 18000), "new bounded pool");
  for (int i = 0; i < 9; ++i) ambiguous.observe(frame(0x38, 1, i), 18001 + i);
  ambiguous.observe(frame(0x38, 2), 26001);
  check(!ambiguous.accept(token, 26002) && ambiguous.error() == "mixed_actions",
        "pool overflow fails closed rather than evicting conflicting evidence");

  LearningSession burst;
  burst.begin(token, false, 0); burst.capture(token, 0, 1);
  for (int i = 0; i < 3; ++i) burst.observe(frame(0x38, uint8_t(10 + i)), 100 + i * 2000);
  for (int i = 0; i < 3; ++i) burst.observe(frame(0x38, uint8_t(10 + i)), 10000 + i * 2000);
  check(burst.unique() == 1 && !burst.accept(token, 16000),
        "three-code burst replay cannot impersonate a second controller action");
  burst.capture(token, 0, 17000);
  for (int i = 0; i < 17; ++i) burst.observe(frame(0x38, uint8_t(i)), 17001 + i);
  check(!burst.accept(token, 18000) && burst.error() == "mixed_actions",
        "sample history overflow fails closed");

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
