# Will it work in another home?

The reusable part is the firmware and radio protocol. The installation-specific
part is the set of commands for your shutters. You can flash the same factory
image onto another board, but it won't arrive knowing which motors to control.

So far, two ESP32/nRF24-compatible boards have been tested in one Norman Gen1
installation. Community work provides further evidence for this protocol family,
but not a compatibility guarantee for every Norman product. A second household
test is still needed before describing setup as generally proven.

## Does each panel use different RF?

Each independently controlled section needs a separate profile. The tested
upper/lower shutter arrangement has more motorised sections than physical
window frames, so count the motors you can control, not just the windows.

The observed sections use the same packet format and radio settings. Their
messages contain different identities/selectors and operations, plus rolling
fields that change between commands. This firmware stores the exact learned
Open and preferred Close templates for each section, with an optional opposite
closing direction. It also stores the sequence needed for the next direct command.

The code understands that format; it doesn't guess another home's addresses.
The capture-to-profile process also establishes which physical panel a command
controls. Neither an RF number nor a label imported from a hub proves where the
panel is mounted.

## Do I need a Pluto SDR?

Not as a normal part of operating the bridge. For the supported radio settings,
the nRF24 receiver can capture decoded packets itself, even before any shutter
profiles have been commissioned. Its private Wi-Fi log includes channel numbers
and payloads marked `RX valid` when the application frame passes validation.

The current receiver uses the community sketch's five-byte radio address,
1 Mbps operation and candidate channels 15, 39 and 59. These are built into the
driver; it isn't a general scanner for unknown addresses or modulation. With
relay enabled it concentrates on channels 15 and 39. A new, uncommissioned unit
does not transmit simply because it heard something.

A Pluto was used here to investigate radio settings and independently compare
transmissions. That research is reusable. You don't need to repeat the entire
SDR investigation for every panel if your packets match the supported format.
If they don't, an SDR may help determine why; buying one is not the first setup
step. No captured packets could also mean poor reception, wiring, power, channel
timing or a different controller source, rather than incompatible shutters.

## Guided setup in Home Assistant

Firmware0.10.0 and the matching Norman integration provide a [guided learning
and profile-management flow](learning.md). It captures actions on the ESP,
requires physical confirmation and creates named panel/room controls. It also
supports relay-only actions and explicit removal. The flow does not decode
unknown radio protocols or infer room membership without user input.

## Advanced manual commissioning

1. Keep a working original controller available. Place the ESP/radio near it,
   with the antenna fitted and reliable power. Leave relay disabled while
   identifying captures.
2. Use the [bounded passive logger](commissioning.md#passive-wi-fi-logs).
   Operate one section and one direction at a time, noting the time and movement.
   Capture Open, the preferred Close direction and any other supported endpoint.
   Keep the source consistent: don't mix an app/hub capture with a different
   remote's frames and assume they belong to the same command family.
3. Validate the frames and their relationship. The log carries a 32-byte radio
   payload; this protocol uses the first 30 bytes as the application frame.
   The last two bytes are padding in the tested implementation. The command-line
   commissioning actions take 60 hex characters for the application frame.
4. Establish the current rolling index using the protocol decoder, not a guessed
   number or a copied value from another home. The byte-25 permutation and
   validation code live in `protocol/`; the hub's two rolling bytes are not
   simply interchangeable. The guided flow handles this for supported captures.
5. Save the panel with `commission-target`, using a stable slot, physical name,
   room, templates and current index. Read `targets` back to check the identity
   and supported endpoints. See the [command reference](commissioning.md#explicit-per-installation-learning).
6. Test that panel in both directions under observation and restore its starting
   state. Only then add more panels and use the integration's room controls.

This advanced route still needs somebody comfortable with logs and command-line
tools. Most supported installations should start with the HA wizard instead.
Neither route automatically authorizes every shutter in radio range.

## Repeating versus replacing the hub

For **repeating**, learn the command families the board is allowed to forward,
then enable its autonomous relay. It forwards a newly heard matching command
without inventing a fresh rolling code. Native room commands need separate
relay-only entries; learning individual sections doesn't automatically permit
every room operation.

For **direct HA control**, learn each motorised section and assign each room to
one direct-command bridge. HA sends Open/Close to that bridge, which creates
the next valid learned command. It can replace the hub's role in those HA
actions without needing the hub reachable over RF. The current whole-room
control is an interleaved batch of individual section commands, not a generated
native room packet.

Use other bridges to repeat those frames, not to independently generate commands
for the same profiles. They could otherwise compete over rolling state. The HA
adapter prevents duplicate direct bindings within one HA instance, but it can't
coordinate independent HA instances or manual native actions elsewhere.

The original hub and app can remain available. Their schedules are not disabled
by adding this integration, and their displayed positions may be stale after
an ESP command. Keep competing schedules off during testing. Long-term
coexistence and missed-command recovery remain areas for further testing.

## Sharing a build

Share the firmware, wiring and enclosure files. Don't share commissioned full
flash images, household captures or saved rolling state. Each installation
needs its own learning and checks, and the current unauthenticated bench image
needs better provisioning before it is suitable for sale.
