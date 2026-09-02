#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>

#include "measurement.h"

// The bracelet has no clock: at boot it does not know what time it is. The app
// writes the UTC epoch on the TIME characteristic at every connection; between
// two syncs we extrapolate with millis().
//
// Header-only and Arduino-free: millis() is passed as a parameter, so the wrap
// can be tested on PC without waiting 49 days.
class TimeSource {
public:
    // Called when the app writes TIME. `nowMs` = millis() at the same instant.
    void sync(uint32_t epochSeconds, uint32_t nowMs) {
        sync(epochSeconds, 0, nowMs);
    }

    // Same, plus the local UTC offset in seconds (DST included) the app now sends
    // alongside the epoch. Used only by localDayNumber() for the daily step reset;
    // now()/resolve() stay in UTC.
    void sync(uint32_t epochSeconds, int32_t offsetSeconds, uint32_t nowMs) {
        epochBase_ = epochSeconds;
        utcOffset_ = offsetSeconds;
        msBase_ = nowMs;
        synced_ = true;
    }

    // Before the app gives the time we return the uptime in seconds, not 0:
    // measurements all stamped 0 would share the same ts and the app's dedup by
    // (deviceUid, ts) would keep only one. The uptime is also what resolve()
    // needs later to recover their real time. TS_EPOCH_MIN tells them apart.
    uint32_t now(uint32_t nowMs) const {
        if (!synced_) return nowMs / 1000u;
        // uint32_t subtraction: still correct once millis() has wrapped.
        uint32_t elapsedMs = nowMs - msBase_;
        return epochBase_ + elapsedMs / 1000u;
    }

    // Gives back its real epoch to a measurement taken before the sync: it
    // carries its uptime, and we now know the uptime at the sync point, so we
    // walk back from there. A ts that is already an epoch comes out unchanged.
    //
    // Known limitation, not handled: an uptime later than the sync point can
    // only come from a previous boot (reboot with a non-empty backlog) and its
    // base is gone. We leave it as is - the app will stamp it on reception.
    uint32_t resolve(uint32_t ts) const {
        if (!synced_ || ts >= TS_EPOCH_MIN) return ts;
        uint32_t syncUptime = msBase_ / 1000u;
        if (ts > syncUptime) return ts;
        return epochBase_ - (syncUptime - ts);
    }

    bool isSynced() const { return synced_; }

    // Which local calendar day we're in, as a day count since the epoch. main.cpp
    // compares this between measurements and resets the step counter when it changes
    // (local midnight). Meaningless before the first sync (returns 0).
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
