# Norman RF Bridge

Control Norman Gen1 shutters from Home Assistant using an ESP32 and a small
2.4 GHz radio. Plug it into USB power and leave it near the shutters. Once
set up, it can send commands directly and repeat learned Norman traffic.

The aim is to replace the Norman hub's control role and its USB repeaters with
hardware you can build, update and use with ESPHome. Direct control and radio
forwarding both work on the prototypes. This is still an experimental project,
not a universal plug-in replacement: setup needs watched learning and some commands
still miss panels. Keep the original controls available while testing.

## What it does

**Control shutters from HA.** The companion
[Norman integration](https://github.com/Herbertmt978/Norman-HA-Integration)
adds individual section and whole-room Open/Close controls. HA sends a Wi-Fi
command to the local ESP; the nRF24-compatible module sends the shutter command.
That path doesn't call the Norman hub or need its password. The integration's
RF transport is currently a pre-release feature; follow its
[RF setup guide](https://github.com/Herbertmt978/Norman-HA-Integration/blob/main/docs/esphome-rf.md).

**Work as a USB-powered repeater.** After learning the installation's commands
and enabling relay mode, the board listens for matching traffic and forwards
it automatically. It can forward commands from the Norman hardware or another
ESP bridge. This runs locally, including when Wi-Fi or HA is unavailable.
It isn't an unrestricted repeater for every packet it hears.

**Learn and manage profiles in HA.** Firmware **0.10.2-experimental** uses a
guided flow in the companion integration. Name a section and room, press the
requested controller actions, and confirm what moved. HA can also learn
forwarding-only actions, rename/regroup profiles, and remove a selected profile
with confirmation. Follow the [setup and profile manual](docs/learning.md).
The learner checks repeated matching samples without assuming the hub's codes
advance in order. It tolerates one-off background messages but refuses to guess
between two plausible targets. A fresh board has learned five sections through
the HA wizard using hub commands, without importing profiles or using an SDR.
All five passed watched individual and whole-bedroom Open/Close-upwards tests
after the ESP was placed in the bedroom. Long-term delivery and use in another
household still need qualification.

**Repeat its own commands.** Firmware **0.10.0-experimental** sends up to two
extra full bursts after a direct command, waiting two seconds after each
successful burst finishes before starting the next.
They use the same command bytes and rolling codes, not three different
commands. Each board has an **RF automatic command repeats** switch in HA.
It starts enabled and remembers your choice. Reboots don't replay old commands.

**Stay useful as an ESPHome device.** The image includes an active Bluetooth
proxy, Wi-Fi signal and uptime diagnostics, radio counters, and OTA updates.
Version **0.11.0-experimental** adds last-command and RF-activity ages, transmitter
faults, learning/profile counts, memory and loop timing, plus a three-second
onboard LED Identify button. See [the diagnostic entity guide](docs/diagnostics.md).
You can add supported sensors on spare pins once power and pin conflicts are
checked. The external radio is for Norman RF; it isn't a Wi-Fi adapter or a
general-purpose packet sniffer.

## Where it fits

```text
HA dashboard or automation
        │ Wi-Fi / ESPHome API
        ▼
ESP32 + nRF24 radio ── Norman RF ──► shutters
        │
        └── another commissioned ESP can repeat matching RF

Norman hub / original repeater ── RF ──► ESP repeater ── RF ──► shutters
```

Putting a directly controlled ESP in a distant room can avoid relying on the
hub's radio reaching that room. HA reaches the ESP over Wi-Fi instead. A board
used only as a repeater still needs to hear the upstream transmitter, so its
placement matters.

In the test installation, the existing sunrise/sunset automation now uses the
office and lounge ESP controls. This was an owner-approved move to experimental
daily use, not a claim that every delivery issue has been solved. HA owns the
schedule; the ESP owns the RF transmission and its bounded extra bursts.
Schedules in the Norman phone app must be disabled separately to avoid competing
commands. Removing the hub also means losing its app-based control and schedules.

## Hardware

The prototypes use the same simple assembly:

| Part | Used here |
| --- | --- |
| ESP32 | [Freenove ESP32 board kit](https://www.amazon.co.uk/dp/B0C9THDPXP), ESP32-WROOM-32E, 4 MiB flash, USB-C |
| Radio | [nRF24L01+ PA/LNA module with SMA antenna](https://www.amazon.co.uk/dp/B0DK2Z6C7K); the photographed chip is marked SI24R1, an nRF24-compatible part |
| Antenna | One attached 2.4 GHz antenna per radio |
| Wiring and power | Jumper leads and USB power; the radio takes 3.3 V from the ESP board in these prototypes |
| Enclosure | [Printable base and top](hardware/enclosure/README.md), supplied as STEP files; the owner has printed them and confirmed the fit |

These are the owner's purchase links, not guarantees about future seller stock.
See [hardware and wiring](docs/wiring.md) for the full GPIO map and power advice.
**Never connect the bare radio to 5 V. Fit its antenna before transmitting.**
The PA/LNA supply has not been qualified for a finished product; a stable
dedicated 3.3 V supply and local decoupling are recommended for further testing.

## Will it work with my shutters?

It is a reasonable candidate for another installation using the same Norman
Gen1 radio protocol. It has not yet been qualified in a second home or across
all Norman motor generations. The Norman name alone isn't enough to establish
compatibility, and the HA integration's support for other hub generations
doesn't mean this radio firmware supports them.

Each motorised section needs its own learned command profile. A shutter with
independent upper and lower sections therefore needs two profiles. The sections
share a radio protocol; they don't each require a completely different decoder.
What changes is the command's identity/selector, its supported operation and its
rolling state. The firmware contains the protocol logic, **not this home's
shutter codes**.

**You shouldn't need a Pluto for a compatible installation.** The ESP/nRF24
receiver can log the supported packets from your existing hub or controller.
The remaining job is to associate captures with the panel and direction you
actually operated, validate them, and save them as that panel's profile. This
is handled by the new [HA learning flow](docs/learning.md). A Pluto or
another suitable SDR is useful if your system uses different radio settings,
the logger sees nothing, or the protocol needs further investigation.

Start with [compatibility and learning your shutters](docs/compatibility.md).
Don't copy someone else's frames or an old full-flash backup of a commissioned
board. Matching the protocol is not the same as learning your installation.

## Getting started

1. Assemble the board using the [verified wiring](docs/wiring.md).
2. Run `scripts/test-protocol.ps1`, then `scripts/build-stage1.ps1` using the
   tested ESPHome version, **2026.4.1**.
3. Flash with `scripts/flash-stage1.ps1 -Port COM3`, replacing COM3 with the
   board's actual port. Provision Wi-Fi through USB/Bluetooth Improv or the
   temporary `Norman RF Bridge Setup` access point.
4. Add the discovered ESPHome device in HA. Confirm the radio reports ready.
5. Add the Norman integration's experimental RF transport, select the bridge
   and follow [guided learning](docs/learning.md) for each section or relay action.
6. Select rooms for direct HA control and test their Open/Close while watching.
   Use Reconfigure later to add, rename, regroup or remove profiles.

A new factory image has no learned commands and cannot move or repeat shutters
until commissioned. Once configured, ordinary USB power is enough; a permanent
USB connection to a computer is not required. HA control needs Wi-Fi, local
repeating does not.

The scripts currently use Windows/PowerShell and put build output under
`D:\CodexBuild\norman-rf-bridge`. `scripts/package-experimental.ps1` packages
factory/OTA binaries, source and an ESP Web Tools manifest with checksums.
This is a developer packaging tool, not a hosted one-click installer.
The separate `*-stage0.ps1` scripts are for the older radio-free recovery image,
not normal RF upgrades.

## What has been tested, and what hasn't

The original two boards have moved their local shutters through HA: five office
sections and four lounge panels, individually and as room groups. Learned repeating has
also been observed, including a Wi-Fi-disconnected test. The original Norman
hardware stayed powered during the recent room trials, so those results don't
prove that every RF path used only ESP devices.

A third, freshly commissioned board learned five bedroom sections through HA.
With upward closing selected, each section passed an individual Close/Open
test, and all five passed two whole-bedroom Close/Open cycles on 13 September
2026. These commands used HA's ESP controls without calling the hub. The existing
hub and repeaters remained powered, so they may still have helped carry RF.
The [learning manual](docs/learning.md#fresh-board-testing-and-remaining-limits)
records the capture fixes, closing-direction correction and test limitations.

An earlier automatic-repeat test closed all four lounge panels, but still left
one of five office sections open. A subsequent Open restored both rooms. An
earlier local transmitter fault also remains unexplained. Extra bursts can
recover some misses; they cannot guarantee reception or fix every fault.

HA shows the requested state after the first successful burst. There is no
motor acknowledgement or measured position, so a green HA action is not proof
that a panel moved. Room controls use interleaved individual commands, not a
native whole-room broadcast, and the motors may start at different times.
There are 32 profile slots per bridge and up to eight sections in one room batch.
Stop, arbitrary percentage positioning and battery feedback aren't implemented.

The current bench image has **no API encryption or OTA authentication** and an
open fallback setup AP. Use it only on a trusted local network. Customer-ready
provisioning, longer reliability tests and product/radio compliance are still
work to do before selling it as a finished replacement.

## More detail

- [Commissioning, repeat settings and recovery](docs/commissioning.md)
- [Compatibility and per-panel learning](docs/compatibility.md)
- [Hardware and GPIO wiring](docs/wiring.md)
- [Enclosure CAD files and checksums](hardware/enclosure/README.md)
- [How the firmware works](docs/product-architecture.md)
- [RF captures, test results and known failures](docs/rf-research.md)

## Credits and licence

This project builds on the Norman research and Arduino examples shared in the
[Home Assistant community thread](https://community.home-assistant.io/t/norman-tdbu-blind-control/705405),
including the `NRF24_Sniff` work and Yveaux's NRF24 Sniffer. It is licensed
under [GPL-3.0-or-later](LICENSE). This is an independent project, not an
official Norman product.
