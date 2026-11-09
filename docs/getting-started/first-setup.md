# First-time setup

A freshly flashed tracker uses the placeholder callsign `NOCALL-7` and **will not transmit** until you set your own callsign. At minimum, set:

- your **callsign** with SSID (for example `KJ7NYE-7`)
- the **device role** (tracker, iGate or digipeater)
- the **LoRa radio settings** used by your network

> [!WARNING]
> Every device on a LoRa APRS network must use the same frequency, spreading factor, bandwidth and coding rate. This firmware's defaults are tuned for US 70 cm use (433.775 MHz, SF8, 62.5 kHz, CR 4/5) and differ from stock CA2RXU firmware. Match your local network before transmitting.

Pick any one of the configuration methods below. They all edit the same settings.

=== "Web UI (WiFi)"

    Works on every board with WiFi (all except the Heltec T114).

    1. On first boot the callsign is `NOCALL-7`, so the device starts its configuration access point automatically.
    2. Connect to the `LoRaAPRS` WiFi network (password `1234567890`).
    3. Open `http://192.168.4.1` in a browser.
    4. Set your callsign, role, frequency and path, then click **Save**.
    5. The device reboots into normal operation.

    To reopen the web UI later, hold the **USR** button for 8 seconds or more while the device is running. The access point shuts down and the device reboots after 2 minutes with no client connected.

=== "Serial configuration tool"

    Works on every board, in Chrome or Edge 89+.

    1. Connect the device over USB.
    2. Open the [serial configuration tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html).
    3. Click **Select Port** and choose the device's USB serial port.
    4. The tool enters setup mode and reads the current configuration.
    5. Edit fields. Each change is sent immediately.
    6. Click **Save** to keep the changes, then **Reboot** to apply them.

=== "Android app"

    Install the [FieldOps configuration app](https://github.com/jeepnjonny/lora-fieldops-config-android/releases). It connects over USB, or over Bluetooth once the tracker's [pairing PIN](../guide/bluetooth.md) is enabled.

=== "Serial CLI"

    Open any serial terminal at **115200 baud**, type `setup` and press Enter, then:

    ```text
    beacon callsign KJ7NYE-7
    role set tracker
    lora freq 433775000
    beaconpath WIDE1-1
    save
    reboot
    ```

    See the [Serial CLI reference](../reference/serial-cli.md) for every command.

## Check it works

- Press the **USR** button briefly to send a position beacon. See [Button, display & LED](../guide/button-display.md).
- Look for your callsign on [aprs.fi](https://aprs.fi) if an iGate is in range.
- If the tracker has no GPS, set a fixed position (`role gps fixed`, then `fixed latitude` / `fixed longitude`), or feed it a position from a phone app over [KISS](../guide/kiss-tnc.md).

## Back up your configuration

Once the device is set up, save a copy of its configuration. You can restore it later or copy it to other trackers:

- **Web UI:** use the JSON configuration backup and restore option.
- **Serial CLI:** `export` prints the configuration and `import` loads one. See [Cloning a device](../reference/serial-cli.md#cloning-a-device-via-export-import).
