# iGate & APRS-IS

In the **iGate** role, the device uploads the LoRa packets it hears to the APRS-IS network over WiFi, and can pass messages from APRS-IS back out over RF. The iGate role needs WiFi, so it is available on ESP32 boards only (not the Heltec T114).

## Set up an iGate

1. Set the role: `role set igate`.
2. Set a position source. Most iGates use a fixed position: `role gps fixed`, `fixed latitude <dd>`, `fixed longitude <dd>`, `fixed elevation <m>`.
3. Add one or more WiFi networks and enable station mode:

    ```text
    wifista add MyNetwork
    wifista password 0 MyPassword
    wifista on
    ```

    Up to 5 networks can be stored. They are tried in list order. `wifista list` shows them with masked passwords.

4. Check the APRS-IS settings (the defaults suit most people), then `save` and `reboot`.

You can also do all of this from the **Device Role** and **WiFi** sections of the web UI or serial configuration tool.

## APRS-IS settings

| Setting | Default | CLI |
|---|---|---|
| Server | `rotate.aprs.net` | `aprsiss server <host>` |
| Port | `14580` | `aprsiss port <n>` |
| Filter | `m/20` (packets within 20 km) | `aprsiss filter [<filter>]` (no value clears it) |
| Passcode override | blank (auto) | `aprsiss passcode [<code>]` (no value clears it) |
| Downlink | on | `aprsiss downlink on\|off` |

**Downlink.** With downlink on, directed messages from APRS-IS are passed back to RF, but only to a station heard directly on RF in the last 30 minutes.

## Passcode

The APRS-IS passcode is computed automatically from your callsign, so you don't normally need to enter it.

To use a different passcode, for example for a club callsign, enter it in the **Passcode (override)** field or with `aprsiss passcode <code>`. Leave it blank to use the computed one. A changed passcode is only used on the next connection (save and reboot).

## Check the connection

`wifista status` and `aprsiss status` print live connection details:

```text
wifiSTA.connected=true ip=192.168.1.42 ssid=MyNetwork
aprsIS.connected=true host=rotate.aprs.net port=14580 login=verified server=T2USASW
```

| Field | Meaning |
|---|---|
| `login=verified` | Passcode accepted; uploads and downlink work. |
| `login=unverified` | Passcode rejected. The server keeps the connection open but receive-only. |
| `login=pending` | Waiting for the server's login response. |
| `server=` | The pool member that answered (for example `T2USASW` behind `rotate.aprs.net`). Appears once the login response arrives. |

The web UI's **WiFi** section shows the same status. Remote monitoring systems can also read the link state with the [`?TELEM` query](aprs-queries.md#telem-field-reference).

## TCP KISS

When WiFi station mode is connected, a TCP KISS server also starts on port 8001 (`tcpkiss port <n>`). See [Connecting apps](kiss-tnc.md).
