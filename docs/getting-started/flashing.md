# Install firmware

Firmware for every board is published on the [Releases page](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases/latest). Most people should use the **web flasher**, which picks the right file for you.

## ESP32 boards

Applies to: Heltec V3.2, Heltec Wireless Tracker, T-Beam, T-Beam 1W, T3 V1.6, T3-S3 and LoRanger V1.

### Web flasher (recommended)

1. Connect the board over USB.
2. Open the [web flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/) in Chrome or Edge.
3. Choose your board and an install mode:
    - **Firmware Update** updates the application only and **keeps your saved configuration**. The flasher selects this by default whenever an update image exists for your board.
    - **Fresh Install** writes the full factory image (bootloader, partition table, firmware and filesystem) and **resets configuration to defaults**.

Use **Fresh Install** for a brand-new board, to recover a board that won't boot, or when a release changes the partition table.

### Release files

Two files are published for each ESP32 board:

| File | When to use | How |
|---|---|---|
| `<board>_firmware.bin` | **Normal over-the-air update** of a running device | Device web UI: `192.168.4.1` → **Device** → **Update Firmware** |
| `<board>_web_factory.bin` | **First-time flash or recovery** | USB cable and the [web flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/) |

> [!NOTE]
> The 4 MB **T-Beam** and **T3 V1.6** have a single firmware slot, so they can't update over WiFi. Update them over USB with the web flasher.

**How the WiFi update works.** The new firmware is written to the spare firmware slot and the device boots from it, so the running firmware is never overwritten mid-update. A failed upload leaves the device unchanged. If an upload fails, the dialog shows the reason, which also appears in the web live log prefixed `OTA:`.

**Why two files?** The factory image writes the entire flash from scratch. The `_firmware.bin` file is the application only, which is what the web UI update expects. Uploading the factory image through the web UI fails with "Not Enough Space".

## Heltec T114 (nRF52840)

| Method | Steps | File |
|---|---|---|
| **Web flasher (recommended)** | Connect over USB, open the [web flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/) in Chrome or Edge, choose **Heltec T114** and click **Install**. The flasher reboots the board into its bootloader itself, with no RESET double-tap needed. The bootloader reconnects as a new USB device, so the first time the browser asks you to pick a port twice. Your configuration is kept. | Fetched automatically |
| **USB drag-and-drop** | Double-tap **RESET**, then drag the `.uf2` file onto the USB drive that appears. | `heltec_t114_firmware.uf2` |
| **Bluetooth update** | Requires [*Require PIN*](../guide/bluetooth.md) to be on. In [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) (iOS/Android), first open **Settings → DFU options** and set **Packet receipt notification** to on with **6** packets (the T114's bootloader fails with "OPERATION FAILED" at the default 10). Then: (1) connect to the tracker, pair with the PIN, tap **DFU** and select the zip. The tracker switches to its update mode and nRF Connect usually stops with `GATT INVALID HANDLE`; that is expected. (2) Connect to the tracker again, choose **⋮ → Refresh device cache** (the service list shrinks to the DFU service), tap **DFU** and select the zip again. The upload takes about 5 minutes. Alternatively type `otadfu` in the [serial CLI](../reference/serial-cli.md) (USB, or the setup CLI over Bluetooth) and go straight to step (2). With the PIN off, only `otadfu` over USB can start a Bluetooth update. If an update fails part-way, the tracker stays in its bootloader; recover it with the web flasher or USB drag-and-drop. | `heltec_t114_dfu.zip` |

Both files are on the [latest release](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases/latest).

## Next step

Once the firmware is installed, continue to [First-time setup](first-setup.md).

If the board won't boot after flashing, see [Troubleshooting](../troubleshooting.md).
