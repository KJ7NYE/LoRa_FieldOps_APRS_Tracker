# RF Test Harness

Automated end-to-end validation of the RF protocol chain and most of what
the tracker/iGate/digipeater roles can individually do:

```
Tracker (LoRa TX) --RF--> iGate (LoRa RX) --WiFi--> APRS-IS --internet--> independent read-only tap
                      \                        \
                       `--RF--> Digipeater --RF--'   (WIDE1-1 / WIDE2-2, --digi-port)
                      \
                       `--RF--> status/PHG/Mic-E/NOGATE beacons, station queries, echo rejection
                            (mock APRS-IS server for the IS->RF downlink path, --is-downlink)
```

Drives every device's USB serial CLI (KISS/SETUP/LOG mode-switching), watches
their logs, injects crafted packets over KISS where there's no CLI trigger
for something (station queries, fake-sender edge cases), and taps a public
APRS-IS feed independently of the iGate's own connection -- so a passing run
confirms the packet was correct at every hop, not just that the iGate
*believes* it uploaded something. 14 phases total; see "What each phase
checks" below.

## Prerequisites

- All devices already configured with real callsigns (not `NOCALL-7` --
  the firmware's FCC TX gate blocks transmission on the placeholder).
- Tracker has a GPS fix (or `role gps fixed` + a configured `fixed`
  position) -- pre-flight aborts with `GPS_NOT_FIXED` otherwise.
- iGate has WiFi STA configured and connected, and a working path to
  APRS-IS -- pre-flight aborts with `WIFI_NOT_CONNECTED` /
  `APRSIS_NOT_CONNECTED` otherwise.
- If using `--digi-port`: that device has digipeating enabled
  (`digi wide1` or `digi wide1+wide2` via its own SETUP CLI) -- pre-flight
  aborts with `DIGI_MODE_NOT_ACTIVE` otherwise. Digipeating is
  role-independent in this firmware (works on Tracker/iGate/Digipeater
  roles alike), so no role check is made for it.
- If using `--igate-lan-ip` (phase 10): the iGate's WiFi IP, reachable from
  this machine. No CLI command exposes it -- check its OLED display or your
  router's connected-devices list.
- If using `--harness-lan-ip` (phase 12, opt-in): this machine's own LAN IP,
  reachable *from* the iGate (a separate physical device) -- confirm with a
  plain TCP connectivity check if unsure which of your interfaces is on the
  right network.
- Python 3.9+, with `pip install -r requirements.txt` (pyserial).
- Nothing else has the COM ports open (Arduino Serial Monitor, PuTTY, a
  prior harness run, etc.) -- the harness needs exclusive access to each.

## Usage

```sh
# Find the COM ports first
python run_test.py --list-ports

# Single end-to-end run
python run_test.py --tracker-port COM18 --igate-port COM3

# Three runs, spaced automatically to dodge the upload dedup window
python run_test.py --tracker-port COM18 --igate-port COM3 --runs 3

# Three runs back-to-back, forcing a unique beacon comment each time instead
# of waiting -- see "Repeatability" below for the cost of this flag
python run_test.py --tracker-port COM18 --igate-port COM3 --runs 3 --vary-comment

# Gold-standard run: wait for a real SmartBeacon-timed send instead of
# forcing one, to get the tracker's own TX log as a 4th checkpoint
python run_test.py --tracker-port COM18 --igate-port COM3 --wait-for-natural-beacon

# Include a digipeater -- adds WIDE1-1 fill-in and WIDE2-2 multi-hop relay
# phases to the default list automatically (tracker -> digi -> iGate)
python run_test.py --tracker-port COM18 --igate-port COM3 --digi-port COM9 --digi-callsign K7SWI

# Include the query/ping-response test (needs the iGate's WiFi IP -- no CLI
# command exposes it; check its OLED display or your router's client list)
python run_test.py --tracker-port COM18 --igate-port COM3 --igate-lan-ip 192.168.1.42

# Opt-in: IS->RF downlink via a local mock APRS-IS server (2 iGate reboots)
python run_test.py --tracker-port COM18 --igate-port COM3 \
    --harness-lan-ip 192.168.1.10 --phases phase12_is_downlink

# Opt-in: role-switch the digi Tracker<->Digipeater and back (2 more reboots)
python run_test.py --tracker-port COM18 --igate-port COM3 --digi-port COM9 \
    --digi-callsign K7SWI --phases phase13_role_switch
```

Reports are written to `reports/<UTC-timestamp>_run.json` and `.md`
(gitignored), plus a console summary table.

Run `python run_test.py --help` for the full flag list.

## What each phase checks

| Phase | Checks | On failure |
|---|---|---|
| Pre-flight | Every device is who/what's expected, LoRa RF params match across all of them, tracker has GPS fix, iGate has WiFi + APRS-IS, (if `--digi-port`) digi has digipeating active | Aborts before any RF is sent |
| 1. RF link | Tracker's forced beacon is heard by the iGate with correct callsign/tocall/path/DTI | `IGATE_NO_RX` / `IGATE_RX_CONTENT_MISMATCH` |
| 2. iGate upload | iGate logs an `APRS-IS: Uploaded:` line for that packet with the right q-construct | `IGATE_RX_BUT_NO_UPLOAD` |
| 3. External feed | An independent read-only APRS-IS tap (different server pool than the iGate's own) sees the packet | `UPLOADED_BUT_NOT_ON_FEED` |
| 4. Digipeat relay (`--digi-port`) | Digi heard the same beacon directly, retransmitted it with `WIDE1-1` replaced by its own `CALL*`, the iGate heard *that* copy too, and the upload-dedup logic still uploaded only once despite two RX events | `DIGI_NO_RX` / `DIGI_NO_RELAY` / `IGATE_NO_DIGI_RX` / `IGATE_DOUBLE_UPLOAD` |
| 5. Status beacon | `beacon status <text>` + `tx status` arrives with DTI `>`; a follow-up empty-status trigger correctly falls back to a position beacon instead | `TRACKER_STATUS_TX_REJECTED` / `IGATE_STATUS_RX_CONTENT_MISMATCH` |
| 6. PHG beacon | `phg on` produces a beacon with a `PHGxxxx` comment extension | `TRACKER_PHG_ENABLE_REJECTED` / `IGATE_PHG_RX_CONTENT_MISMATCH` |
| 7. Mic-E beacon | With tactical cleared, `beacon mice <n>` produces a packet with a non-`APLRT1` tocall and a backtick/apostrophe-led payload (structural check only, no lat/lon decode) | `TRACKER_MICE_CONFIG_REJECTED` / `IGATE_MICE_RX_STRUCTURAL_MISMATCH` |
| 8. NOGATE filtering | A `NOGATE`-marked beacon is still RX'd by both digi and iGate, but neither relays/uploads it | `DIGI_NOGATE_LEAK` / `IGATE_NOGATE_LEAK` |
| 9. WIDE2-2 digipeat (`--digi-port`) | With digi in `wide1+wide2` mode, `WIDE2-2` decrements to `<digicall>*,WIDE2-1` (not full consumption) | `DIGI_WIDE2_NO_DECREMENT` / `IGATE_NO_DIGI_RX` |
| 14. Digi dedup (`--digi-port`) | Two byte-identical status beacons within the 60s TTL: digi hears both, relays only the first -- its own dedup instance, separate from the iGate's upload dedup | `DIGI_NO_RX` / `DIGI_NO_RELAY` / `DIGI_DEDUP_LEAK` |
| 10. Query/ping response (`--igate-lan-ip`) | A `?PING?` query injected through the iGate's TCP KISS port gets an ack + a `PING <call>` reply from the tracker | `IGATE_KISS_INJECT_NOT_TXD` / `TRACKER_NO_QUERY_REPLY` |
| 11. Echo rejection | Three fake packets (known iGate tocall, `TCPIP` marker, `}` third-party wrap), injected through the tracker's own serial port, are RX'd but not uploaded | `IGATE_ECHO_NOT_REJECTED` (see caveat below) |
| 12. IS->RF downlink (opt-in, `--is-downlink`/`--phases phase12_is_downlink`) | A message injected via a local mock APRS-IS server reaches RF wrapped in third-party format with a `TCPIP` marker | `APRSIS_NOT_CONNECTED` / `IGATE_NO_DOWNLINK_TX` |
| 13. Role switch (opt-in, `--phases phase13_role_switch`) | Digi flips Tracker->Digipeater->Tracker (2 reboots), digipeating still works under the new role, and it ends up restored | `ROLE_SWITCH_NOT_APPLIED` / `ROLE_SWITCH_DIGI_BROKEN` |

Each failure label points at a different subsystem -- see `report.py`'s
`FAILURE_MODE_HINTS` for the full table.

Phase 4 reuses phase1's trigger rather than sending a second beacon (it reads
`ctx.state["t_trigger"]`), so it only runs meaningfully immediately after
phase1 -- the default `--phases` order accounts for this
(`config.py`'s `CORE_PHASE_ORDER` + `DIGI_RELAY_PHASE` insert), but it
matters if you customize `--phases` yourself.

**Phase 11 caveat**: as of this harness's own development, the
echo-rejection heuristic it validates existed only as an *uncommitted local
diff* to `src/aprs_is_utils.cpp` -- it legitimately fails against firmware
that predates that diff. Check `git diff src/aprs_is_utils.cpp` (or your
current equivalent) before treating a phase 11 failure as a firmware
regression.

## Known limitations

**The tracker's own TX log is unobservable when triggered via `tx comment`.**
The firmware suppresses its logger to ERROR level for the entire
KISS/SETUP-mode lifetime (`src/serial_setup.cpp` lines 343, 375) and only
restores it inside LOG mode. Since `tx comment` is a SETUP-mode command, the
resulting `Beacon: TX:` / `LoRa Tx:` log lines never appear over serial --
this is firmware behavior, not a harness bug. The harness instead treats the
SETUP command's synchronous ack (`OK tx comment beacon sent ...`) as the
trigger timestamp and the **iGate's own `LoRa Rx:` line** as the first
content-bearing checkpoint. If Phase 1 fails with `IGATE_NO_RX`, that could
mean either "the tracker never actually sent it" or "it sent but nothing
heard it" -- the harness can't distinguish those from serial alone.

For a real 4-checkpoint chain including the tracker's own TX log, use
`--wait-for-natural-beacon`: the tracker sits in LOG mode and the harness
waits for a naturally-timed SmartBeacon send (governed by the tracker's live
SmartBeacon interval, so this can take a while while stationary). Good for an
occasional gold-standard run; too slow for iterative testing.

**PHG's rate floor only lets it fire "immediately" once per boot.** The
firmware's `phgLastTx` timer starts at 0, so the *first* `phg on` since the
tracker last rebooted fires almost instantly regardless of the configured
rate -- but every send after that respects the real 60-second floor
(`device_role.cpp:193`), even with `phg rate 1`. Phase 6 accounts for this
(up to a 65s wait, and it keeps PHG enabled for the whole wait rather than
restoring it early -- disabling it before the timer permits a send makes
the beacon structurally impossible to fire at all). A repeat run of just
this phase can legitimately take up to a minute.

**RF congestion between phases with a digipeater active.** Every beacon a
phase triggers gets relayed by the digi ~1-2s later; moving straight to the
next phase's own trigger risks a real on-air collision with that
still-in-flight relay. `--phase-settle-delay` (default 2s, only applied
when `--digi-port` is set) exists specifically because this was
reproducible during development -- a phase that reliably passes standalone
would intermittently fail immediately after another phase's trigger.
Occasional single-phase misses in a `--digi-port` run even with the delay
are still possible (three radios in close physical proximity); treat an
isolated failure as worth a retry before assuming a regression.

## Serial control-line (DTR/RTS) settings per board

Different boards need different, sometimes opposite, DTR/RTS handling --
getting this wrong doesn't just fail to connect, it can actively change
device state:

- **nRF52840 (heltec_t114)** needs `dtr=True` held for the session or its
  TinyUSB CDC never flushes output. The tracker uses this by default.
- **ESP32 boards with the classic auto-reset/bootstrap wiring** (RTS/DTR ->
  EN/GPIO0 via transistors) must NOT have a control line held asserted for
  the whole session. On `heltec_v3_433_aprs` specifically, `BUTTON_PIN` is
  GPIO0 (`variants/heltec_v3_433_aprs/board_pinout.h:43`), so holding a line
  reads to the firmware as a sustained USR-button hold and triggers blocking
  WiFi AP mode once it crosses the 8s threshold (`src/main.cpp:208`) -- this
  actually happened during development of this harness. The iGate uses
  `dtr=False, rts=False` by default for this reason, and so does `--digi-port`
  unless you pass `--digi-dtr-assert` (only if the digipeater is itself an
  nRF52840 board).

If a device you add behaves strangely right after the harness opens its
port -- unexpected mode changes, a config UI appearing, anything not asked
for -- suspect this before anything else.

## Repeatability: the upload dedup window

The iGate dedups uploads by a hash of `sender + payload` with a 60-second TTL
(`include/dedup_utils.h`). A stationary tracker's position beacon has an
identical payload run-to-run (no timestamp field), so **re-triggering within
60s of a prior run will still pass Phase 1 (RX has no dedup) but silently
fail Phase 2** with `IGATE_RX_BUT_NO_UPLOAD` -- this is expected firmware
behavior, not a bug, and the harness labels it accordingly.

Two ways to handle repeated runs:

1. **Default**: `--run-spacing 65` (seconds) is enforced automatically
   between consecutive `--runs N > 1` iterations. No side effects.
2. **`--vary-comment`**: sets a unique `beacon comment RUN-<n>-<epoch>`
   before each trigger to force a distinct payload hash instead of waiting.
   **This writes flash on every run** (SETUP `save`, required to clear the
   dirty flag before `exit`) and temporarily overwrites the tracker's
   persisted beacon comment. The harness restores the original comment
   (captured during pre-flight) and saves once at teardown -- but if the
   harness is killed mid-run (Ctrl-C, crash) before teardown runs, the
   tracker's comment may be left in a test-polluted state; check with a
   manual `setup` -> `show beacons` afterward if in doubt.

## Reboot-based phases (12, 13): opt-in, and the highest teardown risk

Phases 12 and 13 are the only ones that reboot a device, and are
**never included in the default phase list even when their prerequisite
flags are set** -- unlike every other optional phase, you must name them
explicitly via `--phases` (e.g. `--phases phase12_is_downlink`). This is a
deliberate exception to the rest of the harness's "flag present ->
auto-included" convention; don't "fix" it to match, the cost (multi-second
reboots, temporarily taking a device out of its normal configuration) is
why it's opt-in in the first place.

**Phase 12** (`--is-downlink`) temporarily repoints the iGate's
`aprsiss server`/`port` at a local mock server this harness runs itself,
reboots, runs the downlink test, then repoints back and reboots again --
confirmed genuinely necessary, not just convenient: the SETUP CLI's
`aprsiss server`/`port` setters only write config in memory
(`serial_setup.cpp:833-834`) and `checkConnection()` only reconnects once
the existing socket has already dropped (`aprs_is_utils.cpp:142-147`), so
there is no live-reconnect path. The phase's own `try`/`finally` restores
the original server/port/downlinkEnabled and reboots again even if an
assertion fails partway through -- but if the harness process is killed
outright (not a normal exception -- a hard Ctrl-C or crash) between the
repoint and that `finally` running, the iGate is left pointed at a now-dead
local mock server. Recover manually: `setup` -> `aprsiss server <real>` ->
`aprsiss port <real>` -> `save` -> `reboot`.

**Phase 13** (`--phases phase13_role_switch`) reboots the **digi**
specifically (not tracker/iGate) since it's the least disruptive device to
take out of its normal role mid-run. Same teardown shape: a `try`/`finally`
flips it back to its original role and reboots again regardless of
mid-phase failures, with the same hard-kill caveat as phase 12. Recover
manually: `setup` -> `role set tracker` (or whatever the digi's normal role
is) -> `save` -> `reboot`.

Both phases' reboot timing (`--reboot-settle-delay`, default 8s) is
conservative and was not specifically tuned per board -- if you see
spurious `ModeSwitchTimeout`s right after a reboot, that's a place to look
before suspecting the firmware.

## Extending

Every phase is a module in `phases/` exposing `run(ctx: TestContext) ->
TestResult`, registered in `phases/__init__.py`'s `PHASE_REGISTRY`. The
underlying primitives -- `EventBus.wait_for(channel, predicate, timeout)`,
`DeviceSession`'s mode-switch driver, and `log_parser.py`'s predicate
helpers -- are all generic. `phase4_digipeat_relay.py` is a worked example
of the pattern: adding a second tracker or an aprsdroid traffic source
follows the same shape:

1. Add a `DeviceSession` (or raw TCP/BLE KISS connection) to
   `TestContext.extra_devices`, opened/closed in `run_test.py` conditionally
   on a new CLI flag (see how `--digi-port` is threaded through
   `config.py`/`run_test.py`).
2. Extend `preflight.py` with a `_check_<device>()` function if the new
   device needs identity/config validation before the RF phases run (see
   `_check_digi()`).
3. Write a new predicate in `log_parser.py` if the existing ones
   (`is_lora_rx_from`, `is_aprsis_uploaded_from`, `is_digi_repeating_from`,
   etc.) don't cover it.
4. Add a new `phaseN_*.py` file with a `run()` function and register it in
   `PHASE_REGISTRY`; append it to the default phase list in `config.py`'s
   `parse_args()` only when its CLI flag is set, so existing invocations are
   unaffected -- unless the new phase reboots a device, in which case make it
   opt-in-only like phases 12/13 instead (see "Reboot-based phases" above).

If there's no SETUP CLI command to trigger the behavior you want to test
(true for anything RF-external -- station queries, messages, fake-sender
edge cases), `phase10_query_response.py`/`phase11_echo_rejection.py` are the
worked example: `ax25_kiss.py` encodes a raw TNC2 line into a KISS frame,
and either `tcp_kiss_client.py` (through the iGate's TCP KISS port -- the
iGate transmits) or `DeviceSession.send_kiss_frame()` (through a
tracker/digi's own serial port in KISS mode -- that device transmits) gets
it onto real RF. Pick the transport based on which device needs to
*receive* what you're injecting -- a half-duplex radio can't hear its own
transmission, so injecting through the iGate only works for tests where
something else must receive it (see the comment at the top of
`phase11_echo_rejection.py` for the full reasoning; this tripped up the
first draft of that phase).

No changes needed to `serial_link.py`, `device_session.py`, or existing
phases.
