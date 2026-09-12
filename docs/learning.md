# Set up and manage your shutters in Home Assistant

The guided learning flow needs bridge firmware 0.10.0-experimental and the
matching Norman integration update. Older bridges keep the existing manual
commissioning route. This is for the supported Norman Gen1 RF protocol; a
second household has not yet been qualified. A Pluto SDR is not required when
your controller uses the supported radio settings.

## Install and connect

Fit the radio antenna before powering the board. Follow the [wiring guide](wiring.md),
install the firmware, provision Wi-Fi and add the discovered device to HA's
ESPHome integration. Keep your existing Norman controller working.

Install the matching **Norman 0.5.0b2 candidate** from the
[RF integration branch](https://github.com/Herbertmt978/Norman-HA-Integration/tree/Herb/esphome-rf-transport).
At the time of this update, [PR18](https://github.com/Herbertmt978/Norman-HA-Integration/pull/18)
is still unmerged: the normal HACS release does not contain this flow yet.
For a manual candidate install, back up your existing integration folder, copy
the branch's `custom_components/norman_gen1` directory into HA's
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
   to use, and whether to learn the opposite closing direction too.
2. When the page says **Open**, press that individual section's Open action
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

Each capture requires two distinct commands, not just repeated copies of one
radio burst. Mixed traffic is rejected: choose Retry, pause nearby schedules,
and use only the requested action. A capture lasts one minute and the whole
session expires after ten minutes. Cancel discards unsaved captures. Existing
profiles are never overwritten by learning a duplicate.

While learning, this bridge's direct transmissions, automatic repeats and relay
transmissions pause. Its saved relay/repeat preferences do not change. Other
bridges and Norman hardware continue operating. Reboot discards the learning
session; completed profiles survive. Radio reception must reach the learning
bridge, so initially place it near the original controller.

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
