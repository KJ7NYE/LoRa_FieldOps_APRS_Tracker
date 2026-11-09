# Device roles & digipeating

Each device runs as one of three roles. You choose the role at runtime with no reflash; the change takes effect after **save** and a reboot.

| Role | What it does | CLI |
|---|---|---|
| **Tracker** | Beacons its position over LoRa, using [SmartBeacon](smartbeacon.md) to adapt the rate to speed and turns. | `role set tracker` |
| **iGate** | Receives LoRa packets and uploads them to APRS-IS over WiFi. ESP32 boards only. See [iGate & APRS-IS](igate.md). | `role set igate` |
| **Digipeater** | Receives and retransmits LoRa packets following standard APRS path rules. | `role set digipeater` |

## Position source

Set where the device gets its position with `role gps internal|fixed|none`:

| Source | Use for |
|---|---|
| **Internal GPS** | Boards with onboard GPS (T114, T-Beam, T-Beam 1W, Wireless Tracker, LoRanger V1). |
| **Fixed position** | iGates, digipeaters and boards without GPS. Set it with `fixed latitude`, `fixed longitude` and `fixed elevation`. |
| **None** | No position beacons, for example a receive-only iGate. |

## Digipeating on any role

Digipeating is a separate setting, so a tracker or iGate can also relay packets. The **Digipeater** role is for dedicated relay stations.

| Mode | Behaviour | CLI |
|---|---|---|
| Off | No relaying. | `digi off` |
| WIDE1 fill-in | Relays `WIDE1-1` only. Suited to trackers and low fill-in sites. | `digi wide1` |
| WIDE1 + WIDE2 | Full infrastructure digipeater. | `digi wide1+wide2` |

The digipeat setting is saved with the rest of the configuration and persists across reboots.

**Duplicate suppression.** The digipeater remembers recently relayed packets (a 25-slot hash ring with a 30-second lifetime) so it doesn't retransmit copies of the same packet heard via several paths.

**Anti-loop guard.** Messages addressed to the device's own callsign are never digipeated. This prevents APRS-IS → RF → APRS-IS loops when a query or reply re-enters the network through an iGate downlink.

## Beacon path

Set the path used for this device's own beacons with `beaconpath`:

- `WIDE1-1` (default): one hop. Recommended for busy event channels.
- `WIDE1-1,WIDE2-1`: two hops, for wider coverage at the cost of more channel use.

## PHG beaconing (fixed stations)

iGates and digipeaters can advertise their RF capability (power, antenna height, gain and directivity) with an optional PHG beacon. It is sent as a separate uncompressed position beacon on its own timer; normal beacons are unaffected. See [PHG settings](../reference/settings.md#phg-fixed-station-beaconing).
