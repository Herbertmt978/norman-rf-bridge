# Set up and manage your shutters in Home Assistant

Use bridge firmware **0.10.2-experimental** with the matching Norman integration
update for this flow. Version 0.10.0 introduced learning but has the hub-capture
limitations described below. Older bridges keep the existing manual
commissioning route. This is for the supported Norman Gen1 RF protocol; a
second household has not yet been qualified. A Pluto SDR is not required when
your controller uses the supported radio settings.

## Install and connect

Fit the radio antenna before powering the board. Follow the [wiring guide](wiring.md),
install the firmware, provision Wi-Fi and add the discovered device to HA's
ESPHome integration. Keep your existing Norman controller working.

Install the matching **Norman 0.5.0b2 candidate** from the
[integration's main branch](https://github.com/Herbertmt978/Norman-HA-Integration/tree/main).
[PR18](https://github.com/Herbertmt978/Norman-HA-Integration/pull/18)
was merged on 12 September 2026. As of 13 September, the normal HACS release is
still v0.4.1 and does not contain this flow.
For a manual candidate install, back up your existing integration folder, copy
main's `custom_components/norman_gen1` directory into HA's
`config/custom_components/norman_gen1`, and restart HA. A subsequent HACS update
can replace a manual candidate, so check the installed version before learning.

In Settings → Devices & services → Add integration, choose Norman, then
**ESPHome RF bridge**. Select the adopted bridge. The profile-management menu
appears even when the board has not learned anything yet.

For a bridge already configured in Norman, use its **Reconfigure** menu. You
do not need ESPHome YAML, packet logs or a terminal for these steps.

## Add a panel

Count independently controlled motors, not window frames. A shutter with
separate upper and lower sections needs two profiles.

1. Choose **Add a motorised section**. Give it a name and room, such as
   `Bottom left` and `Office`. Choose the closing direction you want HA Close
   to use, and whether to learn the opposite closing direction too. Choose
   **Upwards** if that is your blackout position; do not teach a downward
   close while the wizard asks for upward close.
2. Let earlier movements and repeats finish before starting. When the page says
   **Open**, press that individual section's Open action
   twice on the same original controller, at least eight seconds apart. Check
   that the intended section moves, then select Continue within one minute.
3. Follow the Close prompt in the same way. Do not switch between a physical
   remote and the app/hub during a learning session. Their messages may belong
   to different command families. Do not press whole-room controls here.
4. Confirm the physical target and actions, then save. The bridge stores the
   validated templates and current rolling state. Saving itself sends no RF.
5. Add the remaining sections, then choose **Finish: select rooms for HA**.
   Each selected room gets Open/Close, alongside each individual section.
6. Watch the shutters while testing their new HA controls. Successful packet
   transmission is not confirmation of movement. Restore their starting state.

The controller can be the Norman app/hub, a physical remote, or an existing HA
hub control that sends the requested action. Check the actual closing direction:
HA's existing Close button may be configured for the opposite endpoint. A slider
at 100% normally means Open in HA, not the Norman protocol's upward-close value.
Use the Norman app when the existing HA control cannot send the required direction.

For learning, put the ESP where it receives the controller clearly, initially
near the hub if necessary. After saving, move it near the shutters for direct
control and test again. HA reaches it over Wi-Fi; the hub does not have to reach
that room for direct ESP commands. Repeating is different: a relay still has to
hear the upstream radio. A failed distant-room movement is not, by itself,
evidence that learning failed.

Each capture requires two distinct, matching samples at least eight seconds
apart, not repeated copies of one radio burst. Version 0.10.2 remembers sample
codes heard in the first burst so their later echoes cannot count again. It
does not require the hub's decoded codes to advance within a particular range.
The final sample supplies the initial rolling state; this is not a new universal
rolling-code rule or a change to normal transmit-counter handling.

Open establishes the target/controller identity. The bridge keeps up to eight
action candidates and accepts only one with two matching samples. A one-off
message for another target does not spoil the capture. Two qualifying candidates,
conflicting actions for the selected target, or too much distinct traffic cause
rejection rather than a guess. Candidate evidence freezes after its second
sample, but the bridge keeps checking for competing candidates until Continue.
Close then listens only for the accepted Open identity: traffic for another
target supplies no evidence. For a rejected capture, choose Retry and use only
the requested action. A capture lasts one minute and the whole
session expires after ten minutes. Cancel discards unsaved captures. Existing
profiles are never overwritten by learning a duplicate.

While learning, this bridge's direct transmissions, automatic repeats and relay
transmissions pause. Its saved relay/repeat preferences do not change. Other
bridges and Norman hardware continue operating. Reboot discards the learning
session; completed profiles survive. Radio reception must reach the learning
bridge, so initially place it near the original controller.

### Fresh-board testing and remaining limits

On 13 September 2026, a new board was installed and added through HA with no
copied profiles. Teaching it with individual commands from the existing hub
did not complete reliably: capture windows also heard a different valid packet
family, including delayed traffic. Moving the board beside the hub allowed one
Open capture, but did not eliminate the problem. In a separate Close window,
two distinct same-action frames did not satisfy the learner's forward rolling
index check. A later quiet-window retry reached confirmation, but the unsaved
trial was discarded rather than treated as a reliable onboarding pass.

An initial 0.10.1 candidate still rejected background traffic arriving between
the two Open commands. Version 0.10.2 uses the bounded candidate rules above.
Synthetic regression tests cover the observed code jump, background traffic in
either order, competing targets, multi-code burst echoes, wrong panels, conflicting
actions, capacity limits and expiration.

With 0.10.2, all five bedroom sections were saved through the actual HA wizard
using hub commands: all ten Open/Close capture steps completed without retries.
No profiles were imported and no SDR was needed for that learning pass. The five
profiles survived a power cycle and created individual covers plus a room cover.
Some direct movements were unclear from the camera while the ESP was still
beside the distant hub. It was then moved into the bedroom for direct control.

The owner wanted upward closing for blackout. All five profiles were removed
and relearned through the wizard with Upwards selected, using the hub's Open
and upward-close actions. Two capture windows expired while checking the first
replacement; confirming promptly completed it. The other four replacements
accepted both actions on their first capture attempts. This distinction matters:
the ten-step, no-retry result above was the initial downward-close learning pass.

All five upward-close profiles then passed watched individual ESP Close/Open
tests, followed by a second successful whole-bedroom Close/Open cycle. Across
the two room cycles, all five sections closed upwards and reopened; no extra
manual command retry was needed. The final state was open. HA called only the
ESP covers for these movement tests, with the configured two automatic repeats.
The original hub and repeaters remained powered, so this proves direct command
generation without a hub API call, not an exclusively ESP radio path. HA still
has no motor feedback, and these short watched tests do not establish unattended
or whole-house reliability.

This is a fresh-device test in the same installation, not universal hub
compatibility or a qualified second household. Keep the original controller
available and cancel if the intended action cannot be identified consistently.

## Add a repeater action

Choose **Add an action for repeating**. Name a single action, such as `Office
Open`, and press it twice when prompted. Confirm and save it. Repeat separately
for Close and other required actions. You can learn native room actions this
way without pretending that their direct-generation rolling rules are known.

Enable autonomous repeating on confirmation, or later with the ESPHome device's
**RF autonomous relay** switch. A relay-only profile does not create an HA cover.
It forwards fresh matching frames unchanged, locally, even without HA or Wi-Fi.
Use **Finish without direct controls** for a repeater-only bridge.

## Rename, move or remove a profile

Open Norman → Reconfigure → select the bridge → **Rename, regroup or remove a
saved profile**. Choose the named panel or relay action.

Renaming or changing a room keeps the RF identity and rolling state. Existing
HA bindings refresh. Each directly controlled room currently supports up to
eight motorised sections. Changing a room name can change the room entity;
check automations referring to it.

The current edit screen changes names and rooms, not learned endpoints or the
preferred closing direction. To change direction, remove that profile and learn
it again with the desired direction selected. Do this one section at a time;
confirm the replacement works before removing the next. There is no one-click
undo, and references to the previous HA cover may need updating.

Removal requires a separate unchecked confirmation. It affects only that
profile on that bridge. A removed panel loses its bound HA cover, and its room
cover disappears if no bound sections remain. Other bridges, the original hub
and original controllers are not unpaired or erased. Update automations that
used the removed entities. The freed slot can be learned again; recovery is
relearning, not an undo button. Removing a profile from one repeater does not
remove copies from other repeaters.

## Position sliders and delivery limits

Open/Close are supported. The optional opposite endpoint is retained on the
bridge. A continuous percentage slider is not offered: the current encoder
only sends learned endpoints, not arbitrary verified positions. A captured Open
value of 37 is a protocol setting, not a measured 37% opening. A future position
feature needs validated intermediate-position commands; timed movement would
be an estimate and would need a verified Stop command too.

Automatic direct repeats keep the full 100-copy burst, wait two seconds after
successful completion, then repeat it, up to two extra bursts. Frames and
rolling codes are unchanged. New/conflicting commands, disable, faults and
reboot cancel pending repeats; work also expires after 60 seconds. The radio
cannot listen while transmitting. More bursts improve the chance of reception
but do not guarantee that every section moves.

Assign each panel one direct-command bridge. Other bridges can repeat its
traffic; they should not independently generate competing rolling sequences.
Keep schedules in the Norman app separate from HA automations to avoid
conflicting commands.
