#ifndef RING_INDEX_H
#define RING_INDEX_H

#include <stdint.h>

// Circular buffer arithmetic: which slot to write, which slot to read.
// No flash access here on purpose - that is what makes the class testable on PC
// (`pio test -e native`). Storage does the real reads and writes.
//
// head_ = index of the oldest un-ACKed item. The tail is derived (head_ +
// count_) and never stored: two counters to keep in sync is one too many.
class RingIndex {
public:
    explicit RingIndex(uint32_t capacity) : capacity_(capacity) {}

    // Reserves the next slot. When the ring is full we overwrite the oldest
    // measurement (recent beats old) and count it in dropped_.
    uint32_t push();

    // Slot of the n-th oldest item (offset 0 = the oldest).
    uint32_t slotAt(uint32_t offset) const;

    // Frees the n oldest ones: called ONLY after the app's ACK.
    void release(uint32_t n);

    // Reloads the state from NVS after a reboot. Inconsistent values (corrupted
    // flash, capacity changed) -> start from an empty ring rather than read
    // anywhere in the file.
    void restore(uint32_t head, uint32_t count);

    uint32_t head() const { return head_; }
    uint32_t count() const { return count_; }
    uint32_t capacity() const { return capacity_; }
    uint32_t dropped() const { return dropped_; }
    bool isFull() const { return count_ >= capacity_; }

private:
    uint32_t capacity_;
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint32_t dropped_ = 0;  // measurements lost by overwrite, reported in the logs
};

#endif // RING_INDEX_H
