#include "logic/measurement.h"

void encodeMeasurement(const Measurement& m, uint8_t* out) {
    out[0] = (uint8_t)(m.ts & 0xFF);
    out[1] = (uint8_t)((m.ts >> 8) & 0xFF);
    out[2] = (uint8_t)((m.ts >> 16) & 0xFF);
    out[3] = (uint8_t)((m.ts >> 24) & 0xFF);
    out[4] = m.hr;
    out[5] = m.spo2;
    out[6] = (uint8_t)(m.steps & 0xFF);
    out[7] = (uint8_t)((m.steps >> 8) & 0xFF);
}

Measurement decodeMeasurement(const uint8_t* in) {
    Measurement m{};
    m.ts = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
    m.hr    = in[4];
    m.spo2  = in[5];
    m.steps = (uint16_t)((uint16_t)in[6] | ((uint16_t)in[7] << 8));
    return m;
}

size_t buildHistoryPacket(const Measurement* items, uint8_t count, uint16_t seq, uint8_t* out) {
    out[0] = HISTORY_TYPE_DATA;
    out[1] = count;
    // Little-endian like the rest of the protocol: the app sends these two
    // bytes back untouched in its ACK.
    out[2] = (uint8_t)(seq & 0xFF);
    out[3] = (uint8_t)((seq >> 8) & 0xFF);
    for (uint8_t i = 0; i < count; ++i) {
        encodeMeasurement(items[i], out + HISTORY_HEADER_SIZE + i * MEASUREMENT_SIZE);
    }
    return HISTORY_HEADER_SIZE + (size_t)count * MEASUREMENT_SIZE;
}

size_t buildHistoryEndPacket(uint8_t* out) {
    out[0] = HISTORY_TYPE_END;
    out[1] = 0;
    // No sequence: this packet is never ACKed.
    out[2] = 0;
    out[3] = 0;
    return HISTORY_HEADER_SIZE;
}

uint8_t sanitizeReading(int32_t raw, uint8_t maxPlausible) {
    if (raw <= 0 || raw > (int32_t)maxPlausible) return 0;
    return (uint8_t)raw;
}
