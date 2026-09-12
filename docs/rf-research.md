# Evidence and current limits

## Camera-observed qualification and ESP-to-ESP forwarding — 12 September 2026

Both prototypes have antennas, USB power and native ESPHome connectivity. One
is positioned by the downstairs hub and one in the office; an unplug/reconnect
test confirmed the mapping. Cameras provide independent
physical observations for the four lounge panels and five motorised office
sections. Home Assistant's assumed state is not used as a movement result.

The leftmost lounge panel closed and reopened through the hub, with the first
bridge logging both unchanged39-to59 relay bursts on0.8. It also closed and
reopened via direct HA-to-ESP commands while both ESP relay policies were off.
Original repeaters remained powered: this proves hub-independent command
generation, not original-repeater-free delivery. All lounge panels were
restored open after these tests.

The old enabled relay listened only on39 while direct ESP commands transmit
on15. Version0.9 adds scanning of15/39 with bounded15-to39 and39-to59 routing,
retaining exact learned allowlists and duplicate suppression. Host route/cache
tests and the pinned ESPHome build pass. Both units retained commissioned
profiles across OTA. The updated first unit logged hub-originated15-to39
forwarding and the lounge pilot still closed/reopened through the hub.

The second unit then sent100 copies of an office Top Left command on15. The
first unit received the matching CRC-valid frame and logged20 unchanged copies
on39. This is positive ESP-to-ESP forwarding evidence, but the office shutter
stayed open. A first-bridge direct Bottom Left test also left that section open.
These initial failures were followed by successful local tests described below;
range, receive-channel availability and counter acceptance are not yet isolated
as causes. A logged relay burst alone is not proof of shutter movement.

All five office motorised sections subsequently passed individual direct
close/open pairs from the office ESP. The whole-office batch closed all five
at11:56:15UTC and reopened all five at11:56:39UTC. The motorless top-right section
is excluded. All four lounge panels also passed individual direct close/open
pairs from the downstairs ESP; the four-panel batch closed all four
at12:01:05UTC and reopened all four at12:01:23UTC. Both rooms were restored open.
The scheduler was unchanged: these room actions interleave learned individual
commands, not a synthesized native-room packet. Camera snapshots confirm end
positions, not exact or simultaneous motor start times.

Two initial office commands failed before a hub close followed by successful
direct open/close/open sequences. Only one of those targets adopted a newer
observed rolling index; another succeeded without adopting the observed index.
This does not establish a universal stale-counter diagnosis or justify automatic
resynchronisation. No counter reset or speculative retry was added.

Direct test pairs called only ESPHome actions, not Norman hub services.
Original hub/repeaters were still powered and could have forwarded those
signals. Each room has one successful combined close/open pass at this
placement, not an isolated ESP-only or long-term reliability qualification.
The owner is retaining the hub and original repeaters for normal operation;
exclusive RF-path isolation is deferred rather than reported as a pass.

Four bounded30-second R1 Pluto recordings captured their intended command
windows but decoded no valid Norman frames. The sample-rate and LO readbacks
were correct. These captures do not independently corroborate RF transmission;
the stronger current evidence is matching frames in the other ESP's receiver
log. The Pluto is upstairs in another office, and its new antenna bands are
not independently identified. All Pluto work remains receive-only.

Native room generation, prolonged room reliability and original-repeater-free
range tests remain separate qualification items. Historical results below retain their
original firmware and placement boundaries.

## Native room receive-only support — 0.8

Three consecutive owner-triggered native study Open requests were captured on
channel39 with valid CRC. They retained the native operation/selector body but
did not follow an increment-by-one sequence under the community byte25 decoder.
Do not infer or generate a room counter from that model. The old0.7.1 firmware
received all three without forwarding because none matched an individual target.

The0.8 build introduces explicitly commissioned receive-only endpoints. It
passes the freshly received application frame unchanged to the existing relay,
without synthesizing either sequence byte. All14 direct target records and
their counters were preserved across OTA; two native study endpoints survive
a subsequent reboot with zero startup transmission. Host tests cover all65,536
sequence-byte pairs, invalid frames, ambiguity, storage failures and counter
isolation.

In the first watched 0.8 native-room close/open trial, UART recorded each fresh
channel39 command matching its separate relay-only endpoint and completing
20 copies on59. No individual target counter changed. The owner reported that
Top Left did not move; the trial is not a full-room physical pass. Subsequent
HA→ESP direct close/open commands moved that section, with ten matching
CRC-valid close frames independently decoded on Pluto channel15.

The native-room Pluto recording ended before the app commands were sent.
Its zero decoded packets are a missed recording window, not evidence of absent
RF. UART completion is firmware evidence, not independent antenna-output
verification. Original repeaters remained powered, so a successful movement
cannot yet be attributed solely to this bridge. Signal, timing and receiver
acceptance remain hypotheses for the miss, not established causes.

A fixed-placement individual Top Left comparison subsequently succeeded for
hub Close/Open with ESP relay disabled. With relay enabled, UART recorded
Close/Open39→59 forwarding,20copies each; the owner confirmed both movements
and all five study sections restored open. Other repeaters
remained powered. The successful disabled baseline means this comparison
cannot establish added range or exclusive ESP contribution.

In a later trial the owner found one original repeater still powered, so the
initial supposed isolation and failed Close are not all-repeaters-off evidence.
After correcting this, clean hub Close/Open with the ESP relay enabled both
physically succeeded while all originals were reported unplugged. UART recorded
20 forwarded copies per command. A matching ESP-disabled Close was then received
on59 with no ESP transmission during the recorded window; the owner reported
that it also closed. This therefore does not establish a range benefit.
A later status read found one additional unrecorded relay burst after the
window ended; do not infer its source or extend the zero-TX interval. Reception
channel alone does not identify the emitter, and a hopping receiver's missing
channel record is not proof of absent RF. This is not yet an extra-range claim.

The subsequent restoration exposed another physical failure: a hub Open with
original repeaters absent did not reopen Top Left, and a later direct ESP Open
also left it closed despite completing100copies/channel15. The originals were
restored by the owner; one normal-hub restoration Open was then issued. This
does not establish whether the miss is range, timing, counter acceptance or
another cause. Do not promote successful TX into reliable movement or reset
rolling state as a speculative recovery.

The production HA study room cover currently reports `level_fanout` for Open
and Close. It is not an equivalent source for an app-native room comparison.
Its existing setting and production integration have not been changed.

## Existing Home Assistant integration

The existing
[`Herbertmt978/Norman-HA-Integration`](https://github.com/Herbertmt978/Norman-HA-Integration)
retains its HTTP hub transport. The experimental RF branch adds a separate
ESPHome-backed cover without moving packet encoding or counter ownership into HA.

## RF evidence

- The Norman HUB01 FCC test report records proprietary 1 Mbps GFSK at 2415,
  2439, and 2459 MHz (nRF channels 15, 39, and 59):
  <https://fcc.report/FCC-ID/PPQ-HUB01/4063103.pdf>.
- The RPT01 filing covers 2415–2459 MHz:
  <https://fccid.io/PPQ-RPT01>.
- Home Assistant community post 58, dated 9 August 2026, provides a working
  Arduino/nRF24L01+ capture-and-replay proof of concept:
  <https://community.home-assistant.io/t/norman-tdbu-blind-control/705405/58>.
- That proof of concept derives from the GPL NRF24 Sniffer:
  <https://github.com/Yveaux/NRF24_Sniffer>.

The reported channel-15 parameters are 1 Mbps, five-byte address
`dc:5c:9c:1c:05`, nRF hardware CRC disabled, and Norman application CRC-16
polynomial `0x0083` with initial value `0xacc8`. The sketch handles a 30-byte
application frame, but its payload-size call is commented out; with the RF24
library's static default it may actually put 32 bytes on air, ending in two
zero pad bytes. Receive-only firmware must retain all 32 bytes until captures
from this installation settle the on-air width.

The shared sketch is not a transparent repeater: it embeds one author's
captured open/close templates, learns/advances a rolling value, and transmits a
100-copy burst. Its AVR pin names, `fdevopen`, ISR behaviour, and device-specific
payloads are not suitable for direct ESP32 deployment.

## What other 2.4 GHz equipment can see

- A stock CC2652P SLZB-06 is a Zigbee/Thread coordinator. Its exposed firmware
  and serial/network protocols do not offer arbitrary proprietary-GFSK capture
  or transmit, and Zigbee's IEEE 802.15.4 PHY does not decode Nordic Enhanced
  ShockBurst. The CC2652P silicon has a flexible proprietary radio mode, but
  using it for Norman would require replacement TI firmware and would take the
  adapter out of ZHA service; this is not a safe experiment on the live
  coordinator.
- UniFi radio capture and Wireshark monitor mode decode IEEE 802.11 frames.
  UniFi can correlate the Norman hub's HTTP calls or show aggregate RF energy,
  but it cannot recover Norman/nRF24 payloads.
- The purchased nRF24L01+ is the appropriate targeted receiver after channel,
  data rate, and address are known. A HackRF-class SDR plus GNU Radio/gr-nordic
  is the better discovery tool when those parameters are unknown.

## Unknowns to resolve on the user's installation

- whether the five-byte address is universal;
- whether the radio payload is 30 bytes or 32 bytes with two pad bytes;
- which channels the Gen 1 plantation shutters use in practice;
- the user's remote/room/panel selectors and command bytes;
- genuine RPT01 forwarding timing, cross-channel behaviour, and loop control;
- whether one half-duplex nRF24L01+ provides adequate relay timing;
- safe rolling-state behaviour across reboot and missed traffic.

## Local RF investigation — 10 September 2026

All 58 posts in the linked community discussion were read, including post39's
no-ACK/repeated-burst observation and the complete post58 Arduino sketch. The
256-entry permutation and reported three-sequential-command resynchronisation
are community findings; resynchronisation is not yet locally proven.

The initial monitor wrote RX_ADDR_P0 in the wrong order. RF24's uint64_t overload
for `0xdc5c9c1c05` writes register bytes `05 1c 9c 5c dc` (least-significant
first). Correcting this produced the first CRC-valid local reception. The
subsequent diagnostic fix drains FIFO occupancy rather than relying on the
cleared RX_DR event and compares only the first 30 bytes for duplicates. The two
extra captured bytes vary and must not be part of application identity.

A Pluto-compatible device identifies as Rev.C Z7010/AD9361. Its R1 connector has
the owner's 2.4 GHz antenna; R2/T1/T2 are unconnected. Receive-only captures use
R1/A_BALANCED, 4 Msps, 3.2 MHz RF bandwidth, manual gain30 dB and int16 I/Q
containers containing signed12-bit samples. The official ADI Windows drivers
were installed manually by the owner; IIO and RNDIS then reported OK.

Independent offline GFSK demodulation at1 Mbps recovered50 CRC-valid copies of
one hub open frame at2415 MHz. Its30 application bytes matched the ESP32 capture
on channel59 exactly. A separate12-second192 MB USB capture recovered73
CRC-valid copies of the corresponding close command, matching the ESP32's
channel39 capture. Open versus close changes application byte4 (`38` versus
`f8`) for this particular hub/panel command, in addition to rolling bytes24/25
and CRC28/29. These are learned installation-specific templates, not universal
open/close opcodes or proof of physical position.

Raw IQ, private templates, command timestamps and hashes are retained outside
Git under `D:/CodexTemp/norman-rf-investigation-20260910`. The first command IQ
recording is explicitly a13.305344-second prefix: native `/tmp` filled before
the requested15 seconds. That prefix transferred with a matching SHA256; it is
usable for frame decoding, not complete-burst counting. Normal host-streamed
captures avoid the native `/tmp` limit and check exact sample count.

`scripts/decode-iq.py` searches clock phase and polarity, accepts only the known
application length and CRC, and does no bit repair. Three deterministic
synthetic waveform/CRC tests and a real idle recording support its validation;
the idle recording contained no CRC-valid Norman frames. The historical0.4
transmit action was bounded to supported channels, valid30-byte frames,1–100
copies,55 ms spacing, lowest nRF power and a10-second cooldown. It is neither
an autonomous repeater nor a sellable release. The current0.5 learned-command
API replaces that raw bench action; see the measurements below.

## Learned commands and autonomous relay — 0.5

The owner physically confirmed native HA→ESPHome→RF close-down/open after reboot.
The profile/counter survive reboot; the relay switch was corrected to stop its
default boot action overwriting the RF owner's saved enabled state.

Independent Pluto reception found84 copies on channel39 while ESP sent only on15.
With the nearby original repeater unplugged, the comparable test decoded zero
on39. The owner restored that repeater; two other repeaters and the hub remained
on throughout, so this does not isolate every path in the house.

For one exact learned command, ESP UART recorded RX39 followed by20 unchanged
TX59 copies and duplicate suppression. A corresponding Pluto59 recording decoded
82 valid copies; other repeaters also transmit there, so not all82 are attributed
to ESP. A second UART test recorded the same relay while Wi-Fi was disabled.
The initial Wi-Fi test did not promptly reconnect; local recovery was then moved
to a script with a bounded restart if reconnection fails. Recovery requalification
is separate from the already observed offline RF operation.

The owner's isolated lower-left upward-close capture decoded61 identical valid
frames. It retains the learned target body but changes bytes2/3 versus close-down,
not byte4 alone. Firmware now accepts only exact saved endpoint bodies, excluding
rolling fields and CRC, instead of treating one byte as a universal command.
The other lower panel differed in the retained target body and is not admitted.

Application counters and the physical effect of newly generated direct commands
are proven for this pilot, not arbitrary controller co-existence over256 cycles.
The 30-byte application identity is established; transmitting32 static bytes
with two zero pads works locally, but this is not a complete on-air format proof
for every Norman product.

## Native room selector investigation — 10 September 2026

Offline comparison of one owner's whole-room open/close pair with five watched
section identities found a consistent selector relationship: reversing the bits
of `section_byte21 XOR room_byte21` gives the section's HA group ID. All five
groups matched the independently read HA metadata. Apart from command and
sequence fields, these frames differ only at byte21. This is evidence for this
room's selector encoding, not a universal address constant, physical membership
guarantee or permission to synthesize arbitrary group commands.

The native room operation also has different command fields from individual
position commands. It must not be implemented by changing only the selector in
an individual template. The captured native pair passes the existing CRC checks,
but its two decoded sequence values were observed about five minutes apart.
They do not establish a next-code rule or independent room-counter ownership.

The [community Arduino sample](https://community.home-assistant.io/t/norman-tdbu-blind-control/705405/58)
uses a byte25 lookup to update one in-memory sequence and generates the next
value for its captured commands. That is not evidence of safe simultaneous
multi-target sequencing or shared hub/remote coexistence on this installation.
Original hub samples do not consistently have byte24 equal to decoded byte25;
do not mistake our generated-frame convention for the original hub's format.

Before 0.8, native room selectors matched none of the14 individual profiles,
so those commands were received but not forwarded. Version0.8 closes that
eligibility gap with separate receive-only endpoints, as described above;
it does not establish a cause or remedy for every missed physical movement.
No generated native-room direct command or room counter has been introduced.

## Later HA room checks and identical repeats — 12 September 2026

The production HA RF candidate exposed nine physically mapped section covers
and two room covers. Separate room close/open checks passed again, but subsequent
combined and sequential-room runs missed sections. In the sequential run, with
a five-second gap after the office burst, HA completed both actions while the
cameras showed office4/5 and lounge2/4 closed. Open restored the lounge but left
two office sections closed; individual Opens restored them. The schedule was
not migrated or enabled. The HA position is still an assumed command intent.

At the owner's request,0.9.1 adds explicit repeats of the most recent completed
local command. These are separate100-copy bursts using the original frames,
not repeated new rolling-code reservations. Maximum two additional bursts
within60seconds, exact request matching, volatile storage and invalidation
rules are documented in [commissioning](commissioning.md#explicit-identical-command-repeat).
Default sending and autonomous relay policy are unchanged.

Both prototypes received0.9.1; all14 identities/endpoints were retained. Office
rolling counters were identical across its update; downstairs office counters
advanced while it received the ongoing office test, with no reset. Five host
CTest suites, three decoder tests, four logger tests and the pinned ESPHome
build passed. Fresh boot rejected repeat without a prior local command.

Camera-observed comparison:

| Trial | Original burst | Later identical burst(s) | Restoration |
| --- | --- | --- | --- |
| Office Close | Four of five closed; Top Left remained open | First repeat still4/5; second repeat5/5 | New Open reopened5/5; an extra identical Open did not reverse them |
| Office second pair | Close5/5 on first burst | One identical Close retained5/5 | Open5/5 on first burst |
| Lounge Close | Three of four closed; third from camera-left stayed open | First identical repeat4/4; second retained4/4 | New Open reopened4/4 |

Originals and repeats were about20seconds apart in these manual comparisons.
The office log recorded500,1000,1500 total transmitted copies across its first
Close and two repeats. Native counter readback showed only the original
reservation. A further repeat request was refused. Lounge repeats likewise
left the four command counters unchanged. These results demonstrate recovery
from specific misses, not a generally validated automatic retry cadence.

During the first office comparison, the downstairs bridge, still running0.9,
logged `radio_tx_failed` partway through a channel39 relay. The latched fault
disabled autonomous repeating. Its subsequent0.9.1 OTA/reboot restored the
saved enabled policy. The existing diagnostic does not distinguish a timing,
SPI, supply or radio fault; no root cause or permanent fix is claimed. Remote
shutter range alone cannot explain the local transmitter-completion failure,
because this sender has automatic acknowledgements disabled. Investigate this
fault before relying on unattended schedules or claiming a finished repeater.

Original hub/repeaters remained powered throughout. Exact-copy replays can be
suppressed by other bridges' duplicate caches; these tests do not prove every
RF hop repeats again. All tested shutters were returned open. Raw captures,
household images and installation identities remain outside the public repo.

## Automatic bursts and direct HA schedules — 12 September 2026

The owner requested that the later identical bursts happen on the ESP without
manual intervention. Firmware 0.9.2 uses the existing cache and radio scheduler:
two extra bursts at least 20 seconds apart, expiring 60 seconds after the
original start. A separate persistent HA switch defaults ON. Only that policy
is persisted, never the pending command. The manual action shares the budget.
No additional rolling counter is reserved for a repeat. Cancellation and
fault behaviour are described in [commissioning](commissioning.md#automatic-command-repeats).

Both prototypes received the same pinned ESPHome 2026.4.1 build. Native reads
before and after OTA showed all 14 target records, including their counters,
unchanged on each. Both booted ready, relay enabled, automatic policy ON and
zero pending/started automatic bursts.

One HA Close was sent to each local room cover, with no manual repeat action:

| Room | Early camera observation | After both automatic bursts | New Open |
| --- | --- | --- | --- |
| Office | Four of five closed; Bottom Left stayed open | Still four of five closed | All five open |
| Lounge | Three of four closed; second from camera-left stayed open | All four closed | All four open |

Each board logged exactly two automatic bursts about 20 seconds apart. Both
Open commands also completed their automatic budgets. No transmitter fault
was logged during these bounded recordings, but this is not a diagnosis or
fix for the earlier fault. This run recovered a lounge miss and did not
recover the office miss. The original hub and repeaters remained powered.

The office opt-out test sent another Open, then disabled automatic repeats
before its due time. Pending work cleared and the started-repeat count did
not increase. After the preference had time to save, a reboot retained OFF
with no cached command. Re-enabling did not revive it; the policy was returned
to ON for normal use. The unit tests cover expiry, busy-radio deferral,
counter/profile conflicts, shared manual budget and clock wrap.

The owner also explicitly requested moving sunrise/sunset off hold. The
existing HA automation now uses local office/lounge RF room controls and is
enabled, with the same sun triggers and a five-second gap after the office
action. Both branches passed isolated HA execution tests with fake cover
services on the pinned and cached-latest test environments; the saved live
configuration and enabled state were then read back. The physical tests above
exercise the real covers, not a naturally occurring sunrise or sunset.
The prior automation is backed up; phone-app schedules were not changed.
This is experimental daily use at the owner's request, not a new claim of
reliable unattended or isolated ESP-only operation.
