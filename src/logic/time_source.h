#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>

#include "measurement.h"

// The bracelet has no clock: at boot it does not know the time. The app
// writes the UTC epoch on the TIME characteristic at every connection; between
// two syncs we extrapolate with millis().
//
// Header-only and Arduino-free: millis() is passed as a parameter, so the wrap
// can be tested on PC without waiting 49 days.
class TimeSource {
public:
    // Sync with an epoch only (legacy), no local offset.
    void sync(uint32_t epochSeconds, uint32_t nowMs) {
        sync(epochSeconds, 0, nowMs);
    }

    // Sync with epoch + local UTC offset (used only by localDayNumber()).
    void sync(uint32_t epochSeconds, int32_t offsetSeconds, uint32_t nowMs) {
        epochBase_ = epochSeconds;
        utcOffset_ = offsetSeconds;
        msBase_ = nowMs;
        synced_ = true;
    }

    // Current time: uptime in seconds before first sync (so measurements stay
    // distinguishable), real epoch after.
    uint32_t now(uint32_t nowMs) const {
        if (!synced_) return nowMs / 1000u;
        // uint32_t subtraction: still correct once millis() has wrapped.
        uint32_t elapsedMs = nowMs - msBase_;
        return epochBase_ + elapsedMs / 1000u;
    }

    // Convert a pre-sync uptime timestamp back into a real epoch.
    // A ts that is already an epoch comes out unchanged.
    //
    // Known limitation: an uptime later than the sync point (leftover from a
    // previous boot) cannot be resolved and is returned as is.
    uint32_t resolve(uint32_t ts) const {
        if (!synced_ || ts >= TS_EPOCH_MIN) return ts;
        uint32_t syncUptime = msBase_ / 1000u;
        if (ts > syncUptime) return ts;
        return epochBase_ - (syncUptime - ts);
    }

    // Whether the app has already provided the time.
    bool isSynced() const { return synced_; }

    // Local calendar day number (epoch day + UTC offset), used by main.cpp to
    // detect local midnight and reset the step counter. Returns 0 before sync.
    int32_t localDayNumber(uint32_t nowMs) const {
        if (!synced_) return 0;
        int64_t localSeconds = (int64_t)now(nowMs) + utcOffset_;
        if (localSeconds < 0) localSeconds = 0;
        return (int32_t)(localSeconds / 86400);
    }

private:
    uint32_t epochBase_ = 0;
    uint32_t msBase_ = 0;
    int32_t  utcOffset_ = 0;
    bool synced_ = false;
};

#endif // TIME_SOURCE_H