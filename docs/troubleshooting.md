# Troubleshooting

## Device stuck in ROM download mode

**Symptom:** the board no longer boots into the firmware. Pressing RESET only prints `waiting for download` (or `boot:0x0 (DOWNLOAD(USB/UART0))`) on the serial console.

**Cause:** on the Heltec V3.2, LoRanger V1 and LilyGo T3-S3, the USR/BOOT button shares the ESP32-S3's GPIO0 boot pin. If GPIO0 is held low at the moment of reset, the chip boots into the ROM bootloader instead of the firmware. The usual trigger isn't the button itself. It's a serial monitor or terminal (Arduino IDE, PlatformIO monitor, a serial configuration tool tab and so on) still holding the board's USB port open, whose control lines can force the same reset-to-bootloader sequence used for flashing.

This is **not a hardware fault**. The chip is already in exactly the state needed to reflash it. To recover:

1. Close every program and browser tab connected to the board's serial port.
2. Unplug and replug the USB cable.
3. Press RESET. It should boot into the firmware normally.
4. If it is still stuck in download mode, reflash it with the [web flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/) or `esptool.py`. The board is ready to accept a flash in this state.

For the technical details of the serial control lines, see [USB serial connection quirks](reference/serial-cli.md#usb-serial-connection-quirks-dtrrts).

## Configuration reset after reflashing

The web flasher's **Firmware Update** mode updates the application only and keeps your saved configuration. **Fresh Install** writes the full factory image and resets configuration to defaults.

The flasher defaults to Firmware Update whenever an update image exists for your board. Switch to Fresh Install only when you mean to wipe the configuration, for example after a partition table change or to recover a corrupted filesystem.

> [!TIP]
> Keep a backup of each tracker's configuration (`export` in the serial CLI, or the web UI backup) so a reset is quick to undo.

## Tracker doesn't transmit

- **Callsign still `NOCALL`.** The firmware blocks all transmissions until a real callsign is set. Set it with `beacon callsign <CALL-SSID>` and `save`.
- **No GPS fix.** A tracker using internal GPS can't report a position until it has a fix. Check with `gps read`, move outside, or set a fixed position.
- **Radio settings don't match the network.** Every station must use the same frequency, spreading factor, bandwidth and coding rate. Compare with `show lora`.

## Display stays dark

- **Eco mode.** If eco mode is on, the display blanks after the timeout. Press the button to wake it.
- **Heltec V3 (not V3.2).** This firmware is built for the V3.2, which switches peripheral power the opposite way. On an original V3 the OLED never powers up.
- **LoRanger V1** has no display.

## Phone won't pair over Bluetooth

- Enter the six-digit passkey, padding a shorter PIN with leading zeroes (`1234` → `001234`).
- After a PIN change, forget the tracker in the phone's Bluetooth settings and pair again.
- Only one phone can connect at a time. Disconnect other apps first.
- Watch the pairing messages in `log` mode. See [Bluetooth log messages](guide/bluetooth.md#log-messages).

## WiFi configuration page doesn't appear

- The access point only starts on a `NOCALL-7` callsign or after holding USR for 8 seconds or more **while running**. Holding the button at power-up doesn't work.
- The access point shuts down after 2 minutes with no client connected. Hold the button again.
- The Heltec T114 has no WiFi. Use the [serial configuration tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html) instead.

## iGate shows `login=unverified`

The APRS-IS server rejected the passcode. It keeps the connection open but treats it as receive-only: uploads are tagged `qAO` (unverified) and messages from APRS-IS are not passed to RF. Clear any passcode override (`aprsiss passcode` with no value) so it is computed from your callsign, or enter the correct one, then save and reboot. See [iGate & APRS-IS](guide/igate.md).
