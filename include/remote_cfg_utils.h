#pragma once
#ifndef REMOTE_CFG_UTILS_H_
#define REMOTE_CFG_UTILS_H_

#include <Arduino.h>

// Compact remote read/write configuration protocol carried over APRS
// directed messages (RF via query_utils.cpp, APRS-IS via aprs_is_utils.cpp).
// Intended for a field-management system (e.g. CourseSentry) to remotely
// inspect and adjust a small allowlist of runtime parameters on deployed
// nodes without a truck roll.
//
// Wire format (message body, after any trailing "{NNN}" msg-number has
// already been stripped by the caller):
//   CSR                    -- Stage 1: full status read (no auth)
//   CSR RO,BR,DM           -- Stage 1: selective read (comma-separated field codes)
//   CSU <token>            -- Stage 2: unlock, opens a timed write window
//   CSW RO=2,BR=20         -- Stage 2: write (only while unlocked, comma-separated key=value)
//
// Field codes: RO=deviceRole TC=tacticalCallsign SY=symbol(overlay+code)
//              BP=beaconPath DM=digiMode BR=nonSmartBeaconRate GS=gpsSource
//              LA=latitude LO=longitude EL=elevation
//
// This module is transport-agnostic: it never transmits anything itself.
// Callers pass the sender callsign and command text, get back a reply body
// (without the "{NNN}" suffix), and are responsible for wrapping/queuing it
// on whichever transport the command arrived on.
//
// The entire feature is gated by Config.remoteCfg.enabled: when false, every
// command (read or write) is silently ignored (empty reply, no side
// effects) so a device in transport/storage doesn't even acknowledge that
// it understands the command shape.
namespace RemoteCfg_Utils {

    // True if `text` looks like a remote-cfg command (CSR/CSU/CSW prefix).
    // Pure prefix check, no side effects, no state mutation.
    bool isCommand(const String& text);

    // Parses and executes a remote-cfg command. Returns the reply body text
    // (no leading "CS ", no trailing "{NNN}" -- caller adds both), or an
    // empty string if no reply should be sent (e.g. feature disabled).
    // `sender` scopes the unlock window to a single holder; `text` is the
    // full command including its "CSR"/"CSU"/"CSW" prefix.
    String handleCommand(const String& sender, const String& text);

    // Call once per main loop iteration. Fires a deferred device reboot if a
    // prior CSW write touched a field (deviceRole/gpsSource) that only takes
    // effect after a restart, once enough time has passed for the write's
    // reply to have drained through the output packet buffer.
    void pollReboot();

}

#endif
