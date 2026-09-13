# Bridge diagnostics in Home Assistant

Firmware 0.11.1-experimental adds read-only diagnostics to each ESPHome device.
The Norman integration's room and panel controls are unchanged. No new hardware
is needed on the documented Freenove ESP32-WROOM board.

## RF activity

- **RF last direct command** describes the last accepted new direct command,
  its first target and how many additional targets were included. Automatic or
  manual identical repeats do not overwrite it. It is not movement confirmation.
- **RF last direct command age** is seconds since that command started.
- **RF last received age** is seconds since any CRC-valid Norman frame arrived,
  including duplicates and unlearned targets. It does not prove a relay path.
- **RF last relay age** is seconds since a relay burst completed successfully.
- **RF pending repeats** counts remaining scheduled extra bursts, not shutters
  awaiting acknowledgement.
- **RF transmitter fault** reports whether the last completed burst failed.
  A later successful burst clears it. **RF last transmitter fault** retains the
  last failure reason until reboot. Hardware readiness remains a separate entity.

Ages use monotonic uptime, so they do not need internet access or time sync.
They are unknown until the relevant event occurs and reset at reboot. Fault
history also resets; no event history is written to flash. HA can record these
entities if longer history is needed. `complete_not_acknowledged` means the
radio completed its burst, not that the motor acknowledged or moved.

## Learning and capacity

**RF learning active**, **RF learning status** and **RF learning samples** expose
the current guided session without its token or raw frames. Samples mean the
strongest candidate's distinct samples (0–2), not approval to save an ambiguous
capture. The wizard remains the authority for accepting and saving profiles.
Once the capture window expires, the diagnostic reports `capture_expired` even
if an earlier acceptance attempt left a cached error such as `need_two_presses`.
This status-priority correction is the only behavior change from 0.11.0.

Saved panels/free panel slots and saved relay profiles/free relay slots are
reported separately: there are 32 slots in each namespace. Only usable saved
profiles count; a storage fault still needs investigation, not blind replacement.

## Board health and identification

**Free memory**, **Minimum free memory** (since boot) and **Loop time** use the
native ESPHome debug component. Loop time is the longest main-loop interval
observed in the reporting period, not CPU utilisation. Existing Wi-Fi signal,
uptime and reset-reason entities remain available.

**Identify board** flashes the onboard GPIO2 LED six times over three seconds,
then leaves it off. Repeated presses during a sequence do not queue more work.
The sequence uses nonblocking ESPHome delays and does not transmit RF. The LED
may not be visible through an opaque enclosure. This mapping is for the
[Freenove ESP32-WROOM board](https://docs.freenove.com/projects/fnk0090/en/latest/fnk0090/codes/C/1_LED.html);
check other boards before using GPIO2 or connecting anything to it.

These diagnostics do not alter RF timings, rolling codes, profile storage,
repeater policy or shutter automations. Keep health polling moderate: the RF
task and existing Bluetooth proxy share a small microcontroller.

## Verification

On 13 September 2026, this build was installed by OTA on three Freenove bridges.
All saved panel identities, names, closing directions and rolling counters
survived the update. HA discovered the new entities and reported the expected
profile counts. A temporary learning session showed active/capturing status,
then returned to idle after cancellation without saving a profile. Identify was
accepted through HA and the radio transmit count stayed unchanged; the LED
itself was not observed through the enclosure.

The 0.11.1 expiry correction was also checked on a real bridge: an empty capture
produced `need_two_presses`, the diagnostic changed to `capture_expired` after
the 60-second window, and cancellation returned it to `idle`. No profile was
saved. Native regression tests cover the expiry boundary with a cached error.

Seven native CTest suites, seven Python tool tests and the pinned ESPHome
2026.4.1 build passed. No shutters were moved for this diagnostics update and no
transmitter fault was deliberately induced. These checks do not extend the
existing RF delivery or whole-house reliability claims.
