# Norman RF Bridge

Experimental ESP32/nRF24 firmware for direct Norman Gen1 shutter control and
bounded autonomous repeating, with optional Home Assistant/ESPHome features.

This project is deliberately separate from
[`Norman-HA-Integration`](https://github.com/Herbertmt978/Norman-HA-Integration).
That integration's RF candidate uses this bridge without calling the hub. Its
existing HTTP hub controls remain separate; neither path provides verified
physical shutter position through the RF prototype.

## Current state: experimental; room delivery remains intermittent

Version 0.9 adds bounded forwarding of bridge-originated commands: an enabled
relay scans channels 15 and 39, forwarding 15 to 39 or 39 to 59. Channel 59 is
terminal and exact-frame echoes are suppressed. This is not an unlimited mesh.
Both prototypes have the update. Camera observation confirmed individual
close/open controls for five office sections and four lounge panels, followed
by a successful whole-room close/open for each room using its local ESP.
Another ESP also received and forwarded a matching bridge-originated frame.
The original Norman hardware remained powered: isolated ESP-only delivery and
reliable range extension are still unqualified. See [current evidence](docs/rf-research.md).

Later HA-cover tests reproduced missed sections in both rooms, including when
rooms were sent sequentially with a five-second gap. A completed radio burst is
not a shutter acknowledgement. The sunrise/sunset migration is therefore on
hold. Version0.9.1 adds an explicit diagnostic repeat action; it does not change
default sending or claim to fix delivery. See [repeat testing](docs/commissioning.md#explicit-identical-command-repeat).

The connected Freenove ESP32-WROOM board has an nRF24-compatible PA/LNA attached.
It provides:

- native ESPHome API discovery in Home Assistant;
- USB and Bluetooth Improv plus fallback-access-point Wi-Fi provisioning;
- OTA updates and safe-mode recovery;
- an active Home Assistant Bluetooth proxy;
- uptime, Wi-Fi, firmware, and reset diagnostics;
- SPI-backed radio detection and configuration readback;
- bounded receive/transmit/relay/fault counters;
- explicitly learned open/close-down/optional close-up commands;
- persistent rolling state reserved before a direct RF burst; and
- opt-in autonomous repeating of learned commands, restored from USB power.

Direct open/down-close/up-close were physically confirmed on the initial pilot,
including HA-native actions. The firmware now supports32 independent target slots;
14 real section profiles in3 rooms are captured and persisted. The HA candidate
creates section and room covers with interleaved batches of up to8 targets.
Earlier fixed-order0.7.0 and rotating-order0.7.1 batches moved only one of five
study sections in watched trials. On12September, the existing interleaved
scheduler passed five-section office and four-panel lounge tests from their
respective local bridges. This is one successful close/open pair per room,
followed by further passes and failures, not prolonged reliability or simultaneous
motor-start qualification. Generating
native whole-room packets remains under investigation; these room controls use
learned individual commands. Whole-house qualification is separate from
commissioning. Channel39→59 relay was
observed with Wi-Fi connected and disconnected. Factory defaults cannot send.

## Demonstrated repeater functionality

The tested USB-powered ESP bridge has demonstrated autonomous forwarding of
learned individual-panel commands from channel39 to59, including while its
Wi-Fi was disabled. The saved enable policy survives reboot; startup itself
sends no movement command. This is positive evidence of repeating on one
prototype, not qualification of every board or installation.

Version0.8 adds a separate receive-only allowlist for native room operations.
The study's native Open/Close entries are commissioned and survive reboot;
UART now records both fresh native commands being accepted and forwarded
39→59, with 20 copies each and no individual-counter changes. In the watched
room trial, the owner reported that Top Left did not move. A subsequent direct
ESP close/open moved that section successfully. This is partial native-room
evidence, not a room reliability pass or a proven diagnosis of the miss.
These entries never generate counters, create HA motor covers or enter
individual room batches.

A later one-section Close/Open trial succeeded with all original repeaters
reported unplugged, and the ESP logged both relay bursts. However, the owner
also reported success for the matching ESP-relay-disabled Close. These
placements have not demonstrated added range or exclusive ESP contribution.
A complete original USB repeater replacement still requires reliable native
room operation, independent RF attribution and controlled range/reliability
comparisons. Both boards now have matching firmware and learned profiles,
with relay policy and profiles verified after reboot. Each has passed local
individual and room commands under camera observation. This does not establish
repeater-only delivery at every placement. Neither is a secured customer release.

Historical individual delivery was also unreliable: a direct ESP Open
completed its100-copy burst but the owner reported the section stayed closed.
Transmit completion is not movement confirmation. Keep the original controls
available and do not treat this prototype as a finished drop-in replacement.

Hardware details, the actual tested radio marking, power limitations and the
pin map are in [hardware and wiring](docs/wiring.md), in this same repository.
The [prototype enclosure base and top](hardware/enclosure/README.md) are also
included as the owner's original STEP files, with checksums and fit limitations.

## First flash

1. Test with `scripts/test-protocol.ps1`; build with `scripts/build-stage1.ps1`.
2. Flash a board with `scripts/flash-stage1.ps1 -Port COM3` (or its current
   serial port).
3. Provision the board from this Windows PC with
   `scripts/provision-stage0.ps1 -Port COM3`, use USB/Bluetooth Improv, or join
   the fallback access point named `Norman RF Bridge Setup`.
4. Add the discovered ESPHome device in Home Assistant.

Build products are directed to `D:\CodexBuild\norman-rf-bridge` so large toolchain
caches do not enter Dropbox or the Git worktree. Builds and flashes deliberately
require the tested ESPHome version, 2026.4.1.

`scripts/package-experimental.ps1` creates factory/OTA binaries, source ZIP,
ESP Web Tools manifest and SHA256 inventory under the external build root. It
refuses a dirty checkout unless `-AllowDirty` explicitly labels the development
snapshot. Historical `stage1` script names now refer to the current RF image.
The separate `*-stage0.ps1` scripts target `esphome/stage0-recovery.yaml`, the
original radio-free recovery image; do not use them for an RF upgrade.

The bench image is unencrypted, has unauthenticated OTA and an open fallback
setup AP. Keep it on a trusted local network. A sellable release still requires
owner-specific credentials, a pinned public adoption package, product testing
and a supported update/recovery lifecycle. The factory image contains no learned
customer commands: commission each installation separately.

## Commissioning and scope

Use `scripts/bridge-control.py` to commission each captured open/preferred-close
pair with a stable slot, section name and room. An optional opposite closing
endpoint can be learned separately. The ESP owns templates/counters and global
relay policy; HA stores only explicit target bindings. See the commissioning guide.

Arbitrary position, stop, exhaustive counter recovery,
range/coexistence qualification and secured customer provisioning remain open.
Use only one direct-command bridge per learned controller identity until
multi-controller counter coordination is qualified. Repeating freshly received
frames is distinct from generating commands. Never clone stale rolling state.

See [product-architecture.md](docs/product-architecture.md),
[wiring.md](docs/wiring.md), [commissioning.md](docs/commissioning.md), and
[rf-research.md](docs/rf-research.md).

## Protocol core

`protocol/` contains framework-independent C++ for the facts already supported by
the August 2026 proof of concept: the observed 30-byte application frame, Norman's
application CRC, and the reported rolling-code permutation. The actual nRF payload
may be 32 bytes with two trailing pad bytes, so monitor firmware must capture the
full radio payload before normalising it. Run `scripts/test-protocol.ps1` for fresh
host tests. No captured remote or shutter identifiers are stored in this repository.

## Licence and provenance

The RF research builds on the GPL-3.0-or-later `NRF24_Sniff` work shared in the
Home Assistant community thread and the original Yveaux NRF24 Sniffer. This
project is therefore GPL-3.0-or-later. See [LICENSE](LICENSE) and
[rf-research.md](docs/rf-research.md).
