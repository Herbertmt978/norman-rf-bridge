# Hardware and verified wiring

This repository owns the bridge firmware, hardware description, wiring and
installation tools. The separate Norman HA integration consumes its ESPHome API.

## Tested prototype

| Part | Observed hardware and qualification boundary |
| --- | --- |
| Controller | Freenove ESP32 development board, ESP32-WROOM-32E shield,4MiB flash, USB-C power/data |
| Radio | SMA-antenna PA/LNA module sold as nRF24L01+; photographed radio IC is marked SI24R1. Describe it as nRF24-compatible, not verified genuine Nordic silicon. |
| Antenna | Attached2.4GHz SMA antenna on the radio module |
| Connections | Jumper-wire SPI prototype using the pin map below; no custom production PCB |
| Power | USB powers the ESP32 board. The radio requires regulated3.3V and common ground; this prototype has not qualified a production PA/LNA supply. |

Both prototypes have been tested with their own antennas. Results do not qualify
another module vendor, antenna gain or supply. Keep installation
identifiers and private commissioning captures outside this source repository.

## Parts used in the prototype

The owner supplied these purchase links for the actual build (not affiliate
links or a guarantee of current seller specifications):

- [Freenove ESP32 board kit, ASIN B0C9THDPXP](https://www.amazon.co.uk/dp/B0C9THDPXP).
- [PA/LNA radio purchase, ASIN B0DK2Z6C7K](https://www.amazon.co.uk/dp/B0DK2Z6C7K).

The assembled prototype uses the ESP32 board, radio with its 2.4 GHz antenna,
jumper leads and USB power. No additional regulator/capacitor assembly was
reported fitted. The power improvements below are recommendations for further
qualification, not claims about parts already installed. The Amazon pages could
not be independently retrieved during publication; the supplied order photos
and board markings are the hardware evidence. Seller substitutions are possible.

## Pin map

This mapping is verified by read-only SPI register access and is used by the
Stage 1 monitor firmware. Stage 0 remains a radio-free rollback image.

| nRF24L01+ pin | ESP32 connection | Notes |
|---|---|---|
| GND | Common ground | Join ESP32 and external regulator grounds. |
| VCC | Clean regulated 3.3 V | Never connect the bare module to 5 V. |
| CE | GPIO4 | Tested monitor enable. GPIO4 is strap-sampled at reset but does not select a supported ESP32 boot mode. |
| CSN | GPIO5 | Tested SPI chip-select. GPIO5 is strap-sampled at reset; retain the nRF24 input-only connection and prove cold boot/flash reliability before product use. |
| SCK | GPIO18 | ESP32 VSPI clock. |
| MOSI | GPIO23 | ESP32 VSPI controller-to-radio data. |
| MISO | GPIO19 | ESP32 VSPI radio-to-controller data. |
| IRQ | GPIO27 | Reserved; current firmware polls the RX FIFO and does not use IRQ. |

The PA/LNA board can draw sharp current peaks. Use a dedicated, stable 3.3 V
regulator and place 100 nF ceramic plus 47–100 µF bulk capacitance at the radio
connector. Attach the SMA antenna before any transmit-capable firmware is used.

The owner-supplied [enclosure STEP files](../hardware/enclosure/README.md) are
stored in this repository. The owner confirms the printed enclosure fits and
works with the prototype. Print settings, electrical clearances and thermal
performance are not separately documented or qualified.

The mapping assumes the module shield reads `ESP32-WROOM-32E`. Recheck that
marking before wiring; ESP32-WROVER boards use some pins for PSRAM.
