#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"

bool Storage::begin() {
    // true = formate si le montage échoue (première utilisation, ou partition
    // corrompue). Sans ça le bracelet resterait bloqué sans stockage.
    if (!LittleFS.begin(true)) {
        Serial.println("[FAIL] Storage - montage LittleFS impossible");
        return false;
    }

    // Le fichier ring a une taille fixe : on l'alloue en entier au premier
    // boot pour ne jamais tomber en "disque plein" en pleine mesure.
    const size_t wantedSize = (size_t)RING_CAPACITY * MEASUREMENT_SIZE;
    File f = LittleFS.open(STORAGE_FILE, LittleFS.exists(STORAGE_FILE) ? "r+" : "w+");
    if (!f) {
        Serial.println("[FAIL] Storage - ouverture de " STORAGE_FILE " impossible");
        return false;
    }
    if (f.size() < wantedSize) {
        Serial.print("[Storage] Allocation du fichier ring (");
        Serial.print((unsigned)wantedSize);
        Serial.println(" octets), premier boot...");
        uint8_t zeros[MEASUREMENT_SIZE] = {0};
        f.seek(f.size());
        while (f.size() < wantedSize) {
            if (f.write(zeros, MEASUREMENT_SIZE) != MEASUREMENT_SIZE) {
                Serial.println("[FAIL] Storage - flash pleine pendant l'allocation");
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
    Serial.print(" mesures en flash (head=");
    Serial.print(ring_.head());
    Serial.print(", capacite=");
    Serial.print(ring_.capacity());
    Serial.println(")");
    return true;
}

bool Storage::append(const Measurement& m) {
    if (!ready_) return false;

    ramBuffer_[ramCount_++] = m;
    if (ramCount_ >= RAM_BATCH) {
        return flush();
    }
    return true;
}

bool Storage::flush() {
    if (!ready_ || ramCount_ == 0) return true;

    File f = LittleFS.open(STORAGE_FILE, "r+");
    if (!f) {
        Serial.println("[Storage] Flush impossible : " STORAGE_FILE " illisible");
        return false;
    }

    uint8_t buf[MEASUREMENT_SIZE];
    uint8_t written = 0;
    for (uint8_t i = 0; i < ramCount_; ++i) {
        uint32_t slot = ring_.push();
        encodeMeasurement(ramBuffer_[i], buf);
        f.seek((uint32_t)slot * MEASUREMENT_SIZE);
        if (f.write(buf, MEASUREMENT_SIZE) != MEASUREMENT_SIZE) {
            Serial.print("[Storage] Ecriture ratee au slot ");
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
    Serial.print(" mesures -> flash=");
    Serial.print(ring_.count());
    Serial.print(" drop=");
    Serial.println(ring_.dropped());
    return written > 0;
}

uint8_t Storage::readBatch(Measurement* out, uint8_t max) {
    if (!ready_) return 0;

    // On sert toujours depuis la flash : le tampon RAM y est vidé d'abord pour
    // que l'ordre des mesures soit celui de leur acquisition.
    if (ramCount_ > 0) flush();

    uint32_t available = ring_.count();
    uint8_t n = (available < max) ? (uint8_t)available : max;
    if (n == 0) return 0;

    File f = LittleFS.open(STORAGE_FILE, "r");
    if (!f) {
        Serial.println("[Storage] Lecture impossible : " STORAGE_FILE " illisible");
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

void Storage::confirm(uint8_t n) {
    if (!ready_ || n == 0) return;
    ring_.release(n);
    saveIndex();
    Serial.print("[Storage] Purge de ");
    Serial.print(n);
    Serial.print(" mesures apres ACK -> flash=");
    Serial.println(ring_.count());
}

void Storage::saveIndex() {
    prefs_.putUInt("head", ring_.head());
    prefs_.putUInt("count", ring_.count());
}
