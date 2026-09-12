# Product architecture

## Current implementation: 0.9.0 experimental

Up to32 explicitly commissioned Gen1 section targets per ESP32. Each has its own
exact templates/counter and preferred close direction; all share one SPI radio
owner and global opt-in relay policy.14 targets across3 rooms are commissioned;
physical whole-house acceptance remains distinct from a successful RF burst.

| Owner | Responsibility |
| --- | --- |
| `protocol/` | 30-byte frames, application CRC, rolling permutation, exact learned-command matching, bounded duplicate cache |
| `LearnedPanel` | One193-byte target record per slot: the compatible95-byte command payload, labels and primary-close direction; migration marker and durable sequence reservation |
| `LearnedRelayEndpoint` | Separate79-byte receive-only command/name record per slot; no outgoing counter or motor membership |
| `match_received_command` | One ambiguity-rejecting classifier across both namespaces; only a panel match may update that panel's observed counter |
| `NormanRFMonitor` | Radio detection/readback, receive FIFO, direct bursts, relay timing, watchdog and radio-fault handling |
| ESPHome YAML | Provisioning, HA API, diagnostics, optional Bluetooth and service actions |
| Norman HA RF adapter | Section and room covers using adopted ESPHome services; one shared serializer, bound target identities, no RF encoding/counter ownership/hub fallback |

## Direct control and repeating are different operations

Direct commands select an explicitly learned open, close-down or optional
close-up template. The ESP reserves the next counter in nonvolatile storage,
updates the two rolling bytes and CRC, then sends 100 copies on channel15 at
55ms spacing. A room batch reserves all selected counters before any RF, then
interleaves up to8 targets in each55ms round. An individual command uses that
same path with one target. A storage failure consumes any earlier reservations
without transmission or rewind. Batch failure makes all selected states uncertain.
This copy count follows the locally successful burst test; the
earlier proposed 1–3-copy limit was not established by measurement.

Repeating does not generate a counter. While enabled, the receiver scans
channels15 and39 at20ms nominal dwell. An exact learned command received on15
is repeated unchanged on39; a command received on39 is repeated on59, with
20 copies at55ms spacing. Channel59 is terminal. The receive FIFO is drained
before retuning so queued frames retain their channel attribution. A
64-entry/60-second exact-frame cache suppresses echoes, including this unit's
direct commands. Live entries are not evicted to admit new traffic; saturation
fails relay closed. Each bridge forwards an exact frame at most once within
the suppression window, even if it later hears that frame on another channel.
An originating bridge suppresses returning copies of its own direct command.
Thus the path is bounded but does not guarantee both hops occur: placement,
receive timing and which channel is heard first matter. Other channels,
unlearned command families and reply packets are not relayed.

Native room endpoints are explicitly learned in a separate32-slot receive
allowlist, using the same protocol command comparison. Only the newly received
30-byte application frame is forwarded; the saved template is never queued
for replay. Room matching ignores only the two sequence bytes and dependent
CRC, not operation/address/selector bytes. It neither observes nor generates
an individual target counter. Panel inventory protocol3 remains unchanged;
the new relay inventory has its own version1 status action.

Both paths use minimum nRF power, an attached antenna, one half-duplex radio,
TX-completion checking and a bounded watchdog. A successful API response means
the RF burst completed, not that the shutter acknowledged or physically moved.

## Boot, persistence and offline operation

Factory defaults have no learned panel and cannot transmit. Once explicitly
commissioned and relay-enabled, USB power restores the profile and repeat
policy after validation, independently of Wi-Fi/HA. Boot itself sends no command.
The HA relay switch uses `restore_mode: DISABLED`; its presentation must not
overwrite the RF owner's saved enabled state during boot.

Configuration/storage failure blocks transmission. Counter reservation is
durable before a direct burst so a reboot cannot reuse a reserved index. Normal
forward observed counter changes are adopted; ambiguous half-range jumps are
ignored. Community three-code resynchronisation is not enabled. Long outages,
counter wrap/coexistence across real controllers and flash endurance still need
extended qualification.

Slot0 imports the deployed95-byte profile once. A durable migration marker
prevents a missing/corrupt new record reviving stale legacy counters. Existing
commissioned slots may be labelled, but cannot silently change identity or reset
their counter/optional endpoint. Other target slots have independent NVS keys.
Relay-only records use disjoint persistent keys and commissioning markers;
they do not migrate, replace or erase panel records. Default empty, invalid
storage fails closed, and a commissioned endpoint cannot silently change its
command identity. The global relay policy has its own persistent owner. Legacy native actions
address slot0 only. Native protocol3 requires explicit slots, fingerprints and
endpoints for each bounded batch. HA owns room membership and one command lock;
the ESP owns batch reservation and RF scheduling. Repeating needs neither HA nor
Wi-Fi. The unreleased sequential HA sender is replaced, not retained as fallback.

## Product B and A* targets

The connected prototype demonstrates learned individual-command repeating
during Wi-Fi loss. UART records native room Open/Close forwarding, but the
watched trial missed one study section; subsequent direct control moved it.
Native room physical reliability and independent RF attribution remain open.
Version0.9 adds reception of direct hub/ESP channel15 traffic and logs confirm
that first-hop forwarding. Exclusive ESP range extension and reliable delivery
without original repeaters are still unqualified. A dedicated
radio-only Product B image, physical commissioning control,
qualified enclosure and production fixture remain future work. Owner-supplied
STEP enclosure designs are stored under hardware/enclosure; the owner confirms
a printed enclosure fits and works. Broader product qualification is still
pending. It is not yet a universal
Norman replacement that can be sold without installation-specific commissioning.

Product A* adds HA control and diagnostics through ESPHome while keeping the
same local repeat policy. Bluetooth proxy remains active; further sensors can
be added using spare GPIOs after checking power, timing and pin conflicts.
It does not provide arbitrary 2.4GHz or Wi-Fi packet decoding.

## Security and release boundary

The current bench image has no API encryption or OTA authentication and an
open Improv/fallback-AP onboarding path. Keep it on a trusted local network;
do not expose it to the internet or distribute it as a secured customer image.
Customer adoption needs unique API/OTA credentials, protected commissioning,
pinned public packages, signed/versioned updates and a recovery policy.

The local experimental package contains source, factory/OTA binaries and hashes,
not Wi-Fi credentials or learned shutter templates. A factory binary is not a
clone of a commissioned device's NVS. Commission each installation separately.

## Evidence and remaining acceptance work

Direct open/down-close and HA-native control were physically confirmed on one
bedroom lower section. Independent Pluto IQ captures match the transmitted
frames. Relay39→59 was observed with Wi-Fi connected and disconnected; stored
relay enable survives reboot. Upward-close has been correlated and learned;
its separate physical replay result belongs in the investigation evidence log.

Remaining work: whole-house physical acceptance, arbitrary position and stop commands,
all-house range, remote-source variations, congested Wi-Fi/Bluetooth coexistence,
extended power-cycle/soak testing, PA/LNA supply qualification, enclosure and
applicable product/radio compliance. No certification or commercial-readiness
claim is made.

See [commissioning](commissioning.md), [RF evidence](rf-research.md) and the
[earlier design](history/2026-09-03-product-architecture.md).
