# Remote configuration (CourseSentry)

Remote configuration lets a field-management system read and change a small set of settings by sending ordinary directed APRS messages, over LoRa RF and, for iGate-role nodes, over APRS-IS. No physical access to the device is needed.

The feature is **off by default**. A device stays locked down until an operator enables it locally over USB, Bluetooth or the web UI.

## Enable it

| Setting | Default | CLI |
|---|---|---|
| Remote configuration | off | `remotecfg on\|off` |
| Unlock token (shared secret for writes) | empty (writes disabled) | `remotecfg token <secret>` |
| Write window after unlock | 300 s | `remotecfg window <60-3600>` |

`remotecfg show` prints the current state, with the token masked, and whether a write window is open. The web UI has the same settings in its **Remote Configuration (CourseSentry)** section.

## Commands

Send a directed APRS message to the node's callsign (or its tactical name) containing one of:

| Command | Stage | Description |
|---|---|---|
| `CSR` | 1 — read, no auth | Full status read. |
| `CSR <code>[,<code>...]` | 1 — read, no auth | Read selected fields. |
| `CSU <token>` | 2 — unlock | Opens a timed write window (default 300 s) if the token matches. |
| `CSW <code>=<value>[,...]` | 2 — write, needs an open window | Writes one or more fields. The batch is checked as a whole: one invalid field rejects all of them. |

## Field codes

| Code | Field |
|---|---|
| `RO` | Device role |
| `TC` | Tactical name |
| `SY` | Symbol (2 characters: overlay + code) |
| `BP` | Beacon path |
| `DM` | Digipeater mode |
| `BR` | Beacon rate (minutes) |
| `GS` | GPS source |
| `LA` / `LO` / `EL` | Fixed latitude / longitude / elevation |

Writing `RO` (role) or `GS` (GPS source) reboots the device about 5 seconds after the write, since those two settings only take effect after a restart. Every other field applies immediately.

## Security

- Five failed `CSU` attempts lock out further unlock attempts for 10 minutes.
- The token is sent in plain text over RF and APRS-IS, because amateur radio rules prohibit encryption. Treat the unlock window as a brief, trusted-site access control, not cryptographic security.
- Never set the token remotely; set it locally.
