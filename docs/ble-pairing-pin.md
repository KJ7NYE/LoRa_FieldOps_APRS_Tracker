# BLE pairing PIN

The BLE KISS TNC (APRS.fi, APRSDroid, Pinpoint) is protected by a pairing PIN on every target: ESP32 / ESP32-S3 (NimBLE) and nRF52840 (Heltec T114, Bluefruit). Until a phone has paired with the PIN, the tracker does not pass any KISS traffic to or from it.

| Setting | Default | Notes |
|---|---|---|
| Require PIN | on | Turn off for an open link that any nearby phone can use. |
| PIN | `123456` | 4–6 digits. Leading zeroes are kept. Six digits works on every phone. |

## Setting the PIN

You can use any of the configuration tools:

- **Web UI**: Bluetooth section → *Require PIN* and *PIN* → Save (the tracker reboots).
- **USB Serial Configuration Tool / Android app**: Bluetooth section → *Require PIN* and *PIN*, then save and reboot.
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

BLE pairing always uses a **six-digit** passkey. The default `123456` is entered as-is. A shorter PIN is padded with leading zeroes, so `1234` becomes passkey `001234`. Some phones, including iOS, only accept six digits, so enter `001234` there. Every config tool shows the padded passkey next to the PIN field, and the tracker logs it at boot:

```text
[BLE] Advertising as "LoRaTracker" (APRS.fi KISS), PIN pairing required - enter 123456 on the phone
```

To avoid the padding entirely, keep the PIN at six digits.

## Changing the PIN

PIN changes apply at the next reboot. If the PIN or the *Require PIN* setting changed since the last boot, the tracker **forgets every paired phone**. Otherwise a phone paired under the old PIN could keep connecting. After a change:

1. On the phone, forget/remove the tracker in Bluetooth settings.
2. Reconnect from the app and enter the new PIN.

A phone that is already paired reconnects without asking for the PIN until the PIN changes again.

## Log messages

BLE events print on USB serial in `log` mode (the serial logger is muted in the default KISS mode) and also appear in the web UI live log, prefixed `BLE:`.

| Message | Meaning |
|---|---|
| `Phone connected, waiting for PIN pairing` | A phone connected; the link stays closed until it pairs. The phone shows its PIN prompt when the app first sends or subscribes. |
| `Pairing rejected by phone: ...` / `Pairing rejected by tracker: ...` | Pairing failed, with the reason (e.g. `confirm value failed (wrong PIN)`). ESP32 only. |
| `Phone presented a pairing key this tracker doesn't have` | The phone kept an old pairing; it should re-pair, otherwise forget the tracker on the phone. ESP32 only. |
| `Phone is re-pairing; replacing its old pairing` | The phone started a fresh pairing over an existing one. ESP32 only. |
| `Phone is asking for the PIN, waiting for entry` | The phone is showing its PIN prompt (ESP32). |
| `Phone authenticated with PIN, KISS link open` | Pairing succeeded (or a paired phone reconnected). Traffic flows. |
| `Pairing failed (wrong PIN, cancelled, or a stale pairing on the phone); waiting for the phone to retry` | Wrong PIN, a dismissed prompt, or the phone tried an old pairing the tracker has forgotten. The phone usually asks for the PIN again on the same connection. If it doesn't, forget the tracker on the phone and pair again. |
| `Phone used a pairing made without the PIN; waiting for it to pair again with the PIN` | The phone presented an old no-PIN pairing; it should re-pair with the PIN. |
| `Ignoring data from phone until it pairs with the PIN` | The app sent data before pairing finished. It's dropped. |
| `Phone did not pair with the PIN within 60 s, disconnecting` | Pairing never completed, so the connection is freed for another attempt. |
| `PIN setting changed since last boot; forgetting all paired phones` | Expected once after a PIN change. |
| `PIN is enabled but not 4-6 digits; BLE disabled until the PIN is fixed` | The saved PIN is malformed. BLE stays off rather than running unprotected. |
| `Advertising as ... PIN disabled - any nearby phone can connect` | *Require PIN* is off. |

## Security notes

- The PIN is stored in plain text in `tracker_conf.json` and shown in the config tools, so anyone who can access the web UI or USB serial can read it. Treat exported configs as sensitive.
- The PIN blocks casual nearby connections, but a static PIN cannot stop a determined attacker who is able to capture the pairing exchange. Use a six-digit PIN that isn't the default.
- Only one phone can be connected at a time.
- Bluetooth Classic SPP is not covered. Every current target uses BLE.
