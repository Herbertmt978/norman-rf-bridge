# Prototype enclosure

Owner-supplied STEP originals, added 12 September 2026:

| Part | File | Size |
| --- | --- | ---: |
| Base | [Print Base Final.step](Print%20Base%20Final.step) | 41,938 bytes |
| Top | [Print Top Final.step](Print%20Top%20Final.step) | 14,423 bytes |

These files are small enough to store directly in Git; Git LFS or external
hosting is not needed. Both are STEP AP214 exports with millimetre geometry
units, exported on 11 September 2026. The originals are retained byte-for-byte.

SHA256 checksums:

```text
9be7ac4f990673bee6f19e478c7d4ce23a979b6654de5eac412a7a8af830f098  Print Base Final.step
4fdfd57ec9b67c29abea7e3dbea5255121f62da969045b637946aaa0161d31f8  Print Top Final.step
```

Import these into a STEP-capable CAD application, inspect the dimensions and
export a mesh supported by your slicer. Do not assume the filenames establish
a tested final product: assembly fit, fastening, print orientation, material,
wall strength, electrical clearance and thermal performance are not qualified
by adding these files. No print settings or STL files are supplied yet.

Check fit against the actual [board, radio and wiring](../../docs/wiring.md).
Keep the USB connector accessible and both the ESP32 antenna area and the
external 2.4 GHz antenna clear. Avoid pinching jumper wires or leaving exposed
conductors able to short. Fit the radio antenna before powering a transmit-capable
build. An enclosure is not a substitute for a stable 3.3 V radio supply.
