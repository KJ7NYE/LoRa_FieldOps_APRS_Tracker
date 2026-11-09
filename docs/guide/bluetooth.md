# Bluetooth & pairing PIN

Every board offers two services over Bluetooth LE:

- **KISS TNC** for APRS apps such as APRS.fi, APRSDroid and Pinpoint. See [Connecting apps](kiss-tnc.md).
- **Setup over Bluetooth**, the configuration CLI that the FieldOps Android app uses. It is only available while the pairing PIN is required.

Both are protected by a pairing PIN on every target: ESP32 / ESP32-S3 (NimBLE) and nRF52840 (Heltec T114, Bluefruit). Until a phone has paired with the PIN, the tracker passes no traffic to or from it.

| Setting | Default | CLI | Notes |
|---|---|---|---|
| Bluetooth | off | `bt on\|off` | Start Bluetooth at boot. |
| Device name | `LoRaAPRS` | `bt name <text>` | Name the phone sees when scanning. |
| Require PIN | on | `bt pin on\|off` | Turn off for an open link that any nearby phone can use. Turning it off also disables setup over Bluetooth. |
| PIN | `123456` | `bt pin <4-6 digits>` | Leading zeroes are kept. Six digits works on every phone. |

## Setting the PIN

Use any of the configuration tools:

- **Web UI**: **Bluetooth** section → *Require PIN* and *PIN* → **Save** (the tracker reboots).
- **Serial configuration tool / Android app**: **Bluetooth** section → *Require PIN* and *PIN*, then save and reboot.
- **Serial CLI**:

  ```text
  setup
  bt pin 482913
  bt pin on
  save
  reboot
  ```

`show bt` prints the current state and the exact passkey to enter on the phone.

## Entering the PIN on a phone

Bluetooth pairing always uses a **six-digit** passkey. The default `123456` is entered as-is. A shorter PIN is padded with leading zeroes, so `1234` becomes passkey `001234`. Some phones, including iPhones, only accept six digits, so enter `001234` there. Every configuration tool shows the padded passkey next to the PIN field, and the tracker logs it at boot:

```text
[BLE] Advertising as "LoRaTracker" (APRS.fi KISS), PIN pairing required - enter 123456 on the phone
```

To avoid the padding entirely, use a six-digit PIN.

## Changing the PIN

PIN changes apply at the next reboot. If the PIN or the *Require PIN* setting changed since the last boot, the tracker **forgets every paired phone**; otherwise a phone paired under the old PIN could keep connecting. After a change:

1. On the phone, forget or remove the tracker in Bluetooth settings.
2. Reconnect from the app and enter the new PIN.

A phone that is already paired reconnects without asking for the PIN until the PIN changes again.

## Setup over Bluetooth

The [serial CLI](../reference/serial-cli.md) is also available over Bluetooth LE, so the FieldOps app, or any BLE UART terminal such as nRF Connect or Serial Bluetooth Terminal, can configure a tracker without a cable. Every CLI command works the same way over both links.

**Requirements**

- Bluetooth on (`bt on`) **and PIN pairing required** (`bt pin on`). With the PIN off, the tracker does not offer the setup service at all, so nobody nearby can reconfigure it. Turn the PIN on over USB first, then `save` and `reboot`.
- The phone pairs with the PIN the first time it connects.
- One Bluetooth connection at a time: disconnect APRSDroid or APRS.fi before connecting the FieldOps app, and the reverse.

**Differences from USB**

- There is no KISS or log mode on this link. Until you type `setup`, it ignores every line except `setup`, just as USB does in KISS mode. `log` is USB-only.
- `import` does not echo the pasted JSON back (typed commands are still echoed).
- Only one link can hold the setup session. `setup` on the other link answers `ERR: config session busy on USB` (or `BLE`). A session with no input for 2 minutes (for example, a USB cable pulled without `exit`) can be taken over.
- If the Bluetooth link drops mid-session, the session ends. Unsaved edits stay in RAM, and the next `setup` on either link reports them so you can `save` or `discard`.
- On the Heltec T114 (nRF52) the link uses the default 23-byte ATT MTU, so long output (`help`, `export`) takes a few seconds to arrive.

**GATT layout.** The setup CLI uses the Nordic UART Service, which is not advertised. Find the tracker by its APRS.fi KISS service UUID `00000001-ba2a-46c9-ae49-01b0961f68bb`, then discover services:

| UUID | Direction | Use |
|---|---|---|
| `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | service | Setup CLI |
| `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | phone → tracker (write) | Command bytes, for example `setup\r\n` |
| `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | tracker → phone (notify) | CLI output |

Both characteristics require an authenticated (PIN-paired), encrypted link. Firmware builds that should not offer this can opt out with `-D DISABLE_BLE_CONFIG`.

## Log messages

Bluetooth events print on USB serial in `log` mode (the serial logger is muted in the default KISS mode) and also appear in the web UI live log, prefixed `BLE:`.

| Message | Meaning |
|---|---|
| `Phone connected, waiting for PIN pairing` | A phone connected; the link stays closed until it pairs. The phone shows its PIN prompt when the app first sends or subscribes. |
| `Pairing rejected by phone: ...` / `Pairing rejected by tracker: ...` | Pairing failed, with the reason (for example `confirm value failed (wrong PIN)`). ESP32 only. |
| `Phone presented a pairing key this tracker doesn't have` | The phone kept an old pairing. It should re-pair; otherwise, forget the tracker on the phone. ESP32 only. |
| `Phone is re-pairing; replacing its old pairing` | The phone started a fresh pairing over an existing one. ESP32 only. |
| `Phone is asking for the PIN, waiting for entry` | The phone is showing its PIN prompt (ESP32). |
| `Phone authenticated with PIN, KISS link open` | Pairing succeeded (or a paired phone reconnected). Traffic flows. |
| `Pairing failed (wrong PIN, cancelled, or a stale pairing on the phone); waiting for the phone to retry` | Wrong PIN, a dismissed prompt, or the phone tried an old pairing the tracker has forgotten. The phone usually asks for the PIN again on the same connection. If it doesn't, forget the tracker on the phone and pair again. |
| `Phone used a pairing made without the PIN; waiting for it to pair again with the PIN` | The phone presented an old no-PIN pairing; it should re-pair with the PIN. |
| `Ignoring data from phone until it pairs with the PIN` | The app sent data before pairing finished. It's dropped. |
| `Phone did not pair with the PIN within 60 s, disconnecting` | Pairing never completed, so the connection is freed for another attempt. |
| `PIN setting changed since last boot; forgetting all paired phones` | Expected once after a PIN change. |
| `PIN is enabled but not 4-6 digits; BLE disabled until the PIN is fixed` | The saved PIN is malformed. Bluetooth stays off rather than running unprotected. |
| `Advertising as ... PIN disabled - any nearby phone can connect` | *Require PIN* is off. |
| `Config link open (phone subscribed to the setup CLI)` | A PIN-paired phone opened setup over Bluetooth (for example the FieldOps app). |
| `config link closed, unsaved edits kept in RAM` | The Bluetooth setup session ended mid-edit; the next `setup` (USB or Bluetooth) reports the pending edits. |

## Security notes

- The PIN is stored in plain text in `tracker_conf.json` and shown in the configuration tools, so anyone who can reach the web UI or USB serial can read it. Treat exported configurations as sensitive.
- The PIN blocks casual nearby connections, but a static PIN cannot stop a determined attacker who captures the pairing exchange. Use a six-digit PIN that isn't the default.
- Anyone who knows the PIN can also **reconfigure** the tracker over Bluetooth (callsign, frequency, TX power, everything the CLI can change). This is a second reason not to leave it at the default `123456`. To keep the KISS link but rule out over-the-air setup, build with `-D DISABLE_BLE_CONFIG`.
- Only one phone can be connected at a time.
- Bluetooth Classic SPP is not covered by the PIN.
