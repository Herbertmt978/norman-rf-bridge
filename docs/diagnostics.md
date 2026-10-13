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

## Optional Bluetooth scan pause (0.11.2-experimental candidate)

**RF pause Bluetooth scanning** is an opt-in configuration switch, initially off.
When enabled, all actual RF bursts (direct commands, manual/automatic repeats and
autonomous relays) request a temporary advertisement scan pause. RF never waits
for Bluetooth acknowledgement, so there is no added RF holdoff or relay wait. Pausing is best
effort: a scan already starting is stopped on a later main-loop iteration, and a
Home Assistant scanner-mode change can briefly restart scanning before it is
stopped again. This is not exclusive RF time or an established timeout fix.

The BLE stack, Improv and existing active proxy connections stay enabled. Those
connections can still generate Bluetooth traffic. Advertisements may be missed
while scanning is paused. Each completed or failed burst releases its own pause;
scanning can resume between automatic repeats. Switching the option off also
releases a pause without waiting for RF completion.

The guard only owns scanning it observed running and actually requested to stop.
After that, it restores this firmware's configured continuous-scanning policy;
the pinned tracker API cannot reveal an externally changed continuous policy.
It never enables an externally disabled BLE stack or forces a previously idle
scan it did not own on. If BLE is disabled externally during an owned pause,
only the overridden continuous policy is restored, so a future external BLE
re-enable is not stranded with scanning off. Restoring a stop still in progress requests continuous scanning immediately,
then lets the native tracker finish its asynchronous transition; underlying BLE
stack failures are not repaired or hidden by this feature.

**Bluetooth RF scan pause** reports the current guard/scanner state. The native
`rf_targets_status` response additionally includes the enabled setting, state and
pause-request count since boot. A `scanning` readback means the tracker reported
RUNNING, not that a particular Bluetooth device was received. Verify actual BLE
advertisement/connected-device activity before accepting a hardware trial. OTA
reboots clear volatile radio faults, so compare equivalent relay policy/settings
and do not attribute an improvement after a reboot to scan pausing alone.

The pinned ESPHome 2026.4.1 tracker skips idle work until its state version
changes. A guarded PlatformIO pre-build compatibility script changes only the
generated build copy of `set_scan_continuous()` to wake that native loop when
the policy changes. The RF component never calls `start_scan()` directly;
native connecting/disconnecting/discovered-client guards remain in control.
The installed ESPHome package is untouched. The hook runs through ordinary
`esphome compile`; another version or unexpected tracker source fails the build
rather than applying an unverified patch. Builds without `ble_tracker_id` omit
the hook and Bluetooth-specific includes.

### Limited hardware trial, 13 October 2026

The local candidate was installed on one bridge with 14 commissioned targets
preserved. With automatic repeats disabled for the comparison, one four-panel
Close/Open pair passed with the pause option off. With it on, Close moved all
four but Open missed one panel despite successful API completion. This sample
does not establish a delivery benefit or regression. The prior firmware also
missed a panel in a separate trial; its fault latch and the OTA reboot confound
comparison between firmware versions.

During a subsequent deliberate Open restoration with normal automatic repeats
enabled, the guard paused for the original burst and both repeats, resumed
between them, and returned to scanning afterwards. All four panels were open
after the observation window. Fresh advertisements reached HA. No active GATT
connection was exercised, so uninterrupted connected-device operation remains
unqualified on hardware. The option remains experimental, not a demonstrated
repair for missed movement or intermittent native API status timeouts.

Eight native suites, seven existing Python tool tests, five compatibility-hook
tests, eight native tracker-loop scenarios and the full pinned firmware build
passed. Main promotion and wider rollout remain on hold pending RF reliability
acceptance.

## Transmitter-completion failure probe (0.11.3-experimental candidate)

A `radio_tx_failed` event now emits one bounded ERROR line before cleanup clears
radio interrupts/FIFO and restores receive mode. It contains the original and
later STATUS, FIFO_STATUS, CONFIG, EN_AA, SETUP_RETR, RF_CH and RF_SETUP registers,
completion-wait and probe durations in microseconds, selected-target ordinal,
selected-target count and remaining rounds. No frames, target identities, names
or rolling counters are logged. The snapshot is a short sequence of reads,
not an atomic capture.

Original STATUS bit `0x20` is TX_DS (local packet transmission completed) and
`0x10` is MAX_RT. With automatic acknowledgement/retransmission configured off,
MAX_RT is unexpected and deserves comparison with EN_AA/SETUP_RETR. If TX_DS is
absent in the original STATUS but present in the later read, completion arrived
(or became visible) after the bounded poll ended. That distinguishes a late
local completion from a STATUS that remains without TX_DS; it does not establish
why the earlier poll missed it. CONFIG, RF_CH and RF_SETUP help identify an
unexpected mode/channel/setup or apparent radio reset. All-zero/all-one reads
may also indicate SPI or power trouble and are not conclusive by themselves.

The original two-millisecond completion poll, RF power, packet cadence, counters
and failure handling remain unchanged. The probe adds read/log work only after
the failure has already been decided. Even if later STATUS shows TX_DS, the
original failure is retained: no retry, false success or movement acknowledgement
is introduced. This is diagnostic instrumentation, not a reliability fix; the
separately reproduced receive-FIFO channel-attribution defect is not changed.

## Contiguous batches and stale-status read (0.11.4-experimental candidate)

A multi-target batch now gives each target one contiguous block of rounds instead
of interleaving every target in each 55 ms round. Total rounds, copies per round
and airtime are unchanged; uneven splits differ by one round. Single-target
commands and relays behave as before. Separately, when the two-millisecond
completion poll ends without TX_DS or MAX_RT, the sender reads STATUS once more
before declaring `radio_tx_failed`. That removes a reproduced stale-sample false
abort without resending a payload; MAX_RT and missing completion still fail.

Installed on the downstairs bridge on 13 October 2026 with all 14 target records
and counters unchanged. With automatic repeats and downstairs relay off, the
four-panel lounge room batch still missed one panel on Close and one on Open.
Contiguous blocks are therefore not a delivery fix. The same panels sent as four
separate single-target commands moved 24 of 24 times over three close/open pairs.
The Norman HA integration now sends room sections one at a time; multi-target
batches remain available to other callers. Eight native suites, the completion
probe harness and the pinned ESPHome 2026.4.1 build passed.
