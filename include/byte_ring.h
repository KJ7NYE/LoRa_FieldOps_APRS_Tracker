/* byte_ring.h — single-producer / single-consumer byte FIFO.
 *
 * Used by the BLE configuration link (ble_utils.cpp) to move bytes between
 * the BLE stack's task (NimBLE host task on ESP32, Bluefruit callback task on
 * nRF52) and the Arduino main loop without locks:
 *   - exactly one context calls the producer side (push),
 *   - exactly one context calls the consumer side (pop / peek / drop),
 *   - head is written only by the producer, tail only by the consumer.
 * The indices are std::atomic so each side sees the other's progress in
 * order on both dual-core ESP32 and Cortex-M4.
 *
 * Storage is heap-allocated on first use (allocate()) and deliberately never
 * freed: freeing while the other context might still touch the buffer is the
 * race this class exists to avoid, and the cost is paid only on devices that
 * actually use the feature during this boot.
 */

#pragma once

#include <Arduino.h>
#include <atomic>
#include <stdlib.h>

class ByteRing {
public:
    // Allocates `capacity` bytes of storage (one slot is kept empty to tell
    // full from empty, so usable space is capacity - 1). Idempotent: a second
    // call is a no-op. Returns false when the heap allocation fails.
    bool allocate(size_t capacity) {
        if (storage != nullptr) return true;
        uint8_t* block = static_cast<uint8_t*>(malloc(capacity));
        if (block == nullptr) return false;
        size = capacity;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        storage = block;
        ready.store(true, std::memory_order_release);
        return true;
    }

    bool isAllocated() const { return ready.load(std::memory_order_acquire); }

    // ── producer side ──────────────────────────────────────────────────────
    // Copies as many bytes as fit; returns the number accepted.
    size_t push(const uint8_t* data, size_t length) {
        if (!isAllocated()) return 0;
        const size_t currentHead = head.load(std::memory_order_relaxed);
        const size_t currentTail = tail.load(std::memory_order_acquire);
        const size_t space       = freeSpace(currentHead, currentTail);
        const size_t accepted    = length < space ? length : space;
        size_t writeIndex = currentHead;
        for (size_t i = 0; i < accepted; i++) {
            storage[writeIndex] = data[i];
            writeIndex = (writeIndex + 1) % size;
        }
        head.store(writeIndex, std::memory_order_release);
        return accepted;
    }

    size_t freeSpace() const {
        if (!isAllocated()) return 0;
        return freeSpace(head.load(std::memory_order_relaxed), tail.load(std::memory_order_acquire));
    }

    // ── consumer side ──────────────────────────────────────────────────────
    size_t available() const {
        if (!isAllocated()) return 0;
        const size_t currentHead = head.load(std::memory_order_acquire);
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        return (currentHead + size - currentTail) % size;
    }

    // Returns the next byte, or -1 when empty.
    int pop() {
        if (available() == 0) return -1;
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        const uint8_t value = storage[currentTail];
        tail.store((currentTail + 1) % size, std::memory_order_release);
        return value;
    }

    // Copies up to `maxLength` bytes without consuming them.
    size_t peek(uint8_t* destination, size_t maxLength) const {
        const size_t count = available() < maxLength ? available() : maxLength;
        size_t readIndex = tail.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; i++) {
            destination[i] = storage[readIndex];
            readIndex = (readIndex + 1) % size;
        }
        return count;
    }

    // Consumes `count` bytes previously returned by peek().
    void drop(size_t count) {
        const size_t pending = available();
        if (count > pending) count = pending;
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        tail.store((currentTail + count) % size, std::memory_order_release);
    }

    // Discards everything currently queued (consumer side only).
    void clear() { drop(available()); }

private:
    size_t freeSpace(size_t currentHead, size_t currentTail) const {
        return size - 1 - ((currentHead + size - currentTail) % size);
    }

    uint8_t*            storage = nullptr;
    size_t              size    = 0;
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
    std::atomic<bool>   ready{false};
};
