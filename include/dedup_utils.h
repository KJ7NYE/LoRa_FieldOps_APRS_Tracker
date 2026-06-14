#pragma once
#include <Arduino.h>

// Ring-buffer packet deduplicator keyed on djb2(sender + payload).
//
// Use a separate instance per subsystem (digipeater and iGate must not share
// a ring — the digi seeing a packet must not suppress the iGate from uploading
// it, and vice versa).
//
// 50 slots handles ~15-20 active trackers with multi-hop digipeating headroom.
// 60-second TTL covers worst-case propagation delay across a busy net.
struct PacketDedup {
    static constexpr int      SLOTS = 50;
    static constexpr uint32_t TTL   = 60000;  // ms

    struct Entry { uint32_t hash; uint32_t seenAt; };
    Entry buf[SLOTS] = {};
    int   head       = 0;

    // Returns true if this (sender, payload) pair has NOT been seen within TTL,
    // and records it. Returns false if it is a duplicate within the TTL window.
    bool isNew(const String& sender, const String& payload) {
        uint32_t h   = djb2(sender + payload);
        uint32_t now = millis();
        for (int i = 0; i < SLOTS; i++) {
            if (buf[i].hash == h && (now - buf[i].seenAt) < TTL) return false;
        }
        buf[head] = { h, now };
        head = (head + 1) % SLOTS;
        return true;
    }

private:
    static uint32_t djb2(const String& s) {
        uint32_t h = 5381;
        for (unsigned i = 0; i < s.length(); i++)
            h = ((h << 5) + h) + (uint8_t)s[i];
        return h;
    }
};
