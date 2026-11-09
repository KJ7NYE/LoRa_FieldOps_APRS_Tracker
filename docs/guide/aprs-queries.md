# APRS station queries

Every role (tracker, iGate and digipeater) answers APRS station capability queries as defined in APRS 1.01 §13. Send them from any APRS tool: APRSDroid, Xastir, APRS.fi, a handheld radio and so on.

- Queries are case-insensitive.
- The trailing `?` is optional: `?TELEM` and `?TELEM?`, `?PING` and `?PING?`, `?APRS` and `?APRS?` are the same query.
- Send `?` on its own to get the list of supported queries.
- Duplicate queries from the same station are ignored for 60 seconds.
- Unrecognised queries get an ACK but no reply.

Directed queries are also answered when addressed to the configured **tactical callsign** (object name), not just the device's real callsign. `?APRSP` and `?APRS` addressed to the tactical name reply with the Object Report.

## Directed queries

Send as an APRS message addressed to the device's callsign (or its tactical callsign, if set):

| Query | Response |
|---|---|
| `?` | List of supported queries: `Query: ?APRSD ?APRSH ?APRSL ?APRSP ?APRSS ?PING ?VER ?TELEM` (plus `?IGATE` in iGate mode) |
| `?APRSD` | Stations heard directly (no digipeater hop), space-separated |
| `?APRSH <CALL>` | Whether `<CALL>` has been heard recently, and how long ago |
| `?APRSL` | All recently heard stations, newest first |
| `?APRSP` | Current position beacon (same as pressing the beacon button) |
| `?APRSS` | Current status text |
| `?APRST` or `?PING` | Ping reply with the device callsign |
| `?APRSV` or `?VER` | Firmware version string |
| `?TELEM` | Infrastructure health snapshot (see [below](#telem-field-reference)) |

## Undirected queries

Send as an APRS message addressed to `APRS` or `IGATE`:

| Query | Addressee | Response |
|---|---|---|
| `?APRS` | `APRS` | Position beacon from every listening station |
| `?IGATE` | `IGATE` | iGate-online confirmation *(iGate role only)* |

`?` (the query list) is only answered when addressed to the device's own callsign, so a broadcast `?` doesn't make every station in range reply at once.

## `?TELEM` field reference

`?TELEM` is meant for infrastructure monitoring, for example an event system polling field iGates and digipeaters, either directly or through the APRS-IS downlink. The reply looks like:

```text
TELEM BATT=87% UP=3h12m IS=RW RPT=241 GATE=1033
```

| Field | Meaning |
|---|---|
| `BATT` | Battery percentage (0–100), or `NA` on boards with no battery sensing (for example a LoRanger running from DC power) |
| `UP` | Uptime since boot: `<n>m` or `<n>h<n>m` |
| `IS` | APRS-IS link state (iGate role only; otherwise always `DOWN`). `RW` = connected, verified and downlink enabled (fully two-way). `R` = connected but unverified and/or downlink disabled; uploads are tagged `qAO` and downlink messages are not sent to RF. `DOWN` = no APRS-IS session. |
| `RPT` | Packets digipeated since boot (0 if digipeating is off) |
| `GATE` | Packets uploaded to APRS-IS since boot (iGate role only; otherwise always 0) |

All counters reset to 0 on reboot. Compare with `UP` to tell "just rebooted" apart from "running but not relaying anything".

## Message numbers and ACKs

Messaging follows APRS 1.01 chapter 14:

- **Incoming:** a message or query ending in a message number (`{N`, 1–5 letters or digits) is ACKed with `ackN` before any reply. Every copy is ACKed, including retries the duplicate filter otherwise drops, so a sender whose first ACK was lost stops retrying. The APRS 1.1 reply-ack form `{MM}AA` is also understood and ACKed as `ackMM`.
- **Outgoing:** every reply carries its own message number in the standard form `{N` with **no closing brace**, for example `PING KG7KMV-13{12`. The receiving client strips it and answers `ack12`. Numbers run from 1 to 999 and wrap.
- **ACKs and rejects received** (`ackN`, `rejN`) are handled silently: they aren't displayed, ACKed back or treated as commands. Replies are sent once and not retried.

**Plain messages are ACKed too.** A free-text APRS message (not a query) addressed to the device's callsign or tactical name gets an ACK if it carries a message number. There is no automated reply, only the ACK. The tracker sends the ACK itself rather than relying on an attached KISS client, since it is often deployed with no client attached.

## Loop protection

Messages addressed to the device's own callsign are never digipeated, which prevents APRS-IS → RF → APRS-IS loops when a query or reply comes back through an iGate downlink.
