# Board notes

## Boot button on GPIO0

On the **Heltec V3.2**, **LoRanger V1** and **LilyGo T3-S3**, the USR button (BOOT on the T3-S3) is wired to GPIO0, the ESP32 boot pin. Holding it while powering up or resetting puts the chip into ROM download mode instead of running the firmware, so the configuration access point is triggered by an 8-second hold **while running**, never at boot. See [Troubleshooting](../troubleshooting.md#device-stuck-in-rom-download-mode).

## LilyGo T-Beam 1W

The T-Beam 1W has a cooling fan (GPIO41) and an NTC thermistor (GPIO14). The firmware controls the fan automatically:

| Condition | Fan behaviour |
|---|---|
| LoRa TX active | On immediately when TX starts |
| Within 30 s of TX end | Held on (post-TX cooldown) |
| Temperature ≥ 50 °C | On |
| Temperature < 42 °C and no TX cooldown | Off |
| Temperature ≥ 75 °C | On, and `!OVERTEMP` is added to the next beacon |
| Temperature ≥ 85 °C | Clean shutdown (same as a low-battery shutdown) |

Temperature is sampled every 30 seconds from a Murata NCP18XH103F03RB (10 kΩ NTC, B = 3380 K) through a 10 kΩ pull-down divider, using the ESP32-S3's eFuse-calibrated `analogReadMilliVolts()`.

The 1 W power amplifier means `lora power 20` gives about 30 dBm (1 W) at the antenna. See the [power table](../reference/serial-cli.md#lora).

## LilyGo T3-S3

Firmware target `lilygo_t3s3_433_aprs` supports the T3-S3 **V1.2 and V1.3** with the **SX1262** radio.

- **Buy the 433 MHz version.** LilyGo sells the T3-S3 with several radios and RF front ends. An 868/915 MHz board will tune to 433 MHz, but its band-specific filter and matching make the output very poor.
- **No onboard GPS.** Use a fixed position, the iGate or digipeater roles, or a phone app over Bluetooth or USB KISS. The external GPS header (GPIO 42/46) is noted in [`board_pinout.h`](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/variants/lilygo_t3s3_433_aprs/board_pinout.h) but not enabled.
- **Native USB.** There is no USB-to-serial bridge chip. If the first USB flash doesn't start, hold **BOOT**, plug in USB (or tap RST), then release BOOT. Later flashes are automatic.
- **WiFi firmware updates work.** The board has 4 MB of flash, but the ESP32-S3 build fits two firmware slots, unlike the 4 MB T-Beam and T3 V1.6.
- **Controls.** The BOOT button is the USR button. The green LED (GPIO 37) flashes on TX/RX. Battery is read on GPIO 1 through the onboard 2:1 divider; fine-tune it with the battery correction setting if needed.

## LoRanger V1

- **Headless.** The LoRanger V1 has no display. Use the status LED, the serial CLI, or the web UI.
- **Radio module.** The EBYTE E22-400M30S is sold as an SX1268 but ships with SX1262 silicon, so the SX1262 driver is used. This was confirmed by reading back the chip's version string during bring-up.
- **1 W output.** TX power is limited to 2–20 dBm in the settings; the module's amplifier brings the output to about 30 dBm.

## Heltec T114 (nRF52840)

- **No WiFi.** The T114 can't be an iGate and has no web UI. Configure it with the [serial configuration tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html), the serial CLI, or the Android app over Bluetooth.
- **Firmware updates** are by USB drag-and-drop (`.uf2`) or over Bluetooth with `otadfu`. See [Install firmware](../getting-started/flashing.md#heltec-t114-nrf52840).
- **Slower Bluetooth setup.** Long CLI output over Bluetooth takes a few seconds because of the default 23-byte packet size.

## Heltec WiFi LoRa 32 V3.2

The firmware targets **V3.2 hardware only**. See the warning on [Supported hardware](index.md).
