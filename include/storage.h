#ifndef STORAGE_H
#define STORAGE_H

#include <Preferences.h>

#include "config.h"
#include "logic/measurement.h"
#include "logic/ring_index.h"

// Backlog of measurements the app has not acknowledged yet.
//
// Two stages:
//   1. a RAM buffer of RAM_BATCH measurements. Writing flash for one
//      measurement every 4 s would wear it out for nothing.
//   2. a fixed-size LittleFS file (STORAGE_FILE) used as a circular buffer:
//      we seek() to the slot given by RingIndex and write 8 bytes. The whole
//      file is never rewritten.
//
// head/count live in NVS (Preferences) so they survive a reboot or deep sleep;
// without that, a restart would lose the whole backlog.
//
// Protocol invariant: nothing is dropped before confirm(), and confirm() is
// only called once the app has ACKed.
class Storage {
public:
    Storage() : ring_(RING_CAPACITY), ramBuffer_{} {}

    // Mounts LittleFS, creates/grows the file if needed, reloads the indexes.
    bool begin();

    // Adds a measurement (goes to RAM first, auto-flushes when full).
    bool append(const Measurement& m);

    // Forces the RAM buffer out to flash.
    bool flush();

    // Reads up to `max` of the oldest measurements WITHOUT removing them.
    // Returns how many were read.
    uint8_t readBatch(Measurement* out, uint8_t max);

    // Drops the n oldest ones. Call ONLY after the app's ACK.
    void confirm(uint8_t n);

    uint32_t pending() const { return ring_.count() + ramCount_; }
    uint32_t pendingInFlash() const { return ring_.count(); }
    uint32_t ramPending() const { return ramCount_; }
    uint32_t dropped() const { return ring_.dropped(); }
    bool isReady() const { return ready_; }

private:
    void saveIndex();

    RingIndex ring_;
    Preferences prefs_;
    Measurement ramBuffer_[RAM_BATCH];
    uint8_t ramCount_ = 0;
    bool ready_ = false;
};

#endif // STORAGE_H
