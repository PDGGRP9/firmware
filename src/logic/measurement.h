#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stddef.h>
#include <stdint.h>

// One bracelet measurement, as stored in flash AND sent over BLE.
// It is the only data format of the protocol: live and history use the exact
// same record.
struct Measurement {
    uint32_t ts;     // UTC epoch in seconds; 0 = the app has not given the time yet
    uint8_t  hr;     // BPM, 0 = no reading
    uint8_t  spo2;   // %, 0 = no reading
    uint16_t steps;  // step total, clamped to 65535
};

// Size on the wire and in flash. Hardcoded rather than sizeof(): the compiler
// may add padding, which would shift the whole ring file.
constexpr size_t MEASUREMENT_SIZE = 8;

// A history packet = [type][count][seqLo][seqHi] then the measurements.
// The sequence number comes back untouched in the ACK: without it, a late ACK
// would be taken for the current packet's and would drop measurements the app
// never received.
constexpr size_t  HISTORY_HEADER_SIZE = 4;
// 0x11 and not 0x01: an app older than the sequence number would read the
// measurements two bytes too early and ACK shifted data without noticing.
// With a type it does not know, it rejects the packet and we see it.
constexpr uint8_t HISTORY_TYPE_DATA   = 0x11;  // measurements left
constexpr uint8_t HISTORY_TYPE_END    = 0xFF;  // backlog empty, the app can go live

// Commands received on the SYNC_CTRL characteristic.
// START and STOP are 1 byte, ACK is 3: [0x02][seqLo][seqHi].
constexpr uint8_t SYNC_CMD_START = 0x01;
constexpr uint8_t SYNC_CMD_ACK   = 0x02;
constexpr uint8_t SYNC_CMD_STOP  = 0x03;

// Below this threshold, `ts` is not an epoch but the bracelet uptime in
// seconds: the measurement was taken before the app gave the time. A real epoch
// is always above (Sept. 2020), an uptime never reaches it. The Android app
// applies the same rule (BraceletMeasurementCodec.TS_EPOCH_MIN).
constexpr uint32_t TS_EPOCH_MIN = 1600000000u;

// The MAX30102 driver returns -1 when it has no valid reading (no finger, too
// much noise). Stored in a uint8_t that -1 becomes 255: a value the protocol
// never planned for and the backend rejects. So any implausible reading is
// brought back to 0, the contract's "no reading".
// `maxPlausible`: 250 BPM and 100 % SpO2, above that it is noise.
uint8_t sanitizeReading(int32_t raw, uint8_t maxPlausible);

constexpr uint8_t MAX_PLAUSIBLE_HR   = 250;  // above this the sensor is raving
constexpr uint8_t MAX_PLAUSIBLE_SPO2 = 100;  // a percentage, by definition

// Explicit little-endian, byte by byte: firmware and Android must read the same
// thing whatever the architecture. `out` must be MEASUREMENT_SIZE long.
void encodeMeasurement(const Measurement& m, uint8_t* out);
Measurement decodeMeasurement(const uint8_t* in);

// Writes [0x11][count][seqLo][seqHi] + count measurements into `out`.
// Returns the number of bytes written.
size_t buildHistoryPacket(const Measurement* items, uint8_t count, uint16_t seq, uint8_t* out);

// Writes [0xFF][0][0][0]: "nothing left in storage". Returns 4.
// This packet is never ACKed, so its sequence is 0.
size_t buildHistoryEndPacket(uint8_t* out);

#endif // MEASUREMENT_H
