# SmartBeacon

SmartBeacon changes how often the tracker beacons based on how it is moving, so fast-moving trackers report often and parked ones stay quiet.

- **Moving fast:** short interval (*fast rate*).
- **Moving slowly:** long interval (*slow rate*).
- **Stopped** (1 km/h or less): beacons at the non-SmartBeacon rate (`nonsmartrate`, default 900 s) regardless of distance. GPS speed noise on a parked device counts as stopped, and the tracker never goes silent.

## Corner pegging

A heading change greater than `turnMinDeg + turnSlope / speed` sends an early beacon, so turns show up in the track.

- Corner pegging only applies at or above the profile's *slow speed*, so GPS course noise while parked can't trigger it.
- A corner beacon is never sent sooner than the profile's **Min Turn Time** after the previous beacon of any kind. A run of switchbacks or city blocks doesn't produce a burst of near-duplicate positions.

## Built-in profiles

Choose a profile with `beacon smartset <0..4>`. The default is **Car**.

| Profile | Slow rate | Slow speed | Fast rate | Fast speed | Min turn angle | Turn slope | Min turn time |
|---|---|---|---|---|---|---|---|
| **0 — Runner** | 90 s | 5 km/h | 45 s | 18 km/h | 12° | 60 | 20 s |
| **1 — Bike** | 120 s | 5 km/h | 45 s | 50 km/h | 12° | 60 | 15 s |
| **2 — Car** | 90 s | 10 km/h | 36 s | 97 km/h | 10° | 80 | 10 s |
| **3 — Jetboat** | 60 s | 30 km/h | 10 s | 97 km/h | 5° | 150 | 5 s |

Min turn time scales with each profile's speed, so corner beacons land roughly every 50–150 m through a series of turns.

> [!TIP]
> The Jetboat profile's 5 s minimum turn time is aggressive on slow LoRa settings. At SF12 a position packet takes about 1.5–3 s of airtime, so a long run of turns can keep the channel busy. Use the Custom profile with a longer minimum turn time if that matters on your channel.

## Custom profile

Profile **4 — Custom** is fully editable. Its defaults are bike-like (120 s / 5 km/h / 60 s / 40 km/h / 12° / 60 / 15 s). Edits take effect immediately, so you can retune cadence in the field; use `save` to keep them across reboots.

Edit it in the web UI's **SmartBeacon Custom** section, or with the `smartcustom` commands described in the [Serial CLI reference](../reference/serial-cli.md#smartbeacon-custom-profile).

## When SmartBeacon is off

With `beacon smart off`, the tracker beacons at a fixed interval set by `nonsmartrate <sec>` (default 900 s).
