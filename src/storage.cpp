#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"

// Mount the filesystem and allocate the ring file if needed.
bool Storage::begin() {
    // Format on mount failure (first use or corrupted partition).
    if (!LittleFS.begin(true)) {
        Serial.println("[FAIL] Storage - LittleFS mount failed");
        return false;
    }

    // Ring file has a fixed size, fully allocated on first boot.
    const size_t wantedSize = (size_t)RING_CAPACITY * MEASUREMENT_SIZE;
    File f = LittleFS.open(STORAGE_FILE, LittleFS.exists(STORAGE_FILE) ? "r+" : "w+");
    if (!f) {
        Serial.println("[FAIL] Storage - cannot open " STORAGE_FILE);
        return false;
    }
    if (f.size() < wantedSize) {
        Serial.print("[Storage] Allocating the ring file (");
        Serial.print((unsigned)wantedSize);
        Serial.println(" bytes), first boot...");
        uint8_t zeros[MEASUREMENT_SIZE] = {0};
        f.seek(f.size());
        while (f.size() < wantedSize) {
            if (f.write(zeros, MEASUREMENT_SIZE) != MEASUREMENT_SIZE) {
                Serial.println("[FAIL] Storage - flash full during allocation");
                f.close();
                return false;
            }
        }
    }
    f.close();

    prefs_.begin(PREFS_NAMESPACE, false);
    uint32_t head = prefs_.getUInt("head", 0);
    uint32_t count = prefs_.getUInt("count", 0);
    ring_.restore(head, count);

    ready_ = true;
    Serial.print("[OK] Storage - ");
    Serial.print(ring_.count());
    Serial.print(" measurements in flash (head=");
    Serial.print(ring_.head());
    Serial.print(", capacity=");
    Serial.print(ring_.capacity());
    Serial.println(")");
    return true;
}

// Add a measurement to the RAM buffer, flush when the batch is full.
bool Storage::append(const Measurement& m) {
    if (!ready_) return false;

    ramBuffer_[ramCount_++] = m;
    if (ramCount_ >= RAM_BATCH) {
        return flush();
    }
    return true;
}

// Write the RAM buffer to flash.
bool Storage::flush() {
    if (!ready_ || ramCount_ == 0) return true;

    File f = LittleFS.open(STORAGE_FILE, "r+");
    if (!f) {
        Serial.println("[Storage] Cannot flush: " STORAGE_FILE " unreadable");
        return false;
    }

    uint8_t buf[MEASUREMENT_SIZE];
    uint8_t written = 0;
    for (uint8_t i = 0; i < ramCount_; ++i) {
        uint32_t slot = ring_.push();
        encodeMeasurement(ramBuffer_[i], buf);
        f.seek((uint32_t)slot * MEASUREMENT_SIZE);
        if (f.write(buf, MEASUREMENT_SIZE) != MEASUREMENT_SIZE) {
            Serial.print("[Storage] Write failed at slot ");
            Serial.println(slot);
            break;
        }
        written++;
    }
    f.close();
    ramCount_ = 0;
    saveIndex();

    Serial.print("[Storage] Flush ");
    Serial.print(written);
    Serial.print(" measurements -> flash=");
    Serial.print(ring_.count());
    Serial.print(" drop=");
    Serial.println(ring_.dropped());
    return written > 0;
}

// Read up to max measurements from flash, in order.
uint8_t Storage::readBatch(Measurement* out, uint8_t max) {
    if (!ready_) return 0;

    // Flush RAM first to keep acquisition order.
    if (ramCount_ > 0) flush();

    uint32_t available = ring_.count();
    uint8_t n = (available < max) ? (uint8_t)available : max;
    if (n == 0) return 0;

    File f = LittleFS.open(STORAGE_FILE, "r");
    if (!f) {
        Serial.println("[Storage] Cannot read: " STORAGE_FILE " unreadable");
        return 0;
    }

    uint8_t buf[MEASUREMENT_SIZE];
    uint8_t read = 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t slot = ring_.slotAt(i);
        f.seek((uint32_t)slot * MEASUREMENT_SIZE);
        if (f.read(buf, MEASUREMENT_SIZE) != MEASUREMENT_SIZE) break;
        out[i] = decodeMeasurement(buf);
        read++;
    }
    f.close();
    return read;
}

// Drop n measurements from the ring after they were ACKed.
void Storage::confirm(uint8_t n) {
    if (!ready_ || n == 0) return;
    ring_.release(n);
    saveIndex();
    Serial.print("[Storage] Dropping ");
    Serial.print(n);
    Serial.print(" measurements after ACK -> flash=");
    Serial.println(ring_.count());
}

// Persist ring head/count to survive reboots.
void Storage::saveIndex() {
    prefs_.putUInt("head", ring_.head());
    prefs_.putUInt("count", ring_.count());
}