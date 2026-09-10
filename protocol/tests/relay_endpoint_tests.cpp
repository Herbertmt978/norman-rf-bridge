#include "received_command.h"
#include <iostream>

using namespace esphome::norman_rf_monitor;
int failures = 0;
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void crc(norman_rf::Frame &f) {
  const auto c = norman_rf::crc16(f.data(), 28);
  f[28] = static_cast<uint8_t>(c >> 8);
  f[29] = static_cast<uint8_t>(c);
}
norman_rf::Frame synthetic() {
  norman_rf::Frame f{};
  f[0] = 24; f[2] = 0x57; f[4] = 0xf8; f[10] = 7; f[21] = 0xd8;
  crc(f);
  return f;
}
void reset_storage() {
  esphome::persisted.clear(); esphome::pending.clear();
  esphome::fail_save = false; esphome::fail_sync = false;
  esphome::syncs_before_failure = -1;
}

int main() {
  reset_storage();
  const auto room = synthetic();
  LearnedRelayEndpoint endpoint;
  check(!endpoint.configure(room, "Room open"), "setup required before commissioning");
  endpoint.setup(0);
  check(!endpoint.ready() && !endpoint.accepts(room), "factory default cannot authorize RF");
  check(!endpoint.configure(room, ""), "empty label rejected");
  check(!endpoint.configure(room, std::string(48, 'x')), "oversize label rejected");
  check(!endpoint.configure(room, "bad\nname"), "control character rejected");
  check(endpoint.configure(room, "Room open"), "explicit endpoint learned durably");
  const auto saved = esphome::persisted;
  for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b) {
    auto received = room;
    received[24] = static_cast<uint8_t>(a); received[25] = static_cast<uint8_t>(b); crc(received);
    const auto before = received;
    check(endpoint.accepts(received), "arbitrary received sequence accepted without invented counter rule");
    check(received == before, "received frame remains byte-for-byte unchanged");
  }
  check(esphome::persisted == saved && esphome::pending.empty(), "receive matching performs no NVS write");
  for (size_t i = 0; i < 30; ++i) for (int bit = 0; bit < 8; ++bit) {
    auto damaged = room; damaged[i] ^= static_cast<uint8_t>(1U << bit);
    check(!endpoint.accepts(damaged), "every single-bit corrupt frame rejected");
  }
  for (size_t i = 0; i < 28; ++i) {
    if (i == 24 || i == 25) continue;
    auto other = room; other[i] ^= 1; crc(other);
    check(!endpoint.accepts(other), "other command/address/selector rejected even with valid CRC");
    check(!endpoint.configure(other, "Replacement"), "commissioned command identity cannot be replaced");
  }
  auto new_sequence = room; new_sequence[24] = 3; new_sequence[25] = 201; crc(new_sequence);
  check(endpoint.configure(new_sequence, "Renamed open"), "same endpoint can be relabelled without sequence generation");
  LearnedRelayEndpoint reboot; reboot.setup(0);
  check(reboot.ready() && reboot.name() == "Renamed open" && reboot.accepts(room), "reboot restores receive allowlist");

  std::array<LearnedPanel, 32> panels{};
  std::array<LearnedRelayEndpoint, kRelayEndpointCapacity> endpoints{};
  for (uint8_t i = 0; i < 32; ++i) { panels[i].setup(i); endpoints[i].setup(i); }
  auto individual = room; individual[2] = 0x7d; individual[21] = 0x58; crc(individual);
  auto close = individual; close[4] = 0x18; crc(close);
  check(panels[9].configure_target(individual, close, 79, 37, 100, "Panel", "Room"), "existing target commissioned");
  const auto panel_id = panels[9].profile_id();
  const auto panel_record = esphome::persisted[0x4e540109U];
  auto match = match_received_command(panels, endpoints, room);
  check(match.authorized() && match.relay_slot == 0 && match.panel_slot == -1, "native room matches only receive namespace");
  match = match_received_command(panels, endpoints, individual);
  check(match.authorized() && match.panel_slot == 9 && match.relay_slot == -1, "individual path unchanged");
  check(panels[9].last_index() == 79 && panels[9].profile_id() == panel_id &&
        esphome::persisted[0x4e540109U] == panel_record, "room matching never changes panel state");
  auto unknown = room; unknown[10] = 8; crc(unknown);
  check(!match_received_command(panels, endpoints, unknown).authorized(), "unlearned room rejected");
  auto native_close = room; native_close[2] = 0x77; crc(native_close);
  check(!match_received_command(panels, endpoints, native_close).authorized(), "unlearned opposite endpoint rejected");
  check(endpoints[1].configure(native_close, "Room close"), "opposite endpoint learned separately");
  match = match_received_command(panels, endpoints, native_close);
  check(match.relay_slot == 1 && match.panel_slot == -1, "separate native close accepted");
  check(endpoints[2].configure(room, "Duplicate test fixture"), "construct conflicting persisted fixture");
  check(!match_received_command(panels, endpoints, room).authorized(), "duplicate receive matches fail closed");
  check(endpoints[3].configure(individual, "Overlap test fixture"), "construct cross-namespace overlap");
  check(!match_received_command(panels, endpoints, individual).authorized(), "panel/relay overlap fails closed");
  check(panels[10].configure_target(individual, close, 10, 37, 100, "Other", "Room"), "construct duplicate panel fixture");
  check(!match_received_command(panels, endpoints, individual).authorized(), "duplicate panel matches fail closed");

  norman_rf::RelayCache cache;
  check(cache.admit(room, 1) == norman_rf::RelayDecision::eligible, "native frame admitted once");
  check(cache.admit(room, 5000) == norman_rf::RelayDecision::duplicate, "native frame echo suppressed");
  check(cache.admit(new_sequence, 5001) == norman_rf::RelayDecision::eligible, "new received room sequence independent");

  reset_storage();
  LearnedRelayEndpoint invalid_slot; invalid_slot.setup(32);
  check(!invalid_slot.configure(room, "Bad slot"), "out-of-range storage namespace rejected");
  for (int fail_after : {0, 1}) {
    reset_storage();
    LearnedRelayEndpoint broken; broken.setup(0);
    esphome::syncs_before_failure = fail_after;
    check(!broken.configure(room, "Room") && !broken.ready(), "sync failure blocks RF authorization");
    esphome::pending.clear(); esphome::syncs_before_failure = -1;  // Simulated loss of volatile writes.
    LearnedRelayEndpoint recovered; recovered.setup(0);
    check(!recovered.ready() && !recovered.accepts(room), "interrupted commissioning cannot authorize after reboot");
    if (fail_after == 1) check(!recovered.configure(room, "Room"), "durable marker prevents silent reinitialization");
  }
  reset_storage();
  LearnedRelayEndpoint save_failed; save_failed.setup(0); esphome::fail_save = true;
  check(!save_failed.configure(room, "Room") && !save_failed.ready(), "save failure blocks RF authorization");
  reset_storage();
  LearnedRelayEndpoint good; good.setup(0); check(good.configure(room, "Room"), "corruption baseline persisted");
  esphome::persisted[0x4e570100U][10] ^= 1;
  LearnedRelayEndpoint corrupt; corrupt.setup(0);
  check(!corrupt.ready() && !corrupt.configure(room, "Room"), "corrupt persistent template fails closed");
  check(esphome::persisted.find(0x4e540100U) == esphome::persisted.end(), "relay store never uses panel key");

  std::cout << checks << " relay endpoint checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
