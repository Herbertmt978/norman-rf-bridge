# Experimental commissioning

## Build and install

Use the tested ESPHome2026.4.1 and the Freenove ESP32-WROOM/4MiB hardware in
[wiring](wiring.md). The PA/LNA module needs an attached antenna and a stable
3.3V supply; never apply5V directly to the radio.

1. Run `scripts/test-protocol.ps1` and `scripts/build-stage1.ps1`.
2. Flash a new board with `scripts/flash-stage1.ps1 -Port <verified-COM-port>`.
   Despite their historical names, these two scripts now build/install the
   current experimental RF image, not the old receive-only baseline.
3. Provision Wi-Fi with USB/Bluetooth Improv or the temporary fallback AP and
   adopt the discovered ESPHome device in HA. Complete this on a trusted LAN.
4. Verify radio-ready diagnostics. An uncommissioned profile must remain
   unavailable for direct control and relay.
5. Capture each section's open and preferred close commands with the original app/hub,
   keeping raw captures private. Confirm physical action, CRC and target matching.

Builds and packages stay under `D:/CodexBuild/norman-rf-bridge`, outside Git.
`scripts/package-experimental.ps1 -AllowDirty` makes a labelled development
bundle with a source archive and file hashes. Omitting `-AllowDirty` requires a
clean checkout. A local bundle does not publish a GitHub release.

## Explicit per-installation learning

Use `scripts/bridge-control.py --host <IP> --expected-name <ESPHome-name>` with
one operation below. Inputs are captured60-character application-frame hex,
not example commands from someone else's installation.

| Operation | Additional arguments | Effect |
| --- | --- | --- |
| `targets` | none | Read inventory, slots, fingerprints and capabilities |
| `commission-target` | `--slot`, `--name`, `--room`, `--open-frame`, `--close-frame`, `--last-index`, `--open-position`, `--close-position` | Persist a target in slot0–31; preferred close is0 or100 |
| `learn-target-endpoint` | `--slot`, `--position`, `--frame` | Save a physically correlated opposite closing template |
| `target-command` | `--slot`, `--profile-id`, `--position` | Identity-checked command for a learned endpoint; automatic repeat policy applies |
| `relay` | `--enabled on` or `off` | Persist the local autonomous repeat policy |
| `relay-endpoints` | none | Read the separate receive-only allowlist and relay counters |
| `learn-relay-endpoint` | `--slot`, `--name`, `--frame` | Persist one receive-only command endpoint; never transmits the supplied frame |

### Native room repeating (0.8 experimental)

Native room commands are not individual section targets. Learn each captured
room operation in the separate relay-only namespace using `learn-relay-endpoint`.
It has32 independent slots and is not returned by `targets`, so it cannot create
a motor cover, enter room fan-out or reserve an outgoing rolling code.

An entry matches only the exact CRC-valid learned30-byte application command,
allowing variation in bytes24/25 and their dependent CRC. Only a newly received
matching frame may be forwarded unchanged through the existing39-to59 relay.
The saved frame is a matching template, not a replay queue. Learning alone never
transmits. Other room operations, selectors, replies and unlearned families are
not granted permission. Existing endpoint identity cannot silently be replaced.

The same opt-in relay switch, duplicate cache and fault policy apply. Native
room support still needs physical and independent RF qualification on each
installation; accepting a template is not proof of room-wide movement.

Keep slots stable. Each target owns its counter and exact endpoint templates.
Names and room labels are printable ASCII, at most47 characters. Matching room
labels create room covers in HA. Recommissioning an existing matching target
updates labels without rewinding its counter or deleting its optional endpoint.
Duplicate families in different slots are rejected. Do not recover corrupt
rolling state by erasing NVS or restoring a stale full-flash backup.

Legacy `status`, `commission`, `command` and `learn-close-up` operate on slot0
only for existing native callers; they are never whole-room broadcasts.

### Verify physical names before HA discovery

An imported hub label is not proof of physical location. From a known starting
state, operate one identity-checked slot, ask which section moved, then restore
that same slot and confirm it. Record the observed location with its slot and
fingerprint. Do not infer the final location by elimination or use assumed HA
cover states as motor feedback.

For an already commissioned matching slot, `commission-target` can save its
confirmed display name using the same captured frames, room and endpoints.
Read the live inventory first and supply its current index; existing matching
slots retain their stored counter and optional endpoint. This is a metadata
operation, not a movement command. Keep the original captures unchanged.
Read back all targets and verify that only the intended names changed, with
fingerprints, counters, rooms, endpoint capabilities and relay policy retained.
Stop on any mismatch; do not restore old rolling state as a rollback.

The Norman RF integration imports these names during setup. For an existing RF
entry, use **Reconfigure** on that same bridge to refresh its saved metadata.
Display names are not polled into the bound entry automatically. Section unique
IDs are slot-based, not name-based; keep slots fixed. A room label is also a
grouping input, so a room reassignment is not merely a panel display-name edit.
Existing hub entries are separate and are not renamed by changing ESP metadata.

Firmware0.7 native protocol3 adds `rf_targets_command(targets_json)`. Its JSON
object has equal-length arrays `slots`, `positions`, `profile_ids`, with1–8
unique targets. Every identity/endpoint is checked and every counter is durably
reserved before the first RF packet. The single scheduler sends each target
once per55ms round, for100 copies each. Any failure leaves all selected physical
states uncertain. HA does not retry the action. Firmware 0.10.0 can schedule
additional identical bursts after successful local transmission, as described below;
a transmitter failure cancels them.

The optional encrypted-API key is read only from `ESPHOME_NOISE_PSK`. The current
bench image is unencrypted; this option supports a later owner-secured build.

For the locally tested section raw open is37, close-down0 and close-up100.
These are not HA percentages: HA's RF cover exposes open/close only, not a
position slider. Do not extrapolate these values to another motor or blind type.
The upward-close capture changes bytes2/3 as well as its difference from open
at byte4. Runtime matching compares the entire learned command body excluding
only rolling fields and the recalculated CRC, not a guessed universal opcode.

## Verify each commissioned unit

- Physically observe each supported direction, then restore the starting state.
- Reboot and verify the saved panel, counter and relay-enabled state; boot must
  not emit a movement command.
- Confirm source channel15 produces one bounded unchanged channel39 relay and
  source channel39 produces a channel59 relay. Confirm echoes and channel59
  inputs are not forwarded. Measure each placement; do not claim an all-house
  test from one received frame or one successful movement.
- Disconnect Wi-Fi temporarily and confirm local repeating continues. The
  commissioning `rf_test_wifi_pause` action pauses Wi-Fi30seconds, then enables
  it; a local recovery script restarts after another45seconds if still offline.
- Verify HA/API recovery, Bluetooth availability, power integrity and fault
  diagnostics before leaving a unit unattended.

## Home Assistant Norman RF transport

The candidate in `Herbertmt978/Norman-HA-Integration` adds **ESPHome RF bridge
(experimental)** as a separate setup choice. Adopt ESPHome first, then select
its commissioned bridge. It creates section and room covers using each target's
preferred closing direction. A room can contain up to8 interleaved targets.
This needs neither hub credentials
nor a hub fallback. Existing hub covers remain separate and may show stale
assumed states after direct RF movement.

The bridge owns counters/templates; HA pins target slots, fingerprints, room
membership and endpoints. Changed commissioning or a missing bridge makes
affected covers unavailable. Reconfigure deliberately refreshes those bindings.
State is unknown on startup and explicitly assumed after a completed RF burst.
There is no physical position or battery feedback through this RF transport.

## Recovery and limits

### Automatic command repeats

Firmware **0.10.0-experimental** changes automatic repeats to a two-second
quiet gap after each completed burst. Version0.9.2 used20-second start intervals.
The **RF automatic command repeats** switch on each ESPHome device defaults
to ON. It is separate from **RF autonomous relay**, which controls forwarding
received traffic. Either switch can be used without the other.

After a successful local command, the ESP can send up to two more identical
bursts, waiting two seconds after each successful burst finishes. Each
burst retains the existing 100 copies per target and 55 ms round cadence.
The extra bursts reuse the original application bytes and rolling indices;
they don't reserve more counters or write the command back to flash.

The radio must be idle and the previous direct burst must have completed.
Busy periods defer the next attempt, but the whole budget expires 60 seconds
after the original start. There is no endless retry loop. Manual repeats share
the same two-attempt budget; the next automatic attempt waits two seconds after
the manual burst finishes.

A newer local command attempt cancels the old pending repeats, even if that
new attempt is rejected as busy. An already-running burst finishes normally;
it is not preempted, so a new HA command during that roughly six-second burst
may need to be sent again after completion. Reconfiguration, a changed learned
identity/counter, or an observed conflicting command also invalidates the cache.
The ESP cannot cancel in response to an external command it never receives.

Turning the automatic-repeat switch OFF clears pending repeats. Turning it
back ON also clears the shared cache, including any manual-repeat allowance
created by a command sent while automatic repeats were OFF. Only future
commands get automatic repeats. The setting uses normal ESPHome preference
storage and survives reboot once saved; allow the preference flush interval
before removing power. Cached commands never survive reboot. Startup alone
therefore cannot move a shutter.

Radio faults cancel pending repeats and leave the existing fault protection
in place. This update does not automatically rearm a failed radio or claim to
fix the earlier unexplained transmitter fault. Inspect the RF diagnostics.
The native `rf_targets_status` response also reports `automatic_repeats`,
`repeats_remaining` and `automatic_repeat_count` (started automatic bursts
since boot). The HA command still responds after the first burst; it doesn't
wait for the later bursts or obtain motor feedback.

The office/lounge sunrise/sunset automation in the test installation now uses
the local ESP room covers, at the owner's request. Its original sun triggers
are retained, with the lounge action five seconds after the office action
completes. That gap isn't a demonstrated reception fix. The earlier hub-based
configuration is backed up, and phone-app schedules were not changed.
Other installations must use their own verified cover IDs and schedule choices.

### Explicit identical-command repeat

Firmware0.9.1 adds `rf_targets_repeat`, a manual diagnostic action with the same
`targets_json` shape as `rf_targets_command`. Supply the identical slots, order,
positions and profile IDs from the most recent completed local command. A repeat
sends the same30-byte application frames plus the same two padding bytes on
channel15,100copies per target at the existing55ms round cadence. It does not
reserve or advance any rolling index. The first command already sends100copies;
this action tests a later additional burst, not a previously absent copy loop.

At most two repeats are allowed within60seconds of the original burst starting.
The radio must be idle and the previous command must have completed. A new local
command, reconfiguration, radio failure, changed profile/counter or conflicting
observed command invalidates eligibility. Reboot clears the cache. Incoming RF
can invalidate the cache but cannot create repeat authority. An unseen external
command cannot be detected, so test promptly with other controls idle.

For one target originally sent on its own:

```powershell
python scripts/bridge-control.py --host BRIDGE_IP --expected-name BRIDGE_NAME repeat-target --slot SLOT --profile-id PROFILE_ID --position POSITION
```

Do not retry a stale command or run an unbounded loop. Identical duplicates may
be ignored by a motor that already accepted them, and may still fail if that
code is unacceptable. Autonomous relay retains its exact-frame duplicate cache:
a repeated local packet is not promised another forwarding burst at every bridge.
Observe actual movement and restore the starting state. HA room covers do not
automatically call this action; firmware 0.10.0 owns the background repeat policy
above. Long-term unattended delivery remains unqualified, despite the owner's
decision to begin using the direct ESP schedules.

### Passive Wi-Fi logs

USB power alone is sufficient once Wi-Fi has been provisioned. For a bounded
read-only log recording, without sending a radio or movement action:

```powershell
python scripts/bridge-log.py --host BRIDGE_IP --expected-name BRIDGE_NAME --seconds 60 --output D:\PrivateCaptures\bridge.jsonl
```

Create the private destination directory first. The file must not already
exist. Logs are timestamped at the PC and may contain installation-specific
RF frames; do not commit them. The default cap is16MiB; recordings are limited
to one hour and64MiB. The tool disconnects when its duration or byte cap is
reached. A quiet log is not proof that no RF was present: radio tuning, reception
and firmware log budgets also affect visibility.

Both prototypes were updated to0.9 on12September with their14 target identities
and two native-room receive endpoints retained. Both have their own antennas
and relay enabled. This supersedes the earlier antenna-loan state below.

`esphome/stage0-recovery.yaml` and the `*-stage0.ps1` build/flash/OTA scripts use
the original radio-free0.2.0-stage0 image. They do not accidentally build the
current RF YAML. Recovery does not erase stored NVS; returning to an RF image
can restore its commissioned policy. Disable relay explicitly before ordinary
maintenance where that is required.

The old `scripts/esphome-rf-test.py` is historical0.4 bench tooling; its raw
transmit API is absent from current firmware. Use the learned-command controller.
The second prototype was updated on 10 September 2026 to the same 0.8 image,
then commissioned with an attached test antenna: 14 profiles, the pilot's extra
closing direction and two native-room relay endpoints. Relay policy and profiles
survive reboot; fresh over-the-air qualification remains pending. The first
bridge's relay was disabled while its antenna was loaned to the second.
There are32 target
slots and14 commissioned sections across3 rooms in the current installation.
Only the original pilot has both closing directions learned; other sections
currently support their preferred close only. Capacity is not physical range
evidence. See the security, coexistence and commercial gaps in
[product architecture](product-architecture.md).
